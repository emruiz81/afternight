// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the registration canvas, its overlaps, its coordinate maps and its warp.
//
// A warp takes one of three routes, decided when it is planned. A transform that is a whole-pixel
// translation with no distortion is a direct rectangle copy. Any other linear transform with no
// distortion is handed to the geometry module as one destination-to-source matrix. A polynomial
// transform or a plate distortion is warped through a destination-to-source coordinate map filled
// in bands of rows under a fixed scratch ceiling, and the bands' Float32 samples are gathered
// through the interpolating kernel. Every coordinate here is an engine pixel coordinate, whose
// first centre is (0.5, 0.5); the geometry module and the gather put centres at integers, and the
// half pixel between them is converted at those two boundaries only.

#include <AfterNightEngine/Algorithms/Registration/RegistrationPlan.h>

#include <AfterNightEngine/Algorithms/Geometry/Geometry.h>
#include <AfterNightEngine/Algorithms/Kernel/GatherScatterKernel.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/PlaneView.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>
#include <AfterNightEngine/Runtime/ParallelExecutor.h>

#include "algorithms/registration/RegistrationInternal.h"
#include "kernels/reference/PlaneSourceAccess.h"
#include "kernels/reference/ReferenceKernelInternal.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory_resource>
#include <optional>
#include <string>
#include <vector>

namespace an
{
namespace
{

constexpr const char* kCanvasOperation = "an::planRegistrationCanvas";
constexpr const char* kOverlapOperation = "an::findRegistrationOverlaps";
constexpr const char* kDestinationMapOperation = "an::computeRegistrationDestinationToSourceMap";
constexpr const char* kSourceMapOperation = "an::computeRegistrationSourceToDestinationMap";
constexpr const char* kPlanOperation = "an::planRegistrationWarp";
constexpr const char* kWarpOperation = "an::warpRegisteredImage";

/// The tallest band of rows one coordinate map fills in a single tile.
constexpr std::uint32_t kMaximumMapTileHeight = 32u;

/// How many tiles each worker should have to draw from.
constexpr std::uint32_t kMapTilesPerWorker = 4u;

/// What one mapped pixel costs, so the scheduler can decide whether a tile is worth handing over.
///
/// Two floats out, and arithmetic in the low hundreds of operations for an inverted pixel: a
/// polynomial Newton step evaluates two polynomials and their derivatives, and a distortion inverse
/// may add its own. Leaving the cost at its default of one operation per item tells the scheduler a
/// megapixel map is smaller than its own hand-off.
constexpr std::uint64_t kMapBytesPerPixel = 8u;
constexpr std::uint64_t kInverseMapOperationsPerPixel = 160u;
constexpr std::uint64_t kForwardMapOperationsPerPixel = 48u;

/// The most engine memory one warp holds for coordinates and gathered samples at once.
///
/// The number that makes the destination band a band rather than the whole canvas: a full-canvas
/// coordinate map of a large sensor is two doubles per pixel on top of the frame itself, and the
/// warp needs none of it at once.
constexpr std::uint64_t kMaximumWarpScratchBytes = 4u * 1024u * 1024u;

/// Builds a registration-domain failure.
///
/// @param code the typed reason.
/// @param message the explanation for a person.
/// @param operation the public operation that failed.
/// @return the complete failure.
[[nodiscard]] Status planFailure(const ErrorCode code, std::string message, const char* const operation)
{
    return Status::failure(
        code, StatusDetail{.message = std::move(message), .operation = operation, .domain = ErrorDomain::Algorithms});
}

/// Whether a position holds two finite numbers.
///
/// @param pixel the position to inspect.
/// @return true when both components are finite.
[[nodiscard]] bool isFinitePixel(const PixelCoordinate& pixel) noexcept
{
    return std::isfinite(pixel.x) && std::isfinite(pixel.y);
}

/// Whether a rectangle is finite and not inverted.
///
/// @param bounds the rectangle to inspect.
/// @return true when all four numbers are finite and the maxima are not below the minima.
[[nodiscard]] bool isUsableBounds(const RegistrationBounds& bounds) noexcept
{
    return std::isfinite(bounds.minimumX) && std::isfinite(bounds.minimumY) && std::isfinite(bounds.maximumX) &&
           std::isfinite(bounds.maximumY) && bounds.maximumX >= bounds.minimumX && bounds.maximumY >= bounds.minimumY;
}

/// Whether a value is a whole number a canvas offset can hold.
///
/// @param value the offset to inspect.
/// @return true when it is finite, integral, and within the offset type.
[[nodiscard]] bool isWholePixelOffset(const double value) noexcept
{
    constexpr double kOffsetCeiling = 1.0e15;
    return std::isfinite(value) && value == std::trunc(value) && std::abs(value) <= kOffsetCeiling;
}

/// Whether a distortion model would change any position.
///
/// @param distortion the model, or null.
/// @return true when a model is present and has a polynomial in either direction.
[[nodiscard]] bool correctsAnything(const SipDistortion* distortion) noexcept
{
    return distortion != nullptr && (distortion->hasForwardModel() || distortion->hasInverseModel());
}

/// One coordinate map's shared counters.
struct MapCounters
{
    /// Pixels that received a finite position.
    std::atomic<std::uint64_t> mapped{0u};

    /// Pixels left holding the unmapped sentinel.
    std::atomic<std::uint64_t> unmapped{0u};

    /// Newton steps run across every pixel that needed one.
    std::atomic<std::uint64_t> newtonIterations{0u};

    /// Pixels answered from the previous position along the row.
    std::atomic<std::uint64_t> warmAccepted{0u};

    /// Pixels whose warm solve was abandoned.
    std::atomic<std::uint64_t> warmRejected{0u};

    /// The largest residual any solved pixel left, as its bit pattern.
    std::atomic<std::uint64_t> maximumResidualBits{0u};
};

/// One tile's private tally, published once when the tile finishes.
struct MapTally
{
    /// Pixels that received a finite position.
    std::uint64_t mapped = 0u;

    /// Pixels left holding the unmapped sentinel.
    std::uint64_t unmapped = 0u;

    /// Newton steps run.
    std::uint64_t newtonIterations = 0u;

    /// Pixels answered from a warm seed.
    std::uint64_t warmAccepted = 0u;

    /// Pixels whose warm solve was abandoned.
    std::uint64_t warmRejected = 0u;

