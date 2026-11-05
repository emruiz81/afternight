// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the scalar oracle of the sum reduction.
//
// The selected samples of one Float32 plane are added in Float64, one at a time in row-major order,
// on the calling thread. The parallel primitives in `algorithms/reduction` merge chunks through a
// pairwise tree instead, so their sums need not be this sum's bits. A plane whose rule selects
// nothing is refused with `ErrorCode::EmptySelection` rather than answered with zero, and a stop is
// checked once per row.

#include <AfterNightEngine/Algorithms/Kernel/ReductionKernel.h>

#include "kernels/reference/ReferenceKernelInternal.h"

#include <AfterNightEngine/Image/ValidityDescriptor.h>

#include <cstdint>
#include <span>

namespace an
{

Result<ReductionKernelResult> referenceSumKernel(const ExecutionContext& execution, PlaneView<const float> source,
                                                 const KernelValidityView& validity)
{
    constexpr const char* kOperation = "referenceSumKernel";
    Status status = reference_detail::checkInput(execution, source, validity, kOperation);
    if (status.failed())
    {
        return status;
    }

    const reference_detail::ValidityAccessor accepts(validity);
    ReductionKernelResult result;
    for (std::uint32_t y = 0u; y < source.height(); ++y)
    {
        status = reference_detail::checkpoint(execution);
        if (status.failed())
        {
            return status;
        }
        const std::span<const float> input = *source.row(y);
        for (std::uint32_t x = 0u; x < source.width(); ++x)
        {
            ++result.run.selection.total;
            if (!accepts.accepts(input[x], x, y))
            {
                continue;
            }
            ++result.run.selection.valid;
            result.sum += static_cast<double>(input[x]);
        }
    }
    status = requireSelection(result.run.selection, kOperation);
    if (status.failed())
    {
        return status;
    }
    result.run.outputsWritten = 1u;
    return result;
}

} // namespace an
