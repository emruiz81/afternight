// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - stable fixed-tree reduction primitives over resident image samples.
//
// **The answer does not depend on the thread count.** The selected component is cut into
// `deterministicChunkCount` chunks, a number derived from the sample count and the per-sample cost
// alone; each chunk accumulates its partial statistics in sample order, and the partials are merged
// by one fixed pairwise tree - neighbours first, an odd tail carried up - after every chunk has
// finished. Which worker ran which chunk therefore changes nothing.
//
// **Two passes, so the variance is not a difference of large numbers.** The first pass produces the
// count, the extremes, the sum and the norms; the second sums squared deviations from the first
// pass's mean through the same chunks and the same tree. Every accumulator is Float64 whatever the
// sample representation.
//
// **The error bound is stated a priori.** No partial sum passes through more additions than the
// largest chunk's length plus the tree's depth, so each sum carries Higham's recursive-summation
// bound for that depth, `gamma = n * u / (1 - n * u)` times the sum of the absolute terms, with the
// observed absolute sum widened by `1 / (1 - gamma)` because it was itself computed in floating
// point, and both roundings taken outward.
//
// A Float32 plane with one sample per pixel, an all-valid rule and no value interval takes the
// compiled run from the operation's dispatch table when it has one, which accumulates in vector
// lanes before it reduces them. That changes the order of the additions within a chunk, so the
// result may differ in its last bits between instruction sets, never between thread counts.

#include <AfterNightEngine/Algorithms/Kernel/ReductionKernel.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "algorithms/reduction/ReductionPrimitivesInternal.h"
#include "kernels/reference/AlgorithmKernels.h"
#include "kernels/reference/PlaneSourceAccess.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>

namespace an
{
namespace
{

constexpr const char* kOperation = "an::reducePlaneStatistics";
constexpr const char* kSelectionOperation = "an::reducePlaneStatisticsOver";

/// One chunk's accumulators, merged by the fixed tree. The extremes start at the infinities so an
/// empty chunk merges as nothing.
struct PartialStatistics
{
    std::uint64_t count = 0u;
    double minimum = std::numeric_limits<double>::infinity();
    double maximum = -std::numeric_limits<double>::infinity();
    double sum = 0.0;
    double l1 = 0.0;
    double squaredL2 = 0.0;
    double infinityNorm = 0.0;
    double centeredMoment = 0.0;
};

/// Returns the partials array to the context's memory resource on every exit path.
class ScopedPartialAllocation
{
public:
    ScopedPartialAllocation(MemoryResource& resource, void* pointer, std::uint64_t bytes) noexcept
        : m_resource(&resource), m_pointer(pointer), m_bytes(bytes)
    {
    }

    ~ScopedPartialAllocation()
    {
        m_resource->deallocate(m_pointer, m_bytes, alignof(PartialStatistics));
    }