    /// The largest residual this tile left.
    double maximumResidual = 0.0;
};

/// Publishes one tile's tally into the shared counters.
///
/// @param counters the shared counters.
/// @param tally the tile's private tally.
void publishTally(MapCounters& counters, const MapTally& tally) noexcept
{
    counters.mapped.fetch_add(tally.mapped, std::memory_order_relaxed);
    counters.unmapped.fetch_add(tally.unmapped, std::memory_order_relaxed);
    counters.newtonIterations.fetch_add(tally.newtonIterations, std::memory_order_relaxed);
    counters.warmAccepted.fetch_add(tally.warmAccepted, std::memory_order_relaxed);
    counters.warmRejected.fetch_add(tally.warmRejected, std::memory_order_relaxed);
    // The residual is compared as an unsigned integer, which orders non-negative doubles the same
    // way they order as numbers, so the maximum needs no lock and no floating compare-exchange.
    std::uint64_t bits = 0u;
    std::memcpy(&bits, &tally.maximumResidual, sizeof(bits));
    std::uint64_t seen = counters.maximumResidualBits.load(std::memory_order_relaxed);
    while (bits > seen && !counters.maximumResidualBits.compare_exchange_weak(seen, bits, std::memory_order_relaxed))
    {
    }
}

/// Reads the shared counters into a report.
///
/// @param counters the shared counters.
/// @return the finished report.
[[nodiscard]] RegistrationMapReport readCounters(const MapCounters& counters) noexcept
{
    RegistrationMapReport report;
    report.mapped = counters.mapped.load(std::memory_order_relaxed);
    report.unmapped = counters.unmapped.load(std::memory_order_relaxed);
    report.newtonIterations = counters.newtonIterations.load(std::memory_order_relaxed);
    report.warmSeedAccepted = counters.warmAccepted.load(std::memory_order_relaxed);
    report.warmSeedRejected = counters.warmRejected.load(std::memory_order_relaxed);
    const std::uint64_t bits = counters.maximumResidualBits.load(std::memory_order_relaxed);
    std::memcpy(&report.maximumResidualPixels, &bits, sizeof(report.maximumResidualPixels));
    return report;
}

/// Checks the grid and the planes one coordinate map is asked to fill.
///
/// @param request the grid and the caller-owned planes.
/// @param operation the public operation being validated.
/// @return the failure, or success.
[[nodiscard]] Status checkMapRequest(const RegistrationMapRequest& request, const char* const operation)
{
    if (request.width == 0u || request.height == 0u)
    {
        return planFailure(ErrorCode::InvalidArgument, "a coordinate map covers at least one pixel", operation);
    }
    if (!isRegistrationInverseSeed(request.seed))
    {
        return planFailure(ErrorCode::InvalidArgument, "a coordinate map names a seed policy this build has",
                           operation);
    }
    if (!isFinitePixel(request.postTranslation))
    {
        return planFailure(ErrorCode::InvalidArgument, "a coordinate map's translation is finite", operation);
    }
    const std::uint64_t pixels = static_cast<std::uint64_t>(request.width) * request.height;
    if (pixels > static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)()))
    {
        return planFailure(ErrorCode::LimitExceeded, "a coordinate map's pixel count does not fit", operation);
    }
    if (request.xMap.size() != static_cast<std::size_t>(pixels) ||
        request.yMap.size() != static_cast<std::size_t>(pixels))
    {
        return planFailure(ErrorCode::IncompatibleArguments,
                           "a coordinate map's planes are exactly one entry per pixel", operation);
    }
    const float* xBegin = request.xMap.data();
    const float* yBegin = request.yMap.data();
    if (xBegin < yBegin + request.yMap.size() && yBegin < xBegin + request.xMap.size())
    {
        return planFailure(ErrorCode::Aliasing, "a coordinate map's two planes overlap", operation);
    }
    return Status::success();
}

/// Fills one destination-to-source tile.
///
/// The warm seed restarts at the beginning of every row and after every pixel that did not map,
/// so a pixel's answer depends on the row's traversal and never on how the grid was divided into
/// tiles.
///
/// @param transform the frame's source-to-reference model.
/// @param prepared the transform's linear inverse, prepared once for the whole map.
/// @param distortion the frame's plate distortion, or null.
/// @param request the grid, the planes and the policies.
/// @param tile the rows and columns to fill.
/// @param tally the tile's private counters.
void fillInverseTile(const RegistrationTransform& transform,
                     const registration_internal::PreparedRegistrationInverse& prepared,
                     const SipDistortion* distortion, const RegistrationMapRequest& request, const ParallelTile& tile,
                     MapTally& tally)
{
    const bool warmStart = request.seed == RegistrationInverseSeed::RowNeighbourWarmStart;
    for (std::uint32_t row = tile.yBegin; row < tile.yEnd; ++row)
    {
        const std::size_t rowOffset = static_cast<std::size_t>(row) * request.width;
        std::optional<PixelCoordinate> neighbour;
        for (std::uint32_t column = tile.xBegin; column < tile.xEnd; ++column)
        {
            const PixelCoordinate destination{.x = static_cast<double>(column) + 0.5,
                                              .y = static_cast<double>(row) + 0.5};
            const PixelCoordinate* seed = warmStart && neighbour.has_value() ? &neighbour.value() : nullptr;
            RegistrationInverseResult inverted;
            const std::size_t index = rowOffset + column;
            if (!registration_internal::invertWithPreparedInverse(transform, prepared, destination,
                                                                  request.postTranslation, warmStart, seed, inverted))
            {
                request.xMap[index] = unmappedCoordinate();
                request.yMap[index] = unmappedCoordinate();
                ++tally.unmapped;
                neighbour.reset();
                continue;
            }
            tally.newtonIterations += inverted.iterations;
            if (seed != nullptr)
            {
                if (inverted.warmSeedAccepted)
                {
                    ++tally.warmAccepted;
                }
                else
                {
                    ++tally.warmRejected;
                }
            }
            tally.maximumResidual = (std::max)(tally.maximumResidual, inverted.residualPixels);
            neighbour = inverted.pixel;

            PixelCoordinate source = inverted.pixel;
            if (correctsAnything(distortion))
            {
                const Result<SipInverseResult> corrected = applySipInverse(*distortion, source);
                if (!corrected.hasValue())
                {
                    request.xMap[index] = unmappedCoordinate();
                    request.yMap[index] = unmappedCoordinate();
                    ++tally.unmapped;
                    neighbour.reset();
                    continue;
                }
                source = corrected->pixel;
            }
            request.xMap[index] = static_cast<float>(source.x);
            request.yMap[index] = static_cast<float>(source.y);
            ++tally.mapped;
        }
    }
}

/// Fills one source-to-destination tile.
///
/// @param transform the frame's source-to-reference model.
/// @param distortion the frame's plate distortion, or null.
/// @param request the grid, the planes and the translation.
/// @param tile the rows and columns to fill.
/// @param tally the tile's private counters.
void fillForwardTile(const RegistrationTransform& transform, const SipDistortion* distortion,
                     const RegistrationMapRequest& request, const ParallelTile& tile, MapTally& tally)
{
    for (std::uint32_t row = tile.yBegin; row < tile.yEnd; ++row)
    {
        const std::size_t rowOffset = static_cast<std::size_t>(row) * request.width;
        for (std::uint32_t column = tile.xBegin; column < tile.xEnd; ++column)
        {
            const std::size_t index = rowOffset + column;
            PixelCoordinate source{.x = static_cast<double>(column) + 0.5, .y = static_cast<double>(row) + 0.5};
            if (correctsAnything(distortion))
            {
                const Result<PixelCoordinate> corrected = applySipForward(*distortion, source);
                if (!corrected.hasValue())
                {
                    request.xMap[index] = unmappedCoordinate();
                    request.yMap[index] = unmappedCoordinate();
                    ++tally.unmapped;
                    continue;
                }
                source = *corrected;
            }
            PixelCoordinate mapped{};
            if (!registration_internal::mapWithoutChecking(transform, source, request.postTranslation, mapped))
            {
                request.xMap[index] = unmappedCoordinate();
                request.yMap[index] = unmappedCoordinate();
                ++tally.unmapped;
                continue;
            }
            request.xMap[index] = static_cast<float>(mapped.x);
            request.yMap[index] = static_cast<float>(mapped.y);
            ++tally.mapped;
        }
    }
}

