// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The image snapshot scenarios: what a snapshot's write and its read do when an allocation is refused and when a
// stop arrives, at every point either has.
//
// A snapshot is what an undo history restores from, so both halves are held to the campaign's claims. The write
// is checked as a published file is: the destination holds the whole snapshot after a run that succeeded, and
// nothing - no snapshot, no pending sibling - after one that failed. The read is checked for what it hands back:
// a refused allocation or a stop at any point answers with a typed failure, nothing escapes as an exception, and
// every block the read took - the metadata section, the samples, the mask - is back.

#include "FaultFixtures.h"
#include "FaultHarness.h"

#include <AfterNightEngine/IO/ArtifactIdentity.h>
#include <AfterNightEngine/IO/ImageSnapshot.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <utility>

namespace
{
using an::faults::FaultKind;
using an::faults::RunContext;
using an::faults::ScopedDirectory;
using an::faults::Sweep;

/// The identity every snapshot of these scenarios is written for.
[[nodiscard]] const an::ArtifactIdentity& snapshotIdentity()
{
    static const an::ArtifactIdentity identity = *an::ArtifactIdentity::create("fault-snapshot", "campaign");
    return identity;
}

/// Metadata whose validity a mask decides, with cards to encode.
[[nodiscard]] an::ImageMetadata maskedMetadata()
{
    const an::ImageSemantics semantics = *an::ImageSemantics::create(
        {.validity = *an::ValidityDescriptor::create({.rule = an::ValidityRule::ExplicitMask})});
    const an::CodecMetadata codec = *an::CodecMetadata::create(
        {}, {*an::CodecCard::create(an::CodecNamespace::Fits, "OBJECT", "'M 31'", "the target"),
             *an::CodecCard::create(an::CodecNamespace::Fits, "EXPTIME", "300.0", "")});
    return an::ImageMetadata::create({}, semantics, {}, {}, codec, std::nullopt);
}

/// Writes one frame's snapshot, with a mask, to one destination, and destroys everything it built.
///
/// @param run what this run was given.
/// @param destination where the snapshot is to be published.
/// @return the write's own answer.
an::Status writeOneSnapshot(RunContext& run, const std::string& destination)
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
    an::Result<an::ValidityMask> mask = an::ValidityMask::createAllValid(
        run.engine,
        *an::ImageExtent::create(an::faults::kFrameWidth, an::faults::kFrameHeight, 1u, run.engine.limits()));
    if (!mask.hasValue())
    {
        return an::faults::settle(mask);
    }
    an::Result<an::ImageSnapshotWriteReport> written = an::writeImageSnapshot(
        run.engine, *execution, {.destinationPath = destination, .identity = &snapshotIdentity()}, *frame,
        maskedMetadata(), &*mask);
    return an::faults::settle(written);
}

/// Reads one snapshot whole and releases what it read.
///
/// @param run what this run was given.
/// @param source the snapshot.
/// @return the read's own answer.
an::Status readOneSnapshot(RunContext& run, const std::string& source)
{
    an::ExecutionOptions policy;
    policy.stopToken = run.stop.get_token();
    policy.progressSink = &run.progress;
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(run.engine, policy);
    if (!execution.hasValue())
    {
        return an::faults::settle(execution);
    }
    an::Result<an::ImageSnapshotContents> contents = an::readImageSnapshot(run.engine, *execution, source);
    if (contents.hasValue() && !contents->validity.has_value())
    {
        return an::Status::failure(an::ErrorCode::Internal, "the snapshot's mask did not come back");
    }
    return an::faults::settle(contents);
}

/// Writes the snapshot the read scenarios read, through an engine nothing is injected into.
///
/// @param destination where to write it.
/// @return whether it was written.
[[nodiscard]] bool prepareSnapshot(const std::string& destination)
{
    an::Result<an::EngineContext> engine = an::EngineContext::create({.maxConcurrency = 1u});
    if (!engine.hasValue())
    {
        return false;
    }
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, {});
    an::Result<an::RawFrame> frame = an::faults::makeFrame(*engine, 3u);
    an::Result<an::ValidityMask> mask = an::ValidityMask::createAllValid(
        *engine, *an::ImageExtent::create(an::faults::kFrameWidth, an::faults::kFrameHeight, 1u, engine->limits()));
    if (!execution.hasValue() || !frame.hasValue() || !mask.hasValue())
    {
        return false;
    }
    return an::writeImageSnapshot(*engine, *execution,
                                  {.destinationPath = destination, .identity = &snapshotIdentity()}, *frame,
                                  maskedMetadata(), &*mask)
        .hasValue();
}

TEST(FaultCampaign, AnAllocationIsRefusedAtEveryPointOfASnapshotWrite)
{
    ScopedDirectory directory("an-fault-snapshot-write-allocation");
    const std::string destination = directory.file("history.ansnap");

    Sweep sweep("alloc.snapshot.write", FaultKind::Allocation);
    sweep.engineOptions({.maxConcurrency = 2u});
    sweep.accept(an::ErrorCode::AllocationFailed);
    sweep.accept(an::ErrorCode::MemoryBudgetExceeded);
    sweep.destination(directory.path() / "history.ansnap");
    sweep.runBound(std::chrono::milliseconds{5000});
    sweep.run([&destination](RunContext& run) { return writeOneSnapshot(run, destination); });
}

TEST(FaultCampaign, AStopIsRequestedAtEveryPointOfASnapshotWrite)
{
    ScopedDirectory directory("an-fault-snapshot-write-cancellation");
    const std::string destination = directory.file("history.ansnap");

    Sweep sweep("cancel.snapshot.write", FaultKind::Cancellation);
    sweep.engineOptions({.maxConcurrency = 2u});
    sweep.accept(an::ErrorCode::Cancelled);
    sweep.destination(directory.path() / "history.ansnap");
    sweep.runBound(std::chrono::milliseconds{5000});
    sweep.run([&destination](RunContext& run) { return writeOneSnapshot(run, destination); });
}

TEST(FaultCampaign, AnAllocationIsRefusedAtEveryPointOfASnapshotRead)
{
    ScopedDirectory directory("an-fault-snapshot-read-allocation");
    const std::string source = directory.file("history.ansnap");
    ASSERT_TRUE(prepareSnapshot(source));

    Sweep sweep("alloc.snapshot.read", FaultKind::Allocation);
    sweep.engineOptions({.maxConcurrency = 2u});
    sweep.accept(an::ErrorCode::AllocationFailed);
    sweep.accept(an::ErrorCode::MemoryBudgetExceeded);
    sweep.runBound(std::chrono::milliseconds{5000});
    sweep.run([&source](RunContext& run) { return readOneSnapshot(run, source); });
}

TEST(FaultCampaign, AStopIsRequestedAtEveryPointOfASnapshotRead)
{
    ScopedDirectory directory("an-fault-snapshot-read-cancellation");
    const std::string source = directory.file("history.ansnap");
    ASSERT_TRUE(prepareSnapshot(source));

    Sweep sweep("cancel.snapshot.read", FaultKind::Cancellation);
    sweep.engineOptions({.maxConcurrency = 2u});
    sweep.accept(an::ErrorCode::Cancelled);
    sweep.runBound(std::chrono::milliseconds{5000});
    sweep.run([&source](RunContext& run) { return readOneSnapshot(run, source); });
}

} // namespace
