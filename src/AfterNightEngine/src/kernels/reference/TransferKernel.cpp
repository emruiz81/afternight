// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the scalar oracle of the transfer kernel.
//
// The simplest kernel there is, and the one whose checks the others are described against: the
// execution, the two planes, the mask and the overlap, in that order, then one row-major walk on the
// calling thread that copies every selected sample and leaves every rejected one's destination as
// it was. Exact in-place is accepted and any other overlap refused, and a stop is checked once per
// row.

#include <AfterNightEngine/Algorithms/Kernel/TransferKernel.h>

#include "kernels/reference/ReferenceKernelInternal.h"

#include <cstdint>
#include <span>

namespace an
{

Result<KernelRunResult> referenceTransferKernel(const ExecutionContext& execution, PlaneView<const float> source,
                                                PlaneView<float> destination, const KernelValidityView& validity)
{
    constexpr const char* kOperation = "referenceTransferKernel";
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
            output[x] = input[x];
            ++result.outputsWritten;
        }
    }
    return result;
}

} // namespace an
