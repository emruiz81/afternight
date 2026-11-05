// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the parallel resample, gather and scatter of one Float32 plane.
//
// These are the parallel entry points whose scalar oracles live beside the reference kernels. Both
// sides sample through the same `reference_detail::planSampling` and `sampleInterpolated`, so a
// resampled or gathered value is the oracle's value whatever thread computed it: every output
// depends on its own coordinate alone, and the parallel tier only divides the outputs among workers.
//
// A scatter is where threads could disagree, because two inputs may name one destination. Rather
// than the oracle's comparison of each input with every earlier one, the selected inputs are sorted
// by destination index with a stable least-significant-digit radix sort - eight passes of one byte -
// which keeps row-major source order inside each destination, and each run of equal destinations is
// then written by one worker in that order. First, Last and Add therefore give the oracle's answer
// in linear time. The scratch is two arrays of sixteen bytes per source sample, and the second is
// reused for the group list once the sort is done; the partition and the grouping run on the calling
// thread, and only the writes are parallel.

#include <AfterNightEngine/Algorithms/Kernel/GatherScatterKernel.h>
#include <AfterNightEngine/Algorithms/Kernel/ResampleKernel.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>
#include <AfterNightEngine/Runtime/ParallelExecutor.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "kernels/reference/ReferenceKernelInternal.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <utility>

namespace an
{
namespace
{

/// Roughly how many operations one output of each interpolation costs, for the tile planner: an
/// order of magnitude that grows with the number of taps, not a measurement.
[[nodiscard]] std::uint64_t interpolationCost(KernelInterpolation interpolation) noexcept
{
    switch (interpolation)
    {
    case KernelInterpolation::Nearest:
    case KernelInterpolation::NearestExact:
        return 4u;
    case KernelInterpolation::Linear:
    case KernelInterpolation::Area:
    case KernelInterpolation::LinearExact:
        return 20u;
    case KernelInterpolation::Cubic:
        return 80u;
    case KernelInterpolation::Lanczos4:
        return 320u;
    }
    return 4u;
}

/// Returns one context-owned temporary allocation on every exit path.
class ScopedSamplingAllocation
{
public:
    /// Records an allocation already made by the resource.
    ///
    /// @param resource the resource that made the allocation.
    /// @param pointer the non-null allocation.
    /// @param bytes its requested size.
    /// @param alignment its requested alignment.
    ScopedSamplingAllocation(MemoryResource& resource, void* pointer, std::uint64_t bytes,
                             std::uint64_t alignment) noexcept
        : m_resource(&resource), m_pointer(pointer), m_bytes(bytes), m_alignment(alignment)
    {
    }

    /// Returns the allocation with its original size and alignment.
    ~ScopedSamplingAllocation()
    {
        m_resource->deallocate(m_pointer, m_bytes, m_alignment);
    }

    ScopedSamplingAllocation(const ScopedSamplingAllocation&) = delete;
    ScopedSamplingAllocation& operator=(const ScopedSamplingAllocation&) = delete;

private:
    /// Resource that owns the allocation.
    MemoryResource* m_resource;

    /// Allocation to return.
    void* m_pointer;

    /// Requested bytes.
    std::uint64_t m_bytes;

    /// Requested alignment.
    std::uint64_t m_alignment;
};

/// One selected scatter input, sorted first by target and then implicitly by source order.
struct ScatterEntry
{
    /// Row-major destination sample index.
    std::uint64_t target = 0u;

    /// Row-major source sample index.
    std::uint64_t source = 0u;
};

/// One run of entries sharing a destination.
struct ScatterGroup
{
    /// First entry in the stable sorted run.
    std::uint64_t begin = 0u;

