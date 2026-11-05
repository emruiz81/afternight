// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the drizzle geometry's private state, and the accessor through which map
// generation, corner interpolation and accumulation reach it.
//
// A `DrizzleGeometryPlan` is a pointer to `DrizzleGeometryState`, counted intrusively so that no
// shared-control allocation escapes the context's budget. The state holds the descriptor and the
// forward SIP compiled to its nonzero terms, which `applyPreparedSip` evaluates at every source
// pixel of a map. `DrizzleGeometryAccess` is the one friend of the public plan, view and map: it
// hands out their internals without the per-call checks the public accessors make, to callers that
// have already made those checks once for a whole row or band.
#pragma once

#include <AfterNightEngine/Algorithms/Drizzle/DrizzleGeometry.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include "runtime/EngineContextState.h"

#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <utility>

namespace an
{

class DrizzlePixmap;
class DrizzlePixmapView;
struct DrizzleCoordinateRow;
struct DrizzleRowBounds;
struct DrizzleCoordinate;
struct DrizzlePixmapRequest;

/// One nonzero SIP coefficient; the table preserves the model's horizontal/vertical term order.
struct DrizzleSipTerm
{
    double coefficient = 0.0; ///< The term's coefficient.
    std::uint32_t horizontal = 0u; ///< The power of the column offset from the reference pixel.
    std::uint32_t vertical = 0u; ///< The power of the row offset from the reference pixel.
};

/// The shared state of one geometry plan. Intrusive ownership avoids an unbudgeted shared-control
/// allocation; the final release retains the context until after the state allocation itself has
/// been returned.
class DrizzleGeometryState
{
public:
    /// Copies the descriptor, retains the execution's engine context and compiles the source SIP's
    /// nonzero forward terms.
    /// @param execution the operation whose context and memory resource own the state.
    /// @param geometry the descriptor to own.
    DrizzleGeometryState(const ExecutionContext& execution, const DrizzleMapDescriptor& geometry) noexcept;
    std::atomic<std::uint64_t> references{1u}; ///< Handles sharing the state; the last one destroys it.
    EngineContextLease lease; ///< Keeps the engine context alive until the state's bytes are returned.
    MemoryResource* memory; ///< The resource the state was allocated from and returns to.
    DrizzleMapDescriptor descriptor; ///< The owned geometry.
    /// The column correction's nonzero terms in the model's order, at most the 28 of a sixth-order
    /// polynomial.
    std::array<DrizzleSipTerm, 28u> horizontal{};
    std::array<DrizzleSipTerm, 28u> vertical{}; ///< The row correction's nonzero terms, likewise.
    std::uint32_t horizontalCount = 0u; ///< How many entries of `horizontal` are used.
    std::uint32_t verticalCount = 0u; ///< How many entries of `vertical` are used.
    std::uint32_t sipOrder = 0u; ///< The higher forward order, or zero when there is no source SIP.
    /// The source distortion's reference pixel, read once rather than through the descriptor at
    /// every pixel of a map.
    PixelCoordinate sipReference{};
};

/// The private accessor of `DrizzleGeometryPlan`, `DrizzlePixmapView` and `DrizzlePixmap`: it reads
/// their internals without the checks their public accessors repeat, for callers that have made
/// those checks once.
class DrizzleGeometryAccess
{
public:
    /// One stored row's two coordinate spans. The caller has checked that the row is stored.
    /// @param map the view.
    /// @param row the global source row.
    /// @return the row's spans.
    static DrizzleCoordinateRow rowUnchecked(const DrizzlePixmapView& map, std::uint32_t row) noexcept;
    /// One stored row's recorded destination bounds. The caller has checked that the view carries
    /// bounds and that the row is stored.
    /// @param map the view.
    /// @param row the global source row.
    /// @return the row's bounds.
    static DrizzleRowBounds rowBoundsUnchecked(const DrizzlePixmapView& map, std::uint32_t row) noexcept;
    /// Whether a byte range overlaps either coordinate plane or the row bounds of a view.
    /// @param map the view.
    /// @param data the range's first byte.
    /// @param bytes the range's length.
    /// @return true on any overlap; false for an empty range.
    static bool overlaps(const DrizzlePixmapView& map, const void* data, std::size_t bytes) noexcept;
    /// A square drop's four corners, as `interpolateDrizzleCorners` computes them, taking the shared
    /// 3x3 path for an interior pixel. The caller has checked dimensions, core coordinates and the
    /// complete clipped halo. Invalid taps return false without allocating an error for an ordinary
    /// map hole.
    /// @param map the view.
    /// @param x the source column.
    /// @param y the global source row.
    /// @param halfSize the drop's half-side, in source pixels.
    /// @param corners receives the corners.
    /// @return true when all four corners are finite.
    [[nodiscard]] static bool cornersUnchecked(const DrizzlePixmapView& map, std::uint32_t x, std::uint32_t y,
                                               double halfSize, std::array<DrizzleCoordinate, 4u>& corners) noexcept;
    /// The plan's shared state.
    /// @param plan the plan.
    /// @return the state, or null for an empty plan.
    [[nodiscard]] static const DrizzleGeometryState* state(const DrizzleGeometryPlan& plan) noexcept
    {
        return plan.m_state;
    }
    /// The body of `generateDrizzlePixmap`, a member so it can build the map's private state.
    /// @param execution the operation.
    /// @param plan the prepared geometry.
    /// @param request the storage mode, source band and halo.
    /// @return what `generateDrizzlePixmap` returns.
    [[nodiscard]] static Result<DrizzlePixmap>
    generate(const ExecutionContext& execution, const DrizzleGeometryPlan& plan, const DrizzlePixmapRequest& request);
};

namespace drizzle_detail
{

/// A failure in the algorithms domain.
/// @param code why the operation failed.
/// @param message what a person reads.
/// @param operation the entry point speaking.
/// @return the failure.
[[nodiscard]] Status failure(ErrorCode code, const char* message, const char* operation);
/// The checks every geometry entry makes before it allocates.
/// @param execution the operation.
/// @param descriptor the geometry.
/// @return success; `ErrorCode::NotInitialized` for an uninitialized execution; cancellation; or
///         `ErrorCode::LimitExceeded` when an extent exceeds the execution's limits.
[[nodiscard]] Status checkGeometry(const ExecutionContext& execution, const DrizzleMapDescriptor& descriptor);

/// Releases a context-owned control object only after moving its lease out of the object being
/// destroyed. Destructors may return subsidiary allocations through the still-live resource.
template <typename StateT> void destroyState(StateT* state) noexcept
{
    if (state == nullptr)
        return;
    auto lease = std::move(state->lease);
    MemoryResource* memory = state->memory;
    std::destroy_at(state);
    memory->deallocate(state, sizeof(StateT), alignof(StateT));
}

/// Evaluates compiled sparse SIP terms, sharing powers between both components. No inverse SIP
/// work belongs in source-to-destination mapping. Returns false on overflow before registration.
///
/// Inline because a map runs it for every source pixel, and a call into another unit for each
/// one costs as much as the arithmetic.
[[nodiscard]] inline bool applyPreparedSip(const DrizzleGeometryState& state, PixelCoordinate& point) noexcept
{
    if (state.sipOrder == 0u)
        return true;
    const double u = point.x - state.sipReference.x;
    const double v = point.y - state.sipReference.y;
    std::array<double, kSipCoefficientRowCount> xPowers;
    std::array<double, kSipCoefficientRowCount> yPowers;
    xPowers[0] = yPowers[0] = 1.0;
    for (std::uint32_t i = 1u; i <= state.sipOrder; ++i)
    {
        xPowers[i] = xPowers[i - 1u] * u;
        yPowers[i] = yPowers[i - 1u] * v;
    }
    const auto evaluate = [&](const auto& terms, std::uint32_t count) {
        double sum = 0.0;
        for (std::uint32_t i = 0u; i < count; ++i)
        {
            const auto& term = terms[i];
            sum += term.coefficient * xPowers[term.horizontal] * yPowers[term.vertical];
        }
        return sum;
    };
    point.x += evaluate(state.horizontal, state.horizontalCount);
    point.y += evaluate(state.vertical, state.verticalCount);
    return std::isfinite(point.x) && std::isfinite(point.y);
}

} // namespace drizzle_detail
} // namespace an
