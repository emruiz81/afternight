// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngineGmicAdapter - opening a G'MIC session, running commands through it, and the
// process-wide counters and lock every session shares.
//
// This is the one translation unit that calls into the library. A session is opened by checking the
// options, taking the engine context's lease and, once per process, probing the library; the probe
// first refuses a bridge older than this repository's changes to it and then runs a command that
// does nothing. Every command then runs under one process-wide lock, on a thread with the stack the
// interpreter needs, while the calling thread watches the deadline and the progress field. A call
// over an image hands the bridge an array of result slots with zeroed room behind it, asks the bridge
// to refuse a result it cannot fit, checks afterwards that nothing was written past the slots it
// reported, and describes the one image that came back as a frame whose buffer says whether it is the
// caller's memory or the library's.
//
// When no library is compiled in, opening a session is refused with `LibraryNotCompiledIn` before
// the options or the engine context are looked at, and nothing is recorded as a load failure; the
// handle's forwarding functions below are the same in both configurations.

#include "GmicSession.h"

#include "GmicCallThread.h"

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ExceptionBoundary.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/CfaPattern.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/RawFrame.h>
#include <AfterNightEngine/Image/RawSampleFormat.h>
#include <AfterNightEngine/Image/SampleBuffer.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>

namespace an
{
namespace
{

/// How many sessions this process has open.
///
/// A count rather than a seat: the library keeps nothing between calls, so a second session is
/// ordinary. What the sessions share is the lock below.
std::atomic<std::uint64_t> gOpenSessions{0u};

/// How many calls are in progress right now: at most one inside the library, and the rest waiting
/// for `gLibraryMutex`.
std::atomic<std::uint64_t> gCallsInFlight{0u};

/// Whether the library probe has run in this process, successfully or not.
std::atomic<bool> gProbed{false};

/// Why the last refusal after the library was asked to answer happened, for the capability report.
std::atomic<ErrorCode> gLastFailureCode{ErrorCode::Ok};
std::atomic<GmicAdapterReason> gLastFailureReason{GmicAdapterReason::None};

/// Serialises every call into the library, across every session in this process.
///
/// The C bridge builds an interpreter per call and is not written to be entered from two threads at
/// once; CImg and the interpreter carry process-wide state while a command runs. This is the scope
/// that state is confined to, and it is a cost stated rather than a race discovered later.
std::mutex gLibraryMutex;

/// Serialises the probe, so two threads opening their first session do not both run it.
std::mutex gProbeMutex;

} // namespace

Status gmicAdapterFailure(ErrorCode code, GmicAdapterReason reason, const char* operation, std::string message)
{
    return Status::failure(code,
                           StatusDetail{.message = std::move(message),
                                        .operation = operation,
                                        .domain = ErrorDomain::Interop,
                                        .context = {StatusContextField::ofText(ContextKey::Library, "gmic"),
                                                    StatusContextField::ofCount(ContextKey::AdapterReason,
                                                                                static_cast<std::uint64_t>(reason))}});
}

void recordGmicStartupFailure(ErrorCode code, GmicAdapterReason reason) noexcept
{
    gLastFailureCode.store(code, std::memory_order_relaxed);
    gLastFailureReason.store(reason, std::memory_order_relaxed);
}

GmicAdapterCapabilities currentGmicAdapterCapabilities() noexcept
{
    GmicAdapterCapabilities capabilities;
    capabilities.libraryCompiledIn = kGmicLibraryCompiledIn;
    capabilities.probed = gProbed.load(std::memory_order_acquire);
    capabilities.openSessions = gOpenSessions.load(std::memory_order_relaxed);
    capabilities.callsInFlight = gCallsInFlight.load(std::memory_order_relaxed);
    capabilities.loadFailure = gLastFailureCode.load(std::memory_order_relaxed);
    capabilities.loadReason = gLastFailureReason.load(std::memory_order_relaxed);
    if (!kGmicLibraryCompiledIn)
    {
        capabilities.state = GmicAdapterState::Absent;
        return capabilities;
    }
    if (capabilities.openSessions != 0u)
    {
        capabilities.state = GmicAdapterState::Loaded;
        return capabilities;
    }
    capabilities.state =
        capabilities.loadFailure == ErrorCode::Ok ? GmicAdapterState::Available : GmicAdapterState::FailedToLoad;
    return capabilities;
}

GmicSessionState::GmicSessionState(EngineContextLease lease, std::string customCommands, bool useStandardLibrary,
                                   GmicCancellationPolicy cancellation) noexcept
    : m_lease(std::move(lease)), m_customCommands(std::move(customCommands)), m_useStandardLibrary(useStandardLibrary),
      m_cancellation(cancellation)
{
}

GmicSessionState::~GmicSessionState()
{
    if (m_lease.valid())
    {
        gOpenSessions.fetch_sub(1u, std::memory_order_release);
    }
}

GmicCallCounters GmicSessionState::counters() const noexcept
{
    return GmicCallCounters{.calls = m_calls.load(std::memory_order_relaxed),
                            .sharedBytes = m_sharedBytes.load(std::memory_order_relaxed),
                            .copiedIntoLibraryBytes = m_copiedIntoLibraryBytes.load(std::memory_order_relaxed),
                            .libraryResultBytes = m_libraryResultBytes.load(std::memory_order_relaxed),
                            .peakLibraryResultBytes = m_peakLibraryResultBytes.load(std::memory_order_relaxed),
                            .bridgeRefusals = m_bridgeRefusals.load(std::memory_order_relaxed),
                            .cancellations = m_cancellations.load(std::memory_order_relaxed),
                            .commandFailures = m_commandFailures.load(std::memory_order_relaxed)};
}

namespace
{

/// Whether the options a session was opened with are ones this adapter accepts.
///
/// Before anything is asked of the library, because a configuration mistake costs nothing to report
/// and the caller's repair is the same whatever the library is doing.
///
/// @param options what the caller stated.
/// @param operation the adapter operation, for the failure.
/// @return success, or the refusal.
[[nodiscard]] Status checkRuntimeOptions(const GmicRuntimeOptions& options, const char* operation)
{
    if (!isGmicCancellationPolicy(options.cancellation))
    {
        return gmicAdapterFailure(ErrorCode::InvalidArgument, GmicAdapterReason::OptionsRejected, operation,
                                  "the cancellation policy is not one of the two");
    }
    if (options.customCommands.find('\0') != std::string_view::npos)
    {
        return gmicAdapterFailure(ErrorCode::InvalidArgument, GmicAdapterReason::OptionsRejected, operation,
                                  "the custom command block contains a null; the library reads it as a C "
                                  "string and would take only the part before it");
    }
    return Status::success();
}

/// Whether what a call states about its result is something a frame could be built from.
///
/// The membership and range rules only. Whether the stated schema agrees with the channel count the
/// command produced is a question about the result, and it is asked after the command has run.
[[nodiscard]] Status checkResultIntent(const GmicResultIntent& intent, const char* operation)
{
    if (!isChannelSchema(intent.channelSchema) || !isColorSpace(intent.colorSpace) ||
        !isNumericDomain(intent.numericDomain) || !isCoordinateOrigin(intent.coordinateOrigin))
    {
        return gmicAdapterFailure(ErrorCode::InvalidArgument, GmicAdapterReason::OptionsRejected, operation,
                                  "a value in the result intent is outside its own enumeration");
    }
    if (!(intent.pixelAspectRatio > 0.0) || intent.pixelAspectRatio > 1.0e300)
    {
        return gmicAdapterFailure(ErrorCode::InvalidArgument, GmicAdapterReason::OptionsRejected, operation,
                                  "the pixel aspect ratio is not a positive finite number");
    }
    return Status::success();
}

} // namespace

#if defined(AFTERNIGHT_HAS_GMIC)

namespace
{

// The abort flag the library polls is a plain `bool*` in its own interface, and this adapter writes
// it from another thread while the library reads it. An `std::atomic<bool>` is what makes that a
// defined write rather than a race that happens to work, and these are the properties that let one
// be handed over through the pointer the interface declares. A platform where they do not hold is
// one this adapter has to be told about rather than one it should discover.
static_assert(sizeof(std::atomic<bool>) == sizeof(bool), "the abort flag is handed over as a bool*");
static_assert(alignof(std::atomic<bool>) == alignof(bool), "the abort flag is handed over as a bool*");
static_assert(std::atomic<bool>::is_always_lock_free, "the library polls the abort flag without a lock");

/// How many result images one call reserves room for.
///
/// The library's C bridge writes one structure per image a command produced into the array it was
/// given, which is sized for the images it was *handed*. A caller that passes a one-element array -
/// which is the obvious way to call it - has a heap
/// overflow the moment a command splits an image. This adapter's contract is one image out, so the
/// room reserved here is not a capacity the caller can use: it is where an overrun lands, so that it
/// lands in memory this adapter owns and can report.
constexpr unsigned int kResultSlotCapacity = 64u;

/// How many bytes sit after the reserved slots.
///
/// The same bridge copies an image's name back with `strcpy` and no bound at all, past the 256-byte
/// field its own structure declares. This repository's build of it refuses such a name before copying
/// anything when asked to, and every call here asks, so no write should reach this room. It stays as
/// the second line: a bridge that ignored the request would write here, and the check after every
/// call would refuse the result rather than trust it. It is finite, and a name longer than every slot
/// and this room together would run past it - which is why the refusal is the bridge's and not this.
constexpr std::size_t kNameOverflowSlackBytes = 4096u;

/// The array one call hands to the library, with the room an overrun needs behind it.
///
/// One object rather than two allocations, because containment is the point: the slots and the slack
/// have to be adjacent for a write past the last slot to land in the slack.
struct CallSlots
{
    /// What the library reads and writes. Slot zero is the image this adapter handed it.
    gmic_interface_image images[kResultSlotCapacity];

