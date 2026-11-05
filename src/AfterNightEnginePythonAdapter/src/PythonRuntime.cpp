// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEnginePythonAdapter - starting and finalising the one interpreter this process may
// have, running calls in it, and the process slot the capability report reads.
//
// This is the one translation unit that calls into Python. A start checks the options, checks under a
// lock that the process slot is free, refuses an interpreter somebody else started, takes the engine
// context's lease, and then brings the runtime up in an isolated configuration with no signal
// handlers on a thread the state owns; that thread releases the interpreter lock and waits, and
// finalises the runtime on the same thread when the state is destroyed. Every call takes the
// interpreter lock through `PyGILState_Ensure`, installs the per-line cancellation check when the
// policy asks for it, and translates what Python raised - by class name and message, never by
// traceback - unless the call recognised the failure as one of its own reasons. Buffers are lent as
// memoryviews over the engine's own samples and released before the call returns, whatever Python
// kept of them.
//
// When no runtime is compiled in, starting is refused with `RuntimeNotCompiledIn` before the options
// are looked at, and the refusal is recorded for the capability report; the handle's forwarding
// functions below are the same in both configurations.

#include "PythonRuntime.h"

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ExceptionBoundary.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <atomic>
#include <condition_variable>
#include <cstring>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace an
{
namespace
{

/// The runtime version of the one adapter this process may have, or zero when the seat is free.
///
/// Keeping the immutable value in the seat means a capability report never dereferences adapter
/// storage whose last owner another thread may be releasing.
std::atomic<std::uint32_t> gActiveAdapter{0u};

/// Serialises starting and finalising, so two threads cannot both find the seat free.
std::mutex gStartupMutex;

/// Why the last start in this process failed, for the capability report.
std::atomic<ErrorCode> gLastFailureCode{ErrorCode::Ok};
std::atomic<PythonAdapterReason> gLastFailureReason{PythonAdapterReason::None};

/// The build's own version, as a value.
[[nodiscard]] PythonRuntimeVersion buildVersion() noexcept
{
    return PythonRuntimeVersion{.major = kPythonBuildMajor, .minor = kPythonBuildMinor, .patch = kPythonBuildPatch};
}

/// Packs a version into the seat's word, one byte per number. The lowest byte is always one, so no
/// packed version is the zero that means a free seat - not even a runtime that reports 0.0.0.
[[nodiscard]] constexpr std::uint32_t encodedVersion(PythonRuntimeVersion version) noexcept
{
    return (static_cast<std::uint32_t>(version.major) << 24u) | (static_cast<std::uint32_t>(version.minor) << 16u) |
           (static_cast<std::uint32_t>(version.patch) << 8u) | 1u;
}

/// Reads a version back out of the seat's word, ignoring the marker byte `encodedVersion` sets.
[[nodiscard]] constexpr PythonRuntimeVersion decodedVersion(std::uint32_t version) noexcept
{
    return {.major = static_cast<int>((version >> 24u) & 0xffu),
            .minor = static_cast<int>((version >> 16u) & 0xffu),
            .patch = static_cast<int>((version >> 8u) & 0xffu)};
}

} // namespace

Status pythonAdapterFailure(ErrorCode code, PythonAdapterReason reason, const char* operation, std::string message)
{
    return Status::failure(code,
                           StatusDetail{.message = std::move(message),
                                        .operation = operation,
                                        .domain = ErrorDomain::Interop,
                                        .context = {StatusContextField::ofText(ContextKey::Library, "python"),
                                                    StatusContextField::ofCount(ContextKey::AdapterReason,
                                                                                static_cast<std::uint64_t>(reason))}});
}

void recordPythonStartupFailure(ErrorCode code, PythonAdapterReason reason) noexcept
{
    gLastFailureCode.store(code, std::memory_order_relaxed);
    gLastFailureReason.store(reason, std::memory_order_relaxed);
}

PythonCallCounters PythonInterpreterState::counters() const noexcept
{
    return PythonCallCounters{.calls = m_calls.load(std::memory_order_relaxed),
                              .sharedBytes = m_sharedBytes.load(std::memory_order_relaxed),
                              .copiedBytes = 0u,
                              .bridgeRefusals = m_bridgeRefusals.load(std::memory_order_relaxed),
                              .cancellations = m_cancellations.load(std::memory_order_relaxed),
                              .pythonExceptions = m_pythonExceptions.load(std::memory_order_relaxed)};
}

PythonInterpreterState::PythonInterpreterState(EngineContextLease lease, PythonCancellationPolicy cancellation,
                                               PythonRuntimeVersion runtimeVersion) noexcept
    : m_lease(std::move(lease)), m_cancellation(cancellation), m_runtimeVersion(runtimeVersion)
{
}

#if defined(AFTERNIGHT_HAS_PYTHON)

// This adapter is written against the runtime this repository pins, and the exception accessors it
// uses are the ones that replaced the fetch-and-normalise pair. A build against an older runtime
// would compile a different error path with different ownership rules, which is not a thing to
// discover at run time.
static_assert(PY_VERSION_HEX >= 0x030C0000, "this adapter uses PyErr_GetRaisedException, which arrived in Python 3.12");

namespace
{

/// The global interpreter lock, held for the duration of a scope.
///
/// `PyGILState_Ensure` is the entry point that works from a thread the interpreter has never seen,
/// which is every thread that reaches this adapter: the interpreter is left with its lock released
/// between calls, so there is no state to inherit.
class GilLock
{
public:
    GilLock() noexcept : m_state(PyGILState_Ensure()) {}

    ~GilLock()
    {
        PyGILState_Release(m_state);
    }

    GilLock(const GilLock&) = delete;
    GilLock& operator=(const GilLock&) = delete;
    GilLock(GilLock&&) = delete;
    GilLock& operator=(GilLock&&) = delete;

private:
    PyGILState_STATE m_state;
};

/// What a per-line cancellation check reads, for the duration of one call.
struct CancellationScope
{
    /// The operation whose stop token and deadline decide. Never null while this scope is installed.
    const ExecutionContext* execution;

    /// Whether this check is what raised, which is how an interrupt is told apart from a
    /// `KeyboardInterrupt` the Python code raised itself.
    bool raised;
};

/// The scope the trace function reads.
///
/// Thread-local because a trace function is installed per thread state and runs on the thread whose
/// code it is tracing, which is the thread that installed it. Nothing else can see it, which is what
/// makes this safe where a cross-thread interrupt would not be.
thread_local CancellationScope* tCancellation = nullptr;

/// Raises inside the interpreter when the operation this call belongs to has been stopped.
///
/// Runs on the interpreter's own thread with the lock held, which is the only place CPython permits
/// an exception to be set. Returning -1 propagates it as though the traced line had raised.
extern "C" int traceCancellation(PyObject*, PyFrameObject*, int what, PyObject*)
{
    if (what != PyTrace_LINE)
    {
        return 0;
    }
    CancellationScope* scope = tCancellation;
    if (scope == nullptr || scope->raised)
    {
        return 0;
    }
    if (!scope->execution->stopRequested() && !scope->execution->deadlineExceeded())
    {
        return 0;
    }
    scope->raised = true;
    PyErr_SetString(PyExc_KeyboardInterrupt, "AfterNight stopped this operation");
    return -1;
}

/// The exception the interpreter is holding, as text, with the exception cleared afterwards.
///
/// The class name and the message are what a person needs, and they are all that is taken: a
/// traceback is a diagnostic rather than a value, and carrying one would put an unbounded string into
/// a failure.
[[nodiscard]] std::string takePythonErrorText()
{
    PyObject* raised = PyErr_GetRaisedException();
    if (raised == nullptr)
    {
        return "Python reported a failure and raised nothing";
    }
    std::string text = Py_TYPE(raised)->tp_name != nullptr ? Py_TYPE(raised)->tp_name : "Exception";
    PyObject* rendered = PyObject_Str(raised);
    if (rendered != nullptr)
    {
        const char* utf8 = PyUnicode_AsUTF8(rendered);
        if (utf8 != nullptr && *utf8 != '\0')
        {
            text += ": ";
            text += utf8;
        }
        else if (utf8 == nullptr)
        {
            // Rendering the exception succeeded, but encoding that rendering (for example one with
            // an unpaired surrogate) raised a second exception. Keep the original class name and do
            // not leave the secondary encoding error pending for the next call.
            PyErr_Clear();
        }
        Py_DECREF(rendered);
    }
    else
    {
        PyErr_Clear();
    }
    Py_DECREF(raised);
    return text;
}

/// Whether the interpreter is holding an exception of this class.
[[nodiscard]] bool raisedExceptionMatches(PyObject* type) noexcept
{
    return PyErr_Occurred() != nullptr && PyErr_ExceptionMatches(type) != 0;
}

/// Whether the pending exception says the module the caller named is absent.
///
/// A ModuleNotFoundError raised while that module imports one of its own dependencies is restored
/// unchanged and translated as a Python exception; it is not evidence that the requested module is
/// missing.
[[nodiscard]] bool takeRequestedModuleNotFound(std::string_view requested) noexcept
{
    if (!raisedExceptionMatches(PyExc_ModuleNotFoundError))
        return false;
    PyObject* raised = PyErr_GetRaisedException();
    if (raised == nullptr)
        return false;
    PyObject* name = PyObject_GetAttrString(raised, "name");
    const char* missing = name != nullptr ? PyUnicode_AsUTF8(name) : nullptr;
    const std::size_t missingLength = missing != nullptr ? std::strlen(missing) : 0u;
    const bool matches = missing != nullptr &&
                         (requested == missing || (requested.size() > missingLength && requested.starts_with(missing) &&
                                                   requested[missingLength] == '.'));
    Py_XDECREF(name);
    if (matches)
    {
        Py_DECREF(raised);
        PyErr_Clear();
    }
    else
    {
        PyErr_Clear();
        PyErr_SetRaisedException(raised);
    }
    return matches;
}

/// Ends one memoryview export and drops the adapter's reference without losing the first exception.
///
/// Calling `release()` matters even when Python retained the memoryview: a decref alone leaves the
/// export usable, although the engine's single-call borrow has ended.
void releaseLentMemoryView(PyObject* view, PyObject*& firstError)
{
    PyObject* released = PyObject_CallMethod(view, "release", nullptr);
    if (released == nullptr)
    {
        PyObject* error = PyErr_GetRaisedException();
        if (firstError == nullptr)
            firstError = error;
        else
            Py_XDECREF(error);
    }
    else
        Py_DECREF(released);
    Py_DECREF(view);
}

/// Whether a buffer this adapter is about to lend to Python is one it can describe.
///
/// Not `adoptPythonBufferCheck`: that answers the other direction, where a foreign buffer is being
/// turned back into an engine layout and the caller has to say which reading of three axes it means.
/// This answers whether a description - which this adapter's own bridge produced - can be handed to
/// CPython's buffer protocol as it stands, which is a shorter and different list.
///
/// Write permission is not judged here. Whether a buffer may be written through is a rule of the call
/// rather than of the two memory models, and the bridge's alphabet has no honest name for a source
/// that arrived writable - so `PythonAdapterReason::BufferPermissionMismatch` says that instead.
///
/// @param description the buffer to lend.
/// @return `BridgeRefusal::None` when it can be lent; otherwise the first rule that refused.
[[nodiscard]] BridgeRefusal checkLentBuffer(const PythonBufferDescription& description)
{
    if (description.data == nullptr)
    {
        return BridgeRefusal::NoSamples;
    }
    if (!isPythonBufferFormat(description.format))
    {
        return BridgeRefusal::UnknownContract;
    }
    const std::optional<SampleType> type = pythonSampleTypeOf(description.format);
    if (!type.has_value() || description.itemSizeBytes != static_cast<std::int64_t>(sampleTypeSizeBytes(*type)))
    {
        return BridgeRefusal::SampleTypeNotRepresentable;
    }
    if (description.axes < 2 || description.axes > kMaxPythonBufferAxes)
    {
        return BridgeRefusal::ArrangementNotRepresentable;
    }
    std::int64_t logical = description.itemSizeBytes;
    for (std::int32_t axis = 0; axis < description.axes; ++axis)
    {
        if (description.shape[static_cast<std::size_t>(axis)] <= 0 ||
            description.strides[static_cast<std::size_t>(axis)] <= 0)
        {
            return BridgeRefusal::ExtentExceedsLibraryIndex;
        }
        logical *= description.shape[static_cast<std::size_t>(axis)];
    }
    if (description.strides[static_cast<std::size_t>(description.axes - 1)] != description.itemSizeBytes)
    {
        return BridgeRefusal::ArrangementNotRepresentable;
    }
    if (description.lengthBytes != logical)
    {
        return BridgeRefusal::RowStrideNotRepresentable;
    }
    return BridgeRefusal::None;
}

/// A memoryview over samples the engine owns, or null with an exception set.
///
/// The shape and stride arrays are the caller's and must outlive the view: the protocol's structure
/// carries pointers, and whether CPython copies them is an implementation detail of the version being
/// built against rather than a promise.
[[nodiscard]] PyObject* lendAsMemoryView(const PythonBufferDescription& description, Py_ssize_t* shape,
                                         Py_ssize_t* strides)
{
    for (std::int32_t axis = 0; axis < description.axes; ++axis)
    {
        shape[axis] = static_cast<Py_ssize_t>(description.shape[static_cast<std::size_t>(axis)]);
        strides[axis] = static_cast<Py_ssize_t>(description.strides[static_cast<std::size_t>(axis)]);
    }
    Py_buffer view{};
    view.buf = description.data;
    view.obj = nullptr;
    view.len = static_cast<Py_ssize_t>(description.lengthBytes);
    view.itemsize = static_cast<Py_ssize_t>(description.itemSizeBytes);
    view.readonly = description.readOnly ? 1 : 0;
    view.ndim = description.axes;
    // The format string is a literal compiled into this library, so it outlives every view built
    // from it. CPython does not copy it.
    view.format = const_cast<char*>(pythonBufferFormatCode(description.format));
    view.shape = shape;
    view.strides = strides;
    view.suboffsets = nullptr;
    view.internal = nullptr;
    return PyMemoryView_FromBuffer(&view);
}

} // namespace

Result<std::shared_ptr<PythonInterpreterState>> PythonInterpreterState::start(const EngineContext& engine,
                                                                              const PythonRuntimeOptions& options)
{
    // The options first, because a mistake in them costs nothing to report and the caller's repair is
    // the same whatever the process is doing.
    // None of the refusals before the runtime is asked to start is recorded as a load failure: the
    // capability report distinguishes "nobody has successfully started one" from "starting one was
    // tried and refused", and a mistake in the options is neither.
    if (!isPythonCancellationPolicy(options.cancellation))
    {
        return Result<std::shared_ptr<PythonInterpreterState>>{
            pythonAdapterFailure(ErrorCode::InvalidArgument, PythonAdapterReason::InterpreterConfigurationRejected,
                                 "PythonRuntimeAdapter::create", "the cancellation policy is not one of the two")};
    }
    for (const std::string_view path : options.modulePaths)
    {
        if (path.empty())
        {
            return Result<std::shared_ptr<PythonInterpreterState>>{
                pythonAdapterFailure(ErrorCode::InvalidArgument, PythonAdapterReason::InterpreterConfigurationRejected,
                                     "PythonRuntimeAdapter::create",
                                     "a module search path is empty, which Python reads as the current directory")};
        }
    }

    std::unique_lock<std::mutex> guard(gStartupMutex);
    if (gActiveAdapter.load(std::memory_order_acquire) != 0u)
    {
        return Result<std::shared_ptr<PythonInterpreterState>>{pythonAdapterFailure(
            ErrorCode::AlreadyInitialized, PythonAdapterReason::AdapterAlreadyActive, "PythonRuntimeAdapter::create",
            "this process already holds a Python adapter, and CPython has one interpreter per process")};
    }
    if (Py_IsInitialized() != 0)
    {
        // Reported through `foreignInterpreter` in the capability report rather than as a load
        // failure: an application that embeds its own Python is a legitimate host, not a broken one.
        return Result<std::shared_ptr<PythonInterpreterState>>{pythonAdapterFailure(
            ErrorCode::InvalidState, PythonAdapterReason::ForeignInterpreterActive, "PythonRuntimeAdapter::create",
            "an interpreter this adapter did not start is already running in this process")};
    }

    // The lease before the interpreter, so that a context which cannot admit the adapter is reported
    // before a runtime has been started that would then have to be torn down again.
    Result<EngineContextLease> lease = engine.acquireLease(0u);
    if (!lease.hasValue())
    {
        return Result<std::shared_ptr<PythonInterpreterState>>{
            Status::failure(ErrorCode::NotInitialized,
                            StatusDetail{.message = "the engine context cannot hold a Python adapter open",
                                         .operation = "PythonRuntimeAdapter::create",
                                         .domain = ErrorDomain::Interop},
                            lease.status())};
    }

    const PythonRuntimeVersion runtimeVersion{.major = static_cast<int>((Py_Version >> 24) & 0xffu),
                                              .minor = static_cast<int>((Py_Version >> 16) & 0xffu),
                                              .patch = static_cast<int>((Py_Version >> 8) & 0xffu)};
    static constexpr ExternalBoundary kStartupBoundary{
        .library = "python", .operation = "PythonRuntimeAdapter::create", .domain = ErrorDomain::Interop};
    Result<std::shared_ptr<PythonInterpreterState>> state =
        evaluateAcrossBoundary(kStartupBoundary, [&lease, &options, runtimeVersion]() {
            return std::shared_ptr<PythonInterpreterState>(
                new PythonInterpreterState(std::move(*lease), options.cancellation, runtimeVersion));
        });
    if (!state.hasValue())
    {
        recordPythonStartupFailure(state.status().code(), PythonAdapterReason::InterpreterStartupFailed);
        return state;
    }
    Status initialized;
    const Status crossed = callAcrossBoundary(kStartupBoundary, [&] { initialized = (*state)->initialize(options); });
    if (crossed.failed())
        initialized = crossed;
    if (initialized.failed())
    {
        const PythonAdapterReason reason = pythonAdapterReason(initialized) == PythonAdapterReason::None
                                               ? PythonAdapterReason::InterpreterStartupFailed
                                               : pythonAdapterReason(initialized);
        recordPythonStartupFailure(initialized.code(), reason);
        guard.unlock();
        state = Result<std::shared_ptr<PythonInterpreterState>>{initialized};
        return state;
    }
    (*state)->m_ownsSeat = true;
    gActiveAdapter.store(encodedVersion(runtimeVersion), std::memory_order_release);
    recordPythonStartupFailure(ErrorCode::Ok, PythonAdapterReason::None);
    return state;
}

Status PythonInterpreterState::initialize(const PythonRuntimeOptions& options)
{
    const std::string home(options.pythonHome);
    const std::string programName(options.programName);
    std::vector<std::string> modulePaths;
    modulePaths.reserve(options.modulePaths.size());
    for (const std::string_view path : options.modulePaths)
        modulePaths.emplace_back(path);

    m_ownerThread = std::thread([this, home, programName, modulePaths = std::move(modulePaths)] {
        static constexpr ExternalBoundary kStartupBoundary{
            .library = "python", .operation = "PythonRuntimeAdapter::create", .domain = ErrorDomain::Interop};
        const auto startInterpreter = [&]() -> Status {
            PyConfig config;
            PyConfig_InitIsolatedConfig(&config);
            // An isolated configuration is the one an embedder wants: no environment variables, no
            // user site directory, and no argument parsing. An embedded interpreter must not take
            // the application's SIGINT handler either.
            config.parse_argv = 0;
            config.install_signal_handlers = 0;
            PyStatus configured = PyStatus_Ok();
            if (!home.empty())
                configured = PyConfig_SetBytesString(&config, &config.home, home.c_str());
            if (!PyStatus_Exception(configured) && !programName.empty())
                configured = PyConfig_SetBytesString(&config, &config.program_name, programName.c_str());
            if (PyStatus_Exception(configured))
            {
                std::string message = "the Python configuration refused a stated path";
                if (configured.err_msg != nullptr)
                {
                    message += ": ";
                    message += configured.err_msg;
                }
                PyConfig_Clear(&config);
                return pythonAdapterFailure(ErrorCode::InvalidArgument,
                                            PythonAdapterReason::InterpreterConfigurationRejected,
                                            "PythonRuntimeAdapter::create", std::move(message));
            }

            const PyStatus started = Py_InitializeFromConfig(&config);
            PyConfig_Clear(&config);
            if (PyStatus_Exception(started))
            {
                std::string message = "the Python runtime refused to start";
                if (started.err_msg != nullptr)
                {
                    message += ": ";
                    message += started.err_msg;
                }
                if (Py_IsInitialized() != 0)
                    (void)Py_FinalizeEx();
                return pythonAdapterFailure(ErrorCode::ExternalFailure, PythonAdapterReason::InterpreterStartupFailed,
                                            "PythonRuntimeAdapter::create", std::move(message));
            }

            for (const std::string& entry : modulePaths)
            {
                PyObject* searchPath = PySys_GetObject("path");
                PyObject* value = PyUnicode_DecodeFSDefault(entry.c_str());
                if (searchPath == nullptr || value == nullptr || PyList_Append(searchPath, value) != 0)
                {
                    const std::string message = "a module search path could not be appended to sys.path: " + entry;
                    Py_XDECREF(value);
                    PyErr_Clear();
                    (void)Py_FinalizeEx();
                    return pythonAdapterFailure(ErrorCode::InvalidArgument,
                                                PythonAdapterReason::InterpreterConfigurationRejected,
                                                "PythonRuntimeAdapter::create", message);
                }
                Py_DECREF(value);
            }

            // Calls enter through PyGILState_Ensure. The owning thread waits without the lock and
            // retains the saved state solely so it can restore it on this same OS thread at shutdown.
            m_savedThreadState = PyEval_SaveThread();
            return Status::success();
        };

        Status started;
        const Status crossed = callAcrossBoundary(kStartupBoundary, [&] { started = startInterpreter(); });
        if (crossed.failed())
        {
            if (m_savedThreadState != nullptr)
            {
                PyEval_RestoreThread(static_cast<PyThreadState*>(m_savedThreadState));
                m_savedThreadState = nullptr;
            }
            if (Py_IsInitialized() != 0)
                (void)Py_FinalizeEx();
            started = crossed;
        }
        {
            const std::lock_guard lock(m_ownerMutex);
            m_startupStatus = started;
            m_startupComplete = true;
        }
        m_ownerChanged.notify_all();
        if (started.failed())
            return;

        std::unique_lock lock(m_ownerMutex);
        m_ownerChanged.wait(lock, [this] { return m_stopOwner; });
        lock.unlock();
        PyEval_RestoreThread(static_cast<PyThreadState*>(m_savedThreadState));
        m_savedThreadState = nullptr;
        (void)Py_FinalizeEx();
    });

    std::unique_lock lock(m_ownerMutex);
    m_ownerChanged.wait(lock, [this] { return m_startupComplete; });
    const Status started = m_startupStatus;
    lock.unlock();
    if (started.failed())
        m_ownerThread.join();
    return started;
}

PythonInterpreterState::~PythonInterpreterState()
{
    const std::lock_guard<std::mutex> guard(gStartupMutex);
    if (m_ownerThread.joinable())
    {
        {
            const std::lock_guard lock(m_ownerMutex);
            m_stopOwner = true;
        }
        m_ownerChanged.notify_all();
        m_ownerThread.join();
    }
    if (m_ownsSeat)
        gActiveAdapter.store(0u, std::memory_order_release);
}

template <typename BodyT>
Status PythonInterpreterState::withInterpreter(const ExecutionContext& execution, const char* operation, BodyT&& body,
                                               RecognisedFailure* recognised)
{
    // The boundary check, before the lock: an operation the caller has already stopped does not need
    // an interpreter, and entering one to find out would serialise against every other call first.
    const Status boundary = execution.checkCancellation();
    if (boundary.failed())
    {
        m_cancellations.fetch_add(1u, std::memory_order_relaxed);
        return pythonAdapterFailure(boundary.code(), PythonAdapterReason::PythonInterrupted, operation,
                                    "the caller stopped this operation before entering the interpreter");
    }

    m_callsInFlight.fetch_add(1u, std::memory_order_acq_rel);
    struct InFlightGuard
    {
        std::atomic<std::uint64_t>* counter;

        ~InFlightGuard()
        {
            counter->fetch_sub(1u, std::memory_order_release);
        }
    } inFlight{&m_callsInFlight};

    const GilLock lock;
    CancellationScope scope{.execution = &execution, .raised = false};
    const bool traced = m_cancellation == PythonCancellationPolicy::PerLine;
    if (traced)
    {
        tCancellation = &scope;
        PyEval_SetTrace(traceCancellation, nullptr);
    }
    const bool succeeded = std::forward<BodyT>(body)();
    if (traced)
    {
        PyEval_SetTrace(nullptr, nullptr);
        tCancellation = nullptr;
    }
    m_calls.fetch_add(1u, std::memory_order_relaxed);
    const Status stopped = execution.checkCancellation();
    if (scope.raised || stopped.failed())
    {
        PyErr_Clear();
        m_cancellations.fetch_add(1u, std::memory_order_relaxed);
        return pythonAdapterFailure(stopped.failed() ? stopped.code() : ErrorCode::Cancelled,
                                    PythonAdapterReason::PythonInterrupted, operation,
                                    scope.raised ? "the caller stopped this operation and the interpreter gave up at "
                                                   "the next statement"
                                                 : "the caller stopped this operation before the interpreter returned");
    }
    if (succeeded)
    {
        return Status::success();
    }

    // The interpreter is holding an exception. Which of the remaining answers it is depends on
    // whether the body recognised it as part of the adapter's own protocol.
    if (recognised != nullptr && recognised->reason != PythonAdapterReason::None)
    {
        PyErr_Clear();
        return pythonAdapterFailure(ErrorCode::ExternalFailure, recognised->reason, operation,
                                    std::move(recognised->message));
    }
    m_pythonExceptions.fetch_add(1u, std::memory_order_relaxed);
    return pythonAdapterFailure(ErrorCode::ExternalFailure, PythonAdapterReason::PythonExceptionRaised, operation,
                                takePythonErrorText());
}

Status PythonInterpreterState::execute(const ExecutionContext& execution, std::string_view source)
{
    const std::string code(source);
    return withInterpreter(execution, "PythonRuntimeAdapter::execute", [&code]() {
        PyObject* main = PyImport_AddModule("__main__");
        if (main == nullptr)
        {
            return false;
        }
        PyObject* globals = PyModule_GetDict(main);
        PyObject* result = PyRun_String(code.c_str(), Py_file_input, globals, globals);
        if (result == nullptr)
        {
            return false;
        }
        Py_DECREF(result);
        return true;
    });
}

Status PythonInterpreterState::callImageFunction(const ExecutionContext& execution, const PythonImageCall& call)
{
    // What the call requires of each borrow, before the interpreter and before the memory models: the
    // source is lent read-only and the destination writable, and a description that says otherwise is a
    // call this adapter will not make rather than a bridge its two models cannot express.
    if (!call.source.readOnly || call.destination.readOnly)
    {
        m_bridgeRefusals.fetch_add(1u, std::memory_order_relaxed);
        return pythonAdapterFailure(ErrorCode::InvalidArgument, PythonAdapterReason::BufferPermissionMismatch,
                                    "PythonRuntimeAdapter::callImageFunction",
                                    !call.source.readOnly
                                        ? "the source buffer is described writable; it is lent read-only"
                                        : "the destination buffer is described read-only; Python writes its "
                                          "result through it");
    }

    // Then the bridge: a buffer this adapter would not lend is a refusal the caller can answer, and
    // reporting it without taking the lock keeps a mistake cheap.
    const BridgeRefusal sourceRefusal = checkLentBuffer(call.source);
    const BridgeRefusal destinationRefusal =
        sourceRefusal == BridgeRefusal::None ? checkLentBuffer(call.destination) : sourceRefusal;
    if (destinationRefusal != BridgeRefusal::None)
    {
        m_bridgeRefusals.fetch_add(1u, std::memory_order_relaxed);
        const bool aboutSource = sourceRefusal != BridgeRefusal::None;
        return pythonAdapterFailure(ErrorCode::InvalidArgument, PythonAdapterReason::BufferBridgeRefused,
                                    "PythonRuntimeAdapter::callImageFunction",
                                    std::string(aboutSource ? "the source buffer" : "the destination buffer") +
                                        " cannot be lent to Python: " + bridgeRefusalName(destinationRefusal));
    }

    const std::string moduleName(call.moduleName);
    const std::string functionName(call.functionName);
    RecognisedFailure recognised;
    const Status status = withInterpreter(
        execution, "PythonRuntimeAdapter::callImageFunction",
        [&]() {
            PyObject* module = PyImport_ImportModule(moduleName.c_str());
            if (module == nullptr)
            {
                if (takeRequestedModuleNotFound(moduleName))
                {
                    recognised.reason = PythonAdapterReason::ModuleNotFound;
                    recognised.message = "no module named " + moduleName + " could be imported";
                }
                return false;
            }
            PyObject* function = PyObject_GetAttrString(module, functionName.c_str());
            Py_DECREF(module);
            if (function == nullptr)
            {
                if (raisedExceptionMatches(PyExc_AttributeError))
                {
                    recognised.reason = PythonAdapterReason::AttributeNotFound;
                    recognised.message = moduleName + " has no attribute named " + functionName;
                }
                return false;
            }
            if (PyCallable_Check(function) == 0)
            {
                Py_DECREF(function);
                recognised.reason = PythonAdapterReason::NotCallable;
                recognised.message = moduleName + "." + functionName + " is not callable";
                return false;
            }

            Py_ssize_t sourceShape[kMaxPythonBufferAxes] = {};
            Py_ssize_t sourceStrides[kMaxPythonBufferAxes] = {};
            Py_ssize_t destinationShape[kMaxPythonBufferAxes] = {};
            Py_ssize_t destinationStrides[kMaxPythonBufferAxes] = {};
            PyObject* sourceView = lendAsMemoryView(call.source, sourceShape, sourceStrides);
            PyObject* destinationView = sourceView == nullptr
                                            ? nullptr
                                            : lendAsMemoryView(call.destination, destinationShape, destinationStrides);
            if (sourceView == nullptr)
            {
                Py_DECREF(function);
                return false;
            }
            if (destinationView == nullptr)
            {
                PyObject* firstError = PyErr_GetRaisedException();
                releaseLentMemoryView(sourceView, firstError);
                Py_DECREF(function);
                PyErr_SetRaisedException(firstError);
                return false;
            }

            PyObject* result = PyObject_CallFunctionObjArgs(function, sourceView, destinationView, nullptr);
            // The views are released before the result is judged, so the borrow ends with the call
            // whichever way it went. A Python object that kept one alive keeps a released memoryview
            // rather than access to a buffer whose single-call borrow has ended.
            PyObject* firstError = result == nullptr ? PyErr_GetRaisedException() : nullptr;
            releaseLentMemoryView(sourceView, firstError);
            releaseLentMemoryView(destinationView, firstError);
            Py_DECREF(function);
            if (firstError != nullptr)
            {
                Py_XDECREF(result);
                PyErr_SetRaisedException(firstError);
                return false;
            }
            const bool returnedNothing = result == Py_None;
            Py_DECREF(result);
            if (!returnedNothing)
            {
                recognised.reason = PythonAdapterReason::UnexpectedResult;
                recognised.message = moduleName + "." + functionName +
                                     " returned a value; it writes through the destination buffer and returns None";
                return false;
            }
            return true;
        },
        &recognised);

    if (status.ok())
    {
        m_sharedBytes.fetch_add(static_cast<std::uint64_t>(call.source.lengthBytes) +
                                    static_cast<std::uint64_t>(call.destination.lengthBytes),
                                std::memory_order_relaxed);
    }
    return status;
}

namespace
{

/// Whether an interpreter nobody here started is running.
[[nodiscard]] bool foreignInterpreterActive() noexcept
{
    return Py_IsInitialized() != 0 && gActiveAdapter.load(std::memory_order_acquire) == 0u;
}

} // namespace

#else

namespace
{

/// False: a build with no runtime cannot have started one, and cannot see one either.
[[nodiscard]] bool foreignInterpreterActive() noexcept
{
    return false;
}

} // namespace

Result<std::shared_ptr<PythonInterpreterState>> PythonInterpreterState::start(const EngineContext&,
                                                                              const PythonRuntimeOptions&)
{
    // The whole of the absent configuration: a named refusal, no side effect, and nothing held. It is
    // built and tested rather than reasoned about, which is why the disabled build exists.
    const Status failure = pythonAdapterFailure(ErrorCode::Unsupported, PythonAdapterReason::RuntimeNotCompiledIn,
                                                "PythonRuntimeAdapter::create",
                                                "this build of the Python adapter contains no Python runtime");
    recordPythonStartupFailure(failure.code(), PythonAdapterReason::RuntimeNotCompiledIn);
    return Result<std::shared_ptr<PythonInterpreterState>>{failure};
}

PythonInterpreterState::~PythonInterpreterState() = default;

Status PythonInterpreterState::execute(const ExecutionContext&, std::string_view)
{
    return pythonAdapterFailure(ErrorCode::Unsupported, PythonAdapterReason::RuntimeNotCompiledIn,
                                "PythonRuntimeAdapter::execute",
                                "this build of the Python adapter contains no Python runtime");
}

Status PythonInterpreterState::callImageFunction(const ExecutionContext&, const PythonImageCall&)
{
    return pythonAdapterFailure(ErrorCode::Unsupported, PythonAdapterReason::RuntimeNotCompiledIn,
                                "PythonRuntimeAdapter::callImageFunction",
                                "this build of the Python adapter contains no Python runtime");
}

#endif

PythonAdapterCapabilities currentPythonAdapterCapabilities() noexcept
{
    PythonAdapterCapabilities capabilities;
    capabilities.runtimeCompiledIn = kPythonRuntimeCompiledIn;
    capabilities.buildVersion = buildVersion();
    capabilities.loadFailure = gLastFailureCode.load(std::memory_order_relaxed);
    capabilities.loadReason = gLastFailureReason.load(std::memory_order_relaxed);
    if (!kPythonRuntimeCompiledIn)
    {
        capabilities.state = PythonAdapterState::Absent;
        return capabilities;
    }
    const std::uint32_t active = gActiveAdapter.load(std::memory_order_acquire);
    if (active != 0u)
    {
        capabilities.state = PythonAdapterState::Loaded;
        capabilities.runtimeVersion = decodedVersion(active);
        return capabilities;
    }
    capabilities.foreignInterpreter = foreignInterpreterActive();
    capabilities.state =
        capabilities.loadFailure == ErrorCode::Ok ? PythonAdapterState::Available : PythonAdapterState::FailedToLoad;
    return capabilities;
}

// --- the handle -------------------------------------------------------------------------------
//
// Everything below is Python-free by construction: the handle forwards to the state, and the state is
// what differs between the two configurations.

PythonRuntimeAdapter::~PythonRuntimeAdapter() = default;

PythonRuntimeAdapter& PythonRuntimeAdapter::operator=(PythonRuntimeAdapter&& other) noexcept
{
    if (this != &other)
    {
        m_state = std::move(other.m_state);
    }
    return *this;
}

Result<PythonRuntimeAdapter> PythonRuntimeAdapter::create(const EngineContext& engine,
                                                          const PythonRuntimeOptions& options)
{
    Result<std::shared_ptr<PythonInterpreterState>> state = PythonInterpreterState::start(engine, options);
    if (!state.hasValue())
    {
        return Result<PythonRuntimeAdapter>{state.status()};
    }
    return Result<PythonRuntimeAdapter>{PythonRuntimeAdapter(std::move(*state))};
}

PythonAdapterCapabilities PythonRuntimeAdapter::capabilities() const noexcept
{
    return currentPythonAdapterCapabilities();
}

std::uint64_t PythonRuntimeAdapter::engineContextId() const noexcept
{
    return m_state != nullptr ? m_state->engineContextId() : 0u;
}

std::uint64_t PythonRuntimeAdapter::callsInFlight() const noexcept
{
    return m_state != nullptr ? m_state->callsInFlight() : 0u;
}

PythonCallCounters PythonRuntimeAdapter::counters() const noexcept
{
    return m_state != nullptr ? m_state->counters() : PythonCallCounters{};
}

Status PythonRuntimeAdapter::execute(const ExecutionContext& execution, std::string_view source)
{
    if (m_state == nullptr)
    {
        return pythonAdapterFailure(ErrorCode::InvalidState, PythonAdapterReason::AdapterShutDown,
                                    "PythonRuntimeAdapter::execute",
                                    "this adapter holds no interpreter; it was shut down or moved from");
    }
    // A copy of the reference count for the duration of the call, so an adapter destroyed on another
    // thread leaves the interpreter alive until this returns.
    const std::shared_ptr<PythonInterpreterState> state = m_state;
    return state->execute(execution, source);
}

Status PythonRuntimeAdapter::callImageFunction(const ExecutionContext& execution, const PythonImageCall& call)
{
    if (m_state == nullptr)
    {
        return pythonAdapterFailure(ErrorCode::InvalidState, PythonAdapterReason::AdapterShutDown,
                                    "PythonRuntimeAdapter::callImageFunction",
                                    "this adapter holds no interpreter; it was shut down or moved from");
    }
    const std::shared_ptr<PythonInterpreterState> state = m_state;
    return state->callImageFunction(execution, call);
}

Status PythonRuntimeAdapter::shutdown()
{
    if (m_state == nullptr)
    {
        return Status::success();
    }
    if (m_state->callsInFlight() != 0u)
    {
        return pythonAdapterFailure(ErrorCode::Busy, PythonAdapterReason::CallInFlight,
                                    "PythonRuntimeAdapter::shutdown", "a call into the interpreter is still running");
    }
    // Dropping the last reference is what finalises the interpreter and releases the lease; a call
    // that is still holding one finalises it when it returns instead.
    m_state.reset();
    return Status::success();
}

} // namespace an
