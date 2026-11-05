// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// A comet registration's products, from the frames and the saved observations to the records the registration
/// database keeps: the run, its summary, every frame's tracking point, the comet-aligned transforms, their output plane
/// and their readiness. The run is a "PureComet" run: no star registration lies under its transforms.
///
/// The bridge reads no file: the stage lends the tracker a refiner that reads a frame through the run's registry.

#include "processes/ImageIntegration/core/CometTrackingSidecarProductWriter.h"

#include <QCryptographicHash>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace an::ui
{
struct CometRegistrationFrameInput
{
    FrameId originalFrameId;
    QString refinementSourcePath;
    QString dateObs;
    double exposureSeconds = 0.0;
    int width = 0;
    int height = 0;
    bool hasSipDistortion = false;
    QString calibrationFingerprint;
    QString chromaticCorrectionFingerprint;
    std::optional<an::RegistrationTransform> sourceToRefinementTransform;
    std::optional<an::SipDistortion> sourceDistortion;
};

struct CometRegistrationProductRequest
{
    QString groupKey;
    int settingsVersion = 0;
    QString detectionHash;
    QString registrationHash;
    QString lightFilesHash;
    QString calibrationHash;
    RegistrationSettings::CometTrackingSettings cometSettings;
    FrameId resolvedReferenceFrame;
    std::vector<CometRegistrationFrameInput> frames;
    bool allowPredictedOnlyTransforms = true;
    bool requiresMaterializedOutputs = false;
    qint64 updatedAt = 0;

    /// Reads a frame's refinement file and measures its nucleus.
    CometGuidedRefiner refiner;

    /// Whether the run was asked to stop.
    CometGuidedCancellation isCancellationRequested;
};

struct CometRegistrationProductResult
{
    bool succeeded = false;
    bool cancelled = false;
    QString message;
    CometTrackingFrameContextResult frameContexts;
    CometGuidedAutoTrackingResult tracking;
    CometTrackingSidecarProductWriteRequest sidecarRequest;
};

[[nodiscard]] inline QString cometRegistrationBridgeSha256(const QStringList& parts)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const QString& part : parts)
    {
        hash.addData(part.toUtf8());
        hash.addData(QByteArrayLiteral("\n"));
    }
    return QString::fromLatin1(hash.result().toHex());
}

[[nodiscard]] inline QString cometRegistrationBridgeContextId(const CometRegistrationProductRequest& request,
                                                              const QString& mode)
{
    return cometRegistrationBridgeSha256({QStringLiteral("comet-registration-context-v1"), request.groupKey,
                                          request.registrationHash, request.lightFilesHash, request.calibrationHash,
                                          request.resolvedReferenceFrame.canonicalPath(), mode});
}

[[nodiscard]] inline QString cometRegistrationBridgeRunId(const QString& contextId, qint64 updatedAt)
{
    return QStringLiteral("comet:%1:%2").arg(contextId, QString::number(updatedAt));
}

[[nodiscard]] inline QString cometRegistrationBridgePointId(const QString& cometRunId, const FrameId& frameId,
                                                            const QString& fitState)
{
    return QStringLiteral("point:%1")
        .arg(cometRegistrationBridgeSha256({cometRunId, frameId.canonicalPath(), fitState}));
}

[[nodiscard]] inline bool cometRegistrationBridgePointIsFinite(const an::PixelCoordinate& point)
{
    return std::isfinite(point.x) && std::isfinite(point.y);
}

[[nodiscard]] inline QPointF cometRegistrationBridgeQPoint(const an::PixelCoordinate& point)
{
    return cometRegistrationBridgePointIsFinite(point) ? QPointF(point.x, point.y) : QPointF();
}

[[nodiscard]] inline QString cometRegistrationBridgeFitModelName(const std::optional<an::CometTrackResult>& fit)
{
    if (!fit.has_value())
    {
        return QStringLiteral("none");
    }
    switch (fit->model)
    {
    case an::CometTrackModel::Linear:
        return QStringLiteral("linear_xy");
    case an::CometTrackModel::Quadratic:
        return QStringLiteral("quadratic_xy");
    case an::CometTrackModel::None:
        break;
    }
    return QStringLiteral("none");
}

