// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A G'MIC session, with its state and its copies said out loud.
///
/// The library's C bridge - an array of `gmic_interface_image` and a `gmic_interface_options` per call -
/// leaves several questions to whoever calls it: whether the library works on the engine's own float
/// samples or on a converted copy, where the abort flag and the error text live, whether
/// `gmic_delete_external` or `free` releases a result, whether the rows are padded, whether the command
/// produced one image or six, and whether the name the library writes back fits the field it writes it
/// into.
///
/// This adapter answers each of them:
///
/// **The library's process-wide state is scoped, and the scope is a lock.** The C bridge builds an
/// interpreter per call and keeps nothing between them, so there is no session to seat - but CImg and
/// G'MIC carry process-wide state while a call runs, and the bridge is not written to be entered from
/// two threads at once. This adapter serialises every call into the library across the whole process,
/// which is a cost stated here rather than a race discovered later. Several sessions may be open at
/// the same time; what they share is the lock.
///
/// **A session holds its engine context open.** It takes an `EngineContextLease`, so a context with a
/// live session refuses to shut down and the caller is told rather than finding out. A library with
/// process-wide state is held to one rule: the adapter provides the functionality and the engine
/// context owns the lifetime.
///
/// **Whether the library gets the engine's samples or a copy of them is the caller's decision.**
/// `GmicSourceSharing` is that decision, and it is the one this adapter exists to make visible. The
/// library works in place on what it is given, so sharing is free and consumes the source; copying
/// leaves the source alone and costs one pass. Both are counted, in bytes, in both directions.
///
/// **Cancellation is real, because the library polls for it.** The interface takes the address of a
/// `bool` it reads while a command runs, so a stop request reaches a running command without this
/// adapter installing anything in it. What that does not give for free is a deadline, which nothing
/// polls - so a call under a deadline is watched, and `GmicCancellationPolicy` states which of the
/// two a session pays for.
///
/// **What comes back is described, not assumed.** The library's own contract lets a command replace
/// the image, change its channel count, produce several images or none, and leave a buffer that
/// either is the engine's memory or is the library's. The result is a `RawFrame` whose
/// `SampleBuffer` says which of those it is, so a caller adopts it, copies it or converts it with the
/// engine's ordinary vocabulary instead of a convention about who frees what.

#pragma once

#include <AfterNightEngineGmicAdapter/Api.h>
#include <AfterNightEngineGmicAdapter/GmicAdapterCapabilities.h>
#include <AfterNightEngineGmicAdapter/GmicImageBridge.h>

#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/CfaPattern.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/RawFrame.h>

#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>

namespace an
{

class EngineContext;
class ExecutionContext;

/// The session this adapter owns, declared here and defined only inside the adapter.
///
/// Named in this header so that a session handle can hold a pointer to one; it is never defined in
/// the published surface. A caller can do nothing with the name, which is the point: what it holds
/// is a lease, a lock discipline and the library's own structures, and none of those may cross into
/// a header a consumer compiles without G'MIC installed.
class GmicSessionState;

/// When a cancellation request reaches a running command.
///
/// Stated once per session rather than per call, because it is a property of how a caller uses this
/// adapter rather than of one call site's patience.
enum class GmicCancellationPolicy : std::uint8_t
{
    /// Cancellation is observed at the boundary: before the command is started, and again when it
    /// returns. A command that has started is not interrupted, so a filter that takes a minute takes
    /// a minute. The library is given no abort flag and no progress field, so a call costs exactly
    /// what it costs, and no progress is reported.
    AtBoundary,

    /// Cancellation is observed at the boundary and while the command runs.
    ///
    /// The stop token is wired to the abort flag the library polls, through a stop callback rather
    /// than a poll of this adapter's own - so a stop request costs nothing until it arrives. A
    /// deadline is different: nothing in the library measures time, so a call whose execution
    /// carries one is watched by the thread that made it - which is waiting for the command anyway -
    /// and that thread sets the same flag when the deadline passes.
    ///
    /// The same watch is what reports progress, on the thread that made the call, because the
    /// library's progress field is a float it writes and nobody calls back about.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    DuringCall
};

/// Whether the library is given the engine's samples or a copy of them.
///
/// The decision this adapter exists to make visible. Made implicitly - float images shared and
/// everything else converted into a temporary - its consequence, that a shared source comes back
/// written through, is stated nowhere.
enum class GmicSourceSharing : std::uint8_t
{
    /// The library copies the samples on the way in and works on its own memory. The source image is
    /// exactly as it was when the call returns, and the copy is counted.
    Copy,