/// Fills a coordinate map in one direction.
///
/// @param execution the operation this map runs in.
/// @param transform the frame's source-to-reference model.
/// @param distortion the frame's plate distortion, or null.
/// @param request the grid, the planes and the policies.
/// @param inverse whether the destination-to-source direction is filled.
/// @param operation the public operation being run.
/// @return what the map did, or a validation, scheduling or cancellation failure.
[[nodiscard]] Result<RegistrationMapReport>
fillMap(const ExecutionContext& execution, const RegistrationTransform& transform, const SipDistortion* distortion,
        const RegistrationMapRequest& request, const bool inverse, const char* const operation)
{
    if (!execution.valid())
    {
        return planFailure(ErrorCode::NotInitialized,
                           "a coordinate map needs an execution context that holds a lease on an engine context",
                           operation);
    }
    if (const Status checked = checkMapRequest(request, operation); !checked.ok())
    {
        return checked;
    }

    const std::uint64_t wanted =
        (std::max<std::uint64_t>)(1u, static_cast<std::uint64_t>((std::max)(1u, execution.admittedConcurrency())) *
                                          kMapTilesPerWorker);
    const std::uint64_t rowsPerTile = (static_cast<std::uint64_t>(request.height) + wanted - 1u) / wanted;
    ParallelTileRequest tiles;
    tiles.width = request.width;
    tiles.height = request.height;
    tiles.tileWidth = request.width;
    tiles.tileHeight = static_cast<std::uint32_t>(
        (std::clamp<std::uint64_t>)(rowsPerTile, 1u, static_cast<std::uint64_t>(kMaximumMapTileHeight)));
    tiles.pixelCost =
        TaskCost{.bytesPerItem = kMapBytesPerPixel,
                 .operationsPerItem = inverse ? kInverseMapOperationsPerPixel : kForwardMapOperationsPerPixel};
    const Result<ParallelTilePlan> plan = planParallelTiles(execution, tiles);
    if (!plan.hasValue())
    {
        return plan.status();
    }

    // The linear inverse is the same matrix for every position of the map, so it is computed once
    // here rather than at each of a million pixels.
    const registration_internal::PreparedRegistrationInverse prepared =
        registration_internal::prepareRegistrationInverse(transform);
    MapCounters counters;
    const auto body = [&](const ParallelTile& tile) -> Status {
        MapTally tally;
        if (inverse)
        {
            fillInverseTile(transform, prepared, distortion, request, tile, tally);
        }
        else
        {
            fillForwardTile(transform, distortion, request, tile, tally);
        }
        publishTally(counters, tally);
        return Status::success();
    };
    if (const Status executed = executeParallelTiles(execution, *plan, std::span<std::byte>{}, body); !executed.ok())
    {
        return executed;
    }
    return readCounters(counters);
}

/// The destination-to-source matrix a linear warp hands to the geometry module.
///
/// The geometry module puts pixel centres at integers and this domain puts the first centre at one
/// half, so the conversion is a half-pixel shift on each side of the inverse: a destination kernel
/// coordinate becomes an engine coordinate, the canvas translation is removed, the transform is
/// inverted, and the source engine coordinate becomes a source kernel coordinate.
///
/// @param transform the frame's source-to-reference model.
/// @param canvasOrigin the canvas offset.
/// @param matrix the row-major matrix, in the geometry module's convention.
/// @return false when the transform has no inverse.
[[nodiscard]] bool buildGeometryMatrix(const RegistrationTransform& transform, const PixelCoordinate& canvasOrigin,
                                       std::array<double, 9>& matrix)
{
    const Result<RegistrationTransform> inverse = invertRegistrationTransform(transform);
    if (!inverse.hasValue())
    {
        return false;
    }
    const std::array<double, 9>& raw = inverse->matrix();
    // The composition `translate(-0.5) * inverse * translate(0.5 - origin)`, written out.
    const double shiftX = 0.5 - canvasOrigin.x;
    const double shiftY = 0.5 - canvasOrigin.y;
    const double numeratorX2 = raw[0] * shiftX + raw[1] * shiftY + raw[2];
    const double numeratorY2 = raw[3] * shiftX + raw[4] * shiftY + raw[5];
    const double denominator2 = raw[6] * shiftX + raw[7] * shiftY + raw[8];
    matrix[0] = raw[0] - 0.5 * raw[6];
    matrix[1] = raw[1] - 0.5 * raw[7];
    matrix[2] = numeratorX2 - 0.5 * denominator2;
    matrix[3] = raw[3] - 0.5 * raw[6];
    matrix[4] = raw[4] - 0.5 * raw[7];
    matrix[5] = numeratorY2 - 0.5 * denominator2;
    matrix[6] = raw[6];
    matrix[7] = raw[7];
    matrix[8] = denominator2;
    for (const double value : matrix)
    {
        if (!std::isfinite(value))
        {
            return false;
        }
    }
    return true;
}

/// One channel's plane inside a whole-image buffer.
///
/// @tparam SampleT the sample type, const for a read-only plane.
/// @param base the image's first byte.
/// @param layout where the image's samples sit.
/// @param channel which channel to describe.
/// @return the plane view, or an empty optional when the channel is not describable.
template <typename SampleT>
[[nodiscard]] std::optional<PlaneView<SampleT>> channelPlane(void* base, const ImageLayout& layout,
                                                             const std::uint32_t channel)
{
    auto* bytes = static_cast<std::uint8_t*>(base) + static_cast<std::uint64_t>(channel) * layout.planeStrideBytes();
    const std::uint32_t width = static_cast<std::uint32_t>(layout.extent().width());
    const std::uint32_t height = static_cast<std::uint32_t>(layout.extent().height());
    const std::uint64_t rowElements = layout.rowStrideBytes() / sizeof(std::remove_const_t<SampleT>);
    const std::size_t spanned =
        static_cast<std::size_t>((static_cast<std::uint64_t>(height) - 1u) * rowElements + width);
    const std::span<SampleT> samples{reinterpret_cast<SampleT*>(bytes), spanned};
    return PlaneView<SampleT>::create(samples, width, height, 1u, layout.rowStrideBytes());
}

} // namespace

const char* registrationCanvasModeName(const RegistrationCanvasMode mode) noexcept
{
    switch (mode)
    {
    case RegistrationCanvasMode::Reference:
        return "Reference";
    case RegistrationCanvasMode::Union:
        return "Union";
    case RegistrationCanvasMode::Intersection:
        return "Intersection";
    }
    return "UnknownRegistrationCanvasMode";
}

