// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The streaming scenarios: what a bounded read-ahead and write-behind run does when an allocation is
// refused, when a stop arrives, when one of its three stages refuses a frame, and when one of them
// throws instead.
//
// This is the subject with background threads and two bounded queues, so it is the one where a
// half-finished failure path costs a deadlock rather than a wrong answer. The stream's own contract
// says every coordinator is stopped and joined before it returns, on every path; the sweep is how
// that sentence is held to a number instead of a reading.

#include "FaultFixtures.h"
#include "FaultHarness.h"

#include <AfterNightEngine/IO/FrameStream.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <utility>

namespace
{
using an::faults::CountingProcessor;
using an::faults::CountingSink;
using an::faults::CountingSource;
using an::faults::FailureMode;
using an::faults::FaultKind;
using an::faults::frameSequence;
using an::faults::RunContext;
using an::faults::streamBounds;
using an::faults::StreamPlan;
using an::faults::Sweep;

/// Runs one bounded stream under a plan the scenario has already armed.
///
/// Everything it builds is destroyed before it returns, which is what lets the sweep read "nothing
/// leaked" off the resource the moment the subject comes back.
///
/// @param run what this run was given.
/// @param plan what the source, processor and sink are to do.
/// @return the stream's own answer, or the first failure that stopped it being started.
an::Status runStream(RunContext& run, StreamPlan& plan)
{
    an::ExecutionOptions policy;
    policy.stopToken = run.stop.get_token();
    policy.progressSink = &run.progress;
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(run.engine, policy);
    if (!execution.hasValue())
    {
        return an::faults::settle(execution);
    }

    an::Result<an::FrameSource> source =
        an::FrameSource::create(run.engine, frameSequence(plan.frames), std::make_unique<CountingSource>(plan));
    if (!source.hasValue())
    {
        return an::faults::settle(source);
    }
    an::Result<an::FrameSink> sink =
        an::FrameSink::create(run.engine, frameSequence(plan.frames), std::make_unique<CountingSink>(plan));
    if (!sink.hasValue())
    {
        return an::faults::settle(sink);
    }

    CountingProcessor processor(plan);
    an::FrameStreamCounters counters;
    const an::Status status =
        an::streamFrames(run.engine, *execution, *source, &*sink, &processor, streamBounds(), counters);

    // Both sessions are closed here rather than left to their destructors, so that a cleanup failure
    // is a value this scenario can see rather than something swallowed during unwinding.
    const an::Status closedSource = source->close();
    const an::Status closedSink = sink->close();
    if (status.ok() && closedSource.failed())
    {
        return closedSource;
    }
    if (status.ok() && closedSink.failed())
    {
        return closedSink;
    }
    return status;
}

TEST(FaultCampaign, AnAllocationIsRefusedAtEveryPointOfABoundedStream)
{
    Sweep sweep("alloc.stream", FaultKind::Allocation);
    sweep.engineOptions({.maxConcurrency = 3u});
    sweep.accept(an::ErrorCode::AllocationFailed);
    sweep.accept(an::ErrorCode::MemoryBudgetExceeded);
    // Three runs per point: which of the stream's threads asks for the armed block depends on how
    // the two queues happen to be scheduled, so one run of a point proves one of the arrangements.
    sweep.repeat(3u);
    sweep.runBound(std::chrono::milliseconds{5000});
    sweep.run([](RunContext& run) {
        StreamPlan plan;
        return runStream(run, plan);
    });
}

TEST(FaultCampaign, AStopIsRequestedAtEveryPointOfABoundedStream)
{
    Sweep sweep("cancel.stream", FaultKind::Cancellation);
    sweep.engineOptions({.maxConcurrency = 3u});
    sweep.accept(an::ErrorCode::Cancelled);
    sweep.repeat(3u);
    sweep.runBound(std::chrono::milliseconds{5000});
    sweep.run([](RunContext& run) {
        StreamPlan plan;
        return runStream(run, plan);
    });
}

TEST(FaultCampaign, EveryStageOfABoundedStreamRefusesEveryFrameInTurn)
{
    Sweep sweep("io.stream", FaultKind::InputOutput);
    sweep.engineOptions({.maxConcurrency = 3u});
    sweep.accept(an::ErrorCode::InvalidData);
    sweep.accept(an::ErrorCode::Unsupported);
    sweep.accept(an::ErrorCode::ExternalFailure);
    sweep.runBound(std::chrono::milliseconds{5000});
    sweep.run([](RunContext& run) {
        StreamPlan plan;
        // Three stages, each refusing each frame in turn, and one refusal to complete the output:
        // the read of frame k, the processing of frame k, the write of frame k, and the finish.
        const std::uint64_t perStage = plan.frames;
        run.opportunities = 3u * perStage + 1u;
        if (!run.census())
        {
            const std::uint64_t point = run.point - 1u;
            if (point < perStage)
            {
                plan.failRead = point;
            }
            else if (point < 2u * perStage)
            {
                plan.failProcess = point - perStage;
            }
            else if (point < 3u * perStage)
            {
                plan.failWrite = point - 2u * perStage;
            }
            else
            {
                plan.failFinish = true;
            }
        }
        const an::Status status = runStream(run, plan);
        run.fired = plan.fired.load();
        return status;
    });
}

TEST(FaultCampaign, EveryStageOfABoundedStreamThrowsOnEveryFrameInTurn)
{
    Sweep sweep("external.stream", FaultKind::External);
    sweep.engineOptions({.maxConcurrency = 3u});
    sweep.accept(an::ErrorCode::ExternalFailure);
    sweep.runBound(std::chrono::milliseconds{5000});
    sweep.run([](RunContext& run) {
        StreamPlan plan;
        plan.mode = FailureMode::Throw;
        const std::uint64_t perStage = plan.frames;
        run.opportunities = 3u * perStage;
        if (!run.census())
        {
            const std::uint64_t point = run.point - 1u;
            if (point < perStage)
            {
                plan.failRead = point;
            }
            else if (point < 2u * perStage)
            {
                plan.failProcess = point - perStage;
            }
            else
            {
                plan.failWrite = point - 2u * perStage;
            }
        }
        const an::Status status = runStream(run, plan);
        run.fired = plan.fired.load();
        return status;
    });
}

} // namespace
