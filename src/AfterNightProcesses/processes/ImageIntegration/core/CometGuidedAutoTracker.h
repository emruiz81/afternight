// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The comet's track from the observations a person saved, and the comet found again in every other frame near where
/// the track places it.
///
/// Two or more saved observations fit the track (`fitCometTrack`), evaluated at each frame's midpoint; a frame with an
/// observation keeps it, and every other frame's nucleus is refined around the prediction (`refineCometCenter`) and
/// kept as a candidate when it clears the signal threshold, or left as the prediction. A single observation is a
/// bootstrap: the frames after it are searched around its position for a candidate, and no track is fitted.
///
/// The tracker reads no file: the stage that runs it lends a refiner that reads a frame and measures its nucleus.

#include "processes/ImageIntegration/core/CometTrackingCoordinateMapper.h"
#include "processes/ImageIntegration/core/RegistrationSettingsUtils.h"

#include <AfterNightEngine/Algorithms/Registration/CometDetection.h>
#include <AfterNightEngine/Algorithms/Registration/CometTracking.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>

#include <QString>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <vector>

namespace an
{
class ExecutionContext;
} // namespace an

namespace an::ui
{
enum class CometGuidedAutoTrackingStatus
{
    AcceptedObservation,
    Candidate,
    PredictedOnly,
    Failed,
    Cancelled
};

struct CometGuidedAutoTrackingFrame
{
    CometTrackingFrameContext context;
    CometTrackingCoordinateMapping mapping;
};

/// A coordinate no tracker produced.
inline constexpr an::PixelCoordinate kCometUnsetPosition{std::numeric_limits<double>::quiet_NaN(),
                                                         std::numeric_limits<double>::quiet_NaN()};

struct CometGuidedAutoTrackingDiagnostic
{
    FrameId originalFrameId;
    QString refinementSourcePath;
    double midpointEpoch = std::numeric_limits<double>::quiet_NaN();
    an::PixelCoordinate predictedReferencePlanePosition = kCometUnsetPosition;
    an::PixelCoordinate predictedSourcePosition = kCometUnsetPosition;
    an::PixelCoordinate refinedSourcePosition = kCometUnsetPosition;
    an::PixelCoordinate refinedReferencePlanePosition = kCometUnsetPosition;
    double snr = 0.0;
    double confidence = 0.0;
    double measuredFwhmPixels = 0.0;
    double measuredDiameterPixels = 0.0;
    double measuredEllipticity = 0.0;
    double apertureRadiusPixels = 0.0;
    double localBackground = 0.0;
    double roiX = 0.0;
    double roiY = 0.0;
    double roiWidth = 0.0;
    double roiHeight = 0.0;
    CometGuidedAutoTrackingStatus status = CometGuidedAutoTrackingStatus::Failed;
    QString message;
};

/// One nucleus measurement in a frame's refinement file, and that file's extent.
struct CometRefinementMeasurement
{
    an::CometCenterRefinementResult refined;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
};

/// Reads a frame's refinement file and measures the nucleus around a seed, in that file's coordinates.
using CometGuidedRefiner = std::function<an::Result<CometRefinementMeasurement>(
    const CometTrackingFrameContext&, const an::PixelCoordinate&, const an::CometCenterRefinementOptions&)>;
using CometGuidedCancellation = std::function<bool()>;

struct CometGuidedAutoTrackingRequest
{
    std::vector<CometGuidedAutoTrackingFrame> frames;
    QVector<RegistrationSettings::CometTrackingObservation> observations;
    RegistrationSettings::CometTrackingOptions trackingOptions;
    RegistrationSettings::CometFitOptions fitOptions;
    CometGuidedRefiner refiner;
    CometGuidedCancellation isCancellationRequested;
};

struct CometGuidedAutoTrackingResult
{
    bool succeeded = false;
    bool cancelled = false;
    bool productionFitAvailable = false;
    bool bootstrapOnly = false;

    /// The fitted track, when one was fitted.
    std::optional<an::CometTrackResult> fit;