    ScopedPartialAllocation(const ScopedPartialAllocation&) = delete;
    ScopedPartialAllocation& operator=(const ScopedPartialAllocation&) = delete;

private:
    MemoryResource* m_resource;
    void* m_pointer;
    std::uint64_t m_bytes;
};

[[nodiscard]] Status reductionFailure(ErrorCode code, const char* message, const char* operation)
{
    return Status::failure(code,
                           StatusDetail{.message = message, .operation = operation, .domain = ErrorDomain::Algorithms});
}

[[nodiscard]] bool pointerIsSampleAligned(const void* pointer, std::uint32_t elementBytes) noexcept
{
    return reinterpret_cast<std::uintptr_t>(pointer) % elementBytes == 0u;
}

[[nodiscard]] Status validateReduction(const ExecutionContext& execution, const KernelPlaneSource& source,
                                       const KernelValidityView& validity, const detail::ReductionWindow& window)
{
    if (!execution.valid())
    {
        return reductionFailure(ErrorCode::NotInitialized, "the reduction was given an empty execution context",
                                window.operation);
    }
    Status status = execution.checkCancellation();
    if (status.failed())
    {
        return status;
    }
    const Result<BackendSelection> selected = selectBackend(execution);
    if (!selected.hasValue())
    {
        return selected.status();
    }
    if (source.samples == nullptr || source.layout == nullptr || !isSampleType(source.sampleType))
    {
        return reductionFailure(ErrorCode::InvalidArgument,
                                "the reduction source has a null pointer, null layout, or unknown sample type",
                                window.operation);
    }
    if (source.layout->elementSizeBytes() != sampleTypeSizeBytes(source.sampleType) ||
        source.plane >= source.layout->planeCount() || source.sampleInPixel >= source.layout->samplesPerPixel())
    {
        return reductionFailure(ErrorCode::IncompatibleArguments,
                                "the reduction source type or selected component does not match its layout",
                                window.operation);
    }
    if (!pointerIsSampleAligned(source.samples, source.layout->elementSizeBytes()))
    {
        return reductionFailure(ErrorCode::InvalidArgument,
                                "the reduction source is not aligned for its resident sample type", window.operation);
    }
    if (!validity.descriptor.needsMask())
    {
        if (validity.mask != nullptr || validity.maskPlane != 0u)
        {
            return reductionFailure(ErrorCode::InvalidArgument,
                                    "a packed mask was supplied for a reduction validity rule that does not use one",
                                    window.operation);
        }
        return Status::success();
    }
    if (validity.mask == nullptr)
    {
        return reductionFailure(ErrorCode::InvalidArgument, "the explicit-mask reduction was given no packed mask",
                                window.operation);
    }
    const bool exactExtent = validity.mask->width() == source.layout->extent().width() &&
                             validity.mask->height() == source.layout->extent().height();
    const bool containsWindow = window.maskOriginX < validity.mask->width() &&
                                window.maskOriginY < validity.mask->height() &&
                                source.layout->extent().width() <= validity.mask->width() - window.maskOriginX &&
                                source.layout->extent().height() <= validity.mask->height() - window.maskOriginY;
    if ((window.requireExactMaskExtent ? !exactExtent : !containsWindow) ||
        validity.maskPlane >= validity.mask->planeCount())
    {
        return reductionFailure(ErrorCode::IncompatibleArguments,
                                "the reduction mask does not cover the source extent and selected plane",
                                window.operation);
    }
    return Status::success();
}

/// The samples one chunk covers: an equal share, with the first `itemCount % chunkCount` chunks one
/// sample longer, so the partition is a function of the two counts alone.
[[nodiscard]] TaskRange deterministicRange(std::uint64_t index, std::uint64_t itemCount,
                                           std::uint64_t chunkCount) noexcept
{
    const std::uint64_t base = itemCount / chunkCount;
    const std::uint64_t remainder = itemCount % chunkCount;
    const std::uint64_t extra = index < remainder ? index : remainder;
    const std::uint64_t begin = index * base + extra;
    return TaskRange{.begin = begin, .end = begin + base + (index < remainder ? 1u : 0u)};
}

/// Reads one selected component and decides whether a sample participates: the value rule first,
/// then the packed mask at the window's offset, then the value interval on the widened value.
template <ResidentSample SampleT> class SampleAccessor
{
public:
    SampleAccessor(const KernelPlaneSource& source, const KernelValidityView& validity,
                   const detail::ReductionWindow& window) noexcept
        : m_source(source), m_validity(validity), m_window(window),
          m_valueRuleAcceptsAll(validity.descriptor.rule() == ValidityRule::AllValid ||
                                validity.descriptor.rule() == ValidityRule::ExplicitMask)
    {
    }

    [[nodiscard]] const SampleT* rowStart(std::uint32_t y) const noexcept
    {
        const std::uint8_t* const bytes = static_cast<const std::uint8_t*>(m_source.samples);
        return reinterpret_cast<const SampleT*>(
            bytes + *m_source.layout->sampleByteOffset(m_source.plane, y, 0u, m_source.sampleInPixel));
    }

    [[nodiscard]] std::uint32_t columnStride() const noexcept
    {
        return m_source.layout->samplesPerPixel();
    }

    [[nodiscard]] const std::uint8_t* maskRowStart(std::uint32_t y) const noexcept
    {
        if (m_validity.mask == nullptr)
        {
            return nullptr;
        }
        const ValidityMaskView<const std::uint8_t>& mask = *m_validity.mask;
        const std::uint64_t offset = static_cast<std::uint64_t>(m_validity.maskPlane) * mask.planeStrideBytes() +
                                     static_cast<std::uint64_t>(m_window.maskOriginY + y) * mask.rowStrideBytes();
        return mask.data() + offset;
    }

    [[nodiscard]] bool selectedInMaskRow(SampleT value, std::uint32_t x, const std::uint8_t* maskRow,
                                         double& widened) const noexcept
    {
        if (!m_valueRuleAcceptsAll && !sampleIsValidByValue(m_validity.descriptor, value))
        {
            return false;
        }
        if (maskRow != nullptr)
        {
            const std::uint32_t maskX = m_window.maskOriginX + x;
            if ((maskRow[maskX / 8u] & static_cast<std::uint8_t>(1u << (maskX % 8u))) == 0u)
            {
                return false;
            }
        }
        widened = static_cast<double>(value);
        // A value interval narrows the rule rather than replacing it, so it is applied after it and
        // on the widened value: an integer representation and a floating one then agree about which
        // samples a clipping window retained.
        return !m_window.hasValueWindow || (widened >= m_window.lowestValue && widened <= m_window.highestValue);
    }

private:
    const KernelPlaneSource& m_source;
    const KernelValidityView& m_validity;
    const detail::ReductionWindow& m_window;
    bool m_valueRuleAcceptsAll = false;
};

void mergeFirstPass(PartialStatistics& destination, const PartialStatistics& source) noexcept
{
    destination.count += source.count;
    destination.minimum = (std::min)(destination.minimum, source.minimum);
    destination.maximum = (std::max)(destination.maximum, source.maximum);
    destination.sum += source.sum;
    destination.l1 += source.l1;
    destination.squaredL2 += source.squaredL2;
    destination.infinityNorm = (std::max)(destination.infinityNorm, source.infinityNorm);
}

[[nodiscard]] std::uint32_t ceilingLog2(std::uint64_t value) noexcept
{
    std::uint32_t depth = 0u;
    std::uint64_t power = 1u;
    while (power < value)
    {
        power *= 2u;
        ++depth;
    }
    return depth;
}

/// The a priori bound on a sum whose terms passed through at most `depth` additions.
///
/// An infinite bound when the observed absolute sum is not finite or `depth * u` reaches one, where
/// no finite statement holds.
[[nodiscard]] ReductionErrorBound errorBound(std::uint64_t depth, double observedAbsoluteSum) noexcept
{
    constexpr double kUnitRoundoff = 0x1p-53;
    const double product = static_cast<double>(depth) * kUnitRoundoff;
    if (!(observedAbsoluteSum >= 0.0) || !std::isfinite(observedAbsoluteSum) || product >= 1.0)
    {
        return ReductionErrorBound{.maximumAdditionDepth = depth,
                                   .sumAbsoluteTerms = std::numeric_limits<double>::infinity(),
                                   .absoluteError = std::numeric_limits<double>::infinity()};
    }
    const double gamma = product / (1.0 - product);
    const double upper = std::nextafter(observedAbsoluteSum / (1.0 - gamma), std::numeric_limits<double>::infinity());
    return ReductionErrorBound{.maximumAdditionDepth = depth,
                               .sumAbsoluteTerms = upper,
                               .absoluteError = std::nextafter(gamma * upper, std::numeric_limits<double>::infinity())};
}

template <ResidentSample SampleT>
[[nodiscard]] Result<ReductionStatistics> reduceAs(const ExecutionContext& execution, const KernelPlaneSource& source,
                                                   const KernelValidityView& validity,
                                                   const detail::ReductionWindow& window)
{
    const std::uint64_t itemCount =
        static_cast<std::uint64_t>(source.layout->extent().width()) * source.layout->extent().height();
    const TaskCost sampleCost{.bytesPerItem = sizeof(SampleT), .operationsPerItem = 32u};
    const std::uint64_t chunkCount = deterministicChunkCount(itemCount, sampleCost);
    MemoryResource* const resource = execution.memoryResource();
    if (resource == nullptr)
    {
        return reductionFailure(ErrorCode::NotInitialized, "the reduction execution has no context memory resource",
                                window.operation);
    }
    const std::uint64_t partialBytes = chunkCount * sizeof(PartialStatistics);
    Result<void*> allocated = resource->allocate(partialBytes, alignof(PartialStatistics));
    if (!allocated.hasValue())
    {
        return allocated.status();
    }
    const ScopedPartialAllocation release{*resource, *allocated, partialBytes};
    PartialStatistics* const partials = static_cast<PartialStatistics*>(*allocated);
    std::fill_n(partials, static_cast<std::size_t>(chunkCount), PartialStatistics{});

    const SampleAccessor<SampleT> samples(source, validity, window);
    const ReductionFirstRunFn reductionFirstRun = reductionFirstRunFor(execution);
    const ReductionCenteredRunFn reductionCenteredRun = reductionCenteredRunFor(execution);
    // The vectorized run reads a run of samples directly rather than through the accessor, so it is
    // eligible only when the accessor would have admitted every one of them: no mask, no value rule,
    // and no value interval.
    const bool vectorEligible = validity.descriptor.isAllValid() && !window.hasValueWindow &&
                                source.sampleType == SampleType::Float32 && source.layout->samplesPerPixel() == 1u;
    const std::uint64_t largestChunk = itemCount / chunkCount + (itemCount % chunkCount != 0u ? 1u : 0u);
    const TaskCost chunkCost{.bytesPerItem = largestChunk * sizeof(SampleT),
                             .operationsPerItem = largestChunk * sampleCost.operationsPerItem};
    Status status = parallelFor(execution, chunkCount, chunkCost, [&](TaskRange chunkIndices) -> Status {
        for (std::uint64_t chunk = chunkIndices.begin; chunk < chunkIndices.end; ++chunk)
        {
            PartialStatistics& partial = partials[static_cast<std::size_t>(chunk)];
            const TaskRange range = deterministicRange(chunk, itemCount, chunkCount);
            if (vectorEligible && reductionFirstRun != nullptr)
            {
                ReductionRunAccumulator vectorPartial;
                std::uint64_t index = range.begin;
                while (index < range.end)
                {
                    const std::uint32_t width = source.layout->extent().width();
                    const std::uint32_t y = static_cast<std::uint32_t>(index / width);
                    const std::uint32_t x = static_cast<std::uint32_t>(index % width);
                    const std::uint64_t run = (std::min)(range.end - index, static_cast<std::uint64_t>(width - x));
                    const std::uint8_t* const bytes = static_cast<const std::uint8_t*>(source.samples);
                    const void* const runStart =
                        bytes + *source.layout->sampleByteOffset(source.plane, y, x, source.sampleInPixel);
                    reductionFirstRun(runStart, run, vectorPartial);
                    index += run;
                }
                if (!vectorPartial.finite)
                {
                    return reductionFailure(
                        ErrorCode::InvalidData,
                        "a selected reduction sample is not finite; use a validity rule that excludes it",
                        window.operation);
                }
                partial.count = vectorPartial.count;
                partial.minimum = vectorPartial.minimum;
                partial.maximum = vectorPartial.maximum;
                partial.sum = vectorPartial.sum;
                partial.l1 = vectorPartial.l1;
                partial.squaredL2 = vectorPartial.squaredL2;
                partial.infinityNorm = vectorPartial.infinityNorm;
                continue;
            }
            std::uint64_t index = range.begin;
            const std::uint32_t width = source.layout->extent().width();
            const std::uint32_t stride = samples.columnStride();
            while (index < range.end)
            {
                const std::uint32_t y = static_cast<std::uint32_t>(index / width);
                const std::uint32_t firstX = static_cast<std::uint32_t>(index % width);
                const std::uint32_t run = static_cast<std::uint32_t>(
                    (std::min)(range.end - index, static_cast<std::uint64_t>(width - firstX)));
                const SampleT* const row = samples.rowStart(y);
                const std::uint8_t* const maskRow = samples.maskRowStart(y);
                for (std::uint32_t offset = 0u; offset < run; ++offset)
                {
                    const std::uint32_t x = firstX + offset;
                    double value = 0.0;
                    if (!samples.selectedInMaskRow(row[static_cast<std::uint64_t>(x) * stride], x, maskRow, value))
                    {
                        continue;
                    }
                    if (!std::isfinite(value))
                    {
                        return reductionFailure(
                            ErrorCode::InvalidData,
                            "a selected reduction sample is not finite; use a validity rule that excludes it",
                            window.operation);
                    }
                    const double magnitude = std::abs(value);
                    const double squared = value * value;
                    if (!std::isfinite(squared))
                    {
                        return reductionFailure(ErrorCode::InvalidData,
                                                "a selected reduction sample overflows the squared-norm accumulator",
                                                window.operation);
                    }
                    ++partial.count;
                    partial.minimum = (std::min)(partial.minimum, value);
                    partial.maximum = (std::max)(partial.maximum, value);
                    partial.sum += value;
                    partial.l1 += magnitude;
                    partial.squaredL2 += squared;
                    partial.infinityNorm = (std::max)(partial.infinityNorm, magnitude);
                }
                index += run;
            }
        }
        return Status::success();
    });
    if (status.failed())
    {
        return status;
    }

    std::uint64_t remaining = chunkCount;
    while (remaining > 1u)
    {
        const std::uint64_t pairs = remaining / 2u;
        for (std::uint64_t pair = 0u; pair < pairs; ++pair)
        {
            PartialStatistics& left = partials[static_cast<std::size_t>(2u * pair)];
            const PartialStatistics& right = partials[static_cast<std::size_t>(2u * pair + 1u)];
            mergeFirstPass(left, right);
            partials[static_cast<std::size_t>(pair)] = left;
        }
        if (remaining % 2u != 0u)
        {
            partials[static_cast<std::size_t>(pairs)] = partials[static_cast<std::size_t>(remaining - 1u)];
        }
        remaining = pairs + remaining % 2u;
    }
    const PartialStatistics first = partials[0];
    if (first.count == 0u)
    {
        return requireSelection(ValidityCount{.valid = 0u, .total = itemCount}, window.operation);
    }
    if (!std::isfinite(first.sum) || !std::isfinite(first.l1) || !std::isfinite(first.squaredL2))
    {
        return reductionFailure(ErrorCode::InvalidData, "a reduction accumulator overflowed Float64", window.operation);
    }

    const double mean = first.sum / static_cast<double>(first.count);
    for (std::uint64_t chunk = 0u; chunk < chunkCount; ++chunk)
    {
        partials[static_cast<std::size_t>(chunk)].centeredMoment = 0.0;
    }
    status = parallelFor(execution, chunkCount, chunkCost, [&](TaskRange chunkIndices) -> Status {
        for (std::uint64_t chunk = chunkIndices.begin; chunk < chunkIndices.end; ++chunk)
        {
            PartialStatistics& partial = partials[static_cast<std::size_t>(chunk)];
            const TaskRange range = deterministicRange(chunk, itemCount, chunkCount);
            if (vectorEligible && reductionCenteredRun != nullptr)
            {
                ReductionRunAccumulator vectorPartial;
                std::uint64_t index = range.begin;
                while (index < range.end)
                {
                    const std::uint32_t width = source.layout->extent().width();
                    const std::uint32_t y = static_cast<std::uint32_t>(index / width);
                    const std::uint32_t x = static_cast<std::uint32_t>(index % width);
                    const std::uint64_t run = (std::min)(range.end - index, static_cast<std::uint64_t>(width - x));
                    const std::uint8_t* const bytes = static_cast<const std::uint8_t*>(source.samples);
                    const void* const runStart =
                        bytes + *source.layout->sampleByteOffset(source.plane, y, x, source.sampleInPixel);
                    reductionCenteredRun(runStart, run, mean, vectorPartial);
                    index += run;
                }
                partial.centeredMoment = vectorPartial.centeredMoment;
                continue;
            }
            std::uint64_t index = range.begin;
            const std::uint32_t width = source.layout->extent().width();
            const std::uint32_t stride = samples.columnStride();
            while (index < range.end)
            {
                const std::uint32_t y = static_cast<std::uint32_t>(index / width);
                const std::uint32_t firstX = static_cast<std::uint32_t>(index % width);
                const std::uint32_t run = static_cast<std::uint32_t>(
                    (std::min)(range.end - index, static_cast<std::uint64_t>(width - firstX)));
                const SampleT* const row = samples.rowStart(y);
                const std::uint8_t* const maskRow = samples.maskRowStart(y);
                for (std::uint32_t offset = 0u; offset < run; ++offset)
                {
                    const std::uint32_t x = firstX + offset;
                    double value = 0.0;
                    if (samples.selectedInMaskRow(row[static_cast<std::uint64_t>(x) * stride], x, maskRow, value))
                    {
                        const double deviation = value - mean;
                        partial.centeredMoment += deviation * deviation;
                    }
                }
                index += run;
            }
        }
        return Status::success();
    });
    if (status.failed())
    {
        return status;
    }
    remaining = chunkCount;
    while (remaining > 1u)
    {
        const std::uint64_t pairs = remaining / 2u;
        for (std::uint64_t pair = 0u; pair < pairs; ++pair)
        {
            partials[static_cast<std::size_t>(pair)].centeredMoment =
                partials[static_cast<std::size_t>(2u * pair)].centeredMoment +
                partials[static_cast<std::size_t>(2u * pair + 1u)].centeredMoment;
        }
        if (remaining % 2u != 0u)
        {
            partials[static_cast<std::size_t>(pairs)].centeredMoment =
                partials[static_cast<std::size_t>(remaining - 1u)].centeredMoment;
        }
        remaining = pairs + remaining % 2u;
    }
    const double centeredMoment = partials[0].centeredMoment;
    if (!std::isfinite(centeredMoment))
    {
        return reductionFailure(ErrorCode::InvalidData, "the centered-moment accumulator overflowed Float64",
                                window.operation);
    }

    const std::uint64_t additionDepth = largestChunk + ceilingLog2(chunkCount);
    ReductionStatistics result;
    result.run.selection = ValidityCount{.valid = first.count, .total = itemCount};
    result.run.outputsWritten = 9u;
    result.plan.deterministicChunks = chunkCount;
    result.plan.plannedWorkers =
        static_cast<std::uint32_t>((std::min)(static_cast<std::uint64_t>(execution.admittedConcurrency()), chunkCount));
    result.plan.maximumAdditionDepth = additionDepth;
    result.minimum = first.minimum;
    result.maximum = first.maximum;
    result.sum = first.sum;
    result.mean = mean;
    result.sumSquaredDeviations = centeredMoment;
    result.populationVariance = centeredMoment / static_cast<double>(first.count);
    result.l1Norm = first.l1;
    result.l2Norm = std::sqrt(first.squaredL2);
    result.infinityNorm = first.infinityNorm;
    result.sumError = errorBound(additionDepth, first.l1);
    result.centeredMomentError = errorBound(additionDepth, centeredMoment);
    result.l1Error = errorBound(additionDepth, first.l1);
    result.squaredL2Error = errorBound(additionDepth, first.squaredL2);
    return result;
}

} // namespace

namespace detail
{
Result<ReductionStatistics> reducePlaneStatisticsWindow(const ExecutionContext& execution,
                                                        const KernelPlaneSource& source,
                                                        const KernelValidityView& validity,
                                                        const ReductionWindow& window)
{
    const Status ready = validateReduction(execution, source, validity, window);
    if (ready.failed())
    {
        return ready;
    }
    switch (source.sampleType)
    {
    case SampleType::UInt8:
        return reduceAs<std::uint8_t>(execution, source, validity, window);
    case SampleType::Int8:
        return reduceAs<std::int8_t>(execution, source, validity, window);
    case SampleType::UInt16:
        return reduceAs<std::uint16_t>(execution, source, validity, window);
    case SampleType::Int16:
        return reduceAs<std::int16_t>(execution, source, validity, window);
    case SampleType::UInt32:
        return reduceAs<std::uint32_t>(execution, source, validity, window);
    case SampleType::Int32:
        return reduceAs<std::int32_t>(execution, source, validity, window);
    case SampleType::Float32:
        return reduceAs<float>(execution, source, validity, window);
    case SampleType::Float64:
        return reduceAs<double>(execution, source, validity, window);
    }
    return reductionFailure(ErrorCode::InvalidArgument, "the reduction source names no resident sample type",
                            window.operation);
}
} // namespace detail

Result<ReductionStatistics> reducePlaneStatistics(const ExecutionContext& execution, const KernelPlaneSource& source,
                                                  const KernelValidityView& validity)
{
    return detail::reducePlaneStatisticsWindow(
        execution, source, validity,
        detail::ReductionWindow{
            .maskOriginX = 0u, .maskOriginY = 0u, .requireExactMaskExtent = true, .operation = kOperation});
}

Result<ReductionStatistics> reducePlaneStatisticsOver(const ExecutionContext& execution,
                                                      const KernelPlaneSource& source,
                                                      const KernelValidityView& validity,
                                                      const KernelPlaneSelection& selection)
{
    const detail::ReductionWindow completeSourceWindow{
        .maskOriginX = 0u, .maskOriginY = 0u, .requireExactMaskExtent = true, .operation = kSelectionOperation};
    const Status sourceReady = validateReduction(execution, source, validity, completeSourceWindow);
    if (sourceReady.failed())
    {
        return sourceReady;
    }

    if (selection.width == 0u || selection.height == 0u)
    {
        return requireSelection(ValidityCount{.valid = 0u, .total = 0u}, kSelectionOperation);
    }
    const Result<plane_detail::PlaneRectangle> rectangle =
        plane_detail::describePlaneRectangle(execution, source, selection, kSelectionOperation);
    if (!rectangle.hasValue())
    {
        return rectangle.status();
    }

    const KernelPlaneSource regionSource{.sampleType = source.sampleType,
                                         .samples = rectangle->samples,
                                         .layout = &rectangle->layout,
                                         .plane = source.plane,
                                         .sampleInPixel = source.sampleInPixel};
    return detail::reducePlaneStatisticsWindow(
        execution, regionSource, validity,
        detail::ReductionWindow{.maskOriginX = rectangle->window.maskOriginX,
                                .maskOriginY = rectangle->window.maskOriginY,
                                .requireExactMaskExtent = false,
                                .hasValueWindow = rectangle->window.hasValueWindow,
                                .lowestValue = rectangle->window.lowestValue,
                                .highestValue = rectangle->window.highestValue,
                                .operation = kSelectionOperation});
}

} // namespace an
