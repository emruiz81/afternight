// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The reading scenarios: a file read into a view's asset, as File > Open and the viewer read one, and an extension's
/// shared buffer imported as a new asset, with an allocation refused and - for the read, which takes a stop - a stop
/// requested at every point either has.
///
/// Neither publishes anything, so what each is held to is that a failure makes no asset and keeps no block of the
/// session: the harness finds every block given back once the subject has let its session go, which an asset left
/// behind by a failure path would make impossible.

#include "Test_ConsumerFaults/ConsumerFaultSupport.h"

#include "app/SharedBufferManager.h"
#include "controls/ImageViewFiles.h"

#include <gtest/gtest.h>

#include <chrono>
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

/// Reads the frame into a new asset of a session over the run's resource, and lets it go.
an::Status readFrame(RunContext& run, const std::filesystem::path& frame)
{
    an::Result<an::consumer::EngineSession> session = openSession(run);
    if (!session.hasValue())
    {
        return an::faults::settle(session);
    }
    an::Result<an::ui::ViewReadResult> read =
        an::ui::readViewImage(*session, an::ui::ViewReadRequest{.path = qtPath(frame)}, run.stop.get_token(), nullptr);
    return an::faults::settle(read);
}

TEST(FaultCampaign, AnAllocationIsRefusedAtEveryPointOfAViewsRead)
{
    ScopedDirectory inputs("an-consumer-fault-read-inputs");
    const std::filesystem::path frame = inputs.path() / "frame.fits";
    ASSERT_TRUE(an::tests::consumerfaults::writeFrame(frame, 128u, 96u, 41u));

    Sweep sweep("alloc.view.read", FaultKind::Allocation);
    acceptRefusals(sweep);
    sweep.runBound(std::chrono::milliseconds{10000});
    sweep.run([&](RunContext& run) { return readFrame(run, frame); });
}

TEST(FaultCampaign, AStopIsRequestedAtEveryPointOfAViewsRead)
{
    ScopedDirectory inputs("an-consumer-fault-read-stop-inputs");
    const std::filesystem::path frame = inputs.path() / "frame.fits";
    ASSERT_TRUE(an::tests::consumerfaults::writeFrame(frame, 128u, 96u, 42u));

    Sweep sweep("cancel.view.read", FaultKind::Cancellation);
    acceptStops(sweep);
    sweep.runBound(std::chrono::milliseconds{10000});
    sweep.run([&](RunContext& run) { return readFrame(run, frame); });
}

/// A buffer an extension would be handed: the frame exported through the seam the full host lends images through,
/// written before the sweep by a session over the platform's memory.
struct ExportedBuffer
{
    ScopedDirectory root{"an-consumer-fault-buffers"};
    std::optional<an::ui::extensions::SharedImageBuffer> buffer;

    [[nodiscard]] bool prepare()
    {
        const std::filesystem::path frame = root.path() / "frame.fits";
        if (!an::tests::consumerfaults::writeFrame(frame, 128u, 96u, 43u))
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
        an::ui::extensions::ImageExportRequest request;
        request.sessionId = "consumer-fault-session";
        request.role = an::ui::extensions::SharedBufferRole::Source;
        request.accessMode = an::ui::extensions::SharedBufferAccessMode::ReadOnly;
        request.bufferId = "source";
        request.symbolicName = "source";
        an::Result<an::ui::extensions::SharedImageBuffer> exported =
            an::ui::extensions::SharedBufferManager(root.path() / "buffers").exportImage(lease->image(), request);
        if (!exported.hasValue())
        {
            ADD_FAILURE() << exported.status().message();
            return false;
        }
        buffer.emplace(std::move(*exported));
        return true;
    }
};

TEST(FaultCampaign, AnAllocationIsRefusedAtEveryPointOfAnExtensionBuffersImport)
{
    ExportedBuffer exported;
    ASSERT_TRUE(exported.prepare());
    const an::ui::extensions::SharedBufferDescriptor descriptor = exported.buffer->descriptor();

    Sweep sweep("alloc.buffer.import", FaultKind::Allocation);
    acceptRefusals(sweep);
    sweep.runBound(std::chrono::milliseconds{10000});
    sweep.run([&](RunContext& run) {
        an::Result<an::consumer::EngineSession> session = openSession(run);
        if (!session.hasValue())
        {
            return an::faults::settle(session);
        }
        an::Result<an::ui::extensions::ImportedImage> imported =
            an::ui::extensions::SharedBufferManager(exported.root.path() / "buffers").importImage(*session, descriptor);
        return an::faults::settle(imported);
    });
}

} // namespace
