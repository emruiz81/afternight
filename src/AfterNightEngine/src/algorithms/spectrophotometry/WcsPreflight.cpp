// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the WCS preflight: source selection, catalog re-match and the quality verdict.
//
// The re-match projects every record the cone query returns, moved to the observation epoch, into
// the frame, sorts the records that land inside it by row, and for each source walks only the
// records whose row lies within the match radius. It visits the candidate pairs twice: once to
// count them, so that a field above the candidate ceiling is refused before anything is allocated
// and the table is then taken from the context at its exact size, and once to fill it. The pairs
// are sorted by residual, then source index, then catalog identifier, and taken greedily, one per
// source and one per record, so the matches do not depend on the order the query returned records.

#include <AfterNightEngine/Algorithms/Spectrophotometry/WcsPreflight.h>

#include <AfterNightEngine/Algorithms/Catalog/CatalogEnrichment.h>
#include <AfterNightEngine/Algorithms/Catalog/GaiaCatalogRecord.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "algorithms/spectrophotometry/SpectrophotometryInternal.h"
#include "kernels/reference/PlaneSourceAccess.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace an
{
namespace
{

using spectrophotometry_detail::failure;
using spectrophotometry_detail::limitFailure;

constexpr const char* kSourcesOperation = "an::makePreflightSources";
constexpr const char* kPreflightOperation = "an::runWcsPreflight";
/// Sources or records visited between cancellation checks.
constexpr std::uint32_t kCancellationStride = 1024u;
/// How close the automatic magnitude limit may come to the ceiling and count as having reached it.
constexpr double kLimitEpsilon = 1.0e-9;
/// The automatic limit's catalog target when there is no detected source to double.
constexpr std::uint32_t kDefaultAutoTarget = 512u;
/// The least automatic target: twice the source count is held at or above this.
constexpr std::uint32_t kMinimumAutoTarget = 64u;
/// The greatest automatic target: twice the source count is held at or below this.
constexpr std::uint32_t kMaximumAutoTarget = 4096u;

/// One catalog record projected into the frame.
struct ProjectedRecord
{
    /// The record's position in the catalog.
    std::uint32_t recordIndex = 0u;

    /// Where it projected.
    PixelCoordinate pixel{};
};

/// One source-to-record pair inside the match radius.
struct Candidate
{
    /// The source.
    std::uint32_t sourceIndex = 0u;

    /// The projected record.
    std::uint32_t projectedIndex = 0u;

    /// The distance between them, in pixels.
    double residualPixels = 0.0;

    /// The record's identifier, for the tie break.
    std::uint64_t sourceIdentifier = 0u;
};

/// Whether every field of a preflight request is inside its stated range.
///
/// @param request the request.
/// @return success, or the first field that refused.
[[nodiscard]] Status validateRequest(const WcsPreflightRequest& request)
{
    const auto positive = [](const double value) { return std::isfinite(value) && value > 0.0; };
    if (!positive(request.matchRadiusArcseconds))
    {
        return failure(ErrorCode::InvalidArgument, "the match radius is finite and positive", kPreflightOperation);
    }
    if (!std::isfinite(request.magnitudeLimit))
    {
        return failure(ErrorCode::InvalidArgument, "the magnitude limit is finite", kPreflightOperation);
    }
    if (request.autoMagnitude.enabled)
    {
        if (!positive(request.autoMagnitude.minimumLimit) ||
            request.autoMagnitude.minimumLimit > request.magnitudeLimit)
        {
            return failure(ErrorCode::InvalidArgument,
                           "the automatic magnitude floor is finite, positive and at or below the ceiling",
                           kPreflightOperation);
        }
        if (!positive(request.autoMagnitude.step))
        {
            return failure(ErrorCode::InvalidArgument, "the automatic magnitude step is finite and positive",
                           kPreflightOperation);
        }
        if (request.autoMagnitude.targetSourceCount.has_value() && *request.autoMagnitude.targetSourceCount == 0u)
        {
            return failure(ErrorCode::InvalidArgument, "the automatic target count is at least one",
                           kPreflightOperation);
        }
    }
    if (!positive(request.residualThresholdPixels) || !positive(request.residualThresholdArcseconds))
    {
        return failure(ErrorCode::InvalidArgument, "the residual thresholds are finite and positive",
                       kPreflightOperation);
    }
    if (!std::isfinite(request.observationEpochYear))
    {
        return failure(ErrorCode::InvalidArgument, "the observation epoch is finite", kPreflightOperation);
    }
    return Status::success();
}

/// Whether the solution projects a pixel to the sky and back to a finite pixel.
///
/// @param solution the solution.
/// @param pixel the pixel.
/// @return true when both directions succeed.
[[nodiscard]] bool roundTrips(const WcsSolution& solution, const PixelCoordinate& pixel)
{
    const Result<SkyCoordinate> direction = solution.pixelToWorld(pixel);
    if (!direction.hasValue())
    {
        return false;
    }
    const Result<PixelCoordinate> back = solution.worldToPixel(*direction);
    return back.hasValue() && std::isfinite(back->x) && std::isfinite(back->y);
}

/// The exact median of a non-empty range this call may reorder.
///
/// @param values the values.
/// @return the median.
[[nodiscard]] double medianOf(std::vector<double>& values) noexcept
{
    const std::size_t middle = values.size() / 2u;
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(middle), values.end());
    double result = values[middle];
    if ((values.size() % 2u) == 0u)
    {
        const auto lower = std::max_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(middle));
        result = (*lower + result) * 0.5;
    }
    return result;
}

