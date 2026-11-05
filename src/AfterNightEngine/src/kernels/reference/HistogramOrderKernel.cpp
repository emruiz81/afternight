// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the scalar oracles of the histogram and order-statistic kernels.
//
// Both walk one Float32 plane in row-major order on the calling thread. The histogram maps a
// selected value `v` in `[lowest, highest]` to bin `floor((v - lowest) / (highest - lowest) * n)`,
// in Float64, with the highest endpoint and any rounding past the last bin placed in the last bin;
// a not-a-number and a value outside the interval are counted beside the bins instead.
//
// The order statistic copies every selected sample into the caller's scratch and insertion-sorts
// it under `reference_detail::totalFloatLess`, which puts numbers before not-a-numbers and breaks
// ties between equal values by their bits. That is quadratic in the selected count; the plane
// primitives in `algorithms/histogram` select in bounded memory instead.

#include <AfterNightEngine/Algorithms/Kernel/HistogramOrderKernel.h>

#include "kernels/reference/ReferenceKernelInternal.h"

#include <AfterNightEngine/Image/ValidityDescriptor.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace an
{

Result<HistogramKernelResult> referenceHistogramKernel(const ExecutionContext& execution, PlaneView<const float> source,
                                                       std::span<std::uint64_t> bins,
                                                       const HistogramKernelParameters& parameters,
                                                       const KernelValidityView& validity)
{
    constexpr const char* kOperation = "referenceHistogramKernel";
    Status status = reference_detail::checkInput(execution, source, validity, kOperation);
    if (status.failed())
    {
        return status;
    }
    if (bins.empty())
    {
        return reference_detail::kernelFailure(ErrorCode::InvalidArgument, kOperation,
                                               "a histogram needs at least one bin");
    }
    if (!std::isfinite(parameters.lowest) || !std::isfinite(parameters.highest) ||
        parameters.highest <= parameters.lowest)
    {
        return reference_detail::kernelFailure(ErrorCode::InvalidArgument, kOperation,
                                               "the histogram interval must be finite and increasing");
    }
    if (regionsOverlap(reinterpret_cast<const std::uint8_t*>(source.data()), source.layout().totalBytes(),
                       reinterpret_cast<const std::uint8_t*>(bins.data()),
                       static_cast<std::uint64_t>(bins.size_bytes())))
    {
        return reference_detail::kernelFailure(ErrorCode::InvalidArgument, kOperation,
                                               "the histogram bins overlap the source plane");
    }
    for (std::uint64_t& bin : bins)
    {
        bin = 0u;
    }

    const reference_detail::ValidityAccessor accepts(validity);
    HistogramKernelResult result;
    const double width = parameters.highest - parameters.lowest;
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
            const double value = static_cast<double>(input[x]);
            if (std::isnan(value))
            {
                ++result.notANumber;
                continue;
            }
            if (value < parameters.lowest)
            {
                ++result.belowRange;
                continue;
            }
            if (value > parameters.highest)
            {
                ++result.aboveRange;
                continue;
            }
            std::size_t index = bins.size() - 1u;
            if (value < parameters.highest)
            {
                const std::size_t calculated =
                    static_cast<std::size_t>(((value - parameters.lowest) / width) * static_cast<double>(bins.size()));
                index = (std::min)(calculated, bins.size() - 1u);
            }
            ++bins[index];
            ++result.run.outputsWritten;
        }
    }
    status = requireSelection(result.run.selection, kOperation);
    if (status.failed())
    {
        return status;
    }
    return result;
}

Result<OrderStatisticKernelResult> referenceOrderStatisticKernel(const ExecutionContext& execution,
                                                                 PlaneView<const float> source, std::uint64_t rank,
                                                                 std::span<float> scratch,
                                                                 const KernelValidityView& validity)
{
    constexpr const char* kOperation = "referenceOrderStatisticKernel";
    Status status = reference_detail::checkInput(execution, source, validity, kOperation);
    if (status.failed())
    {
        return status;
    }
    if (static_cast<std::uint64_t>(scratch.size()) < source.sampleCount())
    {
        return reference_detail::kernelFailure(ErrorCode::IncompatibleArguments, kOperation,
                                               "the caller-owned order-statistic scratch is smaller than the input");
    }
    if (regionsOverlap(source.data(), source.layout().totalBytes(), scratch.data(),
                       static_cast<std::uint64_t>(scratch.size_bytes())))
    {
        return reference_detail::kernelFailure(ErrorCode::InvalidArgument, kOperation,
                                               "the order-statistic scratch overlaps the source plane");
    }

    const reference_detail::ValidityAccessor accepts(validity);
    OrderStatisticKernelResult result;
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
            scratch[static_cast<std::size_t>(result.scratchUsed)] = input[x];
            ++result.scratchUsed;
            ++result.run.selection.valid;
        }
    }
    status = requireSelection(result.run.selection, kOperation);
    if (status.failed())
    {
        return status;
    }
    if (rank >= result.scratchUsed)
    {
        return reference_detail::kernelFailure(ErrorCode::OutOfRange, kOperation,
                                               "the requested order-statistic rank is outside the selected samples");
    }

    std::uint64_t movesSinceCheckpoint = 0u;
    for (std::uint64_t index = 1u; index < result.scratchUsed; ++index)
    {
        status = reference_detail::checkpoint(execution);
        if (status.failed())
        {
            return status;
        }
        const float held = scratch[static_cast<std::size_t>(index)];
        std::uint64_t position = index;
        while (position > 0u &&
               reference_detail::totalFloatLess(held, scratch[static_cast<std::size_t>(position - 1u)]))
        {
            scratch[static_cast<std::size_t>(position)] = scratch[static_cast<std::size_t>(position - 1u)];
            --position;
            ++movesSinceCheckpoint;
            if (movesSinceCheckpoint == 256u)
            {
                status = reference_detail::checkpoint(execution);
                if (status.failed())
                {
                    return status;
                }
                movesSinceCheckpoint = 0u;
            }
        }
        scratch[static_cast<std::size_t>(position)] = held;
    }
    result.value = scratch[static_cast<std::size_t>(rank)];
    result.run.outputsWritten = 1u;
    return result;
}

} // namespace an
