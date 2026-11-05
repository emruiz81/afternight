// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Whether there is a Python runtime here, and what went wrong.
///
/// The engine this adapter sits beside links no Python runtime, so nothing about the engine's
/// capabilities changes when this library is present or absent. What a caller needs instead is a way
/// to ask this library what it can do, and the answer has four states rather than two: the runtime
/// may be missing from the build entirely, present and startable, running because this adapter
/// started it, or refused when this adapter tried. Collapsing those into a boolean is what makes an
/// absent optional subsystem look like a broken one.
///
/// `Absent` is not a hypothetical. A build regenerated with AFTERNIGHT_PYTHON=0 contains this library
/// with no Python runtime in it: this report answers, and starting an interpreter refuses by name
/// rather than failing to load or crashing at the call site. That configuration exists so the
/// refusal path is built and tested rather than reasoned about, which is the whole of what "an
/// adapter that is not present is an ordinary outcome" has to mean in code.
///
/// Nothing here names a Python type, and this header includes no Python header. A consumer compiles
/// against it with no Python toolchain installed - that is the property that makes the dependency
/// optional in a build rather than only in a document.

#pragma once

#include <AfterNightEnginePythonAdapter/Api.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>

#include <cstdint>

namespace an
{

/// What state this adapter's Python runtime is in.
///
/// The four answers are deliberately separate, because a caller does something different about each
/// one: an absent runtime is a build decision, an available one is something to start, a loaded one
/// is something to use, and a refused one is something to report with the reason beside it.
enum class PythonAdapterState : std::uint8_t
{
    /// This build of the adapter contains no Python runtime. Nothing can start one:
    /// `PythonRuntimeAdapter::create` refuses with `PythonAdapterReason::RuntimeNotCompiledIn`, so
    /// every handle in this build is empty, and a call on an empty handle is refused with
    /// `AdapterShutDown`, as in any build.
    Absent,

    /// A Python runtime is compiled in and no interpreter is running in this process yet.
    Available,

    /// This adapter started an interpreter and it is running. Only one adapter in a process reaches
    /// this state, because CPython has one interpreter state per process.
    Loaded,

    /// Starting the interpreter was attempted and refused. The reason that refused it is reported
    /// beside this state rather than remembered by the caller.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    FailedToLoad
};

/// Why this adapter refused, in its own vocabulary.
///
/// An `ErrorCode` says what kind of failure it was and this says which of the adapter's own rules
/// produced it, so a caller branches on a value rather than on the text of a message. It travels on
/// a failure as a `ContextKey::AdapterReason` field, which is why it is an enumeration the engine
/// stores as a count and never interprets.
///
/// A Python exception's own type is not in here on purpose. The engine's vocabulary is closed and
/// Python's is not, so an exception's class name is carried in the failure's message, for a person,
/// while the branchable fact is that Python raised at all.
enum class PythonAdapterReason : std::uint16_t
{
    /// No adapter-specific reason. The value a failure carries when it came from somewhere else.
    None,

    /// This build of the adapter contains no Python runtime.
    RuntimeNotCompiledIn,

    /// A Python interpreter is already running in this process and this adapter did not start it.
    /// Initialising a second one in the same process is not something CPython supports, and taking
    /// over somebody else's is not something this adapter will do.
    ForeignInterpreterActive,

    /// This process already has a live adapter. One interpreter per process is CPython's rule, so
    /// the second adapter is refused rather than silently sharing the first one's interpreter.
    AdapterAlreadyActive,

    /// The interpreter refused to start. What it said is carried in the failure's message.
    InterpreterStartupFailed,

    /// A stated option is not one the adapter or the runtime accepts - a cancellation policy outside
    /// its enumeration, an empty module search path, a Python home or program name the runtime cannot
    /// decode, a module search path it cannot append to `sys.path`. A Python home that decodes but
    /// holds no standard library is found only when the runtime starts, as `InterpreterStartupFailed`.
    InterpreterConfigurationRejected,

    /// The call was made on an adapter that has been shut down or moved from.
    AdapterShutDown,

    /// Shutting down was refused because a call into the interpreter is still running.
    CallInFlight,

    /// The Python code raised. Its class name and message are in the failure's message; the
    /// traceback is not captured, because a traceback is a diagnostic for a person and this failure
    /// is a value.
    PythonExceptionRaised,

    /// The call was stopped once the interpreter had been entered: this adapter's per-line check
    /// raised in it, or the stop or deadline was found when it returned. Reported with the
    /// execution's own code - `ErrorCode::Cancelled` for a requested stop,
    /// `ErrorCode::DeadlineExceeded` for a deadline. This reason also accompanies a stop observed
    /// before entering the interpreter.
    PythonInterrupted,

    /// The named module, or a package on its dotted path, does not exist. A module that exists and
    /// fails to import - including one whose own import of another module fails - is reported as
    /// `PythonExceptionRaised` instead.
    ModuleNotFound,

    /// The module has no attribute of that name.
    AttributeNotFound,

    /// The named attribute exists and is not callable.
    NotCallable,

    /// A buffer could not be shared with Python as it stands. Which rule refused is a
    /// `BridgeRefusal` from `PythonBufferBridge.h`, named in the failure's message.
    BufferBridgeRefused,

    /// A lent buffer's write permission is not what the call requires: a source is lent read-only and
    /// a destination writable. Its own reason rather than a `BridgeRefusal`, because the bridge's
    /// alphabet describes what two memory models can express and this is a rule of this call.
    BufferPermissionMismatch,