[[nodiscard]] inline QString
cometRegistrationBridgeObservationOrigin(const RegistrationSettings::CometObservationOrigin origin)
{
    switch (origin)
    {
    case RegistrationSettings::CometObservationOrigin::AcceptedAutomatic:
        return QStringLiteral("AcceptedAutomatic");
    case RegistrationSettings::CometObservationOrigin::Imported:
        return QStringLiteral("Imported");
    case RegistrationSettings::CometObservationOrigin::Manual:
    default:
        return QStringLiteral("Manual");
    }
}

[[nodiscard]] inline QString
cometRegistrationBridgeObservationMethod(const RegistrationSettings::CometObservationOrigin origin)
{
    switch (origin)
    {
    case RegistrationSettings::CometObservationOrigin::AcceptedAutomatic:
        return QStringLiteral("auto_refine");
    case RegistrationSettings::CometObservationOrigin::Imported:
        return QStringLiteral("imported");
    case RegistrationSettings::CometObservationOrigin::Manual:
    default:
        return QStringLiteral("manual");
    }
}

[[nodiscard]] inline bool
cometRegistrationBridgeObservationOriginIsProductionAccepted(const RegistrationSettings::CometObservationOrigin origin)
{
    switch (origin)
    {
    case RegistrationSettings::CometObservationOrigin::AcceptedAutomatic:
    case RegistrationSettings::CometObservationOrigin::Imported:
    case RegistrationSettings::CometObservationOrigin::Manual:
        return true;
    }
    return false;
}

[[nodiscard]] inline std::optional<RegistrationSettings::CometTrackingObservation>
cometRegistrationBridgeObservationForFrame(const RegistrationSettings::CometTrackingSettings& settings,
                                           const FrameId& frameId)
{
    const auto it = std::find_if(settings.observations.cbegin(), settings.observations.cend(),
                                 [&](const RegistrationSettings::CometTrackingObservation& observation) {
                                     return observation.enabled && FrameId(observation.frameId) == frameId;
                                 });
    if (it == settings.observations.cend())
    {
        return std::nullopt;
    }
    return *it;
}

[[nodiscard]] inline std::vector<CometTrackingFrameContextInput>
cometRegistrationBridgeFrameContextInputs(const std::vector<CometRegistrationFrameInput>& frames)
{
    std::vector<CometTrackingFrameContextInput> inputs;
    inputs.reserve(frames.size());
    for (const CometRegistrationFrameInput& frame : frames)
    {
        inputs.push_back({.originalFrameId = frame.originalFrameId,
                          .refinementSourcePath = frame.refinementSourcePath,
                          .dateObs = frame.dateObs,
                          .exposureSeconds = frame.exposureSeconds,
                          .hasSipDistortion = frame.hasSipDistortion,
                          .calibrationFingerprint = frame.calibrationFingerprint,
                          .chromaticCorrectionFingerprint = frame.chromaticCorrectionFingerprint});
    }
    return inputs;
}

[[nodiscard]] inline const CometRegistrationFrameInput*
cometRegistrationBridgeFindFrameInput(const std::vector<CometRegistrationFrameInput>& frames, const FrameId& frameId)
{
    const auto it = std::find_if(frames.begin(), frames.end(), [&](const CometRegistrationFrameInput& frame) {
        return frame.originalFrameId == frameId;
    });
    return it == frames.end() ? nullptr : &(*it);
}

[[nodiscard]] inline std::vector<CometGuidedAutoTrackingFrame>
cometRegistrationBridgeGuidedFrames(const std::vector<CometTrackingFrameContext>& contexts,
                                    const std::vector<CometRegistrationFrameInput>& inputs)
{
    std::vector<CometGuidedAutoTrackingFrame> frames;
    frames.reserve(contexts.size());
    for (const CometTrackingFrameContext& context : contexts)
    {
        CometGuidedAutoTrackingFrame frame;
        frame.context = context;
        if (const CometRegistrationFrameInput* input =
                cometRegistrationBridgeFindFrameInput(inputs, context.originalFrameId);
            input != nullptr)
        {
            frame.mapping.sourceToRefinementTransform = input->sourceToRefinementTransform;
            frame.mapping.sourceDistortion = input->sourceDistortion;
        }
        frames.push_back(std::move(frame));
    }
    return frames;
}

