// AfterNightEngine - coordinate maps: the checked view, map generation over the execution's
// tiles, and the square drop's corner interpolation, one at a time and a row at a time.
//
// Generation takes one allocation for both coordinate planes and the per-row bounds, and runs the
// tiles on the operation's workers; every tile writes only its own coordinates, so the map does not
// depend on the worker count. There are three routes through the composition: a linear transform
// without source SIP is evaluated a row at a time with the row's products shared, its divisions a
// run of columns at a time and its row bounds recorded as the row is written; a curved
// transform is mapped a run of columns at a time through the registration domain's batch entry;
// and everything else is mapped point by point, through the public operations on the reference
// backend and through the prepared SIP terms otherwise.
//
// The corner interpolation keeps the arithmetic of `interpolate_four_points`, adapted from Siril's
// routine of the same name (https://siril.org, `siril/src/drizzle/cdrizzlemap.c`): the 3x3 weighting, the four
// sums and the Float32 tap order. Siril's routine is the single-precision form of the STScI drizzle
// library's routine of that name (https://github.com/spacetelescope/drizzle, `drizzle/src/cdrizzlemap.c`), and
// the file that carries it in Siril states the drizzle code's notice -
// `Copyright (C) 2011,2014 Association of Universities for Research in Astronomy (AURA)`, under the
// three-clause BSD terms (BSD-3-Clause) - and that the code as integrated into Siril is modified from
// the original AURA code by team free-astro; Siril is distributed under the GNU General Public License,
// version 3 or any later version. This file is part of that derived work: it is distributed under the
// GPL with AURA's notice kept, and the notice in full is in
// `src/AfterNightEngine/THIRD_PARTY_NOTICES.md`. The checked views, the halo checks, the border cells
// and the typed failures around it are this engine's. The provenance note is
// `docs/dev/DRIZZLE_CORNER_INTERPOLATION_PROVENANCE.md`.

// SPDX-License-Identifier: GPL-3.0-or-later AND BSD-3-Clause
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Algorithms/Drizzle/DrizzlePixmap.h>

#include <AfterNightEngine/Core/CheckedSize.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/ParallelExecutor.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>
#include <AfterNightEngine/Runtime/TraceSink.h>

#include "algorithms/drizzle/DrizzleGeometryInternal.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <memory>
#include <span>
#include <utility>

