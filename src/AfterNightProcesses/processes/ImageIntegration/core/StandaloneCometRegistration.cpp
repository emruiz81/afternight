// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The standalone registration's comet registration, beside the integration's comet registration products it records:
/// the comet's track fitted over the observations the settings hold, the run, its transforms and its output plane
/// recorded in the registration database, every frame written aligned on the comet, and the run activated for
/// stacking. It runs the integration's comet chain; the standalone window reaches it only through
/// `runStandaloneCometRegistration`.

#include "stdafx.hpp"

#include "processes/ImageIntegration/core/CometRegistrationProductBridge.h"
#include "processes/ImageIntegration/core/FilenameUtils.h"
#include "processes/ImageIntegration/workflow/ArtifactSchema.h"
#include "processes/ImageIntegration/workflow/ArtifactTracker.h"
#include "processes/ImageIntegration/workflow/WorkflowArtifactStamp.h"
#include "processes/ImageIntegration/workflow/stages/RegistrationFrames.h"
#include "processes/ImageIntegration/workflow/stages/StageParsingUtils.h"
#include "processes/core/RegistrationMetricsWriter.h"
#include "processes/core/RegistrationRun.h"
#include "processes/core/WorkflowToolCompanions.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/Registration/CometDetection.h>
#include <AfterNightEngine/Algorithms/Stars/StarDetection.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <optional>
#include <unordered_map>
#include <utility>

namespace
{

[[nodiscard]] an::Status cometFailure(const QString& message)
{
    return an::Status::failure(an::ErrorCode::ExternalFailure,
                               an::StatusDetail{.message = message.toStdString(), .operation = "comet registration"});
}

[[nodiscard]] an::Status stopped()
{
    return an::Status::failure(an::ErrorCode::Cancelled, an::StatusDetail{.message = "the registration stopped"});
}

/// The identity a comet-aligned output satisfies: its family, the run's transform for the frame, the plane and the
/// file it was read from.
[[nodiscard]] QString registeredOutputHash(const an::ui::CometTransformRoleRecord& transform,
                                           const an::ui::CometOutputPlaneRecord& plane, const QString& inputPath)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("schemaVersion"), 1);
    ImageIntegration::schema::insertArtifactFamilyIdentity(obj,
                                                           ImageIntegration::schema::ArtifactFamily::RegisteredLight);
    obj.insert(QStringLiteral("role"), QStringLiteral("comet_aligned"));
    obj.insert(QStringLiteral("cometRunId"), transform.cometRunId);
    obj.insert(QStringLiteral("frame"), transform.frameId.canonicalPath());
    obj.insert(QStringLiteral("transform"), transform.transformJson);
    obj.insert(QStringLiteral("sourceDistortion"), transform.sourceDistortionJson);
    obj.insert(QStringLiteral("plane"), plane.sourceFingerprint);
    const QFileInfo input(inputPath);
    obj.insert(QStringLiteral("input"), QDir::cleanPath(inputPath));
    if (input.exists())
    {
        obj.insert(QStringLiteral("inputSize"), static_cast<qint64>(input.size()));
        obj.insert(QStringLiteral("inputModified"), input.lastModified().toMSecsSinceEpoch());
    }
    return QString::fromLatin1(
        QCryptographicHash::hash(QJsonDocument(obj).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256)
            .toHex());
}

/// The comet registration's frame as the registration database records it: its comet transform, or its failure.
[[nodiscard]] an::ui::RegisteredFrame registeredFrameOf(const an::ui::CometTransformRoleRecord& record)
{
    an::ui::RegisteredFrame frame;
    frame.sourcePath = record.frameId.canonicalPath();
    frame.failed = true;
    frame.metrics.succeeded = false;
    if (!record.succeeded)
    {
        return frame;
    }
    an::RegistrationTransform transform;
    stage_utils::SourceDistortionProvenance provenance;
    QString error;
    if (stage_utils::parseRegistrationTransformJson(record.transformJson, transform, &provenance, error))
    {
        frame.transform = transform;
        frame.distortion = provenance;
        frame.failed = false;
        frame.metrics.succeeded = true;
    }
    return frame;
}

} // namespace

