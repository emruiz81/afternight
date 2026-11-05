/// @file
/// Painting a detected star list into a mask.
///
/// Two paintings over one rule set: `paintShapedStarMask` gives every admitted star a radial
/// falloff from `StarMaskShape`, and `paintEllipticalStarMask` gives it a solid ellipse. Both write a
/// single-channel Float32 or Float64 coverage in [0, 1] into caller storage, clear it first, and
/// merge overlapping stars by keeping the larger contribution, so the result depends on neither the
/// order of the list nor the thread count. The ellipse's boundary rule is stated here rather than
/// inherited from an imaging library.
///
/// Painting uses the same pixel coordinates as `DetectedStar`. A shaped destination sample at
/// column `c` and row `r` is evaluated at `(c + 0.5, r + 0.5)`. Ellipse coverage is sampled within
/// `[c, c + 1]` by `[r, r + 1]`, so neither painting needs a position adjustment.
///
/// **Derived from DeepSkyStacker.** The six falloff shapes, their reach of three radii and the
/// largest contribution kept where stars overlap are DeepSkyStacker's star mask
/// (https://github.com/deepskystacker/DSS, `DeepSkyStackerKernel/StarMask.h` and `StarMask.cpp`),
/// whose licence states `Copyright (c) 2006-2019, LucCoiffier` and `Copyright (c) 2018-2023, David C.
/// Partridge, Tony Cook, Mat Draper, Simon C. Smith, Vitali Pelenjow, Michal Schulz, Martin Toeltsch`
/// and the three-clause BSD terms (BSD-3-Clause). This header and its implementation are part of that
/// derived work, distributed under the GNU General Public License, version 3 or any later version,
/// with that notice kept, in full in `src/AfterNightEngine/THIRD_PARTY_NOTICES.md`. The solid ellipse,
/// the coverage in caller storage and the answer's independence of list order and thread count are
/// this engine's. The provenance note is `docs/dev/DEEPSKYSTACKER_PROVENANCE.md`.

// SPDX-License-Identifier: GPL-3.0-or-later AND BSD-3-Clause
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <AfterNightEngine/Algorithms/Stars/StarProfile.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <cstdint>
#include <span>

namespace an
{

/// One operation's policy and engine-context lease. Defined in `Runtime/ExecutionContext.h`.
class ExecutionContext;

/// How a star's brightness falls off from its centre in a shaped mask.
///
/// Six falloffs. Each takes the distance `d` from the centre and the star's radius `r` and
/// returns a coverage in [0, 1]. The painting uses a star's larger full width as its radius and
/// covers the square of three radii about its centre, so the extent painted is the same for all
/// six: the four polynomial shapes reach zero at three radii, and the two bells are cut off
/// there.
enum class StarMaskShape : std::uint8_t
{
    /// A Gaussian bell: `exp(-d^2 / (2 r^2))`. Exactly one at the centre and about 0.011 at three
    /// radii, where the painting stops; it is the smoothest of the six.
    Bell = 0u,

    /// The bell scaled up by `exp(0.5)` and clipped at one, so the core is flat and the falloff is
    /// the bell's outer half. The default.
    TruncatedBell = 1u,

    /// `1 - d / (3 r)`, clamped at zero. A cone.
    Linear = 2u,

    /// The cone scaled up by one and a half and clipped at one, so the inner third is flat.
    TruncatedLinear = 3u,

    /// `1 - (d / (3 r))^3`, clamped at zero. Flatter than the cone near the centre and steeper at
    /// the edge.
    Cubic = 4u,

    /// `1 - (d / (3 r))^4`, clamped at zero. Flatter still.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Quadratic = 5u
};

/// Whether a value names a shape this build paints.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param shape the value to inspect.
/// @return true for a declared shape; false for an out-of-range cast.
[[nodiscard]] constexpr bool isStarMaskShape(StarMaskShape shape) noexcept
{
    return static_cast<std::uint8_t>(shape) <= static_cast<std::uint8_t>(StarMaskShape::Quadratic);
}

/// The shape's own name, for a log line, a diagnostic field or a failure message.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param shape the shape to name.
/// @return a null-terminated string literal compiled into this library; `"UnknownStarMaskShape"`
///         for an out-of-range cast.
[[nodiscard]] AFTERNIGHTENGINE_API const char* starMaskShapeName(StarMaskShape shape) noexcept;

/// One shape's coverage at one distance.
///
/// The whole of what a shape is, exposed so a caller can plot one, compose one with something
/// else, or check a painted mask against the definition rather than against another painting of
/// it.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param shape which falloff.
/// @param distance distance from the star's centre, in pixels. Must be finite and not negative.
/// @param radius the star's radius, in pixels. Must be finite and positive.
/// @return the coverage, clamped to [0, 1]; `ErrorCode::InvalidArgument` for an out-of-range
///         shape, a non-finite or negative distance, or a non-finite or non-positive radius.
[[nodiscard]] AFTERNIGHTENGINE_API Result<double> starMaskCoverage(StarMaskShape shape, double distance, double radius);

/// What a caller asks a shaped star mask for.
///
/// A star is painted when twice its larger full width falls inside the size window, and its radius
/// is that width scaled and then extended.
struct StarMaskShapeRequest
{
    /// Which falloff to paint.
    StarMaskShape shape = StarMaskShape::TruncatedBell;

