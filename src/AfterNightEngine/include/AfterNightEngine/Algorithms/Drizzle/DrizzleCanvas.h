// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The output plane a drizzled set is written on, the guard an intersection keeps at its edges, and
/// the plane a local normalization measures the frames on before they are drizzled.
///
/// Which part of the reference plane a drizzle covers is the registration canvas's decision: the
/// reference frame's rectangle, the union of every frame's, or their intersection. How many output
/// pixels that rectangle becomes is the drizzle's, and the product made that decision itself in three
/// stages, each with its own copy of the arithmetic. This header is that arithmetic, once.
///
/// **A reference or union canvas** keeps its rectangle as the frames' bounds give it and is enlarged
/// to the whole number of pixels that holds it: the rectangle's width and height times the scale,
/// rounded up.
///
/// **An intersection** first moves its corners inward to whole reference pixels, so no output pixel
/// lies partly outside a frame, and is then enlarged rounding down. Where the scale enlarges, an
/// enlarged drop near the edge still reaches only part of an output pixel, so the finished stack
/// keeps a guard of whole pixels at every edge, cropped once it is combined: the scale times one and
/// a half drop fractions, rounded up, and at least one. At a scale of one there is no guard and the
/// drizzled extent loses one pixel instead. A canvas too small to keep its guard keeps none.
///
/// **A plane already chosen** - the comet-aligned output plane - is enlarged to the nearest whole
/// number of pixels and guarded by nothing.
///
/// **The analysis plane** is the same rectangle before the scale: a local normalization fitted on
/// frames registered but not drizzled measures them on it, as many whole pixels as the rectangle
/// holds - rounded down for an intersection, up otherwise. A position `p` of the drizzled canvas lies
/// at `p / scale` on the analysis plane, both in engine pixel coordinates, because a drizzle maps the
/// reference plane onto its canvas by the post-translation and then the scale.
///
/// Positions are engine pixel coordinates - the first pixel's centre is (0.5, 0.5). The rectangle
/// a frame occupies comes from `computeRegistrationBounds`, and the canvas's origin is the
/// post-translation every frame's `DrizzleMappingRequest` carries.

#pragma once

#include <AfterNightEngine/Algorithms/Registration/RegistrationPlan.h>
#include <AfterNightEngine/Algorithms/Registration/RegistrationTransform.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>

#include <cstdint>
#include <span>

namespace an
{

/// What a drizzle canvas is planned for.
struct DrizzleCanvasRequest
{
    /// Which frames the canvas holds.
    RegistrationCanvasMode mode = RegistrationCanvasMode::Reference;

    /// The enlargement, finite and in [1, 4]: the drizzle model's.
    double scale = 2.0;

    /// The drop's width as a fraction of an input pixel, finite and in (0, 1]: the drizzle model's.
    /// Read by an intersection's guard alone.
    double pixelFraction = 0.9;
};

/// The output plane of a drizzled set, its guard and its analysis plane.
struct DrizzleCanvas
{
    /// The rectangle of the reference plane the canvas covers, before the scale. An intersection's
    /// corners lie on whole reference pixels.
    RegistrationBounds boundingBox{};

    /// The offset every frame's mapping adds after its transform and before the scale: the negated
    /// corner of the rectangle, which is each frame's `DrizzleMappingRequest::postTranslation`.
    PixelCoordinate origin{};

    /// The columns the drizzle writes: the output extent a `DrizzleOutputRequest` states.
    std::uint32_t width = 0u;

    /// The rows the drizzle writes.
    std::uint32_t height = 0u;

    /// The whole pixels the finished stack crops from every edge; zero when it crops nothing.
    std::uint32_t edgeGuard = 0u;

    /// The columns of the finished stack: `width` less the guard at both edges.
    std::uint32_t outputWidth = 0u;

    /// The rows of the finished stack.
    std::uint32_t outputHeight = 0u;

    /// The columns of the plane a local normalization measures the frames on before the scale.
    std::uint32_t analysisWidth = 0u;

    /// The rows of the analysis plane.
    std::uint32_t analysisHeight = 0u;

    /// Whether two canvases are the same plane.
    ///
    /// @param left one canvas.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend constexpr bool operator==(const DrizzleCanvas& left,
                                                   const DrizzleCanvas& right) noexcept = default;
};

/// How many whole pixels an intersection's guard crops from every edge of a stack drizzled at a
/// scale.
///
/// The scale times one and a half drop fractions, rounded up, and at least one; nothing at a scale
/// within a millionth of one, which does not enlarge. The drop fraction is held to the unit
/// interval and read as one when it is not finite.
///
/// Thread-safe, reentrant and allocation-free.
///
/// @param scale the enlargement; a value that is not finite guards nothing.
/// @param pixelFraction the drop's width as a fraction of an input pixel.
/// @return the guard, in output pixels.
[[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t drizzleIntersectionGuard(double scale, double pixelFraction) noexcept;

/// Plans a drizzle's output plane from every frame's mapped rectangle.
///
/// The rectangle is the reference frame's under `RegistrationCanvasMode::Reference`, the union of
/// every frame's under `Union`, and the intersection of every frame's under `Intersection`; the
/// extents, the guard and the analysis plane follow the rules this header states.
///
/// Thread-safe, reentrant and allocation-free apart from a failure it describes.
///
/// @param bounds one mapped rectangle per frame, from `computeRegistrationBounds`, in the set's
///        own order.
/// @param referenceIndex which entry is the reference, below `bounds.size()`.
/// @param request the mode, the scale and the drop fraction.
/// @return the canvas; `ErrorCode::InvalidArgument` for an empty set, an unknown mode, a reference
///         index outside the set, a non-finite or inverted rectangle, or a scale or drop fraction
///         outside its range; `ErrorCode::EmptySelection` when the selected rectangle holds no
///         whole pixel, which is what an intersection of frames with nothing in common produces;
///         or `ErrorCode::LimitExceeded` when the rectangle exceeds
///         `kMaximumRegistrationCanvasExtent` on an axis.
[[nodiscard]] AFTERNIGHTENGINE_API Result<DrizzleCanvas> planDrizzleCanvas(std::span<const RegistrationBounds> bounds,
                                                                           std::uint32_t referenceIndex,
                                                                           const DrizzleCanvasRequest& request);

/// Plans a drizzle's output plane on a rectangle already chosen.
///
/// The comet-aligned output plane is such a rectangle: its extent is the nearest whole number of
/// pixels to the rectangle's width and height times the scale, halfway cases away from zero and at
/// least one, and nothing is guarded.
///
/// Thread-safe, reentrant and allocation-free apart from a failure it describes.
///
/// @param plane the rectangle of the reference plane the canvas covers.
/// @param scale the enlargement, finite and in [1, 4].
/// @return the canvas; `ErrorCode::InvalidArgument` for a non-finite, inverted or empty rectangle
///         or a scale outside its range; or `ErrorCode::LimitExceeded` when the rectangle exceeds
///         `kMaximumRegistrationCanvasExtent` on an axis.
[[nodiscard]] AFTERNIGHTENGINE_API Result<DrizzleCanvas> planDrizzleCanvasOnPlane(const RegistrationBounds& plane,
                                                                                  double scale);

} // namespace an