[[nodiscard]] inline const CometGuidedAutoTrackingDiagnostic*
cometRegistrationBridgeFindDiagnostic(const std::vector<CometGuidedAutoTrackingDiagnostic>& diagnostics,
                                      const FrameId& frameId)
{
    const auto it =
        std::find_if(diagnostics.begin(), diagnostics.end(), [&](const CometGuidedAutoTrackingDiagnostic& diagnostic) {
            return diagnostic.originalFrameId == frameId;
        });
    return it == diagnostics.end() ? nullptr : &(*it);
}

[[nodiscard]] inline an::PixelCoordinate
cometRegistrationBridgeBestReferencePosition(const CometGuidedAutoTrackingDiagnostic& diagnostic)
{
    if (cometRegistrationBridgePointIsFinite(diagnostic.refinedReferencePlanePosition))
    {
        return diagnostic.refinedReferencePlanePosition;
    }
    return diagnostic.predictedReferencePlanePosition;
}

[[nodiscard]] inline an::PixelCoordinate
cometRegistrationBridgeBestSourcePosition(const CometGuidedAutoTrackingDiagnostic& diagnostic)
{
    if (cometRegistrationBridgePointIsFinite(diagnostic.refinedSourcePosition))
    {
        return diagnostic.refinedSourcePosition;
    }
    return diagnostic.predictedSourcePosition;
}

[[nodiscard]] inline int
cometRegistrationBridgePredictedOnlyCount(const std::vector<CometGuidedAutoTrackingDiagnostic>& diagnostics)
{
    return static_cast<int>(
        std::count_if(diagnostics.begin(), diagnostics.end(), [](const CometGuidedAutoTrackingDiagnostic& diagnostic) {
            return diagnostic.status == CometGuidedAutoTrackingStatus::PredictedOnly;
        }));
}

[[nodiscard]] inline QStringList cometRegistrationBridgeWarningFlags(const CometGuidedAutoTrackingResult& tracking)
{
    QStringList warnings = tracking.fitWarnings;
    if (!tracking.message.trimmed().isEmpty())
    {
        warnings << tracking.message;
    }
    const int predicted_only_count = cometRegistrationBridgePredictedOnlyCount(tracking.diagnostics);
    if (!tracking.diagnostics.empty() && predicted_only_count * 2 > static_cast<int>(tracking.diagnostics.size()))
    {
        warnings << QStringLiteral("predicted-only-high: most comet frames used predicted-only positions");
    }
    warnings.removeDuplicates();
    return warnings;
}

[[nodiscard]] inline bool cometRegistrationBridgeObservationIsUsableForProduction(
    const RegistrationSettings::CometTrackingObservation& observation,
    const std::vector<CometTrackingFrameContext>& contexts)
{
    if (!observation.enabled || observation.frameId.trimmed().isEmpty() ||
        !cometRegistrationBridgeObservationOriginIsProductionAccepted(observation.origin) ||
        !std::isfinite(observation.sourcePosition.x()) || !std::isfinite(observation.sourcePosition.y()))
    {
        return false;
    }

    const FrameId frame_id(observation.frameId);
    return std::any_of(contexts.cbegin(), contexts.cend(), [&](const CometTrackingFrameContext& context) {
        return context.originalFrameId == frame_id && context.status == CometTrackingFrameContextStatus::Ready &&
               context.midpoint.has_value();
    });
}

[[nodiscard]] inline int
cometRegistrationBridgeUsableProductionObservationCount(const RegistrationSettings::CometTrackingSettings& settings,
                                                        const std::vector<CometTrackingFrameContext>& contexts)
{
    QSet<FrameId> frames;
    for (const RegistrationSettings::CometTrackingObservation& observation : settings.observations)
    {
        if (cometRegistrationBridgeObservationIsUsableForProduction(observation, contexts))
        {
            frames.insert(FrameId(observation.frameId));
        }
    }
    return static_cast<int>(frames.size());
}