    /// The library is given the samples themselves and works in place on them.
    ///
    /// No copy in either direction for a command that does not change the image's size, and the
    /// result then *is* the source's memory - reported as borrowed rather than owned, because
    /// nothing about it may be freed by whoever receives it. The cost is that the source is
    /// consumed: it needs a writable view and a contract that permits writes, and a caller that
    /// still wants the original keeps one.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Share
};

/// Whether a value is one of the cancellation policies this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param policy the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isGmicCancellationPolicy(GmicCancellationPolicy policy) noexcept
{
    return static_cast<std::uint8_t>(policy) <= static_cast<std::uint8_t>(GmicCancellationPolicy::DuringCall);
}

/// Whether a value is one of the sharing decisions this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param sharing the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isGmicSourceSharing(GmicSourceSharing sharing) noexcept
{
    return static_cast<std::uint8_t>(sharing) <= static_cast<std::uint8_t>(GmicSourceSharing::Share);
}

/// Everything a caller states when opening a session.
///
/// A plain aggregate with a default for every member, so a caller names only what it is changing.
/// Every string is borrowed for the duration of `create` and copied into the session; nothing here
/// is retained by reference afterwards.
struct GmicRuntimeOptions
{
    /// Command definitions the library parses before every command this session runs.
    ///
    /// Stated once here rather than prefixed to each command as a `command <file>` of its own.
    /// Borrowed for the duration of `create` and copied; an embedded null is refused, because the
    /// library reads this as a C string and would silently take the prefix.
    std::string_view customCommands{};

    /// Whether the library's own standard library of commands is available.
    ///
    /// True for every caller that runs a named filter, because those are defined in it. False is for
    /// a caller running only built-in commands that wants neither the parse nor the surface.
    bool useStandardLibrary = true;

    /// When a cancellation request reaches a running command.
    ///
    /// `DuringCall` by default, which is the opposite of the Python adapter's default and for a
    /// reason that is about the two libraries rather than about taste: arming a stop token here is a
    /// callback that costs nothing until it fires, where installing a per-statement trace hook in an
    /// interpreter slows every line it runs.
    GmicCancellationPolicy cancellation = GmicCancellationPolicy::DuringCall;
};

/// What a result means, which the library does not say.
///
/// A command returns samples and four dimensions. Whether three planes are red, green and blue or
/// three narrowband filters, whether the magnitudes are a fraction of full scale or codes, and which
/// corner row zero is at, are all facts the caller had and the library never learned - so they are
/// stated on the call rather than inferred from the shape of what came back. The adapter checks the
/// stated schema against the channel count the library produced and refuses a disagreement, which is
/// what turns "this command changed the channel count" from a silent relabelling into a failure.
///
/// This is also where the whole of the adapter's metadata behaviour sits. It carries no acquisition
/// record, no astrometric solution and no provenance, because a filter did not produce any of those
/// and carrying them forward across an opaque command would be a claim nobody checked. A caller that
/// applies one of these results to an `Image` states the lineage entry that explains the change, as
/// `Image::transition` requires of every other change.
struct GmicResultIntent
{
    /// What the result's channels are. Its channel count must be the one the command produced, or
    /// the call is refused with `GmicAdapterReason::ResultGeometryRejected`.
    /// `ChannelSchema::Named` accepts whatever count came back, because that is what it is for.
    ChannelSchema channelSchema = ChannelSchema::Mono;

    /// What basis the channels are expressed in.
    ColorSpace colorSpace = ColorSpace::Unspecified;

    /// What the magnitudes mean.
    NumericDomain numericDomain = NumericDomain::NormalizedLinear;

    /// Which corner row zero is at. The library indexes from the top and does not transpose, so a
    /// caller that handed it a top-left image receives one.
    CoordinateOrigin coordinateOrigin = CoordinateOrigin::TopLeft;

    /// The width of one pixel divided by its height. Must be positive and finite.
    double pixelAspectRatio = 1.0;
};

/// One command run over one image.
///
/// The shape is deliberate: one image in, one image out. A command that produces several images or
/// none is refused by name rather than truncated, because the library's own C bridge writes one
/// structure per image it produced into an array sized for the images it was given - which is a
/// buffer overflow in every caller that assumes a command keeps the count it was handed.
struct GmicImageCall
{
    /// The command to run, in the library's own language. Borrowed for the duration of the call. An
    /// empty command is refused with `GmicAdapterReason::CommandEmpty`.
    std::string_view command;

