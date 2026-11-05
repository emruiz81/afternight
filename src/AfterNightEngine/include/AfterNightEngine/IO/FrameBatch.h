// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Running independent per-frame jobs in parallel under one execution, with a complete report of every
/// job that began.
///
/// A batch is a list of jobs - a token and two paths each - and one caller-supplied operation that
/// processes a job: typically reading a frame, working on it and publishing its output through a
/// transaction of its own. `executeFrameBatch` runs the jobs on the execution's admitted workers, never
/// through a process-wide cap, and returns only after every job that began has finished. The report
/// therefore holds a record for each of those jobs - a cancellation never loses one whose output was
/// published - and counts the jobs that never began. A job's failure is a value in the report rather
/// than the call's failure, and `FrameBatchFailurePolicy` decides whether it stops the jobs after it.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace an
{

class EngineContext;
class ExecutionContext;

/// One caller-owned unit of independent frame work.
///
/// Paths are identities and destinations for the callback; the engine neither opens nor interprets
/// them. They are borrowed until `executeFrameBatch` returns.
struct FrameBatchJob
{
    /// Opaque caller token copied to the completion record.
    std::uint64_t token = 0u;

    /// Input path or other UTF-8 source identity.
    std::string_view inputPath{};

    /// Output path or other UTF-8 destination identity.
    std::string_view outputPath{};
};

/// What the executor does after one independent job fails.
enum class FrameBatchFailurePolicy : std::uint8_t
{
    /// No job after the failed one in input order is started; jobs before it, and jobs already running,
    /// still finish and are reported.
    Stop = 0u,

    /// Attempt every job and retain every per-job result.
    Continue = 1u
};

/// Bounds and failure policy for an independent-frame batch.
struct FrameBatchOptions
{
    /// Maximum number of jobs accepted in one call.
    std::uint64_t maximumJobs = 1000000u;

    /// Whether a per-job failure stops later admission.
    FrameBatchFailurePolicy failurePolicy = FrameBatchFailurePolicy::Stop;
};

/// The operation one batch job performs.
///
/// A callback may read, calibrate, detect, register and atomically publish its output by composing
/// the domain operations. Different jobs run concurrently inside the execution's admitted arena,
/// so implementations must be safe for concurrent calls and must keep mutable per-job state local.
class FrameBatchOperation
{
public:
    /// Releases caller-owned operation state.
    virtual ~FrameBatchOperation() = default;

    /// Executes one independent job.
    ///
    /// Exceptions are translated at the callback boundary. A successful return means every effect
    /// the job promises, including any output publication, is complete before the completion record
    /// is made visible.
    ///
    /// @param engine allocation owner, borrowed for the call.
    /// @param execution operation policy and permission, borrowed for the call.
    /// @param job caller token and paths, borrowed for the call.
    /// @return success or a typed per-job failure.
    [[nodiscard]] virtual Status run(const EngineContext& engine, const ExecutionContext& execution,
                                     const FrameBatchJob& job) = 0;
};

/// The settled outcome of one job that began.
struct FrameBatchCompletion
{
    /// Input position in the submitted job span.
    std::uint64_t jobIndex = 0u;

    /// Opaque token copied from the job.
    std::uint64_t token = 0u;

    /// Owned copy of the input identity.
    std::string inputPath{};

    /// Owned copy of the output identity.
    std::string outputPath{};

    /// What the callback returned, including cancellation.
    Status status{};

    /// Dense scheduler worker slot that ran the job.
    std::uint32_t workerSlot = 0u;

    /// Calling-thread CPU time spent inside the callback, in nanoseconds.
    std::uint64_t workerCpuNanoseconds = 0u;
};

/// Settled counts for one independent-frame batch.
struct FrameBatchCounters
{
    /// Jobs offered by the caller.
    std::uint64_t jobsSubmitted = 0u;

    /// Jobs whose callback began.
    std::uint64_t jobsStarted = 0u;

    /// Jobs whose callback succeeded.
    std::uint64_t jobsSucceeded = 0u;

    /// Jobs whose callback failed for a reason other than cancellation or deadline.
    std::uint64_t jobsFailed = 0u;

    /// Jobs whose callback returned cancellation or deadline expiry.
    std::uint64_t jobsCancelled = 0u;

    /// Jobs not admitted after a stop, deadline or stop-on-failure decision.
    std::uint64_t jobsNotStarted = 0u;

    /// Sum of callback CPU time across scheduler workers, in nanoseconds.
    std::uint64_t workerCpuNanoseconds = 0u;

    /// How many stable scheduler worker slots the batch planned to use.
    std::uint32_t plannedWorkers = 0u;
};

/// The complete report returned after every admitted callback has finished.
struct FrameBatchReport
{
    /// Started-job records in input order; never an unbounded hidden queue.
    std::vector<FrameBatchCompletion> completions{};

    /// Settled counts over the same run.
    FrameBatchCounters counters{};

    /// Success when every job succeeded; otherwise the first failure in input order, or the
    /// operation's cancellation when later jobs were not admitted.
    Status outcome{};
};

/// Runs independent per-frame jobs under one context budget and returns every settled completion.
///
/// Jobs are partitioned through `TaskScheduler`, never through a process-wide cap. The result array
/// is allocated once and bounded by `FrameBatchOptions::maximumJobs`; callbacks write disjoint
/// slots, and the caller receives compact completion records in input order only after all workers
/// have joined. Thus cancellation cannot lose a job that already published: its record is drained
/// into the returned report before this function returns. Jobs that never began have no completion
/// and are counted explicitly.
///
/// The callback owns output naming and publication. Its `outputPath` travels unchanged into the
/// completion record, so a batch of one-file transactions returns a token, a path and an error per job
/// without putting filesystem conventions into the engine.
///
/// @param engine live allocation owner; must match `execution`.
/// @param execution operation policy and context lease, borrowed until all work has joined.
/// @param jobs caller-filtered eligible jobs; borrowed until return.
/// @param operation thread-safe callback, exclusively borrowed by this run.
/// @param options job bound and per-frame failure policy.
/// @return the settled report, including per-frame failures as values; `ErrorCode::NotInitialized`
///         for an empty context or execution; `ErrorCode::IncompatibleArguments` for different
///         contexts; `ErrorCode::InvalidArgument` for a zero bound, an out-of-range policy or a path
///         that is empty, longer than 131072 bytes, malformed UTF-8 or contains NUL;
///         `ErrorCode::LimitExceeded` when the job count exceeds the bound; or, before any callback,
///         `ErrorCode::AllocationFailed` when the report cannot be allocated.
[[nodiscard]] AFTERNIGHTENGINE_API Result<FrameBatchReport>
executeFrameBatch(const EngineContext& engine, const ExecutionContext& execution, std::span<const FrameBatchJob> jobs,
                  FrameBatchOperation& operation, const FrameBatchOptions& options = {});

} // namespace an
