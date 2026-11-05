// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// An owned Python interpreter, with its lifetime said out loud.
///
/// An interpreter reached through a singleton - started on first use, its existence decided by a static
/// pointer, its global interpreter lock carried in file-scope state, and its shutdown whatever happens
/// when somebody destroys the instance - gives no way to ask whether starting one will work without
/// trying, no way to have the engine without the runtime, and no way for a caller to be told that the
/// interpreter it is about to use belongs to somebody else.
///
/// This adapter moves the lifetime into an object a caller holds:
///
/// **One adapter per process, and it says so.** CPython has one interpreter state per process, so
/// this is a property of the library rather than a policy this adapter chose. A second `create` is
/// refused with `PythonAdapterReason::AdapterAlreadyActive`, an interpreter this adapter did not
/// start is refused with `ForeignInterpreterActive`, and in a build with no runtime `create` refuses
/// with `RuntimeNotCompiledIn`, so no adapter exists to call. Each of those is a returned failure,
/// because each is something a correct caller can provoke.
///
/// **The adapter holds the engine context open.** It takes an `EngineContextLease`, so a context with
/// a live adapter refuses to shut down and the caller is told rather than finding out. A library with
/// process-wide state is held to one rule: the adapter provides the functionality and the engine
/// context owns the lifetime.
///
/// **The interpreter lives on a thread of its own.** CPython must be initialised and finalised on one
/// operating-system thread, so `create` starts the runtime on a thread the adapter owns, which then
/// waits without the interpreter lock until the adapter is finalised; calls enter from whichever
/// thread makes them.
///
/// **The global interpreter lock is acquired per call and released before the call returns.** Between
/// calls this adapter holds no lock, so the interpreter is reachable from another host thread; during
/// a call the lock serialises against every other holder, which is what makes two threads calling
/// this adapter at the same time correct rather than fast.
///
/// **Cancellation has a stated limit, and the limit is the interesting part.** CPython offers no safe
/// way for one thread to interrupt code running in another - the call that would do it needs the lock
/// the running code holds - so a cancellation this adapter observes is either observed before the
/// interpreter is entered or raised from inside it by a check that runs on the interpreter's own
/// thread. `PythonCancellationPolicy` is that choice, stated per adapter, with its cost named: a
/// per-line check can interrupt a Python loop and cannot interrupt one call into a C extension, and
/// tracing every line is not free.

#pragma once

#include <AfterNightEnginePythonAdapter/Api.h>
#include <AfterNightEnginePythonAdapter/PythonAdapterCapabilities.h>
#include <AfterNightEnginePythonAdapter/PythonBufferBridge.h>

#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <utility>

namespace an
{

class EngineContext;
class ExecutionContext;

/// The interpreter this adapter owns, declared here and defined only inside the adapter.
///
/// Named in this header so that an adapter handle can hold a pointer to one; it is never defined in
/// the published surface. A caller can do nothing with the name, which is the point: what it holds
/// is a thread state, a lock and a lease, and a `PyThreadState` must not cross into a header a
/// consumer compiles without Python installed.
class PythonInterpreterState;

/// When a cancellation request reaches the interpreter.
///
/// Stated once per adapter rather than per call, because the cheaper policy is a property of how the
/// adapter was built rather than of one call site's patience, and because installing and removing a
/// trace hook per call would charge every call for the option.
enum class PythonCancellationPolicy : std::uint8_t
{
    /// Cancellation is observed at the boundary: before the interpreter is entered, and again when it
    /// returns. Python code already running is not interrupted, so a call that does not return does
    /// not end. Nothing is installed in the interpreter, so a call costs exactly what it costs.
    AtBoundary,

    /// Cancellation is observed at the boundary and between Python statements. This adapter installs
    /// a trace function for the duration of a call, which raises inside the interpreter when the stop
    /// token is set or the deadline has passed, and the call then reports `ErrorCode::Cancelled` or
    /// `ErrorCode::DeadlineExceeded` with `PythonAdapterReason::PythonInterrupted`.
    ///
    /// Two costs, both real: tracing slows Python code down by a factor rather than a percentage, and
    /// a single statement that spends its time inside a C extension - one NumPy call over a whole
    /// image - is one statement, so it is not interrupted either.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    PerLine
};

/// Whether a value is one of the cancellation policies this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param policy the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isPythonCancellationPolicy(PythonCancellationPolicy policy) noexcept
{
    return static_cast<std::uint8_t>(policy) <= static_cast<std::uint8_t>(PythonCancellationPolicy::PerLine);
}

/// Everything a caller states when starting an interpreter.
///
/// A plain aggregate with a default for every member, so a caller names only what it is changing.
/// Every string is borrowed for the duration of `create` and copied into the runtime's own
/// configuration; nothing here is retained by reference afterwards.
struct PythonRuntimeOptions
{
    /// Where the standard library lives, as Python's own `home` setting.
    ///
    /// Empty leaves the runtime to resolve its own installation, which is what a development host or
    /// a system interpreter wants. A deployment that ships its own Python states this, because a
    /// resolution that searches is a resolution that can find the wrong one; a caller that wants a
    /// path such as `<executable directory>/PythonLib` states it rather than this adapter guessing.
    std::string_view pythonHome{};