[[nodiscard]] inline CometTrackingRunRecord
cometRegistrationBridgeRunRecord(const CometRegistrationProductRequest& request, const QString& cometRunId,
                                 const QString& contextId, const QString& mode, double referenceEpoch)
{
    CometTrackingRunRecord run;
    run.cometRunId = cometRunId;
    run.groupKey = request.groupKey;
    run.settingsVersion = request.settingsVersion;
    run.cometContextId = contextId;
    run.detectionHash = request.detectionHash;
    run.registrationHash = request.registrationHash;
    run.lightFilesHash = request.lightFilesHash;
    run.calibrationHash = request.calibrationHash;
    run.resolvedReferenceFrameId = request.resolvedReferenceFrame.canonicalPath();
    run.referenceEpoch = referenceEpoch;
    run.mode = mode;
    run.status = QStringLiteral("Building");
    run.createdAt = request.updatedAt;
    run.updatedAt = request.updatedAt;
    return run;
}

[[nodiscard]] inline CometTrackingRunSummaryRecord
cometRegistrationBridgeRunSummaryRecord(const QString& cometRunId, const CometGuidedAutoTrackingResult& tracking,
                                        double referenceEpoch, qint64 updatedAt)
{
    CometTrackingRunSummaryRecord summary;
    summary.cometRunId = cometRunId;
    summary.fitModel = cometRegistrationBridgeFitModelName(tracking.fit);
    summary.referenceEpoch = referenceEpoch;
    if (tracking.fit.has_value())
    {
        summary.fitTimeOrigin = cometEpochSeconds(tracking.fit->origin);
        summary.rmsPixels = tracking.fit->rootMeanSquareResidualPixels;
        summary.maxResidualPixels = tracking.fit->maximumResidualPixels;
        summary.acceptedInputCount = static_cast<int>(tracking.fit->observationsUsed);
        summary.rejectedInputCount = static_cast<int>(tracking.fit->outliersRejected);
    }
    summary.predictedOnlyFrameCount = cometRegistrationBridgePredictedOnlyCount(tracking.diagnostics);
    summary.warningFlags = cometRegistrationBridgeWarningFlags(tracking).join(QStringLiteral("; "));
    summary.updatedAt = updatedAt;
    return summary;
}

