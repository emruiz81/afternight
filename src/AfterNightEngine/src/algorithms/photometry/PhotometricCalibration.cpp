// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the photometric zero-point fit and the cross-match that feeds it.

#include <AfterNightEngine/Algorithms/Photometry/PhotometricCalibration.h>

#include <AfterNightEngine/Algorithms/Statistics/RobustStatistics.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "algorithms/photometry/PhotometryInternal.h"
#include "kernels/reference/PlaneSourceAccess.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>
#include <utility>
#include <vector>

namespace an
{
namespace
{

using photometry_detail::failure;
using photometry_detail::limitFailure;

constexpr const char* kFitOperation = "an::fitPhotometricZeroPoint";
constexpr const char* kMatchOperation = "an::crossMatchDetectedStars";
constexpr std::uint32_t kMaxClipIterations = 16u;
constexpr std::uint32_t kCancellationStride = 4096u;

/// Whether every field of a fit request is inside its stated range.
///
/// @param request the request to check.
/// @return success, or the first field that refused.
[[nodiscard]] Status validateFitRequest(const ZeroPointFitRequest& request)
{
    if (request.maximumClipIterations == 0u || request.maximumClipIterations > kMaxClipIterations)
    {
        return failure(ErrorCode::InvalidArgument, "the clipping round count lies between one and sixteen",
                       kFitOperation);
    }
    if (!std::isfinite(request.clipSigmas) || request.clipSigmas <= 0.0)
    {
        return failure(ErrorCode::InvalidArgument, "the clipping width is finite and positive", kFitOperation);
    }
    if (!std::isfinite(request.minimumRobustSigma) || request.minimumRobustSigma < 0.0)
    {
        return failure(ErrorCode::InvalidArgument, "the robust-sigma floor is finite and not negative", kFitOperation);
    }
    if (!std::isfinite(request.minimumClipThreshold) || request.minimumClipThreshold < 0.0)
    {
        return failure(ErrorCode::InvalidArgument, "the clipping floor is finite and not negative", kFitOperation);
    }
    return Status::success();
}

/// Whether a match has a zero point at all.
///
/// @param match the match to inspect.
/// @return true for a finite positive flux.
[[nodiscard]] bool usableFlux(const PhotometricMatch& match) noexcept
{
    return std::isfinite(match.instrumentalFlux) && match.instrumentalFlux > 0.0;
}

} // namespace

const char* starFluxEstimatorName(const StarFluxEstimator estimator) noexcept
{
    switch (estimator)
    {
    case StarFluxEstimator::PsfVolume:
        return "PsfVolume";
    case StarFluxEstimator::BoxSum:
        return "BoxSum";
    }
    return "UnknownStarFluxEstimator";
}

std::optional<double> starInstrumentalFlux(const DetectedStar& star, const StarFluxEstimator estimator) noexcept
{
    switch (estimator)
    {
    case StarFluxEstimator::PsfVolume: {
        if (!std::isfinite(star.psf.amplitude) || !std::isfinite(star.psf.scaleX) || !std::isfinite(star.psf.scaleY))
        {
            return std::nullopt;
        }
        const double amplitude = (std::max)(star.psf.amplitude, 0.0);
        const double sigmaX = (std::max)(std::abs(star.psf.scaleX), kMinimumPsfFluxSigmaPixels);
        const double sigmaY = (std::max)(std::abs(star.psf.scaleY), kMinimumPsfFluxSigmaPixels);
        return amplitude * (2.0 * std::numbers::pi * sigmaX * sigmaY);
    }
    case StarFluxEstimator::BoxSum: {
        if (!star.instrumentalMagnitude.has_value() || !std::isfinite(*star.instrumentalMagnitude))
        {
            return std::nullopt;
        }
        return std::pow(10.0, -0.4 * *star.instrumentalMagnitude);
    }
    }
    return std::nullopt;
}

Result<ZeroPointFitResult> fitPhotometricZeroPoint(const ExecutionContext& execution,
                                                   const std::span<const PhotometricMatch> matches,
                                                   const ZeroPointFitRequest& request)
{
    if (!execution.valid())
    {
        return failure(ErrorCode::NotInitialized, "a zero-point fit needs an execution context", kFitOperation);
    }
    if (const Status stopped = execution.checkCancellation(); stopped.failed())
    {
        return stopped;
    }
    if (const Status valid = validateFitRequest(request); valid.failed())
    {
        return valid;
    }
    if (matches.size() > kMaxZeroPointMatches)
    {
        return limitFailure("the match count exceeds the stated ceiling", kFitOperation, matches.size(),
                            kMaxZeroPointMatches);
    }

    ZeroPointFitResult result{};
    result.suppliedCount = static_cast<std::uint32_t>(matches.size());
    for (std::size_t index = 0u; index < matches.size(); ++index)
    {
        if ((index % kCancellationStride) == 0u)
        {
            if (const Status stopped = execution.checkCancellation(); stopped.failed())
            {
                return stopped;
            }
        }
        if (!std::isfinite(matches[index].catalogMagnitude))
        {
            return Status::failure(
                ErrorCode::InvalidData,
                StatusDetail{.message = "a matched catalog magnitude is not a finite number",
                             .operation = kFitOperation,
                             .domain = ErrorDomain::Algorithms,
                             .context = {StatusContextField::ofCount(ContextKey::Completed, index)}});
        }
        if (usableFlux(matches[index]))
        {
            ++result.usableCount;
        }
    }
    if (result.usableCount == 0u)
    {
        return failure(ErrorCode::EmptySelection, "no matched star carries a positive finite flux", kFitOperation);
    }

    const std::uint64_t workingBytes = static_cast<std::uint64_t>(result.usableCount) * 2u * sizeof(double);
    Result<plane_detail::ScopedContextMemory> memory =
        plane_detail::ScopedContextMemory::take(execution, workingBytes, alignof(double), kFitOperation);
    if (!memory.hasValue())
    {
        return memory.status();
    }
    result.workingBytes = memory->bytes();
    double* const values = static_cast<double*>(memory->data());
    double* const scratch = values + result.usableCount;

    std::uint32_t written = 0u;
    for (const PhotometricMatch& match : matches)
    {
        if (usableFlux(match))
        {
            values[written++] = match.catalogMagnitude + 2.5 * std::log10(match.instrumentalFlux);
        }
    }

    std::uint32_t count = result.usableCount;
    for (std::uint32_t iteration = 0u; iteration < request.maximumClipIterations; ++iteration)
    {
        if (const Status stopped = execution.checkCancellation(); stopped.failed())
        {
            return stopped;
        }
        std::copy(values, values + count, scratch);
        const double median = photometry_detail::selectMedian(std::span<double>{scratch, count});
        const double deviation = photometry_detail::medianAbsoluteDeviation(std::span<const double>{values, count},
                                                                            median, std::span<double>{scratch, count});
        const double sigma = (std::max)(kMedianAbsoluteDeviationToSigma * deviation, request.minimumRobustSigma);
        const double threshold = (std::max)(request.clipSigmas * sigma, request.minimumClipThreshold);
        result.finalRobustSigma = sigma;
        ++result.clipIterations;
        if (count <= 2u)
        {
            break;
        }
        std::uint32_t kept = 0u;
        for (std::uint32_t index = 0u; index < count; ++index)
        {
            if (std::abs(values[index] - median) <= threshold)
            {
                values[kept++] = values[index];
            }
        }
        if (kept == count || kept == 0u)
        {
            break;
        }
        count = kept;
    }

    double sum = 0.0;
    for (std::uint32_t index = 0u; index < count; ++index)
    {
        sum += values[index];
    }
    const double mean = sum / static_cast<double>(count);
    double squares = 0.0;
    for (std::uint32_t index = 0u; index < count; ++index)
    {
        const double residual = values[index] - mean;
        squares += residual * residual;
    }
    result.retainedCount = count;
    result.zeroPoint = mean;
    result.rmsResidual = std::sqrt(squares / static_cast<double>(count));
    result.zeroPointStandardUncertainty = count > 1u ? result.rmsResidual / std::sqrt(static_cast<double>(count)) : 0.0;
    return result;
}

Result<PhotometricCrossMatchResult> crossMatchDetectedStars(const ExecutionContext& execution,
                                                            const WcsSolution& solution,
                                                            const std::span<const DetectedStar> stars,
                                                            const SkyCatalog& catalog,
                                                            const PhotometricCrossMatchRequest& request)
{
    if (!execution.valid())
    {
        return failure(ErrorCode::NotInitialized, "a cross-match needs an execution context", kMatchOperation);
    }
    if (!catalog.valid())
    {
        return failure(ErrorCode::NotInitialized, "a cross-match needs a built catalog", kMatchOperation);
    }
    if (const Status stopped = execution.checkCancellation(); stopped.failed())
    {
        return stopped;
    }
    if (!isStarFluxEstimator(request.fluxEstimator))
    {
        return failure(ErrorCode::InvalidArgument, "the flux estimator is not one this build implements",
                       kMatchOperation);
    }
    if (stars.empty())
    {
        return failure(ErrorCode::EmptySelection, "a cross-match needs at least one star", kMatchOperation);
    }
    if (stars.size() > kMaxCatalogMatchSources)
    {
        return limitFailure("the star count exceeds the catalog match ceiling", kMatchOperation, stars.size(),
                            kMaxCatalogMatchSources);
    }

    PhotometricCrossMatchResult result{};
    std::vector<CatalogSourceMeasurement> sources(stars.size());
    for (std::size_t index = 0u; index < stars.size(); ++index)
    {
        if ((index % kCancellationStride) == 0u)
        {
            if (const Status stopped = execution.checkCancellation(); stopped.failed())
            {
                return stopped;
            }
        }
        const DetectedStar& star = stars[index];
        CatalogSourceMeasurement& source = sources[index];
        source.instrumentalMagnitude = star.instrumentalMagnitude;
        if (!photometry_detail::finitePosition(star.positionX, star.positionY))
        {
            source.hasCoordinate = false;
            ++result.unplacedStarCount;
            continue;
        }
        const Result<SkyCoordinate> placed = solution.pixelToWorld({.x = star.positionX, .y = star.positionY});
        if (!placed.hasValue())
        {
            source.hasCoordinate = false;
            ++result.unplacedStarCount;
            continue;
        }
        source.coordinate = *placed;
        source.hasCoordinate = true;
    }

    Result<CatalogMatchResult> matched = matchCatalogSources(execution, sources, catalog, request.match);
    if (!matched.hasValue())
    {
        return matched.status();
    }
    result.matches = std::move(*matched);
    result.photometry.reserve(result.matches.matches.size());
    result.photometryMatchIndices.reserve(result.matches.matches.size());
    for (std::size_t index = 0u; index < result.matches.matches.size(); ++index)
    {
        const CatalogMatch& match = result.matches.matches[index];
        const CatalogEntry* const entry = catalog.entry(match.entryIndex);
        if (entry == nullptr || !entry->magnitude.has_value())
        {
            ++result.magnitudelessMatchCount;
            continue;
        }
        const std::optional<double> flux = starInstrumentalFlux(stars[match.sourceIndex], request.fluxEstimator);
        if (!flux.has_value())
        {
            ++result.fluxlessMatchCount;
            continue;
        }
        result.photometry.push_back(PhotometricMatch{.instrumentalFlux = *flux, .catalogMagnitude = *entry->magnitude});
        result.photometryMatchIndices.push_back(static_cast<std::uint32_t>(index));
    }
    return result;
}

} // namespace an