    /// What the interpreter calls the program it is embedded in, as Python's own `program_name`.
    std::string_view programName{"AfterNight"};

    /// Directories appended to `sys.path` after the interpreter starts, in order.
    ///
    /// Borrowed for the duration of `create`. An empty entry is refused rather than ignored, because
    /// an empty search path entry means the current directory and nobody asks for that on purpose.
    std::span<const std::string_view> modulePaths{};

    /// When a cancellation request reaches the interpreter.
    PythonCancellationPolicy cancellation = PythonCancellationPolicy::AtBoundary;
};

/// One call into Python over buffers this engine owns.
///
/// The Python side is handed one read-only buffer and one writable buffer and writes its result
/// through the second one; it returns nothing. That shape is deliberate rather than a limitation of
/// what this adapter could marshal: a function that returned an array would return an object whose
/// memory Python owns, and adopting that into an image is a copy this engine will not make quietly.
///
/// Both descriptions come from `describeAsPythonBuffer`, so what reaches the interpreter has already
/// been checked against the engine's bridge rules. They borrow the samples they point at, and the
/// borrow must outlive the call - `BorrowScope::SingleCall` is the contract that says so.
struct PythonImageCall
{
    /// The module to import, by the name `import` would use. `"__main__"` is the module code run
    /// through `execute` defines its functions in.
    std::string_view moduleName;

    /// The attribute of that module to call. It must be callable and take exactly the two buffers.
    std::string_view functionName;

    /// The samples Python reads. Its `readOnly` field must be set, because a read-only borrow is what
    /// this call lends; a writable description is refused as
    /// `PythonAdapterReason::BufferPermissionMismatch` rather than quietly narrowed.
    PythonBufferDescription source;

    /// The samples Python writes. Its `readOnly` field must be clear, refused the same way.
    ///
    /// Describing the same samples as both is not refused: Python then holds two memoryviews over one
    /// buffer, which is what an in-place operation is, and whether that is meaningful is the caller's
    /// decision rather than this adapter's.
    PythonBufferDescription destination;
};

/// What this adapter has done since it started, for a report or a regression test.
///
/// Counters rather than a log: they are cheap, they are always on, and they answer the two questions
/// an optional adapter's cost raises - how much memory crossed the boundary, and how much of it was
/// copied.
struct PythonCallCounters
{
    /// How many calls into the interpreter have been made through this adapter.
    std::uint64_t calls = 0u;

    /// How many bytes of engine samples have been shared with the interpreter without copying: both
    /// buffers' logical bytes, counted for each image call that succeeded.
    std::uint64_t sharedBytes = 0u;

    /// How many bytes of engine samples have been copied to reach the interpreter.
    ///
    /// Always zero, and it is a counter rather than a comment for that reason: this adapter shares
    /// or refuses, and never converts. A build where this stops being zero has grown a copy nobody
    /// asked for, and a test can say so.
    std::uint64_t copiedBytes = 0u;

    /// How many calls were refused before reaching the interpreter because a buffer could not be
    /// lent, whether by the buffer bridge or by the rule on each borrow's write permission.
    std::uint64_t bridgeRefusals = 0u;

    /// How many calls ended because the caller stopped them or their deadline passed, before the
    /// interpreter was entered or while it ran.
    std::uint64_t cancellations = 0u;

    /// How many calls ended because Python raised an exception this adapter did not recognise as one
    /// of its own reasons - a missing module, attribute or callable, or an unexpected return value.
    std::uint64_t pythonExceptions = 0u;
};

/// An owned Python interpreter, and the calls that run in it.
///
/// Built through `create`, which validates what it is given and reports why it refused. Move-only,
/// because a copy would be two owners of one interpreter. Nothing holds one on a caller's behalf:
/// there is no instance accessor, no lazy start and no process-wide default - which is the whole
/// difference from the interface this replaces.
///
/// The calls are thread-safe: several threads may call one adapter, and the global interpreter lock
/// serialises them. What is not thread-safe, as for any other object, is `shutdown()` or a move while
/// another thread is calling.
///
/// Destroying an adapter while a call is running is a defect in the caller, and the answer to it is a
/// late release rather than a use-after-free: the interpreter is finalised when the last call
/// returns. `shutdown()` is the checked half, and it refuses while anything is in flight.
class PythonRuntimeAdapter
{
public:
    /// An adapter that holds nothing.
    ///
    /// The state one is left in by a move and by `shutdown()`, and a usable default for a member
    /// assigned later. `valid()` is how to ask.
    PythonRuntimeAdapter() noexcept = default;