    /// What the library calls this image, which a command may read and may replace.
    ///
    /// Borrowed for the duration of the call and truncated to `kGmicMaxImageNameLength`, because
    /// that is the field the library's structure declares.
    std::string_view imageName{};

    /// The samples to run it over. `describeAsGmicImage` is how one is built from an engine image;
    /// a description built by hand is judged by the same rules.
    GmicImageDescription source;

    /// Whether the library is given those samples or a copy of them.
    GmicSourceSharing sharing = GmicSourceSharing::Copy;

    /// What the result means. See `GmicResultIntent`.
    GmicResultIntent intent{};
};

/// What a session has done since it opened, for a report or a regression test.
///
/// Counters rather than a log: they are cheap, they are always on, and they answer the questions an
/// optional adapter's cost raises - how much memory crossed the boundary, how much of it was copied,
/// and in which direction.
struct GmicCallCounters
{
    /// How many commands this session has handed to the library, including ones that failed or were
    /// stopped there. A call refused before the library was entered is not counted.
    std::uint64_t calls = 0u;

    /// How many bytes of engine samples the library was given directly, with no copy, counted for
    /// the calls that returned a frame.
    std::uint64_t sharedBytes = 0u;

    /// How many bytes the library copied on the way in, which is every byte of a `Copy` call's
    /// source, counted for the calls that returned a frame.
    std::uint64_t copiedIntoLibraryBytes = 0u;

    /// How many bytes came back in memory the library allocated.
    ///
    /// Not a copy this adapter made - the result is handed over as the library's own memory with a
    /// lifetime token - but it is memory that exists because of the call, and a caller sizing a
    /// budget needs it beside the other two.
    std::uint64_t libraryResultBytes = 0u;

    /// The largest single result the library has allocated for this session, in bytes.
    std::uint64_t peakLibraryResultBytes = 0u;

    /// How many calls were refused before reaching the library because their source could not be
    /// handed over - by a bridge rule, or by the rule that a shared source must be writable.
    std::uint64_t bridgeRefusals = 0u;

    /// How many calls ended because the caller stopped them or their deadline passed, before the
    /// library was entered or while it ran.
    std::uint64_t cancellations = 0u;

    /// How many calls ended because the library reported the command failed or threw.
    std::uint64_t commandFailures = 0u;
};

/// An open G'MIC session, and the commands that run through it.
///
/// Built through `create`, which validates what it is given, probes the library once per process and
/// reports why it refused. Move-only, because a copy would be two owners of one lease. Nothing holds
/// one on a caller's behalf: there is no instance accessor, no lazy start and no process-wide
/// default - which is the whole difference from the interface this replaces.
///
/// The calls are thread-safe: several threads may call one session, and a process-wide lock
/// serialises them against each other and against every other session's. What is not thread-safe, as
/// for any other object, is `shutdown()` or a move while another thread is calling.
///
/// Every command runs on a thread this adapter starts for it, with the stack the library's interpreter
/// needs stated, while the calling thread waits. The interpreter nests deeply enough to overflow the
/// one megabyte a Windows thread has by default, and an overflow ends the process rather than failing
/// a call - so the stack is this adapter's to provide rather than every caller's to know about.
/// Starting that thread is a fixed cost every call carries beside the library's own.
///
/// Destroying a session while a call is running is a defect in the caller, and the answer to it is a
/// late release rather than a use-after-free: the session's state is held by the call until it
/// returns. `shutdown()` is the checked half, and it refuses while anything is in flight.
class GmicRuntimeAdapter
{
public:
    /// A session that holds nothing.
    ///
    /// The state one is left in by a move and by `shutdown()`, and a usable default for a member
    /// assigned later. `valid()` is how to ask.
    GmicRuntimeAdapter() noexcept = default;

    /// Closes the session and releases the engine context lease, unless a call is still running.
    AFTERNIGHTENGINEGMICADAPTER_API ~GmicRuntimeAdapter();

    /// Takes over another session, leaving it holding nothing.
    ///
    /// Allocation-free. The lease is not taken again.
    ///
    /// @param other the session to take over. Left holding nothing.
    GmicRuntimeAdapter(GmicRuntimeAdapter&& other) noexcept = default;

