// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The integration's publication scenarios: an image a stage publishes stamped with its artifact identity, a document
/// published through its pending sibling, a payload and the manifest that names it, and the standalone stack's group
/// written whole - each with an allocation refused, a stop requested, or a file operation failed at every point it
/// has.
///
/// These are the paths a run's reuse decisions rest on. A stage's image that is half there would be read as current by
/// its stamp; a document left as its pending sibling would be read as missing and recomputed, which is the right
/// answer, but one left half-replaced would not be; a manifest naming a payload that is not there would be trusted. So
/// every failure is held to the destination holding the whole of what was published or what it held before, with
/// nothing beside it once the workflow's own recovery has run.

#include "Test_ConsumerFaults/ConsumerFaultSupport.h"

#include "controls/ImageViewFiles.h"
#include "processes/StackingBatch.h"
#include "processes/WorkflowToolFiles.h"
#include "processes/ImageIntegration/workflow/DocumentPublication.h"
#include "processes/ImageIntegration/workflow/WorkflowArtifactStamp.h"

#include <AfterNightConsumerRuntime/OperationScope.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/IO/ArtifactIdentity.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

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

// ---- An image a stage publishes.

/// Reads the frame as a stage reads a light, then publishes it under one admitted operation, stamped with the
/// manifest entry it satisfies.
an::Status publishStageImage(RunContext& run, const std::filesystem::path& frame, const std::filesystem::path& output)
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
    const auto* image = std::get_if<an::Image<float>>(&lease->image());
    if (image == nullptr)
    {
        return an::Status::failure(an::ErrorCode::Internal, "the fixture frame was not read as Float32");
    }
    an::Result<an::consumer::OperationScope> scope = session->admit(an::consumer::OperationRequest{
        .purpose = an::consumer::OperationPurpose::Batch, .maxConcurrency = 1u, .stopToken = run.stop.get_token()});
    if (!scope.hasValue())
    {
        return an::faults::settle(scope);
    }
    an::Result<an::ArtifactIdentity> identity =
        ImageIntegration::workflowArtifactIdentity(QStringLiteral("manifest-entry"), QStringLiteral("run"));
    if (!identity.hasValue())
    {
        return an::faults::settle(identity);
    }
    return ImageIntegration::publishWorkflowImage(*scope, *image, qtPath(output), *identity);
}

struct FrameFixture
{
    ScopedDirectory inputs{"an-consumer-fault-workflow-inputs"};
    std::filesystem::path frame = inputs.path() / "light.fits";
};

TEST(FaultCampaign, AnAllocationIsRefusedAtEveryPointOfAStagesPublishedImage)
{
    FrameFixture fixture;
    ASSERT_TRUE(an::tests::consumerfaults::writeFrame(fixture.frame, 96u, 64u, 21u));
    ScopedDirectory directory("an-consumer-fault-stage-image");
    const std::filesystem::path output = directory.path() / "master_bias.fit";

    Sweep sweep("alloc.workflow.image", FaultKind::Allocation);
    acceptRefusals(sweep);
    sweep.destination(output);
    sweep.runBound(std::chrono::milliseconds{10000});
    sweep.run([&](RunContext& run) { return publishStageImage(run, fixture.frame, output); });
}

TEST(FaultCampaign, AStopIsRequestedAtEveryPointOfAStagesPublishedImage)
{
    FrameFixture fixture;
    ASSERT_TRUE(an::tests::consumerfaults::writeFrame(fixture.frame, 96u, 64u, 22u));
    ScopedDirectory directory("an-consumer-fault-stage-image-stop");
    const std::filesystem::path output = directory.path() / "master_bias.fit";

    Sweep sweep("cancel.workflow.image", FaultKind::Cancellation);
    acceptStops(sweep);
    sweep.destination(output);
    sweep.runBound(std::chrono::milliseconds{10000});
    sweep.run([&](RunContext& run) { return publishStageImage(run, fixture.frame, output); });
}