const char* registrationSamplingRouteName(const RegistrationSamplingRoute route) noexcept
{
    switch (route)
    {
    case RegistrationSamplingRoute::DirectRectangle:
        return "DirectRectangle";
    case RegistrationSamplingRoute::Warp:
        return "Warp";
    }
    return "UnknownRegistrationSamplingRoute";
}

Result<RegistrationCanvas> planRegistrationCanvas(std::span<const RegistrationBounds> bounds,
                                                  const std::uint32_t referenceIndex, const RegistrationCanvasMode mode)
{
    if (bounds.empty())
    {
        return planFailure(ErrorCode::InvalidArgument, "a canvas is planned from at least one frame's rectangle",
                           kCanvasOperation);
    }
    if (!isRegistrationCanvasMode(mode))
    {
        return planFailure(ErrorCode::InvalidArgument, "a canvas names a mode this build implements", kCanvasOperation);
    }
    if (referenceIndex >= bounds.size())
    {
        return planFailure(ErrorCode::InvalidArgument, "a canvas's reference index names a frame in the set",
                           kCanvasOperation);
    }
    for (const RegistrationBounds& rectangle : bounds)
    {
        if (!isUsableBounds(rectangle))
        {
            return planFailure(ErrorCode::InvalidArgument,
                               "a frame's rectangle is finite and its maxima are not below its minima",
                               kCanvasOperation);
        }
    }

    RegistrationBounds selected = bounds[referenceIndex];
    if (mode == RegistrationCanvasMode::Union)
    {
        for (const RegistrationBounds& rectangle : bounds)
        {
            selected.minimumX = (std::min)(selected.minimumX, rectangle.minimumX);
            selected.minimumY = (std::min)(selected.minimumY, rectangle.minimumY);
            selected.maximumX = (std::max)(selected.maximumX, rectangle.maximumX);
            selected.maximumY = (std::max)(selected.maximumY, rectangle.maximumY);
        }
    }
    else if (mode == RegistrationCanvasMode::Intersection)
    {
        for (const RegistrationBounds& rectangle : bounds)
        {
            selected.minimumX = (std::max)(selected.minimumX, rectangle.minimumX);
            selected.minimumY = (std::max)(selected.minimumY, rectangle.minimumY);
            selected.maximumX = (std::min)(selected.maximumX, rectangle.maximumX);
            selected.maximumY = (std::min)(selected.maximumY, rectangle.maximumY);
        }
    }

    if (selected.width() <= 0.0 || selected.height() <= 0.0)
    {
        return planFailure(ErrorCode::EmptySelection, "the selected frames share no part of the plane",
                           kCanvasOperation);
    }
    // A union or reference canvas rounds up so no mapped sample falls outside it; an intersection
    // rounds down, because a region only some frames cover is not part of their intersection.
    const double width =
        mode == RegistrationCanvasMode::Intersection ? std::floor(selected.width()) : std::ceil(selected.width());
    const double height =
        mode == RegistrationCanvasMode::Intersection ? std::floor(selected.height()) : std::ceil(selected.height());
    if (width > static_cast<double>(kMaximumRegistrationCanvasExtent) ||
        height > static_cast<double>(kMaximumRegistrationCanvasExtent))
    {
        return Status::failure(
            ErrorCode::LimitExceeded,
            StatusDetail{
                .message = "the planned canvas exceeds this domain's extent ceiling",
                .operation = kCanvasOperation,
                .domain = ErrorDomain::Algorithms,
                .context = {StatusContextField::ofCount(ContextKey::Requested,
                                                        static_cast<std::uint64_t>((std::max)(width, height))),
                            StatusContextField::ofCount(ContextKey::Limit, kMaximumRegistrationCanvasExtent)}});
    }

    return RegistrationCanvas{.boundingBox = selected,
                              .origin = {.x = -selected.minimumX, .y = -selected.minimumY},
                              .width = (std::max)(1u, static_cast<std::uint32_t>(width)),
                              .height = (std::max)(1u, static_cast<std::uint32_t>(height))};
}

RegistrationPlacement resolveRegistrationPlacement(const RegistrationTransform& transform,
                                                   const PixelCoordinate& canvasOrigin,
                                                   const SipDistortion* sourceDistortion) noexcept
{
    RegistrationPlacement placement;
    if (!transform.isLinear() || transform.linearKind() == RegistrationTransformKind::Projective ||
        correctsAnything(sourceDistortion) || !isFinitePixel(canvasOrigin))
    {
        return placement;
    }
    const std::array<double, 9>& matrix = transform.matrix();
    if (matrix[0] != 1.0 || matrix[1] != 0.0 || matrix[3] != 0.0 || matrix[4] != 1.0)
    {
        return placement;
    }
    const double offsetX = matrix[2] + canvasOrigin.x;
    const double offsetY = matrix[5] + canvasOrigin.y;
    if (!isWholePixelOffset(offsetX) || !isWholePixelOffset(offsetY))
    {
        return placement;
    }
    placement.route = RegistrationSamplingRoute::DirectRectangle;
    placement.offsetX = static_cast<std::int64_t>(offsetX);
    placement.offsetY = static_cast<std::int64_t>(offsetY);
    return placement;
}