    /// Finalises the interpreter, unless a call is still running in it.
    AFTERNIGHTENGINEPYTHONADAPTER_API ~PythonRuntimeAdapter();

    /// Takes over another adapter's interpreter, leaving it holding nothing.
    ///
    /// Allocation-free. The interpreter is not restarted and the lease is not taken again.
    ///
    /// @param other the adapter to take over. Left holding nothing.
    PythonRuntimeAdapter(PythonRuntimeAdapter&& other) noexcept = default;

    /// Releases this adapter's interpreter and takes over another's.
    ///
    /// @param other the adapter to take over. Left holding nothing.
    /// @return this adapter.
    AFTERNIGHTENGINEPYTHONADAPTER_API PythonRuntimeAdapter& operator=(PythonRuntimeAdapter&& other) noexcept;

    /// Copying is deleted: a copy would be a second owner of one process-wide interpreter.
    PythonRuntimeAdapter(const PythonRuntimeAdapter&) = delete;

    /// Copying is deleted. See the copy constructor.
    PythonRuntimeAdapter& operator=(const PythonRuntimeAdapter&) = delete;

    /// Starts an interpreter in this process, or reports why it cannot.
    ///
    /// The options are validated before anything is started, so a configuration mistake costs
    /// nothing, and then the process slot, a foreign interpreter and the engine context's lease are
    /// checked in that order. A build with no Python runtime answers `RuntimeNotCompiledIn` before
    /// any of it: there is nothing for the other rules to decide. A lease on the engine context is
    /// taken before the interpreter comes up and released if it does not, so a refused start leaves
    /// no hold behind.
    ///
    /// Thread-safe: two threads may call this at once and exactly one of them gets an adapter.
    /// Allocates the adapter's state, and whatever the Python runtime allocates for itself.
    ///
    /// @param engine the engine context this adapter belongs to. It must hold something, and it
    ///        cannot be shut down while the adapter lives.
    /// @param options what the caller is configuring. Copied; nothing in it is retained by reference.
    /// @return the adapter, or a failure carrying a `ContextKey::AdapterReason` field.
    ///         `ErrorCode::Unsupported` with `RuntimeNotCompiledIn` when this build has no runtime;
    ///         `ErrorCode::AlreadyInitialized` with `AdapterAlreadyActive` when this process already
    ///         has one; `ErrorCode::InvalidState` with `ForeignInterpreterActive` when an interpreter
    ///         this adapter did not start is running; `ErrorCode::InvalidArgument` with
    ///         `InterpreterConfigurationRejected` when an option is not usable;
    ///         `ErrorCode::NotInitialized` when `engine` holds nothing, carrying the lease refusal as
    ///         its cause; `ErrorCode::ExternalFailure` with `InterpreterStartupFailed` when the
    ///         runtime refuses to start.
    [[nodiscard]] static AFTERNIGHTENGINEPYTHONADAPTER_API Result<PythonRuntimeAdapter>
    create(const EngineContext& engine, const PythonRuntimeOptions& options);

    /// Whether this adapter holds an interpreter.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return false for a default-constructed adapter, one that has been moved from, and one that
    ///         has been shut down.
    [[nodiscard]] bool valid() const noexcept
    {
        return m_state != nullptr;
    }

    /// What this adapter can do, reported the way `pythonAdapterCapabilities()` reports it.
    ///
    /// For the caller that already holds an adapter and should not have to know a free function
    /// exists. A valid adapter reports `PythonAdapterState::Loaded` and the runtime version it
    /// started.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return this library's capabilities, with the state this adapter puts the process in.
    [[nodiscard]] AFTERNIGHTENGINEPYTHONADAPTER_API PythonAdapterCapabilities capabilities() const noexcept;

    /// Which engine context this adapter is holding open.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return the context's identity, or `0` when this adapter holds nothing.
    [[nodiscard]] AFTERNIGHTENGINEPYTHONADAPTER_API std::uint64_t engineContextId() const noexcept;

    /// How many calls into the interpreter are in progress right now, whether running or waiting for
    /// the global interpreter lock.
    ///
    /// A diagnostic number rather than a decision: another thread may enter or leave a call
    /// immediately afterwards. `shutdown()` is the call that decides without a gap between asking and
    /// acting.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return the number of calls in flight, or `0` when this adapter holds nothing.
    [[nodiscard]] AFTERNIGHTENGINEPYTHONADAPTER_API std::uint64_t callsInFlight() const noexcept;