namespace an::ui::processes
{

an::Result<StandaloneCometRegistrationOutcome>
runStandaloneCometRegistration(const an::consumer::EngineSession& session,
                               const StandaloneCometRegistrationRequest& request, std::stop_token stop,
                               const std::function<void(int percent, const QString& text)>& progress)
{
    const RegistrationSettings& registrationSettings = request.settings;
    const StandaloneRegistrationPersistenceContext& persistenceContext = request.context;
    if (!registrationSettings.cometTracking.has_value())
    {
        return cometFailure(QStringLiteral("Comet tracking settings are required."));
    }
    const auto& cometSettings = registrationSettings.cometTracking.value();
    if (progress)
    {
        progress(0, QStringLiteral("Fitting comet track..."));
    }

    an::Result<an::consumer::OperationScope> scope = session.admit(
        an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Batch, .stopToken = stop});
    if (!scope.hasValue())
    {
        return scope.status();
    }
    const RegistrationDemosaic demosaic = registrationDemosaicOf(registrationSettings);

    std::vector<an::ui::CometRegistrationFrameInput> bridge_frames;
    std::unordered_map<QString, FrameId> frames_by_id;
    for (const FrameInfo& info : request.frames)
    {
        if (request.ignoredFrameIds.contains(info.id.canonicalPath()))
        {
            continue;
        }
        const QString path = info.id.canonicalPath();
        an::Result<stage_utils::RegistrationFrameFacts> facts =
            stage_utils::probeRegistrationFrame(*scope, nullptr, path);
        if (!facts.hasValue())
        {
            return cometFailure(QStringLiteral("Could not read '%1'.").arg(path));
        }
        // The comet's track is fitted against each frame's exposure; a frame that states no start cannot be placed
        // on it.
        if (facts->dateObs.isEmpty())
        {
            return cometFailure(QStringLiteral("Comet registration requires DATE-OBS on frame '%1'.").arg(path));
        }
        an::ui::CometRegistrationFrameInput bridge_frame;
        bridge_frame.originalFrameId = info.id;
        bridge_frame.refinementSourcePath = path;
        bridge_frame.dateObs = facts->dateObs;
        bridge_frame.exposureSeconds = facts->exposureSeconds.value_or(0.0);
        bridge_frame.width = static_cast<int>(facts->width);
        bridge_frame.height = static_cast<int>(facts->height);
        bridge_frame.hasSipDistortion = facts->distortion.has_value();
        bridge_frame.sourceDistortion = facts->distortion;
        bridge_frames.push_back(std::move(bridge_frame));
        frames_by_id.emplace(path, info.id);
    }

