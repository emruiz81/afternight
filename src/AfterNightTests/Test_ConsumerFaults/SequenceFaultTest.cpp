// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The sequence scenarios: three frames read and written into a SER container, which is published only when the
/// writer finishes, with an allocation refused and a stop requested at every point the frames' reads and the writer
/// have.
///
/// A sequence a process or New Sequence writes is published whole or not at all - a recording that stopped at its
/// second frame would read as a shorter sequence rather than as a failure - so every failure is held to leaving the
/// container's path empty with nothing beside it.

#include "Test_ConsumerFaults/ConsumerFaultSupport.h"

#include "controls/ImageViewFiles.h"
#include "controls/SequenceContainers.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
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

/// The frames a scenario writes into its sequence.
struct SequenceFixture
{
    ScopedDirectory inputs{"an-consumer-fault-sequence-inputs"};
    std::array<std::filesystem::path, 3u> frames{inputs.path() / "frame_0.fits", inputs.path() / "frame_1.fits",
                                                 inputs.path() / "frame_2.fits"};

    [[nodiscard]] bool prepare()
    {
        for (std::uint64_t index = 0u; index < frames.size(); ++index)
        {
            if (!an::tests::consumerfaults::writeFrame(frames[index], 64u, 48u, 51u + index,
                                                       0.25 + 0.1 * static_cast<double>(index)))
            {
                return false;
            }
        }
        return true;
    }
};

/// Reads each frame and appends it to a SER writer under one admitted operation, then publishes the container.
an::Status writeSequence(RunContext& run, const SequenceFixture& fixture, const std::filesystem::path& output)
{
    an::Result<an::consumer::EngineSession> session = openSession(run);
    if (!session.hasValue())
    {
        return an::faults::settle(session);
    }
    an::Result<an::ui::SequenceWriter> writer = an::ui::SequenceWriter::create(
        qtPath(output), an::ui::SequenceWriteSettings{.container = an::SequenceFormat::Ser, .frameCount = 3u});
    if (!writer.hasValue())
    {
        return an::faults::settle(writer);
    }
    an::Result<an::consumer::OperationScope> scope = session->admit(an::consumer::OperationRequest{
        .purpose = an::consumer::OperationPurpose::Batch, .maxConcurrency = 1u, .stopToken = run.stop.get_token()});
    if (!scope.hasValue())
    {
        return an::faults::settle(scope);
    }
    for (const std::filesystem::path& frame : fixture.frames)
    {
        an::Result<an::ui::ViewReadResult> read = an::ui::readViewImage(
            *session, an::ui::ViewReadRequest{.path = qtPath(frame)}, run.stop.get_token(), nullptr);
        if (!read.hasValue())
        {
            return an::faults::settle(read);
        }
        an::Result<an::consumer::AssetReadLease> lease = read->asset.read(run.stop.get_token());
        if (!lease.hasValue())
        {
            return an::faults::settle(lease);
        }
        if (an::Status appended = writer->append(scope->engine(), scope->execution(), lease->image());
            appended.failed())
        {
            return appended;
        }
    }
    an::Result<an::SequenceDescriptor> published = writer->finish(scope->execution());
    if (published.hasValue() && published->frameCount != std::optional<std::uint64_t>{fixture.frames.size()})
    {
        // A writer that passed over a frame it could not write and published the rest would read as a shorter
        // sequence rather than as a failure, and the sweep would count the refusal as absorbed.
        return an::Status::failure(an::ErrorCode::Internal, "the sequence was published with " +
                                                                std::to_string(published->frameCount.value_or(0u)) +
                                                                " of its " + std::to_string(fixture.frames.size()) +
                                                                " frames");
    }
    return an::faults::settle(published);
}

TEST(FaultCampaign, AnAllocationIsRefusedAtEveryPointOfASequencesPublication)
{
    SequenceFixture fixture;
    ASSERT_TRUE(fixture.prepare());
    ScopedDirectory directory("an-consumer-fault-sequence");
    const std::filesystem::path output = directory.path() / "frames.ser";

    Sweep sweep("alloc.sequence.write", FaultKind::Allocation);
    acceptRefusals(sweep);
    sweep.destination(output);
    sweep.runBound(std::chrono::milliseconds{10000});
    sweep.run([&](RunContext& run) { return writeSequence(run, fixture, output); });
}

TEST(FaultCampaign, AStopIsRequestedAtEveryPointOfASequencesPublication)
{
    SequenceFixture fixture;
    ASSERT_TRUE(fixture.prepare());
    ScopedDirectory directory("an-consumer-fault-sequence-stop");
    const std::filesystem::path output = directory.path() / "frames.ser";

    Sweep sweep("cancel.sequence.write", FaultKind::Cancellation);
    acceptStops(sweep);
    sweep.destination(output);
    sweep.runBound(std::chrono::milliseconds{10000});
    sweep.run([&](RunContext& run) { return writeSequence(run, fixture, output); });
}

} // namespace
