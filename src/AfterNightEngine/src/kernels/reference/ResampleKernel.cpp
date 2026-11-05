// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the scalar oracles of the resample kernels.
//
// A resample reads one source coordinate per destination sample, in row-major destination order,
// through `reference_detail::sampleInterpolated` - the one interpolated read the parallel resampler
// shares - so its answers are the resampler's by construction. The whole coordinate list is checked
// before anything is written, and a destination sample whose read the invalid-sample policy refuses
// is left as it was. The source and destination may not overlap at all, because a later read could
// land on an earlier write. The nearest-neighbour entry point only narrows the border choice to
// clamp or constant and forwards.

#include <AfterNightEngine/Algorithms/Kernel/ResampleKernel.h>

#include "kernels/reference/ReferenceKernelInternal.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace an
{

Result<KernelRunResult> referenceResampleNearestKernel(const ExecutionContext& execution, PlaneView<const float> source,
                                                       PlaneView<float> destination,
                                                       std::span<const KernelCoordinate> coordinates,
                                                       const NearestKernelParameters& parameters,
                                                       const KernelValidityView& validity)
{
    if (parameters.border != KernelBorderMode::Clamp && parameters.border != KernelBorderMode::Constant)
    {
        return reference_detail::kernelFailure(ErrorCode::InvalidArgument, "referenceResampleNearestKernel",
                                               "nearest resample accepts only clamp or constant borders");
    }
    return referenceResampleKernel(execution, source, destination, coordinates,
                                   SamplingKernelParameters{.interpolation = KernelInterpolation::Nearest,
                                                            .border = parameters.border,
                                                            .constantBorder = parameters.constantBorder},
                                   validity);
}

Result<KernelRunResult> referenceResampleKernel(const ExecutionContext& execution, PlaneView<const float> source,
                                                PlaneView<float> destination,
                                                std::span<const KernelCoordinate> coordinates,
                                                const SamplingKernelParameters& parameters,
                                                const KernelValidityView& validity)
{
    constexpr const char* kOperation = "referenceResampleKernel";
    Status status = reference_detail::checkInput(execution, source, validity, kOperation);
    if (status.failed())
    {
        return status;
    }
    status = reference_detail::checkSingleChannel(destination, kOperation);
    if (status.failed())
    {
        return status;
    }
    if (coordinates.size() != destination.sampleCount())
    {
        return reference_detail::kernelFailure(ErrorCode::IncompatibleArguments, kOperation,
                                               "the resample coordinate count does not equal the output sample count");
    }
    status = reference_detail::checkSamplingParameters(parameters, kOperation);
    if (status.failed())
    {
        return status;
    }
    status = reference_detail::checkCoordinates(execution, coordinates, kOperation);
    if (status.failed())
    {
        return status;
    }
    status = reference_detail::checkOverlap(source, destination, false, kOperation);
    if (status.failed())
    {
        return status;
    }

    const reference_detail::SamplingPlan plan = reference_detail::planSampling(source, validity, parameters);
    KernelRunResult result;
    std::size_t coordinateIndex = 0u;
    for (std::uint32_t outputY = 0u; outputY < destination.height(); ++outputY)
    {
        status = reference_detail::checkpoint(execution);
        if (status.failed())
        {
            return status;
        }
        const std::span<float> output = *destination.row(outputY);
        for (std::uint32_t outputX = 0u; outputX < destination.width(); ++outputX, ++coordinateIndex)
        {
            ++result.selection.total;
            float value = 0.0f;
            if (!reference_detail::sampleInterpolated(plan, coordinates[coordinateIndex], value))
            {
                continue;
            }
            ++result.selection.valid;
            output[outputX] = value;
            ++result.outputsWritten;
        }
    }
    return result;
}

} // namespace an
