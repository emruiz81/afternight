// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The concurrent scenario: two operations running at once in one engine context, with one
// allocation of the pair refused.
//
// Every other scenario in this campaign runs one operation at a time, which is what makes its
// numbers legible - and it is also what would let a failure path that is only wrong when something
// else is running pass twelve times. This one is here for that case: the refusal lands on whichever
// operation asks for the k-th block of the shared context, the other one is running while it
// unwinds, and both have to come back with an answer of their own and leave nothing behind.
//
// It is also the scenario the thread sanitizer has something to do in, which is why the sanitizer
// matrix runs this suite as a focused pipeline as well as a suite.

#include "FaultFixtures.h"
#include "FaultHarness.h"

#include <AfterNightEngine/IO/FrameStream.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <thread>
#include <utility>

namespace
{
using an::faults::CountingSink;
using an::faults::CountingSource;
using an::faults::FaultKind;
using an::faults::frameSequence;
using an::faults::RunContext;
using an::faults::streamBounds;
using an::faults::StreamPlan;
using an::faults::Sweep;

/// Runs one bounded stream of its own, on the thread it is called from.
///
/// @param run what this run was given.
/// @param plan what its source and sink are to do.
/// @return the stream's own answer.
an::Status oneOfThePair(RunContext& run, StreamPlan& plan)
{
    an::ExecutionOptions policy;
    policy.stopToken = run.stop.get_token();
    // Half the context's budget each, stated rather than inherited: an execution that asked for
    // everything would admit the whole budget and the second one would be refused as `Busy` before
    // it ever allocated, which is the context doing its job and not the concurrency this scenario is
    // about.
    policy.maxConcurrency = 3u;
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
    an::FrameStreamCounters counters;
    const an::Status status =
        an::streamFrames(run.engine, *execution, *source, &*sink, nullptr, streamBounds(), counters);
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

TEST(FaultCampaign, AnAllocationIsRefusedWhileTwoOperationsShareOneContext)
{
    Sweep sweep("alloc.concurrent", FaultKind::Allocation);
    // Six threads for two operations of three, so neither operation is waiting for the other's
    // workers and a refusal is the only reason either of them stops.
    sweep.engineOptions({.maxConcurrency = 6u});
    sweep.accept(an::ErrorCode::AllocationFailed);
    sweep.accept(an::ErrorCode::MemoryBudgetExceeded);
    // Five runs per point rather than one: the pair is not deterministic, so one run of a point
    // samples one interleaving of the failure path and the interleaving is where a deadlock lives.
    sweep.repeat(5u);
    sweep.runBound(std::chrono::milliseconds{10000});
    sweep.run([](RunContext& run) {
        StreamPlan first;
        StreamPlan second;
        an::Status other{};
        // The second operation runs on a thread of its own so that the refusal lands on whichever of
        // the two asks for the armed block, rather than on a predetermined one.
        std::thread beside([&] { other = oneOfThePair(run, second); });
        const an::Status mine = oneOfThePair(run, first);
        beside.join();

        if (mine.failed())
        {
            return mine;
        }
        if (other.failed())
        {
            return other;
        }
        return an::Status::success();
    });
}

} // namespace