    /// The call returned something other than `None` where this adapter's contract says the Python
    /// side writes through the buffer it was handed rather than returning a value.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    UnexpectedResult
};

/// A Python runtime's version, as three numbers.
///
/// Separate from the engine's own version type because it describes somebody else's release and is
/// compared against nothing of the engine's. Every member is zero when no runtime is compiled in.
struct PythonRuntimeVersion
{
    /// The major version - `3` for every CPython this adapter supports.
    int major = 0;

    /// The minor version.
    int minor = 0;

    /// The patch version, which is `micro` in CPython's own spelling.
    int patch = 0;

    /// Whether two versions are the same release.
    ///
    /// @param left one version.
    /// @param right the other.
    /// @return true when all three numbers are equal.
    [[nodiscard]] friend constexpr bool operator==(const PythonRuntimeVersion& left,
                                                   const PythonRuntimeVersion& right) noexcept = default;
};

/// What this adapter can do, in this build and in this process, right now.
///
/// Cheap to ask and safe to ask at any time, including before anything has been constructed: that
/// is what a discovery report is for. Two of its members are properties of the build and do not
/// change; the state and the failure describe the process and do.
struct PythonAdapterCapabilities
{
    /// Which of the four states this adapter is in.
    PythonAdapterState state = PythonAdapterState::Absent;

    /// Whether a Python runtime is compiled into this build of the adapter at all.
    ///
    /// A property of the binary rather than of the process, so it answers the question a deployment
    /// asks - "is this the enabled build?" - without a caller inferring it from a state that also
    /// depends on what is running.
    bool runtimeCompiledIn = false;

    /// The runtime this adapter was compiled against, from Python's own headers. Every member is
    /// zero when no runtime is compiled in.
    PythonRuntimeVersion buildVersion{};

    /// The runtime that is loaded, as the library itself reports it. Zero unless an interpreter this
    /// adapter started is running; the two versions differing is how a mismatched deployment is
    /// found, because CPython's binary interface is version-specific.
    PythonRuntimeVersion runtimeVersion{};

    /// Whether an interpreter this adapter did not start is running in this process.
    ///
    /// Not a failure on its own - an application embedding its own Python is a legitimate host - but
    /// it is why `create` will refuse, so it is reported before the attempt rather than after.
    bool foreignInterpreter = false;

    /// The code of the last start in this process refused after the engine context's lease was
    /// taken, which is where the runtime is asked to start, or `ErrorCode::Ok` when there has been
    /// none or a later start succeeded. With a runtime compiled in it is set only in
    /// `PythonAdapterState::FailedToLoad`; in the `Absent` build a refused `create` records
    /// `ErrorCode::Unsupported` here while the state stays `Absent`.
    ErrorCode loadFailure = ErrorCode::Ok;

    /// Which of this adapter's rules refused the start `loadFailure` records, or
    /// `PythonAdapterReason::None`.
    PythonAdapterReason loadReason = PythonAdapterReason::None;
};

/// What this adapter can do, asked of the library rather than of an object.
///
/// The entry point a consumer calls before deciding whether to ask for an adapter at all. It starts
/// nothing, loads nothing and cannot fail: a build with no Python runtime answers `Absent` and a
/// process with somebody else's interpreter answers `Available` with `foreignInterpreter` set, which
/// is a more useful answer than a refusal would be.
///
/// Thread-safe and allocation-free.
///
/// @return the state, the build and runtime versions, and the last startup refusal in this process.
[[nodiscard]] AFTERNIGHTENGINEPYTHONADAPTER_API PythonAdapterCapabilities pythonAdapterCapabilities() noexcept;

/// The state's own name, for a log line, a diagnostic field or a test failure message.
///
/// The spelling of the enumerator: `PythonAdapterState::FailedToLoad` is `"FailedToLoad"`.
///
/// Thread-safe and allocation-free.
///
/// @param state the state to name. A value outside the enumeration is named
///        `"UnknownPythonAdapterState"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINEPYTHONADAPTER_API const char* pythonAdapterStateName(PythonAdapterState state) noexcept;

/// The reason's own name, which is what a person reads to find out which rule refused.
///
/// Thread-safe and allocation-free.
///
/// @param reason the reason to name. A value outside the enumeration is named
///        `"UnknownPythonAdapterReason"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINEPYTHONADAPTER_API const char*
pythonAdapterReasonName(PythonAdapterReason reason) noexcept;

/// Which of this adapter's rules a failure came from.
///
/// Reads the typed context field the adapter attaches, so a caller branches on a value instead of
/// parsing a message. A failure from anywhere else - the engine, another library - carries none and
/// is reported as `PythonAdapterReason::None`.
///
/// Thread-safe and allocation-free.
///
/// @param status the failure to read. A successful status carries no reason.
/// @return the reason, or `PythonAdapterReason::None` when the failure does not carry one - which is
///         also the answer for a value outside this build's enumeration, because a reason this build
///         cannot name is not one it can branch on.
[[nodiscard]] AFTERNIGHTENGINEPYTHONADAPTER_API PythonAdapterReason pythonAdapterReason(const Status& status) noexcept;

/// Whether a value is one of the states this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param state the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isPythonAdapterState(PythonAdapterState state) noexcept
{
    return static_cast<std::uint8_t>(state) <= static_cast<std::uint8_t>(PythonAdapterState::FailedToLoad);
}

/// Whether a value is one of the reasons this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param reason the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isPythonAdapterReason(PythonAdapterReason reason) noexcept
{
    return static_cast<std::uint16_t>(reason) <= static_cast<std::uint16_t>(PythonAdapterReason::UnexpectedResult);
}

} // namespace an