Result<RegistrationOverlapReport> findRegistrationOverlaps(const ExecutionContext& execution,
                                                           std::span<const RegistrationFootprint> footprints,
                                                           const RegistrationCanvas& canvas,
                                                           const RegistrationOverlapRequest& request,
                                                           std::span<RegistrationOverlap> overlaps)
{
    if (!execution.valid())
    {
        return planFailure(ErrorCode::NotInitialized,
                           "overlap discovery needs an execution context that holds a lease on an engine context",
                           kOverlapOperation);
    }
    if (footprints.size() > kMaxRegistrationOverlapFrames)
    {
        return Status::failure(
            ErrorCode::LimitExceeded,
            StatusDetail{.message = "an overlap search exceeds this domain's frame ceiling",
                         .operation = kOverlapOperation,
                         .domain = ErrorDomain::Algorithms,
                         .context = {StatusContextField::ofCount(ContextKey::Requested, footprints.size()),
                                     StatusContextField::ofCount(ContextKey::Limit, kMaxRegistrationOverlapFrames)}});
    }
    if (!std::isfinite(request.minimumOverlapFraction) || request.minimumOverlapFraction < 0.0 ||
        request.minimumOverlapFraction > 1.0)
    {
        return planFailure(ErrorCode::InvalidArgument, "an overlap fraction is between zero and one",
                           kOverlapOperation);
    }
    if (canvas.width == 0u || canvas.height == 0u || !isFinitePixel(canvas.origin))
    {
        return planFailure(ErrorCode::InvalidArgument, "an overlap search needs a canvas with an extent",
                           kOverlapOperation);
    }
    for (const RegistrationFootprint& footprint : footprints)
    {
        if (!isUsableBounds(footprint.bounds) || footprint.framePixels == 0u)
        {
            return planFailure(ErrorCode::InvalidArgument,
                               "a footprint states a usable rectangle and a frame that has pixels", kOverlapOperation);
        }
    }

    RegistrationOverlapReport report;
    for (std::size_t first = 0u; first + 1u < footprints.size(); ++first)
    {
        if (const Status running = execution.checkCancellation(); running.failed())
        {
            return running;
        }
        for (std::size_t second = first + 1u; second < footprints.size(); ++second)
        {
            ++report.pairsExamined;
            const RegistrationBounds& left = footprints[first].bounds;
            const RegistrationBounds& right = footprints[second].bounds;
            const double minimumX = (std::max)(left.minimumX, right.minimumX) + canvas.origin.x;
            const double minimumY = (std::max)(left.minimumY, right.minimumY) + canvas.origin.y;
            const double maximumX = (std::min)(left.maximumX, right.maximumX) + canvas.origin.x;
            const double maximumY = (std::min)(left.maximumY, right.maximumY) + canvas.origin.y;
            if (maximumX <= minimumX || maximumY <= minimumY)
            {
                continue;
            }
            const double clampedMinimumX = (std::max)(0.0, std::floor(minimumX));
            const double clampedMinimumY = (std::max)(0.0, std::floor(minimumY));
            const double clampedMaximumX = (std::min)(static_cast<double>(canvas.width), std::ceil(maximumX));
            const double clampedMaximumY = (std::min)(static_cast<double>(canvas.height), std::ceil(maximumY));
            if (clampedMaximumX <= clampedMinimumX || clampedMaximumY <= clampedMinimumY)
            {
                continue;
            }

            const RegistrationRegion region{.x = static_cast<std::uint32_t>(clampedMinimumX),
                                            .y = static_cast<std::uint32_t>(clampedMinimumY),
                                            .width = static_cast<std::uint32_t>(clampedMaximumX - clampedMinimumX),
                                            .height = static_cast<std::uint32_t>(clampedMaximumY - clampedMinimumY)};
            const std::uint64_t smaller = (std::min)(footprints[first].framePixels, footprints[second].framePixels);
            const std::uint64_t relative =
                static_cast<std::uint64_t>(std::ceil(request.minimumOverlapFraction * static_cast<double>(smaller)));
            if (region.pixelCount() < (std::max)(request.minimumOverlapPixels, relative))
            {
                continue;
            }

            if (report.pairsAccepted < overlaps.size())
            {
                overlaps[static_cast<std::size_t>(report.pairsAccepted)] =
                    RegistrationOverlap{.frameA = static_cast<std::uint32_t>(first),
                                        .frameB = static_cast<std::uint32_t>(second),
                                        .region = region};
                ++report.pairsWritten;
            }
            else
            {
                report.truncated = true;
            }
            ++report.pairsAccepted;
        }
    }
    return report;
}

Result<RegistrationMapReport> computeRegistrationDestinationToSourceMap(const ExecutionContext& execution,
                                                                        const RegistrationTransform& transform,
                                                                        const SipDistortion* sourceDistortion,
                                                                        const RegistrationMapRequest& request)
{
    return fillMap(execution, transform, sourceDistortion, request, true, kDestinationMapOperation);
}

Result<RegistrationMapReport> computeRegistrationSourceToDestinationMap(const ExecutionContext& execution,
                                                                        const RegistrationTransform& transform,
                                                                        const SipDistortion* sourceDistortion,
                                                                        const RegistrationMapRequest& request)
{
    return fillMap(execution, transform, sourceDistortion, request, false, kSourceMapOperation);
}

RegistrationWarpPlan::RegistrationWarpPlan(const RegistrationSamplingRoute route,
                                           const ImageDescriptor& sourceDescriptor,
                                           const ImageDescriptor& destinationDescriptor,
                                           const RegistrationTransform& transform, const SipDistortion& distortion,
                                           const RegistrationWarpRequest& request) noexcept
    : m_route(route), m_sourceDescriptor(sourceDescriptor), m_destinationDescriptor(destinationDescriptor),
      m_transform(transform), m_distortion(distortion), m_request(request)
{
}

RegistrationSamplingRoute RegistrationWarpPlan::route() const noexcept
{
    return m_route;
}

bool RegistrationWarpPlan::needsCoordinateMap() const noexcept
{
    return m_route == RegistrationSamplingRoute::Warp &&
           (!m_transform.isLinear() || m_distortion.hasForwardModel() || m_distortion.hasInverseModel());
}

const ImageDescriptor& RegistrationWarpPlan::sourceDescriptor() const noexcept
{
    return m_sourceDescriptor;
}

const ImageDescriptor& RegistrationWarpPlan::destinationDescriptor() const noexcept
{
    return m_destinationDescriptor;
}

const RegistrationTransform& RegistrationWarpPlan::transform() const noexcept
{
    return m_transform;
}

const SipDistortion& RegistrationWarpPlan::distortion() const noexcept
{
    return m_distortion;
}

const RegistrationWarpRequest& RegistrationWarpPlan::request() const noexcept
{
    return m_request;
}

Result<RegistrationWarpPlan> planRegistrationWarp(const ImageDescriptor& source, const RegistrationTransform& transform,
                                                  const SipDistortion& distortion,
                                                  const RegistrationWarpRequest& request)
{
    if (request.width == 0u || request.height == 0u)
    {
        return planFailure(ErrorCode::InvalidArgument, "a warp writes at least one pixel", kPlanOperation);
    }
    if (!isRegistrationInverseSeed(request.seed))
    {
        return planFailure(ErrorCode::InvalidArgument, "a warp names a seed policy this build has", kPlanOperation);
    }
    if (!isKernelInterpolation(request.sampling.interpolation) || !isKernelBorderMode(request.sampling.border) ||
        !isSamplingInvalidPolicy(request.sampling.invalid))
    {
        return planFailure(ErrorCode::InvalidArgument, "a warp names a sampling policy this build has", kPlanOperation);
    }
    if (!isFinitePixel(request.canvasOrigin))
    {
        return planFailure(ErrorCode::InvalidArgument, "a warp's canvas offset is finite", kPlanOperation);
    }
    if (source.cfa().pattern() != CfaPattern::None)
    {
        return planFailure(ErrorCode::IncompatibleArguments,
                           "resampling an unresolved colour filter array would average different filters",
                           kPlanOperation);
    }

    ImageDescriptorRequest destinationRequest = source.toRequest();
    const std::optional<ImageExtent> extent =
        ImageExtent::create(request.width, request.height, source.extent().channelCount(), defaultEngineLimits());
    if (!extent.has_value())
    {
        return planFailure(ErrorCode::LimitExceeded, "the planned canvas exceeds the image model's extent limits",
                           kPlanOperation);
    }
    destinationRequest.extent = *extent;
    const std::optional<ImageDescriptor> destination = ImageDescriptor::create(destinationRequest);
    if (!destination.has_value())
    {
        return planFailure(ErrorCode::InvalidArgument, "the canvas description is not one this engine can hold",
                           kPlanOperation);
    }

    const RegistrationPlacement placement = resolveRegistrationPlacement(
        transform, request.canvasOrigin,
        distortion.hasForwardModel() || distortion.hasInverseModel() ? &distortion : nullptr);
    return RegistrationWarpPlan{placement.route, source, *destination, transform, distortion, request};
}

