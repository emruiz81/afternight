// AfterNightEngine - painting a detected star list into a mask.
//
// Two footprints over one loop. A shaped mask gives every star a radial falloff and a solid mask
// gives it a filled ellipse, and in both the destination keeps the largest contribution any star
// made to it, so overlapping stars merge instead of adding and the answer does not depend on the
// order of the list or the number of threads.
//
// **The ellipse is rasterised here rather than asked of an imaging library.** A filled rotated
// ellipse drawn through OpenCV follows whatever rule that version uses at the boundary, which is
// neither documented nor stable. This states the rule: a pixel is painted when the fraction of its
// own area inside the ellipse reaches a threshold the caller sets, estimated on a fixed four-by-four
// sub-pixel lattice.
//
// Both footprints use the pixel coordinates of `DetectedStar`: the first pixel spans [0, 1]
// on either axis and its centre is at one half.
//
// The six falloff shapes, their reach of three radii and the largest contribution kept where stars
// overlap are adapted from DeepSkyStacker's star mask (https://github.com/deepskystacker/DSS,
// `DeepSkyStackerKernel/StarMask.h` and `StarMask.cpp`), whose licence states
// `Copyright (c) 2006-2019, LucCoiffier` and `Copyright (c) 2018-2023, David C. Partridge, Tony Cook,
// Mat Draper, Simon C. Smith, Vitali Pelenjow, Michal Schulz, Martin Toeltsch` and the three-clause BSD
// terms (BSD-3-Clause); this file is part of that derived work and is distributed under the GNU General
// Public License, version 3 or any later version, with that notice kept, in full in
// `src/AfterNightEngine/THIRD_PARTY_NOTICES.md`. The solid ellipse and its stated boundary rule are this
// engine's. The provenance note is `docs/dev/DEEPSKYSTACKER_PROVENANCE.md`.

// SPDX-License-Identifier: GPL-3.0-or-later AND BSD-3-Clause
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Algorithms/Stars/StarMask.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "algorithms/stars/StarsInternal.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>

namespace an
{

namespace
{

using stars_detail::failure;
using stars_detail::isFinitePositive;
using stars_detail::parameterFailure;

constexpr const char* kCoverageOperation = "an::starMaskCoverage";
constexpr const char* kShapedOperation = "an::paintShapedStarMask";
constexpr const char* kEllipseOperation = "an::paintEllipticalStarMask";

/// How far out, in radii, every shape has reached zero.
constexpr double kShapeReach = 3.0;

/// The scale the truncated bell is multiplied by before it is clipped at one.
const double kTruncatedBellScale = 1.0 / std::exp(-0.5);

/// The scale the truncated cone is multiplied by before it is clipped at one.
constexpr double kTruncatedLinearScale = 1.5;

/// How many sub-samples across one pixel the ellipse's coverage is estimated on.
///
/// Four, so sixteen sub-samples per pixel and a coverage quantised to a sixteenth. That is finer
/// than the half-pixel decision a threshold of one half makes and far cheaper than an exact area.
constexpr std::uint32_t kCoverageLattice = 4u;

/// One shape's coverage at one distance, with both arguments already checked.
[[nodiscard]] double coverageOf(StarMaskShape shape, double distance, double radius) noexcept
{
    switch (shape)
    {
    case StarMaskShape::Bell:
        return std::exp(-(distance * distance) / (2.0 * radius * radius));
    case StarMaskShape::TruncatedBell:
        return (std::min)(1.0, kTruncatedBellScale * std::exp(-(distance * distance) / (2.0 * radius * radius)));
    case StarMaskShape::Linear:
        return (std::max)(0.0, 1.0 - distance / (kShapeReach * radius));
    case StarMaskShape::TruncatedLinear:
        return (std::max)(0.0, kTruncatedLinearScale - distance * kTruncatedLinearScale / (kShapeReach * radius));
    case StarMaskShape::Cubic: {
        const double scaled = distance / (kShapeReach * radius);
        return (std::max)(0.0, 1.0 - scaled * scaled * scaled);
    }
    case StarMaskShape::Quadratic: {
        const double scaled = distance / (kShapeReach * radius);
        return (std::max)(0.0, 1.0 - scaled * scaled * scaled * scaled);
    }
    }
    return 0.0;
}

/// The rectangle of destination rows and columns one star can reach.
struct PaintBounds
{
    /// First column, inclusive.
    std::int64_t left = 0;