// ---- Documents.

/// The file operations of the real file system, failing the ordinal-th one the publication makes and counting all of
/// them; a run with no ordinal is the census.
struct FailingOperations
{
    std::uint64_t failAt = 0u;
    std::uint64_t made = 0u;
    bool fired = false;

    [[nodiscard]] bool fails()
    {
        ++made;
        if (made == failAt)
        {
            fired = true;
            return true;
        }
        return false;
    }

    [[nodiscard]] ImageIntegration::DocumentFileOperations operations()
    {
        const ImageIntegration::DocumentFileOperations system = ImageIntegration::systemDocumentFileOperations();
        ImageIntegration::DocumentFileOperations wrapped = system;
        wrapped.create = [this, system](const QString& path,
                                        QString& error) -> ImageIntegration::DocumentFileOperations::Handle {
            if (fails())
            {
                error = QStringLiteral("injected: the pending sibling could not be created");
                return nullptr;
            }
            return system.create(path, error);
        };
        wrapped.write = [this, system](ImageIntegration::DocumentFileOperations::Handle handle, const QByteArray& bytes,
                                       bool& diskFull, QString& error) {
            if (fails())
            {
                error = QStringLiteral("injected: the write failed");
                return false;
            }
            return system.write(handle, bytes, diskFull, error);
        };
        wrapped.flush = [this, system](ImageIntegration::DocumentFileOperations::Handle handle, QString& error) {
            if (fails())
            {
                error = QStringLiteral("injected: the flush failed");
                return false;
            }
            return system.flush(handle, error);
        };
        wrapped.close = [this, system](ImageIntegration::DocumentFileOperations::Handle handle, QString& error) {
            if (fails())
            {
                // A close that fails still lets the handle go, as the platform does, so the publication can remove
                // what it wrote.
                QString ignored;
                static_cast<void>(system.close(handle, ignored));
                error = QStringLiteral("injected: the close failed");
                return false;
            }
            return system.close(handle, error);
        };
        wrapped.replace = [this, system](const QString& from, const QString& to, QString& error) {
            if (fails())
            {
                error = QStringLiteral("injected: the replace failed");
                return false;
            }
            return system.replace(from, to, error);
        };
        // Removal is what a failure's cleanup does; it is not a step of the publication, and failing it is the one
        // case the publication reports as a pending sibling left for recovery, which the pair's scenario holds.
        return wrapped;
    }
};

/// A document of the size of a normalization set's coefficients.
[[nodiscard]] ImageIntegration::DocumentEnvelope coefficientsDocument(const char* identity)
{
    ImageIntegration::DocumentEnvelope envelope;
    envelope.documentType = QStringLiteral("normalization-coefficients");
    envelope.schemaVersion = 1;
    envelope.identity = QString::fromLatin1(identity);
    envelope.payload = QByteArray(4096, 'n');
    return envelope;
}

/// The status a publication's outcome stands for, in the engine's codes the harness checks.
[[nodiscard]] an::Status statusOf(const ImageIntegration::DocumentPublicationResult& published)
{
    if (published.ok())
    {
        return an::Status::success();
    }
    if (published.failure == ImageIntegration::DocumentPublicationFailure::Cancelled)
    {
        return an::Status::failure(an::ErrorCode::Cancelled, published.message.toStdString());
    }
    return an::Status::failure(an::ErrorCode::ExternalFailure, published.message.toStdString());
}