namespace an
{
namespace
{

using drizzle_detail::failure;
constexpr const char* kGenerate = "an::generateDrizzlePixmap";
constexpr const char* kInterpolate = "an::interpolateDrizzlePoint";
constexpr std::uint64_t kCoordinateAlignment = 64u;

bool representable(const PixelCoordinate& point) noexcept
{
    constexpr double kMaximum = (std::numeric_limits<float>::max)();
    return std::isfinite(point.x) && std::isfinite(point.y) && std::abs(point.x) <= kMaximum &&
           std::abs(point.y) <= kMaximum;
}

// The linear row kernel consumes a checked matrix directly. The public point composition remains
// the scalar oracle; nonlinear registration stays at its owning domain's public boundary.
bool mapLinear(const RegistrationTransform& transform, const PixelCoordinate& translation,
               PixelCoordinate& point) noexcept
{
    const auto& matrix = transform.matrix();
    const double denominator = transform.linearKind() == RegistrationTransformKind::Projective
                                   ? matrix[6] * point.x + matrix[7] * point.y + matrix[8]
                                   : 1.0;
    if (!std::isfinite(denominator) || std::abs(denominator) < kRegistrationSingularDeterminant)
        return false;
    const double x = (matrix[0] * point.x + matrix[1] * point.y + matrix[2]) / denominator;
    const double y = (matrix[3] * point.x + matrix[4] * point.y + matrix[5]) / denominator;
    point = {x + translation.x, y + translation.y};
    // The following positive scale cannot recover a non-finite value. Check the final pair once.
    return true;
}

/// The least and greatest destination row a run of mapped columns reached, empty until one does.
struct RowSpan
{
    float low = std::numeric_limits<float>::infinity(); ///< Least finite vertical coordinate written.
    float high = -std::numeric_limits<float>::infinity(); ///< Greatest finite vertical coordinate written.
};

// With no source SIP, each row shares its vertical products. Keep the constant additions
// separate so the operation order remains identical to the scalar point oracle. A run of columns
// is mapped in two loops. The first holds only the arithmetic, into locals the stores cannot
// alias, with no branch and no select, so the compiler takes several columns per instruction:
// every operation is still the oracle's, in its order, and a vector division rounds as the scalar
// one does. The second chooses the holes, narrows, and widens the row's span over the positions it
// writes, in column order. A denominator that is singular or not finite, and a position no Float32
// holds, are the holes the checked composition refuses. A run's first centre plus a column index
// is the column's centre exactly, because both are integers and halves far inside a double.
template <bool Projective>
std::uint64_t mapLinearRow(const RegistrationTransform& transform, const PixelCoordinate& translation, double scale,
                           double sourceY, std::uint32_t xBegin, std::uint32_t xEnd, float* xMap, float* yMap,
                           RowSpan& span) noexcept
{
    constexpr std::int32_t kRun = 256;
    const auto& matrix = transform.matrix();
    const double m0 = matrix[0];
    const double m2 = matrix[2];
    const double m3 = matrix[3];
    const double m5 = matrix[5];
    const double m6 = matrix[6];
    const double m8 = matrix[8];
    const double rowX = matrix[1] * sourceY;
    const double rowY = matrix[4] * sourceY;
    const double rowDenominator = Projective ? matrix[7] * sourceY : 0.0;
    const double translationX = translation.x;
    const double translationY = translation.y;
    constexpr double kMaximumDenominator = (std::numeric_limits<double>::max)();
    constexpr double kMaximumCoordinate = (std::numeric_limits<float>::max)();
    constexpr float kHole = std::numeric_limits<float>::quiet_NaN();
    alignas(64) std::array<double, kRun> denominators;
    alignas(64) std::array<double, kRun> xs;
    alignas(64) std::array<double, kRun> ys;
    float low = span.low;
    float high = span.high;
    std::uint64_t unmapped = 0u;
    for (std::uint32_t first = xBegin; first < xEnd; first += static_cast<std::uint32_t>(kRun))
    {
        const std::int32_t count =
            static_cast<std::int32_t>((std::min)(static_cast<std::uint32_t>(kRun), xEnd - first));
        const double firstCentre = static_cast<double>(first) + 0.5;
        double* const runDenominators = denominators.data();
        double* const runX = xs.data();
        double* const runY = ys.data();
#if defined(__clang__) && !defined(_MSC_VER)
#pragma clang loop vectorize(assume_safety) interleave(enable)
#elif defined(_MSC_VER) && !defined(__clang__)
#pragma loop(ivdep)
#endif
        for (std::int32_t index = 0; index < count; ++index)
        {
            const double sourceX = firstCentre + static_cast<double>(index);
            const double denominator = Projective ? m6 * sourceX + rowDenominator + m8 : 1.0;
            runDenominators[index] = denominator;
            runX[index] = ((m0 * sourceX + rowX + m2) / denominator + translationX) * scale - 0.5;
            runY[index] = ((m3 * sourceX + rowY + m5) / denominator + translationY) * scale - 0.5;
        }
        float* const xOut = xMap + (first - xBegin);
        float* const yOut = yMap + (first - xBegin);
        for (std::int32_t index = 0; index < count; ++index)
        {
            const double magnitude = std::abs(runDenominators[index]);
            const double x = runX[index];
            const double y = runY[index];
            if (magnitude >= kRegistrationSingularDeterminant && magnitude <= kMaximumDenominator &&
                std::abs(x) <= kMaximumCoordinate && std::abs(y) <= kMaximumCoordinate)
            {
                const float destinationY = static_cast<float>(y);
                xOut[index] = static_cast<float>(x);
                yOut[index] = destinationY;
                low = (std::min)(low, destinationY);
                high = (std::max)(high, destinationY);
            }
            else
            {
                xOut[index] = kHole;
                yOut[index] = kHole;
                ++unmapped;
            }
        }
    }
    span = {low, high};
    return unmapped;
}

} // namespace

Result<DrizzlePixmapView> DrizzlePixmapView::create(const ImageExtent& source, const ImageExtent& output,
                                                    std::uint32_t storedBegin, std::uint32_t storedEnd,
                                                    std::uint32_t coreBegin, std::uint32_t coreEnd,
                                                    std::span<const float> xMap, std::span<const float> yMap,
                                                    std::span<const float> rowBounds)
{
    constexpr const char* kOperation = "an::DrizzlePixmapView::create";
    if (storedBegin >= storedEnd || storedEnd > source.height() || coreBegin < storedBegin || coreBegin >= coreEnd ||
        coreEnd > storedEnd)
        return failure(ErrorCode::OutOfRange, "core and stored row intervals must nest inside the source", kOperation);
    const CheckedSize elements = CheckedSize{source.width()} * (storedEnd - storedBegin);
    const CheckedSize bytes = elements * sizeof(float);
    if (!bytes.toOffset().has_value())
        return failure(ErrorCode::LimitExceeded, "coordinate spans exceed representable offsets", kOperation);
    if (xMap.size() != *elements.value() || yMap.size() != *elements.value())
        return failure(ErrorCode::IncompatibleArguments, "both coordinate spans must match the stored rows",
                       kOperation);
    const auto xAddress = reinterpret_cast<std::uintptr_t>(xMap.data());
    const auto yAddress = reinterpret_cast<std::uintptr_t>(yMap.data());
    if (xMap.data() == nullptr || yMap.data() == nullptr || xAddress % alignof(float) != 0u ||
        yAddress % alignof(float) != 0u)
        return failure(ErrorCode::InvalidArgument, "coordinate spans must be non-null and aligned for float",
                       kOperation);
    // Address differences avoid pointer ordering across unrelated allocations and endpoint overflow.
    const auto distance = xAddress > yAddress ? xAddress - yAddress : yAddress - xAddress;
    if (distance < *bytes.value())
        return failure(ErrorCode::Aliasing, "coordinate planes must be disjoint", kOperation);
    if (!rowBounds.empty())
    {
        if (rowBounds.size() != 2u * static_cast<std::size_t>(storedEnd - storedBegin))
            return failure(ErrorCode::IncompatibleArguments, "row bounds must hold two values per stored row",
                           kOperation);
        if (rowBounds.data() == nullptr || reinterpret_cast<std::uintptr_t>(rowBounds.data()) % alignof(float) != 0u)
            return failure(ErrorCode::InvalidArgument, "row bounds must be non-null and aligned for float", kOperation);
        for (std::size_t row = 0u; row < rowBounds.size(); row += 2u)
        {
            const float low = rowBounds[row], high = rowBounds[row + 1u];
            const bool empty =
                low == std::numeric_limits<float>::infinity() && high == -std::numeric_limits<float>::infinity();
            if (!empty && !(std::isfinite(low) && std::isfinite(high) && low <= high))
                return failure(ErrorCode::InvalidData,
                               "a row bound is a finite ordered pair or the empty span of infinities", kOperation);
        }
    }
    return DrizzlePixmapView{source, output, storedBegin, storedEnd, coreBegin, coreEnd, xMap, yMap, rowBounds};
}

Result<DrizzleRowBounds> DrizzlePixmapView::rowBounds(std::uint32_t sourceRow) const
{
    constexpr const char* kOperation = "an::DrizzlePixmapView::rowBounds";
    if (!containsRow(sourceRow))
        return failure(ErrorCode::OutOfRange, "the source row is not stored", kOperation);
    if (m_rowBounds.empty())
        return failure(ErrorCode::Unsupported, "this view carries no row bounds", kOperation);
    return DrizzleGeometryAccess::rowBoundsUnchecked(*this, sourceRow);
}

DrizzleRowBounds DrizzleGeometryAccess::rowBoundsUnchecked(const DrizzlePixmapView& map, std::uint32_t row) noexcept
{
    const auto offset = static_cast<std::size_t>(row - map.m_storedBegin) * 2u;
    return DrizzleRowBounds{.low = map.m_rowBounds[offset], .high = map.m_rowBounds[offset + 1u]};
}

Result<DrizzleCoordinateRow> DrizzlePixmapView::row(std::uint32_t sourceRow) const
{
    if (!containsRow(sourceRow))
        return failure(ErrorCode::OutOfRange, "the source row is not stored", "an::DrizzlePixmapView::row");
    const auto offset = static_cast<std::size_t>(sourceRow - m_storedBegin) * m_source.width();
    return DrizzleCoordinateRow{m_xMap.subspan(offset, m_source.width()), m_yMap.subspan(offset, m_source.width())};
}

DrizzleCoordinateRow DrizzleGeometryAccess::rowUnchecked(const DrizzlePixmapView& map, std::uint32_t row) noexcept
{
    const auto offset = static_cast<std::size_t>(row - map.m_storedBegin) * map.m_source.width();
    return {map.m_xMap.subspan(offset, map.m_source.width()), map.m_yMap.subspan(offset, map.m_source.width())};
}

bool DrizzleGeometryAccess::overlaps(const DrizzlePixmapView& map, const void* data, std::size_t bytes) noexcept
{
    if (bytes == 0u)
        return false;
    const auto address = reinterpret_cast<std::uintptr_t>(data);
    for (const auto plane : {map.m_xMap, map.m_yMap, map.m_rowBounds})
    {
        if (plane.empty())
            continue;
        const auto source = reinterpret_cast<std::uintptr_t>(plane.data());
        if (address <= source ? source - address < bytes : address - source < plane.size_bytes())
            return true;
    }
    return false;
}

Result<PixelCoordinate> DrizzlePixmapView::at(std::uint32_t x, std::uint32_t y) const
{
    if (x >= m_source.width() || !containsRow(y))
        return failure(ErrorCode::OutOfRange, "the source coordinate is not stored", "an::DrizzlePixmapView::at");
    const auto offset = static_cast<std::size_t>(y - m_storedBegin) * m_source.width() + x;
    return PixelCoordinate{m_xMap[offset], m_yMap[offset]};
}

class DrizzlePixmap::State
{
public:
    State(const ExecutionContext& execution, const DrizzlePixmapView& coordinates, float* buffer,
          std::uint64_t bytes) noexcept
        : lease(EngineContextState::retain(execution)), memory(execution.memoryResource()), view(coordinates),
          data(buffer), coordinateBytes(bytes)
    {
    }
    ~State()
    {
        memory->deallocate(data, coordinateBytes, kCoordinateAlignment);
    }
    EngineContextLease lease;
    MemoryResource* memory;
    DrizzlePixmapView view;
    float* data;
    std::uint64_t coordinateBytes;
    DrizzlePixmapReport report{};
};

DrizzlePixmap::~DrizzlePixmap()
{
    drizzle_detail::destroyState(m_state);
}
DrizzlePixmap::DrizzlePixmap(DrizzlePixmap&& other) noexcept : m_state(std::exchange(other.m_state, nullptr)) {}
DrizzlePixmap& DrizzlePixmap::operator=(DrizzlePixmap&& other) noexcept
{
    if (this != &other)
    {
        drizzle_detail::destroyState(m_state);
        m_state = std::exchange(other.m_state, nullptr);
    }
    return *this;
}
Result<DrizzlePixmapView> DrizzlePixmap::view() const
{
    if (m_state == nullptr)
        return failure(ErrorCode::NotInitialized, "the pixmap is empty", "an::DrizzlePixmap::view");
    return m_state->view;
}
std::uint64_t DrizzlePixmap::coordinateBytes() const noexcept
{
    return m_state != nullptr ? m_state->coordinateBytes : 0u;
}
std::uint64_t DrizzlePixmap::residentBytes() const noexcept
{
    return m_state != nullptr ? m_state->coordinateBytes + sizeof(State) : 0u;
}
std::uint64_t DrizzlePixmap::controlBytes() noexcept
{
    return sizeof(State);
}
DrizzlePixmapReport DrizzlePixmap::report() const noexcept
{
    return m_state != nullptr ? m_state->report : DrizzlePixmapReport{};
}

Result<DrizzlePixmap> DrizzleGeometryAccess::generate(const ExecutionContext& execution,
                                                      const DrizzleGeometryPlan& plan,
                                                      const DrizzlePixmapRequest& request)
{
    const auto* prepared = state(plan);
    if (prepared == nullptr)
        return failure(ErrorCode::NotInitialized, "the geometry plan is empty", kGenerate);
    const auto& descriptor = prepared->descriptor;
    if (const auto checked = drizzle_detail::checkGeometry(execution, descriptor); checked.failed())
        return checked;
    const bool dense = request.storage == DrizzleMapStorage::Dense;
    if ((!dense && request.storage != DrizzleMapStorage::BoundedRows) || request.haloRows > 1u ||
        (dense && (request.firstRow != 0u || request.haloRows != 0u)) ||
        (!dense && (request.rowCount == 0u || request.rowCount > 256u)))
        return failure(ErrorCode::InvalidArgument, "invalid map storage, row count or halo", kGenerate);
    const auto& source = descriptor.sourceExtent();
    if (request.firstRow >= source.height())
        return failure(ErrorCode::OutOfRange, "first map row lies outside the source", kGenerate);
    const auto coreBegin = dense ? 0u : request.firstRow;
    const auto coreEnd =
        dense ? source.height() : coreBegin + (std::min)(request.rowCount, source.height() - coreBegin);
    const auto storedBegin = coreBegin - (std::min)(coreBegin, request.haloRows);
    const auto storedEnd = coreEnd + (std::min)(request.haloRows, source.height() - coreEnd);
    const auto rows = storedEnd - storedBegin;
    const CheckedSize elements = CheckedSize{source.width()} * rows;
    const CheckedSize boundsValues = CheckedSize{rows} * 2u;
    const CheckedSize payload = (elements * 2u + boundsValues) * sizeof(float);
    if (!payload.toOffset().has_value() || *payload.value() > execution.limits().maxImageBytes)
        return failure(ErrorCode::LimitExceeded, "coordinate map exceeds byte or pointer-offset limits", kGenerate);

    const bool reference = execution.backend() == ComputeBackend::Reference;
    if (execution.backendFallback() == BackendFallback::Forbidden &&
        (execution.backend() == ComputeBackend::Gpu || (execution.instructionSet() != InstructionSet::Automatic &&
                                                        execution.instructionSet() != InstructionSet::Scalar)))
        return failure(ErrorCode::UnsupportedBackend, "drizzle geometry has scalar CPU and reference implementations",
                       kGenerate);

    ParallelTileRequest tileRequest;
    tileRequest.width = source.width();
    tileRequest.height = rows;
    tileRequest.tileWidth = (std::min)(source.width(), 4096u);
    tileRequest.tileHeight = (std::min)(8u, 4096u / tileRequest.tileWidth);
    tileRequest.pixelCost = {.bytesPerItem = 8u, .operationsPerItem = prepared->sipOrder > 0u ? 128u : 32u};
    auto tiles = planParallelTiles(execution, tileRequest);
    if (!tiles.hasValue())
        return tiles.status();
    auto memory = execution.memoryResource()->allocate(*payload.value(), kCoordinateAlignment);
    if (!memory.hasValue())
        return memory.status();
    float* const xMap = static_cast<float*>(*memory);
    float* const yMap = xMap + *elements.toOffset();
    float* const bounds = yMap + *elements.toOffset();
    // The bounds are validated by the view, so they must already be well-formed: every row starts
    // as the empty span, and the pass below narrows the rows that reach somewhere.
    for (std::size_t index = 0u; index < *boundsValues.toAllocationSize(); index += 2u)
    {
        bounds[index] = std::numeric_limits<float>::infinity();
        bounds[index + 1u] = -std::numeric_limits<float>::infinity();
    }
    auto coordinates =
        DrizzlePixmapView::create(source, descriptor.outputExtent(), storedBegin, storedEnd, coreBegin, coreEnd,
                                  {xMap, *elements.toAllocationSize()}, {yMap, *elements.toAllocationSize()},
                                  {bounds, *boundsValues.toAllocationSize()});
    if (!coordinates.hasValue())
    {
        execution.memoryResource()->deallocate(xMap, *payload.value(), kCoordinateAlignment);
        return coordinates.status();
    }
    auto control = execution.memoryResource()->allocate(sizeof(DrizzlePixmap::State), alignof(DrizzlePixmap::State));
    if (!control.hasValue())
    {
        execution.memoryResource()->deallocate(xMap, *payload.value(), kCoordinateAlignment);
        return control.status();
    }
    DrizzlePixmap result{std::construct_at(static_cast<DrizzlePixmap::State*>(*control), execution, *coordinates, xMap,
                                           *payload.value())};
    std::atomic<std::uint64_t> mapped{0u}, unmapped{0u}, sipCount{0u}, polynomialCount{0u};
    const auto& mapping = descriptor.mapping();
    const bool preparedLinear = !reference && prepared->sipOrder == 0u && mapping.transform.isLinear();
    // A tile that spans the full width records its rows' destination bounds while the row is still
    // in cache; a source wider than one tile records them in a pass of their own below.
    const bool boundsInTile = tiles->tileWidth >= source.width();
    const auto recordRowBounds = [&](std::uint32_t row) noexcept {
        const float* const values = yMap + static_cast<std::size_t>(row) * source.width();
        float low = std::numeric_limits<float>::infinity(), high = -low;
        for (std::uint32_t x = 0u; x < source.width(); ++x)
        {
            const float value = values[x];
            if (std::isfinite(value))
            {
                low = (std::min)(low, value);
                high = (std::max)(high, value);
            }
        }
        bounds[2u * static_cast<std::size_t>(row)] = low;
        bounds[2u * static_cast<std::size_t>(row) + 1u] = high;
    };
    const auto body = [&](const ParallelTile& tile) -> Status {
        DrizzlePixmapReport tally;
        if (preparedLinear)
        {
            for (std::uint32_t row = tile.yBegin; row < tile.yEnd; ++row)
            {
                const auto offset = static_cast<std::size_t>(row) * source.width() + tile.xBegin;
                const double sourceY = static_cast<double>(storedBegin + row) + 0.5;
                RowSpan span;
                tally.unmapped +=
                    mapping.transform.linearKind() == RegistrationTransformKind::Projective
                        ? mapLinearRow<true>(mapping.transform, mapping.postTranslation, descriptor.scale(), sourceY,
                                             tile.xBegin, tile.xEnd, xMap + offset, yMap + offset, span)
                        : mapLinearRow<false>(mapping.transform, mapping.postTranslation, descriptor.scale(), sourceY,
                                              tile.xBegin, tile.xEnd, xMap + offset, yMap + offset, span);
                // A tile as wide as the source wrote the whole row, in column order, so the span it
                // widened is the row's bounds as a scan of the row would find them.
                if (boundsInTile)
                {
                    bounds[2u * static_cast<std::size_t>(row)] = span.low;
                    bounds[2u * static_cast<std::size_t>(row) + 1u] = span.high;
                }
            }
            tally.mapped =
                static_cast<std::uint64_t>(tile.xEnd - tile.xBegin) * (tile.yEnd - tile.yBegin) - tally.unmapped;
        }
        else if (!reference && !mapping.transform.isLinear())
        {
            // A curved transform is mapped a run of columns at a time through the registration
            // domain's batch entry point, which checks the transform once and takes each position's
            // arithmetic in the single position's order. The source SIP runs first per column; a
            // column it cannot correct enters the run as a non-finite position and leaves as a hole.
            constexpr std::uint32_t kRun = 256u;
            std::array<PixelCoordinate, kRun> run;
            for (std::uint32_t row = tile.yBegin; row < tile.yEnd; ++row)
            {
                const double sourceY = static_cast<double>(storedBegin + row) + 0.5;
                for (std::uint32_t first = tile.xBegin; first < tile.xEnd; first += kRun)
                {
                    const std::uint32_t count = (std::min)(kRun, tile.xEnd - first);
                    std::uint64_t corrected = count;
                    for (std::uint32_t column = 0u; column < count; ++column)
                    {
                        PixelCoordinate point{static_cast<double>(first + column) + 0.5, sourceY};
                        if (prepared->sipOrder > 0u && !drizzle_detail::applyPreparedSip(*prepared, point))
                        {
                            point = {std::numeric_limits<double>::quiet_NaN(),
                                     std::numeric_limits<double>::quiet_NaN()};
                            --corrected;
                        }
                        run[column] = point;
                    }
                    if (prepared->sipOrder > 0u)
                        tally.sipEvaluations += count;
                    tally.polynomialEvaluations += corrected;
                    (void)mapRegistrationPoints(mapping.transform, mapping.postTranslation,
                                                std::span<PixelCoordinate>(run.data(), count));
                    const auto offset = static_cast<std::size_t>(row) * source.width() + first;
                    for (std::uint32_t column = 0u; column < count; ++column)
                    {
                        const PixelCoordinate point{run[column].x * descriptor.scale() - 0.5,
                                                    run[column].y * descriptor.scale() - 0.5};
                        if (representable(point))
                        {
                            xMap[offset + column] = static_cast<float>(point.x);
                            yMap[offset + column] = static_cast<float>(point.y);
                            ++tally.mapped;
                        }
                        else
                        {
                            xMap[offset + column] = yMap[offset + column] = std::numeric_limits<float>::quiet_NaN();
                            ++tally.unmapped;
                        }
                    }
                }
            }
        }
        else
        {
            for (std::uint32_t row = tile.yBegin; row < tile.yEnd; ++row)
                for (std::uint32_t x = tile.xBegin; x < tile.xEnd; ++x)
                {
                    PixelCoordinate point{static_cast<double>(x) + 0.5, static_cast<double>(storedBegin + row) + 0.5};
                    bool valid = true;
                    if (prepared->sipOrder > 0u)
                    {
                        ++tally.sipEvaluations;
                        if (reference)
                        {
                            auto corrected = applySipForward(*mapping.sourceDistortion, point);
                            valid = corrected.hasValue();
                            if (valid)
                                point = *corrected;
                        }
                        else
                            valid = drizzle_detail::applyPreparedSip(*prepared, point);
                    }
                    if (valid)
                    {
                        if (!mapping.transform.isLinear())
                            ++tally.polynomialEvaluations;
                        if (reference || !mapping.transform.isLinear())
                        {
                            auto registered = mapRegistrationPoint(mapping.transform, point, mapping.postTranslation);
                            valid = registered.hasValue();
                            if (valid)
                                point = *registered;
                        }
                        else
                            valid = mapLinear(mapping.transform, mapping.postTranslation, point);
                    }
                    if (valid)
                    {
                        point.x = point.x * descriptor.scale() - 0.5;
                        point.y = point.y * descriptor.scale() - 0.5;
                        valid = representable(point);
                    }
                    const auto index = static_cast<std::size_t>(row) * source.width() + x;
                    if (valid)
                    {
                        xMap[index] = static_cast<float>(point.x);
                        yMap[index] = static_cast<float>(point.y);
                        ++tally.mapped;
                    }
                    else
                    {
                        xMap[index] = yMap[index] = std::numeric_limits<float>::quiet_NaN();
                        ++tally.unmapped;
                    }
                }
        }
        if (boundsInTile && !preparedLinear)
            for (std::uint32_t row = tile.yBegin; row < tile.yEnd; ++row)
                recordRowBounds(row);
        mapped.fetch_add(tally.mapped, std::memory_order_relaxed);
        unmapped.fetch_add(tally.unmapped, std::memory_order_relaxed);
        sipCount.fetch_add(tally.sipEvaluations, std::memory_order_relaxed);
        polynomialCount.fetch_add(tally.polynomialEvaluations, std::memory_order_relaxed);
        execution.reportTrace(TraceEventKind::Counter, "drizzle", "coordinateTilePairs",
                              static_cast<double>(tally.mapped + tally.unmapped), "pairs");
        return Status::success();
    };
    if (const auto ran = executeParallelTiles(execution, *tiles, {}, body); ran.failed())
        return ran;
    // A source wider than one tile records its per-row destination bounds in one pass over the
    // vertical plane, so that the accumulation never re-reads it per destination strip.
    if (!boundsInTile)
    {
        const auto recordBounds = [&](TaskRange range) -> Status {
            for (std::uint64_t row = range.begin; row < range.end; ++row)
                recordRowBounds(static_cast<std::uint32_t>(row));
            return execution.checkCancellation();
        };
        if (const auto recorded = parallelFor(execution, rows,
                                              {static_cast<std::uint64_t>(source.width()) * sizeof(float),
                                               static_cast<std::uint64_t>(source.width()) * 2u},
                                              recordBounds);
            recorded.failed())
            return recorded;
    }
    if (const auto stopped = execution.checkCancellation(); stopped.failed())
        return stopped;
    result.m_state->report = {.mapped = mapped.load(),
                              .unmapped = unmapped.load(),
                              .sipEvaluations = sipCount.load(),
                              .polynomialEvaluations = polynomialCount.load(),
                              .workers = tiles->plannedWorkers,
                              .implementation = reference ? DrizzleMapImplementation::Reference
                                                          : DrizzleMapImplementation::PreparedScalar};
    return result;
}

Result<DrizzlePixmap> generateDrizzlePixmap(const ExecutionContext& execution, const DrizzleGeometryPlan& plan,
                                            const DrizzlePixmapRequest& request)
{
    return DrizzleGeometryAccess::generate(execution, plan, request);
}

namespace
{

struct FootprintWeights
{
    float centre;
    float side;
    float corner;
};

FootprintWeights footprintWeights(double halfSize) noexcept
{
    const float h = static_cast<float>(halfSize);
    const float complement = 1.0f - h;
    return {complement * complement, complement * h, h * h};
}

// The corner interpolation's shared 3x3 Float32 weighting and tap order. The caller proves three
// contiguous source rows and the destination's disjointness once.
#if defined(_MSC_VER)
__forceinline
#else
__attribute__((always_inline)) inline
#endif
    bool interiorFootprint(const float* xMap, const float* yMap, std::size_t stride, const FootprintWeights& weights,
                           DrizzleFootprint& corners) noexcept
{
    std::array<float, 9u> horizontal, vertical;
    for (std::size_t row = 0u; row < 3u; ++row)
        for (std::size_t column = 0u; column < 3u; ++column)
        {
            const float weight = row == 1u ? (column == 1u ? weights.centre : weights.side)
                                           : (column == 1u ? weights.side : weights.corner);
            horizontal[row * 3u + column] = xMap[row * stride + column] * weight;
            vertical[row * 3u + column] = yMap[row * stride + column] * weight;
        }
    const auto sum = [&](std::size_t a, std::size_t b, std::size_t c, std::size_t d) {
        return DrizzleCoordinate{horizontal[a] + horizontal[b] + horizontal[c] + horizontal[d],
                                 vertical[a] + vertical[b] + vertical[c] + vertical[d]};
    };
    corners = {sum(3u, 4u, 6u, 7u), sum(4u, 5u, 7u, 8u), sum(1u, 2u, 4u, 5u), sum(0u, 1u, 3u, 4u)};
    // Every tap participates, even at zero weight: IEEE propagation also checks all taps.
    for (const auto& point : corners)
        if (!std::isfinite(point.x) || !std::isfinite(point.y))
            return false;
    return true;
}

Result<PixelCoordinate> interpolateCell(const DrizzlePixmapView& map, const PixelCoordinate& source, std::uint32_t x,
                                        std::uint32_t y)
{
    if (!map.containsRow(y) || !map.containsRow(y + 1u))
        return failure(ErrorCode::OutOfRange, "an interpolation row is not stored", kInterpolate);
    const auto row0 = DrizzleGeometryAccess::rowUnchecked(map, y);
    const auto row1 = DrizzleGeometryAccess::rowUnchecked(map, y + 1u);
    const float fx = static_cast<float>(source.x - x);
    const float fy = static_cast<float>(source.y - y);
    const std::array<float, 4u> weights{(1.0f - fx) * (1.0f - fy), fx * (1.0f - fy), (1.0f - fx) * fy, fx * fy};
    const auto interpolate = [&](std::span<const float> top, std::span<const float> bottom) {
        return weights[0] * top[x] + weights[1] * top[x + 1u] + weights[2] * bottom[x] + weights[3] * bottom[x + 1u];
    };
    for (const auto& row : {row0, row1})
        for (const auto& plane : {row.x, row.y})
            if (!std::isfinite(plane[x]) || !std::isfinite(plane[x + 1u]))
                return failure(ErrorCode::InvalidData, "an interpolation tap is not finite", kInterpolate);
    const PixelCoordinate result{interpolate(row0.x, row1.x), interpolate(row0.y, row1.y)};
    if (!representable(result))
        return failure(ErrorCode::InvalidData, "interpolated coordinates overflowed", kInterpolate);
    return result;
}

} // namespace

bool DrizzleGeometryAccess::cornersUnchecked(const DrizzlePixmapView& map, std::uint32_t x, std::uint32_t y,
                                             double halfSize, DrizzleFootprint& corners) noexcept
{
    const auto width = map.sourceExtent().width(), height = map.sourceExtent().height();
    if (x > 0u && y > 0u && x + 1u < width && y + 1u < height)
    {
        const auto row = rowUnchecked(map, y - 1u);
        return interiorFootprint(row.x.data() + x - 1u, row.y.data() + x - 1u, width, footprintWeights(halfSize),
                                 corners);
    }
    const std::array<PixelCoordinate, 4u> offsets{
        {{-halfSize, halfSize}, {halfSize, halfSize}, {halfSize, -halfSize}, {-halfSize, -halfSize}}};
    for (std::size_t i = 0u; i < 4u; ++i)
    {
        const auto cx = (std::min)(x - (offsets[i].x < 0.0 && x > 0u ? 1u : 0u), width - 2u);
        const auto cy = (std::min)(y - (offsets[i].y < 0.0 && y > 0u ? 1u : 0u), height - 2u);
        const auto top = rowUnchecked(map, cy), bottom = rowUnchecked(map, cy + 1u);
        const float fx = static_cast<float>(static_cast<double>(x) + offsets[i].x - cx);
        const float fy = static_cast<float>(static_cast<double>(y) + offsets[i].y - cy);
        const std::array<float, 4u> weights{(1.0f - fx) * (1.0f - fy), fx * (1.0f - fy), (1.0f - fx) * fy, fx * fy};
        const auto value = [&](std::span<const float> a, std::span<const float> b) {
            return weights[0] * a[cx] + weights[1] * a[cx + 1u] + weights[2] * b[cx] + weights[3] * b[cx + 1u];
        };
        corners[i] = {value(top.x, bottom.x), value(top.y, bottom.y)};
        if (!std::isfinite(corners[i].x) || !std::isfinite(corners[i].y))
            return false;
    }
    return true;
}

Result<PixelCoordinate> interpolateDrizzlePoint(const DrizzlePixmapView& map, const PixelCoordinate& source)
{
    if (!std::isfinite(source.x) || !std::isfinite(source.y))
        return failure(ErrorCode::InvalidArgument, "interpolation coordinates must be finite", kInterpolate);
    const auto width = map.sourceExtent().width();
    const auto height = map.sourceExtent().height();
    if (width < 2u || height < 2u)
        return failure(ErrorCode::InvalidArgument, "bilinear map interpolation requires at least two rows and columns",
                       kInterpolate);
    if (source.x < -1.0 || source.y < -1.0 || source.x > width || source.y > height)
        return failure(ErrorCode::OutOfRange, "interpolation exceeds the one-pixel extrapolation band", kInterpolate);
    const auto x = static_cast<std::uint32_t>(std::clamp(std::floor(source.x), 0.0, static_cast<double>(width - 2u)));
    const auto y = static_cast<std::uint32_t>(std::clamp(std::floor(source.y), 0.0, static_cast<double>(height - 2u)));
    return interpolateCell(map, source, x, y);
}

Result<std::array<DrizzleCoordinate, 4u>> interpolateDrizzleCorners(const DrizzlePixmapView& map, std::uint32_t x,
                                                                    std::uint32_t y, double halfSize)
{
    if (!std::isfinite(halfSize) || halfSize <= 0.0 || halfSize > 1.0)
        return failure(ErrorCode::InvalidArgument, "drop half-size must be finite in (0,1]",
                       "an::interpolateDrizzleCorners");
    if (x >= map.sourceExtent().width() || y < map.coreRowBegin() || y >= map.coreRowEnd())
        return failure(ErrorCode::OutOfRange, "drop centre must lie in the core source rows",
                       "an::interpolateDrizzleCorners");
    if (map.sourceExtent().width() < 2u || map.sourceExtent().height() < 2u)
        return failure(ErrorCode::InvalidArgument, "bilinear map interpolation requires at least two rows and columns",
                       "an::interpolateDrizzleCorners");
    if (x > 0u && y > 0u && x + 1u < map.sourceExtent().width() && y + 1u < map.sourceExtent().height())
    {
        if (!map.containsRow(y - 1u) || !map.containsRow(y + 1u))
            return failure(ErrorCode::OutOfRange, "the square footprint requires its halo rows",
                           "an::interpolateDrizzleCorners");
        const auto first = DrizzleGeometryAccess::rowUnchecked(map, y - 1u);
        const float* const xMap = first.x.data() + x - 1u;
        const float* const yMap = first.y.data() + x - 1u;
        const auto stride = map.sourceExtent().width();
        DrizzleFootprint corners;
        if (!interiorFootprint(xMap, yMap, stride, footprintWeights(halfSize), corners))
            return failure(ErrorCode::InvalidData, "a square footprint tap or result is not finite",
                           "an::interpolateDrizzleCorners");
        return corners;
    }
    const std::array<PixelCoordinate, 4u> offsets{
        {{-halfSize, halfSize}, {halfSize, halfSize}, {halfSize, -halfSize}, {-halfSize, -halfSize}}};
    std::array<DrizzleCoordinate, 4u> corners;
    for (std::size_t i = 0u; i < corners.size(); ++i)
    {
        // Select cells adjacent to the centre, including at exactly h=1. Flooring that corner
        // instead would ask for a second halo row even though its extra tap has zero weight.
        const auto cellX = (std::min)(x - (offsets[i].x < 0.0 && x > 0u ? 1u : 0u), map.sourceExtent().width() - 2u);
        const auto cellY = (std::min)(y - (offsets[i].y < 0.0 && y > 0u ? 1u : 0u), map.sourceExtent().height() - 2u);
        auto point = interpolateCell(
            map, {static_cast<double>(x) + offsets[i].x, static_cast<double>(y) + offsets[i].y}, cellX, cellY);
        if (!point.hasValue())
            return point.status();
        corners[i] = {static_cast<float>(point->x), static_cast<float>(point->y)};
    }
    return corners;
}

Result<DrizzleFootprintReport> interpolateDrizzleCornerRow(const ExecutionContext& execution,
                                                           const DrizzlePixmapView& map, std::uint32_t sourceRow,
                                                           std::uint32_t firstColumn, double halfSize,
                                                           std::span<DrizzleFootprint> output)
{
    constexpr const char* kOperation = "an::interpolateDrizzleCornerRow";
    if (!execution.valid())
        return failure(ErrorCode::NotInitialized, "footprint rows require an execution", kOperation);
    if (const auto stopped = execution.checkCancellation(); stopped.failed())
        return stopped;
    const auto width = map.sourceExtent().width(), height = map.sourceExtent().height();
    if (!std::isfinite(halfSize) || halfSize <= 0.0 || halfSize > 1.0 || width < 2u || height < 2u)
        return failure(ErrorCode::InvalidArgument, "footprints require a finite half-size in (0,1] and a 2x2 map",
                       kOperation);
    if (sourceRow < map.coreRowBegin() || sourceRow >= map.coreRowEnd() || firstColumn > width ||
        output.size() > width - firstColumn)
        return failure(ErrorCode::OutOfRange, "footprint output must fit the core source row", kOperation);
    for (const auto& extent : {map.sourceExtent(), map.outputExtent()})
        if (!ImageExtent::create(extent.width(), extent.height(), extent.channelCount(), execution.limits())
                 .has_value())
            return failure(ErrorCode::LimitExceeded, "footprint geometry exceeds execution limits", kOperation);
    if (execution.backendFallback() == BackendFallback::Forbidden &&
        (execution.backend() == ComputeBackend::Gpu || (execution.instructionSet() != InstructionSet::Automatic &&
                                                        execution.instructionSet() != InstructionSet::Scalar)))
        return failure(ErrorCode::UnsupportedBackend, "footprints have scalar CPU and reference implementations",
                       kOperation);
    if (output.empty())
        return DrizzleFootprintReport{};
    if (!map.containsRow(sourceRow == 0u ? 0u : sourceRow - 1u) ||
        !map.containsRow((std::min)(sourceRow + 1u, height - 1u)))
        return failure(ErrorCode::OutOfRange, "the footprint row requires its clipped halo", kOperation);
    if (DrizzleGeometryAccess::overlaps(map, output.data(), output.size_bytes()))
        return failure(ErrorCode::Aliasing, "footprint output overlaps coordinate planes", kOperation);
    const bool interiorRow = sourceRow > 0u && sourceRow + 1u < height;
    const auto first = DrizzleGeometryAccess::rowUnchecked(map, interiorRow ? sourceRow - 1u : sourceRow);
    const auto weights = footprintWeights(halfSize);
    const bool reference = execution.backend() == ComputeBackend::Reference;
    DrizzleFootprintReport report;
    const auto recordInvalid = [&](std::size_t i) {
        ++report.unmapped;
        for (auto& point : output[i])
            point = {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN()};
    };
    const auto checked = [&](std::size_t i) {
        const auto x = firstColumn + static_cast<std::uint32_t>(i);
        auto corners = interpolateDrizzleCorners(map, x, sourceRow, halfSize);
        if (corners.hasValue())
            output[i] = *corners;
        else
            recordInvalid(i);
    };
    for (std::size_t begin = 0u; begin < output.size(); begin += 4096u)
    {
        if (const auto stopped = execution.checkCancellation(); stopped.failed())
            return stopped;
        const auto end = (std::min)(begin + 4096u, output.size());
        if (!reference && interiorRow)
        {
            const auto interiorBegin = (std::max)(begin, firstColumn == 0u ? std::size_t{1u} : std::size_t{0u});
            const auto interiorEnd =
                (std::max)(interiorBegin, (std::min)(end, static_cast<std::size_t>(width - 1u - firstColumn)));
            for (std::size_t i = begin; i < interiorBegin; ++i)
                checked(i);
            for (std::size_t i = interiorBegin; i < interiorEnd; ++i)
            {
                const auto x = firstColumn + i;
                if (!interiorFootprint(first.x.data() + x - 1u, first.y.data() + x - 1u, width, weights, output[i]))
                    recordInvalid(i);
            }
            for (std::size_t i = interiorEnd; i < end; ++i)
                checked(i);
        }
        else
            for (std::size_t i = begin; i < end; ++i)
                checked(i);
        execution.reportTrace(TraceEventKind::Counter, "drizzle", "footprintTileSamples",
                              static_cast<double>(end - begin), "footprints");
    }
    report.mapped = output.size() - report.unmapped;
    if (const auto stopped = execution.checkCancellation(); stopped.failed())
        return stopped;
    return report;
}

} // namespace an
