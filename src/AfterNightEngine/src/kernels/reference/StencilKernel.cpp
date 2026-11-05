// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the scalar oracle of the three-by-three stencil kernel.
//
// Every selected centre becomes the Float64 sum of its nine neighbours times their coefficients, in
// row-major neighbour order, narrowed once to Float32; a rejected centre leaves its destination
// sample as it was. A neighbour outside the plane reads the constant border or, under the clamp
// border, the nearest edge sample, and a neighbour inside the plane that the validity rule rejects -
// including a clamped one - reads the stated substitute instead. The source and destination may not
// overlap at all, because a later centre reads samples an earlier one would have overwritten.

#include <AfterNightEngine/Algorithms/Kernel/StencilKernel.h>

#include "kernels/reference/ReferenceKernelInternal.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace an
{

Result<KernelRunResult> referenceStencil3x3Kernel(const ExecutionContext& execution, PlaneView<const float> source,
                                                  PlaneView<float> destination,
                                                  const Stencil3x3KernelParameters& parameters,
                                                  const KernelValidityView& validity)
{
    constexpr const char* kOperation = "referenceStencil3x3Kernel";
    Status status = reference_detail::checkExecution(execution, kOperation);
    if (status.failed())
    {
        return status;
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
    status = reference_detail::checkOverlap(source, destination, false, kOperation);
    if (status.failed())
    {
        return status;
    }
    if ((parameters.border != KernelBorderMode::Clamp && parameters.border != KernelBorderMode::Constant) ||
        !std::isfinite(parameters.constantBorder) || !std::isfinite(parameters.invalidNeighbor))
    {
        return reference_detail::kernelFailure(ErrorCode::InvalidArgument, kOperation,
                                               "the stencil border and substitute values must be known and finite");
    }
    for (const double coefficient : parameters.coefficients)
    {
        if (!std::isfinite(coefficient))
        {
            return reference_detail::kernelFailure(ErrorCode::InvalidArgument, kOperation,
                                                   "a stencil coefficient is not finite");
        }
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
        const std::span<const float> centerRow = *source.row(y);
        const std::span<float> output = *destination.row(y);
        for (std::uint32_t x = 0u; x < source.width(); ++x)
        {
            ++result.selection.total;
            if (!accepts.accepts(centerRow[x], x, y))
            {
                continue;
            }
            ++result.selection.valid;
            double sum = 0.0;
            std::size_t coefficient = 0u;
            for (std::int32_t offsetY = -1; offsetY <= 1; ++offsetY)
            {
                for (std::int32_t offsetX = -1; offsetX <= 1; ++offsetX, ++coefficient)
                {
                    const std::int64_t candidateX = static_cast<std::int64_t>(x) + offsetX;
                    const std::int64_t candidateY = static_cast<std::int64_t>(y) + offsetY;
                    float neighbor = parameters.constantBorder;
                    if (candidateX >= 0 && candidateY >= 0 && candidateX < static_cast<std::int64_t>(source.width()) &&
                        candidateY < static_cast<std::int64_t>(source.height()))
                    {
                        const std::uint32_t neighborX = static_cast<std::uint32_t>(candidateX);
                        const std::uint32_t neighborY = static_cast<std::uint32_t>(candidateY);
                        neighbor = (*source.row(neighborY))[neighborX];
                        if (!accepts.accepts(neighbor, neighborX, neighborY))
                        {
                            neighbor = parameters.invalidNeighbor;
                        }
                    }
                    else if (parameters.border == KernelBorderMode::Clamp)
                    {
                        const std::uint32_t neighborX = candidateX < 0
                                                            ? 0u
                                                            : (candidateX >= static_cast<std::int64_t>(source.width())
                                                                   ? source.width() - 1u
                                                                   : static_cast<std::uint32_t>(candidateX));
                        const std::uint32_t neighborY = candidateY < 0
                                                            ? 0u
                                                            : (candidateY >= static_cast<std::int64_t>(source.height())
                                                                   ? source.height() - 1u
                                                                   : static_cast<std::uint32_t>(candidateY));
                        neighbor = (*source.row(neighborY))[neighborX];
                        if (!accepts.accepts(neighbor, neighborX, neighborY))
                        {
                            neighbor = parameters.invalidNeighbor;
                        }
                    }
                    sum += static_cast<double>(neighbor) * parameters.coefficients[coefficient];
                }
            }
            output[x] = static_cast<float>(sum);
            ++result.outputsWritten;
        }
    }
    return result;
}

} // namespace an