Result<RegistrationWarpResult> warpRegisteredImage(const ExecutionContext& execution, const SampleType sampleType,
                                                   const void* source, const ImageLayout& sourceLayout,
                                                   void* destination, const ImageLayout& destinationLayout,
                                                   const RegistrationWarpPlan& plan,
                                                   const KernelValidityView& selection,
                                                   const ValidityMaskView<std::uint8_t>& destinationValidity)
{
    if (!execution.valid())
    {
        return planFailure(ErrorCode::NotInitialized,
                           "a warp needs an execution context that holds a lease on an engine context", kWarpOperation);
    }
    if (source == nullptr || destination == nullptr)
    {
        return planFailure(ErrorCode::InvalidArgument, "a warp reads and writes samples that exist", kWarpOperation);
    }
    if (const Status stopped = execution.checkCancellation(); stopped.failed())
    {
        return stopped;
    }
    if (sampleType != plan.sourceDescriptor().sampleType())
    {
        return planFailure(ErrorCode::IncompatibleArguments,
                           "a warp's representation is the one its plan was built for", kWarpOperation);
    }
    const std::uint32_t elementBytes = sampleTypeSizeBytes(sampleType);
    if (elementBytes == 0u || sourceLayout.elementSizeBytes() != elementBytes ||
        destinationLayout.elementSizeBytes() != elementBytes ||
        plan.destinationDescriptor().sampleType() != sampleType ||
        sourceLayout.extent() != plan.sourceDescriptor().extent() ||
        destinationLayout.extent() != plan.destinationDescriptor().extent())
    {
        return planFailure(ErrorCode::IncompatibleArguments, "a warp's layouts agree with its plan's descriptors",
                           kWarpOperation);
    }
    if (reinterpret_cast<std::uintptr_t>(source) % elementBytes != 0u ||
        reinterpret_cast<std::uintptr_t>(destination) % elementBytes != 0u)
    {
        return planFailure(ErrorCode::InvalidArgument, "warp buffers must be aligned for their samples",
                           kWarpOperation);
    }
    const auto sourceAddress = reinterpret_cast<std::uintptr_t>(source);
    const auto destinationAddress = reinterpret_cast<std::uintptr_t>(destination);
    if (sourceAddress <= destinationAddress ? destinationAddress - sourceAddress < sourceLayout.totalBytes()
                                            : sourceAddress - destinationAddress < destinationLayout.totalBytes())
    {
        return planFailure(ErrorCode::Aliasing, "a registration canvas must be disjoint from its source",
                           kWarpOperation);
    }
    if (const Status sampling = reference_detail::checkSamplingParameters(plan.request().sampling, kWarpOperation);
        sampling.failed())
    {
        return sampling;
    }
    if (const Status validity = reference_detail::checkValidity(selection, sourceLayout.extent().width(),
                                                                sourceLayout.extent().height(), kWarpOperation);
        validity.failed())
    {
        return validity;
    }
    if (selection.maskPlane != 0u || (selection.mask != nullptr && !selection.mask->isShared() &&
                                      selection.mask->planeCount() != sourceLayout.extent().channelCount()))
    {
        return planFailure(ErrorCode::IncompatibleArguments,
                           "whole-image validity has one shared plane or one per channel", kWarpOperation);
    }
    if (destinationValidity.width() != plan.destinationDescriptor().extent().width() ||
        destinationValidity.height() != plan.destinationDescriptor().extent().height() ||
        (!destinationValidity.isShared() &&
         destinationValidity.planeCount() != destinationLayout.extent().channelCount()))
    {
        return planFailure(ErrorCode::IncompatibleArguments, "a warp's output mask covers its canvas", kWarpOperation);
    }

    const auto overlaps = [](const void* left, std::uint64_t leftBytes, const void* right, std::uint64_t rightBytes) {
        const auto a = reinterpret_cast<std::uintptr_t>(left);
        const auto b = reinterpret_cast<std::uintptr_t>(right);
        return a <= b ? b - a < leftBytes : a - b < rightBytes;
    };
    if (overlaps(destinationValidity.data(), destinationValidity.spannedByteCount(), source,
                 sourceLayout.totalBytes()) ||
        overlaps(destinationValidity.data(), destinationValidity.spannedByteCount(), destination,
                 destinationLayout.totalBytes()) ||
        (selection.mask != nullptr && (overlaps(selection.mask->data(), selection.mask->spannedByteCount(),
                                                destinationValidity.data(), destinationValidity.spannedByteCount()) ||
                                       overlaps(selection.mask->data(), selection.mask->spannedByteCount(), destination,
                                                destinationLayout.totalBytes()))))
    {
        return planFailure(ErrorCode::Aliasing, "writable warp storage must be disjoint from every other buffer",
                           kWarpOperation);
    }

    const std::uint32_t width = static_cast<std::uint32_t>(destinationLayout.extent().width());
    const std::uint32_t height = static_cast<std::uint32_t>(destinationLayout.extent().height());
    const std::uint32_t channels = static_cast<std::uint32_t>(destinationLayout.extent().channelCount());
    const auto checkedPlan =
        planRegistrationWarp(plan.sourceDescriptor(), plan.transform(), plan.distortion(), plan.request());
    if (!checkedPlan.hasValue())
    {
        return checkedPlan.status();
    }
    if (checkedPlan->route() != plan.route() || checkedPlan->destinationDescriptor() != plan.destinationDescriptor())
    {
        return planFailure(ErrorCode::IncompatibleArguments, "a warp plan must agree with its checked request",
                           kWarpOperation);
    }
    RegistrationWarpResult result;
    result.route = plan.route();
    result.channelCount = channels;
    result.plannedWorkers = (std::max)(1u, execution.admittedConcurrency());

    // A whole-pixel translation copies each admitted source sample to its canvas position on this
    // thread; canvas pixels no source sample lands on keep their samples and are marked invalid.
    if (plan.route() == RegistrationSamplingRoute::DirectRectangle)
    {
        const RegistrationPlacement placement = resolveRegistrationPlacement(
            plan.transform(), plan.request().canvasOrigin,
            plan.distortion().hasForwardModel() || plan.distortion().hasInverseModel() ? &plan.distortion() : nullptr);
        destinationValidity.fill(false);
        const std::uint32_t sourceWidth = static_cast<std::uint32_t>(sourceLayout.extent().width());
        const std::uint32_t sourceHeight = static_cast<std::uint32_t>(sourceLayout.extent().height());
        const std::uint64_t elementSize = sourceLayout.elementSizeBytes();
        Status copied;
        (void)dispatchSampleType(sampleType, [&]<typename SampleT>() {
            for (std::uint32_t channel = 0u; channel < channels; ++channel)
            {
                const auto* sourceBase = static_cast<const std::uint8_t*>(source) +
                                         plane_detail::channelRowByteOffset(sourceLayout, channel, 0u);
                auto* destinationBase = static_cast<std::uint8_t*>(destination) +
                                        plane_detail::channelRowByteOffset(destinationLayout, channel, 0u);
                for (std::uint32_t row = 0u; row < sourceHeight; ++row)
                {
                    if (const Status stopped = execution.checkCancellation(); stopped.failed())
                    {
                        copied = stopped;
                        return;
                    }
                    const std::int64_t canvasRow = static_cast<std::int64_t>(row) + placement.offsetY;
                    if (canvasRow < 0 || canvasRow >= static_cast<std::int64_t>(height))
                    {
                        continue;
                    }
                    for (std::uint32_t column = 0u; column < sourceWidth; ++column)
                    {
                        if (column % 4096u == 0u)
                        {
                            if (const Status stopped = execution.checkCancellation(); stopped.failed())
                            {
                                copied = stopped;
                                return;
                            }
                        }
                        const std::int64_t canvasColumn = static_cast<std::int64_t>(column) + placement.offsetX;
                        if (canvasColumn < 0 || canvasColumn >= static_cast<std::int64_t>(width))
                        {
                            continue;
                        }
                        const auto* sample =
                            sourceBase + static_cast<std::uint64_t>(row) * sourceLayout.rowStrideBytes() +
                            static_cast<std::uint64_t>(column) * elementSize * sourceLayout.samplesPerPixel();
                        const bool admitted =
                            sampleIsValidByValue(selection.descriptor, *reinterpret_cast<const SampleT*>(sample)) &&
                            (selection.mask == nullptr ||
                             selection.mask->test(selection.mask->isShared() ? 0u : channel, column, row).value());
                        // A shared output mask is set by the first channel and only ever cleared by
                        // a later one, so a pixel is valid when every channel was admitted.
                        const std::uint32_t plane = destinationValidity.isShared() ? 0u : channel;
                        if (channel == 0u || !destinationValidity.isShared() || !admitted)
                        {
                            (void)destinationValidity.setValid(plane, static_cast<std::uint32_t>(canvasColumn),
                                                               static_cast<std::uint32_t>(canvasRow), admitted);
                        }
                        if (!admitted)
                        {
                            continue;
                        }
                        std::memcpy(destinationBase +
                                        static_cast<std::uint64_t>(canvasRow) * destinationLayout.rowStrideBytes() +
                                        static_cast<std::uint64_t>(canvasColumn) * elementSize *
                                            destinationLayout.samplesPerPixel(),
                                    sample, static_cast<std::size_t>(elementSize));
                        ++result.run.outputsWritten;
                    }
                }
            }
        });
        if (copied.failed())
        {
            return copied;
        }
        destinationValidity.clearPadBits();
        result.plannedWorkers = 1u;
        result.unmappedSamples = static_cast<std::uint64_t>(width) * height * channels - result.run.outputsWritten;
        result.run.selection =
            ValidityCount{.valid = result.run.outputsWritten,
                          .total = static_cast<std::uint64_t>(sourceWidth) * sourceHeight * channels};
        return result;
    }

    // A linear transform with no distortion is one matrix, which the geometry module warps with its
    // own tiling and validity rules.
    if (!plan.needsCoordinateMap())
    {
        std::array<double, 9> matrix{};
        if (!buildGeometryMatrix(plan.transform(), plan.request().canvasOrigin, matrix))
        {
            return planFailure(ErrorCode::InvalidData,
                               "this transform has no inverse, so no destination pixel has "
                               "a source position",
                               kWarpOperation);
        }
        const ProjectiveWarpRequest warpRequest{.width = width,
                                                .height = height,
                                                .destinationToSource = matrix,
                                                .destinationPixelAspectRatio =
                                                    plan.destinationDescriptor().pixelAspectRatio(),
                                                .sampling = plan.request().sampling};
        const Result<GeometryPlan> geometry = planProjectiveWarp(plan.sourceDescriptor(), warpRequest);
        if (!geometry.hasValue())
        {
            return geometry.status();
        }
        const GeometrySelection geometrySelection{.validity = selection.descriptor, .mask = selection.mask};
        // Only where the frame reaches: the geometry module's warp would otherwise write the whole
        // canvas, the border rule standing in for what lies off the frame.
        const Result<GeometryResult> warped =
            transformImageGeometry(execution, sampleType, source, sourceLayout, destination, destinationLayout,
                                   *geometry, geometrySelection, destinationValidity, WarpCoverage::SourcePixels);
        if (!warped.hasValue())
        {
            return warped.status();
        }
        result.run = warped->run;
        result.plannedWorkers = warped->plannedWorkers;
        result.workingBytes = warped->workingBytes;
        result.unmappedSamples = static_cast<std::uint64_t>(width) * height * channels - warped->run.outputsWritten;
        return result;
    }

    if (sampleType != SampleType::Float32)
    {
        return planFailure(ErrorCode::Unsupported,
                           "a coordinate-map warp reads through this engine's interpolating gather, which takes "
                           "single-precision samples; convert the frame first",
                           kWarpOperation);
    }

    registration_internal::ExecutionMemory memory(*execution.memoryResource());
    // One band row costs its coordinates, the two planes the map fills, and the samples the gather
    // returns. All four are counted, because a ceiling that counts some of them is not a ceiling.
    const std::uint64_t bytesPerRow =
        static_cast<std::uint64_t>(width) * (sizeof(KernelCoordinate) + 3u * sizeof(float));
    const std::uint32_t bandRows = static_cast<std::uint32_t>(
        (std::clamp<std::uint64_t>)(bytesPerRow == 0u ? 1u : kMaximumWarpScratchBytes / bytesPerRow, 1u,
                                    static_cast<std::uint64_t>(height)));

    try
    {
        std::pmr::vector<KernelCoordinate> coordinates(&memory);
        std::pmr::vector<float> gathered(&memory);
        coordinates.resize(static_cast<std::size_t>(bandRows) * width);
        gathered.resize(static_cast<std::size_t>(bandRows) * width);
        std::pmr::vector<float> bandX(&memory);
        std::pmr::vector<float> bandY(&memory);
        bandX.resize(static_cast<std::size_t>(bandRows) * width);
        bandY.resize(static_cast<std::size_t>(bandRows) * width);
        destinationValidity.fill(false);
        const std::uint32_t frameWidth = static_cast<std::uint32_t>(sourceLayout.extent().width());
        const std::uint32_t frameHeight = static_cast<std::uint32_t>(sourceLayout.extent().height());

        for (std::uint32_t bandBegin = 0u; bandBegin < height; bandBegin += bandRows)
        {
            const std::uint32_t rows = (std::min)(bandRows, height - bandBegin);
            RegistrationMapRequest mapRequest;
            mapRequest.width = width;
            mapRequest.height = rows;
            mapRequest.xMap = std::span<float>(bandX).first(static_cast<std::size_t>(rows) * width);
            mapRequest.yMap = std::span<float>(bandY).first(static_cast<std::size_t>(rows) * width);
            mapRequest.postTranslation = plan.request().canvasOrigin;
            mapRequest.seed = plan.request().seed;
            // The band's grid counts its own rows from zero, so the canvas row it stands for is
            // restored by moving the translation down by the band's first row rather than by
            // mapping positions the band does not hold. A destination centre `r + 0.5` in the band
            // then inverts the canvas position `bandBegin + r + 0.5`, which is the row it is.
            mapRequest.postTranslation.y -= static_cast<double>(bandBegin);
            const Result<RegistrationMapReport> mapped = computeRegistrationDestinationToSourceMap(
                execution, plan.transform(),
                plan.distortion().hasForwardModel() || plan.distortion().hasInverseModel() ? &plan.distortion()
                                                                                           : nullptr,
                mapRequest);
            if (!mapped.hasValue())
            {
                return mapped.status();
            }
            result.mapping.mapped += mapped->mapped;
            result.mapping.unmapped += mapped->unmapped;
            result.mapping.newtonIterations += mapped->newtonIterations;
            result.mapping.warmSeedAccepted += mapped->warmSeedAccepted;
            result.mapping.warmSeedRejected += mapped->warmSeedRejected;
            result.mapping.maximumResidualPixels =
                (std::max)(result.mapping.maximumResidualPixels, mapped->maximumResidualPixels);

            const std::size_t bandSamples = static_cast<std::size_t>(rows) * width;
            for (std::size_t index = 0u; index < bandSamples; ++index)
            {
                // The gather puts pixel centres at integers and this domain puts the first centre
                // at one half, so the half pixel between the two conventions is removed here, once.
                // An unmapped position is given a harmless in-range coordinate so the gather's
                // checks accept the band; its output is discarded below. A position off the frame's
                // pixels is marked unmapped with it: the warp writes only where the frame reaches,
                // whatever the border rule would read there.
                coordinates[index] = KernelCoordinate{.x = static_cast<double>(bandX[index]) - 0.5,
                                                      .y = static_cast<double>(bandY[index]) - 0.5};
                if (isUnmappedCoordinate(bandX[index]) || isUnmappedCoordinate(bandY[index]) ||
                    !liesOnPlanePixels(coordinates[index].x, coordinates[index].y, frameWidth, frameHeight))
                {
                    bandX[index] = unmappedCoordinate();
                    coordinates[index] = KernelCoordinate{.x = 0.0, .y = 0.0};
                }
            }

            for (std::uint32_t channel = 0u; channel < channels; ++channel)
            {
                KernelValidityView channelSelection = selection;
                channelSelection.maskPlane = selection.mask != nullptr && !selection.mask->isShared() ? channel : 0u;
                std::fill_n(gathered.begin(), bandSamples, std::numeric_limits<float>::quiet_NaN());
                if (sourceLayout.samplesPerPixel() == 1u)
                {
                    const auto sourcePlane =
                        channelPlane<const float>(const_cast<void*>(source), sourceLayout, channel);
                    if (!sourcePlane.has_value())
                    {
                        return planFailure(ErrorCode::InvalidArgument,
                                           "a warp channel could not be described as a plane", kWarpOperation);
                    }
                    const auto run = gatherSamples(
                        execution, *sourcePlane, std::span<const KernelCoordinate>(coordinates).first(bandSamples),
                        std::span<float>(gathered).first(bandSamples), plan.request().sampling, channelSelection);
                    if (!run.hasValue())
                    {
                        return run.status();
                    }
                }
                else
                {
                    const auto coordinateSpan = std::span<const KernelCoordinate>(coordinates).first(bandSamples);
                    if (const auto checked =
                            reference_detail::checkCoordinates(execution, coordinateSpan, kWarpOperation);
                        checked.failed())
                    {
                        return checked;
                    }
                    const auto& sampling = plan.request().sampling;
                    const reference_detail::SamplingPlan samplingPlan{
                        .samples = reinterpret_cast<const float*>(
                            static_cast<const std::uint8_t*>(source) +
                            plane_detail::channelRowByteOffset(sourceLayout, channel, 0u)),
                        .rowStride = sourceLayout.rowStrideBytes() / sizeof(float),
                        .width = sourceLayout.extent().width(),
                        .height = sourceLayout.extent().height(),
                        .interpolation = sampling.interpolation,
                        .border = sampling.border,
                        .invalid = sampling.invalid,
                        .constantBorder = sampling.constantBorder,
                        .invalidSample = sampling.invalidSample,
                        .validity = &channelSelection,
                        .acceptsEverySample = selection.descriptor.rule() == ValidityRule::AllValid,
                        .pixelStride = sourceLayout.samplesPerPixel()};
                    const auto ran = parallelFor(
                        execution, bandSamples, {.bytesPerItem = 20u, .operationsPerItem = 64u},
                        [&](const TaskRange range) -> Status {
                            for (auto index = range.begin; index < range.end; ++index)
                            {
                                if (index % 4096u == 0u)
                                {
                                    if (const auto stopped = execution.checkCancellation(); stopped.failed())
                                    {
                                        return stopped;
                                    }
                                }
                                (void)reference_detail::sampleInterpolated<true>(samplingPlan, coordinates[index],
                                                                                 gathered[index]);
                            }
                            return Status::success();
                        });
                    if (ran.failed())
                    {
                        return ran;
                    }
                }

                auto* destinationBase = static_cast<std::uint8_t*>(destination) +
                                        plane_detail::channelRowByteOffset(destinationLayout, channel, 0u);
                for (std::uint32_t row = 0u; row < rows; ++row)
                {
                    auto* destinationRow =
                        reinterpret_cast<float*>(destinationBase + static_cast<std::uint64_t>(bandBegin + row) *
                                                                       destinationLayout.rowStrideBytes());
                    for (std::uint32_t column = 0u; column < width; ++column)
                    {
                        const std::size_t index = static_cast<std::size_t>(row) * width + column;
                        const bool positioned =
                            !isUnmappedCoordinate(bandX[index]) && !isUnmappedCoordinate(bandY[index]);
                        const bool written = positioned && std::isfinite(gathered[index]);
                        // As on the direct route, a shared mask is valid only where every channel
                        // was written, and an unwritten destination sample is left as it was.
                        const std::uint32_t plane = destinationValidity.isShared() ? 0u : channel;
                        if (channel == 0u || !destinationValidity.isShared() || !written)
                        {
                            (void)destinationValidity.setValid(plane, column, bandBegin + row, written);
                        }
                        if (!written)
                        {
                            ++result.unmappedSamples;
                            continue;
                        }
                        destinationRow[static_cast<std::size_t>(column) * destinationLayout.samplesPerPixel()] =
                            gathered[index];
                        ++result.run.outputsWritten;
                    }
                }
            }
        }
        destinationValidity.clearPadBits();
        result.workingBytes = memory.peakBytes();
        result.run.selection = ValidityCount{.valid = result.run.outputsWritten,
                                             .total = static_cast<std::uint64_t>(width) * height * channels};
        return result;
    }
    catch (const registration_internal::ContextAllocationFailure& refused)
    {
        return refused.status();
    }
}

} // namespace an