[[nodiscard]] inline CometTrackingPointRecord
cometRegistrationBridgePointRecord(const QString& cometRunId,
                                   const RegistrationSettings::CometTrackingSettings& settings,
                                   const CometGuidedAutoTrackingDiagnostic& diagnostic,
                                   const CometGuidedAutoTrackingResult& tracking, qint64 updatedAt)
{
    const std::optional<RegistrationSettings::CometTrackingObservation> observation =
        cometRegistrationBridgeObservationForFrame(settings, diagnostic.originalFrameId);
    const bool has_observation = observation.has_value();

    QString origin = QStringLiteral("Predicted");
    QString measurement_method = QStringLiteral("predicted");
    QString fit_state = QStringLiteral("Failed");
    if (diagnostic.status == CometGuidedAutoTrackingStatus::AcceptedObservation)
    {
        origin =
            has_observation ? cometRegistrationBridgeObservationOrigin(observation->origin) : QStringLiteral("Manual");
        measurement_method =
            has_observation ? cometRegistrationBridgeObservationMethod(observation->origin) : QStringLiteral("manual");
        fit_state = QStringLiteral("Inlier");
    }
    else if (diagnostic.status == CometGuidedAutoTrackingStatus::Candidate)
    {
        origin = QStringLiteral("AutoCandidate");
        measurement_method = QStringLiteral("auto_refine");
        fit_state = QStringLiteral("Inlier");
    }
    else if (diagnostic.status == CometGuidedAutoTrackingStatus::PredictedOnly)
    {
        fit_state = QStringLiteral("PredictedOnly");
    }

    const an::PixelCoordinate source_position = cometRegistrationBridgeBestSourcePosition(diagnostic);
    const an::PixelCoordinate reference_position = cometRegistrationBridgeBestReferencePosition(diagnostic);

    CometTrackingPointRecord point;
    point.pointId = cometRegistrationBridgePointId(cometRunId, diagnostic.originalFrameId, fit_state);
    point.cometRunId = cometRunId;
    point.originalFrameId = diagnostic.originalFrameId;
    point.refinementSourcePath = diagnostic.refinementSourcePath;
    point.epoch = std::isfinite(diagnostic.midpointEpoch) ? diagnostic.midpointEpoch : 0.0;
    point.sourcePosition = cometRegistrationBridgeQPoint(source_position);
    point.transformDomainPosition = cometRegistrationBridgeQPoint(reference_position);
    point.predictedSourcePosition = cometRegistrationBridgeQPoint(diagnostic.predictedSourcePosition);
    point.referencePlanePosition = cometRegistrationBridgeQPoint(reference_position);
    point.predictedReferencePlanePosition = cometRegistrationBridgeQPoint(diagnostic.predictedReferencePlanePosition);
    point.confidence = diagnostic.confidence;
    point.snr = diagnostic.snr;
    point.measuredFwhmPixels = diagnostic.measuredFwhmPixels;
    point.measuredDiameterPixels = diagnostic.measuredDiameterPixels;
    point.measuredEllipticity = diagnostic.measuredEllipticity;
    point.apertureRadiusPixels = diagnostic.apertureRadiusPixels;
    point.localBackground = diagnostic.localBackground;
    point.roiX = diagnostic.roiX;
    point.roiY = diagnostic.roiY;
    point.roiWidth = diagnostic.roiWidth;
    point.roiHeight = diagnostic.roiHeight;
    if (point.apertureRadiusPixels <= 0.0 && has_observation && std::isfinite(observation->apertureRadiusPixels) &&
        observation->apertureRadiusPixels > 0.0)
    {
        point.apertureRadiusPixels = observation->apertureRadiusPixels;
    }
    if (cometRegistrationBridgePointIsFinite(reference_position) &&
        cometRegistrationBridgePointIsFinite(diagnostic.predictedReferencePlanePosition))
    {
        point.residualPixels = std::hypot(reference_position.x - diagnostic.predictedReferencePlanePosition.x,
                                          reference_position.y - diagnostic.predictedReferencePlanePosition.y);
    }
    if (tracking.productionFitAvailable && tracking.fit.has_value() &&
        cometRegistrationBridgePointIsFinite(reference_position))
    {
        if (const std::optional<an::UtcInstant> midpoint = cometInstantOfEpochSeconds(diagnostic.midpointEpoch))
        {
            if (const an::Result<an::PixelCoordinate> fitted = an::evaluateCometTrack(*tracking.fit, *midpoint);
                fitted.hasValue())
            {
                point.residualPixels = std::hypot(reference_position.x - fitted->x, reference_position.y - fitted->y);
            }
        }
    }
    point.measurementMethod = measurement_method;
    point.origin = origin;
    point.fitState = fit_state;
    point.rejectionReason = fit_state == QStringLiteral("Failed") ? diagnostic.message : QString();
    point.updatedAt = updatedAt;
    return point;
}

[[nodiscard]] inline CometGuidedAutoTrackingDiagnostic
cometRegistrationBridgeFailedDiagnostic(const CometTrackingFrameContext& context)
{
    CometGuidedAutoTrackingDiagnostic diagnostic;
    diagnostic.originalFrameId = context.originalFrameId;
    diagnostic.refinementSourcePath = context.refinementSourcePath;
    diagnostic.midpointEpoch = context.midpointEpoch;
    diagnostic.status = CometGuidedAutoTrackingStatus::Failed;
    diagnostic.message = context.message;
    return diagnostic;
}