TEST(FaultCampaign, EveryFileOperationOfADocumentsPublicationFailsInTurn)
{
    ScopedDirectory directory("an-consumer-fault-document");
    const QString destination = qtPath(directory.path() / "coefficients.json");

    Sweep sweep("io.workflow.document", FaultKind::InputOutput);
    sweep.accept(an::ErrorCode::ExternalFailure);
    sweep.destination(directory.path() / "coefficients.json");
    sweep.runBound(std::chrono::milliseconds{5000});
    sweep.run([&](RunContext& run) {
        FailingOperations failing;
        failing.failAt = run.point;
        const ImageIntegration::DocumentPublicationResult published = ImageIntegration::publishDocument(
            destination, coefficientsDocument("set-1"), run.stop.get_token(), failing.operations());
        if (run.census())
        {
            run.opportunities = failing.made;
        }
        run.fired = failing.fired;
        if (published.pendingLeftBehind)
        {
            return an::Status::failure(an::ErrorCode::Internal, "a failed publication left its pending sibling");
        }
        return statusOf(published);
    });
}

TEST(FaultCampaign, EveryFileOperationOfAPayloadAndItsManifestFailsInTurn)
{
    ScopedDirectory directory("an-consumer-fault-document-pair");
    const QString payloadPath = qtPath(directory.path() / "drizzle.payload");
    const QString manifestPath = qtPath(directory.path() / "drizzle.json");

    Sweep sweep("io.workflow.pair", FaultKind::InputOutput);
    sweep.accept(an::ErrorCode::ExternalFailure);
    sweep.runBound(std::chrono::milliseconds{5000});
    sweep.run([&](RunContext& run) -> an::Status {
        QDir(qtPath(directory.path())).removeRecursively();
        QDir().mkpath(qtPath(directory.path()));
        FailingOperations failing;
        failing.failAt = run.point;
        const ImageIntegration::DocumentPublicationResult published = ImageIntegration::publishDocumentPair(
            payloadPath, coefficientsDocument("payload-1"), manifestPath, coefficientsDocument("manifest-1"),
            run.stop.get_token(), failing.operations());
        if (run.census())
        {
            run.opportunities = failing.made;
        }
        run.fired = failing.fired;

        // What the workflow does at its next start: a payload no manifest names is an orphan of its own.
        const QString payloadName = QFileInfo(payloadPath).fileName();
        const ImageIntegration::DocumentRecoveryReport recovered =
            ImageIntegration::recoverDocumentDirectory(qtPath(directory.path()), [&](const QString& name) {
                return name == payloadName && !QFile::exists(manifestPath);
            });
        if (!recovered.failed.isEmpty())
        {
            return an::Status::failure(an::ErrorCode::Internal, "recovery could not remove what was left");
        }
        // The pair is whole or absent: a manifest is never there without its payload, and nothing else is left.
        const bool manifest = QFile::exists(manifestPath);
        const bool payload = QFile::exists(payloadPath);
        const QStringList left = QDir(qtPath(directory.path())).entryList(QDir::Files);
        if (manifest != payload || left.size() != (manifest ? 2 : 0))
        {
            return an::Status::failure(
                an::ErrorCode::Internal,
                ("the pair is not whole after recovery: " + left.join(QStringLiteral(", "))).toStdString());
        }
        if (published.ok() != manifest)
        {
            return an::Status::failure(an::ErrorCode::Internal, "the publication's answer and the directory disagree");
        }
        return statusOf(published);
    });
}

// ---- The standalone stack.

/// The frames of one group, written and probed before any sweep, as the window probes its list.
struct StackFixture
{
    ScopedDirectory inputs{"an-consumer-fault-stack-inputs"};
    an::ui::StandaloneStackingGroup group;