    /// Last column, inclusive.
    std::int64_t right = -1;

    /// First row, inclusive.
    std::int64_t top = 0;

    /// Last row, inclusive.
    std::int64_t bottom = -1;
};

/// Clips a star's reach to the destination.
[[nodiscard]] PaintBounds boundsOf(double centreX, double centreY, double reachX, double reachY, std::uint32_t width,
                                   std::uint32_t height) noexcept
{
    PaintBounds bounds{};
    bounds.left = (std::max)(std::int64_t{0}, static_cast<std::int64_t>(std::floor(centreX - reachX)));
    bounds.right =
        (std::min)(static_cast<std::int64_t>(width) - 1, static_cast<std::int64_t>(std::ceil(centreX + reachX)));
    bounds.top = (std::max)(std::int64_t{0}, static_cast<std::int64_t>(std::floor(centreY - reachY)));
    bounds.bottom =
        (std::min)(static_cast<std::int64_t>(height) - 1, static_cast<std::int64_t>(std::ceil(centreY + reachY)));
    return bounds;
}

/// One destination sample raised to a coverage, whatever its representation.
template <typename SampleT> void raiseTo(void* destination, std::uint64_t index, double coverage) noexcept
{
    auto* const samples = static_cast<SampleT*>(destination);
    const auto value = static_cast<SampleT>(coverage);
    if (value > samples[index])
    {
        samples[index] = value;
    }
}

/// Checks the destination every painting writes into.
[[nodiscard]] Status checkDestination(const char* operation, SampleType sampleType, const void* destination,
                                      const ImageLayout& layout)
{
    if (destination == nullptr)
    {
        return parameterFailure(operation, ErrorCode::InvalidArgument, "a star mask needs destination storage",
                                "destination");
    }
    if (sampleType != SampleType::Float32 && sampleType != SampleType::Float64)
    {
        return failure(operation, ErrorCode::InvalidArgument,
                       "a star mask is written as a floating coverage, not an integer code");
    }
    if (layout.extent().channelCount() != 1u || layout.samplesPerPixel() != 1u)
    {
        return failure(operation, ErrorCode::IncompatibleArguments, "a star mask is a single-channel image");
    }
    if (layout.elementSizeBytes() != sampleTypeSizeBytes(sampleType))
    {
        return failure(operation, ErrorCode::IncompatibleArguments,
                       "the layout's element size is not the stated representation's");
    }
    return Status::success();
}

/// Sets every sample of a destination's rows to zero, on the operation's threads.
[[nodiscard]] Status clearDestination(const ExecutionContext& execution, SampleType sampleType, void* destination,
                                      const ImageLayout& layout)
{
    const std::uint32_t height = layout.extent().height();
    const std::uint32_t width = layout.extent().width();
    return parallelFor(execution, height,
                       TaskCost{.bytesPerItem = static_cast<std::uint64_t>(width) * layout.elementSizeBytes(),
                                .operationsPerItem = width},
                       [&](TaskRange rows) {
                           for (std::uint64_t row = rows.begin; row < rows.end; ++row)
                           {
                               auto* const line = static_cast<std::byte*>(destination) + row * layout.rowStrideBytes();
                               if (sampleType == SampleType::Float32)
                               {
                                   std::fill_n(reinterpret_cast<float*>(line), width, 0.0f);
                               }
                               else
                               {
                                   std::fill_n(reinterpret_cast<double*>(line), width, 0.0);
                               }
                           }
                           return Status::success();
                       });
}

} // namespace

const char* starMaskShapeName(StarMaskShape shape) noexcept
{
    switch (shape)
    {
    case StarMaskShape::Bell:
        return "Bell";
    case StarMaskShape::TruncatedBell:
        return "TruncatedBell";
    case StarMaskShape::Linear:
        return "Linear";
    case StarMaskShape::TruncatedLinear:
        return "TruncatedLinear";
    case StarMaskShape::Cubic:
        return "Cubic";
    case StarMaskShape::Quadratic:
        return "Quadratic";
    }
    return "UnknownStarMaskShape";
}

Result<double> starMaskCoverage(StarMaskShape shape, double distance, double radius)
{
    if (!isStarMaskShape(shape))
    {
        return parameterFailure(kCoverageOperation, ErrorCode::InvalidArgument,
                                "the shape is not one this build paints", "shape");
    }
    if (!std::isfinite(distance) || distance < 0.0)
    {
        return parameterFailure(kCoverageOperation, ErrorCode::InvalidArgument,
                                "the distance is finite and not negative", "distance");
    }
    if (!isFinitePositive(radius))
    {
        return parameterFailure(kCoverageOperation, ErrorCode::InvalidArgument, "the radius is finite and positive",
                                "radius");
    }
    return (std::clamp)(coverageOf(shape, distance, radius), 0.0, 1.0);
}

Result<StarMaskResult> paintShapedStarMask(const ExecutionContext& execution, std::span<const DetectedStar> stars,
                                           const StarMaskShapeRequest& request, SampleType sampleType,
                                           void* destination, const ImageLayout& destinationLayout)
{
    if (!execution.valid())
    {
        return failure(kShapedOperation, ErrorCode::NotInitialized, "the execution context holds nothing");
    }
    if (!isStarMaskShape(request.shape))
    {
        return parameterFailure(kShapedOperation, ErrorCode::InvalidArgument, "the shape is not one this build paints",
                                "shape");
    }
    if (!std::isfinite(request.minimumSize) || request.minimumSize < 0.0 || !std::isfinite(request.maximumSize) ||
        request.maximumSize < request.minimumSize)
    {
        return parameterFailure(kShapedOperation, ErrorCode::InvalidArgument,
                                "the size window is finite, not negative and ordered", "minimumSize");
    }
    if (!isFinitePositive(request.sizeScale))
    {
        return parameterFailure(kShapedOperation, ErrorCode::InvalidArgument, "the size scale is finite and positive",
                                "sizeScale");
    }
    if (!std::isfinite(request.sizeIncrease) || request.sizeIncrease < 0.0)
    {
        return parameterFailure(kShapedOperation, ErrorCode::InvalidArgument,
                                "the size increase is finite and not negative", "sizeIncrease");
    }
    if (stars.size() > kMaxStarsPerFrame)
    {
        return failure(kShapedOperation, ErrorCode::LimitExceeded, "more stars than one frame may carry");
    }
    const Status destinationReady = checkDestination(kShapedOperation, sampleType, destination, destinationLayout);
    if (destinationReady.failed())
    {
        return destinationReady;
    }
    const Status cleared = clearDestination(execution, sampleType, destination, destinationLayout);
    if (cleared.failed())
    {
        return cleared;
    }

    const std::uint32_t width = destinationLayout.extent().width();
    const std::uint32_t height = destinationLayout.extent().height();
    StarMaskResult result{};
    // Each row is painted by one worker and reads every star, so no two workers write one sample and
    // the merge is a plain maximum rather than a reduction.
    std::atomic<std::uint64_t> covered{0u};
    const Status painted = parallelFor(
        execution, height,
        TaskCost{.bytesPerItem = static_cast<std::uint64_t>(width) * destinationLayout.elementSizeBytes(),
                 .operationsPerItem = static_cast<std::uint64_t>(width) * (stars.empty() ? 1u : stars.size())},
        [&](TaskRange rows) {
            std::uint64_t rowCovered = 0u;
            for (std::uint64_t row = rows.begin; row < rows.end; ++row)
            {
                const Status running = execution.checkCancellation();
                if (running.failed())
                {
                    return running;
                }
                auto* const line = static_cast<std::byte*>(destination) + row * destinationLayout.rowStrideBytes();
                for (const DetectedStar& star : stars)
                {
                    const double largest = (std::max)(star.psf.fullWidthX, star.psf.fullWidthY);
                    if (!isFinitePositive(largest) || 2.0 * largest < request.minimumSize ||
                        2.0 * largest > request.maximumSize)
                    {
                        continue;
                    }
                    const double radius = largest * request.sizeScale + request.sizeIncrease;
                    if (!isFinitePositive(radius))
                    {
                        continue;
                    }
                    const double reach = kShapeReach * radius;
                    const PaintBounds bounds =
                        boundsOf(star.positionX - 0.5, star.positionY - 0.5, reach, reach, width, height);
                    if (static_cast<std::int64_t>(row) < bounds.top || static_cast<std::int64_t>(row) > bounds.bottom)
                    {
                        continue;
                    }
                    const double offsetY = static_cast<double>(row) + 0.5 - star.positionY;
                    for (std::int64_t column = bounds.left; column <= bounds.right; ++column)
                    {
                        const double offsetX = static_cast<double>(column) + 0.5 - star.positionX;
                        const double distance = std::sqrt(offsetX * offsetX + offsetY * offsetY);
                        const double coverage = (std::clamp)(coverageOf(request.shape, distance, radius), 0.0, 1.0);
                        if (coverage <= 0.0)
                        {
                            continue;
                        }
                        if (sampleType == SampleType::Float32)
                        {
                            raiseTo<float>(line, static_cast<std::uint64_t>(column), coverage);
                        }
                        else
                        {
                            raiseTo<double>(line, static_cast<std::uint64_t>(column), coverage);
                        }
                    }
                }
                for (std::uint32_t column = 0u; column < width; ++column)
                {
                    const double value = sampleType == SampleType::Float32
                                             ? static_cast<double>(reinterpret_cast<const float*>(line)[column])
                                             : reinterpret_cast<const double*>(line)[column];
                    if (value > 0.0)
                    {
                        ++rowCovered;
                    }
                }
            }
            covered.fetch_add(rowCovered, std::memory_order_relaxed);
            return Status::success();
        });
    if (painted.failed())
    {
        return painted;
    }

    for (const DetectedStar& star : stars)
    {
        const double largest = (std::max)(star.psf.fullWidthX, star.psf.fullWidthY);
        const bool admitted = isFinitePositive(largest) && 2.0 * largest >= request.minimumSize &&
                              2.0 * largest <= request.maximumSize &&
                              isFinitePositive(largest * request.sizeScale + request.sizeIncrease);
        if (admitted)
        {
            ++result.paintedStars;
        }
        else
        {
            ++result.skippedStars;
        }
    }
    result.coveredSamples = covered.load(std::memory_order_relaxed);
    return result;
}

Result<StarMaskResult> paintEllipticalStarMask(const ExecutionContext& execution, std::span<const DetectedStar> stars,
                                               const StarMaskEllipseRequest& request, SampleType sampleType,
                                               void* destination, const ImageLayout& destinationLayout)
{
    if (!execution.valid())
    {
        return failure(kEllipseOperation, ErrorCode::NotInitialized, "the execution context holds nothing");
    }
    if (!isFinitePositive(request.sizeScale))
    {
        return parameterFailure(kEllipseOperation, ErrorCode::InvalidArgument, "the size scale is finite and positive",
                                "sizeScale");
    }
    if (!std::isfinite(request.coverageThreshold) || request.coverageThreshold < 0.0 || request.coverageThreshold > 1.0)
    {
        return parameterFailure(kEllipseOperation, ErrorCode::InvalidArgument,
                                "the coverage threshold lies within the unit interval", "coverageThreshold");
    }
    if (stars.size() > kMaxStarsPerFrame)
    {
        return failure(kEllipseOperation, ErrorCode::LimitExceeded, "more stars than one frame may carry");
    }
    const Status destinationReady = checkDestination(kEllipseOperation, sampleType, destination, destinationLayout);
    if (destinationReady.failed())
    {
        return destinationReady;
    }
    const Status cleared = clearDestination(execution, sampleType, destination, destinationLayout);
    if (cleared.failed())
    {
        return cleared;
    }

    const std::uint32_t width = destinationLayout.extent().width();
    const std::uint32_t height = destinationLayout.extent().height();
    const double subStep = 1.0 / static_cast<double>(kCoverageLattice);
    const double subOrigin = 0.5 * subStep;
    StarMaskResult result{};
    std::atomic<std::uint64_t> covered{0u};
    const Status painted = parallelFor(
        execution, height,
        TaskCost{.bytesPerItem = static_cast<std::uint64_t>(width) * destinationLayout.elementSizeBytes(),
                 .operationsPerItem = static_cast<std::uint64_t>(width) * (stars.empty() ? 1u : stars.size())},
        [&](TaskRange rows) {
            std::uint64_t rowCovered = 0u;
            for (std::uint64_t row = rows.begin; row < rows.end; ++row)
            {
                const Status running = execution.checkCancellation();
                if (running.failed())
                {
                    return running;
                }
                auto* const line = static_cast<std::byte*>(destination) + row * destinationLayout.rowStrideBytes();
                for (const DetectedStar& star : stars)
                {
                    // The ellipse: four times the major full width across, the axis
                    // ratio tall, turned by the negated position angle.
                    const double major = star.psf.fullWidthX * 4.0 * request.sizeScale;
                    if (!isFinitePositive(major))
                    {
                        continue;
                    }
                    const double ratio = star.psf.fullWidthX > 0.0 ? star.psf.fullWidthY / star.psf.fullWidthX : 1.0;
                    const double minor = major * ratio;
                    if (!isFinitePositive(minor))
                    {
                        continue;
                    }
                    const double semiMajor = 0.5 * major;
                    const double semiMinor = 0.5 * minor;
                    const double angle = -star.psf.angleDegrees * std::numbers::pi / 180.0;
                    const double cosine = std::cos(angle);
                    const double sine = std::sin(angle);
                    const double reach = semiMajor;
                    const PaintBounds bounds =
                        boundsOf(star.positionX - 0.5, star.positionY - 0.5, reach, reach, width, height);
                    if (static_cast<std::int64_t>(row) < bounds.top || static_cast<std::int64_t>(row) > bounds.bottom)
                    {
                        continue;
                    }
                    for (std::int64_t column = bounds.left; column <= bounds.right; ++column)
                    {
                        std::uint32_t inside = 0u;
                        for (std::uint32_t subRow = 0u; subRow < kCoverageLattice; ++subRow)
                        {
                            const double sampleY = static_cast<double>(row) + subOrigin +
                                                   static_cast<double>(subRow) * subStep - star.positionY;
                            for (std::uint32_t subColumn = 0u; subColumn < kCoverageLattice; ++subColumn)
                            {
                                const double sampleX = static_cast<double>(column) + subOrigin +
                                                       static_cast<double>(subColumn) * subStep - star.positionX;
                                const double alongMajor = cosine * sampleX + sine * sampleY;
                                const double alongMinor = -sine * sampleX + cosine * sampleY;
                                const double normalised = (alongMajor * alongMajor) / (semiMajor * semiMajor) +
                                                          (alongMinor * alongMinor) / (semiMinor * semiMinor);
                                if (normalised <= 1.0)
                                {
                                    ++inside;
                                }
                            }
                        }
                        const double coverage =
                            static_cast<double>(inside) / static_cast<double>(kCoverageLattice * kCoverageLattice);
                        if (coverage < request.coverageThreshold || coverage <= 0.0)
                        {
                            continue;
                        }
                        if (sampleType == SampleType::Float32)
                        {
                            raiseTo<float>(line, static_cast<std::uint64_t>(column), 1.0);
                        }
                        else
                        {
                            raiseTo<double>(line, static_cast<std::uint64_t>(column), 1.0);
                        }
                    }
                }
                for (std::uint32_t column = 0u; column < width; ++column)
                {
                    const double value = sampleType == SampleType::Float32
                                             ? static_cast<double>(reinterpret_cast<const float*>(line)[column])
                                             : reinterpret_cast<const double*>(line)[column];
                    if (value > 0.0)
                    {
                        ++rowCovered;
                    }
                }
            }
            covered.fetch_add(rowCovered, std::memory_order_relaxed);
            return Status::success();
        });
    if (painted.failed())
    {
        return painted;
    }

    for (const DetectedStar& star : stars)
    {
        const double major = star.psf.fullWidthX * 4.0 * request.sizeScale;
        const double ratio = star.psf.fullWidthX > 0.0 ? star.psf.fullWidthY / star.psf.fullWidthX : 1.0;
        if (isFinitePositive(major) && isFinitePositive(major * ratio))
        {
            ++result.paintedStars;
        }
        else
        {
            ++result.skippedStars;
        }
    }
    result.coveredSamples = covered.load(std::memory_order_relaxed);
    return result;
}

} // namespace an
