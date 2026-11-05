// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Coordinate maps: two Float32 planes holding each source pixel centre's destination position,
/// generated in bounded row blocks or densely, and the square-drop corner interpolation read from
/// them.
///
/// `generateDrizzlePixmap` evaluates a prepared `DrizzleGeometryPlan` over a full-width block of
/// source rows, with an optional one-row halo, into an owned `DrizzlePixmap` whose memory is charged
/// to the executing context. It also records each stored row's vertical destination span, so the
/// accumulation can skip a row that cannot reach its strip without reading it. `DrizzlePixmapView`
/// is the checked borrow every consumer reads, and a caller holding its own planes can build one
/// directly. A position that cannot be mapped is a pair of quiet NaNs; a finite position off the
/// canvas is still a valid coordinate. A dense map is only ever explicitly requested; a refused
/// bounded map never falls back to one.
///
/// The square kernel's corners are not mapped through the transform. They are bilinear
/// interpolations of the mapped centres, extrapolating one pixel at the image edges, in Float32
/// arithmetic and a fixed tap order: the footprint calculation adapted from Siril's
/// `interpolate_four_points`, the single-precision form of the STScI drizzle library's routine of that
/// name, whose provenance `DrizzlePixmap.cpp` states.
#pragma once

#include <AfterNightEngine/Algorithms/Drizzle/DrizzleGeometry.h>
#include <AfterNightEngine/Algorithms/Drizzle/DrizzlePlanner.h>

#include <array>
#include <cstdint>
#include <limits>
#include <span>