    /// Releases this session and takes over another.
    ///
    /// @param other the session to take over. Left holding nothing.
    /// @return this session.
    AFTERNIGHTENGINEGMICADAPTER_API GmicRuntimeAdapter& operator=(GmicRuntimeAdapter&& other) noexcept;

    /// Copying is deleted: a copy would be a second owner of one lease.
    GmicRuntimeAdapter(const GmicRuntimeAdapter&) = delete;

    /// Copying is deleted. See the copy constructor.
    GmicRuntimeAdapter& operator=(const GmicRuntimeAdapter&) = delete;

    /// Opens a session against the library, or reports why it cannot.
    ///
    /// The options are validated before anything is asked of the library, so a configuration mistake
    /// costs nothing; then the engine context's lease is taken; then, the first time in this
    /// process, the bridge's patch level is checked and the library is asked to run a trivial
    /// command with no image. That probe is what tells a library that is linked apart from a library
    /// that works, and its outcome is remembered, so the second session pays nothing for it - unless
    /// the probe's thread could not be started, in which case the next session asks again.
    ///
    /// A build with no G'MIC library answers `LibraryNotCompiledIn` before any of it: there is
    /// nothing for the other rules to decide.
    ///
    /// Thread-safe. Allocates the session's state, and whatever the library allocates during the
    /// probe.
    ///
    /// @param engine the engine context this session belongs to. It must hold something, and it
    ///        cannot be shut down while the session lives.
    /// @param options what the caller is configuring. Copied; nothing in it is retained by reference.
    /// @return the session, or a failure carrying a `ContextKey::AdapterReason` field.
    ///         `ErrorCode::Unsupported` with `LibraryNotCompiledIn` when this build has no library;
    ///         `ErrorCode::InvalidArgument` with `OptionsRejected` when an option is not usable;
    ///         `ErrorCode::NotInitialized` when `engine` holds nothing, carrying the lease refusal as
    ///         its cause; `ErrorCode::ExternalFailure` or `ErrorCode::AllocationFailed` with
    ///         `LibraryProbeFailed` when the library did not answer or its bridge predates the
    ///         changes this adapter needs - and on every later call in this process, which repeats
    ///         the remembered answer; `ErrorCode::AllocationFailed` with `CallThreadNotStarted` when
    ///         the platform would not start the thread the probe runs on.
    [[nodiscard]] static AFTERNIGHTENGINEGMICADAPTER_API Result<GmicRuntimeAdapter>
    create(const EngineContext& engine, const GmicRuntimeOptions& options);

    /// Whether this session is open.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return false for a default-constructed session, one that has been moved from, and one that
    ///         has been shut down.
    [[nodiscard]] bool valid() const noexcept
    {
        return m_state != nullptr;
    }

    /// What this adapter can do, reported the way `gmicAdapterCapabilities()` reports it.
    ///
    /// For the caller that already holds a session and should not have to know a free function
    /// exists. A valid session reports `GmicAdapterState::Loaded`.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return this library's capabilities, with the state this session puts the process in.
    [[nodiscard]] AFTERNIGHTENGINEGMICADAPTER_API GmicAdapterCapabilities capabilities() const noexcept;

    /// Which engine context this session is holding open.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return the context's identity, or `0` when this session holds nothing.
    [[nodiscard]] AFTERNIGHTENGINEGMICADAPTER_API std::uint64_t engineContextId() const noexcept;

    /// How many calls into the library this session has in progress right now, whether running or
    /// waiting for the process-wide lock.
    ///
    /// A diagnostic number rather than a decision: another thread may enter or leave a call
    /// immediately afterwards. `shutdown()` is the call that decides without a gap between asking and
    /// acting.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return the number of calls in flight, or `0` when this session holds nothing.
    [[nodiscard]] AFTERNIGHTENGINEGMICADAPTER_API std::uint64_t callsInFlight() const noexcept;

    /// What this session has done since it opened.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return the counters, or every field zero when this session holds nothing.
    [[nodiscard]] AFTERNIGHTENGINEGMICADAPTER_API GmicCallCounters counters() const noexcept;

