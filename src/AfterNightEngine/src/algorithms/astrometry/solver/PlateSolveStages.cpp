// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - planning, input, catalog-search, and matching plate-solver stages.
//
// Planning checks every request field and derives the search geometry without allocating. The
// matcher forms its triangles from each side's brightest points, so the two sides must rank the
// same stars first. Input preparation therefore keeps, where enough stars lie in it, the matching
// disc - the largest circle about the image centre inside the frame, the one region the field's
// unknown rotation cannot change - brightest first, and the catalog search keeps the same region as
// the image's stars were kept over, so the two sides never differ in it. A sparser frame keeps its
// whole extent, thinned by one spatial selection so that a bright corner of the frame or of the
// catalog cone cannot take every slot the matcher has. Matching hands both sides to the
// star-alignment matcher in a shared frame: image positions about the image centre, catalog
// positions in pseudo pixels about the trial centre.

#include <AfterNightEngine/Algorithms/Astrometry/PlateSolver.h>

#include <AfterNightEngine/Algorithms/Astrometry/GnomonicProjection.h>
#include <AfterNightEngine/Algorithms/Catalog/Healpix.h>
#include <AfterNightEngine/Algorithms/Catalog/SkyCatalog.h>
#include <AfterNightEngine/Algorithms/Stars/StarAlignment.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/DiagnosticSink.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "algorithms/astrometry/solver/PlateSolverInternal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <utility>
#include <vector>