/// The residual figures of the accepted matches, written into the report.
///
/// @param matches the accepted matches.
/// @param scale arcseconds per pixel.
/// @param report receives the figures.
void populateResiduals(const std::vector<PreflightMatch>& matches, const double scale, WcsQualityReport& report)
{
    if (matches.empty())
    {
        return;
    }
    std::vector<double> residuals;
    residuals.reserve(matches.size());
    double squares = 0.0;
    double largest = 0.0;
    for (const PreflightMatch& match : matches)
    {
        residuals.push_back(match.residualPixels);
        squares += match.residualPixels * match.residualPixels;
        largest = (std::max)(largest, match.residualPixels);
    }
    const double median = medianOf(residuals);
    const double rms = std::sqrt(squares / static_cast<double>(matches.size()));
    report.medianResidualPixels = median;
    report.rmsResidualPixels = rms;
    report.maximumResidualPixels = largest;
    report.medianResidualArcseconds = median * scale;
    report.rmsResidualArcseconds = rms * scale;
    report.maximumResidualArcseconds = largest * scale;
}

/// How many of the frame's quadrants hold a match.
///
/// @param matches the accepted matches.
/// @param width the frame width.
/// @param height the frame height.
/// @return the count, at most four.
[[nodiscard]] std::uint32_t quadrantsCovered(const std::vector<PreflightMatch>& matches, const double width,
                                             const double height) noexcept
{
    std::array<bool, 4u> covered{};
    for (const PreflightMatch& match : matches)
    {
        const std::size_t quadrant =
            (match.detectedPosition.x >= 0.5 * width ? 1u : 0u) + (match.detectedPosition.y >= 0.5 * height ? 2u : 0u);
        covered[quadrant] = true;
    }
    return static_cast<std::uint32_t>(std::count(covered.begin(), covered.end(), true));
}

} // namespace

const char* wcsQualityVerdictName(const WcsQualityVerdict verdict) noexcept
{
    switch (verdict)
    {
    case WcsQualityVerdict::Acceptable:
        return "Acceptable";
    case WcsQualityVerdict::NoProvenance:
        return "NoProvenance";
    case WcsQualityVerdict::UnsupportedDistortion:
        return "UnsupportedDistortion";
    case WcsQualityVerdict::IncompleteProjection:
        return "IncompleteProjection";
    case WcsQualityVerdict::StaleWithoutRematch:
        return "StaleWithoutRematch";
    case WcsQualityVerdict::TooFewMatchedStars:
        return "TooFewMatchedStars";
    case WcsQualityVerdict::PoorSkyCoverage:
        return "PoorSkyCoverage";
    case WcsQualityVerdict::IncompleteResiduals:
        return "IncompleteResiduals";
    case WcsQualityVerdict::RmsResidualExceeded:
        return "RmsResidualExceeded";
    case WcsQualityVerdict::MaximumResidualExceeded:
        return "MaximumResidualExceeded";
    }
    return "UnknownWcsQualityVerdict";
}