    /// What this adapter has done since it started.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return the counters, or every field zero when this adapter holds nothing.
    [[nodiscard]] AFTERNIGHTENGINEPYTHONADAPTER_API PythonCallCounters counters() const noexcept;

    /// Runs Python source in the interpreter's `__main__` module.
    ///
    /// The entry point a caller uses to define what it will then call, and to do the `sys.path` work
    /// a deployment needs. The source is compiled and executed as a module body, so a definition it
    /// makes is reachable afterwards by the module name `"__main__"`.
    ///
    /// Thread-safe; serialised against every other call through the global interpreter lock.
    ///
    /// @param execution the operation this runs under. Its stop token and deadline are observed as
    ///        `PythonCancellationPolicy` states - at the boundary, and under `PerLine` between
    ///        statements too - and its diagnostic sink is not written to: what went wrong is the
    ///        returned failure.
    /// @param source the Python source to run. Borrowed for the duration of the call.
    /// @return success, or a failure. `ErrorCode::InvalidState` with `AdapterShutDown` when this
    ///         adapter holds nothing; `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` when the
    ///         caller stopped it or its deadline passed - with `PythonInterrupted` once the
    ///         interpreter had been entered, and with no adapter reason when the operation was
    ///         already stopped before; `ErrorCode::ExternalFailure` with `PythonExceptionRaised` when
    ///         Python raised, with its class and message in the failure's message.
    [[nodiscard]] AFTERNIGHTENGINEPYTHONADAPTER_API Status execute(const ExecutionContext& execution,
                                                                   std::string_view source);

    /// Calls a Python function over two borrowed engine buffers.
    ///
    /// The source buffer reaches Python as a read-only memoryview and the destination as a writable
    /// one, both describing the engine's own samples with no copy in either direction. The function
    /// is expected to write through the destination and return `None`; anything else it returns is
    /// reported as `PythonAdapterReason::UnexpectedResult`, because a returned array would be memory
    /// Python owns.
    ///
    /// Thread-safe; serialised against every other call through the global interpreter lock.
    ///
    /// @param execution the operation this runs under, on the same terms as `execute`.
    /// @param call which function to call and which samples to lend it. Both descriptions must have
    ///        come from `describeAsPythonBuffer`, and both borrows must outlive the call.
    /// @return success, or a failure. `ErrorCode::InvalidArgument` with `BufferPermissionMismatch`
    ///         when a borrow's write permission is not what this call lends, or with
    ///         `BufferBridgeRefused` when a description is not one this adapter will share - a null
    ///         base, a format or item size it does not represent, an axis count outside two or
    ///         three, a shape or stride that is not positive, a strided fastest axis, or a length that
    ///         is not the logical sample bytes; `ErrorCode::InvalidState` with `AdapterShutDown`;
    ///         `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded`, on the terms `execute` states;
    ///         `ErrorCode::ExternalFailure` with `ModuleNotFound`, `AttributeNotFound`, `NotCallable`,
    ///         `PythonExceptionRaised` or `UnexpectedResult`.
    [[nodiscard]] AFTERNIGHTENGINEPYTHONADAPTER_API Status callImageFunction(const ExecutionContext& execution,
                                                                             const PythonImageCall& call);

    /// Finalises the interpreter, and says whether it could.
    ///
    /// The checked half of destruction: an adapter with a call in flight refuses rather than pulling
    /// the interpreter out from under it. On success this adapter holds nothing afterwards, as though
    /// it had been moved from, and the process is back to `PythonAdapterState::Available`.
    ///
    /// Idempotent: shutting down an adapter that holds nothing succeeds, so cleanup that shuts one
    /// down and then lets a destructor do it again is correct rather than an error to report.
    ///
    /// Not thread-safe with respect to this adapter, which is being modified. Allocates only the
    /// failure it reports.
    ///
    /// @return success, or `ErrorCode::Busy` with `PythonAdapterReason::CallInFlight` when a call is
    ///         still running - in which case nothing has been finalised and the adapter is exactly as
    ///         it was.
    [[nodiscard]] AFTERNIGHTENGINEPYTHONADAPTER_API Status shutdown();

private:
    /// Takes ownership of a state the factory has already built and started.
    ///
    /// @param state the interpreter and the lease that holds its engine context open. Never null on
    ///        this path.
    explicit PythonRuntimeAdapter(std::shared_ptr<PythonInterpreterState> state) noexcept : m_state(std::move(state)) {}

    /// What this adapter owns, or null when it owns nothing. Shared rather than unique so that a call
    /// running on another thread keeps the interpreter alive past a handle destroyed too early.
    std::shared_ptr<PythonInterpreterState> m_state;
};

} // namespace an