    /// What the fit says about itself, worded for the run's summary.
    QStringList fitWarnings;
    std::vector<CometGuidedAutoTrackingDiagnostic> diagnostics;
    std::vector<CometGuidedAutoTrackingDiagnostic> candidates;
    QString message;
};

[[nodiscard]] inline bool cometGuidedAutoTrackingCancelled(const CometGuidedAutoTrackingRequest& request)
{
    return request.isCancellationRequested && request.isCancellationRequested();
}

[[nodiscard]] inline bool cometGuidedAutoTrackingPointIsFinite(const an::PixelCoordinate& point)
{
    return std::isfinite(point.x) && std::isfinite(point.y);
}

[[nodiscard]] inline an::PixelCoordinate cometGuidedAutoTrackingPointFromQPoint(const QPointF& point)
{
    return an::PixelCoordinate{point.x(), point.y()};
}

[[nodiscard]] inline const CometGuidedAutoTrackingFrame*
findCometGuidedAutoTrackingFrame(const std::vector<CometGuidedAutoTrackingFrame>& frames, const FrameId& frame_id)
{
    const auto it = std::find_if(frames.begin(), frames.end(), [&](const CometGuidedAutoTrackingFrame& frame) {
        return frame.context.originalFrameId == frame_id;
    });
    return it == frames.end() ? nullptr : &(*it);
}

[[nodiscard]] inline std::optional<RegistrationSettings::CometTrackingObservation>
findCometGuidedAutoTrackingAcceptedObservation(const CometGuidedAutoTrackingRequest& request, const FrameId& frame_id)
{
    const auto it = std::find_if(request.observations.cbegin(), request.observations.cend(),
                                 [&](const RegistrationSettings::CometTrackingObservation& observation) {
                                     return observation.enabled && FrameId(observation.frameId) == frame_id;
                                 });
    if (it == request.observations.cend())
    {
        return std::nullopt;
    }
    return *it;
}

[[nodiscard]] inline bool isCometGuidedAutoTrackingAcceptedObservation(const CometGuidedAutoTrackingRequest& request,
                                                                       const FrameId& frame_id)
{
    return findCometGuidedAutoTrackingAcceptedObservation(request, frame_id).has_value();
}

/// The track request the saved fit options state: "linear" forces constant velocity, "quadratic" prefers acceleration,
/// any other model lets the fit choose; without robust rejection no observation is ever an outlier.
[[nodiscard]] inline an::CometTrackRequest
makeCometGuidedAutoTrackingFitOptions(const RegistrationSettings::CometFitOptions& settings)
{
    using ImageIntegration::RegistrationSettingsUtils::cometFitModelKey;
    using ImageIntegration::RegistrationSettingsUtils::kSmallestCometOutlierSigma;
    an::CometTrackRequest request;
    const QString model = cometFitModelKey(settings.model);
    if (model == QStringLiteral("linear"))
    {
        request.modelPolicy = an::CometTrackModelPolicy::ForceLinear;
    }
    else if (model == QStringLiteral("quadratic"))
    {
        request.modelPolicy = an::CometTrackModelPolicy::PreferQuadratic;
    }
    else
    {
        request.modelPolicy = an::CometTrackModelPolicy::Automatic;
    }
    request.outlierSigma =
        settings.robustRejection ? (std::max)(settings.outlierSigma, kSmallestCometOutlierSigma) : 1.0e12;
    return request;
}

static_assert(ImageIntegration::RegistrationSettingsUtils::kLargestCometSearchRadius ==
                  static_cast<int>(an::kMaximumCometCenterRadius),
              "the Track Comet dialog offers the search radii the engine's centre refinement measures");

/// The refinement the saved tracking options state: the search radius, at most the engine's largest, and the
/// signal-to-noise threshold.
[[nodiscard]] inline an::CometCenterRefinementOptions
makeCometGuidedAutoTrackingRefinementOptions(const RegistrationSettings::CometTrackingOptions& settings)
{
    an::CometCenterRefinementOptions options;
    options.roiRadius = static_cast<std::uint32_t>(
        ImageIntegration::RegistrationSettingsUtils::effectiveCometSearchRadius(settings.searchRadiusPixels));
    options.minimumSignalToNoise = (std::max)(settings.minimumSnr, 1.0e-6);
    return options;
}

[[nodiscard]] inline double cometGuidedAutoTrackingConfidence(double snr, double minimum_snr)
{
    const double denominator = (std::max)(minimum_snr * 4.0, minimum_snr + 1.0e-6);
    if (!(denominator > 0.0))
    {
        return snr > 0.0 ? 1.0 : 0.0;
    }
    return (std::min)(1.0, (std::max)(0.0, snr / denominator));
}

inline void recordCometGuidedAutoTrackingMeasurement(CometGuidedAutoTrackingDiagnostic& diagnostic,
                                                     const CometRefinementMeasurement& measurement,
                                                     const an::PixelCoordinate& refinement_seed,
                                                     const an::CometCenterRefinementOptions& options)
{
    const an::CometCenterRefinementResult& refined = measurement.refined;
    diagnostic.snr = refined.signalToNoise;
    diagnostic.measuredFwhmPixels =
        std::isfinite(refined.fullWidthAtHalfMaximumPixels) ? refined.fullWidthAtHalfMaximumPixels : 0.0;
    diagnostic.measuredEllipticity = std::isfinite(refined.ellipticity) ? refined.ellipticity : 0.0;
    diagnostic.apertureRadiusPixels = std::isfinite(refined.apertureRadiusPixels) ? refined.apertureRadiusPixels : 0.0;
    diagnostic.measuredDiameterPixels =
        diagnostic.apertureRadiusPixels > 0.0 ? diagnostic.apertureRadiusPixels * 2.0 : diagnostic.measuredFwhmPixels;
    diagnostic.localBackground = std::isfinite(refined.localBackground) ? refined.localBackground : 0.0;

    // The region the measurement read: the seed's pixel and the radius around it, clipped to the file.
    const long radius = static_cast<long>(options.roiRadius);
    const long seed_x = static_cast<long>(std::floor(refinement_seed.x));
    const long seed_y = static_cast<long>(std::floor(refinement_seed.y));
    const long left = (std::max)(0L, seed_x - radius);
    const long top = (std::max)(0L, seed_y - radius);
    const long right = (std::min)(static_cast<long>(measurement.width), seed_x + radius + 1L);
    const long bottom = (std::min)(static_cast<long>(measurement.height), seed_y + radius + 1L);
    diagnostic.roiX = static_cast<double>(left);
    diagnostic.roiY = static_cast<double>(top);
    diagnostic.roiWidth = static_cast<double>((std::max)(0L, right - left));
    diagnostic.roiHeight = static_cast<double>((std::max)(0L, bottom - top));
}

[[nodiscard]] inline CometGuidedAutoTrackingDiagnostic
makeCometGuidedAutoTrackingDiagnostic(const CometGuidedAutoTrackingFrame& frame)
{
    CometGuidedAutoTrackingDiagnostic diagnostic;
    diagnostic.originalFrameId = frame.context.originalFrameId;
    diagnostic.refinementSourcePath = frame.context.refinementSourcePath;
    diagnostic.midpointEpoch = frame.context.midpointEpoch;
    return diagnostic;
}

inline void appendCometGuidedAutoTrackingCandidate(CometGuidedAutoTrackingResult& result,
                                                   const CometGuidedAutoTrackingDiagnostic& diagnostic)
{
    result.diagnostics.push_back(diagnostic);
    result.candidates.push_back(diagnostic);
}

/// The saved observations as the track is fitted from them: each enabled observation of a frame placed in time, mapped
/// onto the comet's reference plane, in the settings' order.
[[nodiscard]] inline std::optional<std::vector<an::CometTrackObservation>>
makeCometGuidedAutoTrackingFitObservations(const CometGuidedAutoTrackingRequest& request,
                                           CometGuidedAutoTrackingResult& result)
{
    std::vector<an::CometTrackObservation> observations;
    observations.reserve(static_cast<std::size_t>(request.observations.size()));

    for (const RegistrationSettings::CometTrackingObservation& observation : request.observations)
    {
        if (!observation.enabled)
        {
            continue;
        }

        const FrameId frame_id(observation.frameId);
        const auto frame_it =
            std::find_if(request.frames.begin(), request.frames.end(), [&](const CometGuidedAutoTrackingFrame& frame) {
                return frame.context.originalFrameId == frame_id;
            });
        if (frame_it == request.frames.end() || frame_it->context.status != CometTrackingFrameContextStatus::Ready ||
            !frame_it->context.midpoint.has_value())
        {
            continue;
        }

        const an::PixelCoordinate source_position = cometGuidedAutoTrackingPointFromQPoint(observation.sourcePosition);
        if (!cometGuidedAutoTrackingPointIsFinite(source_position))
        {
            result.message = QStringLiteral("Accepted comet observation has a non-finite source position.");
            return std::nullopt;
        }

        const CometTrackingCoordinateMappingResult reference_position =
            mapCometSourceToReferencePlane(frame_it->mapping, source_position);
        if (!reference_position.succeeded)
        {
            result.message = reference_position.message;
            return std::nullopt;
        }

        // The frame's position in the request is its identity in the fit; an observation saved twice for one frame
        // is one observation.
        const auto identity = static_cast<std::uint64_t>(std::distance(request.frames.begin(), frame_it)) + 1u;
        if (std::any_of(observations.begin(), observations.end(),
                        [identity](const an::CometTrackObservation& kept) { return kept.frameId == identity; }))
        {
            continue;
        }
        observations.push_back(an::CometTrackObservation{.frameId = identity,
                                                         .midpoint = *frame_it->context.midpoint,
                                                         .position = reference_position.point,
                                                         .accepted = true,
                                                         .required = true});
    }

    return observations;
}

inline void markCometGuidedAutoTrackingCancelled(CometGuidedAutoTrackingResult& result)
{
    result.succeeded = false;
    result.cancelled = true;
    result.message = QStringLiteral("Comet guided auto-tracking was cancelled.");
}

[[nodiscard]] inline CometGuidedAutoTrackingDiagnostic
makeCometGuidedAutoTrackingPredictedDiagnostic(const CometGuidedAutoTrackingFrame& frame,
                                               const an::PixelCoordinate& predicted_reference_position,
                                               const QString& message)
{
    CometGuidedAutoTrackingDiagnostic diagnostic = makeCometGuidedAutoTrackingDiagnostic(frame);
    diagnostic.predictedReferencePlanePosition = predicted_reference_position;
    diagnostic.refinedReferencePlanePosition = predicted_reference_position;
    diagnostic.status = CometGuidedAutoTrackingStatus::PredictedOnly;
    diagnostic.message = message;

    const CometTrackingCoordinateMappingResult predicted_source =
        mapCometReferencePlaneToSource(frame.mapping, predicted_reference_position);
    if (predicted_source.succeeded)
    {
        diagnostic.predictedSourcePosition = predicted_source.point;
        diagnostic.refinedSourcePosition = predicted_source.point;
    }
    else
    {
        diagnostic.status = CometGuidedAutoTrackingStatus::Failed;
        diagnostic.message = predicted_source.message;
    }

    return diagnostic;
}

/// The refiner's answer, or nothing when it could not read the frame; a stop the refiner reports marks the result
/// cancelled.
[[nodiscard]] inline std::optional<CometRefinementMeasurement>
refineCometGuidedAutoTrackingNucleus(const CometGuidedAutoTrackingRequest& request,
                                     const CometGuidedAutoTrackingFrame& frame, const an::PixelCoordinate& seed,
                                     const an::CometCenterRefinementOptions& options, QString& reason, bool& cancelled)
{
    cancelled = false;
    if (!request.refiner)
    {
        reason = QStringLiteral("No refinement image loader is available.");
        return std::nullopt;
    }
    const an::Result<CometRefinementMeasurement> measured = request.refiner(frame.context, seed, options);
    if (!measured.hasValue())
    {
        cancelled = measured.status().code() == an::ErrorCode::Cancelled;
        reason =
            QStringLiteral("No refinement image available: %1").arg(QString::fromUtf8(measured.status().message()));
        return std::nullopt;
    }
    return *measured;
}

inline bool refineCometGuidedAutoTrackingCandidate(const CometGuidedAutoTrackingRequest& request,
                                                   const CometGuidedAutoTrackingFrame& frame,
                                                   const an::PixelCoordinate& predicted_reference_position,
                                                   CometGuidedAutoTrackingResult& result)
{
    CometGuidedAutoTrackingDiagnostic diagnostic = makeCometGuidedAutoTrackingPredictedDiagnostic(
        frame, predicted_reference_position, QStringLiteral("Predicted-only comet diagnostic."));
    if (diagnostic.status == CometGuidedAutoTrackingStatus::Failed)
    {
        result.diagnostics.push_back(diagnostic);
        return false;
    }

    const CometTrackingCoordinateMappingResult refinement_seed =
        mapCometReferencePlaneToRefinement(frame.mapping, predicted_reference_position);
    if (!refinement_seed.succeeded)
    {
        diagnostic.status = CometGuidedAutoTrackingStatus::Failed;
        diagnostic.message = refinement_seed.message;
        result.diagnostics.push_back(diagnostic);
        return false;
    }

    const an::CometCenterRefinementOptions refinement_options =
        makeCometGuidedAutoTrackingRefinementOptions(request.trackingOptions);
    QString reason;
    bool cancelled = false;
    const std::optional<CometRefinementMeasurement> measured = refineCometGuidedAutoTrackingNucleus(
        request, frame, refinement_seed.point, refinement_options, reason, cancelled);
    if (cancelled)
    {
        markCometGuidedAutoTrackingCancelled(result);
        return false;
    }
    if (!measured.has_value())
    {
        diagnostic.message = reason + QStringLiteral(" Keeping predicted-only comet diagnostic.");
        result.diagnostics.push_back(diagnostic);
        return false;
    }

    recordCometGuidedAutoTrackingMeasurement(diagnostic, *measured, refinement_seed.point, refinement_options);
    diagnostic.confidence =
        cometGuidedAutoTrackingConfidence(measured->refined.signalToNoise, request.trackingOptions.minimumSnr);
    if (measured->refined.outcome != an::CometCenterOutcome::Refined ||
        measured->refined.signalToNoise < request.trackingOptions.minimumSnr)
    {
        diagnostic.message =
            QStringLiteral("Comet refinement did not meet the SNR threshold; keeping predicted-only comet diagnostic.");
        result.diagnostics.push_back(diagnostic);
        return false;
    }

    const CometTrackingCoordinateMappingResult refined_source =
        mapCometRefinementToSource(frame.mapping, measured->refined.center);
    if (!refined_source.succeeded)
    {
        diagnostic.status = CometGuidedAutoTrackingStatus::Failed;
        diagnostic.message = refined_source.message;
        result.diagnostics.push_back(diagnostic);
        return false;
    }

    const CometTrackingCoordinateMappingResult refined_reference =
        mapCometSourceToReferencePlane(frame.mapping, refined_source.point);
    if (!refined_reference.succeeded)
    {
        diagnostic.status = CometGuidedAutoTrackingStatus::Failed;
        diagnostic.message = refined_reference.message;
        result.diagnostics.push_back(diagnostic);
        return false;
    }

    diagnostic.refinedSourcePosition = refined_source.point;
    diagnostic.refinedReferencePlanePosition = refined_reference.point;
    diagnostic.status = CometGuidedAutoTrackingStatus::Candidate;
    diagnostic.message = QStringLiteral("Automatic comet candidate refined.");
    appendCometGuidedAutoTrackingCandidate(result, diagnostic);
    return true;
}

/// The fit's own observations, worded: no redundancy, a quadratic, a high residual, too little motion.
[[nodiscard]] inline QStringList cometGuidedAutoTrackingFitWarnings(const an::CometTrackResult& fit)
{
    QStringList warnings;
    if (fit.diagnostics.noRedundancy)
    {
        warnings << QStringLiteral("no-redundancy: only the minimum number of observations survived the fit");
    }
    if (fit.diagnostics.highResidual)
    {
        warnings << QStringLiteral("high-residual: the track's root-mean-square residual is %1 px")
                        .arg(fit.rootMeanSquareResidualPixels, 0, 'f', 2);
    }
    if (fit.diagnostics.motionBelowThreshold)
    {
        warnings << QStringLiteral("small-motion: the comet moved %1 px over the fitted span")
                        .arg(fit.motionPixels, 0, 'f', 2);
    }
    return warnings;
}

[[nodiscard]] inline CometGuidedAutoTrackingResult
runCometGuidedAutoTrackingProductionFit(const an::ExecutionContext& execution,
                                        const CometGuidedAutoTrackingRequest& request,
                                        const std::vector<an::CometTrackObservation>& observations)
{
    CometGuidedAutoTrackingResult result;
    an::CometTrackRequest track_request = makeCometGuidedAutoTrackingFitOptions(request.fitOptions);
    // The coefficients are expressed at the first saved observation's time.
    track_request.coefficientOrigin = observations.front().midpoint;
    const an::Result<an::CometTrackResult> fit = an::fitCometTrack(execution, observations, track_request);
    if (!fit.hasValue())
    {
        if (fit.status().code() == an::ErrorCode::Cancelled)
        {
            markCometGuidedAutoTrackingCancelled(result);
            return result;
        }
        result.message = QString::fromUtf8(fit.status().message());
        return result;
    }

    result.fit = *fit;
    result.fitWarnings = cometGuidedAutoTrackingFitWarnings(*fit);
    result.succeeded = true;
    result.productionFitAvailable = true;

    for (const CometGuidedAutoTrackingFrame& frame : request.frames)
    {
        if (cometGuidedAutoTrackingCancelled(request) || result.cancelled)
        {
            markCometGuidedAutoTrackingCancelled(result);
            result.diagnostics.clear();
            result.candidates.clear();
            return result;
        }
        if (frame.context.status != CometTrackingFrameContextStatus::Ready || !frame.context.midpoint.has_value())
        {
            continue;
        }

        const an::Result<an::PixelCoordinate> predicted_reference =
            an::evaluateCometTrack(*result.fit, *frame.context.midpoint);
        if (!predicted_reference.hasValue())
        {
            CometGuidedAutoTrackingDiagnostic diagnostic = makeCometGuidedAutoTrackingDiagnostic(frame);
            diagnostic.status = CometGuidedAutoTrackingStatus::Failed;
            diagnostic.message = QStringLiteral("Unable to evaluate comet trajectory for frame.");
            result.diagnostics.push_back(diagnostic);
            continue;
        }

        if (const std::optional<RegistrationSettings::CometTrackingObservation> observation =
                findCometGuidedAutoTrackingAcceptedObservation(request, frame.context.originalFrameId))
        {
            CometGuidedAutoTrackingDiagnostic diagnostic = makeCometGuidedAutoTrackingPredictedDiagnostic(
                frame, *predicted_reference, QStringLiteral("Accepted comet observation."));
            diagnostic.status = diagnostic.status == CometGuidedAutoTrackingStatus::Failed
                                    ? CometGuidedAutoTrackingStatus::Failed
                                    : CometGuidedAutoTrackingStatus::AcceptedObservation;
            diagnostic.refinedSourcePosition = cometGuidedAutoTrackingPointFromQPoint(observation->sourcePosition);
            result.diagnostics.push_back(diagnostic);
            continue;
        }

        refineCometGuidedAutoTrackingCandidate(request, frame, *predicted_reference, result);
    }

    if (result.cancelled)
    {
        result.diagnostics.clear();
        result.candidates.clear();
        return result;
    }
    result.message = QStringLiteral("Comet guided auto-tracking completed.");
    return result;
}

[[nodiscard]] inline CometGuidedAutoTrackingResult
runCometGuidedAutoTrackingBootstrap(const CometGuidedAutoTrackingRequest& request,
                                    const std::vector<an::CometTrackObservation>& observations)
{
    CometGuidedAutoTrackingResult result;
    if (observations.empty())
    {
        result.message = QStringLiteral("Comet guided auto-tracking requires at least one accepted observation.");
        return result;
    }

    const CometGuidedAutoTrackingFrame& seed_frame =
        request.frames[static_cast<std::size_t>(observations.front().frameId - 1u)];
    const FrameId seed_frame_id = seed_frame.context.originalFrameId;
    const std::optional<RegistrationSettings::CometTrackingObservation> seed_observation =
        findCometGuidedAutoTrackingAcceptedObservation(request, seed_frame_id);
    if (!seed_observation.has_value())
    {
        result.message = QStringLiteral("Single-seed comet bootstrap requires an accepted seed observation.");
        return result;
    }

    result.succeeded = true;
    result.bootstrapOnly = true;

    const an::PixelCoordinate seed_source_position =
        cometGuidedAutoTrackingPointFromQPoint(seed_observation->sourcePosition);
    const int max_bootstrap_frames = (std::max)(0, request.trackingOptions.maxBootstrapFrames);
    int inspected_frames = 0;
    for (const CometGuidedAutoTrackingFrame& frame : request.frames)
    {
        if (cometGuidedAutoTrackingCancelled(request))
        {
            markCometGuidedAutoTrackingCancelled(result);
            result.diagnostics.clear();
            result.candidates.clear();
            return result;
        }
        if (frame.context.originalFrameId == seed_frame_id ||
            frame.context.status != CometTrackingFrameContextStatus::Ready)
        {
            continue;
        }
        if (inspected_frames >= max_bootstrap_frames)
        {
            break;
        }
        ++inspected_frames;

        CometGuidedAutoTrackingDiagnostic diagnostic = makeCometGuidedAutoTrackingDiagnostic(frame);
        diagnostic.predictedSourcePosition = seed_source_position;
        diagnostic.refinedSourcePosition = seed_source_position;

        const an::CometCenterRefinementOptions refinement_options =
            makeCometGuidedAutoTrackingRefinementOptions(request.trackingOptions);
        QString reason;
        bool cancelled = false;
        const std::optional<CometRefinementMeasurement> measured = refineCometGuidedAutoTrackingNucleus(
            request, frame, seed_source_position, refinement_options, reason, cancelled);
        if (cancelled)
        {
            markCometGuidedAutoTrackingCancelled(result);
            result.diagnostics.clear();
            result.candidates.clear();
            return result;
        }
        if (!measured.has_value())
        {
            diagnostic.status = CometGuidedAutoTrackingStatus::Failed;
            diagnostic.message = reason;
            result.diagnostics.push_back(diagnostic);
            continue;
        }

        recordCometGuidedAutoTrackingMeasurement(diagnostic, *measured, seed_source_position, refinement_options);
        diagnostic.confidence =
            cometGuidedAutoTrackingConfidence(measured->refined.signalToNoise, request.trackingOptions.minimumSnr);
        if (measured->refined.outcome != an::CometCenterOutcome::Refined ||
            measured->refined.signalToNoise < request.trackingOptions.minimumSnr)
        {
            diagnostic.status = CometGuidedAutoTrackingStatus::Failed;
            diagnostic.message = QStringLiteral("Bootstrap comet refinement did not meet the SNR threshold.");
            result.diagnostics.push_back(diagnostic);
            continue;
        }

        const an::PixelCoordinate center = measured->refined.center;
        const double motion_pixels = std::hypot(center.x - seed_source_position.x, center.y - seed_source_position.y);
        if (motion_pixels < 1.0)
        {
            diagnostic.status = CometGuidedAutoTrackingStatus::Failed;
            diagnostic.message =
                QStringLiteral("Bootstrap comet candidate is not sufficiently separated from the seed observation.");
            result.diagnostics.push_back(diagnostic);
            continue;
        }

        const CometTrackingCoordinateMappingResult refined_reference =
            mapCometSourceToReferencePlane(frame.mapping, center);
        if (!refined_reference.succeeded)
        {
            diagnostic.status = CometGuidedAutoTrackingStatus::Failed;
            diagnostic.message = refined_reference.message;
            result.diagnostics.push_back(diagnostic);
            continue;
        }

        diagnostic.predictedReferencePlanePosition = observations.front().position;
        diagnostic.refinedSourcePosition = center;
        diagnostic.refinedReferencePlanePosition = refined_reference.point;
        diagnostic.status = CometGuidedAutoTrackingStatus::Candidate;
        diagnostic.message = QStringLiteral("Single-seed bootstrap proposed an automatic comet candidate.");
        appendCometGuidedAutoTrackingCandidate(result, diagnostic);
    }

    result.message = result.candidates.empty() ? QStringLiteral("Single-seed comet bootstrap did not find a candidate.")
                                               : QStringLiteral("Single-seed comet bootstrap completed.");
    return result;
}

/// Tracks the comet: a fitted track with two or more saved observations, a bootstrap with one.
///
/// @param execution the caller's admitted execution, which the fit runs under.
/// @param request the frames, the saved observations, the options and the refiner.
/// @return the track and every frame's diagnostic; cancelled when the request's stop or the refiner says so.
[[nodiscard]] inline CometGuidedAutoTrackingResult
runCometGuidedAutoTracking(const an::ExecutionContext& execution, const CometGuidedAutoTrackingRequest& request)
{
    CometGuidedAutoTrackingResult result;
    if (cometGuidedAutoTrackingCancelled(request))
    {
        markCometGuidedAutoTrackingCancelled(result);
        return result;
    }

    std::optional<std::vector<an::CometTrackObservation>> observations =
        makeCometGuidedAutoTrackingFitObservations(request, result);
    if (!observations.has_value())
    {
        return result;
    }

    if (observations->size() >= 2u)
    {
        return runCometGuidedAutoTrackingProductionFit(execution, request, *observations);
    }
    if (observations->size() == 1u)
    {
        return runCometGuidedAutoTrackingBootstrap(request, *observations);
    }

    result.message = QStringLiteral("Comet guided auto-tracking requires at least one accepted observation.");
    return result;
}
} // namespace an::ui