namespace an
{

/// A full-width source band, with an explicitly selected dense alternative.
struct DrizzlePixmapRequest
{
    DrizzleMapStorage storage = DrizzleMapStorage::BoundedRows; ///< Dense is never an allocation fallback.
    std::uint32_t firstRow = 0u; ///< First core row in source coordinates; zero for Dense.
    std::uint32_t rowCount = 32u; ///< Maximum core rows, in [1,256], clipped at source end; ignored for Dense.
    std::uint32_t haloRows = 0u; ///< Zero or one, clipped at source edges; zero for Dense.
};

/// Borrowed coordinate spans for one source row; invalid pairs are paired quiet NaNs.
struct DrizzleCoordinateRow
{
    std::span<const float> x{}; ///< Horizontal destinations, in integer-centre output sampling pixels.
    std::span<const float> y{}; ///< Vertical destinations, with the same length and lifetime.
};

/// The vertical destination span of one stored source row: the least and greatest finite `y` its
/// coordinates reach. A row with no finite coordinate carries positive infinity above negative
/// infinity, so `low > high` is the empty span. Recorded once when a map is generated, so the
/// accumulation can ask whether a row reaches a destination strip without reading the row.
struct DrizzleRowBounds
{
    float low = std::numeric_limits<float>::infinity(); ///< Least finite destination row coordinate.
    float high = -std::numeric_limits<float>::infinity(); ///< Greatest finite destination row coordinate.
};

/// One mapped drop corner in the coordinate planes' native Float32 representation.
/// No widening and requantization is needed between footprint generation and accumulation.
struct DrizzleCoordinate
{
    float x = 0.0f; ///< Horizontal destination sampling coordinate, in output pixels.
    float y = 0.0f; ///< Vertical destination sampling coordinate, in output pixels.
    /// Compares the two stored coordinates exactly.
    /// @param left one coordinate pair.
    /// @param right the other coordinate pair.
    /// @return whether both components are equal.
    [[nodiscard]] friend constexpr bool operator==(const DrizzleCoordinate& left,
                                                   const DrizzleCoordinate& right) = default;
};

/// Four native corners in (-h,+h), (+h,+h), (+h,-h), (-h,-h) order.
using DrizzleFootprint = std::array<DrizzleCoordinate, 4u>;

/// Work performed by a successful footprint-row operation.
struct DrizzleFootprintReport
{
    std::uint64_t mapped = 0u; ///< Footprints whose eight components are finite.
    std::uint64_t unmapped = 0u; ///< Invalid footprints, written as eight quiet NaNs.
};

/// Immutable checked borrow of two full-width, tightly packed coordinate planes.
/// Construction checks sizes, alignment, disjointness and the stored and core intervals once, and
/// the shape of any row bounds, without reading a coordinate. The caller keeps both spans alive and
/// unchanged through every borrow. Concurrent reads are safe.
class DrizzlePixmapView
{
public:
    /// Validates external or generated planes; no allocation, conversion or sample scan.
    /// @param source the full source extent, including its original plane count.
    /// @param output the full output extent.
    /// @param storedBegin first stored source row, inclusive.
    /// @param storedEnd one past the last stored source row.
    /// @param coreBegin first requested source row, inclusive, within stored rows.
    /// @param coreEnd one past the last requested source row, within stored rows.
    /// @param xMap exactly source.width() times the stored row count samples.
    /// @param yMap a separate equally sized coordinate plane.
    /// @param rowBounds `low` then `high` per stored row as `DrizzleRowBounds` defines them, or
    ///        empty for a view that records none; the values are checked for shape and order and
    ///        trusted for content.
    /// @return a view; `ErrorCode::OutOfRange` when the core and stored intervals do not nest inside
    ///         the source; `ErrorCode::LimitExceeded` when the planes' byte size cannot be represented;
    ///         `ErrorCode::IncompatibleArguments` when a plane is not the stored rows' size or the
    ///         bounds do not hold two values per stored row; `ErrorCode::InvalidArgument` for a null
    ///         or misaligned plane or bounds span; `ErrorCode::Aliasing` for overlapping coordinate
    ///         planes; or `ErrorCode::InvalidData` for a bound that is neither a finite ordered pair
    ///         nor the empty span.
    [[nodiscard]] static AFTERNIGHTENGINE_API Result<DrizzlePixmapView>
    create(const ImageExtent& source, const ImageExtent& output, std::uint32_t storedBegin, std::uint32_t storedEnd,
           std::uint32_t coreBegin, std::uint32_t coreEnd, std::span<const float> xMap, std::span<const float> yMap,
           std::span<const float> rowBounds = {});
    /// Full source dimensions; maps contain one pair per pixel, regardless of source plane count.
    /// @return the source extent.
    [[nodiscard]] const ImageExtent& sourceExtent() const noexcept
    {
        return m_source;
    }
    /// Full destination dimensions; finite off-canvas coordinates remain valid.
    /// @return the output extent.
    [[nodiscard]] const ImageExtent& outputExtent() const noexcept
    {
        return m_output;
    }
    /// First stored row.
    /// @return the inclusive row index.
    [[nodiscard]] std::uint32_t storedRowBegin() const noexcept
    {
        return m_storedBegin;
    }
    /// End of stored rows.
    /// @return the exclusive row index.
    [[nodiscard]] std::uint32_t storedRowEnd() const noexcept
    {
        return m_storedEnd;
    }
    /// First core row.
    /// @return the inclusive row index.
    [[nodiscard]] std::uint32_t coreRowBegin() const noexcept
    {
        return m_coreBegin;
    }
    /// End of core rows.
    /// @return the exclusive row index.
    [[nodiscard]] std::uint32_t coreRowEnd() const noexcept
    {
        return m_coreEnd;
    }
    /// Whether a source row is stored, including halo rows.
    /// @param row the global source row.
    /// @return whether row is in the stored half-open interval.
    [[nodiscard]] bool containsRow(std::uint32_t row) const noexcept
    {
        return row >= m_storedBegin && row < m_storedEnd;
    }
    /// Whether storage covers the whole source.
    /// @return true when both source edges are stored.
    [[nodiscard]] bool isDense() const noexcept
    {
        return m_storedBegin == 0u && m_storedEnd == m_source.height();
    }
    /// Checks the row once, then returns contiguous spans for inner loops.
    /// @param sourceRow global source row, including stored halo.
    /// @return the two row spans, or `ErrorCode::OutOfRange` when the row is not stored.
    [[nodiscard]] AFTERNIGHTENGINE_API Result<DrizzleCoordinateRow> row(std::uint32_t sourceRow) const;

    /// Whether this view carries the per-row destination bounds a generated map records.
    ///
    /// A view built from bare coordinate planes carries none, and every consumer then scans the
    /// row's coordinates for what the bounds would have said.
    ///
    /// @return true when `rowBounds()` holds two values per stored row.
    [[nodiscard]] bool hasRowBounds() const noexcept
    {
        return !m_rowBounds.empty();
    }