    /// Runs a command with no image.
    ///
    /// What a caller uses to validate a command block, or to run one whose whole effect is on the
    /// interpreter's own state. Nothing is shared and nothing comes back, so the only outcomes are
    /// success, a failure the library reported, and cancellation.
    ///
    /// Thread-safe; serialised against every other call into the library.
    ///
    /// @param execution the operation this runs under. Its stop token is observed as this session's
    ///        `GmicCancellationPolicy` states, its deadline is checked at the boundary and watched
    ///        during the call under `DuringCall`, and under `DuringCall` its progress sink receives
    ///        what the library reports.
    /// @param command the command to run. Borrowed for the duration of the call; empty is refused.
    /// @return success, or a failure carrying a `ContextKey::AdapterReason` field.
    ///         `ErrorCode::InvalidState` with `AdapterShutDown` when this session holds nothing;
    ///         `ErrorCode::InvalidArgument` with `CommandEmpty`; `ErrorCode::Cancelled` or
    ///         `ErrorCode::DeadlineExceeded` with `Interrupted` when the caller stopped it or its
    ///         deadline passed; `ErrorCode::ExternalFailure` or
    ///         `ErrorCode::AllocationFailed` with `CommandFailed`, carrying the library's own
    ///         sentence in the message or cause;
    ///         `ErrorCode::AllocationFailed` with `CallThreadNotStarted` when the platform would not
    ///         start the thread the command runs on.
    [[nodiscard]] AFTERNIGHTENGINEGMICADAPTER_API Status execute(const ExecutionContext& execution,
                                                                 std::string_view command);

    /// Runs a command over one image and describes what came back.
    ///
    /// The source reaches the library either as the engine's own samples or as a copy of them, as
    /// `GmicSourceSharing` states. The result is a frame whose buffer says who owns it: the
    /// library's memory, released through the lifetime token the frame holds, or - for a shared
    /// source a command left in place - the engine's own memory, borrowed and released by nobody.
    /// `SampleBuffer::adoptable()` is therefore false for both, and a caller that wants an owned
    /// image copies or converts through the engine.
    ///
    /// Thread-safe; serialised against every other call into the library.
    ///
    /// @param execution the operation this runs under, on the same terms as `execute`.
    /// @param call which command to run, over which samples, with which sharing and meaning.
    /// @return the frame, or a failure carrying a `ContextKey::AdapterReason` field.
    ///         `ErrorCode::InvalidState` with `AdapterShutDown`; `ErrorCode::InvalidArgument` with
    ///         `CommandEmpty`, `OptionsRejected` when the sharing decision or the result intent is
    ///         not usable, `BufferBridgeRefused` when the source is not one this adapter will hand
    ///         over, or `SourceNotShareable` when sharing was asked for over read-only samples;
    ///         `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` with `Interrupted`;
    ///         `ErrorCode::ExternalFailure` with
    ///         `CommandFailed`, `ResultMissing`, `ResultImageCountUnexpected`,
    ///         `ResultGeometryRejected`, `ResultFormatUnexpected` or `ResultNameOverflowed`;
    ///         `ErrorCode::AllocationFailed` with `CommandFailed` or `CallThreadNotStarted`. Every
    ///         buffer the library allocated is released before a failure is returned.
    [[nodiscard]] AFTERNIGHTENGINEGMICADAPTER_API Result<RawFrame> run(const ExecutionContext& execution,
                                                                       const GmicImageCall& call);

    /// Closes the session, and says whether it could.
    ///
    /// The checked half of destruction: a session with a call in flight refuses rather than releasing
    /// the lease out from under it. On success this session holds nothing afterwards, as though it
    /// had been moved from.
    ///
    /// Idempotent: shutting down a session that holds nothing succeeds, so cleanup that shuts one
    /// down and then lets a destructor do it again is correct rather than an error to report.
    ///
    /// Not thread-safe with respect to this session, which is being modified. Allocates only the
    /// failure it reports.
    ///
    /// @return success, or `ErrorCode::Busy` with `GmicAdapterReason::CallInFlight` when a call is
    ///         still running - in which case nothing has been released and the session is exactly as
    ///         it was.
    [[nodiscard]] AFTERNIGHTENGINEGMICADAPTER_API Status shutdown();

private:
    /// Takes ownership of a state the factory has already built.
    ///
    /// @param state the session and the lease that holds its engine context open. Never null on this
    ///        path.
    explicit GmicRuntimeAdapter(std::shared_ptr<GmicSessionState> state) noexcept : m_state(std::move(state)) {}

    /// What this session owns, or null when it owns nothing. Shared rather than unique so that a
    /// call running on another thread keeps the state alive past a handle destroyed too early.
    std::shared_ptr<GmicSessionState> m_state;
};

} // namespace an