    // The tracker measures each frame's nucleus in the frame read once while the tracker asks about it.
    struct RefinementFrame
    {
        QString path;
        std::optional<an::Image<float>> image;
        std::optional<an::ImageLayout> layout;
        /// The samples the nucleus is measured over: every finite one when the measured channel holds one that is not,
        /// as a light calibrated where its flat is zero does.
        an::KernelValidityView validity;
        std::uint32_t channel = 0u;
    };
    RefinementFrame refinementFrame;
    an::ui::CometRegistrationProductRequest product_request;
    product_request.groupKey = persistenceContext.groupKey;
    product_request.settingsVersion = persistenceContext.settingsVersion;
    product_request.detectionHash = persistenceContext.detectionHash;
    product_request.registrationHash = persistenceContext.registrationHash;
    product_request.lightFilesHash = persistenceContext.lightFilesHash;
    product_request.calibrationHash = persistenceContext.calibrationHash;
    product_request.cometSettings = cometSettings;
    product_request.resolvedReferenceFrame = FrameId(registrationSettings.referenceImage);
    product_request.frames = std::move(bridge_frames);
    product_request.allowPredictedOnlyTransforms = true;
    product_request.requiresMaterializedOutputs = false;
    product_request.updatedAt = QDateTime::currentSecsSinceEpoch();
    product_request.isCancellationRequested = [&stop]() { return stop.stop_requested(); };
    product_request.refiner =
        [&](const an::ui::CometTrackingFrameContext& context, const an::PixelCoordinate& seed,
            const an::CometCenterRefinementOptions& options) -> an::Result<an::ui::CometRefinementMeasurement> {
        if (refinementFrame.path != context.refinementSourcePath || !refinementFrame.image.has_value())
        {
            refinementFrame = RefinementFrame{};
            an::Result<an::Image<float>> image =
                stage_utils::readRegistrationImage(*scope, nullptr, context.refinementSourcePath, nullptr, demosaic);
            if (!image.hasValue())
            {
                return image.status();
            }
            const auto view = std::as_const(*image).view();
            if (!view.has_value())
            {
                return an::Status::failure(an::ErrorCode::NotInitialized, "the comet frame holds nothing");
            }
            refinementFrame.layout = view->layout();
            const std::optional<an::ImageDescriptor> described = image->descriptor();
            refinementFrame.channel = described.has_value() && described->extent().channelCount() >= 3u ? 1u : 0u;
            refinementFrame.validity = registrationChannelValidity(*image, refinementFrame.channel);
            refinementFrame.image = std::move(*image);
            refinementFrame.path = context.refinementSourcePath;
        }
        const an::Image<float>& image = *refinementFrame.image;
        const auto view = image.view();
        const std::optional<an::ImageDescriptor> descriptor = image.descriptor();
        if (!view.has_value() || !descriptor.has_value())
        {
            return an::Status::failure(an::ErrorCode::NotInitialized, "the comet frame holds nothing");
        }
        const an::Result<an::StarFrameView> frameView =
            an::StarFrameView::create(view->data(), refinementFrame.layout->totalBytes(), *refinementFrame.layout,
                                      *descriptor, refinementFrame.validity);
        if (!frameView.hasValue())
        {
            return frameView.status();
        }
        an::CometCenterRefinementOptions measured = options;
        measured.channelIndex = refinementFrame.channel;
        an::Result<an::CometCenterRefinementResult> refined =
            an::refineCometCenter(scope->execution(), *frameView, seed, measured);
        if (!refined.hasValue())
        {
            return refined.status();
        }
        return an::ui::CometRefinementMeasurement{
            .refined = *refined, .width = descriptor->extent().width(), .height = descriptor->extent().height()};
    };

    an::ui::CometRegistrationProductResult products =
        an::ui::buildCometRegistrationProducts(scope->execution(), product_request);
    refinementFrame = RefinementFrame{};
    if (products.cancelled || stop.stop_requested())
    {
        return stopped();
    }
    if (!products.succeeded)
    {
        return cometFailure(products.message);
    }

    an::ui::RegisteredFrameSet registered;
    for (const an::ui::CometTransformRoleRecord& record : products.sidecarRequest.transformProducts.transforms)
    {
        registered.frames.insert_or_assign(record.frameId.canonicalPath(), registeredFrameOf(record));
    }
    registered.referencePath = FrameId(registrationSettings.referenceImage).canonicalPath();
    QString persistence_error;
    if (!an::ui::persistStandaloneRegistrationState(persistenceContext, request.frames, registered, &persistence_error))
    {
        return cometFailure(QStringLiteral("Failed to persist comet registration state: %1").arg(persistence_error));
    }

    an::ui::RegistrationMetricsWriter writer(persistenceContext.databasePath,
                                             QStringLiteral("StandaloneCometRegistration"));
    QString write_error;
    if (!writer.initialize(&write_error))
    {
        return cometFailure(QStringLiteral("Failed to open registration metrics database: %1").arg(write_error));
    }
    if (!an::ui::writeCometTrackingSidecarProducts(writer, products.sidecarRequest, &write_error).succeeded)
    {
        return cometFailure(QStringLiteral("Failed to persist comet sidecar products: %1").arg(write_error));
    }