    /// The borrowed per-row bounds, `low` then `high` for each stored row in order, or empty.
    ///
    /// This is the span a narrower view over the same planes is created with, so a consumer that
    /// re-views a generated map's core rows keeps the bounds the generation recorded.
    ///
    /// @return the bounds span, of twice the stored row count or of zero length.
    [[nodiscard]] std::span<const float> rowBounds() const noexcept
    {
        return m_rowBounds;
    }

    /// The recorded vertical destination span of one stored source row.
    ///
    /// @param sourceRow the source row, which must be stored.
    /// @return the bounds; `ErrorCode::OutOfRange` when the row is not stored and
    ///         `ErrorCode::Unsupported` when this view carries no bounds.
    [[nodiscard]] AFTERNIGHTENGINE_API Result<DrizzleRowBounds> rowBounds(std::uint32_t sourceRow) const;
    /// Checks one coordinate access; generated invalid pairs remain NaN values.
    /// @param x source column.
    /// @param y global source row.
    /// @return the stored Float32 pair widened exactly to double, or `ErrorCode::OutOfRange` when the
    ///         column is outside the source or the row is not stored.
    [[nodiscard]] AFTERNIGHTENGINE_API Result<PixelCoordinate> at(std::uint32_t x, std::uint32_t y) const;

private:
    friend class DrizzleGeometryAccess;
    /// Copies already-checked geometry and borrowed planes.
    /// @param source full source extent.
    /// @param output full output extent.
    /// @param storedBegin first stored row.
    /// @param storedEnd stored end.
    /// @param coreBegin first core row.
    /// @param coreEnd core end.
    /// @param xMap horizontal plane.
    /// @param yMap vertical plane.
    DrizzlePixmapView(const ImageExtent& source, const ImageExtent& output, std::uint32_t storedBegin,
                      std::uint32_t storedEnd, std::uint32_t coreBegin, std::uint32_t coreEnd,
                      std::span<const float> xMap, std::span<const float> yMap,
                      std::span<const float> rowBounds) noexcept
        : m_source(source), m_output(output), m_storedBegin(storedBegin), m_storedEnd(storedEnd),
          m_coreBegin(coreBegin), m_coreEnd(coreEnd), m_xMap(xMap), m_yMap(yMap), m_rowBounds(rowBounds)
    {
    }
    ImageExtent m_source; ///< Source geometry.
    ImageExtent m_output; ///< Destination geometry.
    std::uint32_t m_storedBegin; ///< Stored row origin.
    std::uint32_t m_storedEnd; ///< Stored row end.
    std::uint32_t m_coreBegin; ///< Core row origin.
    std::uint32_t m_coreEnd; ///< Core row end.
    std::span<const float> m_xMap; ///< Horizontal coordinate borrow.
    std::span<const float> m_yMap; ///< Vertical coordinate borrow.
    std::span<const float> m_rowBounds; ///< Two values per stored row, or empty when not recorded.
};

/// The coordinate implementation actually used; neither path dispatches an explicit SIMD kernel.
enum class DrizzleMapImplementation : std::uint8_t
{
    Reference, ///< Public scalar composition, one checked point at a time.
    PreparedScalar ///< Sparse SIP terms and checked-once registration arithmetic on admitted workers.
};

/// Deterministic work counts for one generated map, including halo pixels.
struct DrizzlePixmapReport
{
    std::uint64_t mapped = 0u; ///< Finite Float32-representable pairs, including off-canvas pairs.
    std::uint64_t unmapped = 0u; ///< Failed or unrepresentable pairs, both components written as quiet NaN.
    std::uint64_t sipEvaluations = 0u; ///< Forward SIP evaluations; no inverse coefficients or Newton steps run.
    std::uint64_t polynomialEvaluations = 0u; ///< Registration polynomial evaluations after successful source SIP.
    std::uint32_t workers = 0u; ///< Scheduler plan's admitted worker count.
    DrizzleMapImplementation implementation = DrizzleMapImplementation::Reference; ///< Executed coordinate code.
};

/// Move-only, immutable coordinate storage, fully charged to its context and holding a lease.
/// A failed generation publishes no object and releases all storage. A borrow survives moving
/// the owner but not its destruction or reassignment. Independent const borrows are thread-safe.
class DrizzlePixmap
{
public:
    /// Empty owner.
    DrizzlePixmap() noexcept = default;
    /// Returns coordinates, control bytes and the context lease.
    AFTERNIGHTENGINE_API ~DrizzlePixmap();
    /// Transfers storage without copying coordinates.
    /// @param other the owner to move, left empty.
    AFTERNIGHTENGINE_API DrizzlePixmap(DrizzlePixmap&& other) noexcept;
    /// Releases previous storage and transfers another owner.
    /// @param other the owner to move, left empty; self-assignment is harmless.
    /// @return this owner.
    AFTERNIGHTENGINE_API DrizzlePixmap& operator=(DrizzlePixmap&& other) noexcept;
    /// Coordinate buffers have one owner.
    DrizzlePixmap(const DrizzlePixmap&) = delete;
    /// Coordinate buffers have one owner.
    DrizzlePixmap& operator=(const DrizzlePixmap&) = delete;
    /// Whether this object holds complete generated coordinates.
    /// @return false for an empty or moved-from owner.
    [[nodiscard]] bool valid() const noexcept
    {
        return m_state != nullptr;
    }
    /// Borrows the complete stored geometry and coordinates.
    /// @return the view, or `ErrorCode::NotInitialized` when empty.
    [[nodiscard]] AFTERNIGHTENGINE_API Result<DrizzlePixmapView> view() const;
    /// The coordinate payload: both planes and the recorded row bounds, matching the planner's
    /// `coordinateMapBytes`.
    /// @return payload bytes, or zero when empty.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t coordinateBytes() const noexcept;
    /// All context bytes owned, including the fixed control allocation.
    /// @return total resident bytes, or zero when empty.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t residentBytes() const noexcept;
    /// The fixed control allocation to add when reserving a complete map working set.
    /// @return control bytes, excluding the two coordinate planes.
    [[nodiscard]] static AFTERNIGHTENGINE_API std::uint64_t controlBytes() noexcept;
    /// Generation work, available after complete success.
    /// @return the report, or a zero report when empty.
    [[nodiscard]] AFTERNIGHTENGINE_API DrizzlePixmapReport report() const noexcept;

private:
    friend class DrizzleGeometryAccess;
    class State;
    /// Adopts the complete allocation.
    /// @param state initialized storage state.
    explicit DrizzlePixmap(State* state) noexcept : m_state(state) {}
    State* m_state = nullptr; ///< Exclusive implementation ownership.
};

/// Generates one bounded block or explicitly dense map from immutable prepared geometry.
/// Both extents and byte counts are checked against this execution before allocation. Plans may
/// be reused on another context: map storage and workers belong to the executing context.
/// `ComputeBackend::Reference` runs the public scalar composition one point at a time; any other
/// backend runs the prepared scalar geometry. An explicit unsupported instruction set is refused
/// when fallback is forbidden, otherwise scalar is reported. Tiles contain at most 4096
/// coordinates, with cancellation before each tile and before publication; each completed tile
/// emits the `coordinateTilePairs` trace counter through the execution. No validity rule is
/// inferred from samples: a map reads geometry only, and off-canvas pairs remain finite.
/// @param execution initialized policy, limits, memory and admitted scheduler width.
/// @param plan prepared source geometry; no mutation or cached image result.
/// @param request storage mode, source band and clipped halo.
/// @return complete coordinates, in which a position that cannot be mapped is a pair of quiet NaNs
///         rather than a failure. Otherwise `ErrorCode::NotInitialized` for an empty plan or an
///         uninitialized execution; `ErrorCode::InvalidArgument` for an unknown storage mode, a row
///         count outside [1,256], a halo above one, or a dense request with a row origin or halo;
///         `ErrorCode::OutOfRange` for a first row outside the source; `ErrorCode::LimitExceeded`
///         when the geometry or the map's bytes exceed the execution's limits;
///         `ErrorCode::UnsupportedBackend` for a forced GPU backend or a forced instruction set
///         other than scalar with fallback forbidden; `ErrorCode::Cancelled` or
///         `ErrorCode::DeadlineExceeded`; or the memory resource's refusal.
[[nodiscard]] AFTERNIGHTENGINE_API Result<DrizzlePixmap>
generateDrizzlePixmap(const ExecutionContext& execution, const DrizzleGeometryPlan& plan,
                      const DrizzlePixmapRequest& request = {});

/// Bilinear mapped-centre interpolation, with at most one source pixel of edge extrapolation.
/// Keeps Float32 weights and sums in a fixed tap order, then checks Float32 representability.
/// At least 2x2 source pixels are required; every tap must be finite, even a zero-weight tap.
/// @param map immutable checked coordinate view.
/// @param source fractional source sampling position, each axis in [-1,source dimension].
/// @return the Float32 pair widened to double; `ErrorCode::InvalidArgument` for a non-finite
///         position or a map smaller than 2x2; `ErrorCode::OutOfRange` outside the extrapolation band
///         or for a missing halo row; or `ErrorCode::InvalidData` for a non-finite tap or an
///         overflowing result. No exact-transform fallback occurs.
[[nodiscard]] AFTERNIGHTENGINE_API Result<PixelCoordinate> interpolateDrizzlePoint(const DrizzlePixmapView& map,
                                                                                   const PixelCoordinate& source);

/// Interpolates a square drop's four corners, including border pixels.
/// Corner order is (-h,+h), (+h,+h), (+h,-h), (-h,-h).
/// Uses the cells adjacent to the centre, clamped at image borders, including at exactly h=1;
/// every tap in those cells must be finite, including zero-weight taps.
/// Interior drops share one 3x3 Float32 half-size/weight calculation. Its rounding may differ from
/// four independent point interpolations within Float32 summation error.
/// @param map checked coordinate view; callers provide a one-row halo for bounded square maps.
/// @param x source pixel centre column.
/// @param y source pixel centre row; it must be within the core interval.
/// @param halfSize finite half-side in (0,1], in source pixels; the accumulation uses `pixelFraction / 2`.
/// @return all four corners, never a partial set; `ErrorCode::InvalidArgument` for a half-size that
///         is not finite in (0,1] or a map smaller than 2x2; `ErrorCode::OutOfRange` for a centre
///         outside the source columns or the core rows, or a missing halo row; or
///         `ErrorCode::InvalidData` for a non-finite tap or a corner that overflows Float32.
[[nodiscard]] AFTERNIGHTENGINE_API Result<std::array<DrizzleCoordinate, 4u>>
interpolateDrizzleCorners(const DrizzlePixmapView& map, std::uint32_t x, std::uint32_t y, double halfSize);

/// Interpolates consecutive square drops after checking the row and halo once.
/// A serial row primitive for use inside the caller's scheduled accumulation tiles; it allocates
/// nothing and creates no parallelism limit. Invalid geometry writes eight NaNs and is counted;
/// image validity is not read or inferred. Reference uses checked individual queries, while the
/// CPU path shares row addresses and Float32 weights. Unsupported forced backends are refused.
/// Cancellation is checked before writing, every 4096 footprints and before reporting success.
/// A cancellation failure can leave an overwritten prefix; callers discard that output.
/// @param execution initialized cancellation, backend and geometry-limit policy.
/// @param map immutable coordinate planes, disjoint from output, with the required clipped halo.
/// @param sourceRow source centre row, inside the map's core interval.
/// @param firstColumn first source centre column; output must fit the remaining columns.
/// @param halfSize finite drop half-side in (0,1], in source pixels.
/// @param output caller-owned destination footprints; an empty span requests no work.
/// @return the mapped and unmapped counts, zero for an empty output. Otherwise
///         `ErrorCode::NotInitialized` for an uninitialized execution; `ErrorCode::InvalidArgument`
///         for a half-size that is not finite in (0,1] or a map smaller than 2x2;
///         `ErrorCode::OutOfRange` for a row outside the core, an output that does not fit the row
///         from `firstColumn`, or a missing clipped halo; `ErrorCode::LimitExceeded` when the map's
///         extents exceed the execution's limits; `ErrorCode::UnsupportedBackend` for a forced GPU
///         backend or a forced instruction set other than scalar with fallback forbidden;
///         `ErrorCode::Aliasing` when the output overlaps the map; or `ErrorCode::Cancelled` or
///         `ErrorCode::DeadlineExceeded`. Validation failures write nothing.
[[nodiscard]] AFTERNIGHTENGINE_API Result<DrizzleFootprintReport>
interpolateDrizzleCornerRow(const ExecutionContext& execution, const DrizzlePixmapView& map, std::uint32_t sourceRow,
                            std::uint32_t firstColumn, double halfSize, std::span<DrizzleFootprint> output);

} // namespace an