    [[nodiscard]] bool prepare()
    {
        an::Result<an::consumer::EngineSession> session = an::tests::consumerfaults::openSetupSession();
        if (!session.hasValue())
        {
            ADD_FAILURE() << session.status().message();
            return false;
        }
        group.label = QStringLiteral("group");
        group.outputFileName = QStringLiteral("stacked.fit");
        for (std::uint64_t index = 0u; index < 4u; ++index)
        {
            const std::filesystem::path path = inputs.path() / ("frame_" + std::to_string(index) + ".fits");
            if (!an::tests::consumerfaults::writeFrame(path, 80u, 60u, 31u + index,
                                                       0.2 + 0.05 * static_cast<double>(index)))
            {
                return false;
            }
            an::Result<an::ui::processes::WorkflowFile> file =
                an::ui::processes::probeWorkflowFile(*session, qtPath(path), {});
            if (!file.hasValue())
            {
                ADD_FAILURE() << file.status().message();
                return false;
            }
            group.images.push_back(an::ui::StandaloneStackingFrame{.entry = file->entry,
                                                                   .filter = file->figures.filter(),
                                                                   .capturedAtUtc = file->figures.capturedAtUtc(),
                                                                   .exposureSeconds = file->figures.exposureSeconds()});
        }
        group.frameCount = static_cast<int>(group.images.size());
        return true;
    }
};

/// Stacks the group into the directory, as the window's Stack does, and lets the written files' assets go.
an::Status stackGroup(RunContext& run, const an::ui::StandaloneStackingGroup& group, const QString& outputDirectory,
                      const std::function<void(double)>& progress)
{
    an::Result<an::consumer::EngineSession> session = openSession(run);
    if (!session.hasValue())
    {
        return an::faults::settle(session);
    }
    an::ui::processes::StandaloneStackRun settings;
    settings.stacking.rejectionMethod = QStringLiteral("Sigma Clipping");
    settings.stacking.normalizationMode = NormalizationMode::None;
    settings.outputDirectory = outputDirectory;
    an::Result<an::ui::processes::StackedGroup> stacked =
        an::ui::processes::stackStandaloneGroup(*session, group, settings, run.stop.get_token(), progress);
    return an::faults::settle(stacked);
}

TEST(FaultCampaign, AnAllocationIsRefusedAtEveryPointOfAStandaloneStack)
{
    StackFixture fixture;
    ASSERT_TRUE(fixture.prepare());
    ScopedDirectory directory("an-consumer-fault-stack");

    Sweep sweep("alloc.stack.standalone", FaultKind::Allocation);
    acceptRefusals(sweep);
    sweep.destination(directory.path() / "stacked.fit");
    sweep.runBound(std::chrono::milliseconds{20000});
    sweep.run([&](RunContext& run) { return stackGroup(run, fixture.group, qtPath(directory.path()), {}); });
}

// The stack states its progress through the window's callback rather than an engine sink, so the stop is requested at
// the k-th report the window would have shown: between two of the stack's own stages, which is where a person's Cancel
// lands.
TEST(FaultCampaign, AStopIsRequestedAtEveryProgressReportOfAStandaloneStack)
{
    StackFixture fixture;
    ASSERT_TRUE(fixture.prepare());
    ScopedDirectory directory("an-consumer-fault-stack-stop");

    Sweep sweep("cancel.stack.standalone", FaultKind::Cancellation);
    // The points are the stack's own reports, which this subject counts and stops at itself; the harness's sink is
    // given none, so arming it changes nothing.
    sweep.pointsFrom(an::faults::PointSource::ProgressReports);
    acceptStops(sweep);
    sweep.destination(directory.path() / "stacked.fit");
    sweep.runBound(std::chrono::milliseconds{20000});
    sweep.run([&](RunContext& run) {
        // Point one is the stop requested before the call, which the harness arms; every later point is a report.
        std::atomic<std::uint64_t> reports{0u};
        std::atomic<bool> fired{false};
        const std::uint64_t stopAt = run.point > 1u ? run.point - 1u : 0u;
        const an::Status status = stackGroup(run, fixture.group, qtPath(directory.path()), [&](double) {
            const std::uint64_t report = reports.fetch_add(1u) + 1u;
            if (report == stopAt)
            {
                fired = true;
                run.stop.request_stop();
            }
        });
        if (run.census())
        {
            run.opportunities = reports.load();
        }
        run.fired = run.point == 1u || fired.load();
        return status;
    });
}

} // namespace