    const an::ui::CometOutputPlaneRecord& plane = products.sidecarRequest.transformProducts.outputPlane;
    if (plane.canvasWidth <= 0 || plane.canvasHeight <= 0)
    {
        return cometFailure(QStringLiteral("Comet output plane '%1' has invalid dimensions %2x%3.")
                                .arg(plane.cometOutputPlaneId)
                                .arg(plane.canvasWidth)
                                .arg(plane.canvasHeight));
    }
    const an::RegistrationCanvas canvas{.origin =
                                            an::PixelCoordinate{.x = plane.canvasOffsetX, .y = plane.canvasOffsetY},
                                        .width = static_cast<std::uint32_t>(plane.canvasWidth),
                                        .height = static_cast<std::uint32_t>(plane.canvasHeight)};
    const QString run_dir =
        QDir(persistenceContext.outputRoot)
            .filePath(QStringLiteral("comet_aligned/%1")
                          .arg(ImageIntegration::sanitizeFilename(products.sidecarRequest.run.cometRunId)));
    if (!QDir().mkpath(run_dir))
    {
        return cometFailure(QStringLiteral("Failed to create comet-aligned run directory '%1'.").arg(run_dir));
    }
    if (progress)
    {
        progress(35, QStringLiteral("Materializing comet-aligned outputs..."));
    }

    const bool forceFits = std::any_of(request.frames.cbegin(), request.frames.cend(), [](const FrameInfo& frame) {
        return !ImageIntegration::isFitsExtension(QFileInfo(frame.id.canonicalPath()).suffix());
    });
    const an::Result<an::KernelInterpolation> interpolation =
        alignmentInterpolationOf(registrationSettings.interpolation);
    const an::KernelInterpolation sampling =
        interpolation.hasValue() ? *interpolation : an::KernelInterpolation::Lanczos4;

    int succeeded_outputs = 0;
    int failed_outputs = 0;
    QStringList failure_messages;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const auto& transforms = products.sidecarRequest.transformProducts.transforms;
    int done = 0;
    for (const an::ui::CometTransformRoleRecord& record : transforms)
    {
        if (stop.stop_requested())
        {
            return stopped();
        }
        const auto frame_it = frames_by_id.find(record.frameId.canonicalPath());
        if (frame_it == frames_by_id.end())
        {
            continue;
        }
        const QString input_path = frame_it->first;
        const QFileInfo input_info(input_path);
        an::ui::CometRegisteredOutputRecord output;
        output.cometRunId = products.sidecarRequest.run.cometRunId;
        output.frameId = frame_it->second;
        output.role = QStringLiteral("comet_aligned");
        output.cometOutputPlaneId = plane.cometOutputPlaneId;
        output.outputPath = QDir(run_dir).filePath(input_info.completeBaseName() + QStringLiteral("_comet_reg.") +
                                                   (forceFits ? QStringLiteral("fits") : input_info.suffix()));
        output.updatedAt = now;
        output.artifactHash = registeredOutputHash(record, plane, input_path);

        const an::Status written = [&]() -> an::Status {
            an::RegistrationTransform transform;
            stage_utils::SourceDistortionProvenance provenance;
            QString parse_error;
            if (!record.succeeded ||
                !stage_utils::parseRegistrationTransformJson(record.transformJson, transform, &provenance, parse_error))
            {
                return cometFailure(parse_error.isEmpty() ? QStringLiteral("the frame has no comet transform")
                                                          : parse_error);
            }
            std::optional<an::SipDistortion> distortion =
                provenance.policy == stage_utils::SourceDistortionPolicy::ExplicitModel ? provenance.model
                                                                                        : std::nullopt;
            if (!record.sourceDistortionJson.trimmed().isEmpty())
            {
                const QJsonDocument document = QJsonDocument::fromJson(record.sourceDistortionJson.toUtf8());
                an::SipDistortion parsed;
                if (document.isObject() && stage_utils::sipDistortionFromJson(document.object(), parsed))
                {
                    distortion = parsed;
                }
            }
            an::Result<an::Image<float>> image =
                stage_utils::readRegistrationImage(*scope, nullptr, input_path, nullptr, demosaic);
            if (!image.hasValue())
            {
                return image.status();
            }
            // A comet-aligned frame carries no world coordinates: its plane follows the comet, not the sky.
            an::Result<an::Image<float>> warped = stage_utils::warpRegistrationFrame(
                *scope, *image, transform, distortion, canvas, sampling, std::nullopt);
            if (!warped.hasValue())
            {
                return warped.status();
            }
            an::Result<an::ArtifactIdentity> identity =
                ImageIntegration::workflowArtifactIdentity(output.artifactHash, output.cometRunId);
            if (!identity.hasValue())
            {
                return identity.status();
            }
            return ImageIntegration::publishWorkflowImage(*scope, *warped, output.outputPath, *identity);
        }();
        if (written.failed() &&
            (written.code() == an::ErrorCode::Cancelled || written.code() == an::ErrorCode::DeadlineExceeded))
        {
            return stopped();
        }
        if (written.ok())
        {
            if (const auto revision = ArtifactTracker::captureFileRevision(output.outputPath); revision.has_value())
            {
                output.succeeded = true;
                output.outputPath = revision->path;
                output.outputSizeBytes = revision->sizeBytes;
                output.outputModifiedAtMs = revision->modifiedAtMs;
                output.outputIdentityToken = revision->identityToken;
            }
            else
            {
                output.failureReason = QStringLiteral("Failed to capture comet registered output revision");
            }
        }
        else
        {
            output.failureReason = QString::fromUtf8(written.message());
        }
        if (output.succeeded)
        {
            ++succeeded_outputs;
        }
        else
        {
            ++failed_outputs;
            failure_messages.push_back(
                QStringLiteral("%1 (%2)").arg(output.frameId.canonicalPath(), output.failureReason));
        }
        if (!writer.upsertCometRegisteredOutput(output, &write_error))
        {
            return cometFailure(QStringLiteral("Failed to persist comet registered output: %1").arg(write_error));
        }
        ++done;
        if (progress)
        {
            progress(35 + (64 * done) / (std::max)(1, static_cast<int>(transforms.size())),
                     QStringLiteral("Materializing comet-aligned image %1 of %2").arg(done).arg(transforms.size()));
        }
    }

