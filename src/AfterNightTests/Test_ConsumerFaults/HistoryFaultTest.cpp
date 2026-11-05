// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The history's snapshot scenarios: the copy a step keeps written as the engine's image snapshot, and read back for
/// the identity the history drew for it, with an allocation refused and a stop requested at every point either has.
///
/// A step's file is the history's one durable state, so a failure while writing it must leave the step's path holding
/// the whole snapshot or nothing - never a payload the next undo reads half of - and a failure while reading it must
/// make no asset and keep no block.

#include "Test_ConsumerFaults/ConsumerFaultSupport.h"

#include "controls/ImageHistoryBackupQueue.h"
#include "controls/ImageViewFiles.h"

#include <AfterNightEngine/IO/ArtifactIdentity.h>

#include <gtest/gtest.h>

#include <chrono>
#include <string>

namespace
{
using an::faults::FaultKind;
using an::faults::RunContext;
using an::faults::ScopedDirectory;
using an::faults::Sweep;
using an::tests::consumerfaults::acceptRefusals;
using an::tests::consumerfaults::acceptStops;
using an::tests::consumerfaults::openSession;
using an::tests::consumerfaults::qtPath;

/// The identity every scenario's snapshot is written for and read back under.
[[nodiscard]] an::ArtifactIdentity stepIdentity()
{
    return *an::ArtifactIdentity::create("history-step-1", "consumer-fault-sweep");
}

/// Reads the frame into an asset of a session over the run's resource, as the viewer reads a file, and writes the
/// history's snapshot of it.
an::Status writeStepSnapshot(RunContext& run, const std::filesystem::path& frame, const std::string& destination)
{
    an::Result<an::consumer::EngineSession> session = openSession(run);
    if (!session.hasValue())
    {
        return an::faults::settle(session);
    }
    an::Result<an::ui::ViewReadResult> read =
        an::ui::readViewImage(*session, an::ui::ViewReadRequest{.path = qtPath(frame)}, run.stop.get_token(), nullptr);
    if (!read.hasValue())
    {
        return an::faults::settle(read);
    }
    an::Result<an::consumer::AssetReadLease> lease = read->asset.read(run.stop.get_token());
    if (!lease.hasValue())
    {
        return an::faults::settle(lease);
    }
    return an::ui::history::writeHistorySnapshot(*session, *lease, destination, stepIdentity(), run.stop.get_token());
}

/// Reads a snapshot back as a new asset, as an undo does, and lets it go.
an::Status readStepSnapshot(RunContext& run, const std::string& snapshot)
{
    an::Result<an::consumer::EngineSession> session = openSession(run);
    if (!session.hasValue())
    {
        return an::faults::settle(session);
    }
    an::Result<an::consumer::ImageAsset> restored =
        an::ui::history::readHistorySnapshot(*session, snapshot, stepIdentity(), run.stop.get_token());
    return an::faults::settle(restored);
}

/// A frame and, beside it, the snapshot of it the read scenarios restore.
struct SnapshotFixture
{
    ScopedDirectory inputs{"an-consumer-fault-history-inputs"};
    std::filesystem::path frame = inputs.path() / "frame.fits";
    std::string snapshot = inputs.file("step.ansnapshot");

    /// Writes the frame and its snapshot through a session over the platform's memory, before any sweep.
    [[nodiscard]] bool prepare()
    {
        if (!an::tests::consumerfaults::writeFrame(frame, 96u, 64u, 11u))
        {
            return false;
        }
        an::Result<an::consumer::EngineSession> session = an::tests::consumerfaults::openSetupSession();
        if (!session.hasValue())
        {
            ADD_FAILURE() << session.status().message();
            return false;
        }
        an::Result<an::ui::ViewReadResult> read =
            an::ui::readViewImage(*session, an::ui::ViewReadRequest{.path = qtPath(frame)}, {}, nullptr);
        if (!read.hasValue())
        {
            ADD_FAILURE() << read.status().message();
            return false;
        }
        an::Result<an::consumer::AssetReadLease> lease = read->asset.read();
        if (!lease.hasValue())
        {
            ADD_FAILURE() << lease.status().message();
            return false;
        }
        const an::Status written =
            an::ui::history::writeHistorySnapshot(*session, *lease, snapshot, stepIdentity(), {});
        EXPECT_TRUE(written.ok()) << written.message();
        return written.ok();
    }
};

TEST(FaultCampaign, AnAllocationIsRefusedAtEveryPointOfAHistoryStepsSnapshot)
{
    SnapshotFixture fixture;
    ASSERT_TRUE(fixture.prepare());
    ScopedDirectory directory("an-consumer-fault-history-write");
    const std::string destination = directory.file("step.ansnapshot");

    Sweep sweep("alloc.history.write", FaultKind::Allocation);
    acceptRefusals(sweep);
    sweep.destination(directory.path() / "step.ansnapshot");
    sweep.runBound(std::chrono::milliseconds{10000});
    sweep.run([&](RunContext& run) { return writeStepSnapshot(run, fixture.frame, destination); });
}

TEST(FaultCampaign, AStopIsRequestedAtEveryPointOfAHistoryStepsSnapshot)
{
    SnapshotFixture fixture;
    ASSERT_TRUE(fixture.prepare());
    ScopedDirectory directory("an-consumer-fault-history-stop");
    const std::string destination = directory.file("step.ansnapshot");

    Sweep sweep("cancel.history.write", FaultKind::Cancellation);
    acceptStops(sweep);
    sweep.destination(directory.path() / "step.ansnapshot");
    sweep.runBound(std::chrono::milliseconds{10000});
    sweep.run([&](RunContext& run) { return writeStepSnapshot(run, fixture.frame, destination); });
}

TEST(FaultCampaign, AnAllocationIsRefusedAtEveryPointOfAnUndosRead)
{
    SnapshotFixture fixture;
    ASSERT_TRUE(fixture.prepare());

    Sweep sweep("alloc.history.read", FaultKind::Allocation);
    acceptRefusals(sweep);
    sweep.runBound(std::chrono::milliseconds{10000});
    sweep.run([&](RunContext& run) { return readStepSnapshot(run, fixture.snapshot); });
}

TEST(FaultCampaign, AStopIsRequestedAtEveryPointOfAnUndosRead)
{
    SnapshotFixture fixture;
    ASSERT_TRUE(fixture.prepare());

    Sweep sweep("cancel.history.read", FaultKind::Cancellation);
    acceptStops(sweep);
    sweep.runBound(std::chrono::milliseconds{10000});
    sweep.run([&](RunContext& run) { return readStepSnapshot(run, fixture.snapshot); });
}

} // namespace