[[nodiscard]] inline std::vector<CometTrackingPointRecord>
cometRegistrationBridgePointRecords(const QString& cometRunId,
                                    const RegistrationSettings::CometTrackingSettings& settings,
                                    const std::vector<CometTrackingFrameContext>& contexts,
                                    const CometGuidedAutoTrackingResult& tracking, qint64 updatedAt)
{
    std::vector<CometTrackingPointRecord> points;
    points.reserve(contexts.size());
    for (const CometTrackingFrameContext& context : contexts)
    {
        if (const CometGuidedAutoTrackingDiagnostic* diagnostic =
                cometRegistrationBridgeFindDiagnostic(tracking.diagnostics, context.originalFrameId);
            diagnostic != nullptr)
        {
            points.push_back(
                cometRegistrationBridgePointRecord(cometRunId, settings, *diagnostic, tracking, updatedAt));
            continue;
        }

        const CometGuidedAutoTrackingDiagnostic failed = cometRegistrationBridgeFailedDiagnostic(context);
        points.push_back(cometRegistrationBridgePointRecord(cometRunId, settings, failed, tracking, updatedAt));
    }
    return points;
}

[[nodiscard]] inline std::optional<an::PixelCoordinate>
cometRegistrationBridgeReferenceCometPosition(const CometGuidedAutoTrackingResult& tracking,
                                              const FrameId& referenceFrame)
{
    const CometGuidedAutoTrackingDiagnostic* diagnostic =
        cometRegistrationBridgeFindDiagnostic(tracking.diagnostics, referenceFrame);
    if (diagnostic == nullptr)
    {
        return std::nullopt;
    }
    const an::PixelCoordinate position = cometRegistrationBridgeBestReferencePosition(*diagnostic);
    if (!cometRegistrationBridgePointIsFinite(position))
    {
        return std::nullopt;
    }
    return position;
}

[[nodiscard]] inline int cometRegistrationBridgeCanvasWidth(const CometRegistrationProductRequest& request)
{
    if (const CometRegistrationFrameInput* reference =
            cometRegistrationBridgeFindFrameInput(request.frames, request.resolvedReferenceFrame);
        reference != nullptr)
    {
        return reference->width;
    }
    return request.frames.empty() ? 0 : request.frames.front().width;
}

[[nodiscard]] inline int cometRegistrationBridgeCanvasHeight(const CometRegistrationProductRequest& request)
{
    if (const CometRegistrationFrameInput* reference =
            cometRegistrationBridgeFindFrameInput(request.frames, request.resolvedReferenceFrame);
        reference != nullptr)
    {
        return reference->height;
    }
    return request.frames.empty() ? 0 : request.frames.front().height;
}

[[nodiscard]] inline double cometRegistrationBridgeReferenceEpoch(const CometTrackingFrameContextResult& contexts,
                                                                  const CometGuidedAutoTrackingResult& tracking)
{
    const auto it =
        std::find_if(contexts.contexts.begin(), contexts.contexts.end(), [](const CometTrackingFrameContext& context) {
            return context.isReferenceFrame && std::isfinite(context.midpointEpoch);
        });
    if (it != contexts.contexts.end())
    {
        return it->midpointEpoch;
    }
    return tracking.fit.has_value() ? cometEpochSeconds(tracking.fit->origin) : 0.0;
}