namespace an
{
namespace
{

using plate_solver_detail::failure;
using plate_solver_detail::parameterFailure;

/// Columns of the grid the spatial selection balances over.
constexpr std::uint32_t kSpatialColumns = 10u;

/// Rows of the grid the spatial selection balances over.
constexpr std::uint32_t kSpatialRows = 8u;

// The request bounds `planPlateSolve` enforces; `PlateSolver.h` states each beside its field.
constexpr std::uint32_t kMaximumSelectedStars = 4096u;
constexpr std::uint32_t kMaximumCatalogCandidates = 1000000u;
constexpr std::uint32_t kMaximumNearRings = 64u;
constexpr std::uint32_t kMaximumNearSamples = 1024u;
constexpr std::uint32_t kMaximumRefinementIterations = 32u;
constexpr std::uint32_t kMaximumRansacIterations = 100000u;
constexpr std::uint32_t kMaximumTriangles = 100000u;

/// The most detections `preparePlateSolveInput` accepts.
constexpr std::uint64_t kMaximumInputStars = 1000000u;

/// Arcseconds in one degree.
constexpr double kArcsecondsPerDegree = 3600.0;

/// The fewest stars the matching disc must hold for a side to be matched over it: enough for the matcher's brightest
/// triangle seed group, sixty points, to lie inside it.
constexpr std::size_t kMatchingDiscMinimumStars = 60u;

/// Whether a value is finite and above zero.
///
/// @param value the value to test.
/// @return true when it is.
[[nodiscard]] bool finitePositive(double value) noexcept
{
    return std::isfinite(value) && value > 0.0;
}

/// The grid cell one position falls in.
///
/// The rectangle is divided into `kSpatialColumns` by `kSpatialRows` cells; a position outside it
/// is clamped into the nearest edge cell, and a span below one unit is treated as one.
///
/// @param x the horizontal position.
/// @param y the vertical position.
/// @param minX the rectangle's left edge.
/// @param maxX its right edge.
/// @param minY its first edge on the vertical axis.
/// @param maxY its last edge on the vertical axis.
/// @return the row-major cell index.
[[nodiscard]] std::uint32_t spatialCell(double x, double y, double minX, double maxX, double minY, double maxY) noexcept
{
    const double spanX = (std::max)(maxX - minX, 1.0);
    const double spanY = (std::max)(maxY - minY, 1.0);
    const double nx = std::clamp((x - minX) / spanX, 0.0, 0.999999);
    const double ny = std::clamp((y - minY) / spanY, 0.0, 0.999999);
    const auto column = static_cast<std::uint32_t>(nx * static_cast<double>(kSpatialColumns));
    const auto row = static_cast<std::uint32_t>(ny * static_cast<double>(kSpatialRows));
    return row * kSpatialColumns + column;
}

/// Keeps at most `maximum` values, balanced over the grid and in rank order within it.
///
/// Values are taken in rounds: round `k` walks the input in order and keeps each value whose cell
/// has kept only `k` so far, so every occupied cell gives up its best-ranked value before any cell
/// gives up its second. A set that already fits is returned unchanged, in its own order.
///
/// @tparam ValueT the element type.
/// @tparam Position a callable returning a value's `(x, y)` pair.
/// @param values the candidates, best rank first.
/// @param maximum how many to keep.
/// @param minX the grid rectangle's left edge.
/// @param maxX its right edge.
/// @param minY its first edge on the vertical axis.
/// @param maxY its last edge on the vertical axis.
/// @param position reads one value's position.
/// @return the kept values, in the order they were kept.
template <typename ValueT, typename Position>
[[nodiscard]] std::vector<ValueT> spatialSelection(const std::vector<ValueT>& values, std::uint32_t maximum,
                                                   double minX, double maxX, double minY, double maxY,
                                                   Position position)
{
    if (values.size() <= maximum)
    {
        return values;
    }

    std::vector<ValueT> selected;
    selected.reserve(maximum);
    std::vector<bool> used(values.size(), false);
    std::array<std::uint32_t, kSpatialColumns * kSpatialRows> cellCounts{};

    for (std::uint32_t pass = 0u; pass < maximum && selected.size() < maximum; ++pass)
    {
        bool added = false;
        for (std::size_t index = 0u; index < values.size() && selected.size() < maximum; ++index)
        {
            if (used[index])
            {
                continue;
            }
            const auto [x, y] = position(values[index]);
            const std::uint32_t cell = spatialCell(x, y, minX, maxX, minY, maxY);
            if (cellCounts[cell] > pass)
            {
                continue;
            }
            selected.push_back(values[index]);
            used[index] = true;
            ++cellCounts[cell];
            added = true;
        }
        if (!added)
        {
            break;
        }
    }

    for (std::size_t index = 0u; index < values.size() && selected.size() < maximum; ++index)
    {
        if (!used[index])
        {
            selected.push_back(values[index]);
        }
    }
    return selected;
}

/// The coarsest HEALPix order whose pixels all fit inside one catalog query cone.
///
/// When the grid's largest centre-to-corner distance is within the query radius, the cones about
/// its pixel centres cover the sky. The search stops at `kMaximumPlateSolveBlindHealpixOrder` even
/// when that order's pixels are still too large.
///
/// @param queryRadiusDegrees the per-centre cone radius.
/// @return the order, or the HEALPix failure.
[[nodiscard]] Result<std::uint8_t> chooseBlindOrder(double queryRadiusDegrees)
{
    for (std::uint8_t order = 0u; order <= kMaximumPlateSolveBlindHealpixOrder; ++order)
    {
        const HealpixGrid grid{.order = order, .ordering = HealpixOrdering::Ring};
        const Result<double> radius = healpixMaximumPixelRadiusDegrees(grid);
        if (!radius.hasValue())
        {
            return radius.status();
        }
        if (*radius <= queryRadiusDegrees)
        {
            return order;
        }
    }
    return kMaximumPlateSolveBlindHealpixOrder;
}

/// Checks the execution and plan every stage after planning needs.
///
/// @param execution the operation.
/// @param plan the plan the stage was given.
/// @param operation the public operation, for the failure.
/// @return success, or `ErrorCode::NotInitialized` for an empty execution or an invalid plan.
[[nodiscard]] Status validatePlanUse(const ExecutionContext& execution, const PlateSolvePlan& plan,
                                     const char* operation)
{
    if (!execution.valid())
    {
        return failure(ErrorCode::NotInitialized, operation, "plate solving requires a live execution context");
    }
    if (!plan.valid())
    {
        return failure(ErrorCode::NotInitialized, operation, "plate solving requires a checked plan");
    }
    return Status::success();
}

} // namespace

Result<double> estimatePlateSolveCatalogQueryRadius(std::uint32_t imageWidth, std::uint32_t imageHeight,
                                                    double scaleArcsecPerPixel)
{
    if (imageWidth == 0u)
    {
        return parameterFailure(plate_solver_detail::kPlanOperation, "imageWidth",
                                "a plate-solve image width must not be zero");
    }
    if (imageHeight == 0u)
    {
        return parameterFailure(plate_solver_detail::kPlanOperation, "imageHeight",
                                "a plate-solve image height must not be zero");
    }
    if (!finitePositive(scaleArcsecPerPixel))
    {
        return parameterFailure(plate_solver_detail::kPlanOperation, "scaleArcsecPerPixel",
                                "a plate-solve scale must be finite and positive");
    }

    const double diagonalPixels = std::hypot(static_cast<double>(imageWidth), static_cast<double>(imageHeight));
    const double radius = (std::max)(0.5 * diagonalPixels * scaleArcsecPerPixel / kArcsecondsPerDegree * 1.25,
                                     kMinimumPlateSolveCatalogRadiusDegrees);
    if (!std::isfinite(radius) || radius > 180.0)
    {
        return failure(ErrorCode::OutOfRange, plate_solver_detail::kPlanOperation,
                       "the scaled plate-solve field does not form a finite catalog cone");
    }
    return radius;
}

Result<PlateSolvePlan> planPlateSolve(const PlateSolveRequest& request)
{
    if (request.imageWidth == 0u)
    {
        return parameterFailure(plate_solver_detail::kPlanOperation, "imageWidth",
                                "a plate-solve image width must not be zero");
    }
    if (request.imageHeight == 0u)
    {
        return parameterFailure(plate_solver_detail::kPlanOperation, "imageHeight",
                                "a plate-solve image height must not be zero");
    }
    if (request.initialCenter.has_value() && !isSkyCoordinate(*request.initialCenter))
    {
        return parameterFailure(plate_solver_detail::kPlanOperation, "initialCenter",
                                "a plate-solve centre hint must be a finite sky direction");
    }
    if (request.approximateScaleArcsecPerPixel.has_value() && !finitePositive(*request.approximateScaleArcsecPerPixel))
    {
        return parameterFailure(plate_solver_detail::kPlanOperation, "approximateScaleArcsecPerPixel",
                                "a stated plate-solve scale must be finite and positive");
    }
    if (!std::isfinite(request.searchRadiusDegrees) || request.searchRadiusDegrees < 0.0 ||
        request.searchRadiusDegrees > 180.0)
    {
        return parameterFailure(plate_solver_detail::kPlanOperation, "searchRadiusDegrees",
                                "the trial-centre search radius must be in [0, 180] degrees");
    }
    if (request.magnitudeLimit.has_value() && !std::isfinite(*request.magnitudeLimit))
    {
        return parameterFailure(plate_solver_detail::kPlanOperation, "magnitudeLimit",
                                "a catalog magnitude limit must be finite");
    }
    if (request.maximumRefinementIterations == 0u || request.maximumRefinementIterations > kMaximumRefinementIterations)
    {
        return parameterFailure(plate_solver_detail::kPlanOperation, "maximumRefinementIterations",
                                "plate-solve refinement iterations must be in [1, 32]");
    }
    if (request.maximumImageStars < 3u || request.maximumImageStars > kMaximumSelectedStars)
    {
        return parameterFailure(plate_solver_detail::kPlanOperation, "maximumImageStars",
                                "the selected image-star limit must be in [3, 4096]");
    }
    if (request.maximumCatalogStars < 3u || request.maximumCatalogStars > kMaximumSelectedStars)
    {
        return parameterFailure(plate_solver_detail::kPlanOperation, "maximumCatalogStars",
                                "the selected catalog-star limit must be in [3, 4096]");
    }
    if (request.maximumCatalogCandidates < request.maximumCatalogStars ||
        request.maximumCatalogCandidates > kMaximumCatalogCandidates)
    {
        return parameterFailure(plate_solver_detail::kPlanOperation, "maximumCatalogCandidates",
                                "the catalog candidate limit must cover selected stars and not exceed one million");
    }
    if (request.nearSearchRings > kMaximumNearRings)
    {
        return parameterFailure(plate_solver_detail::kPlanOperation, "nearSearchRings",
                                "the near-search ring count must not exceed 64");
    }
    if (request.nearSearchSamplesPerRing == 0u || request.nearSearchSamplesPerRing > kMaximumNearSamples)
    {
        return parameterFailure(plate_solver_detail::kPlanOperation, "nearSearchSamplesPerRing",
                                "the near-search sample count must be in [1, 1024]");
    }
    if (!finitePositive(request.convergenceToleranceArcsec))
    {
        return parameterFailure(plate_solver_detail::kPlanOperation, "convergenceToleranceArcsec",
                                "the convergence tolerance must be finite and positive");
    }
    const std::uint32_t maximumMatches = (std::min)(request.maximumImageStars, request.maximumCatalogStars);
    if (request.minimumMatchedStars < 3u || request.minimumMatchedStars > maximumMatches)
    {
        return parameterFailure(plate_solver_detail::kPlanOperation, "minimumMatchedStars",
                                "the required match count must fit both selected fields");
    }
    if (!finitePositive(request.maximumRmsResidualPixels))
    {
        return parameterFailure(plate_solver_detail::kPlanOperation, "maximumRmsResidualPixels",
                                "the maximum plate-solve residual must be finite and positive");
    }
    if (request.maximumTriangles == 0u || request.maximumTriangles > kMaximumTriangles)
    {
        return parameterFailure(plate_solver_detail::kPlanOperation, "maximumTriangles",
                                "the plate-solve triangle limit must be in [1, 100000]");
    }
    if (!finitePositive(request.minimumTriangleSidePixels))
    {
        return parameterFailure(plate_solver_detail::kPlanOperation, "minimumTriangleSidePixels",
                                "the minimum triangle side must be finite and positive");
    }
    if (!std::isfinite(request.triangleRatioTolerance) || request.triangleRatioTolerance <= 0.0 ||
        request.triangleRatioTolerance > 0.25)
    {
        return parameterFailure(plate_solver_detail::kPlanOperation, "triangleRatioTolerance",
                                "the triangle-ratio tolerance must be in (0, 0.25]");
    }
    if (request.ransacIterations == 0u || request.ransacIterations > kMaximumRansacIterations)
    {
        return parameterFailure(plate_solver_detail::kPlanOperation, "ransacIterations",
                                "the robust-fit iteration count must be in [1, 100000]");
    }

    const double scale = request.approximateScaleArcsecPerPixel.value_or(kDefaultPlateSolveScaleArcsecPerPixel);
    const Result<double> queryRadius =
        estimatePlateSolveCatalogQueryRadius(request.imageWidth, request.imageHeight, scale);
    if (!queryRadius.hasValue())
    {
        return queryRadius.status();
    }

    std::uint8_t blindOrder = 0u;
    std::uint32_t trialCount = 1u;
    if (request.initialCenter.has_value())
    {
        if (request.searchRadiusDegrees > 0.0 && request.nearSearchRings > 0u)
        {
            const std::uint64_t count =
                1u + static_cast<std::uint64_t>(request.nearSearchRings) * request.nearSearchSamplesPerRing;
            if (count > (std::numeric_limits<std::uint32_t>::max)())
            {
                return failure(ErrorCode::LimitExceeded, plate_solver_detail::kPlanOperation,
                               "the hinted plate-solve trial sequence is too large to represent");
            }
            trialCount = static_cast<std::uint32_t>(count);
        }
    }
    else
    {
        const Result<std::uint8_t> selectedOrder = chooseBlindOrder(*queryRadius);
        if (!selectedOrder.hasValue())
        {
            return selectedOrder.status();
        }
        blindOrder = *selectedOrder;
        trialCount = healpixPixelCount(HealpixGrid{.order = blindOrder, .ordering = HealpixOrdering::Ring});
    }

    return PlateSolvePlan(request, scale, *queryRadius, blindOrder, trialCount);
}

Result<PlateSolveInput> preparePlateSolveInput(const ExecutionContext& execution, std::span<const PlateSolveStar> stars,
                                               const PlateSolvePlan& plan)
{
    const Status ready = validatePlanUse(execution, plan, plate_solver_detail::kInputOperation);
    if (ready.failed())
    {
        return ready;
    }
    const auto limits = execution.limits();
    if (plan.request().imageWidth > limits.maxImageWidth || plan.request().imageHeight > limits.maxImageHeight)
    {
        return failure(ErrorCode::LimitExceeded, plate_solver_detail::kInputOperation,
                       "the plate-solve image dimensions exceed the engine context limits");
    }
    if (stars.size() > kMaximumInputStars || stars.size() > (std::numeric_limits<std::uint32_t>::max)())
    {
        return Status::failure(
            ErrorCode::LimitExceeded,
            StatusDetail{.message = "the plate-solve input exceeds the supported star count",
                         .operation = plate_solver_detail::kInputOperation,
                         .domain = ErrorDomain::Algorithms,
                         .context = {StatusContextField::ofCount(ContextKey::Requested, stars.size()),
                                     StatusContextField::ofCount(ContextKey::Limit, kMaximumInputStars)}});
    }
    if (stars.size() < 3u)
    {
        return failure(ErrorCode::EmptySelection, plate_solver_detail::kInputOperation,
                       "plate solving needs at least three detected stars");
    }

    const double maximumX = static_cast<double>(plan.request().imageWidth) - 0.5;
    const double maximumY = static_cast<double>(plan.request().imageHeight) - 0.5;
    for (std::size_t index = 0u; index < stars.size(); ++index)
    {
        if ((index & 255u) == 0u)
        {
            const Status cancellation = execution.checkCancellation();
            if (cancellation.failed())
            {
                return plate_solver_detail::cancelledFailure(plate_solver_detail::kInputOperation, cancellation);
            }
        }
        const PixelCoordinate position = stars[index].position;
        if (!std::isfinite(position.x) || !std::isfinite(position.y) || position.x < 0.5 || position.y < 0.5 ||
            position.x > maximumX || position.y > maximumY)
        {
            return Status::failure(
                ErrorCode::InvalidData,
                StatusDetail{.message = "a plate-solve star is not a finite in-frame pixel position",
                             .operation = plate_solver_detail::kInputOperation,
                             .domain = ErrorDomain::Algorithms,
                             .context = {StatusContextField::ofCount(ContextKey::Completed, index)}});
        }
    }

    try
    {
        // A crowded frame is matched over its disc, brightest first, as the catalog search keeps the catalog.
        const auto insideDisc = [&plan](const PlateSolveStar& star) {
            return plate_solver_detail::insideMatchingDisc(star.position, plan.request());
        };
        if (static_cast<std::size_t>(std::count_if(stars.begin(), stars.end(), insideDisc)) >=
            kMatchingDiscMinimumStars)
        {
            std::vector<PlateSolveStar> selected;
            selected.reserve((std::min)(stars.size(), static_cast<std::size_t>(plan.request().maximumImageStars)));
            for (const PlateSolveStar& star : stars)
            {
                if (selected.size() == plan.request().maximumImageStars)
                {
                    break;
                }
                if (insideDisc(star))
                {
                    selected.push_back(star);
                }
            }
            return PlateSolveInput{.stars = std::move(selected),
                                   .suppliedStarCount = static_cast<std::uint32_t>(stars.size())};
        }
        const std::vector<PlateSolveStar> values(stars.begin(), stars.end());
        std::vector<PlateSolveStar> selected =
            spatialSelection(values, plan.request().maximumImageStars, 0.5, maximumX, 0.5, maximumY,
                             [](const PlateSolveStar& star) { return std::pair{star.position.x, star.position.y}; });
        return PlateSolveInput{.stars = std::move(selected),
                               .suppliedStarCount = static_cast<std::uint32_t>(stars.size())};
    }
    catch (const std::bad_alloc&)
    {
        return Status::failure(ErrorCode::AllocationFailed);
    }
}

Result<PlateSolveCatalogSearchResult> searchPlateSolveCatalog(const ExecutionContext& execution,
                                                              const SkyCatalog& catalog, const PlateSolveInput& input,
                                                              const PlateSolvePlan& plan,
                                                              const SkyCoordinate& trialCenter)
{
    const Status ready = validatePlanUse(execution, plan, plate_solver_detail::kSearchOperation);
    if (ready.failed())
    {
        return ready;
    }
    if (!catalog.valid())
    {
        return failure(ErrorCode::NotInitialized, plate_solver_detail::kSearchOperation,
                       "plate solving requires a built sky catalog");
    }
    if (input.stars.size() < 3u)
    {
        return failure(ErrorCode::EmptySelection, plate_solver_detail::kSearchOperation,
                       "plate solving requires prepared image stars");
    }
    if (!isSkyCoordinate(trialCenter))
    {
        return parameterFailure(plate_solver_detail::kSearchOperation, "trialCenter",
                                "the plate-solve trial centre must be a finite sky direction");
    }

    const CatalogRegionQuery query{.center = trialCenter,
                                   .radiusDegrees = plan.catalogQueryRadiusDegrees(),
                                   .magnitudeLimit = plan.request().magnitudeLimit,
                                   .admitNamedEntriesWithoutMagnitude = false,
                                   .maximumResults = plan.request().maximumCatalogCandidates};
    Result<CatalogQueryResult> queried = queryCatalogRegion(execution, catalog, query);
    if (!queried.hasValue())
    {
        return queried.status();
    }

    try
    {
        PlateSolveCatalogSearchResult result{.trialCenter = trialCenter,
                                             .queryRadiusDegrees = plan.catalogQueryRadiusDegrees(),
                                             .examinedCatalogEntries = queried->examinedCount,
                                             .admittedCatalogEntries = queried->admittedCount,
                                             .queryTruncated = queried->truncated};
        result.stars.reserve(queried->entryIndices.size());
        const double pixelsPerDegree = kArcsecondsPerDegree / plan.effectiveScaleArcsecPerPixel();
        for (std::size_t position = 0u; position < queried->entryIndices.size(); ++position)
        {
            if ((position & 255u) == 0u)
            {
                const Status cancellation = execution.checkCancellation();
                if (cancellation.failed())
                {
                    return plate_solver_detail::cancelledFailure(plate_solver_detail::kSearchOperation, cancellation);
                }
            }
            const std::uint32_t index = queried->entryIndices[position];
            const CatalogEntry* entry = catalog.entry(index);
            if (entry == nullptr)
            {
                return failure(ErrorCode::Internal, plate_solver_detail::kSearchOperation,
                               "a catalog query returned an index outside its catalog");
            }
            if (entry->extent.has_value())
            {
                ++result.rejectedExtendedEntries;
                continue;
            }
            const Result<GnomonicCoordinate> tangent = projectGnomonic(entry->coordinate, trialCenter);
            if (!tangent.hasValue())
            {
                ++result.rejectedProjectionEntries;
                continue;
            }
            result.stars.push_back(PlateSolveCatalogStar{.catalogIndex = index,
                                                         .coordinate = entry->coordinate,
                                                         .tangentXDegrees = tangent->xDegrees,
                                                         .tangentYDegrees = tangent->yDegrees,
                                                         .matchingX = tangent->xDegrees * pixelsPerDegree,
                                                         .matchingY = tangent->yDegrees * pixelsPerDegree});
        }

        if (result.stars.size() < 3u)
        {
            return failure(ErrorCode::EmptySelection, plate_solver_detail::kSearchOperation,
                           "fewer than three catalog point sources project at this trial centre");
        }

        // The catalog keeps the region the image's stars were kept over. A frame matched over its disc is set
        // beside the catalog's stars of the same circle about the trial centre, however few they are: a catalog
        // star outside it has no image star to be matched with, and among the matcher's brightest it would only
        // displace one that has. The query answered brightest first, and that order is kept.
        if (plate_solver_detail::keptOverMatchingDisc(input, plan))
        {
            const double disc = plate_solver_detail::matchingDiscRadius(plan.request());
            std::erase_if(result.stars, [disc](const PlateSolveCatalogStar& star) {
                return std::hypot(star.matchingX, star.matchingY) > disc;
            });
            if (result.stars.size() < 3u)
            {
                return failure(ErrorCode::EmptySelection, plate_solver_detail::kSearchOperation,
                               "fewer than three catalog point sources lie in the matching disc at this trial centre");
            }
            if (result.stars.size() > plan.request().maximumCatalogStars)
            {
                result.stars.resize(plan.request().maximumCatalogStars);
            }
        }
        else
        {
            // A frame whose stars reach past the disc keeps its whole extent, and the catalog the sources that can
            // be in the frame: a rectangle 1.1 times the image about the trial centre, aligned with east and north
            // because the field's rotation is not known yet. With sixteen or more inside, the rest are dropped;
            // with fewer, the ones outside stay, behind those inside.
            const double preferredHalfWidth = 0.55 * static_cast<double>(plan.request().imageWidth);
            const double preferredHalfHeight = 0.55 * static_cast<double>(plan.request().imageHeight);
            const auto preferredEnd = std::stable_partition(
                result.stars.begin(), result.stars.end(), [preferredHalfWidth, preferredHalfHeight](const auto& star) {
                    return std::abs(star.matchingX) <= preferredHalfWidth &&
                           std::abs(star.matchingY) <= preferredHalfHeight;
                });
            const std::size_t preferredCount = static_cast<std::size_t>(preferredEnd - result.stars.begin());
            if (preferredCount >= 16u)
            {
                result.stars.erase(preferredEnd, result.stars.end());
            }

            if (result.stars.size() > plan.request().maximumCatalogStars)
            {
                const double fallbackHalfExtent = 0.55 * std::hypot(static_cast<double>(plan.request().imageWidth),
                                                                    static_cast<double>(plan.request().imageHeight));
                result.stars = spatialSelection(
                    result.stars, plan.request().maximumCatalogStars, -fallbackHalfExtent, fallbackHalfExtent,
                    -fallbackHalfExtent, fallbackHalfExtent,
                    [](const PlateSolveCatalogStar& star) { return std::pair{star.matchingX, star.matchingY}; });
            }
        }

        if (result.queryTruncated)
        {
            execution.reportDiagnostic(DiagnosticSeverity::Warning, ErrorDomain::Algorithms,
                                       plate_solver_detail::kSearchOperation,
                                       "the plate-solve catalog query was truncated at its configured bound");
        }
        return result;
    }
    catch (const std::bad_alloc&)
    {
        return Status::failure(ErrorCode::AllocationFailed);
    }
}

Result<PlateSolveMatchResult> matchPlateSolveStars(const ExecutionContext& execution, const PlateSolveInput& input,
                                                   const PlateSolveCatalogSearchResult& catalogSearch,
                                                   const PlateSolvePlan& plan)
{
    const Status ready = validatePlanUse(execution, plan, plate_solver_detail::kMatchOperation);
    if (ready.failed())
    {
        return ready;
    }
    if (input.stars.size() < 3u || catalogSearch.stars.size() < 3u)
    {
        return failure(ErrorCode::EmptySelection, plate_solver_detail::kMatchOperation,
                       "plate-solve matching needs at least three points in each field");
    }
    if (!isSkyCoordinate(catalogSearch.trialCenter))
    {
        return failure(ErrorCode::InvalidData, plate_solver_detail::kMatchOperation,
                       "the projected catalog carries an invalid trial centre");
    }

    try
    {
        std::vector<StarAlignmentPoint> image;
        std::vector<StarAlignmentPoint> catalog;
        image.reserve(input.stars.size());
        catalog.reserve(catalogSearch.stars.size());
        // Both sides are measured from their own centre - the image's `(width / 2, height / 2)`
        // and the trial centre at the pseudo-pixel origin - so a hint near the true centre leaves
        // the affine model a small translation.
        const double centerX = 0.5 * static_cast<double>(plan.request().imageWidth);
        const double centerY = 0.5 * static_cast<double>(plan.request().imageHeight);
        for (const PlateSolveStar& star : input.stars)
        {
            image.push_back(StarAlignmentPoint{.x = star.position.x - centerX, .y = star.position.y - centerY});
        }
        for (const PlateSolveCatalogStar& star : catalogSearch.stars)
        {
            catalog.push_back(StarAlignmentPoint{.x = star.matchingX, .y = star.matchingY});
        }

        const StarAlignmentRequest request{.model = StarTransformModel::Affine,
                                           .trianglePolicy = StarTrianglePolicy::Automatic,
                                           .maximumTriangles = plan.request().maximumTriangles,
                                           .minimumTriangleSide = plan.request().minimumTriangleSidePixels,
                                           .triangleRatioTolerance = plan.request().triangleRatioTolerance,
                                           .triangleToleranceMinimumScale = 0.5,
                                           .triangleToleranceMaximumScale = 1.0,
                                           .maximumTriangleNeighbours = 5u,
                                           .minimumVotes = 3u,
                                           .minimumMatches = plan.request().minimumMatchedStars,
                                           .inlierThreshold = plan.request().maximumRmsResidualPixels,
                                           .minimumInlierFraction = 0.25,
                                           .ransacIterations = plan.request().ransacIterations,
                                           .seed = plan.request().deterministicSeed};
        Result<StarAlignmentResult> aligned = alignStarFields(execution, image, catalog, request);
        if (!aligned.hasValue())
        {
            return aligned.status();
        }

        PlateSolveMatchResult result{.imageTriangles = aligned->quality.sourceTriangles,
                                     .catalogTriangles = aligned->quality.referenceTriangles,
                                     .candidateMatches = aligned->quality.candidateMatches,
                                     .rejectedOutliers = aligned->quality.rejectedOutliers,
                                     .peakWorkingBytes = aligned->quality.workingBytes};
        result.matches.reserve(aligned->matches.size());
        for (const StarMatch& match : aligned->matches)
        {
            if (match.sourceIndex >= input.stars.size() || match.referenceIndex >= catalogSearch.stars.size())
            {
                return failure(ErrorCode::Internal, plate_solver_detail::kMatchOperation,
                               "star alignment returned a correspondence outside its inputs");
            }
            const PlateSolveCatalogStar& catalogStar = catalogSearch.stars[match.referenceIndex];
            result.matches.push_back(PlateSolveMatch{.imageStarIndex = match.sourceIndex,
                                                     .catalogIndex = catalogStar.catalogIndex,
                                                     .imagePosition = input.stars[match.sourceIndex].position,
                                                     .catalogCoordinate = catalogStar.coordinate,
                                                     .tangentXDegrees = catalogStar.tangentXDegrees,
                                                     .tangentYDegrees = catalogStar.tangentYDegrees,
                                                     .votes = match.votes,
                                                     .confidence = match.confidence,
                                                     .residualPixels = match.residual});
        }
        return result;
    }
    catch (const std::bad_alloc&)
    {
        return Status::failure(ErrorCode::AllocationFailed);
    }
}

} // namespace an
