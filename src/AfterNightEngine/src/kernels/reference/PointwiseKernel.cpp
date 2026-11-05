// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the scalar oracle of the pointwise affine kernel.
//
// One Float32 plane is walked in row-major order on the calling thread. A selected sample is
// widened to Float64, scaled, biased and narrowed once; a rejected one leaves its destination
// sample as it was. Because each sample is read before it is written, an exact in-place call is
// accepted and any other overlap is refused. The coefficients are checked right after the
// execution, then the planes, the mask and the overlap, and a stop is checked once per row.

#include <AfterNightEngine/Algorithms/Kernel/PointwiseKernel.h>

#include "kernels/reference/ReferenceKernelInternal.h"

#include <cmath>
#include <cstdint>
#include <span>

namespace an
{

Result<KernelRunResult> referencePointwiseAffineKernel(const ExecutionContext& execution, PlaneView<const float> source,
                                                       PlaneView<float> destination,
                                                       const AffineKernelParameters& parameters,
                                                       const KernelValidityView& validity)
{
    constexpr const char* kOperation = "referencePointwiseAffineKernel";
    Status status = reference_detail::checkExecution(execution, kOperation);
    if (status.failed())
    {
        return status;
    }
    if (!std::isfinite(parameters.scale) || !std::isfinite(parameters.bias))
    {
        return reference_detail::kernelFailure(ErrorCode::InvalidArgument, kOperation,
                                               "the affine coefficients must be finite");
    }
    status = reference_detail::checkPair(source, destination, kOperation);
    if (status.failed())
    {
        return status;
    }
    status = reference_detail::checkValidity(validity, source.width(), source.height(), kOperation);
    if (status.failed())
    {
        return status;
    }
    status = reference_detail::checkOverlap(source, destination, true, kOperation);
    if (status.failed())
    {
        return status;
    }

    const reference_detail::ValidityAccessor accepts(validity);
    KernelRunResult result;
    for (std::uint32_t y = 0u; y < source.height(); ++y)
    {
        status = reference_detail::checkpoint(execution);
        if (status.failed())
        {
            return status;
        }
        const std::span<const float> input = *source.row(y);
        const std::span<float> output = *destination.row(y);
        for (std::uint32_t x = 0u; x < source.width(); ++x)
        {
            ++result.selection.total;
            if (!accepts.accepts(input[x], x, y))
            {
                continue;
            }
            ++result.selection.valid;
            output[x] = static_cast<float>(static_cast<double>(input[x]) * parameters.scale + parameters.bias);
            ++result.outputsWritten;
        }
    }
    return result;
}

} // namespace an
