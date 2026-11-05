// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The machinery every AfterNightEngine fault scenario shares: where a fault is injected, how a
// sweep enumerates the places it can be injected at, and what is checked after every injected run.
//
// A fault scenario is not a test that makes one operation fail. It is a *sweep*: the subject is run
// once with nothing injected to find out how many injection points it has, and then once per point
// with the fault armed at that point. The reason is the one the fuzz campaign already learned - a
// hand-written failure case proves the one boundary somebody thought of, and the boundary that
// leaks, half-commits or deadlocks is the one nobody thought of. Sweeping removes the choice.
//
// **The injection point is an ordinal, not a place in the code.** An engine context is built over a
// resource that counts every allocation the engine asks it for, and the scenario arms the k-th one.
// That gives a dense, deterministic enumeration of interior points for any operation, without a
// single hook in the engine and without a debug build: the engine cannot tell an injected refusal
// from a machine that is out of memory, which is the whole point. Cancellation uses the same
// enumeration - the stop is requested *at* the k-th allocation - or, where a scenario says so, the
// k-th progress report.
//
// **What is checked after every run, spelled as four questions.** Did anything
// escape as an exception rather than being returned. Is every byte the resource handed out back.
// Does the destination hold a whole file or no file, with nothing left beside it. Did the run come
// back at all, inside the bound a deadlock would blow. A scenario adds the codes it will accept and
// the destination it writes, and the harness does the rest.
//
// **A sweep that injected nothing prints what a sweep that injected everywhere prints**, so every
// scenario reports how many points it found, how many injections actually fired and how many runs
// reached their point at all. That line is what the campaign runner reads, and a scenario whose
// injection never fired fails here rather than passing quietly.