WcsQualityVerdict checkWcsQuality(const WcsQualityReport& report, const WcsQualityThresholds& thresholds) noexcept
{
    if (report.source == WcsQualitySource::None)
    {
        return WcsQualityVerdict::NoProvenance;
    }
    if (report.distortionModel == WcsDistortionModel::Unsupported)
    {
        return WcsQualityVerdict::UnsupportedDistortion;
    }
    if (!report.centerProjectionValid || !report.cornerProjectionValid || !report.inverseProjectionSupported)
    {
        return WcsQualityVerdict::IncompleteProjection;
    }
    if (report.staleExternalWcs && report.source != WcsQualitySource::PreflightRematch)
    {
        return WcsQualityVerdict::StaleWithoutRematch;
    }
    if (report.matchedStarCount < thresholds.minimumMatchedStars)
    {
        return WcsQualityVerdict::TooFewMatchedStars;
    }
    if (report.skyQuadrantsCovered < thresholds.minimumSkyQuadrants)
    {
        return WcsQualityVerdict::PoorSkyCoverage;
    }
    const auto finite = [](const std::optional<double>& value) { return value.has_value() && std::isfinite(*value); };
    if (!finite(report.rmsResidualPixels) || !finite(report.maximumResidualPixels) ||
        !finite(report.rmsResidualArcseconds) || !finite(report.maximumResidualArcseconds))
    {
        return WcsQualityVerdict::IncompleteResiduals;
    }
    if (*report.rmsResidualPixels > report.residualThresholdPixels ||
        *report.rmsResidualArcseconds > report.residualThresholdArcseconds)
    {
        return WcsQualityVerdict::RmsResidualExceeded;
    }
    if (*report.maximumResidualPixels > report.residualThresholdPixels * thresholds.maximumResidualFactor ||
        *report.maximumResidualArcseconds > report.residualThresholdArcseconds * thresholds.maximumResidualFactor)
    {
        return WcsQualityVerdict::MaximumResidualExceeded;
    }
    return WcsQualityVerdict::Acceptable;
}

Result<std::vector<PreflightSource>> makePreflightSources(const std::span<const DetectedStar> stars,
                                                          const PreflightSourceSelection& selection)
{
    if (!std::isfinite(selection.minimumSignalToNoise) || selection.minimumSignalToNoise < 0.0)
    {
        return failure(ErrorCode::InvalidArgument, "the signal-to-noise floor is finite and not negative",
                       kSourcesOperation);
    }
    if (selection.maximumSources.has_value() && *selection.maximumSources == 0u)
    {
        return failure(ErrorCode::InvalidArgument, "the source ceiling is at least one", kSourcesOperation);
    }
    std::vector<PreflightSource> sources;
    sources.reserve(stars.size());
    for (const DetectedStar& star : stars)
    {
        if (!std::isfinite(star.positionX) || !std::isfinite(star.positionY))
        {
            continue;
        }
        if (selection.rejectSaturated && star.psf.saturated)
        {
            continue;
        }
        const double signalToNoise =
            star.signalToNoise.has_value() && std::isfinite(*star.signalToNoise) && *star.signalToNoise > 0.0
                ? *star.signalToNoise
                : 0.0;
        if (signalToNoise < selection.minimumSignalToNoise)
        {
            continue;
        }
        double fullWidth = std::isfinite(star.metrics.meanFullWidth) && star.metrics.meanFullWidth > 0.0
                               ? star.metrics.meanFullWidth
                               : 0.0;
        if (fullWidth == 0.0 && std::isfinite(star.psf.fullWidthX) && std::isfinite(star.psf.fullWidthY) &&
            star.psf.fullWidthX > 0.0 && star.psf.fullWidthY > 0.0)
        {
            fullWidth = 0.5 * (star.psf.fullWidthX + star.psf.fullWidthY);
        }
        sources.push_back(PreflightSource{.position = {.x = star.positionX, .y = star.positionY},
                                          .fullWidthPixels = fullWidth,
                                          .signalToNoise = signalToNoise});
    }
    if (selection.sortBySignalToNoise)
    {
        std::stable_sort(sources.begin(), sources.end(), [](const PreflightSource& left, const PreflightSource& right) {
            if (left.signalToNoise != right.signalToNoise)
            {
                return left.signalToNoise > right.signalToNoise;
            }
            if (left.position.y != right.position.y)
            {
                return left.position.y < right.position.y;
            }
            return left.position.x < right.position.x;
        });
    }
    if (selection.maximumSources.has_value() && sources.size() > *selection.maximumSources)
    {
        sources.resize(*selection.maximumSources);
    }
    return sources;
}

