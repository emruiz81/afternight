// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the photometric colour model, the robust gain fit and the orchestration that
// detects, matches, fits and applies.

#include <AfterNightEngine/Algorithms/Photometry/PhotometricColorCalibration.h>

#include "runtime/ProgressRange.h"

#include <AfterNightEngine/Algorithms/Statistics/RobustStatistics.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "algorithms/photometry/PhotometryInternal.h"
#include "kernels/reference/PlaneSourceAccess.h"

#include <AfterNightEngine/Image/OperationTransition.h>

#include <algorithm>
#include <charconv>
#include <new>
#include <string>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace an
{
namespace
{

using photometry_detail::failure;
using photometry_detail::limitFailure;

constexpr const char* kFitOperation = "an::fitPhotometricColorGains";
constexpr const char* kCalibrateOperation = "an::calibratePhotometricColor";
constexpr std::uint32_t kCancellationStride = 4096u;
constexpr std::uint32_t kColorChannelCount = 3u;
constexpr std::uint64_t kStageCount = 6u;

/// One ASCII letter folded to lower case; anything else unchanged.
///
/// @param value the byte.
/// @return the folded byte.
[[nodiscard]] constexpr char foldAscii(const char value) noexcept
{
    return (value >= 'A' && value <= 'Z') ? static_cast<char>(value - 'A' + 'a') : value;
}

/// Whether a byte is ASCII white space.
///
/// @param value the byte.
/// @return true for the six ASCII space characters.
[[nodiscard]] constexpr bool isAsciiSpace(const char value) noexcept
{
    return value == ' ' || value == '\t' || value == '\r' || value == '\n' || value == '\v' || value == '\f';
}

/// Whether two texts are equal under ASCII case folding.
///
/// @param left one text.
/// @param right the other.
/// @return true when they fold to the same bytes.
[[nodiscard]] bool equalsFolded(const std::string_view left, const std::string_view right) noexcept
{
    if (left.size() != right.size())
    {
        return false;
    }
    for (std::size_t index = 0u; index < left.size(); ++index)
    {
        if (foldAscii(left[index]) != foldAscii(right[index]))
        {
            return false;
        }
    }
    return true;
}

/// Whether a text begins with another under ASCII case folding.
///
/// @param text the text.
/// @param prefix the prefix.
/// @return true when the text is at least as long and its start folds equal.
[[nodiscard]] bool startsWithFolded(const std::string_view text, const std::string_view prefix) noexcept
{
    return text.size() >= prefix.size() && equalsFolded(text.substr(0u, prefix.size()), prefix);
}

/// Whether a text contains another under ASCII case folding.
///
/// @param haystack the text searched.
/// @param needle the text looked for.
/// @return true when some window of the haystack folds equal to the needle.
[[nodiscard]] bool containsFolded(const std::string_view haystack, const std::string_view needle) noexcept
{
    if (needle.empty() || needle.size() > haystack.size())
    {
        return false;
    }
    for (std::size_t start = 0u; start + needle.size() <= haystack.size(); ++start)
    {
        if (equalsFolded(haystack.substr(start, needle.size()), needle))
        {
            return true;
        }
    }
    return false;
}

/// Whether every field of a gain-fit request is inside its stated range.
///
/// @param request the request.
/// @return success, or the first field that refused.
[[nodiscard]] Status validateFitRequest(const PhotometricColorFitRequest& request)
{
    if (request.minimumSamples == 0u)
    {
        return failure(ErrorCode::InvalidArgument, "the sample floor is at least one", kFitOperation);
    }
    if (!std::isfinite(request.clipSigmas) || request.clipSigmas <= 0.0)
    {
        return failure(ErrorCode::InvalidArgument, "the clipping width is finite and positive", kFitOperation);
    }
    if (!std::isfinite(request.minimumRobustScale) || request.minimumRobustScale < 0.0)
    {
        return failure(ErrorCode::InvalidArgument, "the robust-scale floor is finite and not negative", kFitOperation);
    }
    return Status::success();
}

/// Whether a sample's three fluxes can form ratios.
///
/// @param sample the sample.
/// @return true when every flux is finite and above the floor.
[[nodiscard]] bool usableSample(const PhotometricColorSample& sample) noexcept
{
    return std::isfinite(sample.redFlux) && std::isfinite(sample.greenFlux) && std::isfinite(sample.blueFlux) &&
           sample.redFlux > kMinimumPhotometricSampleFlux && sample.greenFlux > kMinimumPhotometricSampleFlux &&
           sample.blueFlux > kMinimumPhotometricSampleFlux;
}

/// The robust gain of a candidate set: the median of the candidates inside the clipping window.
///
/// @param candidates the candidates, which are only read.
/// @param scratch storage at least as long as the candidates, reordered by this call.
/// @param request the clipping rule.
/// @return the gain.
[[nodiscard]] double robustGain(const std::span<const double> candidates, const std::span<double> scratch,
                                const PhotometricColorFitRequest& request) noexcept
{
    std::copy(candidates.begin(), candidates.end(), scratch.begin());
    const double median = photometry_detail::selectMedian(scratch.first(candidates.size()));
    const double deviation = photometry_detail::medianAbsoluteDeviation(candidates, median, scratch);
    const double sigma = (std::max)(kMedianAbsoluteDeviationToSigma * deviation, request.minimumRobustScale);
    const double threshold = request.clipSigmas * sigma;
    std::size_t kept = 0u;
    for (const double candidate : candidates)
    {
        if (std::abs(candidate - median) <= threshold)
        {
            scratch[kept++] = candidate;
        }
    }
    if (kept == 0u)
    {
        return median;
    }
    return photometry_detail::selectMedian(scratch.first(kept));
}

/// The nearest unclaimed star within a radius of a position, claimed on the way out.
///
/// The nearest one wins, and once claimed a star pairs with nothing else.
/// Ties in distance fall to the lower index, so the answer does not depend on how the list was
/// produced.
///
/// @param stars the channel's stars.
/// @param claimed one flag per star, set for the winner.
/// @param x the anchor's horizontal position.
/// @param y the anchor's vertical position.
/// @param radius how far a star may sit and still be the anchor's.
/// @return the winner's index, or absent when no unclaimed star is within the radius.
[[nodiscard]] std::optional<std::size_t> claimNearestStar(const std::span<const DetectedStar> stars,
                                                          std::vector<bool>& claimed, const double x, const double y,
                                                          const double radius) noexcept
{
    std::optional<std::size_t> best{};
    double bestDistance = radius;
    for (std::size_t index = 0u; index < stars.size(); ++index)
    {
        if (claimed[index])
        {
            continue;
        }
        const double distance = std::hypot(stars[index].positionX - x, stars[index].positionY - y);
        if (distance < bestDistance || (distance == bestDistance && !best.has_value()))
        {
            bestDistance = distance;
            best = index;
        }
    }
    if (best.has_value())
    {
        claimed[*best] = true;
    }
    return best;
}

/// Whether every field of a calibration request is inside its stated range.
///
/// @param request the request.
/// @return success, or the first field that refused.
[[nodiscard]] Status validateCalibrationRequest(const PhotometricColorCalibrationRequest& request)
{
    if (request.maximumStarsPerChannel == 0u || request.maximumStarsPerChannel > kMaxStarsPerFrame)
    {
        return failure(ErrorCode::InvalidArgument, "the per-channel star limit lies between one and the frame ceiling",
                       kCalibrateOperation);
    }
    if (!std::isfinite(request.channelMatchRadiusPixels) || request.channelMatchRadiusPixels <= 0.0)
    {
        return failure(ErrorCode::InvalidArgument, "the channel match radius is finite and positive",
                       kCalibrateOperation);
    }
    if (!isStarFluxEstimator(request.fluxEstimator))
    {
        return failure(ErrorCode::InvalidArgument, "the flux estimator is not one this build implements",
                       kCalibrateOperation);
    }
    if (!isColorCalibrationClipping(request.clipping))
    {
        return failure(ErrorCode::InvalidArgument, "the clipping policy is not one this build implements",
                       kCalibrateOperation);
    }
    if (!isColorCalibrationBackgroundMode(request.background.mode))
    {
        return failure(ErrorCode::InvalidArgument, "the background mode is not one this build implements",
                       kCalibrateOperation);
    }
    if (isNarrowbandFilterName(request.filterName))
    {
        return failure(ErrorCode::Unsupported, "a narrowband frame has no broadband colour to calibrate",
                       kCalibrateOperation);
    }
    return Status::success();
}

} // namespace

bool isNarrowbandFilterName(std::string_view filterName) noexcept
{
    while (!filterName.empty() && isAsciiSpace(filterName.front()))
    {
        filterName.remove_prefix(1u);
    }
    while (!filterName.empty() && isAsciiSpace(filterName.back()))
    {
        filterName.remove_suffix(1u);
    }
    if (filterName.empty())
    {
        return false;
    }
    return equalsFolded(filterName, "ha") || startsWithFolded(filterName, "ha ") ||
           containsFolded(filterName, "halpha") || containsFolded(filterName, "h-alpha") ||
           containsFolded(filterName, "oiii") || equalsFolded(filterName, "sii") ||
           startsWithFolded(filterName, "sii ") || containsFolded(filterName, "s-ii") ||
           containsFolded(filterName, "narrow") || containsFolded(filterName, "dualband") ||
           containsFolded(filterName, "triad");
}

PhotometricColorRatios predictPhotometricColorRatios(const std::optional<double> colorIndexBpMinusRp) noexcept
{
    const double stated = colorIndexBpMinusRp.has_value() && std::isfinite(*colorIndexBpMinusRp)
                              ? *colorIndexBpMinusRp
                              : kPhotometricDefaultColorIndex;
    const double color = std::clamp(stated, kPhotometricColorIndexFloor, kPhotometricColorIndexCeiling);
    return PhotometricColorRatios{.redToGreen = std::exp(kPhotometricRedColorSlope * color),
                                  .greenToGreen = 1.0,
                                  .blueToGreen = std::exp(kPhotometricBlueColorSlope * color)};
}

Result<PhotometricColorFitResult> fitPhotometricColorGains(const ExecutionContext& execution,
                                                           const std::span<const PhotometricColorSample> samples,
                                                           const PhotometricColorFitRequest& request)
{
    if (!execution.valid())
    {
        return failure(ErrorCode::NotInitialized, "a colour-gain fit needs an execution context", kFitOperation);
    }
    if (const Status stopped = execution.checkCancellation(); stopped.failed())
    {
        return stopped;
    }
    if (const Status valid = validateFitRequest(request); valid.failed())
    {
        return valid;
    }
    if (samples.size() > kMaxPhotometricColorSamples)
    {
        return limitFailure("the sample count exceeds the stated ceiling", kFitOperation, samples.size(),
                            kMaxPhotometricColorSamples);
    }

    PhotometricColorFitResult result{};
    result.suppliedCount = static_cast<std::uint32_t>(samples.size());
    for (std::size_t index = 0u; index < samples.size(); ++index)
    {
        if ((index % kCancellationStride) == 0u)
        {
            if (const Status stopped = execution.checkCancellation(); stopped.failed())
            {
                return stopped;
            }
        }
        if (usableSample(samples[index]))
        {
            result.usedSampleIndices.push_back(static_cast<std::uint32_t>(index));
            if (samples[index].colorIndexBpMinusRp.has_value())
            {
                ++result.coloredCount;
            }
        }
    }
    result.usableCount = static_cast<std::uint32_t>(result.usedSampleIndices.size());
    result.usedCatalogColorModel = result.usableCount > 0u && result.coloredCount * 2u > result.usableCount;
    if (result.usableCount < request.minimumSamples)
    {
        return Status::failure(
            ErrorCode::EmptySelection,
            StatusDetail{.message = "fewer usable colour samples than the fit needs",
                         .operation = kFitOperation,
                         .domain = ErrorDomain::Algorithms,
                         .context = {StatusContextField::ofCount(ContextKey::Available, result.usableCount),
                                     StatusContextField::ofCount(ContextKey::Requested, request.minimumSamples)}});
    }

    const std::uint64_t workingBytes = static_cast<std::uint64_t>(result.usableCount) * 3u * sizeof(double);
    Result<plane_detail::ScopedContextMemory> memory =
        plane_detail::ScopedContextMemory::take(execution, workingBytes, alignof(double), kFitOperation);
    if (!memory.hasValue())
    {
        return memory.status();
    }
    double* const red = static_cast<double*>(memory->data());
    double* const blue = red + result.usableCount;
    double* const scratch = blue + result.usableCount;
    for (std::size_t slot = 0u; slot < result.usableCount; ++slot)
    {
        const PhotometricColorSample& sample = samples[result.usedSampleIndices[slot]];
        const PhotometricColorRatios expected = predictPhotometricColorRatios(sample.colorIndexBpMinusRp);
        red[slot] = expected.redToGreen / (sample.redFlux / sample.greenFlux);
        blue[slot] = expected.blueToGreen / (sample.blueFlux / sample.greenFlux);
    }
    const std::span<const double> redCandidates{red, result.usableCount};
    const std::span<const double> blueCandidates{blue, result.usableCount};
    const std::span<double> scratchSpan{scratch, result.usableCount};
    result.gains = {robustGain(redCandidates, scratchSpan, request), 1.0,
                    robustGain(blueCandidates, scratchSpan, request)};

    double squares = 0.0;
    std::uint64_t residuals = 0u;
    for (std::size_t slot = 0u; slot < result.usableCount; ++slot)
    {
        const PhotometricColorSample& sample = samples[result.usedSampleIndices[slot]];
        const PhotometricColorRatios expected = predictPhotometricColorRatios(sample.colorIndexBpMinusRp);
        const double correctedRed = (sample.redFlux * result.gains[0]) / (sample.greenFlux * result.gains[1]);
        const double correctedBlue = (sample.blueFlux * result.gains[2]) / (sample.greenFlux * result.gains[1]);
        if (std::isfinite(correctedRed) && correctedRed > 0.0)
        {
            const double residual = std::log(correctedRed / expected.redToGreen);
            squares += residual * residual;
            ++residuals;
        }
        if (std::isfinite(correctedBlue) && correctedBlue > 0.0)
        {
            const double residual = std::log(correctedBlue / expected.blueToGreen);
            squares += residual * residual;
            ++residuals;
        }
    }
    result.rmsLogResidual = residuals == 0u ? 0.0 : std::sqrt(squares / static_cast<double>(residuals));
    return result;
}

std::vector<std::optional<double>> gaiaColorIndices(const std::span<const GaiaStarRecord> records)
{
    std::vector<std::optional<double>> colors;
    colors.reserve(records.size());
    for (const GaiaStarRecord& record : records)
    {
        colors.push_back(gaiaColorIndexBpMinusRp(record));
    }
    return colors;
}

Result<PhotometricColorCalibrationResult>
calibratePhotometricColor(const ExecutionContext& execution, const StarFrameView& frame, void* const destination,
                          const std::uint64_t destinationBytes, const WcsSolution& solution,
                          const PhotometricReferenceCatalog& reference, const StarDetectionRequest& detectionRequest,
                          const PhotometricColorCalibrationRequest& request)
{
    if (!execution.valid())
    {
        return failure(ErrorCode::NotInitialized, "a colour calibration needs an execution context",
                       kCalibrateOperation);
    }
    if (reference.catalog == nullptr || !reference.catalog->valid())
    {
        return failure(ErrorCode::NotInitialized, "a colour calibration needs a built reference catalog",
                       kCalibrateOperation);
    }
    if (const Status stopped = execution.checkCancellation(); stopped.failed())
    {
        return stopped;
    }
    if (!frame.valid())
    {
        return failure(ErrorCode::InvalidArgument, "a colour calibration needs a checked frame", kCalibrateOperation);
    }
    if (!photometry_detail::isRgbFrame(frame) || frame.descriptor().colorSpace() != ColorSpace::LinearRgb ||
        frame.descriptor().numericDomain() == NumericDomain::EncodedNonlinear)
    {
        return failure(ErrorCode::Unsupported,
                       "a photometric colour calibration needs a three-channel linear RGB frame", kCalibrateOperation);
    }
    const SampleType sampleType = frame.descriptor().sampleType();
    if (sampleType != SampleType::Float32 && sampleType != SampleType::Float64)
    {
        return failure(ErrorCode::Unsupported,
                       "a colour calibration writes a floating frame; an integer frame converts first",
                       kCalibrateOperation);
    }
    if (!reference.colorIndexBpMinusRp.empty() &&
        reference.colorIndexBpMinusRp.size() != reference.catalog->entryCount())
    {
        return failure(ErrorCode::InvalidArgument, "the colour indices are index-aligned with the catalog's entries",
                       kCalibrateOperation);
    }
    if (destination == nullptr || destinationBytes < frame.layout().totalBytes())
    {
        return failure(ErrorCode::InvalidArgument, "the destination holds every byte the layout names",
                       kCalibrateOperation);
    }
    if (const Status valid = validateCalibrationRequest(request); valid.failed())
    {
        return valid;
    }

    PhotometricColorCalibrationResult result{};
    execution.reportProgress(0u, kStageCount, "detecting stars in each channel");

    std::array<StarDetectionResult, kColorChannelCount> detections{};
    StarDetectionRequest detection = detectionRequest;
    detection.maximumStars = request.maximumStarsPerChannel;
    for (std::uint32_t channel = 0u; channel < kColorChannelCount; ++channel)
    {
        const Result<StarDetectionPlan> plan =
            planStarDetection(detection, frame.descriptor(), frame.layout(), channel);
        if (!plan.hasValue())
        {
            return plan.status();
        }
        const ProgressRange detectionStage(execution, 0u, 1u, kStageCount);
        const ProgressRange channelStage(execution, channel, channel + 1u, kColorChannelCount);
        if (const Status detected = detectStars(execution, frame, *plan, detections[channel]); detected.failed())
        {
            return detected;
        }
        result.detectedStars[channel] = static_cast<std::uint32_t>(detections[channel].stars.size());
        if (detections[channel].stars.empty())
        {
            return Status::failure(
                ErrorCode::EmptySelection,
                StatusDetail{.message = "a channel yielded no star to calibrate on",
                             .operation = kCalibrateOperation,
                             .domain = ErrorDomain::Algorithms,
                             .context = {StatusContextField::ofCount(ContextKey::Completed, channel)}});
        }
    }
    execution.reportProgress(1u, kStageCount, "matching green stars to the catalog");

    const std::span<const DetectedStar> green = detections[1].stars;
    Result<PhotometricCrossMatchResult> crossMatch = [&]() {
        const ProgressRange progress(execution, 1u, 2u, kStageCount);
        return crossMatchDetectedStars(
            execution, solution, green, *reference.catalog,
            PhotometricCrossMatchRequest{.match = request.match, .fluxEstimator = request.fluxEstimator});
    }();
    if (!crossMatch.hasValue())
    {
        return crossMatch.status();
    }
    result.crossMatchedStars = static_cast<std::uint32_t>(crossMatch->matches.matches.size());
    if (crossMatch->photometry.empty())
    {
        return failure(ErrorCode::EmptySelection, "no green star matched a catalog entry with a magnitude",
                       kCalibrateOperation);
    }
    execution.reportProgress(2u, kStageCount, "pairing red and blue detections");

    std::vector<bool> claimedRed(detections[0].stars.size(), false);
    std::vector<bool> claimedBlue(detections[2].stars.size(), false);
    result.samples.reserve(crossMatch->photometry.size());
    for (std::size_t index = 0u; index < crossMatch->photometry.size(); ++index)
    {
        if ((index % kCancellationStride) == 0u)
        {
            if (const Status stopped = execution.checkCancellation(); stopped.failed())
            {
                return stopped;
            }
        }
        const CatalogMatch& match = crossMatch->matches.matches[crossMatch->photometryMatchIndices[index]];
        const DetectedStar& greenStar = green[match.sourceIndex];
        const std::optional<std::size_t> redIndex =
            claimNearestStar(detections[0].stars, claimedRed, greenStar.positionX, greenStar.positionY,
                             request.channelMatchRadiusPixels);
        const std::optional<std::size_t> blueIndex =
            claimNearestStar(detections[2].stars, claimedBlue, greenStar.positionX, greenStar.positionY,
                             request.channelMatchRadiusPixels);
        if (!redIndex.has_value() || !blueIndex.has_value())
        {
            ++result.unpairedStars;
            continue;
        }
        const std::optional<double> redFlux =
            starInstrumentalFlux(detections[0].stars[*redIndex], request.fluxEstimator);
        const std::optional<double> blueFlux =
            starInstrumentalFlux(detections[2].stars[*blueIndex], request.fluxEstimator);
        if (!redFlux.has_value() || !blueFlux.has_value())
        {
            ++result.unpairedStars;
            continue;
        }
        result.samples.push_back(PhotometricColorSample{
            .position = {.x = greenStar.positionX, .y = greenStar.positionY},
            .catalogMagnitude = crossMatch->photometry[index].catalogMagnitude,
            .colorIndexBpMinusRp =
                reference.colorIndexBpMinusRp.empty() ? std::nullopt : reference.colorIndexBpMinusRp[match.entryIndex],
            .redFlux = *redFlux,
            .greenFlux = crossMatch->photometry[index].instrumentalFlux,
            .blueFlux = *blueFlux,
            .separationArcseconds = match.separationArcseconds,
            .catalogEntryIndex = match.entryIndex});
    }
    execution.reportProgress(3u, kStageCount, "fitting channel gains");

    Result<PhotometricColorFitResult> fit = fitPhotometricColorGains(execution, result.samples, request.fit);
    if (!fit.hasValue())
    {
        return fit.status();
    }
    result.fit = std::move(*fit);
    execution.reportProgress(4u, kStageCount, "measuring the sky");

    if (request.background.neutralize)
    {
        Result<ColorBackgroundEstimate> background =
            estimateColorBackground(execution, frame, request.background.region);
        if (!background.hasValue())
        {
            return background.status();
        }
        result.offsets = colorCalibrationOffsets(result.fit.gains, background->levels, request.background.mode,
                                                 meanBackgroundPedestal(background->levels));
        result.background = *background;
    }
    execution.reportProgress(5u, kStageCount, "writing the calibrated frame");

    Result<ColorCalibrationApplyResult> applied =
        applyColorCalibration(execution, frame, destination, destinationBytes,
                              ColorCalibrationApplyRequest{
                                  .gains = result.fit.gains, .offsets = result.offsets, .clipping = request.clipping});
    if (!applied.hasValue())
    {
        return applied.status();
    }
    result.apply = *applied;
    execution.reportProgress(kStageCount, kStageCount, "colour calibration complete");
    return result;
}

Result<MetadataTransition> photometricCalibrationTransition(const ImageSemantics& input,
                                                            const PhotometricColorCalibrationResult& result)
try
{
    Result<ImageSemantics> semantics = colorCalibratedSemantics(input);
    if (!semantics.hasValue())
        return semantics.status();
    std::string detail = "recipe=";
    detail += kPhotometricColorCalibrationAlgorithmVersion;
    const auto append = [&](const char* name, const auto& values) {
        detail += name;
        for (std::size_t index = 0u; index < values.size(); ++index)
        {
            if (index != 0u)
                detail += ',';
            char buffer[64];
            const auto written = std::to_chars(buffer, buffer + sizeof(buffer), values[index]);
            detail.append(buffer, written.ptr);
        }
    };
    append("; gains=", result.fit.gains);
    append("; offsets=", result.offsets);
    return operationTransition(kColorCalibrationApplyEffect,
                               OperationOutcome{.operation = "an::calibratePhotometricColor",
                                                .detail = std::move(detail),
                                                .semantics = std::move(*semantics)});
}
catch (const std::bad_alloc&)
{
    return Status::failure(ErrorCode::AllocationFailed);
}

} // namespace an