    /// One past the last entry in the run.
    std::uint64_t end = 0u;
};

static_assert(sizeof(ScatterEntry) == sizeof(ScatterGroup));
static_assert(alignof(ScatterEntry) == alignof(ScatterGroup));

/// The coordinate finiteness preflight, run on the operation's workers.
///
/// Identical in outcome to the scalar one the reference kernels use: every coordinate is read
/// before the caller is told anything, so a refusal still happens before the first output write.
/// It is parallel because it reads the whole coordinate run - two doubles for every output sample -
/// and doing that on one thread made the preflight a serial fraction of an otherwise parallel
/// operation.
///
/// @param execution the operation whose workers run it.
/// @param coordinates the run to check.
/// @param operation the name a refusal carries.
/// @return success, `ErrorCode::InvalidArgument` when any coordinate is unusable, or a scheduler
///         or cancellation failure.
[[nodiscard]] Status checkCoordinatesInParallel(const ExecutionContext& execution,
                                                std::span<const KernelCoordinate> coordinates, const char* operation)
{
    std::atomic<bool> refused{false};
    const Status ran = parallelFor(
        execution, coordinates.size(), TaskCost{.bytesPerItem = 16u, .operationsPerItem = 4u}, [&](TaskRange range) {
            for (std::uint64_t index = range.begin; index < range.end; ++index)
            {
                if (!reference_detail::coordinateIsFinite(coordinates[static_cast<std::size_t>(index)]))
                {
                    // Recorded rather than returned: every refusal reads the same, so no range
                    // has to win a race to be the one that reports it.
                    refused.store(true, std::memory_order_relaxed);
                    return Status::success();
                }
            }
            return Status::success();
        });
    if (ran.failed())
    {
        return ran;
    }
    if (refused.load(std::memory_order_relaxed))
    {
        return reference_detail::kernelFailure(ErrorCode::InvalidArgument, operation,
                                               "a kernel coordinate is not finite or exactly representable");
    }
    return Status::success();
}

[[nodiscard]] Status validateSampling(const ExecutionContext& execution, PlaneView<const float> source,
                                      std::span<const KernelCoordinate> coordinates,
                                      const SamplingKernelParameters& parameters, const KernelValidityView& validity,
                                      const char* operation)
{
    Status status = reference_detail::checkInput(execution, source, validity, operation);
    if (status.failed())
    {
        return status;
    }
    status = reference_detail::checkSamplingParameters(parameters, operation);
    if (status.failed())
    {
        return status;
    }
    return checkCoordinatesInParallel(execution, coordinates, operation);
}

[[nodiscard]] Status validateResample(const ExecutionContext& execution, PlaneView<const float> source,
                                      PlaneView<float> destination, std::span<const KernelCoordinate> coordinates,
                                      const SamplingKernelParameters& parameters, const KernelValidityView& validity,
                                      const char* operation)
{
    Status status = validateSampling(execution, source, coordinates, parameters, validity, operation);
    if (status.failed())
    {
        return status;
    }
    status = reference_detail::checkSingleChannel(destination, operation);
    if (status.failed())
    {
        return status;
    }
    if (coordinates.size() != destination.sampleCount())
    {
        return reference_detail::kernelFailure(ErrorCode::IncompatibleArguments, operation,
                                               "the resample coordinate count does not equal the output sample count");
    }
    return reference_detail::checkOverlap(source, destination, false, operation);
}

[[nodiscard]] Status validateGather(const ExecutionContext& execution, PlaneView<const float> source,
                                    std::span<const KernelCoordinate> coordinates, std::span<float> destination,
                                    const SamplingKernelParameters& parameters, const KernelValidityView& validity,
                                    const char* operation)
{
    Status status = validateSampling(execution, source, coordinates, parameters, validity, operation);
    if (status.failed())
    {
        return status;
    }
    if (destination.size() < coordinates.size())
    {
        return reference_detail::kernelFailure(ErrorCode::IncompatibleArguments, operation,
                                               "the gather destination is shorter than the coordinate run");
    }
    if (regionsOverlap(source.data(), source.layout().totalBytes(), destination.data(),
                       static_cast<std::uint64_t>(destination.size_bytes())))
    {
        return reference_detail::kernelFailure(ErrorCode::IncompatibleArguments, operation,
                                               "the gather destination overlaps its source plane");
    }
    return Status::success();
}

} // namespace

Result<SamplingExecutionResult> resamplePlane(const ExecutionContext& execution, PlaneView<const float> source,
                                              PlaneView<float> destination,
                                              std::span<const KernelCoordinate> coordinates,
                                              const SamplingKernelParameters& parameters,
                                              const KernelValidityView& validity)
{
    constexpr const char* kOperation = "an::resamplePlane";
    Status status = validateResample(execution, source, destination, coordinates, parameters, validity, kOperation);
    if (status.failed())
    {
        return status;
    }

    const Result<ParallelTilePlan> planned = planParallelTiles(
        execution, ParallelTileRequest{.width = destination.width(),
                                       .height = destination.height(),
                                       .tileWidth = 256u,
                                       .tileHeight = 16u,
                                       .pixelCost = {.bytesPerItem = 8u,
                                                     .operationsPerItem = interpolationCost(parameters.interpolation)},
                                       .scratchBytesPerWorker = 0u});
    if (!planned.hasValue())
    {
        return planned.status();
    }

    const reference_detail::SamplingPlan plan = reference_detail::planSampling(source, validity, parameters);
    std::atomic<std::uint64_t> valid{0u};
    std::atomic<std::uint64_t> written{0u};
    status = executeParallelTiles(execution, *planned, {}, [&](const ParallelTile& tile) {
        std::uint64_t localValid = 0u;
        std::uint64_t localWritten = 0u;
        for (std::uint32_t y = tile.yBegin; y < tile.yEnd; ++y)
        {
            std::span<float> output = *destination.row(y);
            for (std::uint32_t x = tile.xBegin; x < tile.xEnd; ++x)
            {
                const std::size_t index = static_cast<std::size_t>(y) * destination.width() + x;
                float value = 0.0f;
                if (!reference_detail::sampleInterpolated(plan, coordinates[index], value))
                {
                    continue;
                }
                ++localValid;
                output[x] = value;
                ++localWritten;
            }
        }
        valid.fetch_add(localValid, std::memory_order_relaxed);
        written.fetch_add(localWritten, std::memory_order_relaxed);
        return Status::success();
    });
    if (status.failed())
    {
        return status;
    }

    return SamplingExecutionResult{
        .run = {.selection = {.valid = valid.load(std::memory_order_relaxed), .total = destination.sampleCount()},
                .outputsWritten = written.load(std::memory_order_relaxed)},
        .tileCount = planned->tileCount,
        .plannedWorkers = planned->plannedWorkers};
}

Result<KernelRunResult> gatherSamples(const ExecutionContext& execution, PlaneView<const float> source,
                                      std::span<const KernelCoordinate> coordinates, std::span<float> destination,
                                      const SamplingKernelParameters& parameters, const KernelValidityView& validity)
{
    constexpr const char* kOperation = "an::gatherSamples";
    Status status = validateGather(execution, source, coordinates, destination, parameters, validity, kOperation);
    if (status.failed())
    {
        return status;
    }

    const reference_detail::SamplingPlan plan = reference_detail::planSampling(source, validity, parameters);
    std::atomic<std::uint64_t> valid{0u};
    std::atomic<std::uint64_t> written{0u};
    status = parallelFor(
        execution, coordinates.size(), TaskCost{.bytesPerItem = 8u, .operationsPerItem = 20u}, [&](TaskRange range) {
            std::uint64_t localValid = 0u;
            for (std::uint64_t index = range.begin; index < range.end; ++index)
            {
                float value = 0.0f;
                if (!reference_detail::sampleInterpolated(plan, coordinates[static_cast<std::size_t>(index)], value))
                {
                    continue;
                }
                ++localValid;
                destination[static_cast<std::size_t>(index)] = value;
            }
            valid.fetch_add(localValid, std::memory_order_relaxed);
            written.fetch_add(localValid, std::memory_order_relaxed);
            return Status::success();
        });
    if (status.failed())
    {
        return status;
    }
    return KernelRunResult{.selection = {.valid = valid.load(std::memory_order_relaxed),
                                         .total = static_cast<std::uint64_t>(coordinates.size())},
                           .outputsWritten = written.load(std::memory_order_relaxed)};
}

Result<ScatterExecutionResult> scatterPlane(const ExecutionContext& execution, PlaneView<const float> source,
                                            std::span<const KernelCoordinate> coordinates, PlaneView<float> destination,
                                            const ScatterKernelParameters& parameters,
                                            const KernelValidityView& validity)
{
    constexpr const char* kOperation = "an::scatterPlane";
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

    ScatterExecutionResult result;
    result.run.run.selection.total = source.sampleCount();
    const std::uint64_t inputCount = source.sampleCount();
    if (inputCount == 0u)
    {
        return result;
    }
    constexpr std::uint64_t kEntryAlignment = alignof(ScatterEntry);
    constexpr std::uint64_t kEntryPairBytes = 2u * sizeof(ScatterEntry);
    if (inputCount > (std::numeric_limits<std::uint64_t>::max)() / kEntryPairBytes)
    {
        return reference_detail::kernelFailure(ErrorCode::LimitExceeded, kOperation,
                                               "the scatter grouping request exceeds addressable memory");
    }
    const std::uint64_t allocationBytes = inputCount * kEntryPairBytes;
    MemoryResource* const resource = execution.memoryResource();
    if (resource == nullptr)
    {
        return reference_detail::kernelFailure(ErrorCode::NotInitialized, kOperation,
                                               "the scatter execution has no memory resource");
    }
    Result<void*> allocated = resource->allocate(allocationBytes, kEntryAlignment);
    if (!allocated.hasValue())
    {
        return allocated.status();
    }
    const ScopedSamplingAllocation release{*resource, *allocated, allocationBytes, kEntryAlignment};
    auto* const first = static_cast<ScatterEntry*>(*allocated);
    auto* const second = first + inputCount;
    result.scratchBytes = allocationBytes;

    const reference_detail::ValidityAccessor accepts(validity);
    std::uint64_t selectedCount = 0u;
    for (std::uint64_t index = 0u; index < inputCount; ++index)
    {
        if ((index & 0xFFu) == 0u)
        {
            status = execution.checkCancellation();
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
        ++result.run.run.selection.valid;
        const reference_detail::ResolvedCoordinate resolved = reference_detail::resolveCoordinate(
            coordinates[static_cast<std::size_t>(index)], destination.width(), destination.height(), false);
        if (resolved.outside)
        {
            if (parameters.bounds == ScatterBoundsPolicy::Reject)
            {
                return reference_detail::kernelFailure(ErrorCode::OutOfRange, kOperation,
                                                       "a selected scatter coordinate is outside the destination");
            }
            ++result.run.discarded;
            continue;
        }
        first[selectedCount++] = ScatterEntry{
            .target = static_cast<std::uint64_t>(resolved.y) * destination.width() + resolved.x, .source = index};
    }

    ScatterEntry* current = first;
    ScatterEntry* spare = second;
    for (std::uint32_t pass = 0u; pass < sizeof(std::uint64_t); ++pass)
    {
        status = execution.checkCancellation();
        if (status.failed())
        {
            return status;
        }
        std::array<std::uint64_t, 256u> counts{};
        const std::uint32_t shift = pass * 8u;
        for (std::uint64_t index = 0u; index < selectedCount; ++index)
        {
            if ((index & 0xFFFu) == 0u)
            {
                status = execution.checkCancellation();
                if (status.failed())
                {
                    return status;
                }
            }
            ++counts[static_cast<std::uint8_t>(current[index].target >> shift)];
        }
        std::uint64_t offset = 0u;
        for (std::uint64_t& count : counts)
        {
            const std::uint64_t bucketCount = count;
            count = offset;
            offset += bucketCount;
        }
        for (std::uint64_t index = 0u; index < selectedCount; ++index)
        {
            if ((index & 0xFFFu) == 0u)
            {
                status = execution.checkCancellation();
                if (status.failed())
                {
                    return status;
                }
            }
            const std::uint8_t bucket = static_cast<std::uint8_t>(current[index].target >> shift);
            spare[counts[bucket]++] = current[index];
        }
        std::swap(current, spare);
    }

    auto* const groups = reinterpret_cast<ScatterGroup*>(spare);
    std::uint64_t groupCount = 0u;
    for (std::uint64_t begin = 0u; begin < selectedCount;)
    {
        std::uint64_t end = begin + 1u;
        while (end < selectedCount && current[end].target == current[begin].target)
        {
            if (((end - begin) & 0xFFFu) == 0u)
            {
                status = execution.checkCancellation();
                if (status.failed())
                {
                    return status;
                }
            }
            ++end;
        }
        std::construct_at(groups + groupCount, ScatterGroup{.begin = begin, .end = end});
        ++groupCount;
        begin = end;
    }
    result.destinationGroups = groupCount;
    result.run.conflicts = selectedCount - groupCount;
    if (parameters.conflicts == ScatterConflictPolicy::Reject && result.run.conflicts != 0u)
    {
        return reference_detail::kernelFailure(ErrorCode::IncompatibleArguments, kOperation,
                                               "two selected scatter inputs name the same destination");
    }

    constexpr TaskCost kGroupCost{.bytesPerItem = 1024u * 1024u, .operationsPerItem = 1024u * 1024u};
    result.plannedWorkers = plannedWorkerCount(execution, groupCount, kGroupCost);
    status = parallelFor(execution, groupCount, kGroupCost, [&](TaskRange range) {
        for (std::uint64_t groupIndex = range.begin; groupIndex < range.end; ++groupIndex)
        {
            const ScatterGroup group = groups[groupIndex];
            const std::uint64_t target = current[group.begin].target;
            const std::uint32_t destinationY = static_cast<std::uint32_t>(target / destination.width());
            const std::uint32_t destinationX = static_cast<std::uint32_t>(target % destination.width());
            std::span<float> output = *destination.row(destinationY);
            const std::uint64_t firstEnd =
                parameters.conflicts == ScatterConflictPolicy::First ? group.begin + 1u : group.end;
            for (std::uint64_t entryIndex = group.begin; entryIndex < firstEnd; ++entryIndex)
            {
                if (((entryIndex - group.begin) & 0xFFu) == 0u)
                {
                    Status stopped = execution.checkCancellation();
                    if (stopped.failed())
                    {
                        return stopped;
                    }
                }
                std::uint32_t sourceX = 0u;
                std::uint32_t sourceY = 0u;
                const float value = reference_detail::sourceAt(source, current[entryIndex].source, sourceX, sourceY);
                if (parameters.conflicts == ScatterConflictPolicy::Add)
                {
                    output[destinationX] += value;
                }
                else
                {
                    output[destinationX] = value;
                }
            }
        }
        return Status::success();
    });
    if (status.failed())
    {
        return status;
    }
    result.run.run.outputsWritten = parameters.conflicts == ScatterConflictPolicy::First ? groupCount : selectedCount;
    return result;
}

} // namespace an
