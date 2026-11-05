// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the scalar oracles of the gather and scatter kernels.
//
// A gather is a loop over its coordinates through `reference_detail::sampleInterpolated`, the one
// interpolated read every sampler in this engine shares, so its answers are the resampler's by
// construction. The nearest-neighbour entry point only narrows the border choice and forwards.
//
// A scatter walks its source in row-major order and decides a conflict by asking whether an earlier
// selected input rounded to the same destination pixel. That search compares each selected input
// with every one before it, so the oracle is quadratic in the sample count; `scatterPlane` groups
// the inputs by destination instead, keeping source order within a group. A rejecting policy runs
// the whole search once before anything is written, so its refusal leaves the destination
// untouched.

#include <AfterNightEngine/Algorithms/Kernel/GatherScatterKernel.h>

#include "kernels/reference/ReferenceKernelInternal.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace an
{

Result<KernelRunResult> referenceGatherKernel(const ExecutionContext& execution, PlaneView<const float> source,
                                              std::span<const KernelCoordinate> coordinates,
                                              std::span<float> destination, const NearestKernelParameters& parameters,
                                              const KernelValidityView& validity)
{
    if (parameters.border != KernelBorderMode::Clamp && parameters.border != KernelBorderMode::Constant)
    {
        return reference_detail::kernelFailure(ErrorCode::InvalidArgument, "referenceGatherKernel",
                                               "nearest gather accepts only clamp or constant borders");
    }
    return referenceGatherInterpolatedKernel(execution, source, coordinates, destination,
                                             SamplingKernelParameters{.interpolation = KernelInterpolation::Nearest,
                                                                      .border = parameters.border,
                                                                      .constantBorder = parameters.constantBorder},
                                             validity);
}

Result<KernelRunResult>
referenceGatherInterpolatedKernel(const ExecutionContext& execution, PlaneView<const float> source,
                                  std::span<const KernelCoordinate> coordinates, std::span<float> destination,
                                  const SamplingKernelParameters& parameters, const KernelValidityView& validity)
{
    constexpr const char* kOperation = "referenceGatherInterpolatedKernel";
    Status status = reference_detail::checkInput(execution, source, validity, kOperation);
    if (status.failed())
    {
        return status;
    }
    if (destination.size() < coordinates.size())
    {
        return reference_detail::kernelFailure(ErrorCode::IncompatibleArguments, kOperation,
                                               "the gather destination is shorter than the coordinate run");
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
    if (regionsOverlap(source.data(), source.layout().totalBytes(), destination.data(),
                       static_cast<std::uint64_t>(destination.size_bytes())))
    {
        return reference_detail::kernelFailure(ErrorCode::InvalidArgument, kOperation,
                                               "the gather destination overlaps its source plane");
    }

    const reference_detail::SamplingPlan plan = reference_detail::planSampling(source, validity, parameters);
    KernelRunResult result;
    for (std::size_t index = 0u; index < coordinates.size(); ++index)
    {
        if ((index & 0xFFu) == 0u)
        {
            status = reference_detail::checkpoint(execution);
            if (status.failed())
            {
                return status;
            }
        }
        ++result.selection.total;
        float value = 0.0f;
        if (!reference_detail::sampleInterpolated(plan, coordinates[index], value))
        {
            continue;
        }
        ++result.selection.valid;
        destination[index] = value;
        ++result.outputsWritten;
    }
    return result;
}

Result<ScatterKernelResult> referenceScatterKernel(const ExecutionContext& execution, PlaneView<const float> source,
                                                   std::span<const KernelCoordinate> coordinates,
                                                   PlaneView<float> destination,
                                                   const ScatterKernelParameters& parameters,
                                                   const KernelValidityView& validity)
{
    constexpr const char* kOperation = "referenceScatterKernel";
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
    if (coordinates.size() != source.sampleCount())
    {
        return reference_detail::kernelFailure(ErrorCode::IncompatibleArguments, kOperation,
                                               "the scatter coordinate count does not equal the source sample count");
    }
    if (!isScatterConflictPolicy(parameters.conflicts) || !isScatterBoundsPolicy(parameters.bounds))
    {
        return reference_detail::kernelFailure(ErrorCode::InvalidArgument, kOperation,
                                               "a scatter policy is outside its enumeration");
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

    const reference_detail::ValidityAccessor accepts(validity);

    if (parameters.bounds == ScatterBoundsPolicy::Reject || parameters.conflicts == ScatterConflictPolicy::Reject)
    {
        for (std::uint64_t index = 0u; index < source.sampleCount(); ++index)
        {
            if ((index & 0xFFu) == 0u)
            {
                status = reference_detail::checkpoint(execution);
                if (status.failed())
                {
                    return status;
                }
            }
            std::uint32_t sourceX = 0u;
            std::uint32_t sourceY = 0u;
            const float value = reference_detail::sourceAt(source, index, sourceX, sourceY);
            if (!accepts.accepts(value, sourceX, sourceY))
            {
                continue;
            }
            const reference_detail::ResolvedCoordinate resolved = reference_detail::resolveCoordinate(
                coordinates[static_cast<std::size_t>(index)], destination.width(), destination.height(), false);
            if (resolved.outside)
            {
                if (parameters.bounds == ScatterBoundsPolicy::Reject)
                {
                    return reference_detail::kernelFailure(ErrorCode::OutOfRange, kOperation,
                                                           "a selected scatter coordinate is outside the destination");
                }
                continue;
            }
            if (parameters.conflicts == ScatterConflictPolicy::Reject)
            {
                bool conflict = false;
                status = reference_detail::findPriorScatterTarget(execution, source, coordinates, accepts, index,
                                                                  destination.width(), destination.height(), resolved,
                                                                  conflict);
                if (status.failed())
                {
                    return status;
                }
                if (conflict)
                {
                    return reference_detail::kernelFailure(ErrorCode::IncompatibleArguments, kOperation,
                                                           "two selected scatter inputs name the same destination");
                }
            }
        }
    }

    status = reference_detail::checkpoint(execution);
    if (status.failed())
    {
        return status;
    }
    ScatterKernelResult result;
    for (std::uint64_t index = 0u; index < source.sampleCount(); ++index)
    {
        if ((index & 0xFFu) == 0u)
        {
            status = reference_detail::checkpoint(execution);
            if (status.failed())
            {
                return status;
            }
        }
        std::uint32_t sourceX = 0u;
        std::uint32_t sourceY = 0u;
        const float value = reference_detail::sourceAt(source, index, sourceX, sourceY);
        ++result.run.selection.total;
        if (!accepts.accepts(value, sourceX, sourceY))
        {
            continue;
        }
        ++result.run.selection.valid;
        const reference_detail::ResolvedCoordinate resolved = reference_detail::resolveCoordinate(
            coordinates[static_cast<std::size_t>(index)], destination.width(), destination.height(), false);
        if (resolved.outside)
        {
            ++result.discarded;
            continue;
        }
        bool conflict = false;
        status =
            reference_detail::findPriorScatterTarget(execution, source, coordinates, accepts, index,
                                                     destination.width(), destination.height(), resolved, conflict);
        if (status.failed())
        {
            return status;
        }
        if (conflict)
        {
            ++result.conflicts;
            if (parameters.conflicts == ScatterConflictPolicy::First)
            {
                continue;
            }
        }
        std::span<float> output = *destination.row(resolved.y);
        if (parameters.conflicts == ScatterConflictPolicy::Add)
        {
            output[resolved.x] += value;
        }
        else
        {
            output[resolved.x] = value;
        }
        ++result.run.outputsWritten;
    }
    return result;
}

} // namespace an