    /// Where an overrun lands. Zero before the call, and checked afterwards.
    unsigned char guard[kNameOverflowSlackBytes];
};

/// How many bytes the error buffer holds.
///
/// The bridge copies at most 255 characters and a terminator into it, so this is four times what it
/// can need. Sizing it from the library's own bound rather than from a habit is the point.
constexpr std::size_t kErrorBufferBytes = 1024u;

/// How often the watch looks at the library's progress field while a command runs.
constexpr auto kProgressInterval = std::chrono::milliseconds(20);

/// What the library writes into its progress field when it is not reporting one.
constexpr float kProgressUnknown = -1.0f;

/// Sets the abort flag when the operation is stopped, without a thread and without a poll.
struct AbortOnStop
{
    /// The flag the library reads. Never null while this callback is registered.
    std::atomic<bool>* flag;

    /// Sets it. Runs on whichever thread requested the stop.
    void operator()() const noexcept
    {
        flag->store(true, std::memory_order_relaxed);
    }
};

/// Waits for a call running on its own thread, and watches it for the two things the library cannot
/// watch for itself.
///
/// The library polls an abort flag, so a stop request needs no watch at all - a stop callback sets
/// the flag from the thread that requested it. A *deadline* is different: nothing in the library
/// measures time, so somebody has to wait until it and set the same flag. So does progress, which
/// the library writes into a float and never calls back about.
///
/// That somebody is the thread that made the call. The command runs on a thread of its own because of
/// the stack it needs (see "GmicCallThread.h"), so the calling thread would otherwise only be blocked;
/// it does the watching instead, and a call under no deadline and with no progress sink simply waits.
/// Progress is therefore reported on the thread that asked for the operation.
///
/// @param execution the operation whose deadline and progress sink decide.
/// @param call the running command. It must have started.
/// @param abort the flag the library polls.
/// @param progress the library's progress field, or null when no sink is listening.
void waitWatchingCall(const ExecutionContext& execution, GmicCallThread& call, std::atomic<bool>& abort,
                      const float* progress)
{
    const std::optional<std::chrono::steady_clock::time_point> deadline = execution.deadline();
    const bool watchProgress = progress != nullptr && execution.progressSink() != nullptr;
    if (!deadline.has_value() && !watchProgress)
    {
        call.wait();
        return;
    }
    std::uint64_t lastReported = 0u;
    for (;;)
    {
        auto until = deadline.value_or(std::chrono::steady_clock::time_point::max());
        if (watchProgress)
        {
            until = (std::min)(until, std::chrono::steady_clock::now() + kProgressInterval);
        }
        if (call.waitUntil(until))
        {
            return;
        }
        if (deadline.has_value() && std::chrono::steady_clock::now() >= *deadline)
        {
            abort.store(true, std::memory_order_relaxed);
            call.wait();
            return;
        }
        if (watchProgress)
        {
            // Advisory: the library writes this field only for the commands that compute a
            // percentage, and leaves it at its "unknown" value otherwise. A report that would move
            // backwards is dropped by the engine, so a filter that restarts its own count is not this
            // adapter's problem to smooth.
            const float reported = *progress;
            if (reported >= 0.0f)
            {
                const auto completed = static_cast<std::uint64_t>(reported);
                if (completed != lastReported)
                {
                    lastReported = completed;
                    execution.reportProgress(completed, 100u, "G'MIC command");
                }
            }
        }
    }
}

/// Releases every buffer the library allocated for one call.
///
/// A guard rather than a call at each exit, because `run` has many ways out after the library has
/// returned and every one of them has to release what it left behind. Slot zero is
/// skipped when it still points at the caller's own samples, which is what an in-place command
/// leaves there.
struct SlotRelease
{
    /// The slots to release. Never null.
    CallSlots* slots;