    /// The smallest star diameter, in pixels, that is painted at all. Finite and not negative.
    double minimumSize = 2.0;

    /// The largest star diameter, in pixels, that is painted at all. Finite, and at or above
    /// `minimumSize`.
    double maximumSize = 25.0;

    /// The factor the radius is multiplied by before painting. Finite and positive.
    double sizeScale = 1.0;

    /// Pixels added to the radius after scaling. Finite and not negative.
    double sizeIncrease = 0.0;
};

/// What a caller asks a filled elliptical star mask for.
///
/// Each star becomes one solid ellipse: four times its major full width across, its axis ratio
/// tall, turned by its position angle.
struct StarMaskEllipseRequest
{
    /// The factor the ellipse's axes are multiplied by. Finite and positive.
    double sizeScale = 1.0;

    /// How much of a pixel's area must be inside the ellipse for it to be painted, in [0, 1].
    ///
    /// An imaging library's rule for a filled ellipse is neither documented nor stable across
    /// versions, so this states the rule: a pixel is painted when the fraction of its area inside the
    /// ellipse reaches this threshold, estimated on a fixed sub-pixel lattice. One half, the default,
    /// is the nearest thing to OpenCV's filled ellipse.
    double coverageThreshold = 0.5;
};

/// What a mask painting produced.
struct StarMaskResult
{
    /// How many stars of the list were painted.
    std::uint64_t paintedStars = 0u;

    /// How many were skipped because their size fell outside the window, or because their fitted
    /// width was not usable.
    std::uint64_t skippedStars = 0u;

    /// How many destination samples the painting left above zero.
    std::uint64_t coveredSamples = 0u;
};

/// Paints a star list into a single-channel mask as shaped falloffs.
///
/// The destination is cleared first, then each admitted star contributes its shape and each sample
/// keeps the largest contribution it received, so overlapping stars merge rather than add. The
/// result does not depend on the order of the list or on how many threads the operation was
/// admitted, because the maximum of a set does not.
///
/// A star's footprint is the square of three radii about its centre, and the shape is evaluated at
/// every sample of it. Four of the six shapes have reached zero by then; the two bells have not, so
/// the square's edge is a small step rather than a smooth fade. That is deliberate and not a defect:
/// the alternative is a different mask from the one
/// every existing preset was chosen against.
///
/// The painting evaluates at pixel centres `(column + 0.5, row + 0.5)`, in the same coordinate
/// system as `(positionX, positionY)`.
///
/// Thread-safe with respect to `execution` and `stars`, which are only read. It allocates nothing
/// beyond what the scheduler needs, runs on the admitted threads, and checks cancellation at row
/// boundaries. It is not failure-atomic: a cancelled painting leaves the destination partly
/// written.
///
/// @param execution the operation policy and engine-context lease.
/// @param stars the stars to paint.
/// @param request which falloff, which size window and which adjustments.
/// @param sampleType what the destination samples are. Must be `Float32` or `Float64`.
/// @param destination where the mask goes. Must not be null, must be aligned for `sampleType`, and
///        must hold every byte `destinationLayout` names; neither is checked.
/// @param destinationLayout where it sits. Must describe a single-channel image.
/// @return what was painted; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::InvalidArgument` for a null destination, an out-of-range shape or request
///         field, or a representation this operation does not write;
///         `ErrorCode::IncompatibleArguments` for a destination that is not one channel or whose
///         element size is not the representation's; `ErrorCode::LimitExceeded` for a list longer
///         than `kMaxStarsPerFrame`; or a cancellation or deadline failure.
[[nodiscard]] AFTERNIGHTENGINE_API Result<StarMaskResult> paintShapedStarMask(const ExecutionContext& execution,
                                                                              std::span<const DetectedStar> stars,
                                                                              const StarMaskShapeRequest& request,
                                                                              SampleType sampleType, void* destination,
                                                                              const ImageLayout& destinationLayout);

/// Paints a star list into a single-channel mask as solid ellipses.
///
/// The same clearing, merging, ordering, threading and pixel-convention rules as
/// `paintShapedStarMask`; the difference is the footprint, which is a rotated ellipse at full value
/// rather than a radial falloff, and a star is skipped only when its major or minor axis is not
/// finite and positive. A caller that wants softened edges blurs the result.
///
/// @param execution the operation policy and engine-context lease.
/// @param stars the stars to paint.
/// @param request the axis scale and the coverage rule.
/// @param sampleType what the destination samples are. Must be `Float32` or `Float64`.
/// @param destination where the mask goes, under the same conditions as `paintShapedStarMask`'s.
/// @param destinationLayout where it sits. Must describe a single-channel image.
/// @return what was painted; the same failures as `paintShapedStarMask`, with an out-of-range axis
///         scale or coverage threshold as this request's `ErrorCode::InvalidArgument`.
[[nodiscard]] AFTERNIGHTENGINE_API Result<StarMaskResult>
paintEllipticalStarMask(const ExecutionContext& execution, std::span<const DetectedStar> stars,
                        const StarMaskEllipseRequest& request, SampleType sampleType, void* destination,
                        const ImageLayout& destinationLayout);

/// What the two paintings do to the image they write.
inline constexpr OperationEffect kStarMaskEffect{.samples = SampleEffect::Writes,
                                                 .description = DescriptionEffect::Restates};

} // namespace an