Result<WcsPreflightResult> runWcsPreflight(const ExecutionContext& execution, const WcsSolution& solution,
                                           const std::span<const PreflightSource> sources,
                                           const SpectrumCatalog& catalog, const WcsPreflightRequest& request)
{
    if (!execution.valid())
    {
        return failure(ErrorCode::NotInitialized, "a preflight needs an execution context", kPreflightOperation);
    }
    if (!catalog.valid())
    {
        return failure(ErrorCode::NotInitialized, "a preflight needs a built spectrum catalog", kPreflightOperation);
    }
    if (const Status stopped = execution.checkCancellation(); stopped.failed())
    {
        return stopped;
    }
    if (const Status valid = validateRequest(request); valid.failed())
    {
        return valid;
    }
    if (solution.imageWidth() == 0u || solution.imageHeight() == 0u)
    {
        return failure(ErrorCode::InvalidArgument, "the solution states the frame's size", kPreflightOperation);
    }
    if (sources.size() > kMaxPreflightSources)
    {
        return limitFailure("the source count exceeds the stated ceiling", kPreflightOperation, sources.size(),
                            kMaxPreflightSources);
    }

    const auto width = static_cast<double>(solution.imageWidth());
    const auto height = static_cast<double>(solution.imageHeight());
    WcsPreflightResult result{};
    WcsQualityReport& report = result.report;
    report.source = WcsQualitySource::PreflightRematch;
    report.distortionModel =
        solution.distortion().hasForwardModel() ? WcsDistortionModel::TangentSip : WcsDistortionModel::Tangent;
    report.centerProjectionValid = roundTrips(solution, {.x = 0.5 * width, .y = 0.5 * height});
    report.inverseProjectionSupported = report.centerProjectionValid;
    const std::array<PixelCoordinate, 4u> corners{
        PixelCoordinate{.x = 0.0, .y = 0.0}, PixelCoordinate{.x = width, .y = 0.0},
        PixelCoordinate{.x = 0.0, .y = height}, PixelCoordinate{.x = width, .y = height}};
    report.cornerProjectionValid = std::all_of(corners.begin(), corners.end(), [&](const PixelCoordinate& corner) {
        const Result<SkyCoordinate> direction = solution.pixelToWorld(corner);
        return direction.hasValue() && isSkyCoordinate(*direction);
    });
    report.staleExternalWcs = request.staleExternalWcs;
    report.residualThresholdPixels = request.residualThresholdPixels;
    report.residualThresholdArcseconds = request.residualThresholdArcseconds;
    report.astrometricCatalogFamilyId = request.astrometricCatalogFamilyId;
    report.astrometricCatalogManifestId = request.astrometricCatalogManifestId;
    report.observationEpoch = request.observationEpoch;
    report.properMotionModelVersion = request.properMotionModelVersion;
    report.wcsSolutionId = request.wcsSolutionId;
    result.selectedMagnitudeLimit = request.magnitudeLimit;

    const Result<SkyCoordinate> center = solution.fieldCenter();
    const Result<double> radius = solution.queryRadiusDegrees(request.matchRadiusArcseconds);
    const double scale = solution.meanPixelScaleArcsecPerPixel();
    if (!center.hasValue() || !radius.hasValue() || !std::isfinite(scale) || scale <= 0.0)
    {
        return result;
    }

    CatalogRegionQuery query;
    query.center = *center;
    query.radiusDegrees = *radius;
    Result<CatalogQueryResult> found = Status::success();
    // The automatic limit starts at its floor and moves by its step, re-querying the catalog each
    // time, until the query returns the target count or the limit reaches the ceiling.
    if (request.autoMagnitude.enabled)
    {
        const std::uint32_t target = request.autoMagnitude.targetSourceCount.value_or(
            std::clamp(static_cast<std::uint32_t>((std::min)(sources.size() * 2u, std::size_t{kMaximumAutoTarget})),
                       sources.empty() ? kDefaultAutoTarget : kMinimumAutoTarget, kMaximumAutoTarget));
        for (double limit = request.autoMagnitude.minimumLimit;; limit += request.autoMagnitude.step)
        {
            const double queryLimit = (std::min)(limit, request.magnitudeLimit);
            query.magnitudeLimit = queryLimit;
            found = querySpectrumCatalogRegion(execution, catalog, query);
            if (!found.hasValue())
            {
                return found.status();
            }
            result.selectedMagnitudeLimit = queryLimit;
            if (found->entryIndices.size() >= target || queryLimit >= request.magnitudeLimit - kLimitEpsilon)
            {
                break;
            }
        }
    }
    else
    {
        query.magnitudeLimit = request.magnitudeLimit;
        found = querySpectrumCatalogRegion(execution, catalog, query);
        if (!found.hasValue())
        {
            return found.status();
        }
    }
    result.catalogCandidates = static_cast<std::uint32_t>(found->entryIndices.size());

    std::vector<ProjectedRecord> projected;
    projected.reserve(found->entryIndices.size());
    for (std::size_t index = 0u; index < found->entryIndices.size(); ++index)
    {
        if ((index % kCancellationStride) == 0u)
        {
            if (const Status stopped = execution.checkCancellation(); stopped.failed())
            {
                return stopped;
            }
        }
        const std::uint32_t recordIndex = found->entryIndices[index];
        const SpectrumRecord& record = *catalog.record(recordIndex);
        const GaiaStarRecord motion{.coordinate = record.coordinate,
                                    .sourceIdentifier = record.sourceIdentifier,
                                    .properMotionRightAscensionMasPerYear = record.properMotionRightAscensionMasPerYear,
                                    .properMotionDeclinationMasPerYear = record.properMotionDeclinationMasPerYear};
        const SkyCoordinate corrected =
            applyGaiaEpochCorrection(motion, record.referenceEpochYear, request.observationEpochYear);
        const Result<PixelCoordinate> pixel = solution.worldToPixel(corrected);
        if (!pixel.hasValue() || !std::isfinite(pixel->x) || !std::isfinite(pixel->y) || pixel->x < 0.0 ||
            pixel->y < 0.0 || pixel->x >= width || pixel->y >= height)
        {
            continue;
        }
        projected.push_back(ProjectedRecord{.recordIndex = recordIndex, .pixel = *pixel});
    }
    result.projectedInsideFrame = static_cast<std::uint32_t>(projected.size());
    if (projected.empty() || sources.empty())
    {
        return result;
    }
    std::sort(projected.begin(), projected.end(), [](const ProjectedRecord& left, const ProjectedRecord& right) {
        return left.pixel.y < right.pixel.y || (left.pixel.y == right.pixel.y && left.recordIndex < right.recordIndex);
    });

    const double radiusPixels = request.matchRadiusArcseconds / scale;
    const auto forEachCandidate = [&](auto&& body) -> Status {
        for (std::uint32_t sourceIndex = 0u; sourceIndex < sources.size(); ++sourceIndex)
        {
            if ((sourceIndex % kCancellationStride) == 0u)
            {
                if (const Status stopped = execution.checkCancellation(); stopped.failed())
                {
                    return stopped;
                }
            }
            const PixelCoordinate& detected = sources[sourceIndex].position;
            if (!std::isfinite(detected.x) || !std::isfinite(detected.y))
            {
                continue;
            }
            const auto first = std::lower_bound(
                projected.begin(), projected.end(), detected.y - radiusPixels,
                [](const ProjectedRecord& record, const double value) { return record.pixel.y < value; });
            for (auto at = first; at != projected.end() && at->pixel.y <= detected.y + radiusPixels; ++at)
            {
                const double residual = std::hypot(detected.x - at->pixel.x, detected.y - at->pixel.y);
                if (std::isfinite(residual) && residual * scale <= request.matchRadiusArcseconds)
                {
                    body(sourceIndex, static_cast<std::uint32_t>(at - projected.begin()), residual);
                }
            }
        }
        return Status::success();
    };

    std::uint64_t candidateCount = 0u;
    if (const Status counted = forEachCandidate([&](std::uint32_t, std::uint32_t, double) { ++candidateCount; });
        counted.failed())
    {
        return counted;
    }
    if (candidateCount > kDefaultMaxCatalogMatchCandidates)
    {
        return limitFailure("the field produces more candidate pairs than the preflight allows", kPreflightOperation,
                            candidateCount, kDefaultMaxCatalogMatchCandidates);
    }
    result.candidatePairs = static_cast<std::uint32_t>(candidateCount);
    if (candidateCount == 0u)
    {
        return result;
    }
    Result<plane_detail::ScopedContextMemory> memory = plane_detail::ScopedContextMemory::take(
        execution, candidateCount * sizeof(Candidate) + sources.size() + projected.size(), alignof(Candidate),
        kPreflightOperation);
    if (!memory.hasValue())
    {
        return memory.status();
    }
    Candidate* const candidates = static_cast<Candidate*>(memory->data());
    auto* const takenSources = reinterpret_cast<std::uint8_t*>(candidates + candidateCount);
    std::uint8_t* const takenRecords = takenSources + sources.size();
    std::fill(takenSources, takenSources + sources.size() + projected.size(), static_cast<std::uint8_t>(0));
    std::size_t written = 0u;
    if (const Status filled = forEachCandidate(
            [&](const std::uint32_t sourceIndex, const std::uint32_t projectedIndex, const double residual) {
                if (written < candidateCount)
                {
                    candidates[written++] = Candidate{
                        .sourceIndex = sourceIndex,
                        .projectedIndex = projectedIndex,
                        .residualPixels = residual,
                        .sourceIdentifier = catalog.record(projected[projectedIndex].recordIndex)->sourceIdentifier};
                }
            });
        filled.failed())
    {
        return filled;
    }
    std::sort(candidates, candidates + written, [](const Candidate& left, const Candidate& right) {
        if (left.residualPixels != right.residualPixels)
        {
            return left.residualPixels < right.residualPixels;
        }
        if (left.sourceIndex != right.sourceIndex)
        {
            return left.sourceIndex < right.sourceIndex;
        }
        return left.sourceIdentifier < right.sourceIdentifier;
    });

    for (std::size_t index = 0u; index < written; ++index)
    {
        const Candidate& candidate = candidates[index];
        if (takenSources[candidate.sourceIndex] != 0u || takenRecords[candidate.projectedIndex] != 0u)
        {
            continue;
        }
        takenSources[candidate.sourceIndex] = 1u;
        takenRecords[candidate.projectedIndex] = 1u;
        const ProjectedRecord& record = projected[candidate.projectedIndex];
        const PreflightSource& source = sources[candidate.sourceIndex];
        result.matches.push_back(PreflightMatch{.sourceIndex = candidate.sourceIndex,
                                                .recordIndex = record.recordIndex,
                                                .sourceIdentifier = candidate.sourceIdentifier,
                                                .detectedPosition = source.position,
                                                .projectedPosition = record.pixel,
                                                .residualPixels = candidate.residualPixels,
                                                .residualArcseconds = candidate.residualPixels * scale,
                                                .fullWidthPixels = source.fullWidthPixels,
                                                .signalToNoise = source.signalToNoise});
    }
    std::sort(
        result.matches.begin(), result.matches.end(),
        [](const PreflightMatch& left, const PreflightMatch& right) { return left.sourceIndex < right.sourceIndex; });
    report.matchedStarCount = static_cast<std::uint32_t>(result.matches.size());
    report.skyQuadrantsCovered = quadrantsCovered(result.matches, width, height);
    populateResiduals(result.matches, scale, report);
    return result;
}

} // namespace an