#pragma once

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>
#include <AfterNightEngine/Runtime/ProgressSink.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <stop_token>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace an::faults
{

/// Which of the four kinds of injected fault - allocation, cancellation, I/O and external - a
/// scenario performs.
///
/// The kind is reported beside the scenario's counts and is what the campaign's registry groups by,
/// so a campaign that lost every I/O scenario is visible rather than merely smaller.
enum class FaultKind : std::uint8_t
{
    /// An allocation the engine asked for is refused.
    Allocation,

    /// A stop is requested while the operation is running.
    Cancellation,

    /// A read, a write, a completion or a publication fails.
    InputOutput,

    /// Something outside the engine throws or refuses at an adapter boundary.
    External
};

/// Where a sweep's injection points come from.
enum class PointSource : std::uint8_t
{
    /// The k-th allocation the engine asks its memory resource for. The default, because every
    /// non-trivial operation has some and they are dense enough to cover a pass rather than a stage.
    Allocations,

    /// The k-th progress report the operation delivers. For a subject whose stages are what a
    /// cancellation has to be tried between, rather than whose buffers are.
    ProgressReports
};

/// The name of a kind, as the registry and the report line spell it.
///
/// @param kind the kind to spell.
/// @return a literal with static storage duration.
[[nodiscard]] const char* spell(FaultKind kind) noexcept;

/// The memory resource a sweep puts underneath an engine context.
///
/// It is an ordinary caller-supplied resource - the one `Runtime/MemoryResource.h` says a test may
/// implement - with three additions: it counts the requests the engine makes, it can refuse a chosen
/// one, and it can request a stop at a chosen one. It also remembers every block it handed out, so
/// "nothing leaked" is a fact about this resource rather than a number the engine reports about
/// itself.
///
/// Thread-safe, because an engine context's operations allocate concurrently by design.
class InjectingMemoryResource final : public MemoryResource
{
public:
    /// A resource that refuses nothing and stops nothing.
    InjectingMemoryResource() = default;

    /// Releases nothing: the blocks belong to whoever asked for them.
    ~InjectingMemoryResource() override = default;

    /// Neither copied nor moved: one of these sits under one engine context.
    InjectingMemoryResource(const InjectingMemoryResource&) = delete;

    /// Neither copied nor moved. See the copy constructor.
    InjectingMemoryResource& operator=(const InjectingMemoryResource&) = delete;

    /// Refuses the ordinal-th request this resource is given.
    ///
    /// @param ordinal which request to refuse, counting from one. Zero refuses nothing.
    void refuseAt(std::uint64_t ordinal) noexcept;

    /// Requests a stop when the ordinal-th request arrives, and serves it normally.
    ///
    /// The stop is requested before the memory is handed back, so the operation observes it at its
    /// next checkpoint rather than at a boundary of the harness's choosing.
    ///
    /// @param ordinal which request to stop at, counting from one. Zero stops at none.
    /// @param source the stop source to request through. Borrowed; must outlive this resource.
    void stopAt(std::uint64_t ordinal, std::stop_source& source) noexcept;

    /// Takes memory from the platform, unless this request is the one that was armed.
    ///
    /// @param bytes how many bytes are needed.
    /// @param alignment the boundary the address must sit on.
    /// @return the address, or the refusal this resource was armed with.
    [[nodiscard]] Result<void*> allocate(std::uint64_t bytes, std::uint64_t alignment) override;

    /// Takes zero-filled memory under the same arming as `allocate`.
    ///
    /// @param bytes how many bytes are needed.
    /// @param alignment the boundary the address must sit on.
    /// @return the address, or the refusal this resource was armed with.
    [[nodiscard]] Result<void*> allocateZeroed(std::uint64_t bytes, std::uint64_t alignment) override;

    /// Gives memory back and forgets the block.
    ///
    /// A block this resource did not hand out, or a size that does not match what it handed out, is
    /// recorded as a mismatch rather than corrected - it is exactly the defect a fault sweep is
    /// looking for on a failure path.
    ///
    /// @param pointer what `allocate` or `allocateZeroed` returned, or null.
    /// @param bytes what was asked for.
    /// @param alignment what was asked for.
    void deallocate(void* pointer, std::uint64_t bytes, std::uint64_t alignment) noexcept override;

    /// What this resource has done.
    ///
    /// @return the counters.
    [[nodiscard]] MemoryCounters counters() const noexcept override;

    /// How many requests this resource has been given, refused ones included.
    ///
    /// @return the count, which is the number of injection points an allocation sweep has.
    [[nodiscard]] std::uint64_t requests() const noexcept;

    /// Whether the armed refusal or stop actually happened.
    ///
    /// @return true when the armed ordinal was reached.
    [[nodiscard]] bool fired() const noexcept;

    /// How many blocks this resource handed out and has not been given back.
    ///
    /// @return the count, which must be zero once the run's objects are destroyed.
    [[nodiscard]] std::uint64_t outstandingBlocks() const noexcept;

    /// How many bytes those blocks hold.
    ///
    /// @return the total requested size of the outstanding blocks.
    [[nodiscard]] std::uint64_t outstandingBytes() const noexcept;

    /// How many deallocations named a block or a size this resource did not hand out.
    ///
    /// @return the count, which must be zero.
    [[nodiscard]] std::uint64_t mismatchedReleases() const noexcept;

private:
    /// One block this resource handed out.
    struct Block
    {
        /// What the caller asked for, which is what it must give back.
        std::uint64_t bytes = 0u;

        /// The boundary it asked for.
        std::uint64_t alignment = 0u;
    };

    /// Serves one request under either initialization policy.
    ///
    /// @param bytes how many bytes are needed.
    /// @param alignment the boundary the address must sit on.
    /// @param zero whether every byte must be zero.
    /// @return the address, or the armed refusal.
    [[nodiscard]] Result<void*> serve(std::uint64_t bytes, std::uint64_t alignment, bool zero);

    /// Guards every member below; an engine context allocates from several threads at once.
    mutable std::mutex m_guard;

    /// The blocks handed out and not yet given back, by address.
    std::unordered_map<void*, Block> m_blocks;

    /// How many requests have arrived.
    std::uint64_t m_requests = 0u;

    /// Which request is refused, or zero.
    std::uint64_t m_refuseAt = 0u;

    /// Which request stops the operation, or zero.
    std::uint64_t m_stopAt = 0u;

    /// Where a stop is requested. Borrowed.
    std::stop_source* m_stop = nullptr;

    /// Whether the armed ordinal was reached.
    bool m_fired = false;

    /// What the engine's own counters would say, kept here because this resource is the one the
    /// engine's resource takes its bytes from.
    MemoryCounters m_counters{};

    /// How many releases named something this resource did not hand out.
    std::uint64_t m_mismatched = 0u;
};

/// A progress sink that counts reports and can request a stop at a chosen one.
///
/// Supplied to every subject whether or not it uses it: a scenario that enumerates its points by
/// progress report attaches it to its execution, and one that enumerates by allocation does not have
/// to care that it exists.
class TriggerProgressSink final : public ProgressSink
{
public:
    /// A sink that counts and stops nothing.
    TriggerProgressSink() = default;

    /// Requests a stop when the ordinal-th report arrives.
    ///
    /// @param ordinal which report to stop at, counting from one. Zero stops at none.
    /// @param source the stop source to request through. Borrowed; must outlive this sink.
    void stopAt(std::uint64_t ordinal, std::stop_source& source) noexcept;

    /// Counts the report, and requests the stop when this is the armed one.
    ///
    /// @param report what the operation delivered.
    void onProgress(const ProgressReport& report) noexcept override;

    /// How many reports arrived.
    ///
    /// @return the count, which is the number of injection points a progress sweep has.
    [[nodiscard]] std::uint64_t reports() const noexcept;

    /// Whether the armed report arrived.
    ///
    /// @return true when the armed ordinal was reached.
    [[nodiscard]] bool fired() const noexcept;

private:
    /// How many reports have arrived. Atomic: progress is delivered on the operation's threads.
    std::atomic<std::uint64_t> m_reports{0u};

    /// Which report stops the operation, or zero.
    std::uint64_t m_stopAt = 0u;

    /// Where a stop is requested. Borrowed.
    std::stop_source* m_stop = nullptr;

    /// Whether the armed ordinal was reached.
    std::atomic<bool> m_fired{false};
};

/// A directory of this campaign's own, removed when the scope ends.
///
/// Test-owned storage named by `AFTERNIGHT_TEST_STORAGE_ROOT` when a runner states one, and the
/// platform's temporary directory otherwise. Never the production tree, which is the rule every
/// suite in this repository follows; a scenario that writes files makes one of these per run, so a
/// sweep's residue check is about the run that is being checked and no other.
class ScopedDirectory
{
public:
    /// Creates a directory whose name nothing else in this process will choose.
    ///
    /// @param name what to call it, before the uniquifying suffix.
    explicit ScopedDirectory(std::string_view name);

    /// Removes the directory and everything in it.
    ~ScopedDirectory();

    /// Neither copied nor moved: it owns a directory.
    ScopedDirectory(const ScopedDirectory&) = delete;

    /// Neither copied nor moved. See the copy constructor.
    ScopedDirectory& operator=(const ScopedDirectory&) = delete;

    /// Where it is.
    ///
    /// @return the directory.
    [[nodiscard]] const std::filesystem::path& path() const noexcept;

    /// A file in it, spelled the way the engine's own path parameters are.
    ///
    /// @param name the file's name.
    /// @return the UTF-8 path.
    [[nodiscard]] std::string file(std::string_view name) const;

private:
    /// The directory this scope owns.
    std::filesystem::path m_path;
};

/// What one run of a subject is given.
///
/// A borrowed view of the run the harness built, valid for the subject call only.
struct RunContext
{
    /// The engine context, built over the injecting resource.
    const EngineContext& engine;

    /// The resource underneath it, armed for this run.
    InjectingMemoryResource& memory;

    /// The stop source a cancellation scenario requests through.
    std::stop_source& stop;

    /// The progress sink a scenario may attach to its execution.
    TriggerProgressSink& progress;

    /// Which ordinal this run is injecting at, or zero for the census run.
    std::uint64_t point = 0u;

    /// What a census run reports: how many places this subject can be made to fail.
    ///
    /// Left at zero by a subject whose points the harness enumerates for it - every allocation and
    /// cancellation scenario - and set by one that injects its own fault, because only the subject
    /// knows how many steps its codec has or how many frames its source will be asked for.
    std::uint64_t opportunities = 0u;

    /// What an injected run reports: whether its own injection fired.
    ///
    /// Left alone by a subject whose injection the harness arms; set by one that injects its own.
    bool fired = false;

    /// Whether this is the census run, which injects nothing and must succeed.
    [[nodiscard]] bool census() const noexcept
    {
        return point == 0u;
    }
};

/// What one run of a subject did, as the sweep needs to see it.
struct RunOutcome
{
    /// What the subject returned.
    Status status{};

    /// How many places the run said it could be made to fail, for a census run.
    std::uint64_t opportunities = 0u;

    /// Whether this run's injection fired.
    bool fired = false;
};

/// One run of the operation a scenario is about.
///
/// The subject builds its own execution context, runs the operation, destroys everything it created
/// and returns the operation's own answer. Destroying everything matters: the harness checks that
/// the resource is empty the moment the subject returns, so an image still held in a local is a
/// leak as far as the sweep is concerned - which is the right answer, because the sweep cannot tell
/// it from one the engine forgot.
using Subject = std::function<Status(RunContext&)>;

/// The two-state check every returned value owes, performed where the value's type is known.
///
/// `Result<T>` promises that exactly one of "holds a value" and "holds a failure" is true. A partial
/// result - a value beside a failed status, or a successful status with nothing in it - is the
/// untyped partial result the campaign exists to refuse, and it cannot be checked by the harness because
/// the harness never sees `T`.
///
/// @tparam ValueT what the operation produces.
/// @param result what it returned.
/// @return the result's status, for the subject to return.
template <typename ValueT> [[nodiscard]] Status settle(const Result<ValueT>& result)
{
    if (result.hasValue() != result.status().ok())
    {
        return Status::failure(ErrorCode::Internal, "a result held a value and a failure at once, or neither of them");
    }
    return result.status();
}

/// One scenario: a sweep of one subject with one kind of fault injected at every point it has.
///
/// Built on the stack of the test that is the scenario, run once, and destroyed - the destructor is
/// what prints the report line the campaign runner reads. Failures are reported through the test
/// framework as they are found, so a sweep of two hundred points names the point that broke rather
/// than stopping at it.
class Sweep
{
public:
    /// A scenario with a label, a kind and nothing swept yet.
    ///
    /// @param label the name the registry gives this scenario. Must outlive the sweep.
    /// @param kind which of the four injections it performs.
    Sweep(const char* label, FaultKind kind);

    /// Prints the report line and fails the test when the sweep proved nothing.
    ~Sweep();

    /// Neither copied nor moved: a sweep is a scope, not a value.
    Sweep(const Sweep&) = delete;

    /// Neither copied nor moved. See the copy constructor.
    Sweep& operator=(const Sweep&) = delete;

    /// The engine options every run is built with.
    ///
    /// The harness installs its own resource over whatever this carries, because that is the seam
    /// the whole campaign stands on; everything else - the concurrency, the limits, the codec
    /// registry - is the scenario's.
    ///
    /// @param options what to build each run's engine context with.
    void engineOptions(EngineContextOptions options);

    /// Where this sweep's injection points come from.
    ///
    /// @param source allocations, or progress reports.
    void pointsFrom(PointSource source) noexcept;

    /// A code an injected run may answer with.
    ///
    /// Checked against the failure and against its root cause, because a layer that noticed a
    /// refusal reports its own code and carries the refusal underneath.
    ///
    /// @param code the code to accept.
    void accept(ErrorCode code);

    /// A file this sweep publishes, whose existence is checked after every run.
    ///
    /// After a successful run it must exist; after a failed one it must not, and no other file may
    /// remain in its directory - a pending sibling left behind is a half-commit that a later reuse
    /// decision would read.
    ///
    /// @param path the destination path. Its directory is the one that is swept for residue.
    void destination(std::filesystem::path path);

    /// How many times each point is injected at.
    ///
    /// One by default, because a sweep of a deterministic subject learns nothing from the second
    /// run. A subject with background threads is not deterministic: one run of a point samples one
    /// interleaving of the failure path, and the interleaving is what a deadlock or a race lives in.
    ///
    /// @param times how many runs per point. Zero is read as one.
    void repeat(std::uint32_t times) noexcept;

    /// How long one run may take before it is a deadlock rather than a slow run.
    ///
    /// @param bound the per-run bound. The default is generous; a scenario with background threads
    ///        and a queue may want it tighter, because that is where a deadlock lives.
    void runBound(std::chrono::milliseconds bound) noexcept;

    /// Runs the census and then one injected run per point it found.
    ///
    /// A cancellation sweep has one point more than its census found, and it is the first one: a
    /// stop already requested before the operation is called at all. Cancellation is injected before
    /// start as well as during, because an operation that reserves before it first
    /// looks at its token does its work anyway. That first point is the one a cancellation sweep
    /// requires a refusal at; a later one may finish before it observes the stop, which is what
    /// `absorbed` counts.
    ///
    /// @param subject the operation under test.
    void run(const Subject& subject);

    /// How many injection points the census found.
    ///
    /// @return the count.
    [[nodiscard]] std::uint64_t points() const noexcept;

    /// How many injected runs actually reached their point.
    ///
    /// @return the count.
    [[nodiscard]] std::uint64_t reached() const noexcept;

    /// How many injected runs there were, counting the repeats of each point.
    ///
    /// @return the count.
    [[nodiscard]] std::uint64_t runs() const noexcept;

    /// How many injected runs answered with a failure this sweep accepts.
    ///
    /// @return the count.
    [[nodiscard]] std::uint64_t refused() const noexcept;

    /// How many injected runs fired their injection and still succeeded.
    ///
    /// Not a failure: an engine may legitimately recover from a refused allocation or finish before
    /// it observes a stop. It is reported because the two are told apart by the number, not by the
    /// outcome.
    ///
    /// @return the count.
    [[nodiscard]] std::uint64_t absorbed() const noexcept;

private:
    /// Builds one engine context over a fresh resource, arms it, runs the subject and checks it.
    ///
    /// @param subject the operation under test.
    /// @param point which ordinal to inject at, or zero for the census.
    /// @return what the run answered, how many points it found, and whether its injection fired.
    RunOutcome once(const Subject& subject, std::uint64_t point);

    /// Checks what one injected run left behind.
    ///
    /// @param point which ordinal was injected at.
    /// @param status what the subject returned.
    /// @param memory the resource underneath the run.
    /// @param elapsed how long the run took.
    void check(std::uint64_t point, const Status& status, const InjectingMemoryResource& memory,
               std::chrono::milliseconds elapsed);

    /// Whether a failure, or anything it was caused by, carries a code this sweep accepts.
    ///
    /// @param status the failure to examine.
    /// @return true when the code is accepted.
    [[nodiscard]] bool accepted(const Status& status) const;

    /// What is left in the destination's directory beside the destination itself.
    ///
    /// @return the names, sorted, for a failure message.
    [[nodiscard]] std::vector<std::string> residue() const;

    /// The registry's name for this scenario.
    const char* m_label;

    /// Which injection it performs.
    FaultKind m_kind;

    /// Where its points come from.
    PointSource m_source = PointSource::Allocations;

    /// What each run's engine context is built with.
    EngineContextOptions m_options{};

    /// The codes an injected run may answer with.
    std::vector<ErrorCode> m_accepted;

    /// The file this sweep publishes, if it publishes one.
    std::filesystem::path m_destination;

    /// How long one run may take.
    std::chrono::milliseconds m_bound{60000};

    /// How many times each point is injected at.
    std::uint32_t m_repeat = 1u;

    /// How many points the census found.
    std::uint64_t m_points = 0u;

    /// How many injected runs there were.
    std::uint64_t m_runs = 0u;

    /// How many injected runs reached their point.
    std::uint64_t m_reached = 0u;

    /// How many answered with an accepted failure.
    std::uint64_t m_refused = 0u;

    /// How many fired and succeeded anyway.
    std::uint64_t m_absorbed = 0u;

    /// How long every run took together. Microseconds because a run of this campaign is measured in
    /// them, and a report that said every scenario took zero seconds would be reporting nothing.
    std::chrono::microseconds m_elapsed{0};

    /// Whether `run` was called at all.
    bool m_ran = false;
};

} // namespace an::faults