    /// How many the library says it wrote, clamped to what was reserved.
    unsigned int count;

    /// The caller's samples, which this adapter does not own.
    const void* source;

    ~SlotRelease()
    {
        for (unsigned int index = 0u; index < count; ++index)
        {
            void* data = slots->images[index].data;
            if (data != nullptr && data != source)
            {
                (void)gmic_delete_external(static_cast<float*>(data));
            }
            slots->images[index].data = nullptr;
        }
    }
};

/// Whether the library wrote past the slots it was allowed to write.
///
/// Slot zero is this adapter's own input and slots up to the reported count are the library's own
/// output; everything after that was zero before the call and must still be. A name written past its
/// field's end lands in the next slot, and a count past the reserved capacity lands in the slack, so
/// one scan answers both.
///
/// @param slots the array the library was given.
/// @param written how many slots the library reported, clamped to the reserved capacity.
/// @return true when every byte after the written slots is still zero.
[[nodiscard]] bool tailIsUntouched(const CallSlots& slots, unsigned int written) noexcept
{
    const auto* bytes = reinterpret_cast<const unsigned char*>(&slots);
    const std::size_t from = (std::max)(written, 1u) * sizeof(gmic_interface_image);
    for (std::size_t index = from; index < sizeof(CallSlots); ++index)
    {
        if (bytes[index] != 0u)
        {
            return false;
        }
    }
    return true;
}

/// Fills the option structure every call in this session shares.
void fillOptions(gmic_interface_options& options, const std::string& customCommands, bool useStandardLibrary,
                 char* errorBuffer) noexcept
{
    std::memset(&options, 0, sizeof(options));
    options.custom_commands = customCommands.empty() ? nullptr : customCommands.c_str();
    options.ignore_stdlib = !useStandardLibrary;
    options.interleave_output = false;
    // Float planes in both directions, always. The byte representation the interface also names is a
    // conversion the library would perform, and a conversion in this engine is five named decisions
    // rather than a flag on somebody else's structure.
    options.output_format = E_FORMAT_FLOAT;
    options.error_message_buffer = errorBuffer;
    // This private extension to the vendored bridge makes the reserved array a real capacity. When
    // a command produces more images, the bridge reports the count but exports none, so its own
    // image list releases every result instead of writing and leaking buffers beyond this array.
    options.output_capacity = kResultSlotCapacity;
    // The second private extension bounds the one write the first leaves unbounded: a result name
    // longer than the field it would be copied into makes the bridge return GMIC_CALL_NAME_UNFIT and
    // copy back no image, rather than write the name past the field and into whatever follows it.
    options.refuse_unfit_names = true;
}

/// The refusal for a call whose thread the platform would not start.
///
/// Nothing was asked of the library, so nothing about the library is recorded either.
///
/// @param operation the adapter operation, for the failure.
/// @return the failure, carrying `GmicAdapterReason::CallThreadNotStarted`.
[[nodiscard]] Status callThreadRefusal(const char* operation)
{
    return gmicAdapterFailure(ErrorCode::AllocationFailed, GmicAdapterReason::CallThreadNotStarted, operation,
                              "the platform would not start the thread a call into the G'MIC library runs on, "
                              "with the " +
                                  std::to_string(kGmicCallStackBytes / (std::size_t{1024u} * 1024u)) +
                                  " MiB of stack that library needs; nothing was asked of the library");
}

/// Adds this adapter's typed reason to an exception translated at the library boundary.
///
/// The cause remains the external boundary's answer, including `AllocationFailed` for
/// `std::bad_alloc`. If describing it needs the allocation that just failed, the translated cause
/// is returned by itself rather than allowing another exception to escape the adapter.
///
/// @param cause the exception already translated into the engine's vocabulary.
/// @param reason which adapter operation the exception prevented.
/// @param operation the adapter operation, for the failure.
/// @param message the stable sentence for the adapter layer.
/// @return the typed adapter failure, or its cause when no detail can be allocated.
[[nodiscard]] Status gmicBoundaryFailure(const Status& cause, GmicAdapterReason reason, const char* operation,
                                         const char* message) noexcept
{
    try
    {
        return Status::failure(
            cause.code(),
            StatusDetail{.message = message,
                         .operation = operation,
                         .domain = ErrorDomain::Interop,
                         .context = {StatusContextField::ofText(ContextKey::Library, "gmic"),
                                     StatusContextField::ofCount(ContextKey::AdapterReason,
                                                                 static_cast<std::uint64_t>(reason))}},
            cause);
    }
    catch (...)
    {
        return cause;
    }
}

/// Asks the library to run a trivial command, once per process, and remembers the answer.
///
/// The difference between a library that is linked and a library that works. On a delay-loaded
/// Windows build the second is not implied by the first, and on any build the standard library has to
/// decompress before a named command exists. It costs one interpreter and one parse, once.
///
/// @return success, or the failure to report as a refused session.
[[nodiscard]] Status probeLibrary()
{
    const std::lock_guard<std::mutex> probeGuard(gProbeMutex);
    if (gProbed.load(std::memory_order_acquire))
    {
        const ErrorCode code = gLastFailureCode.load(std::memory_order_relaxed);
        if (code == ErrorCode::Ok)
        {
            return Status::success();
        }
        return gmicAdapterFailure(code, gLastFailureReason.load(std::memory_order_relaxed),
                                  "GmicRuntimeAdapter::create",
                                  "the G'MIC library did not answer this process's probe");
    }

    // Two of the protections every call relies on are this repository's changes to the bridge, carried
    // in option fields an unchanged bridge would never read. Importing this function is what makes a
    // bridge built before them fail to load rather than accept the options and ignore them; the level
    // is what refuses one built before a later change.
    if (const unsigned int level = gmic_afternight_patch_level(); level < GMIC_AFTERNIGHT_PATCH_LEVEL)
    {
        gProbed.store(true, std::memory_order_release);
        recordGmicStartupFailure(ErrorCode::ExternalFailure, GmicAdapterReason::LibraryProbeFailed);
        return gmicAdapterFailure(
            ErrorCode::ExternalFailure, GmicAdapterReason::LibraryProbeFailed, "GmicRuntimeAdapter::create",
            "the G'MIC bridge carries this repository's changes to level " + std::to_string(level) +
                " and this adapter needs level " + std::to_string(GMIC_AFTERNIGHT_PATCH_LEVEL));
    }

    char errorBuffer[kErrorBufferBytes] = {};
    gmic_interface_options options;
    const std::string none;
    fillOptions(options, none, true, errorBuffer);

    int result = 0;
    bool started = false;
    Status invocation;
    {
        const std::lock_guard<std::mutex> libraryGuard(gLibraryMutex);
        gCallsInFlight.fetch_add(1u, std::memory_order_relaxed);
        // A command with no image and no effect: it sets the verbosity the interpreter already has.
        // The library has no command that does nothing at all, and this is the cheapest one that
        // proves the interpreter was built and the command file parsed into it. It runs on a thread
        // of its own for the reason every other call does.
        static constexpr ExternalBoundary kProbeBoundary{
            .library = "gmic", .operation = "GmicRuntimeAdapter::create", .domain = ErrorDomain::Interop};
        auto probe = [&result, &options, &invocation]() noexcept {
            invocation =
                callAcrossBoundary(kProbeBoundary, [&] { result = gmic_call("v 0", nullptr, nullptr, &options); });
        };
        {
            const GmicCallThread call(probe);
            started = call.started();
        }
        gCallsInFlight.fetch_sub(1u, std::memory_order_relaxed);
    }
    if (!started)
    {
        // Not a probe failure: the library was never asked, so the next session asks again.
        return callThreadRefusal("GmicRuntimeAdapter::create");
    }
    gProbed.store(true, std::memory_order_release);
    if (invocation.failed())
    {
        const Status failure =
            gmicBoundaryFailure(invocation, GmicAdapterReason::LibraryProbeFailed, "GmicRuntimeAdapter::create",
                                "the G'MIC library threw while its probe was running");
        recordGmicStartupFailure(failure.code(), GmicAdapterReason::LibraryProbeFailed);
        return failure;
    }
    if (result == 0)
    {
        recordGmicStartupFailure(ErrorCode::Ok, GmicAdapterReason::None);
        return Status::success();
    }
    std::string message = "the G'MIC library refused a trivial command";
    if (errorBuffer[0] != '\0')
    {
        message += ": ";
        message += static_cast<const char*>(errorBuffer);
    }
    const Status failure = gmicAdapterFailure(ErrorCode::ExternalFailure, GmicAdapterReason::LibraryProbeFailed,
                                              "GmicRuntimeAdapter::create", std::move(message));
    recordGmicStartupFailure(failure.code(), GmicAdapterReason::LibraryProbeFailed);
    return failure;
}

} // namespace

Result<std::shared_ptr<GmicSessionState>> GmicSessionState::open(const EngineContext& engine,
                                                                 const GmicRuntimeOptions& options)
{
    const Status stated = checkRuntimeOptions(options, "GmicRuntimeAdapter::create");
    if (stated.failed())
    {
        return Result<std::shared_ptr<GmicSessionState>>{stated};
    }

    // The lease before the library, so that a context which cannot admit a session is reported before
    // an interpreter has been built that would then have to be torn down again.
    Result<EngineContextLease> lease = engine.acquireLease(0u);
    if (!lease.hasValue())
    {
        return Result<std::shared_ptr<GmicSessionState>>{
            Status::failure(ErrorCode::NotInitialized,
                            StatusDetail{.message = "the engine context cannot hold a G'MIC session open",
                                         .operation = "GmicRuntimeAdapter::create",
                                         .domain = ErrorDomain::Interop},
                            lease.status())};
    }

    const Status probed = probeLibrary();
    if (probed.failed())
    {
        return Result<std::shared_ptr<GmicSessionState>>{probed};
    }

    static constexpr ExternalBoundary kOpenBoundary{
        .library = "gmic", .operation = "GmicRuntimeAdapter::create", .domain = ErrorDomain::Interop};
    Result<std::shared_ptr<GmicSessionState>> state = evaluateAcrossBoundary(kOpenBoundary, [&lease, &options]() {
        return std::shared_ptr<GmicSessionState>(new GmicSessionState(
            std::move(*lease), std::string(options.customCommands), options.useStandardLibrary, options.cancellation));
    });
    if (!state.hasValue())
    {
        return state;
    }
    gOpenSessions.fetch_add(1u, std::memory_order_release);
    return state;
}

template <typename BodyT>
Status GmicSessionState::withLibrary(const ExecutionContext& execution, const char* operation, std::string& errorText,
                                     BodyT&& body)
{
    // The boundary check, before the lock: an operation the caller has already stopped does not need
    // an interpreter, and taking the lock to find out would serialise against every other call first.
    const Status boundary = execution.checkCancellation();
    if (boundary.failed())
    {
        m_cancellations.fetch_add(1u, std::memory_order_relaxed);
        return gmicAdapterFailure(boundary.code(), GmicAdapterReason::Interrupted, operation,
                                  "the caller had already stopped this operation before the library was "
                                  "entered");
    }

    m_callsInFlight.fetch_add(1u, std::memory_order_relaxed);
    gCallsInFlight.fetch_add(1u, std::memory_order_relaxed);
    struct InFlightGuard
    {
        std::atomic<std::uint64_t>* sessionCounter;

        ~InFlightGuard()
        {
            gCallsInFlight.fetch_sub(1u, std::memory_order_relaxed);
            sessionCounter->fetch_sub(1u, std::memory_order_relaxed);
        }
    } inFlight{&m_callsInFlight};

    const std::lock_guard<std::mutex> libraryGuard(gLibraryMutex);

    char errorBuffer[kErrorBufferBytes] = {};
    gmic_interface_options options;
    fillOptions(options, m_customCommands, m_useStandardLibrary, errorBuffer);

    std::atomic<bool> aborted{false};
    float progress = kProgressUnknown;
    std::optional<std::stop_callback<AbortOnStop>> stopCallback;
    const bool armed = m_cancellation == GmicCancellationPolicy::DuringCall;
    if (armed)
    {
        options.p_is_abort = reinterpret_cast<bool*>(&aborted);
        if (execution.progressSink() != nullptr)
        {
            options.p_progress = &progress;
        }
        // Registering the callback fires it immediately when the stop has already been requested,
        // which is what makes the flag correct before the library is entered as well as during.
        stopCallback.emplace(execution.stopToken(), AbortOnStop{&aborted});
    }

    int result = 0;
    Status invocation;
    {
        // The command runs on a thread with the stack the library needs rather than on the caller's,
        // and the caller waits for it, watching its deadline and its progress while it does.
        const ExternalBoundary libraryBoundary{
            .library = "gmic", .operation = operation, .domain = ErrorDomain::Interop};
        auto invoke = [&result, &body, &options, &invocation, &libraryBoundary]() noexcept {
            invocation = callAcrossBoundary(libraryBoundary,
                                            [&] { result = static_cast<int>(std::forward<BodyT>(body)(options)); });
        };
        GmicCallThread call(invoke);
        if (!call.started())
        {
            return callThreadRefusal(operation);
        }
        waitWatchingCall(execution, call, aborted, armed ? &progress : nullptr);
    }
    m_calls.fetch_add(1u, std::memory_order_relaxed);

    // A command the library abandoned because the flag was set still returns success, so what
    // decides is this adapter's own view of the operation rather than the library's result code. The
    // engine's own code is kept - a deadline that ran out is told apart from a stop that was
    // requested, because a caller does something different about each.
    const Status cancellation = execution.checkCancellation();
    if (aborted.load(std::memory_order_relaxed) || cancellation.failed())
    {
        m_cancellations.fetch_add(1u, std::memory_order_relaxed);
        return gmicAdapterFailure(cancellation.failed() ? cancellation.code() : ErrorCode::Cancelled,
                                  GmicAdapterReason::Interrupted, operation,
                                  "the caller stopped this operation and the command gave up at its next "
                                  "abort check");
    }
    if (invocation.failed())
    {
        m_commandFailures.fetch_add(1u, std::memory_order_relaxed);
        return gmicBoundaryFailure(invocation, GmicAdapterReason::CommandFailed, operation,
                                   "the G'MIC library threw before the command returned");
    }
    if (result == GMIC_CALL_NAME_UNFIT)
    {
        // The command ran; what the bridge refused was copying its result back. Nothing was written
        // into the caller's slots, and the bridge released every image it held.
        return gmicAdapterFailure(ErrorCode::ExternalFailure, GmicAdapterReason::ResultNameOverflowed, operation,
                                  "the command named an image longer than the field the library's own structure "
                                  "declares; the library was asked to refuse rather than copy it, and returned no "
                                  "image");
    }
    if (result != 0)
    {
        m_commandFailures.fetch_add(1u, std::memory_order_relaxed);
        errorText.assign(static_cast<const char*>(errorBuffer));
        std::string message = "the G'MIC command failed";
        if (!errorText.empty())
        {
            message += ": ";
            message += errorText;
        }
        return gmicAdapterFailure(ErrorCode::ExternalFailure, GmicAdapterReason::CommandFailed, operation,
                                  std::move(message));
    }
    return Status::success();
}

Status GmicSessionState::execute(const ExecutionContext& execution, std::string_view command)
{
    if (command.empty())
    {
        return gmicAdapterFailure(ErrorCode::InvalidArgument, GmicAdapterReason::CommandEmpty,
                                  "GmicRuntimeAdapter::execute",
                                  "the command is empty; the library would build an interpreter to run nothing");
    }
    const std::string text(command);
    std::string errorText;
    return withLibrary(execution, "GmicRuntimeAdapter::execute", errorText, [&text](gmic_interface_options& options) {
        return gmic_call(text.c_str(), nullptr, nullptr, &options);
    });
}

Result<RawFrame> GmicSessionState::run(const ExecutionContext& execution, const GmicImageCall& call)
{
    static constexpr const char* kOperation = "GmicRuntimeAdapter::run";
    if (call.command.empty())
    {
        return Result<RawFrame>{
            gmicAdapterFailure(ErrorCode::InvalidArgument, GmicAdapterReason::CommandEmpty, kOperation,
                               "the command is empty; the library would build an interpreter to run nothing")};
    }
    if (!isGmicSourceSharing(call.sharing))
    {
        return Result<RawFrame>{gmicAdapterFailure(ErrorCode::InvalidArgument, GmicAdapterReason::OptionsRejected,
                                                   kOperation, "the sharing decision is not one of the two")};
    }
    const Status intent = checkResultIntent(call.intent, kOperation);
    if (intent.failed())
    {
        return Result<RawFrame>{intent};
    }

    // What the call requires of the borrow, before the memory models: sharing is how the library
    // works in place, so it needs samples it may write through. A description that says otherwise is
    // a call this adapter will not make rather than a bridge its two models cannot express.
    if (call.sharing == GmicSourceSharing::Share && call.source.readOnly)
    {
        m_bridgeRefusals.fetch_add(1u, std::memory_order_relaxed);
        return Result<RawFrame>{gmicAdapterFailure(
            ErrorCode::InvalidArgument, GmicAdapterReason::SourceNotShareable, kOperation,
            "the source is described read-only; a shared source is what the library works in place on")};
    }
    const BridgeRefusal refusal = checkGmicImageDescription(call.source);
    const std::optional<std::uint64_t> sourceBytes = gmicByteCount(call.source);
    if (refusal != BridgeRefusal::None || !sourceBytes.has_value())
    {
        m_bridgeRefusals.fetch_add(1u, std::memory_order_relaxed);
        return Result<RawFrame>{gmicAdapterFailure(
            ErrorCode::InvalidArgument, GmicAdapterReason::BufferBridgeRefused, kOperation,
            std::string("the source cannot be handed to the library: ") + bridgeRefusalName(refusal))};
    }

    const std::string command(call.command);
    const std::string name(
        call.imageName.substr(0, (std::min<std::size_t>)(call.imageName.size(), kGmicMaxImageNameLength)));
    auto slots = std::make_unique<CallSlots>();
    std::memset(slots.get(), 0, sizeof(CallSlots));
    gmic_interface_image& input = slots->images[0];
    input.data = call.source.data;
    input.width = call.source.width;
    input.height = call.source.height;
    input.depth = call.source.depth;
    input.spectrum = call.source.spectrum;
    // The description's own fields, not this adapter's expectations of them: the check above already
    // refused an interleaved or non-float description, so copying them across is exact rather than
    // hopeful.
    input.is_interleaved = call.source.interleaved;
    input.format = static_cast<EPixelFormat>(static_cast<int>(call.source.format));
    std::memcpy(input.name, name.c_str(), name.size() + 1u);

    unsigned int count = 1u;
    const bool share = call.sharing == GmicSourceSharing::Share;
    std::string errorText;
    int libraryResult = 0;
    const Status status = withLibrary(execution, kOperation, errorText, [&](gmic_interface_options& options) {
        // Sharing and copying are the same call with one flag reversed, and the flag is the whole
        // difference: told not to process in place, the library copies the samples into its own
        // image and leaves the caller's alone.
        options.no_inplace_processing = !share;
        libraryResult = gmic_call(command.c_str(), &count, slots->images, &options);
        return libraryResult;
    });

    // What decides whether there are buffers to release is the library's own result code, not this
    // adapter's: the bridge fills the output slots exactly when the command succeeded, and a command
    // the caller stopped succeeds - it returns its images and this adapter reports a cancellation
    // over them. Reading the adapter's status here instead would leak every stopped call's result.
    const unsigned int written = libraryResult == 0 ? (std::min)(count, kResultSlotCapacity) : 0u;
    SlotRelease release{slots.get(), written, call.source.data};
    if (status.failed())
    {
        return Result<RawFrame>{status};
    }

    if (!tailIsUntouched(*slots, written))
    {
        return Result<RawFrame>{gmicAdapterFailure(
            ErrorCode::ExternalFailure, GmicAdapterReason::ResultNameOverflowed, kOperation,
            "the library wrote an image name past the end of the field its own structure declares; the "
            "write landed in the room this adapter reserves for it and the result is not trusted")};
    }
    if (count == 0u)
    {
        return Result<RawFrame>{gmicAdapterFailure(ErrorCode::ExternalFailure, GmicAdapterReason::ResultMissing,
                                                   kOperation,
                                                   "the command left no image; one image in and one image out is "
                                                   "what this adapter runs")};
    }
    if (count != 1u)
    {
        return Result<RawFrame>{
            gmicAdapterFailure(ErrorCode::ExternalFailure, GmicAdapterReason::ResultImageCountUnexpected, kOperation,
                               "the command left " + std::to_string(count) +
                                   " images; one image in and one image out is what this adapter runs")};
    }

    const gmic_interface_image& produced = slots->images[0];
    GmicImageDescription result{};
    result.data = produced.data;
    result.width = produced.width;
    result.height = produced.height;
    result.depth = produced.depth;
    result.spectrum = produced.spectrum;
    result.interleaved = produced.is_interleaved;
    result.format = static_cast<GmicPixelFormat>(static_cast<std::int32_t>(produced.format));
    result.readOnly = false;

    if (result.format != GmicPixelFormat::Float32 || result.interleaved)
    {
        return Result<RawFrame>{
            gmicAdapterFailure(ErrorCode::ExternalFailure, GmicAdapterReason::ResultFormatUnexpected, kOperation,
                               std::string("the command produced a ") + gmicPixelFormatName(result.format) +
                                   (result.interleaved ? " interleaved" : " planar") +
                                   " image; float planes are what this adapter asked for")};
    }
    const EngineLimits limits = execution.limits();
    const std::optional<ImageLayout> layout = gmicResultLayout(result, limits);
    const std::optional<std::uint64_t> resultBytes = gmicByteCount(result);
    if (!layout.has_value() || !resultBytes.has_value())
    {
        return Result<RawFrame>{gmicAdapterFailure(
            ErrorCode::ExternalFailure, GmicAdapterReason::ResultGeometryRejected, kOperation,
            "the command produced an image this engine cannot describe: " + std::to_string(result.width) + "x" +
                std::to_string(result.height) + "x" + std::to_string(result.depth) + " in " +
                std::to_string(result.spectrum) + " planes")};
    }
    const std::optional<std::uint32_t> statedChannels = channelCountOf(call.intent.channelSchema);
    if (statedChannels.has_value() && *statedChannels != result.spectrum)
    {
        return Result<RawFrame>{gmicAdapterFailure(
            ErrorCode::ExternalFailure, GmicAdapterReason::ResultGeometryRejected, kOperation,
            std::string("the command produced ") + std::to_string(result.spectrum) + " planes and the call states " +
                channelSchemaName(call.intent.channelSchema) + ", which has " + std::to_string(*statedChannels))};
    }

    const RawFrameDescriptorRequest request{.extent = layout->extent(),
                                            .sampleFormat =
                                                RawSampleFormat{.sizeBytes = sampleTypeSizeBytes(kGmicSampleType),
                                                                .sampleClass = RawSampleClass::FloatingPoint},
                                            .byteOrder = ByteOrder::Native,
                                            .channelSchema = call.intent.channelSchema,
                                            .colorSpace = call.intent.colorSpace,
                                            .numericDomain = call.intent.numericDomain,
                                            .cfa = CfaDescriptor{},
                                            .coordinateOrigin = call.intent.coordinateOrigin,
                                            .pixelAspectRatio = call.intent.pixelAspectRatio};
    const std::optional<RawFrameDescriptor> descriptor = RawFrameDescriptor::create(request);
    if (!descriptor.has_value())
    {
        return Result<RawFrame>{gmicAdapterFailure(
            ErrorCode::ExternalFailure, GmicAdapterReason::ResultGeometryRejected, kOperation,
            "what the call states about the result does not describe the image the command produced")};
    }

    // Who owns the bytes is decided here rather than assumed, and the two answers are genuinely
    // different memory. A command that worked in place on shared samples leaves the caller's own
    // buffer, which nothing here may free; anything else leaves the library's, which is released
    // through the token the frame holds.
    const bool inPlace = result.data == call.source.data;
    Result<SampleBuffer> buffer =
        inPlace ? SampleBuffer::borrow(std::span<std::byte>(static_cast<std::byte*>(result.data),
                                                            static_cast<std::size_t>(*resultBytes)))
                : Result<SampleBuffer>{Status::failure(ErrorCode::Internal)};
    if (!inPlace)
    {
        // The raw pointer leaves the release guard before the shared pointer is built, so that a
        // control block that could not be allocated releases it exactly once - through the deleter
        // the standard runs on that failure - rather than twice or not at all.
        void* owned = result.data;
        slots->images[0].data = nullptr;
        release.count = 0u;
        static constexpr ExternalBoundary kResultBoundary{
            .library = "gmic", .operation = "GmicRuntimeAdapter::run", .domain = ErrorDomain::Interop};
        Result<std::shared_ptr<void>> lifetime = evaluateAcrossBoundary(kResultBoundary, [owned]() {
            return std::shared_ptr<void>(owned, [](void* released) noexcept {
                if (released != nullptr)
                {
                    (void)gmic_delete_external(static_cast<float*>(released));
                }
            });
        });
        if (!lifetime.hasValue())
        {
            return Result<RawFrame>{lifetime.status()};
        }
        buffer = SampleBuffer::external(SampleBufferOwnership::DecoderOwned, owned, *resultBytes, std::move(*lifetime),
                                        true);
    }
    if (!buffer.hasValue())
    {
        return Result<RawFrame>{buffer.status()};
    }
    Result<RawFrame> frame = RawFrame::create(*descriptor, *layout, std::move(*buffer));
    if (!frame.hasValue())
    {
        return frame;
    }

    if (share)
    {
        m_sharedBytes.fetch_add(*sourceBytes, std::memory_order_relaxed);
    }
    else
    {
        m_copiedIntoLibraryBytes.fetch_add(*sourceBytes, std::memory_order_relaxed);
    }
    if (!inPlace)
    {
        m_libraryResultBytes.fetch_add(*resultBytes, std::memory_order_relaxed);
        std::uint64_t peak = m_peakLibraryResultBytes.load(std::memory_order_relaxed);
        while (peak < *resultBytes &&
               !m_peakLibraryResultBytes.compare_exchange_weak(peak, *resultBytes, std::memory_order_relaxed))
        {
        }
    }
    return frame;
}

#else

Result<std::shared_ptr<GmicSessionState>> GmicSessionState::open(const EngineContext&, const GmicRuntimeOptions&)
{
    // The whole of the absent configuration: a named refusal, no side effect, and nothing held. It is
    // built and tested rather than reasoned about, which is why the disabled build exists. Nothing is
    // recorded as a load failure either - a library that is not in this build was never asked to
    // answer, and the report says `Absent` rather than `FailedToLoad`.
    return Result<std::shared_ptr<GmicSessionState>>{
        gmicAdapterFailure(ErrorCode::Unsupported, GmicAdapterReason::LibraryNotCompiledIn,
                           "GmicRuntimeAdapter::create", "this build of the G'MIC adapter contains no G'MIC library")};
}

Status GmicSessionState::execute(const ExecutionContext&, std::string_view)
{
    return gmicAdapterFailure(ErrorCode::Unsupported, GmicAdapterReason::LibraryNotCompiledIn,
                              "GmicRuntimeAdapter::execute",
                              "this build of the G'MIC adapter contains no G'MIC library");
}

Result<RawFrame> GmicSessionState::run(const ExecutionContext&, const GmicImageCall&)
{
    return Result<RawFrame>{gmicAdapterFailure(ErrorCode::Unsupported, GmicAdapterReason::LibraryNotCompiledIn,
                                               "GmicRuntimeAdapter::run",
                                               "this build of the G'MIC adapter contains no G'MIC library")};
}

#endif

// --- the handle -------------------------------------------------------------------------------
//
// Everything below is G'MIC-free by construction: the handle forwards to the session state, and the
// state is what differs between the two configurations.

GmicRuntimeAdapter::~GmicRuntimeAdapter() = default;

GmicRuntimeAdapter& GmicRuntimeAdapter::operator=(GmicRuntimeAdapter&& other) noexcept
{
    if (this != &other)
    {
        m_state = std::move(other.m_state);
    }
    return *this;
}

Result<GmicRuntimeAdapter> GmicRuntimeAdapter::create(const EngineContext& engine, const GmicRuntimeOptions& options)
{
    Result<std::shared_ptr<GmicSessionState>> state = GmicSessionState::open(engine, options);
    if (!state.hasValue())
    {
        return Result<GmicRuntimeAdapter>{state.status()};
    }
    return Result<GmicRuntimeAdapter>{GmicRuntimeAdapter(std::move(*state))};
}

GmicAdapterCapabilities GmicRuntimeAdapter::capabilities() const noexcept
{
    return currentGmicAdapterCapabilities();
}

std::uint64_t GmicRuntimeAdapter::engineContextId() const noexcept
{
    return m_state != nullptr ? m_state->engineContextId() : 0u;
}

std::uint64_t GmicRuntimeAdapter::callsInFlight() const noexcept
{
    return m_state != nullptr ? m_state->callsInFlight() : 0u;
}

GmicCallCounters GmicRuntimeAdapter::counters() const noexcept
{
    return m_state != nullptr ? m_state->counters() : GmicCallCounters{};
}

Status GmicRuntimeAdapter::execute(const ExecutionContext& execution, std::string_view command)
{
    if (m_state == nullptr)
    {
        return gmicAdapterFailure(ErrorCode::InvalidState, GmicAdapterReason::AdapterShutDown,
                                  "GmicRuntimeAdapter::execute",
                                  "this session holds nothing; it was shut down or moved from");
    }
    // A copy of the reference count for the duration of the call, so a session destroyed on another
    // thread leaves the state alive until this returns.
    const std::shared_ptr<GmicSessionState> state = m_state;
    return state->execute(execution, command);
}

Result<RawFrame> GmicRuntimeAdapter::run(const ExecutionContext& execution, const GmicImageCall& call)
{
    if (m_state == nullptr)
    {
        return Result<RawFrame>{gmicAdapterFailure(ErrorCode::InvalidState, GmicAdapterReason::AdapterShutDown,
                                                   "GmicRuntimeAdapter::run",
                                                   "this session holds nothing; it was shut down or moved from")};
    }
    const std::shared_ptr<GmicSessionState> state = m_state;
    return state->run(execution, call);
}

Status GmicRuntimeAdapter::shutdown()
{
    if (m_state == nullptr)
    {
        return Status::success();
    }
    if (m_state->callsInFlight() != 0u)
    {
        return gmicAdapterFailure(ErrorCode::Busy, GmicAdapterReason::CallInFlight, "GmicRuntimeAdapter::shutdown",
                                  "a call into the library is still running");
    }
    // Dropping the last reference is what releases the lease; a call that is still holding one
    // releases it when it returns instead.
    m_state.reset();
    return Status::success();
}

} // namespace an
