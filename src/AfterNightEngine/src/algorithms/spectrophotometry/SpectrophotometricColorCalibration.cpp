// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - spectrophotometric colour calibration: the plan, the matched-colour
// prediction, the apply tail, calibration from samples and the whole calibration.
//
// The plan does once what does not depend on the frame: it compiles the response onto the
// catalog's grid, integrates the white reference, and probes the fit's request with no samples so a
// malformed field is refused before any frame is read. The whole calibration then runs its stages in
// order and stops at the first refusal: star detection in the green channel, preflight source
// selection, the catalog re-match and its quality gate, the matched stars' predicted colours,
// aperture photometry, the gain fit, the sky measurement and write, and the report's assembly. Its
// progress is one unit per stage; detection and measurement report inside their own stage's unit.

#include <AfterNightEngine/Algorithms/Spectrophotometry/SpectrophotometricColorCalibration.h>

#include "runtime/ProgressRange.h"

#include <AfterNightEngine/Algorithms/Photometry/PhotometricColorCalibration.h>
#include <AfterNightEngine/Algorithms/Stars/StarDetection.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/DiagnosticSink.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "algorithms/spectrophotometry/SpectrophotometryInternal.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace an
{
namespace
{

using spectrophotometry_detail::failure;

constexpr const char* kPlanOperation = "an::planSpectrophotometricColorCalibration";
constexpr const char* kPredictOperation = "an::predictMatchedCatalogColors";
constexpr const char* kApplyOperation = "an::applySpectrophotometricGains";
constexpr const char* kSamplesOperation = "an::calibrateSpectrophotometricColorFromSamples";
constexpr const char* kCalibrateOperation = "an::calibrateSpectrophotometricColor";
constexpr std::uint32_t kGreenChannel = 1u;

/// Whether a value is a finite positive number.
///
/// @param value the value.
/// @return true when finite and above zero.
[[nodiscard]] bool positiveFinite(const double value) noexcept
{
    return std::isfinite(value) && value > 0.0;
}

/// Whether every field of a calibration request that the plan can judge is inside its range.
///
/// The fit request is checked by the fit itself when the plan probes it, and the aperture request
/// by the sample builder when the calibration runs; everything else is checked here.
///
/// @param request the request.
/// @return success, or the first field that refused.
[[nodiscard]] Status validateRequest(const SpectrophotometricColorCalibrationRequest& request)
{
    if (request.maximumStars == 0u || request.maximumStars > kMaxStarsPerFrame)
    {
        return failure(ErrorCode::InvalidArgument, "the star limit lies between one and the frame ceiling",
                       kPlanOperation);
    }
    if (!std::isfinite(request.sourceSelection.minimumSignalToNoise) ||
        request.sourceSelection.minimumSignalToNoise < 0.0)
    {
        return failure(ErrorCode::InvalidArgument, "the source signal-to-noise floor is finite and not negative",
                       kPlanOperation);
    }
    if (request.sourceSelection.maximumSources.has_value() && *request.sourceSelection.maximumSources == 0u)
    {
        return failure(ErrorCode::InvalidArgument, "the source ceiling is at least one when stated", kPlanOperation);
    }
    const WcsPreflightRequest& preflight = request.preflight;
    if (!positiveFinite(preflight.matchRadiusArcseconds))
    {
        return failure(ErrorCode::InvalidArgument, "the match radius is finite and positive", kPlanOperation);
    }
    if (!std::isfinite(preflight.magnitudeLimit) || !std::isfinite(preflight.observationEpochYear))
    {
        return failure(ErrorCode::InvalidArgument, "the magnitude limit and the observation epoch are finite",
                       kPlanOperation);
    }
    if (!positiveFinite(preflight.residualThresholdPixels) || !positiveFinite(preflight.residualThresholdArcseconds))
    {
        return failure(ErrorCode::InvalidArgument, "the residual thresholds are finite and positive", kPlanOperation);
    }
    if (preflight.autoMagnitude.enabled &&
        (!positiveFinite(preflight.autoMagnitude.minimumLimit) || !positiveFinite(preflight.autoMagnitude.step)))
    {
        return failure(ErrorCode::InvalidArgument, "the automatic magnitude floor and step are finite and positive",
                       kPlanOperation);
    }
    if (!isApertureCentroidSource(request.samples.centroidSource))
    {
        return failure(ErrorCode::InvalidArgument, "the centroid source is one this build implements", kPlanOperation);
    }
    if (!isColorCalibrationBackgroundMode(request.background.mode))
    {
        return failure(ErrorCode::InvalidArgument, "the background mode is not one this build implements",
                       kPlanOperation);
    }
    if (!isColorCalibrationClipping(request.clipping))
    {
        return failure(ErrorCode::InvalidArgument, "the clipping policy is not one this build implements",
                       kPlanOperation);
    }
    if (!request.allowNarrowbandInput && isNarrowbandFilterName(request.filterName))
    {
        return failure(ErrorCode::Unsupported,
                       "a narrowband frame has no broadband colour to calibrate unless the request admits it",
                       kPlanOperation);
    }
    return Status::success();
}

/// The checks every frame-writing entry point makes.
///
/// @param execution the operation.
/// @param frame the frame.
/// @param destination the destination.
/// @param destinationBytes its size.
/// @param operation the entry point speaking.
/// @return success, or the first refusal.
[[nodiscard]] Status checkFrameAndDestination(const ExecutionContext& execution, const StarFrameView& frame,
                                              const void* const destination, const std::uint64_t destinationBytes,
                                              const char* const operation)
{
    if (!execution.valid())
    {
        return failure(ErrorCode::NotInitialized, "a spectrophotometric calibration needs an execution context",
                       operation);
    }
    if (const Status stopped = execution.checkCancellation(); stopped.failed())
    {
        return stopped;
    }
    if (!frame.valid())
    {
        return failure(ErrorCode::InvalidArgument, "a spectrophotometric calibration needs a checked frame", operation);
    }
    if (!spectrophotometry_detail::isRgbFrame(frame) || frame.descriptor().colorSpace() != ColorSpace::LinearRgb)
    {
        return failure(ErrorCode::Unsupported,
                       "a spectrophotometric calibration needs a three-channel linear RGB frame", operation);
    }
    const SampleType sampleType = frame.descriptor().sampleType();
    if (sampleType != SampleType::Float32 && sampleType != SampleType::Float64)
    {
        return failure(ErrorCode::Unsupported,
                       "a colour calibration writes a floating frame; an integer frame converts first", operation);
    }
    if (destination == nullptr || destinationBytes < frame.layout().totalBytes())
    {
        return failure(ErrorCode::InvalidArgument, "the destination holds every byte the layout names", operation);
    }
    return Status::success();
}

/// A fit refused by its verdict, as the failure a calibration returns.
///
/// @param verdict the verdict.
/// @param operation the entry point speaking.
/// @return the failure.
[[nodiscard]] Status refusedFit(const SpectrophotometricFitVerdict verdict, const char* const operation)
{
    return failure(ErrorCode::InvalidData,
                   std::string("the spectrophotometric fit was refused: ") + spectrophotometricFitVerdictName(verdict),
                   operation);
}

} // namespace

Result<SpectrophotometricColorCalibrationPlan>
planSpectrophotometricColorCalibration(const ExecutionContext& execution, const ResponseProfile& profile,
                                       const WhiteReferenceSpectrum& whiteReference,
                                       const ObservationModel& observation, const SpectrumCatalogManifest& manifest,
                                       const SpectrophotometricColorCalibrationRequest& request)
{
    if (!execution.valid())
    {
        return failure(ErrorCode::NotInitialized, "planning a calibration needs an execution context", kPlanOperation);
    }
    if (const Status stopped = execution.checkCancellation(); stopped.failed())
    {
        return stopped;
    }
    if (const Status valid = validateRequest(request); valid.failed())
    {
        return valid;
    }
    Result<PreparedResponse> response = prepareResponse(execution, profile, observation, manifest.grid);
    if (!response.hasValue())
    {
        return response.status();
    }
    const Result<std::array<double, 3u>> counts = integrateWhiteReference(execution, *response, whiteReference);
    if (!counts.hasValue())
    {
        return counts.status();
    }
    for (const double count : *counts)
    {
        if (!positiveFinite(count))
        {
            return failure(ErrorCode::InvalidData,
                           "the white reference measures as a non-positive count in a channel of this response",
                           kPlanOperation);
        }
    }

    SpectrophotometricColorCalibrationPlan plan;
    plan.response = std::move(*response);
    plan.whiteReferenceCounts = *counts;
    plan.whiteReferenceGains = whiteReferenceGains(*counts);
    plan.request = request;
    plan.request.samples.requireAcceptableWcsQuality = request.requireAcceptableWcsQuality;
    plan.request.samples.qualityThresholds = request.qualityThresholds;
    plan.request.fit.whiteReferenceGains = plan.whiteReferenceGains;
    plan.identity.responseProfileId = profile.identifier;
    plan.identity.responseProfileVersion = profile.version;
    plan.identity.whiteReferenceId = whiteReference.identifier;
    plan.identity.whiteReferenceVersion = whiteReference.version;
    plan.identity.catalogFamilyId = manifest.catalogFamilyId;
    plan.identity.catalogManifestId = manifest.manifestId;

    // The fit checks its own request; probing it over no samples reports a malformed field as the
    // fit would, and reports nothing else.
    const Result<SpectrophotometricFitResult> probe =
        fitSpectrophotometricGains(execution, std::span<const SpectrophotometricSample>{}, plan.request.fit);
    if (!probe.hasValue())
    {
        return probe.status();
    }
    return plan;
}

Result<MatchedColorPrediction> predictMatchedCatalogColors(const ExecutionContext& execution,
                                                           const PreparedResponse& response,
                                                           const SpectrumCatalog& catalog,
                                                           const std::span<const PreflightMatch> matches)
{
    if (matches.empty())
    {
        return failure(ErrorCode::EmptySelection, "the preflight produced no match to predict", kPredictOperation);
    }
    std::vector<std::uint64_t> identifiers;
    identifiers.reserve(matches.size());
    for (const PreflightMatch& match : matches)
    {
        identifiers.push_back(match.sourceIdentifier);
    }
    std::vector<SyntheticPhotometry> predictions(matches.size());
    Result<SyntheticPhotometryBatchResult> batch =
        integrateSyntheticPhotometryBatch(execution, response, catalog, identifiers, predictions);
    if (!batch.hasValue())
    {
        return batch.status();
    }
    MatchedColorPrediction prediction;
    prediction.batch = *batch;
    prediction.colors.resize(matches.size());
    for (std::size_t index = 0u; index < matches.size(); ++index)
    {
        prediction.colors[index].available = predictions[index].status == SyntheticPhotometryStatus::Integrated;
        prediction.colors[index].relativeToGreen = predictions[index].relativeToGreen;
    }
    return prediction;
}

Result<SpectrophotometricApplyResult>
applySpectrophotometricGains(const ExecutionContext& execution, const StarFrameView& frame, void* const destination,
                             const std::uint64_t destinationBytes, const ColorChannelGains& gains,
                             const SpectrophotometricBackgroundRequest& background,
                             const ColorCalibrationClipping clipping)
{
    if (const Status checked =
            checkFrameAndDestination(execution, frame, destination, destinationBytes, kApplyOperation);
        checked.failed())
    {
        return checked;
    }
    if (!isColorCalibrationBackgroundMode(background.mode))
    {
        return failure(ErrorCode::InvalidArgument, "the background mode is not one this build implements",
                       kApplyOperation);
    }
    SpectrophotometricApplyResult result;
    if (background.neutralize)
    {
        Result<ColorBackgroundEstimate> estimate = estimateColorBackground(execution, frame, background.region);
        if (!estimate.hasValue())
        {
            return estimate.status();
        }
        result.offsets =
            colorCalibrationOffsets(gains, estimate->levels, background.mode, meanBackgroundPedestal(estimate->levels));
        result.background = *estimate;
    }
    Result<ColorCalibrationApplyResult> applied = applyColorCalibration(
        execution, frame, destination, destinationBytes,
        ColorCalibrationApplyRequest{.gains = gains, .offsets = result.offsets, .clipping = clipping});
    if (!applied.hasValue())
    {
        return applied.status();
    }
    result.apply = *applied;
    return result;
}

Result<SpectrophotometricSampleCalibrationResult> calibrateSpectrophotometricColorFromSamples(
    const ExecutionContext& execution, const SpectrophotometricColorCalibrationPlan& plan, const StarFrameView& frame,
    void* const destination, const std::uint64_t destinationBytes,
    const std::span<const SpectrophotometricSample> samples)
{
    if (const Status checked =
            checkFrameAndDestination(execution, frame, destination, destinationBytes, kSamplesOperation);
        checked.failed())
    {
        return checked;
    }
    SpectrophotometricSampleCalibrationResult result;
    Result<SpectrophotometricFitResult> fit = fitSpectrophotometricGains(execution, samples, plan.request.fit);
    if (!fit.hasValue())
    {
        return fit.status();
    }
    result.fit = std::move(*fit);
    if (!result.fit.accepted())
    {
        return refusedFit(result.fit.verdict, kSamplesOperation);
    }
    Result<SpectrophotometricApplyResult> applied =
        applySpectrophotometricGains(execution, frame, destination, destinationBytes, result.fit.gains,
                                     plan.request.background, plan.request.clipping);
    if (!applied.hasValue())
    {
        return applied.status();
    }
    result.applied = std::move(*applied);
    return result;
}

Result<SpectrophotometricColorCalibrationResult>
calibrateSpectrophotometricColor(const ExecutionContext& execution, const SpectrophotometricColorCalibrationPlan& plan,
                                 const StarFrameView& frame, void* const destination,
                                 const std::uint64_t destinationBytes, const WcsSolution& solution,
                                 const SpectrumCatalog& catalog, const StarDetectionRequest& detectionRequest)
{
    if (const Status checked =
            checkFrameAndDestination(execution, frame, destination, destinationBytes, kCalibrateOperation);
        checked.failed())
    {
        return checked;
    }
    if (!catalog.valid())
    {
        return failure(ErrorCode::NotInitialized, "a spectrophotometric calibration needs a built spectrum catalog",
                       kCalibrateOperation);
    }
    if (!(catalog.grid() == plan.response.grid))
    {
        return failure(ErrorCode::InvalidArgument, "the catalog is not on the grid the plan was compiled onto",
                       kCalibrateOperation);
    }
    const SpectrophotometricColorCalibrationRequest& request = plan.request;
    SpectrophotometricColorCalibrationResult result{};
    constexpr std::uint64_t kStages = kSpectrophotometricCalibrationStageCount;
    execution.reportProgress(0u, kStages, "detecting stars in the green channel");

    StarDetectionRequest detection = detectionRequest;
    detection.maximumStars = request.maximumStars;
    const Result<StarDetectionPlan> detectionPlan =
        planStarDetection(detection, frame.descriptor(), frame.layout(), kGreenChannel);
    if (!detectionPlan.hasValue())
    {
        return detectionPlan.status();
    }
    StarDetectionResult detected;
    {
        const ProgressRange progress(execution, 0u, 1u, kStages);
        if (const Status ran = detectStars(execution, frame, *detectionPlan, detected); ran.failed())
        {
            return ran;
        }
    }
    result.detectedStars = static_cast<std::uint32_t>(detected.stars.size());
    if (detected.stars.empty())
    {
        return failure(ErrorCode::EmptySelection, "the green channel yielded no star to calibrate on",
                       kCalibrateOperation);
    }
    execution.reportProgress(1u, kStages, "selecting preflight sources");

    Result<std::vector<PreflightSource>> sources = makePreflightSources(detected.stars, request.sourceSelection);
    if (!sources.hasValue())
    {
        return sources.status();
    }
    result.preflightSources = static_cast<std::uint32_t>(sources->size());
    if (sources->empty())
    {
        return failure(ErrorCode::EmptySelection, "the selection kept no detected star as a preflight source",
                       kCalibrateOperation);
    }
    if (const Status stopped = execution.checkCancellation(); stopped.failed())
    {
        return stopped;
    }
    execution.reportProgress(2u, kStages, "re-matching the sources against the catalog");

    Result<WcsPreflightResult> preflight = runWcsPreflight(execution, solution, *sources, catalog, request.preflight);
    if (!preflight.hasValue())
    {
        return preflight.status();
    }
    result.preflight = std::move(*preflight);
    result.qualityVerdict = checkWcsQuality(result.preflight.report, request.qualityThresholds);
    if (result.qualityVerdict != WcsQualityVerdict::Acceptable)
    {
        if (request.requireAcceptableWcsQuality)
        {
            return failure(ErrorCode::InvalidState,
                           std::string("the world-coordinate quality gate refused: ") +
                               wcsQualityVerdictName(result.qualityVerdict),
                           kCalibrateOperation);
        }
        execution.reportDiagnostic(DiagnosticSeverity::Warning, ErrorDomain::Algorithms, kCalibrateOperation,
                                   "the world-coordinate quality gate would have refused the preflight report and "
                                   "was not required; the calibration continues on it");
    }
    if (result.preflight.matches.empty())
    {
        return failure(ErrorCode::EmptySelection, "the preflight matched no source to a catalog record",
                       kCalibrateOperation);
    }
    execution.reportProgress(3u, kStages, "predicting the matched stars' catalog colours");

    Result<MatchedColorPrediction> prediction =
        predictMatchedCatalogColors(execution, plan.response, catalog, result.preflight.matches);
    if (!prediction.hasValue())
    {
        return prediction.status();
    }
    result.prediction = prediction->batch;
    execution.reportProgress(4u, kStages, "measuring the matched stars");

    Result<SampleConstructionResult> samples = [&]() {
        const ProgressRange progress(execution, 4u, 5u, kStages);
        return buildSpectrophotometricSamples(execution, frame, result.preflight, catalog, prediction->colors,
                                              request.samples);
    }();
    if (!samples.hasValue())
    {
        return samples.status();
    }
    result.samples = std::move(*samples);
    execution.reportProgress(5u, kStages, "fitting the channel gains");

    Result<SpectrophotometricFitResult> fit =
        fitSpectrophotometricGains(execution, result.samples.samples, request.fit);
    if (!fit.hasValue())
    {
        return fit.status();
    }
    result.fit = std::move(*fit);
    if (!result.fit.accepted())
    {
        return refusedFit(result.fit.verdict, kCalibrateOperation);
    }
    execution.reportProgress(6u, kStages, "measuring the sky and writing the calibrated frame");

    Result<SpectrophotometricApplyResult> applied = applySpectrophotometricGains(
        execution, frame, destination, destinationBytes, result.fit.gains, request.background, request.clipping);
    if (!applied.hasValue())
    {
        return applied.status();
    }
    execution.reportProgress(7u, kStages, "assembling the calibration report");
    result.applied = std::move(*applied);
    execution.reportProgress(kStages, kStages, "spectrophotometric colour calibration complete");
    return result;
}

} // namespace an