    an::ui::CometRoleReadinessRecord readiness = products.sidecarRequest.transformProducts.readiness;
    readiness.requiresMaterializedOutputs = true;
    readiness.materializedOutputCount = succeeded_outputs;
    readiness.failedOutputCount = failed_outputs;
    readiness.missingOutputCount = (std::max)(0, readiness.selectedFrameCount - succeeded_outputs - failed_outputs);
    // The readiness table accepts Failed and OutputMissing for a role whose outputs are not all written.
    readiness.readinessStatus = failed_outputs > 0                 ? QStringLiteral("Failed")
                                : readiness.missingOutputCount > 0 ? QStringLiteral("OutputMissing")
                                                                   : QStringLiteral("Ready");
    readiness.failureReason = failure_messages.join(QStringLiteral("; "));
    readiness.updatedAt = now;
    if (!writer.upsertCometRoleReadiness(readiness, &write_error))
    {
        return cometFailure(QStringLiteral("Failed to update comet readiness: %1").arg(write_error));
    }
    if (failed_outputs > 0 || readiness.missingOutputCount > 0)
    {
        return cometFailure(readiness.failureReason);
    }
    if (!writer.activateCometTrackingRun(products.sidecarRequest.run.cometRunId, {QStringLiteral("comet_aligned")},
                                         true, &write_error))
    {
        return cometFailure(QStringLiteral("Failed to activate comet registration run: %1").arg(write_error));
    }

    StandaloneCometRegistrationOutcome outcome;
    outcome.cometRunId = products.sidecarRequest.run.cometRunId;
    outcome.alignedOutputs = succeeded_outputs;
    const auto loaded = an::ui::loadStandaloneRegistrationState(persistenceContext, request.frames, &persistence_error);
    outcome.registered = loaded.has_value() ? loaded->registeredData : std::move(registered);
    if (progress)
    {
        progress(100, QStringLiteral("Comet registration complete"));
    }
    return outcome;
}

} // namespace an::ui::processes