/// The comet registration's products: the frames placed in time, the comet tracked, the transforms built and the
/// records written into a sidecar request; fails, saying why, when fewer than two saved observations are usable.
///
/// @param execution the caller's admitted execution, which the fit runs under.
/// @param request the registration's context, its frames and its refiner.
/// @return the products; cancelled when the run was asked to stop.
[[nodiscard]] inline CometRegistrationProductResult
buildCometRegistrationProducts(const an::ExecutionContext& execution, const CometRegistrationProductRequest& request)
{
    CometRegistrationProductResult result;
    const QString mode = QStringLiteral("PureComet");
    if (request.groupKey.trimmed().isEmpty() || request.registrationHash.trimmed().isEmpty() ||
        request.lightFilesHash.trimmed().isEmpty() ||
        request.resolvedReferenceFrame.canonicalPath().trimmed().isEmpty())
    {
        result.message = QStringLiteral("Comet registration context is incomplete.");
        return result;
    }

    CometTrackingFrameContextRequest context_request;
    context_request.cometSettings = request.cometSettings;
    context_request.resolvedReferenceFrame = request.resolvedReferenceFrame;
    context_request.frames = cometRegistrationBridgeFrameContextInputs(request.frames);
    result.frameContexts = buildCometTrackingFrameContexts(context_request);
    if (!result.frameContexts.succeeded)
    {
        result.message = result.frameContexts.message;
        return result;
    }

    const std::vector<CometGuidedAutoTrackingFrame> guided_frames =
        cometRegistrationBridgeGuidedFrames(result.frameContexts.contexts, request.frames);

    const int usable_observation_count =
        cometRegistrationBridgeUsableProductionObservationCount(request.cometSettings, result.frameContexts.contexts);
    if (usable_observation_count < 2)
    {
        result.message = QStringLiteral("Comet registration requires at least two saved enabled comet observations "
                                        "with finite positions and timestamps. Use the comet tracking dialog to save "
                                        "manual, imported, or accepted automatic observations before running "
                                        "production registration.");
        return result;
    }

    CometGuidedAutoTrackingRequest tracking_request;
    tracking_request.frames = guided_frames;
    tracking_request.observations = request.cometSettings.observations;
    tracking_request.trackingOptions = request.cometSettings.trackingOptions;
    tracking_request.fitOptions = request.cometSettings.fitOptions;
    tracking_request.refiner = request.refiner;
    tracking_request.isCancellationRequested = request.isCancellationRequested;
    result.tracking = runCometGuidedAutoTracking(execution, tracking_request);
    if (result.tracking.cancelled)
    {
        result.cancelled = true;
        result.message = result.tracking.message;
        return result;
    }
    if (!result.tracking.succeeded)
    {
        result.message = result.tracking.message;
        return result;
    }
    if (!result.tracking.productionFitAvailable)
    {
        result.message = QStringLiteral("Comet registration did not produce a production comet fit from the saved "
                                        "enabled observations.");
        return result;
    }

    const QString context_id = cometRegistrationBridgeContextId(request, mode);
    const QString comet_run_id = cometRegistrationBridgeRunId(context_id, request.updatedAt);
    const double reference_epoch = cometRegistrationBridgeReferenceEpoch(result.frameContexts, result.tracking);

    CometTransformProductRequest product_request;
    product_request.cometRunId = comet_run_id;
    product_request.alignmentMode = mode;
    product_request.frames = guided_frames;
    product_request.diagnostics = result.tracking.diagnostics;
    product_request.referenceFrameId = request.resolvedReferenceFrame;
    product_request.referenceCometPosition =
        cometRegistrationBridgeReferenceCometPosition(result.tracking, request.resolvedReferenceFrame);
    product_request.canvasWidth = cometRegistrationBridgeCanvasWidth(request);
    product_request.canvasHeight = cometRegistrationBridgeCanvasHeight(request);
    product_request.allowPredictedOnlyTransforms = request.allowPredictedOnlyTransforms;
    product_request.requiresMaterializedOutputs = request.requiresMaterializedOutputs;
    product_request.updatedAt = request.updatedAt;
    CometTransformProductResult transform_products = buildCometTransformProducts(product_request);

    result.sidecarRequest.run =
        cometRegistrationBridgeRunRecord(request, comet_run_id, context_id, mode, reference_epoch);
    result.sidecarRequest.summary =
        cometRegistrationBridgeRunSummaryRecord(comet_run_id, result.tracking, reference_epoch, request.updatedAt);
    result.sidecarRequest.points = cometRegistrationBridgePointRecords(
        comet_run_id, request.cometSettings, result.frameContexts.contexts, result.tracking, request.updatedAt);
    result.sidecarRequest.transformProducts = std::move(transform_products);
    result.sidecarRequest.registeredOutputs.clear();

    result.succeeded = result.sidecarRequest.transformProducts.succeeded;
    result.message = result.succeeded ? QStringLiteral("Comet registration sidecar products are transform-ready.")
                                      : result.sidecarRequest.transformProducts.message;
    return result;
}
} // namespace an::ui
