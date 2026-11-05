// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - running an independent-frame batch on the execution's workers.
//
// Every completion record, the paths copied in, is allocated before the first job starts, and each
// job writes only its own record, so the workers share nothing but the stop index and the report
// needs no lock. Under the stop policy a failure lowers that index atomically to its own position, so
// no job after the earliest failure yet seen is started while earlier ones still run. The records of
// jobs that never started are dropped only after every worker has joined.

#include <AfterNightEngine/IO/FrameBatch.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ExceptionBoundary.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "core/Utf8.h"
#include "runtime/ScheduledCall.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <exception>
#include <limits>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <ctime>
#endif

namespace an
{
namespace
{
constexpr std::uint32_t kNotStarted = (std::numeric_limits<std::uint32_t>::max)();
constexpr TaskCost kBatchJobCost{.bytesPerItem = 0u, .operationsPerItem = kMinimumTaskOperations};

[[nodiscard]] std::uint64_t threadCpuNanoseconds() noexcept
{
#ifdef _WIN32
    FILETIME created{};
    FILETIME exited{};
    FILETIME kernel{};
    FILETIME user{};
    if (!GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user))
        return 0u;
    const auto intervals = [](const FILETIME& value) noexcept {
        return (static_cast<std::uint64_t>(value.dwHighDateTime) << 32u) |
               static_cast<std::uint64_t>(value.dwLowDateTime);
    };
    return (intervals(kernel) + intervals(user)) * 100u;
#else
    timespec now{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now) != 0)
        return 0u;
    return static_cast<std::uint64_t>(now.tv_sec) * 1000000000u + static_cast<std::uint64_t>(now.tv_nsec);
#endif
}

void addSaturated(std::uint64_t& value, std::uint64_t increment) noexcept
{
    value += (std::min)(increment, (std::numeric_limits<std::uint64_t>::max)() - value);
}

void lower(std::atomic<std::uint64_t>& value, std::uint64_t candidate) noexcept
{
    std::uint64_t current = value.load(std::memory_order_relaxed);
    while (candidate < current &&
           !value.compare_exchange_weak(current, candidate, std::memory_order_relaxed, std::memory_order_relaxed))
    {
    }
}

[[nodiscard]] bool isCancellation(const Status& status) noexcept
{
    return status.code() == ErrorCode::Cancelled || status.code() == ErrorCode::DeadlineExceeded;
}

[[nodiscard]] Status invalidBatch(ErrorCode code, const char* message)
{
    return Status::failure(code, {.message = message, .operation = "executeFrameBatch", .domain = ErrorDomain::Io});
}
} // namespace

Result<FrameBatchReport> executeFrameBatch(const EngineContext& engine, const ExecutionContext& execution,
                                           std::span<const FrameBatchJob> jobs, FrameBatchOperation& operation,
                                           const FrameBatchOptions& options)
{
    if (!engine.valid() || !execution.valid())
        return Status::failure(ErrorCode::NotInitialized);
    if (engine.id() != execution.engineContextId())
        return Status::failure(ErrorCode::IncompatibleArguments);
    if (options.maximumJobs == 0u || (options.failurePolicy != FrameBatchFailurePolicy::Stop &&
                                      options.failurePolicy != FrameBatchFailurePolicy::Continue))
    {
        return invalidBatch(ErrorCode::InvalidArgument, "the job bound is zero or the failure policy is invalid");
    }
    if (jobs.size() > options.maximumJobs)
        return invalidBatch(ErrorCode::LimitExceeded, "the submitted job count exceeds the configured bound");
    for (const FrameBatchJob& job : jobs)
    {
        if (!isBoundaryPath(job.inputPath) || !isBoundaryPath(job.outputPath))
            return invalidBatch(ErrorCode::InvalidArgument, "a job path is empty, oversized or malformed UTF-8");
    }

    FrameBatchReport report{};
    report.counters.jobsSubmitted = jobs.size();
    report.counters.plannedWorkers = plannedWorkerCount(execution, jobs.size(), kBatchJobCost);
    try
    {
        report.completions.reserve(jobs.size());
        for (std::size_t index = 0u; index < jobs.size(); ++index)
        {
            report.completions.push_back({.jobIndex = index,
                                          .token = jobs[index].token,
                                          .inputPath = std::string(jobs[index].inputPath),
                                          .outputPath = std::string(jobs[index].outputPath),
                                          .workerSlot = kNotStarted});
        }
    }
    catch (const std::exception& error)
    {
        return translateException({"engine", "executeFrameBatch allocation", ErrorDomain::Io}, error);
    }
    catch (...)
    {
        return translateUnknownException({"engine", "executeFrameBatch allocation", ErrorDomain::Io});
    }

    std::atomic<std::uint64_t> stopAt{jobs.size()};
    const Status scheduled = parallelForWorkers(execution, jobs.size(), kBatchJobCost, [&](WorkerTask task) -> Status {
        for (std::uint64_t index = task.range.begin; index < task.range.end; ++index)
        {
            if (execution.checkCancellation().failed())
                break;
            if (options.failurePolicy == FrameBatchFailurePolicy::Stop &&
                index > stopAt.load(std::memory_order_relaxed))
            {
                continue;
            }

            FrameBatchCompletion& completion = report.completions[static_cast<std::size_t>(index)];
            completion.workerSlot = task.workerSlot;
            const std::uint64_t started = threadCpuNanoseconds();
            completion.status =
                scheduled_detail::guarded([&] { return operation.run(engine, execution, jobs[index]); });
            const std::uint64_t finished = threadCpuNanoseconds();
            completion.workerCpuNanoseconds = finished > started ? finished - started : 0u;
            if (completion.status.failed() && options.failurePolicy == FrameBatchFailurePolicy::Stop)
                lower(stopAt, index);
        }
        return Status::success();
    });

    for (const FrameBatchCompletion& completion : report.completions)
    {
        if (completion.workerSlot == kNotStarted)
            continue;
        ++report.counters.jobsStarted;
        addSaturated(report.counters.workerCpuNanoseconds, completion.workerCpuNanoseconds);
        if (completion.status.ok())
            ++report.counters.jobsSucceeded;
        else if (isCancellation(completion.status))
            ++report.counters.jobsCancelled;
        else
            ++report.counters.jobsFailed;
        if (report.outcome.ok() && completion.status.failed())
            report.outcome = completion.status;
    }
    report.counters.jobsNotStarted = report.counters.jobsSubmitted - report.counters.jobsStarted;
    if (report.outcome.ok() && scheduled.failed())
        report.outcome = scheduled;
    if (report.outcome.ok())
    {
        const Status stopped = execution.checkCancellation();
        if (stopped.failed())
            report.outcome = stopped;
    }
    std::erase_if(report.completions,
                  [](const FrameBatchCompletion& completion) { return completion.workerSlot == kNotStarted; });
    return report;
}
} // namespace an
