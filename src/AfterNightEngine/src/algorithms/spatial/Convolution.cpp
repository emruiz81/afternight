// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Algorithms/Spatial/Convolution.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "kernels/reference/SpatialPlane.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

// The image half of the stencil family. What this file is, is an outer loop and a set of refusals:
// the arithmetic lives in `Algorithms/Kernel/StencilKernel.h`, and everything here decides which
// plane the kernel sees and what happens to the answer afterwards.
//
// The one piece of arithmetic that does live here is the rank-one factorization, and it is here
// rather than in a kernel because it reads a kernel rather than an image: it is the question a
// caller asks before choosing between the two entry points, and answering it costs the size of the
// kernel rather than the size of the picture.

namespace an
{
namespace
{

using spatial_detail::filterEveryChannel;
using spatial_detail::spatialFailure;
using spatial_detail::SpatialGeometry;
using spatial_detail::spatialLimitFailure;

constexpr const char* kSeparableOperation = "an::convolveImageSeparable";
constexpr const char* kDenseOperation = "an::convolveImageDense";
constexpr const char* kSeparationOperation = "an::separateConvolutionKernel";

/// Records what one plane convolution reported into the image-level result.
///
/// @param plan what the kernel did to one channel.
/// @param passes how many image passes that method costs.
/// @param result the accumulating image-level answer.
void accumulate(const StencilExecutionResult& plan, std::uint64_t passes, SpatialFilterResult& result) noexcept
{
    result.run.selection.total += plan.run.selection.total;
    result.run.selection.valid += plan.run.selection.valid;
    result.run.outputsWritten += plan.run.outputsWritten;
    // The published radii are stated in pixels rather than in taps, so a dilated pass reports the
    // footprint it covered and not the number of coefficients it covered it with.
    result.horizontalRadius = plan.horizontalRadius * plan.dilation;
    result.verticalRadius = plan.verticalRadius * plan.dilation;
    result.imagePasses += passes;
    result.plannedWorkers = (std::max)(result.plannedWorkers, plan.plannedWorkers);
    result.precision = ComputePrecision::Float32;
}

} // namespace

double convolutionTapSum(std::span<const double> taps) noexcept
{
    double sum = 0.0;
    for (const double tap : taps)
    {
        sum += tap;
    }
    return sum;
}

Result<ConvolutionSeparation> separateConvolutionKernel(std::span<const double> coefficients, std::uint32_t width,
                                                        std::uint32_t height, double tolerance,
                                                        std::span<double> horizontalTaps,
                                                        std::span<double> verticalTaps)
{
    if (width == 0u || height == 0u || width % 2u == 0u || height % 2u == 0u)
    {
        return spatialFailure(ErrorCode::InvalidArgument, kSeparationOperation,
                              "a kernel extent names a centre and an equal number of samples either side of it, so "
                              "it is odd and non-zero");
    }
    if (width > kMaxStencilTaps || height > kMaxStencilTaps)
    {
        return spatialLimitFailure(ErrorCode::OutOfRange, kSeparationOperation,
                                   "a kernel extent is wider than a separable pass may be", (std::max)(width, height),
                                   kMaxStencilTaps);
    }
    const std::uint64_t sampleCount = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
    if (sampleCount > kMaxStencilKernelSamples)
    {
        return spatialLimitFailure(ErrorCode::OutOfRange, kSeparationOperation,
                                   "a dense kernel holds more coefficients than one may", sampleCount,
                                   kMaxStencilKernelSamples);
    }
    if (coefficients.size() != sampleCount)
    {
        return spatialFailure(ErrorCode::IncompatibleArguments, kSeparationOperation,
                              "the coefficient count is not the product of the two extents");
    }
    if (horizontalTaps.size() != width || verticalTaps.size() != height)
    {
        return spatialFailure(ErrorCode::IncompatibleArguments, kSeparationOperation,
                              "each output array must be exactly as long as its own extent");
    }
    if (!std::isfinite(tolerance) || tolerance < 0.0)
    {
        return spatialFailure(ErrorCode::InvalidArgument, kSeparationOperation,
                              "a tolerance is a finite distance and is not negative");
    }
    for (const double coefficient : coefficients)
    {
        if (!std::isfinite(coefficient))
        {
            return spatialFailure(ErrorCode::InvalidArgument, kSeparationOperation,
                                  "a kernel coefficient that is not finite has no rank-one approximation");
        }
    }

    ConvolutionSeparation separation{};
    separation.horizontalTapCount = width;
    separation.verticalTapCount = height;
    separation.coefficientSum = convolutionTapSum(coefficients);

    std::uint32_t pivotRow = 0u;
    std::uint32_t pivotColumn = 0u;
    double pivot = 0.0;
    for (std::uint32_t row = 0u; row < height; ++row)
    {
        for (std::uint32_t column = 0u; column < width; ++column)
        {
            const double value = coefficients[static_cast<std::size_t>(row) * width + column];
            if (std::abs(value) > std::abs(pivot))
            {
                pivot = value;
                pivotRow = row;
                pivotColumn = column;
            }
        }
    }

    if (pivot == 0.0)
    {
        // Every coefficient is zero, so the kernel is the outer product of two zero passes and the
        // approximation is exact. Saying so is better than dividing by the pivot and reporting a
        // not-a-number residual for a kernel that separates perfectly.
        std::fill(horizontalTaps.begin(), horizontalTaps.end(), 0.0);
        std::fill(verticalTaps.begin(), verticalTaps.end(), 0.0);
        separation.separable = true;
        separation.residual = 0.0;
        return separation;
    }

    for (std::uint32_t column = 0u; column < width; ++column)
    {
        horizontalTaps[column] = coefficients[static_cast<std::size_t>(pivotRow) * width + column];
    }
    for (std::uint32_t row = 0u; row < height; ++row)
    {
        verticalTaps[row] = coefficients[static_cast<std::size_t>(row) * width + pivotColumn] / pivot;
    }

    double residual = 0.0;
    for (std::uint32_t row = 0u; row < height; ++row)
    {
        for (std::uint32_t column = 0u; column < width; ++column)
        {
            const double approximated = verticalTaps[row] * horizontalTaps[column];
            const double difference =
                std::abs(coefficients[static_cast<std::size_t>(row) * width + column] - approximated);
            residual = (std::max)(residual, difference);
        }
    }
    separation.residual = residual;
    separation.separable = residual <= tolerance;
    return separation;
}

Result<SpatialFilterResult> convolveImageSeparable(const ExecutionContext& execution, SampleType sampleType,
                                                   const void* source, const ImageLayout& sourceLayout,
                                                   void* destination, const ImageLayout& destinationLayout,
                                                   const SeparableConvolutionRequest& request,
                                                   const SpatialSelection& selection)
{
    SpatialGeometry geometry{};
    const Status ready =
        spatial_detail::checkSpatialImages(execution, sampleType, source, sourceLayout, destination, destinationLayout,
                                           request.output, selection, kSeparableOperation, geometry);
    if (ready.failed())
    {
        return ready;
    }

    SpatialFilterResult result{};
    std::uint64_t kernelScratchBytes = 0u;
    const SeparableStencilRequest pass{.horizontalTaps = request.horizontalTaps,
                                       .verticalTaps = request.verticalTaps,
                                       .borders = request.borders,
                                       .dilation = request.dilation};
    const Status filtered = filterEveryChannel(
        execution, sampleType, source, sourceLayout, destination, destinationLayout, selection,
        request.output.writeBack, geometry, kSeparableOperation, result,
        [&](PlaneView<const float> input, PlaneView<float> output, const KernelValidityView& validity) -> Status {
            const Result<StencilExecutionResult> plan =
                convolveSeparablePlane(execution, input, output, pass, validity);
            if (!plan.hasValue())
            {
                return plan.status();
            }
            accumulate(plan.value(), 2u, result);
            kernelScratchBytes = (std::max)(kernelScratchBytes, plan.value().scratchBytes);
            return Status::success();
        });
    if (filtered.failed())
    {
        return filtered;
    }
    result.workingBytes += kernelScratchBytes;
    return result;
}

Result<SpatialFilterResult> convolveImageDense(const ExecutionContext& execution, SampleType sampleType,
                                               const void* source, const ImageLayout& sourceLayout, void* destination,
                                               const ImageLayout& destinationLayout,
                                               const DenseConvolutionRequest& request,
                                               const SpatialSelection& selection)
{
    SpatialGeometry geometry{};
    const Status ready =
        spatial_detail::checkSpatialImages(execution, sampleType, source, sourceLayout, destination, destinationLayout,
                                           request.output, selection, kDenseOperation, geometry);
    if (ready.failed())
    {
        return ready;
    }

    SpatialFilterResult result{};
    const DenseStencilRequest pass{.coefficients = request.coefficients,
                                   .width = request.width,
                                   .height = request.height,
                                   .borders = request.borders};
    const Status filtered = filterEveryChannel(
        execution, sampleType, source, sourceLayout, destination, destinationLayout, selection,
        request.output.writeBack, geometry, kDenseOperation, result,
        [&](PlaneView<const float> input, PlaneView<float> output, const KernelValidityView& validity) -> Status {
            const Result<StencilExecutionResult> plan = convolveDensePlane(execution, input, output, pass, validity);
            if (!plan.hasValue())
            {
                return plan.status();
            }
            accumulate(plan.value(), 1u, result);
            return Status::success();
        });
    if (filtered.failed())
    {
        return filtered;
    }
    return result;
}

} // namespace an
