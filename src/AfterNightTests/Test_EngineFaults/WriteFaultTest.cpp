// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The output scenarios: what a generated file's journey from a private pending payload to its
// destination name does when an allocation is refused, when a stop arrives, when the encoder fails
// at each of the steps it has, and when it throws instead.
//
// This is the subject where "no half-commit" is a statement about the file system rather than about
// a returned value, so every run here is checked twice: the answer it returned, and what the
// directory holds afterwards. The transaction's own contract is that a failure publishes nothing and
// leaves no pending payload behind; the sweep is what holds every one of its failure paths to that
// sentence rather than the two somebody wrote a case for.

#include "FaultFixtures.h"
#include "FaultHarness.h"

#include <AfterNightEngine/IO/OutputTransaction.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <string>
#include <utility>

namespace
{
using an::faults::FailureMode;
using an::faults::FaultKind;
using an::faults::kWritingCodecSteps;
using an::faults::RunContext;
using an::faults::ScopedDirectory;
using an::faults::Sweep;
using an::faults::WritingCodecPlan;

/// Writes one frame to one destination through the transaction, and destroys everything it built.
///
/// @param run what this run was given.
/// @param destination where the file is to be published.
/// @return the transaction's own answer.
an::Status writeOneFrame(RunContext& run, const std::string& destination)
{
    an::ExecutionOptions policy;
    policy.stopToken = run.stop.get_token();
    policy.progressSink = &run.progress;
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(run.engine, policy);
    if (!execution.hasValue())
    {
        return an::faults::settle(execution);
    }
    an::Result<an::RawFrame> frame = an::faults::makeFrame(run.engine);
    if (!frame.hasValue())
    {
        return an::faults::settle(frame);
    }
    const an::ImageMetadata metadata;
    an::Result<an::OutputReport> written =
        an::writeImage(run.engine, *execution, {.destinationPath = destination}, *frame, metadata);
    return an::faults::settle(written);
}

TEST(FaultCampaign, AnAllocationIsRefusedAtEveryPointOfAPublishedWrite)
{
    an::Result<std::shared_ptr<const an::CodecRegistry>> registry = an::faults::fitsRegistry();
    ASSERT_TRUE(registry.hasValue()) << registry.status().message();

    ScopedDirectory directory("an-fault-write-allocation");
    const std::string destination = directory.file("frame.fits");

    Sweep sweep("alloc.write", FaultKind::Allocation);
    sweep.engineOptions({.maxConcurrency = 2u, .codecRegistry = *registry});
    sweep.accept(an::ErrorCode::AllocationFailed);
    sweep.accept(an::ErrorCode::MemoryBudgetExceeded);
    sweep.destination(directory.path() / "frame.fits");
    sweep.runBound(std::chrono::milliseconds{5000});
    sweep.run([&destination](RunContext& run) { return writeOneFrame(run, destination); });
}

TEST(FaultCampaign, AStopIsRequestedAtEveryPointOfAPublishedWrite)
{
    an::Result<std::shared_ptr<const an::CodecRegistry>> registry = an::faults::fitsRegistry();
    ASSERT_TRUE(registry.hasValue()) << registry.status().message();

    ScopedDirectory directory("an-fault-write-cancellation");
    const std::string destination = directory.file("frame.fits");

    Sweep sweep("cancel.write", FaultKind::Cancellation);
    sweep.engineOptions({.maxConcurrency = 2u, .codecRegistry = *registry});
    sweep.accept(an::ErrorCode::Cancelled);
    sweep.destination(directory.path() / "frame.fits");
    sweep.runBound(std::chrono::milliseconds{5000});
    sweep.run([&destination](RunContext& run) { return writeOneFrame(run, destination); });
}

TEST(FaultCampaign, TheEncoderFailsAtEveryStepOfWritingThePendingPayload)
{
    ScopedDirectory directory("an-fault-write-io");
    const std::string destination = directory.file("frame.fits");
    WritingCodecPlan plan;
    an::Result<std::shared_ptr<const an::CodecRegistry>> registry = an::faults::writingRegistry(plan);
    ASSERT_TRUE(registry.hasValue()) << registry.status().message();

    Sweep sweep("io.write", FaultKind::InputOutput);
    sweep.engineOptions({.maxConcurrency = 2u, .codecRegistry = *registry});
    // The encoder's own refusals, the short payload the transaction proves against the report, and
    // the failure that arrives with bytes already on disk.
    sweep.accept(an::ErrorCode::Unsupported);
    sweep.accept(an::ErrorCode::InvalidData);
    sweep.accept(an::ErrorCode::ExternalFailure);
    sweep.destination(directory.path() / "frame.fits");
    sweep.runBound(std::chrono::milliseconds{5000});
    sweep.run([&](RunContext& run) {
        run.opportunities = kWritingCodecSteps;
        plan.mode = FailureMode::Refuse;
        plan.step = run.census() ? 0u : run.point;
        plan.fired.store(false);
        const an::Status status = writeOneFrame(run, destination);
        run.fired = plan.fired.load();
        return status;
    });
}

TEST(FaultCampaign, TheEncoderThrowsAtEveryStepThatCanThrow)
{
    ScopedDirectory directory("an-fault-write-external");
    const std::string destination = directory.file("frame.fits");
    WritingCodecPlan plan;
    an::Result<std::shared_ptr<const an::CodecRegistry>> registry = an::faults::writingRegistry(plan);
    ASSERT_TRUE(registry.hasValue()) << registry.status().message();

    // The three steps at which an encoder can throw rather than return: before it opens anything,
    // with its bytes already on disk, and at its last step. The other two steps of the plan are a
    // short payload and an over-reported one, which are values rather than exceptions.
    static constexpr std::array<std::uint64_t, 3u> kThrowingSteps{1u, 4u, 5u};

    Sweep sweep("external.write", FaultKind::External);
    sweep.engineOptions({.maxConcurrency = 2u, .codecRegistry = *registry});
    sweep.accept(an::ErrorCode::ExternalFailure);
    sweep.destination(directory.path() / "frame.fits");
    sweep.runBound(std::chrono::milliseconds{5000});
    sweep.run([&](RunContext& run) {
        run.opportunities = kThrowingSteps.size();
        plan.mode = FailureMode::Throw;
        plan.step = run.census() ? 0u : kThrowingSteps[static_cast<std::size_t>(run.point - 1u)];
        plan.fired.store(false);
        const an::Status status = writeOneFrame(run, destination);
        run.fired = plan.fired.load();
        return status;
    });
}

} // namespace
