// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - global normalization: measuring a frame's robust location and spread,
// combining references, deriving and solving matches, and writing a match back.
//
// A measurement gathers a channel's admitted samples once, in row-major order, into engine memory
// and takes every rank from that contiguous copy by radix descent: a sixteen-bit histogram pass per
// digit, filled per worker and summed, until the rank's bin is small enough to gather and select
// from exactly. Both middle ranks of an even count descend together while they share a bin, and the
// deviation ranks descend over the Float64 distance from the median, so every median and deviation
// is an exact rank. When the budget refuses the copy, the channel is ranked where it lies through
// the order-statistics kernel instead. The clipped estimator's biweight scale is summed per row
// chunk into partials combined in chunk order, and the chunking comes from
// `deterministicChunkCount`, so the answer is the same at every admitted width.
//
// The overlap solve builds, per channel, the normal equations of a weighted least-squares problem
// with the reference frame's unknown removed, and reduces them by a Float64 elimination with
// partial pivoting over context memory; the systems are small and diagonally dominant, and this
// engine does not link a linear-algebra library for them. A union-find over the pairs that can take
// part decides connectivity before any system is factored.
//
// The apply computes each admitted sample in Float64 and narrows once, in the scalar row here or
// the compiled row the CPU dispatch table offers; both store identical bits.

#include <AfterNightEngine/Algorithms/Normalization/GlobalNormalization.h>

#include <AfterNightEngine/Algorithms/Kernel/HistogramOrderKernel.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/CheckedSize.h>
#include <AfterNightEngine/Image/OperationTransition.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "kernels/reference/AlgorithmKernels.h"
#include "kernels/reference/PlaneSourceAccess.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <type_traits>

namespace an
{

namespace
{

constexpr const char* kMeasureOperation = "an::measureGlobalNormalization";
constexpr const char* kCombineOperation = "an::combineNormalizationReferences";
constexpr const char* kEstimateOperation = "an::estimateGlobalNormalization";
constexpr const char* kOverlapOperation = "an::measureOverlapStatistics";
constexpr const char* kSelectOperation = "an::selectOverlapPairs";
constexpr const char* kSolveOperation = "an::solveOverlapNormalization";
constexpr const char* kApplyOperation = "an::applyGlobalNormalization";
constexpr const char* kTransitionOperation = "an::globalNormalizationTransition";

/// The most reference frames one combination ranks on the stack.
constexpr std::uint32_t kMaxReferenceMembers = 4096u;

/// The biweight tuning constant.
constexpr double kBiweightTuning = 9.0;

[[nodiscard]] Status normalizationFailure(const char* operation, ErrorCode code, const char* message)
{
    return Status::failure(code,
                           StatusDetail{.message = message, .operation = operation, .domain = ErrorDomain::Algorithms});
}

[[nodiscard]] Status parameterFailure(const char* operation, ErrorCode code, const char* message, const char* parameter)
{
    return Status::failure(code,
                           StatusDetail{.message = message,
                                        .operation = operation,
                                        .domain = ErrorDomain::Algorithms,
                                        .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
}

[[nodiscard]] bool pointerIsAligned(const void* pointer, SampleType sampleType) noexcept
{
    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(pointer);
    return sampleType == SampleType::Float64 ? address % alignof(double) == 0u : address % alignof(float) == 0u;
}

[[nodiscard]] Status checkValidityBinding(const KernelValidityView& validity, const ImageExtent& extent,
                                          const char* operation)
{
    if (validity.maskPlane != 0u)
    {
        return parameterFailure(operation, ErrorCode::InvalidArgument,
                                "an image-wide normalization validity view starts at mask plane zero", "validity");
    }
    if (validity.descriptor.needsMask())
    {
        if (validity.mask == nullptr)
        {
            return parameterFailure(operation, ErrorCode::InvalidArgument,
                                    "an explicit validity rule needs the mask that decides it", "validity");
        }
        return checkMaskCovers(*validity.mask, extent, operation);
    }
    if (validity.mask != nullptr)
    {
        return parameterFailure(operation, ErrorCode::InvalidArgument,
                                "a validity mask was supplied for a rule that does not read one", "validity");
    }
    return Status::success();
}

template <typename SampleT>
[[nodiscard]] const SampleT* sampleAddress(const NormalizationFrameView& frame, std::uint32_t x, std::uint32_t y,
                                           std::uint32_t channel) noexcept
{
    const ImageLayout& layout = frame.layout();
    const std::uint32_t plane = channel / layout.samplesPerPixel();
    const std::uint32_t inPixel = channel % layout.samplesPerPixel();
    const auto* base = static_cast<const std::byte*>(frame.samples());
    const std::uint64_t offset = static_cast<std::uint64_t>(plane) * layout.planeStrideBytes() +
                                 static_cast<std::uint64_t>(y) * layout.rowStrideBytes() +
                                 (static_cast<std::uint64_t>(x) * layout.samplesPerPixel() + inPixel) * sizeof(SampleT);
    return reinterpret_cast<const SampleT*>(base + offset);
}

template <typename SampleT>
[[nodiscard]] SampleT* sampleAddress(void* samples, const ImageLayout& layout, std::uint32_t x, std::uint32_t y,
                                     std::uint32_t channel) noexcept
{
    const std::uint32_t plane = channel / layout.samplesPerPixel();
    const std::uint32_t inPixel = channel % layout.samplesPerPixel();
    auto* base = static_cast<std::byte*>(samples);
    const std::uint64_t offset = static_cast<std::uint64_t>(plane) * layout.planeStrideBytes() +
                                 static_cast<std::uint64_t>(y) * layout.rowStrideBytes() +
                                 (static_cast<std::uint64_t>(x) * layout.samplesPerPixel() + inPixel) * sizeof(SampleT);
    return reinterpret_cast<SampleT*>(base + offset);
}

template <typename SampleT>
[[nodiscard]] bool sampleParticipates(const NormalizationFrameView& frame, SampleT value, std::uint32_t x,
                                      std::uint32_t y, std::uint32_t channel) noexcept
{
    if (!sampleIsValidByValue(frame.validity().descriptor, value))
    {
        return false;
    }
    if (!frame.validity().descriptor.needsMask())
    {
        return true;
    }
    const ValidityMaskView<const std::uint8_t>& mask = *frame.validity().mask;
    const std::uint32_t plane = mask.isShared() ? 0u : channel;
    return mask.test(plane, x, y).value_or(false);
}

[[nodiscard]] bool frameSampleParticipates(const NormalizationFrameView& frame, std::uint32_t x, std::uint32_t y,
                                           std::uint32_t channel) noexcept
{
    if (frame.descriptor().sampleType() == SampleType::Float32)
    {
        return sampleParticipates(frame, *sampleAddress<float>(frame, x, y, channel), x, y, channel);
    }
    return sampleParticipates(frame, *sampleAddress<double>(frame, x, y, channel), x, y, channel);
}

[[nodiscard]] bool regionWithin(const StatisticsRegion& region, const ImageExtent& extent) noexcept
{
    return region.originX <= extent.width() && region.originY <= extent.height() &&
           region.width <= extent.width() - region.originX && region.height <= extent.height() - region.originY;
}

[[nodiscard]] KernelPlaneSelection selectionOf(const StatisticsRegion& region) noexcept
{
    return KernelPlaneSelection{
        .originX = region.originX, .originY = region.originY, .width = region.width, .height = region.height};
}

[[nodiscard]] KernelPlaneSource channelSource(const NormalizationFrameView& frame, std::uint32_t channel) noexcept
{
    return KernelPlaneSource{.sampleType = frame.descriptor().sampleType(),
                             .samples = frame.samples(),
                             .layout = &frame.layout(),
                             .plane = channel / frame.layout().samplesPerPixel(),
                             .sampleInPixel = channel % frame.layout().samplesPerPixel()};
}

[[nodiscard]] KernelValidityView channelValidity(const NormalizationFrameView& frame, std::uint32_t channel) noexcept
{
    KernelValidityView validity = frame.validity();
    validity.maskPlane = validity.mask != nullptr && !validity.mask->isShared() ? channel : 0u;
    return validity;
}

/// What one measurement cost, accumulated over its channels.
struct MeasurementCost
{
    std::uint64_t planeScans = 0u;
    std::uint64_t peakScratchBytes = 0u;
    bool compacted = false;
};

/// The order-preserving integer image of a floating value: ascending keys are ascending values.
[[nodiscard]] std::uint32_t sortableKey(float value) noexcept
{
    const auto bits = std::bit_cast<std::uint32_t>(value);
    return (bits & 0x80000000u) != 0u ? ~bits : (bits | 0x80000000u);
}

/// The order-preserving integer image of a Float64 value.
[[nodiscard]] std::uint64_t sortableKey(double value) noexcept
{
    const auto bits = std::bit_cast<std::uint64_t>(value);
    return (bits & 0x8000000000000000ull) != 0u ? ~bits : (bits | 0x8000000000000000ull);
}

/// The value a Float32 key stands for, widened.
[[nodiscard]] double valueOfKey(std::uint32_t key) noexcept
{
    const std::uint32_t bits = (key & 0x80000000u) != 0u ? (key & 0x7FFFFFFFu) : ~key;
    return static_cast<double>(std::bit_cast<float>(bits));
}

/// The value a Float64 key stands for.
[[nodiscard]] double valueOfKey(std::uint64_t key) noexcept
{
    const std::uint64_t bits = (key & 0x8000000000000000ull) != 0u ? (key & 0x7FFFFFFFFFFFFFFFull) : ~key;
    return std::bit_cast<double>(bits);
}

/// The width of one radix digit the rank descent resolves per pass.
constexpr std::uint32_t kRadixBits = 16u;

/// How many bins one digit has.
constexpr std::uint32_t kRadixBins = 1u << kRadixBits;

/// The most candidates one rank descent gathers and selects from exactly instead of descending
/// further.
constexpr std::uint64_t kGatherCeiling = 1u << 14;

/// How many bins of every worker's histogram one task clears or sums.
constexpr std::uint32_t kHistogramBlockBins = 4096u;

/// One row chunk's admitted samples inside the compact plane.
struct CompactSegment
{
    std::uint64_t offset = 0u;
    std::uint64_t count = 0u;
};

/// The admitted samples of one channel gathered once into engine memory, in row-major order, with
/// the worker-private histograms and candidate buffers the rank descents read them with.
template <typename SampleT> struct CompactPlane
{
    SampleT* samples = nullptr;
    CompactSegment* segments = nullptr;
    std::uint32_t* histograms = nullptr;
    std::uint64_t* candidates = nullptr;
    std::uint64_t* candidateCounts = nullptr;
    double* partials = nullptr;
    std::uint32_t segmentCount = 0u;
    std::uint32_t slots = 0u;
    std::uint64_t admitted = 0u;
    double minimum = 0.0;
    double maximum = 0.0;
};

/// Where one rank descent stands.
template <typename KeyT> struct RankCursor
{
    KeyT prefix = 0u;
    std::uint32_t resolvedBits = 0u;
    std::uint64_t rank = 0u;
    std::uint64_t binCount = 0u;
    bool resolved = false;
    double value = 0.0;
};

/// The two middle ranks of one selection, and how many samples it held.
struct MedianRanks
{
    std::uint64_t total = 0u;
    double lower = 0.0;
    double upper = 0.0;
};

template <typename KeyT> [[nodiscard]] bool keyMatchesPrefix(KeyT key, KeyT prefix, std::uint32_t resolvedBits) noexcept
{
    constexpr std::uint32_t kKeyBits = sizeof(KeyT) * 8u;
    return resolvedBits == 0u || (key >> (kKeyBits - resolvedBits)) == prefix;
}

template <typename KeyT> [[nodiscard]] std::uint32_t keyDigit(KeyT key, std::uint32_t resolvedBits) noexcept
{
    constexpr std::uint32_t kKeyBits = sizeof(KeyT) * 8u;
    return static_cast<std::uint32_t>((key >> (kKeyBits - resolvedBits - kRadixBits)) & (kRadixBins - 1u));
}

/// The single layout used by the measurement and by callers planning its residency.
struct CompactWorkspaceLayout
{
    std::uint64_t chunks;
    std::uint64_t samples;
    std::uint64_t segments;
    std::uint64_t histograms;
    std::uint64_t candidates;
    std::uint64_t candidateCounts;
    std::uint64_t partials;
    std::uint64_t total;
};

[[nodiscard]] Result<CompactWorkspaceLayout> compactWorkspaceLayout(std::uint32_t width, std::uint32_t height,
                                                                    SampleType sampleType, std::uint32_t workers)
{
    if (width == 0u || height == 0u || workers == 0u ||
        (sampleType != SampleType::Float32 && sampleType != SampleType::Float64))
    {
        return normalizationFailure(kMeasureOperation, ErrorCode::InvalidArgument,
                                    "normalization workspace needs a nonempty floating plane and workers");
    }
    const std::uint64_t sampleBytes = sampleTypeSizeBytes(sampleType);
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * sampleBytes,
                        .operationsPerItem = static_cast<std::uint64_t>(width) * 4u};
    const std::uint64_t chunks = (std::max)(deterministicChunkCount(height, cost), std::uint64_t{1});
    const auto aligned = [](CheckedSize bytes) {
        const CheckedSize padded = bytes + 63u;
        return padded.isValid() ? CheckedSize{padded.valueOr(0u) & ~std::uint64_t{63u}} : CheckedSize::overflowed();
    };
    const CheckedSize samples = aligned(CheckedSize{width} * height * sampleBytes);
    const CheckedSize segments = aligned(CheckedSize{chunks} * sizeof(CompactSegment));
    const CheckedSize histograms = aligned(CheckedSize{workers} * kRadixBins * sizeof(std::uint32_t));
    const CheckedSize candidates = aligned(CheckedSize{workers} * kGatherCeiling * sizeof(std::uint64_t));
    const CheckedSize candidateCounts = aligned(CheckedSize{workers} * sizeof(std::uint64_t));
    const CheckedSize partials = aligned(CheckedSize{chunks} * 2u * sizeof(double));
    const CheckedSize total = samples + segments + histograms + candidates + candidateCounts + partials;
    if (!total.isValid())
    {
        return normalizationFailure(kMeasureOperation, ErrorCode::LimitExceeded,
                                    "normalization workspace exceeds the addressable byte count");
    }
    return CompactWorkspaceLayout{chunks,
                                  samples.valueOr(0u),
                                  segments.valueOr(0u),
                                  histograms.valueOr(0u),
                                  candidates.valueOr(0u),
                                  candidateCounts.valueOr(0u),
                                  partials.valueOr(0u),
                                  total.valueOr(0u)};
}

/// Gathers the admitted samples of a region into the compact plane, one row chunk per segment.
template <typename SampleT>
[[nodiscard]] Status compactRegion(const ExecutionContext& execution, const KernelPlaneSource& source,
                                   const KernelValidityView& validity, const KernelPlaneSelection& region,
                                   std::uint64_t rowsPerChunk, CompactPlane<SampleT>& plane)
{
    const plane_detail::PlaneSampleReader<SampleT> reader{source, validity};
    const TaskCost chunkCost{.bytesPerItem = rowsPerChunk * region.width * sizeof(SampleT),
                             .operationsPerItem = rowsPerChunk * region.width * 4u};
    std::atomic<bool> nonFinite{false};
    const Status gathered = parallelFor(execution, plane.segmentCount, chunkCost, [&](TaskRange range) {
        for (std::uint64_t chunk = range.begin; chunk < range.end; ++chunk)
        {
            const std::uint64_t firstRow = chunk * rowsPerChunk;
            const std::uint64_t lastRow =
                (std::min)(static_cast<std::uint64_t>(region.height), firstRow + rowsPerChunk);
            const std::uint64_t offset = firstRow * region.width;
            SampleT* const out = plane.samples + offset;
            std::uint64_t count = 0u;
            double lowest = (std::numeric_limits<double>::max)();
            double highest = -(std::numeric_limits<double>::max)();
            for (std::uint64_t relativeY = firstRow; relativeY < lastRow; ++relativeY)
            {
                const auto y = static_cast<std::uint32_t>(region.originY + relativeY);
                const SampleT* const row = reader.rowStart(y);
                for (std::uint32_t relativeX = 0u; relativeX < region.width; ++relativeX)
                {
                    const std::uint32_t x = region.originX + relativeX;
                    const SampleT value = row[static_cast<std::size_t>(x) * reader.columnStride()];
                    double widened = 0.0;
                    if (!reader.selected(value, x, y, widened))
                    {
                        continue;
                    }
                    if (!std::isfinite(widened))
                    {
                        nonFinite.store(true, std::memory_order_relaxed);
                        continue;
                    }
                    out[count++] = value;
                    lowest = widened < lowest ? widened : lowest;
                    highest = widened > highest ? widened : highest;
                }
            }
            plane.segments[chunk] = CompactSegment{.offset = offset, .count = count};
            plane.partials[2u * chunk] = lowest;
            plane.partials[2u * chunk + 1u] = highest;
        }
        return Status::success();
    });
    if (gathered.failed())
    {
        return gathered;
    }
    if (nonFinite.load(std::memory_order_relaxed))
    {
        return normalizationFailure(kMeasureOperation, ErrorCode::InvalidData,
                                    "a sample the validity rule admits is not finite; state FiniteOnly or a mask");
    }
    plane.admitted = 0u;
    plane.minimum = (std::numeric_limits<double>::max)();
    plane.maximum = -(std::numeric_limits<double>::max)();
    for (std::uint32_t chunk = 0u; chunk < plane.segmentCount; ++chunk)
    {
        plane.admitted += plane.segments[chunk].count;
        plane.minimum = (std::min)(plane.minimum, plane.partials[2u * chunk]);
        plane.maximum = (std::max)(plane.maximum, plane.partials[2u * chunk + 1u]);
    }
    if (plane.admitted == 0u)
    {
        return normalizationFailure(kMeasureOperation, ErrorCode::EmptySelection,
                                    "no sample of the measured channel is admitted by its validity rule");
    }
    return Status::success();
}

/// The cost the scheduler partitions one compact pass by.
template <typename SampleT>
[[nodiscard]] TaskCost compactPassCost(const CompactPlane<SampleT>& plane, std::uint64_t rowsPerChunk,
                                       std::uint32_t width) noexcept
{
    (void)plane;
    return TaskCost{.bytesPerItem = rowsPerChunk * width * sizeof(SampleT),
                    .operationsPerItem = rowsPerChunk * width * 6u};
}

/// Counts, per digit, the admitted keys under one prefix.
///
/// Every worker fills its own histogram and the slots are summed afterwards, so the counts do not
/// depend on which worker read which segment. Only the slots the count runs on are cleared and
/// summed, and both are split by block of bins across the operation's workers: at sixteen workers
/// the slots hold four mebibytes, which one thread took longer to clear and sum than the workers
/// took to count.
template <typename KeyT, typename SampleT, typename KeyOf>
[[nodiscard]] Status histogramPass(const ExecutionContext& execution, CompactPlane<SampleT>& plane,
                                   const TaskCost& cost, KeyOf keyOf, KeyT prefix, std::uint32_t resolvedBits)
{
    const std::uint32_t slots =
        (std::min)((std::max)(plannedWorkerCount(execution, plane.segmentCount, cost), 1u), plane.slots);
    const TaskCost blockCost{.bytesPerItem =
                                 static_cast<std::uint64_t>(slots) * kHistogramBlockBins * sizeof(std::uint32_t),
                             .operationsPerItem = static_cast<std::uint64_t>(slots) * kHistogramBlockBins};
    const Status cleared = parallelFor(execution, kRadixBins / kHistogramBlockBins, blockCost, [&](TaskRange range) {
        for (std::uint64_t block = range.begin; block < range.end; ++block)
        {
            for (std::uint32_t slot = 0u; slot < slots; ++slot)
            {
                std::uint32_t* const first =
                    plane.histograms + static_cast<std::uint64_t>(slot) * kRadixBins + block * kHistogramBlockBins;
                std::fill(first, first + kHistogramBlockBins, 0u);
            }
        }
        return Status::success();
    });
    if (cleared.failed())
    {
        return cleared;
    }
    const Status counted = parallelForWorkers(execution, plane.segmentCount, cost, [&](WorkerTask task) {
        std::uint32_t* const histogram = plane.histograms + static_cast<std::uint64_t>(task.workerSlot) * kRadixBins;
        for (std::uint64_t segment = task.range.begin; segment < task.range.end; ++segment)
        {
            const SampleT* const begin = plane.samples + plane.segments[segment].offset;
            const SampleT* const end = begin + plane.segments[segment].count;
            for (const SampleT* element = begin; element < end; ++element)
            {
                KeyT key = 0u;
                if (!keyOf(*element, key) || !keyMatchesPrefix(key, prefix, resolvedBits))
                {
                    continue;
                }
                ++histogram[keyDigit(key, resolvedBits)];
            }
        }
        return Status::success();
    });
    if (counted.failed())
    {
        return counted;
    }
    if (slots == 1u)
    {
        return Status::success();
    }
    return parallelFor(execution, kRadixBins / kHistogramBlockBins, blockCost, [&](TaskRange range) {
        for (std::uint64_t block = range.begin; block < range.end; ++block)
        {
            std::uint32_t* const total = plane.histograms + block * kHistogramBlockBins;
            for (std::uint32_t slot = 1u; slot < slots; ++slot)
            {
                const std::uint32_t* const other =
                    plane.histograms + static_cast<std::uint64_t>(slot) * kRadixBins + block * kHistogramBlockBins;
                for (std::uint32_t bin = 0u; bin < kHistogramBlockBins; ++bin)
                {
                    total[bin] += other[bin];
                }
            }
        }
        return Status::success();
    });
}

/// Gathers every admitted key under one prefix into the candidate buffers, then puts the keys of
/// two ranks - which may be the same rank - where a sort would have put them.
///
/// A selection rather than a sort: a gather holds up to sixteen thousand keys, and sorting them was
/// a cost of every descent that no worker shared.
///
/// @param lower the lower rank among the gathered keys.
/// @param upper the upper rank, at least `lower`.
/// @return how many candidates were gathered, which the caller proved is within the ceiling. A rank
///         at or past that count is not placed, and the caller refuses it.
template <typename KeyT, typename SampleT, typename KeyOf>
[[nodiscard]] Result<std::uint64_t> gatherPass(const ExecutionContext& execution, CompactPlane<SampleT>& plane,
                                               const TaskCost& cost, KeyOf keyOf, KeyT prefix,
                                               std::uint32_t resolvedBits, std::uint64_t lower, std::uint64_t upper)
{
    std::fill(plane.candidateCounts, plane.candidateCounts + plane.slots, std::uint64_t{0u});
    const Status gathered = parallelForWorkers(execution, plane.segmentCount, cost, [&](WorkerTask task) {
        std::uint64_t* const buffer = plane.candidates + static_cast<std::uint64_t>(task.workerSlot) * kGatherCeiling;
        std::uint64_t& count = plane.candidateCounts[task.workerSlot];
        for (std::uint64_t segment = task.range.begin; segment < task.range.end; ++segment)
        {
            const SampleT* const begin = plane.samples + plane.segments[segment].offset;
            const SampleT* const end = begin + plane.segments[segment].count;
            for (const SampleT* element = begin; element < end; ++element)
            {
                KeyT key = 0u;
                if (!keyOf(*element, key) || !keyMatchesPrefix(key, prefix, resolvedBits))
                {
                    continue;
                }
                if (count >= kGatherCeiling)
                {
                    return normalizationFailure(kMeasureOperation, ErrorCode::Internal,
                                                "a rank descent gathered more candidates than it counted");
                }
                buffer[count++] = static_cast<std::uint64_t>(key);
            }
        }
        return Status::success();
    });
    if (gathered.failed())
    {
        return gathered;
    }
    std::uint64_t total = plane.candidateCounts[0];
    for (std::uint32_t slot = 1u; slot < plane.slots; ++slot)
    {
        const std::uint64_t* const buffer = plane.candidates + static_cast<std::uint64_t>(slot) * kGatherCeiling;
        std::copy(buffer, buffer + plane.candidateCounts[slot], plane.candidates + total);
        total += plane.candidateCounts[slot];
    }
    std::uint64_t* const keys = plane.candidates;
    if (lower < total)
    {
        std::nth_element(keys, keys + lower, keys + total);
        // Every key past the lower rank is at least its key, so the upper rank is selected from them.
        if (upper > lower && upper < total)
        {
            std::nth_element(keys + lower + 1u, keys + upper, keys + total);
        }
    }
    return total;
}

/// Finds the bin a rank falls in, from the merged histogram.
template <typename KeyT>
void locateRank(const std::uint32_t* histogram, RankCursor<KeyT>& cursor, std::uint32_t resolvedBits) noexcept
{
    std::uint64_t cumulative = 0u;
    for (std::uint32_t bin = 0u; bin < kRadixBins; ++bin)
    {
        const std::uint64_t count = histogram[bin];
        if (cursor.rank < cumulative + count)
        {
            cursor.prefix = static_cast<KeyT>((cursor.prefix << kRadixBits) | bin);
            cursor.resolvedBits = resolvedBits + kRadixBits;
            cursor.rank -= cumulative;
            cursor.binCount = count;
            return;
        }
        cumulative += count;
    }
    cursor.binCount = 0u;
}

/// The two middle ranks of the keys `keyOf` admits, by radix descent over the compact plane.
///
/// One histogram pass resolves sixteen bits of both ranks at once while they share a bin; a bin
/// small enough is gathered and sorted exactly, and a fully resolved key needs neither.
template <typename KeyT, typename SampleT, typename KeyOf>
[[nodiscard]] Result<MedianRanks> medianRanks(const ExecutionContext& execution, CompactPlane<SampleT>& plane,
                                              const TaskCost& cost, KeyOf keyOf, std::uint64_t& passes)
{
    constexpr std::uint32_t kKeyBits = sizeof(KeyT) * 8u;
    const Status first = histogramPass<KeyT>(execution, plane, cost, keyOf, KeyT{0u}, 0u);
    if (first.failed())
    {
        return first;
    }
    ++passes;
    std::uint64_t total = 0u;
    for (std::uint32_t bin = 0u; bin < kRadixBins; ++bin)
    {
        total += plane.histograms[bin];
    }
    if (total == 0u)
    {
        return normalizationFailure(kMeasureOperation, ErrorCode::EmptySelection,
                                    "the clipping interval retained no sample");
    }
    RankCursor<KeyT> lower;
    RankCursor<KeyT> upper;
    lower.rank = (total - 1u) / 2u;
    upper.rank = total / 2u;
    locateRank(plane.histograms, lower, 0u);
    locateRank(plane.histograms, upper, 0u);
    const auto settle = [&](RankCursor<KeyT>& cursor, RankCursor<KeyT>* companion) -> Status {
        while (!cursor.resolved)
        {
            const bool shared = companion != nullptr && !companion->resolved && companion->prefix == cursor.prefix &&
                                companion->resolvedBits == cursor.resolvedBits;
            if (cursor.resolvedBits == kKeyBits)
            {
                cursor.value = valueOfKey(cursor.prefix);
                cursor.resolved = true;
                if (shared)
                {
                    companion->value = cursor.value;
                    companion->resolved = true;
                }
                continue;
            }
            if (cursor.binCount <= kGatherCeiling)
            {
                const std::uint64_t otherRank = shared ? companion->rank : cursor.rank;
                const std::uint64_t lowerRank = (std::min)(cursor.rank, otherRank);
                const std::uint64_t upperRank = (std::max)(cursor.rank, otherRank);
                const Result<std::uint64_t> gathered = gatherPass<KeyT>(execution, plane, cost, keyOf, cursor.prefix,
                                                                        cursor.resolvedBits, lowerRank, upperRank);
                if (!gathered.hasValue())
                {
                    return gathered.status();
                }
                ++passes;
                if (cursor.rank >= *gathered || (shared && companion->rank >= *gathered))
                {
                    return normalizationFailure(kMeasureOperation, ErrorCode::Internal,
                                                "a rank descent gathered fewer candidates than it counted");
                }
                cursor.value = valueOfKey(static_cast<KeyT>(plane.candidates[cursor.rank]));
                cursor.resolved = true;
                if (shared)
                {
                    companion->value = valueOfKey(static_cast<KeyT>(plane.candidates[companion->rank]));
                    companion->resolved = true;
                }
                continue;
            }
            const Status counted =
                histogramPass<KeyT>(execution, plane, cost, keyOf, cursor.prefix, cursor.resolvedBits);
            if (counted.failed())
            {
                return counted;
            }
            ++passes;
            const std::uint32_t resolvedBits = cursor.resolvedBits;
            locateRank(plane.histograms, cursor, resolvedBits);
            if (shared)
            {
                locateRank(plane.histograms, *companion, resolvedBits);
            }
            if (cursor.binCount == 0u || (shared && companion->binCount == 0u))
            {
                return normalizationFailure(kMeasureOperation, ErrorCode::Internal,
                                            "a rank descent lost its rank between passes");
            }
        }
        return Status::success();
    };
    const Status lowerSettled = settle(lower, &upper);
    if (lowerSettled.failed())
    {
        return lowerSettled;
    }
    const Status upperSettled = settle(upper, nullptr);
    if (upperSettled.failed())
    {
        return upperSettled;
    }
    return MedianRanks{.total = total, .lower = lower.value, .upper = upper.value};
}

/// The median of two middle ranks, under the linear interpolation rule at one half.
[[nodiscard]] double medianOf(const MedianRanks& ranks) noexcept
{
    return ranks.lower + 0.5 * (ranks.upper - ranks.lower);
}

/// Accumulates the corrected biweight scale over the compact plane, one partial per segment.
///
/// With `u = (x - location) / (9 * clippedDeviation)` over the samples inside `[low, high]`, and a
/// sample with `|u| >= 1` contributing nothing, the scale is
/// `0.991 * sqrt(n * sum((x - location)^2 (1 - u^2)^4)) / |sum((1 - u^2)(1 - 5u^2))|`: the biweight
/// midvariance with its tuning constant and correction factor.
template <typename SampleT>
[[nodiscard]] Status compactBiweight(const ExecutionContext& execution, CompactPlane<SampleT>& plane,
                                     const TaskCost& cost, double low, double high, double location,
                                     double clippedDeviation, std::uint64_t clippedCount, double& scale)
{
    const double factor = 1.0 / (kBiweightTuning * clippedDeviation);
    const Status accumulated = parallelFor(execution, plane.segmentCount, cost, [&](TaskRange range) {
        for (std::uint64_t segment = range.begin; segment < range.end; ++segment)
        {
            double up = 0.0;
            double down = 0.0;
            const SampleT* const begin = plane.samples + plane.segments[segment].offset;
            const SampleT* const end = begin + plane.segments[segment].count;
            for (const SampleT* element = begin; element < end; ++element)
            {
                const double value = static_cast<double>(*element);
                if (value < low || value > high)
                {
                    continue;
                }
                const double distance = value - location;
                const double normalized = distance * factor;
                const double squared = std::abs(normalized) < 1.0 ? normalized * normalized : 1.0;
                const double retained = (1.0 - squared) * (1.0 - squared);
                const double weighted = distance * retained;
                up += weighted * weighted;
                down += (1.0 - squared) * (1.0 - 5.0 * squared);
            }
            plane.partials[2u * segment] = up;
            plane.partials[2u * segment + 1u] = down;
        }
        return Status::success();
    });
    if (accumulated.failed())
    {
        return accumulated;
    }
    double up = 0.0;
    double down = 0.0;
    for (std::uint32_t segment = 0u; segment < plane.segmentCount; ++segment)
    {
        up += plane.partials[2u * segment];
        down += plane.partials[2u * segment + 1u];
    }
    const double variance = down != 0.0 ? static_cast<double>(clippedCount) * up / (down * down) : 0.0;
    scale = std::sqrt(variance) * kIterativeKSigmaScaleCorrection;
    return Status::success();
}

/// Measures one channel by gathering its admitted samples once into engine memory.
///
/// Every rank is exact and every accumulation is partitioned by segment, so the result is
/// bit-identical at every admitted width. The memory is one resident sample per pixel of the region
/// plus the worker-private histograms, taken for this call and released with it.
template <typename SampleT>
[[nodiscard]] Status measureChannelCompact(const ExecutionContext& execution, const KernelPlaneSource& source,
                                           const KernelValidityView& validity, const KernelPlaneSelection& region,
                                           ScaleEstimator estimator, NormalizationChannelStatistics& out,
                                           MeasurementCost& cost)
{
    using KeyT = std::conditional_t<std::is_same_v<SampleT, float>, std::uint32_t, std::uint64_t>;
    const std::uint64_t pixels = static_cast<std::uint64_t>(region.width) * region.height;
    if (pixels == 0u)
    {
        return normalizationFailure(kMeasureOperation, ErrorCode::EmptySelection, "the measured region holds no pixel");
    }
    const std::uint32_t slots = (std::max)(execution.admittedConcurrency(), 1u);
    const auto layout = compactWorkspaceLayout(region.width, region.height, source.sampleType, slots);
    if (!layout.hasValue())
    {
        return layout.status();
    }
    const std::uint64_t chunkCount = layout->chunks;
    const std::uint64_t rowsPerChunk = (region.height + chunkCount - 1u) / chunkCount;
    const std::uint64_t samplesBytes = layout->samples;
    const std::uint64_t segmentsBytes = layout->segments;
    const std::uint64_t histogramBytes = layout->histograms;
    const std::uint64_t candidateBytes = layout->candidates;
    const std::uint64_t candidateCountBytes = layout->candidateCounts;
    const std::uint64_t totalBytes = layout->total;
    Result<plane_detail::ScopedContextMemory> memory =
        plane_detail::ScopedContextMemory::take(execution, totalBytes, 64u, kMeasureOperation);
    if (!memory.hasValue())
    {
        return memory.status();
    }
    auto* const base = static_cast<std::byte*>(memory->data());
    CompactPlane<SampleT> plane;
    plane.samples = reinterpret_cast<SampleT*>(base);
    plane.segments = reinterpret_cast<CompactSegment*>(base + samplesBytes);
    plane.histograms = reinterpret_cast<std::uint32_t*>(base + samplesBytes + segmentsBytes);
    plane.candidates = reinterpret_cast<std::uint64_t*>(base + samplesBytes + segmentsBytes + histogramBytes);
    plane.candidateCounts =
        reinterpret_cast<std::uint64_t*>(base + samplesBytes + segmentsBytes + histogramBytes + candidateBytes);
    plane.partials = reinterpret_cast<double*>(base + samplesBytes + segmentsBytes + histogramBytes + candidateBytes +
                                               candidateCountBytes);
    plane.segmentCount = static_cast<std::uint32_t>(chunkCount);
    plane.slots = slots;
    cost.compacted = true;
    cost.peakScratchBytes = (std::max)(cost.peakScratchBytes, totalBytes);

    const Status compacted = compactRegion<SampleT>(execution, source, validity, region, rowsPerChunk, plane);
    if (compacted.failed())
    {
        return compacted;
    }
    std::uint64_t passes = 1u;
    const TaskCost passCost = compactPassCost(plane, rowsPerChunk, region.width);
    out.selection = ValidityCount{.valid = plane.admitted, .total = pixels};
    out.clippedSelection = ValidityCount{};

    const auto valueKey = [](SampleT value, KeyT& key) {
        key = sortableKey(value);
        return true;
    };
    const Result<MedianRanks> medianDescent = medianRanks<KeyT>(execution, plane, passCost, valueKey, passes);
    if (!medianDescent.hasValue())
    {
        return medianDescent.status();
    }
    out.median = medianOf(*medianDescent);
    const double center = out.median;
    const auto deviationKey = [center](SampleT value, std::uint64_t& key) {
        key = sortableKey(std::abs(static_cast<double>(value) - center));
        return true;
    };
    const Result<MedianRanks> deviationDescent =
        medianRanks<std::uint64_t>(execution, plane, passCost, deviationKey, passes);
    if (!deviationDescent.hasValue())
    {
        return deviationDescent.status();
    }
    out.medianAbsoluteDeviation = medianOf(*deviationDescent);
    if (estimator == ScaleEstimator::MedianAbsoluteDeviation || !(out.medianAbsoluteDeviation > 0.0))
    {
        out.location = out.median;
        out.scale = estimator == ScaleEstimator::MedianAbsoluteDeviation ? out.medianAbsoluteDeviation : 0.0;
        cost.planeScans += passes;
        return Status::success();
    }
    const double low = (std::max)(plane.minimum, out.median - kIterativeKSigmaClipWidth * out.medianAbsoluteDeviation);
    const double high = (std::min)(plane.maximum, out.median + kIterativeKSigmaClipWidth * out.medianAbsoluteDeviation);
    const auto clippedValueKey = [low, high](SampleT value, KeyT& key) {
        const double widened = static_cast<double>(value);
        if (widened < low || widened > high)
        {
            return false;
        }
        key = sortableKey(value);
        return true;
    };
    const Result<MedianRanks> clippedDescent = medianRanks<KeyT>(execution, plane, passCost, clippedValueKey, passes);
    if (!clippedDescent.hasValue())
    {
        return clippedDescent.status();
    }
    out.clippedSelection = ValidityCount{.valid = clippedDescent->total, .total = plane.admitted};
    out.location = medianOf(*clippedDescent);
    const double clippedCenter = out.location;
    const auto clippedDeviationKey = [low, high, clippedCenter](SampleT value, std::uint64_t& key) {
        const double widened = static_cast<double>(value);
        if (widened < low || widened > high)
        {
            return false;
        }
        key = sortableKey(std::abs(widened - clippedCenter));
        return true;
    };
    const Result<MedianRanks> clippedDeviationDescent =
        medianRanks<std::uint64_t>(execution, plane, passCost, clippedDeviationKey, passes);
    if (!clippedDeviationDescent.hasValue())
    {
        return clippedDeviationDescent.status();
    }
    const double clippedDeviation = medianOf(*clippedDeviationDescent);
    if (!(clippedDeviation > 0.0))
    {
        out.scale = 0.0;
        cost.planeScans += passes;
        return Status::success();
    }
    const Status biweight = compactBiweight<SampleT>(execution, plane, passCost, low, high, clippedCenter,
                                                     clippedDeviation, clippedDescent->total, out.scale);
    if (biweight.failed())
    {
        return biweight;
    }
    ++passes;
    cost.planeScans += passes;
    return Status::success();
}

/// Accumulates the corrected biweight scale of the clipped population over a thread-independent
/// partition of its rows, reading the source where it lies.
template <typename SampleT>
[[nodiscard]] Status accumulateClippedBiweight(const ExecutionContext& execution, const KernelPlaneSource& source,
                                               const KernelValidityView& validity, const KernelPlaneSelection& region,
                                               double low, double high, double location, double clippedDeviation,
                                               std::uint64_t clippedCount, double& scale, MeasurementCost& cost)
{
    const std::uint64_t rows = region.height;
    const TaskCost rowCost{.bytesPerItem = static_cast<std::uint64_t>(region.width) * sizeof(SampleT),
                           .operationsPerItem = static_cast<std::uint64_t>(region.width) * 12u};
    const std::uint64_t chunkCount = (std::max)(deterministicChunkCount(rows, rowCost), std::uint64_t{1});
    const std::uint64_t rowsPerChunk = (rows + chunkCount - 1u) / chunkCount;
    const std::uint64_t partialBytes = chunkCount * 2u * sizeof(double);
    Result<plane_detail::ScopedContextMemory> partials =
        plane_detail::ScopedContextMemory::take(execution, partialBytes, alignof(double), kMeasureOperation);
    if (!partials.hasValue())
    {
        return partials.status();
    }
    double* const sums = static_cast<double*>(partials->data());
    const plane_detail::PlaneSampleReader<SampleT> reader{source, validity};
    const double factor = 1.0 / (kBiweightTuning * clippedDeviation);
    const TaskCost chunkCost{.bytesPerItem = rowsPerChunk * rowCost.bytesPerItem,
                             .operationsPerItem = rowsPerChunk * rowCost.operationsPerItem};
    const Status accumulated = parallelFor(execution, chunkCount, chunkCost, [&](TaskRange range) {
        for (std::uint64_t chunk = range.begin; chunk < range.end; ++chunk)
        {
            double up = 0.0;
            double down = 0.0;
            const std::uint64_t firstRow = chunk * rowsPerChunk;
            const std::uint64_t lastRow = (std::min)(rows, firstRow + rowsPerChunk);
            for (std::uint64_t relativeY = firstRow; relativeY < lastRow; ++relativeY)
            {
                const auto y = static_cast<std::uint32_t>(region.originY + relativeY);
                const SampleT* const row = reader.rowStart(y);
                for (std::uint32_t relativeX = 0u; relativeX < region.width; ++relativeX)
                {
                    const std::uint32_t x = region.originX + relativeX;
                    double value = 0.0;
                    if (!reader.selected(row[static_cast<std::size_t>(x) * reader.columnStride()], x, y, value) ||
                        value < low || value > high)
                    {
                        continue;
                    }
                    const double distance = value - location;
                    const double normalized = distance * factor;
                    const double squared = std::abs(normalized) < 1.0 ? normalized * normalized : 1.0;
                    const double retained = (1.0 - squared) * (1.0 - squared);
                    const double weighted = distance * retained;
                    up += weighted * weighted;
                    down += (1.0 - squared) * (1.0 - 5.0 * squared);
                }
            }
            sums[2u * chunk] = up;
            sums[2u * chunk + 1u] = down;
        }
        return Status::success();
    });
    if (accumulated.failed())
    {
        return accumulated;
    }
    double up = 0.0;
    double down = 0.0;
    for (std::uint64_t chunk = 0u; chunk < chunkCount; ++chunk)
    {
        up += sums[2u * chunk];
        down += sums[2u * chunk + 1u];
    }
    const double variance = down != 0.0 ? static_cast<double>(clippedCount) * up / (down * down) : 0.0;
    scale = std::sqrt(variance) * kIterativeKSigmaScaleCorrection;
    cost.planeScans += 1u;
    cost.peakScratchBytes = (std::max)(cost.peakScratchBytes, partialBytes);
    return Status::success();
}

/// Measures one channel where it lies, through the order-statistics kernel.
///
/// The path a refused compaction falls back to: it holds no working element per sample and pays
/// several strided passes per rank instead.
template <typename SampleT>
[[nodiscard]] Status measureChannelStrided(const ExecutionContext& execution, const KernelPlaneSource& source,
                                           const KernelValidityView& validity, const KernelPlaneSelection& region,
                                           ScaleEstimator estimator, NormalizationChannelStatistics& out,
                                           MeasurementCost& cost)
{
    const PlaneOrderRequest request{.interpolation = QuantileInterpolation::Linear, .medianAbsoluteDeviation = true};
    const Result<PlaneOrderStatistics> order = selectPlaneOrderStatisticsOver(
        execution, source, std::span<const double>{}, std::span<double>{}, request, validity, region);
    if (!order.hasValue())
    {
        return order.status();
    }
    cost.planeScans += order->planeScans;
    cost.peakScratchBytes = (std::max)(cost.peakScratchBytes, order->peakScratchBytes);
    out.selection = order->run.selection;
    out.median = order->median;
    out.medianAbsoluteDeviation = order->medianAbsoluteDeviation;
    out.clippedSelection = ValidityCount{};
    if (estimator == ScaleEstimator::MedianAbsoluteDeviation || !(order->medianAbsoluteDeviation > 0.0))
    {
        out.location = order->median;
        out.scale = estimator == ScaleEstimator::MedianAbsoluteDeviation ? order->medianAbsoluteDeviation : 0.0;
        return Status::success();
    }
    const double low =
        (std::max)(order->minimum, order->median - kIterativeKSigmaClipWidth * order->medianAbsoluteDeviation);
    const double high =
        (std::min)(order->maximum, order->median + kIterativeKSigmaClipWidth * order->medianAbsoluteDeviation);
    KernelPlaneSelection clippedRegion = region;
    clippedRegion.valueWindow = KernelValueWindow{.lowest = low, .highest = high};
    const Result<PlaneOrderStatistics> clipped = selectPlaneOrderStatisticsOver(
        execution, source, std::span<const double>{}, std::span<double>{}, request, validity, clippedRegion);
    if (!clipped.hasValue())
    {
        return clipped.status();
    }
    cost.planeScans += clipped->planeScans;
    cost.peakScratchBytes = (std::max)(cost.peakScratchBytes, clipped->peakScratchBytes);
    out.clippedSelection = ValidityCount{.valid = clipped->run.selection.valid, .total = order->run.selection.valid};
    out.location = clipped->median;
    if (!(clipped->medianAbsoluteDeviation > 0.0))
    {
        out.scale = 0.0;
        return Status::success();
    }
    return accumulateClippedBiweight<SampleT>(execution, source, validity, region, low, high, clipped->median,
                                              clipped->medianAbsoluteDeviation, clipped->run.selection.valid, out.scale,
                                              cost);
}

template <typename SampleT>
[[nodiscard]] Status measureChannelTyped(const ExecutionContext& execution, const KernelPlaneSource& source,
                                         const KernelValidityView& validity, const KernelPlaneSelection& region,
                                         ScaleEstimator estimator, NormalizationChannelStatistics& out,
                                         MeasurementCost& cost)
{
    const Status compact = measureChannelCompact<SampleT>(execution, source, validity, region, estimator, out, cost);
    if (!compact.failed() ||
        (compact.code() != ErrorCode::MemoryBudgetExceeded && compact.code() != ErrorCode::AllocationFailed))
    {
        return compact;
    }
    cost.compacted = false;
    return measureChannelStrided<SampleT>(execution, source, validity, region, estimator, out, cost);
}

[[nodiscard]] Status measureChannel(const ExecutionContext& execution, const KernelPlaneSource& source,
                                    const KernelValidityView& validity, const KernelPlaneSelection& region,
                                    ScaleEstimator estimator, NormalizationChannelStatistics& out,
                                    MeasurementCost& cost)
{
    return source.sampleType == SampleType::Float32
               ? measureChannelTyped<float>(execution, source, validity, region, estimator, out, cost)
               : measureChannelTyped<double>(execution, source, validity, region, estimator, out, cost);
}

/// The median of the leading `count` values, ranking them in place.
[[nodiscard]] double medianInPlace(double* values, std::uint32_t count) noexcept
{
    std::sort(values, values + count);
    if (count % 2u == 1u)
    {
        return values[count / 2u];
    }
    const double lower = values[count / 2u - 1u];
    const double upper = values[count / 2u];
    return lower + 0.5 * (upper - lower);
}

[[nodiscard]] bool channelIsFinite(const NormalizationChannelStatistics& channel) noexcept
{
    return std::isfinite(channel.median) && std::isfinite(channel.medianAbsoluteDeviation) &&
           std::isfinite(channel.location) && std::isfinite(channel.scale);
}

[[nodiscard]] Status checkCoefficients(const FrameNormalization& normalization, const char* operation)
{
    if (!isNormalizationMethod(normalization.method) || !isScaleEstimator(normalization.estimator))
    {
        return normalizationFailure(operation, ErrorCode::InvalidArgument,
                                    "the normalization names an unknown method or estimator");
    }
    if (normalization.channelCount == 0u || normalization.channelCount > kMaxNormalizationChannels)
    {
        return normalizationFailure(operation, ErrorCode::InvalidArgument,
                                    "the normalization holds no channels or more than the engine matches");
    }
    for (std::uint32_t channel = 0u; channel < normalization.channelCount; ++channel)
    {
        const NormalizationChannelCoefficients& coefficients = normalization.channels[channel];
        if (!std::isfinite(coefficients.scale) || coefficients.scale == 0.0 || !std::isfinite(coefficients.offset))
        {
            return normalizationFailure(operation, ErrorCode::InvalidArgument,
                                        "a normalization scale must be finite and non-zero and an offset finite");
        }
    }
    return Status::success();
}

/// A union-find over frame indices, held in memory the caller took from the context.
class FrameComponents
{
public:
    FrameComponents(std::uint32_t* parents, std::uint32_t count) noexcept : m_parents(parents), m_count(count)
    {
        for (std::uint32_t index = 0u; index < count; ++index)
        {
            parents[index] = index;
        }
    }

    [[nodiscard]] std::uint32_t find(std::uint32_t index) noexcept
    {
        while (m_parents[index] != index)
        {
            m_parents[index] = m_parents[m_parents[index]];
            index = m_parents[index];
        }
        return index;
    }

    /// Joins two frames' components.
    ///
    /// @return true when they were separate until now.
    bool unite(std::uint32_t left, std::uint32_t right) noexcept
    {
        const std::uint32_t leftRoot = find(left);
        const std::uint32_t rightRoot = find(right);
        if (leftRoot == rightRoot)
        {
            return false;
        }
        m_parents[(std::max)(leftRoot, rightRoot)] = (std::min)(leftRoot, rightRoot);
        return true;
    }

    [[nodiscard]] bool allJoinedTo(std::uint32_t reference) noexcept
    {
        const std::uint32_t root = find(reference);
        for (std::uint32_t index = 0u; index < m_count; ++index)
        {
            if (find(index) != root)
            {
                return false;
            }
        }
        return true;
    }

private:
    std::uint32_t* m_parents;
    std::uint32_t m_count;
};

/// The memory one overlap solve holds: an augmented dense system, the variable map, the component
/// table and three per-frame solution vectors.
struct SolveWorkspace
{
    double* matrix = nullptr;
    std::int32_t* variableOf = nullptr;
    std::uint32_t* parents = nullptr;
    double* offsets = nullptr;
    double* factors = nullptr;
    double* dispersions = nullptr;
    std::uint32_t frameCount = 0u;
    std::uint32_t variableCount = 0u;
};

/// Reduces an augmented `m` by `m + 1` system in place by elimination with partial pivoting.
///
/// @return false when a pivot column is entirely zero, which is a singular system.
[[nodiscard]] Result<bool> eliminate(const ExecutionContext& execution, double* matrix, std::uint32_t variables)
{
    const std::uint64_t stride = static_cast<std::uint64_t>(variables) + 1u;
    for (std::uint32_t pivot = 0u; pivot < variables; ++pivot)
    {
        if ((pivot & 63u) == 0u)
        {
            const Status running = execution.checkCancellation();
            if (running.failed())
            {
                return running;
            }
        }
        std::uint32_t best = pivot;
        double magnitude = std::abs(matrix[pivot * stride + pivot]);
        for (std::uint32_t row = pivot + 1u; row < variables; ++row)
        {
            const double candidate = std::abs(matrix[row * stride + pivot]);
            if (candidate > magnitude)
            {
                magnitude = candidate;
                best = row;
            }
        }
        if (magnitude == 0.0 || !std::isfinite(magnitude))
        {
            return false;
        }
        if (best != pivot)
        {
            for (std::uint64_t column = 0u; column < stride; ++column)
            {
                std::swap(matrix[pivot * stride + column], matrix[best * stride + column]);
            }
        }
        const double diagonal = matrix[pivot * stride + pivot];
        for (std::uint32_t row = pivot + 1u; row < variables; ++row)
        {
            const double factor = matrix[row * stride + pivot] / diagonal;
            if (factor == 0.0)
            {
                continue;
            }
            for (std::uint64_t column = pivot; column < stride; ++column)
            {
                matrix[row * stride + column] -= factor * matrix[pivot * stride + column];
            }
        }
    }
    for (std::uint32_t reversed = variables; reversed > 0u; --reversed)
    {
        const std::uint32_t row = reversed - 1u;
        double value = matrix[row * stride + variables];
        for (std::uint32_t column = row + 1u; column < variables; ++column)
        {
            value -= matrix[row * stride + column] * matrix[column * stride + variables];
        }
        matrix[row * stride + variables] = value / matrix[row * stride + row];
    }
    return true;
}

/// Solves the additive overlap system: offsets `c_i` with `left + c_i` close to `right + c_j`.
template <typename LeftF, typename RightF>
[[nodiscard]] Result<bool> solveAdditiveSystem(const ExecutionContext& execution, SolveWorkspace& workspace,
                                               std::span<const OverlapMeasurement> pairs, std::uint32_t reference,
                                               LeftF&& left, RightF&& right, double* out)
{
    const std::uint32_t frames = workspace.frameCount;
    for (std::uint32_t index = 0u; index < frames; ++index)
    {
        out[index] = 0.0;
    }
    if (frames == 1u)
    {
        return true;
    }
    const auto usable = [&](const OverlapMeasurement& pair) {
        return pair.sharedSamples > 0u && std::isfinite(left(pair)) && std::isfinite(right(pair));
    };
    FrameComponents components{workspace.parents, frames};
    for (const OverlapMeasurement& pair : pairs)
    {
        if (usable(pair))
        {
            components.unite(pair.frameA, pair.frameB);
        }
    }
    if (!components.allJoinedTo(reference))
    {
        return false;
    }
    const std::uint32_t variables = workspace.variableCount;
    const std::uint64_t stride = static_cast<std::uint64_t>(variables) + 1u;
    std::fill(workspace.matrix, workspace.matrix + static_cast<std::uint64_t>(variables) * stride, 0.0);
    double* const matrix = workspace.matrix;
    for (const OverlapMeasurement& pair : pairs)
    {
        if (!usable(pair))
        {
            continue;
        }
        const double delta = right(pair) - left(pair);
        const double weight = static_cast<double>((std::max)(pair.sharedSamples, std::uint64_t{1}));
        const std::int32_t i = workspace.variableOf[pair.frameA];
        const std::int32_t j = workspace.variableOf[pair.frameB];
        if (i >= 0)
        {
            matrix[static_cast<std::uint64_t>(i) * stride + static_cast<std::uint64_t>(i)] += weight;
            matrix[static_cast<std::uint64_t>(i) * stride + variables] += weight * delta;
        }
        if (j >= 0)
        {
            matrix[static_cast<std::uint64_t>(j) * stride + static_cast<std::uint64_t>(j)] += weight;
            matrix[static_cast<std::uint64_t>(j) * stride + variables] -= weight * delta;
        }
        if (i >= 0 && j >= 0)
        {
            matrix[static_cast<std::uint64_t>(i) * stride + static_cast<std::uint64_t>(j)] -= weight;
            matrix[static_cast<std::uint64_t>(j) * stride + static_cast<std::uint64_t>(i)] -= weight;
        }
    }
    const Result<bool> solved = eliminate(execution, matrix, variables);
    if (!solved.hasValue() || !*solved)
    {
        return solved;
    }
    for (std::uint32_t frame = 0u; frame < frames; ++frame)
    {
        const std::int32_t variable = workspace.variableOf[frame];
        if (variable < 0)
        {
            continue;
        }
        const double value = matrix[static_cast<std::uint64_t>(variable) * stride + variables];
        if (!std::isfinite(value))
        {
            return false;
        }
        out[frame] = value;
    }
    return true;
}

/// Solves the multiplicative overlap system: factors `s_i` with `left * s_i` close to `right * s_j`.
template <typename LeftF, typename RightF>
[[nodiscard]] Result<bool> solveMultiplicativeSystem(const ExecutionContext& execution, SolveWorkspace& workspace,
                                                     std::span<const OverlapMeasurement> pairs, std::uint32_t reference,
                                                     LeftF&& left, RightF&& right, double* out)
{
    const std::uint32_t frames = workspace.frameCount;
    for (std::uint32_t index = 0u; index < frames; ++index)
    {
        out[index] = 1.0;
    }
    if (frames == 1u)
    {
        return true;
    }
    const auto usable = [&](const OverlapMeasurement& pair) {
        const double a = left(pair);
        const double b = right(pair);
        return pair.sharedSamples > 0u && std::isfinite(a) && std::isfinite(b) && a > kOverlapValueEpsilon &&
               b > kOverlapValueEpsilon;
    };
    FrameComponents components{workspace.parents, frames};
    for (const OverlapMeasurement& pair : pairs)
    {
        if (usable(pair))
        {
            components.unite(pair.frameA, pair.frameB);
        }
    }
    if (!components.allJoinedTo(reference))
    {
        return false;
    }
    const std::uint32_t variables = workspace.variableCount;
    const std::uint64_t stride = static_cast<std::uint64_t>(variables) + 1u;
    std::fill(workspace.matrix, workspace.matrix + static_cast<std::uint64_t>(variables) * stride, 0.0);
    double* const matrix = workspace.matrix;
    for (const OverlapMeasurement& pair : pairs)
    {
        if (!usable(pair))
        {
            continue;
        }
        const double a = left(pair);
        const double b = right(pair);
        const double weight = static_cast<double>((std::max)(pair.sharedSamples, std::uint64_t{1}));
        const std::int32_t i = workspace.variableOf[pair.frameA];
        const std::int32_t j = workspace.variableOf[pair.frameB];
        if (i >= 0)
        {
            matrix[static_cast<std::uint64_t>(i) * stride + static_cast<std::uint64_t>(i)] += weight * a * a;
        }
        if (j >= 0)
        {
            matrix[static_cast<std::uint64_t>(j) * stride + static_cast<std::uint64_t>(j)] += weight * b * b;
        }
        if (i >= 0 && j >= 0)
        {
            const double cross = weight * a * b;
            matrix[static_cast<std::uint64_t>(i) * stride + static_cast<std::uint64_t>(j)] -= cross;
            matrix[static_cast<std::uint64_t>(j) * stride + static_cast<std::uint64_t>(i)] -= cross;
        }
        else if (i >= 0)
        {
            matrix[static_cast<std::uint64_t>(i) * stride + variables] += weight * a * b;
        }
        else if (j >= 0)
        {
            matrix[static_cast<std::uint64_t>(j) * stride + variables] += weight * a * b;
        }
    }
    const Result<bool> solved = eliminate(execution, matrix, variables);
    if (!solved.hasValue() || !*solved)
    {
        return solved;
    }
    for (std::uint32_t frame = 0u; frame < frames; ++frame)
    {
        const std::int32_t variable = workspace.variableOf[frame];
        if (variable < 0)
        {
            continue;
        }
        const double value = matrix[static_cast<std::uint64_t>(variable) * stride + variables];
        if (!std::isfinite(value))
        {
            return false;
        }
        out[frame] = value;
    }
    return true;
}

[[nodiscard]] bool rangesOverlap(const void* left, std::uint64_t leftBytes, const void* right,
                                 std::uint64_t rightBytes) noexcept
{
    const std::uintptr_t leftBegin = reinterpret_cast<std::uintptr_t>(left);
    const std::uintptr_t rightBegin = reinterpret_cast<std::uintptr_t>(right);
    return leftBegin < rightBegin + static_cast<std::uintptr_t>(rightBytes) &&
           rightBegin < leftBegin + static_cast<std::uintptr_t>(leftBytes);
}

[[nodiscard]] bool sameArrangement(const ImageLayout& left, const ImageLayout& right) noexcept
{
    return left.samplesPerPixel() == right.samplesPerPixel() && left.planeCount() == right.planeCount();
}

[[nodiscard]] bool sameStrides(const ImageLayout& left, const ImageLayout& right) noexcept
{
    return sameArrangement(left, right) && left.rowStrideBytes() == right.rowStrideBytes() &&
           left.planeStrideBytes() == right.planeStrideBytes();
}

/// The scalar authority for one contiguous row, shared with the compiled tables' contract.
template <typename SampleT>
[[nodiscard]] NormalizationRunOutcome referenceNormalizationRun(const SampleT* source, SampleT* destination,
                                                                std::uint64_t pixelCount,
                                                                const NormalizationRunParameters& parameters) noexcept
{
    NormalizationRunOutcome outcome{};
    for (std::uint64_t pixel = 0u; pixel < pixelCount; ++pixel)
    {
        for (std::uint32_t channel = 0u; channel < parameters.channelCount; ++channel)
        {
            const std::uint64_t sample = pixel * parameters.channelCount + channel;
            const SampleT value = source[sample];
            bool admitted = true;
            if (parameters.rule == ValidityRule::FiniteOnly)
            {
                admitted =
                    value >= -(std::numeric_limits<SampleT>::max)() && value <= (std::numeric_limits<SampleT>::max)();
            }
            else if (parameters.rule == ValidityRule::ZeroIsNoData)
            {
                admitted = value != static_cast<SampleT>(0);
            }
            if (!admitted)
            {
                destination[sample] = value;
                ++outcome.samplesPreserved;
                continue;
            }
            const double scaled = static_cast<double>(value) * parameters.scale[channel];
            SampleT written =
                static_cast<SampleT>(parameters.multiplicative ? scaled : scaled + parameters.offset[channel]);
            if (parameters.rule == ValidityRule::ZeroIsNoData && written == static_cast<SampleT>(0))
            {
                written = std::copysign((std::numeric_limits<SampleT>::min)(), static_cast<SampleT>(scaled));
                ++outcome.noDataCollisions;
            }
            destination[sample] = written;
            ++outcome.samplesTransformed;
        }
    }
    return outcome;
}

template <typename SampleT>
[[nodiscard]] Result<NormalizationApplyResult> runRows(
    const ExecutionContext& execution, const FrameNormalization& normalization, const NormalizationFrameView& source,
    void* destination, const ImageLayout& destinationLayout, NormalizationExecutionPath path,
    const BackendSelection& backend,
    NormalizationRunOutcome (*run)(const SampleT*, SampleT*, std::uint64_t, const NormalizationRunParameters&) noexcept)
{
    const ImageExtent& extent = source.layout().extent();
    const bool planar = source.layout().samplesPerPixel() == 1u;
    const std::uint64_t taskCount = static_cast<std::uint64_t>(extent.height()) * (planar ? extent.channelCount() : 1u);
    const TaskCost cost{.bytesPerItem =
                            static_cast<std::uint64_t>(extent.width()) * extent.channelCount() * sizeof(SampleT) * 2u,
                        .operationsPerItem = static_cast<std::uint64_t>(extent.width()) * extent.channelCount() * 4u};
    std::array<double, kMaxNormalizationChannels> scales{};
    std::array<double, kMaxNormalizationChannels> offsets{};
    for (std::uint32_t channel = 0u; channel < normalization.channelCount; ++channel)
    {
        scales[channel] = normalization.channels[channel].scale;
        offsets[channel] = normalization.channels[channel].offset;
    }
    const bool multiplicative = normalizationMethodIsMultiplicative(normalization.method);
    const ValidityRule rule = source.validity().descriptor.rule();
    std::atomic<std::uint64_t> transformed{0u};
    std::atomic<std::uint64_t> preserved{0u};
    std::atomic<std::uint64_t> collisions{0u};
    const Status completed = parallelFor(execution, taskCount, cost, [&](TaskRange range) {
        std::uint64_t localTransformed = 0u;
        std::uint64_t localPreserved = 0u;
        std::uint64_t localCollisions = 0u;
        for (std::uint64_t task = range.begin; task < range.end; ++task)
        {
            const std::uint32_t channel = planar ? static_cast<std::uint32_t>(task / extent.height()) : 0u;
            const std::uint32_t y =
                planar ? static_cast<std::uint32_t>(task % extent.height()) : static_cast<std::uint32_t>(task);
            const SampleT* const sourceRow = sampleAddress<SampleT>(source, 0u, y, channel);
            SampleT* const destinationRow = sampleAddress<SampleT>(destination, destinationLayout, 0u, y, channel);
            const NormalizationRunParameters parameters{.scale = planar ? &scales[channel] : scales.data(),
                                                        .offset = planar ? &offsets[channel] : offsets.data(),
                                                        .channelCount = planar ? 1u : extent.channelCount(),
                                                        .rule = rule,
                                                        .multiplicative = multiplicative};
            const NormalizationRunOutcome row = run(sourceRow, destinationRow, extent.width(), parameters);
            localTransformed += row.samplesTransformed;
            localPreserved += row.samplesPreserved;
            localCollisions += row.noDataCollisions;
        }
        transformed.fetch_add(localTransformed, std::memory_order_relaxed);
        preserved.fetch_add(localPreserved, std::memory_order_relaxed);
        collisions.fetch_add(localCollisions, std::memory_order_relaxed);
        return Status::success();
    });
    if (completed.failed())
    {
        return completed;
    }
    const std::uint64_t written =
        transformed.load(std::memory_order_relaxed) + preserved.load(std::memory_order_relaxed);
    return NormalizationApplyResult{.backend = backend,
                                    .path = path,
                                    .samplesWritten = written,
                                    .samplesTransformed = transformed.load(std::memory_order_relaxed),
                                    .samplesPreserved = preserved.load(std::memory_order_relaxed),
                                    .noDataCollisions = collisions.load(std::memory_order_relaxed),
                                    .plannedWorkers = plannedWorkerCount(execution, taskCount, cost),
                                    .imagePasses = 1u};
}

/// The scalar oracle over every arrangement and validity rule, mask included.
template <typename SampleT>
[[nodiscard]] Result<NormalizationApplyResult>
runScalar(const ExecutionContext& execution, const FrameNormalization& normalization,
          const NormalizationFrameView& source, void* destination, const ImageLayout& destinationLayout)
{
    const ImageExtent& extent = source.layout().extent();
    const std::uint64_t rows = extent.height();
    const TaskCost cost{.bytesPerItem =
                            static_cast<std::uint64_t>(extent.width()) * extent.channelCount() * sizeof(SampleT) * 2u,
                        .operationsPerItem = static_cast<std::uint64_t>(extent.width()) * extent.channelCount() * 8u};
    const bool multiplicative = normalizationMethodIsMultiplicative(normalization.method);
    const bool zeroRule = source.validity().descriptor.rule() == ValidityRule::ZeroIsNoData;
    std::atomic<std::uint64_t> transformed{0u};
    std::atomic<std::uint64_t> preserved{0u};
    std::atomic<std::uint64_t> collisions{0u};
    const Status completed = parallelFor(execution, rows, cost, [&](TaskRange range) {
        std::uint64_t localTransformed = 0u;
        std::uint64_t localPreserved = 0u;
        std::uint64_t localCollisions = 0u;
        for (std::uint64_t index = range.begin; index < range.end; ++index)
        {
            const auto y = static_cast<std::uint32_t>(index);
            for (std::uint32_t x = 0u; x < extent.width(); ++x)
            {
                for (std::uint32_t channel = 0u; channel < extent.channelCount(); ++channel)
                {
                    const SampleT value = *sampleAddress<SampleT>(source, x, y, channel);
                    SampleT* const target = sampleAddress<SampleT>(destination, destinationLayout, x, y, channel);
                    if (!sampleParticipates(source, value, x, y, channel))
                    {
                        *target = value;
                        ++localPreserved;
                        continue;
                    }
                    const NormalizationChannelCoefficients& coefficients = normalization.channels[channel];
                    const double scaled = static_cast<double>(value) * coefficients.scale;
                    SampleT written = static_cast<SampleT>(multiplicative ? scaled : scaled + coefficients.offset);
                    if (zeroRule && written == static_cast<SampleT>(0))
                    {
                        written = std::copysign((std::numeric_limits<SampleT>::min)(), static_cast<SampleT>(scaled));
                        ++localCollisions;
                    }
                    *target = written;
                    ++localTransformed;
                }
            }
        }
        transformed.fetch_add(localTransformed, std::memory_order_relaxed);
        preserved.fetch_add(localPreserved, std::memory_order_relaxed);
        collisions.fetch_add(localCollisions, std::memory_order_relaxed);
        return Status::success();
    });
    if (completed.failed())
    {
        return completed;
    }
    const std::uint64_t written =
        transformed.load(std::memory_order_relaxed) + preserved.load(std::memory_order_relaxed);
    return NormalizationApplyResult{
        .backend = BackendSelection{.backend = ComputeBackend::Reference, .instructionSet = InstructionSet::Scalar},
        .path = NormalizationExecutionPath::Reference,
        .samplesWritten = written,
        .samplesTransformed = transformed.load(std::memory_order_relaxed),
        .samplesPreserved = preserved.load(std::memory_order_relaxed),
        .noDataCollisions = collisions.load(std::memory_order_relaxed),
        .plannedWorkers = plannedWorkerCount(execution, rows, cost),
        .imagePasses = 1u};
}

} // namespace

const char* normalizationMethodName(NormalizationMethod method) noexcept
{
    switch (method)
    {
    case NormalizationMethod::Additive:
        return "Additive";
    case NormalizationMethod::Multiplicative:
        return "Multiplicative";
    case NormalizationMethod::AdditiveScaling:
        return "AdditiveScaling";
    case NormalizationMethod::MultiplicativeScaling:
        return "MultiplicativeScaling";
    }
    return "UnknownNormalizationMethod";
}

const char* scaleEstimatorName(ScaleEstimator estimator) noexcept
{
    switch (estimator)
    {
    case ScaleEstimator::MedianAbsoluteDeviation:
        return "MedianAbsoluteDeviation";
    case ScaleEstimator::IterativeKSigma:
        return "IterativeKSigma";
    }
    return "UnknownScaleEstimator";
}

const char* normalizationExecutionPathName(NormalizationExecutionPath path) noexcept
{
    switch (path)
    {
    case NormalizationExecutionPath::Reference:
        return "Reference";
    case NormalizationExecutionPath::CompiledRow:
        return "CompiledRow";
    }
    return "UnknownNormalizationExecutionPath";
}

const char* overlapGraphPolicyName(OverlapGraphPolicy policy) noexcept
{
    switch (policy)
    {
    case OverlapGraphPolicy::AllPairs:
        return "AllPairs";
    case OverlapGraphPolicy::ReferenceStarSpanningTree:
        return "ReferenceStarSpanningTree";
    }
    return "UnknownOverlapGraphPolicy";
}

const char* overlapDegeneracyPolicyName(OverlapDegeneracyPolicy policy) noexcept
{
    switch (policy)
    {
    case OverlapDegeneracyPolicy::Refuse:
        return "Refuse";
    case OverlapDegeneracyPolicy::SubstituteIdentity:
        return "SubstituteIdentity";
    }
    return "UnknownOverlapDegeneracyPolicy";
}

Result<NormalizationFrameView> NormalizationFrameView::create(const void* samples, std::uint64_t bufferBytes,
                                                              const ImageLayout& layout,
                                                              const ImageDescriptor& descriptor,
                                                              const KernelValidityView& validity)
{
    if (samples == nullptr)
    {
        return parameterFailure(kMeasureOperation, ErrorCode::InvalidArgument,
                                "a normalization frame needs sample storage", "samples");
    }
    if (descriptor.sampleType() != SampleType::Float32 && descriptor.sampleType() != SampleType::Float64)
    {
        return normalizationFailure(kMeasureOperation, ErrorCode::Unsupported,
                                    "normalization reads and writes a floating working image; convert an integer "
                                    "boundary frame explicitly first");
    }
    if (descriptor.extent().channelCount() > kMaxNormalizationChannels)
    {
        return normalizationFailure(kMeasureOperation, ErrorCode::Unsupported,
                                    "normalization matches at most four channels");
    }
    if (descriptor.numericDomain() == NumericDomain::EncodedNonlinear)
    {
        return normalizationFailure(kMeasureOperation, ErrorCode::Unsupported,
                                    "an affine match has no meaning over a nonlinear encoded domain");
    }
    if (!layoutDescribes(descriptor, layout) || storageArrangementOf(layout) == StorageArrangement::SemiPlanar)
    {
        return normalizationFailure(kMeasureOperation, ErrorCode::InvalidArgument,
                                    "the layout does not describe this frame in a planar or interleaved arrangement");
    }
    if (bufferBytes < layout.totalBytes())
    {
        return parameterFailure(kMeasureOperation, ErrorCode::InvalidArgument,
                                "the frame buffer does not cover every byte named by its layout", "bufferBytes");
    }
    if (!pointerIsAligned(samples, descriptor.sampleType()))
    {
        return parameterFailure(kMeasureOperation, ErrorCode::InvalidArgument,
                                "the frame storage is not aligned for its sample type", "samples");
    }
    const Status validityReady = checkValidityBinding(validity, descriptor.extent(), kMeasureOperation);
    if (validityReady.failed())
    {
        return validityReady;
    }
    return NormalizationFrameView{samples, bufferBytes, layout, descriptor, validity};
}

Result<FrameNormalization> identityNormalization(NormalizationMethod method, std::uint32_t channelCount,
                                                 ScaleEstimator estimator)
{
    if (!isNormalizationMethod(method) || !isScaleEstimator(estimator))
    {
        return normalizationFailure(kEstimateOperation, ErrorCode::InvalidArgument,
                                    "the identity was asked for an unknown method or estimator");
    }
    if (channelCount == 0u || channelCount > kMaxNormalizationChannels)
    {
        return normalizationFailure(kEstimateOperation, ErrorCode::InvalidArgument,
                                    "the identity needs between one and four channels");
    }
    FrameNormalization identity;
    identity.method = method;
    identity.estimator = estimator;
    identity.channelCount = channelCount;
    return identity;
}

Result<std::uint64_t> globalNormalizationWorkingBytes(std::uint32_t width, std::uint32_t height, SampleType sampleType,
                                                      std::uint32_t workers)
{
    const auto layout = compactWorkspaceLayout(width, height, sampleType, workers);
    return layout.hasValue() ? Result<std::uint64_t>{layout->total} : Result<std::uint64_t>{layout.status()};
}

Result<NormalizationFrameStatistics> measureGlobalNormalization(const ExecutionContext& execution,
                                                                const NormalizationFrameView& frame,
                                                                ScaleEstimator estimator,
                                                                std::optional<StatisticsRegion> region)
{
    if (!execution.valid())
    {
        return normalizationFailure(kMeasureOperation, ErrorCode::NotInitialized,
                                    "normalization measurement needs an execution holding an engine-context lease");
    }
    const Status running = execution.checkCancellation();
    if (running.failed())
    {
        return running;
    }
    if (!frame.valid())
    {
        return parameterFailure(kMeasureOperation, ErrorCode::InvalidArgument,
                                "normalization measurement needs a checked frame", "frame");
    }
    if (!isScaleEstimator(estimator))
    {
        return normalizationFailure(kMeasureOperation, ErrorCode::InvalidArgument,
                                    "the measurement names an unknown scale estimator");
    }
    const ImageExtent& extent = frame.layout().extent();
    const StatisticsRegion whole{.originX = 0u, .originY = 0u, .width = extent.width(), .height = extent.height()};
    const StatisticsRegion measured = region.value_or(whole);
    if (!regionWithin(measured, extent))
    {
        return parameterFailure(kMeasureOperation, ErrorCode::InvalidArgument,
                                "the measured region lies partly outside the frame", "region");
    }
    NormalizationFrameStatistics out;
    out.estimator = estimator;
    out.channelCount = extent.channelCount();
    MeasurementCost cost;
    bool compacted = true;
    for (std::uint32_t channel = 0u; channel < extent.channelCount(); ++channel)
    {
        cost.compacted = false;
        const Status measuredChannel =
            measureChannel(execution, channelSource(frame, channel), channelValidity(frame, channel),
                           selectionOf(measured), estimator, out.channels[channel], cost);
        if (measuredChannel.failed())
        {
            return measuredChannel;
        }
        compacted = compacted && cost.compacted;
        execution.reportProgress(channel + 1u, extent.channelCount(), "measuring normalization statistics");
    }
    out.planeScans = cost.planeScans;
    out.peakScratchBytes = cost.peakScratchBytes;
    out.compacted = compacted;
    return out;
}

Result<NormalizationReference> combineNormalizationReferences(std::span<const NormalizationFrameStatistics> members)
{
    if (members.empty())
    {
        return normalizationFailure(kCombineOperation, ErrorCode::InvalidArgument,
                                    "a normalization reference needs at least one measured frame");
    }
    if (members.size() > kMaxReferenceMembers)
    {
        return normalizationFailure(kCombineOperation, ErrorCode::LimitExceeded,
                                    "a normalization reference combines at most 4096 frames");
    }
    const NormalizationFrameStatistics& first = members.front();
    if (first.channelCount == 0u || first.channelCount > kMaxNormalizationChannels ||
        !isScaleEstimator(first.estimator))
    {
        return normalizationFailure(kCombineOperation, ErrorCode::InvalidArgument,
                                    "a reference member holds no channels or names an unknown estimator");
    }
    for (const NormalizationFrameStatistics& member : members)
    {
        if (member.channelCount != first.channelCount || member.estimator != first.estimator)
        {
            return normalizationFailure(kCombineOperation, ErrorCode::IncompatibleArguments,
                                        "reference members disagree in channel count or estimator");
        }
    }
    NormalizationReference reference;
    reference.estimator = first.estimator;
    reference.channelCount = first.channelCount;
    reference.memberCount = static_cast<std::uint32_t>(members.size());
    std::array<double, kMaxReferenceMembers> scratch{};
    const auto count = static_cast<std::uint32_t>(members.size());
    for (std::uint32_t channel = 0u; channel < first.channelCount; ++channel)
    {
        NormalizationChannelStatistics& combined = reference.channels[channel];
        const auto combine = [&](double NormalizationChannelStatistics::* field) {
            for (std::uint32_t member = 0u; member < count; ++member)
            {
                scratch[member] = members[member].channels[channel].*field;
            }
            return medianInPlace(scratch.data(), count);
        };
        combined.median = combine(&NormalizationChannelStatistics::median);
        combined.medianAbsoluteDeviation = combine(&NormalizationChannelStatistics::medianAbsoluteDeviation);
        combined.location = combine(&NormalizationChannelStatistics::location);
        combined.scale = combine(&NormalizationChannelStatistics::scale);
        for (const NormalizationFrameStatistics& member : members)
        {
            combined.selection.valid += member.channels[channel].selection.valid;
            combined.selection.total += member.channels[channel].selection.total;
            combined.clippedSelection.valid += member.channels[channel].clippedSelection.valid;
            combined.clippedSelection.total += member.channels[channel].clippedSelection.total;
        }
    }
    return reference;
}

Result<FrameNormalization> estimateGlobalNormalization(const NormalizationReference& reference,
                                                       const NormalizationFrameStatistics& frame,
                                                       NormalizationMethod method)
{
    if (!isNormalizationMethod(method))
    {
        return normalizationFailure(kEstimateOperation, ErrorCode::InvalidArgument,
                                    "the match names an unknown normalization method");
    }
    if (frame.channelCount == 0u || frame.channelCount > kMaxNormalizationChannels)
    {
        return normalizationFailure(kEstimateOperation, ErrorCode::InvalidArgument,
                                    "the measured frame holds no channels");
    }
    if (frame.channelCount != reference.channelCount || frame.estimator != reference.estimator)
    {
        return normalizationFailure(kEstimateOperation, ErrorCode::IncompatibleArguments,
                                    "the frame and the reference disagree in channel count or estimator");
    }
    FrameNormalization match;
    match.method = method;
    match.estimator = frame.estimator;
    match.channelCount = frame.channelCount;
    for (std::uint32_t channel = 0u; channel < frame.channelCount; ++channel)
    {
        const NormalizationChannelStatistics& target = reference.channels[channel];
        const NormalizationChannelStatistics& measured = frame.channels[channel];
        if (!channelIsFinite(target) || !channelIsFinite(measured))
        {
            return normalizationFailure(kEstimateOperation, ErrorCode::InvalidData,
                                        "a normalization statistic is not finite");
        }
        NormalizationChannelCoefficients& coefficients = match.channels[channel];
        switch (method)
        {
        case NormalizationMethod::Additive:
            coefficients.scale = 1.0;
            coefficients.offset = target.median - measured.median;
            break;
        case NormalizationMethod::Multiplicative: {
            double factor = 1.0;
            if (measured.median != 0.0)
            {
                factor = target.median / measured.median;
            }
            else
            {
                ++match.locationFallbacks;
            }
            coefficients.scale = factor;
            coefficients.offset = 0.0;
            break;
        }
        case NormalizationMethod::AdditiveScaling:
        case NormalizationMethod::MultiplicativeScaling: {
            const bool validScale = measured.scale > 0.0 && target.scale > 0.0;
            const double ratio = validScale ? target.scale / measured.scale : 1.0;
            if (!validScale)
            {
                ++match.dispersionFallbacks;
            }
            if (method == NormalizationMethod::AdditiveScaling)
            {
                coefficients.scale = ratio;
                coefficients.offset = target.location - ratio * measured.location;
            }
            else
            {
                double factor = 1.0;
                if (measured.location != 0.0)
                {
                    factor = target.location / measured.location;
                }
                else
                {
                    ++match.locationFallbacks;
                }
                coefficients.scale = ratio * factor;
                coefficients.offset = 0.0;
            }
            break;
        }
        }
        if (!std::isfinite(coefficients.scale) || coefficients.scale == 0.0 || !std::isfinite(coefficients.offset))
        {
            return normalizationFailure(kEstimateOperation, ErrorCode::InvalidData,
                                        "a multiplicative match against a zero or non-finite reference location "
                                        "would erase the frame");
        }
    }
    return match;
}

Result<OverlapMeasurement> measureOverlapStatistics(const ExecutionContext& execution, std::uint32_t frameA,
                                                    const NormalizationFrameView& a, const StatisticsRegion& regionA,
                                                    std::uint32_t frameB, const NormalizationFrameView& b,
                                                    const StatisticsRegion& regionB, ScaleEstimator estimator)
{
    if (!execution.valid())
    {
        return normalizationFailure(kOverlapOperation, ErrorCode::NotInitialized,
                                    "overlap measurement needs an execution holding an engine-context lease");
    }
    const Status running = execution.checkCancellation();
    if (running.failed())
    {
        return running;
    }
    if (!a.valid() || !b.valid())
    {
        return normalizationFailure(kOverlapOperation, ErrorCode::InvalidArgument,
                                    "overlap measurement needs two checked frames");
    }
    if (frameA == frameB)
    {
        return normalizationFailure(kOverlapOperation, ErrorCode::InvalidArgument,
                                    "an overlap relates two different frames");
    }
    if (!isScaleEstimator(estimator))
    {
        return normalizationFailure(kOverlapOperation, ErrorCode::InvalidArgument,
                                    "the overlap measurement names an unknown scale estimator");
    }
    const ImageExtent& extentA = a.layout().extent();
    const ImageExtent& extentB = b.layout().extent();
    if (extentA.channelCount() != extentB.channelCount())
    {
        return normalizationFailure(kOverlapOperation, ErrorCode::IncompatibleArguments,
                                    "the two frames of an overlap disagree in channel count");
    }
    if (!regionWithin(regionA, extentA) || !regionWithin(regionB, extentB))
    {
        return parameterFailure(kOverlapOperation, ErrorCode::InvalidArgument,
                                "an overlap region lies partly outside its frame", "region");
    }
    if (regionA.width != regionB.width || regionA.height != regionB.height)
    {
        return normalizationFailure(kOverlapOperation, ErrorCode::IncompatibleArguments,
                                    "the two overlap rectangles must have one size");
    }
    const std::uint32_t channels = extentA.channelCount();
    const std::uint64_t maskBytesA = packedMaskRowBytes(extentA.width()) * extentA.height() * channels;
    const std::uint64_t maskBytesB = packedMaskRowBytes(extentB.width()) * extentB.height() * channels;
    Result<plane_detail::ScopedContextMemory> masks = plane_detail::ScopedContextMemory::take(
        execution, maskBytesA + maskBytesB, alignof(std::uint64_t), kOverlapOperation);
    if (!masks.hasValue())
    {
        return masks.status();
    }
    auto* const bytesA = static_cast<std::uint8_t*>(masks->data());
    auto* const bytesB = bytesA + maskBytesA;
    const std::optional<ValidityMaskView<std::uint8_t>> writableA =
        ValidityMaskView<std::uint8_t>::createTight(std::span<std::uint8_t>{bytesA, maskBytesA}, extentA);
    const std::optional<ValidityMaskView<std::uint8_t>> writableB =
        ValidityMaskView<std::uint8_t>::createTight(std::span<std::uint8_t>{bytesB, maskBytesB}, extentB);
    if (!writableA.has_value() || !writableB.has_value())
    {
        return normalizationFailure(kOverlapOperation, ErrorCode::Internal,
                                    "the shared-validity masks could not be laid out over the frames");
    }
    writableA->fill(false);
    writableB->fill(false);
    OverlapMeasurement out;
    out.frameA = frameA;
    out.frameB = frameB;
    out.channelCount = channels;
    out.estimator = estimator;
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        for (std::uint32_t dy = 0u; dy < regionA.height; ++dy)
        {
            const Status stillRunning = execution.checkCancellation();
            if (stillRunning.failed())
            {
                return stillRunning;
            }
            const std::uint32_t ya = regionA.originY + dy;
            const std::uint32_t yb = regionB.originY + dy;
            for (std::uint32_t dx = 0u; dx < regionA.width; ++dx)
            {
                const std::uint32_t xa = regionA.originX + dx;
                const std::uint32_t xb = regionB.originX + dx;
                if (!frameSampleParticipates(a, xa, ya, channel) || !frameSampleParticipates(b, xb, yb, channel))
                {
                    continue;
                }
                (void)writableA->setValid(channel, xa, ya, true);
                (void)writableB->setValid(channel, xb, yb, true);
                if (channel == 0u)
                {
                    ++out.sharedSamples;
                }
            }
        }
    }
    const std::optional<ValidityDescriptor> explicitRule =
        ValidityDescriptor::create(ValidityDescriptorRequest{.rule = ValidityRule::ExplicitMask});
    if (!explicitRule.has_value())
    {
        return normalizationFailure(kOverlapOperation, ErrorCode::Internal,
                                    "the explicit-mask validity rule could not be stated");
    }
    const ValidityMaskView<const std::uint8_t> maskA = writableA->constView();
    const ValidityMaskView<const std::uint8_t> maskB = writableB->constView();
    MeasurementCost cost;
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        const KernelValidityView validityA{.descriptor = *explicitRule, .mask = &maskA, .maskPlane = channel};
        const KernelValidityView validityB{.descriptor = *explicitRule, .mask = &maskB, .maskPlane = channel};
        const Status measuredA = measureChannel(execution, channelSource(a, channel), validityA, selectionOf(regionA),
                                                estimator, out.a[channel], cost);
        if (measuredA.failed())
        {
            return measuredA;
        }
        const Status measuredB = measureChannel(execution, channelSource(b, channel), validityB, selectionOf(regionB),
                                                estimator, out.b[channel], cost);
        if (measuredB.failed())
        {
            return measuredB;
        }
        execution.reportProgress(channel + 1u, channels, "measuring overlap statistics");
    }
    out.planeScans = cost.planeScans;
    out.peakScratchBytes = maskBytesA + maskBytesB + cost.peakScratchBytes;
    return out;
}

Result<OverlapPairSelection> selectOverlapPairs(const ExecutionContext& execution,
                                                std::span<const OverlapCandidate> candidates, std::uint32_t frameCount,
                                                std::uint32_t referenceIndex, OverlapGraphPolicy policy,
                                                std::span<std::uint8_t> selected)
{
    if (!execution.valid())
    {
        return normalizationFailure(kSelectOperation, ErrorCode::NotInitialized,
                                    "overlap pair selection needs an execution holding an engine-context lease");
    }
    if (!isOverlapGraphPolicy(policy))
    {
        return normalizationFailure(kSelectOperation, ErrorCode::InvalidArgument,
                                    "the selection names an unknown graph policy");
    }
    if (frameCount == 0u || referenceIndex >= frameCount)
    {
        return normalizationFailure(kSelectOperation, ErrorCode::InvalidArgument,
                                    "the reference frame must lie within a non-empty set");
    }
    if (frameCount > kMaxOverlapNormalizationFrames)
    {
        return normalizationFailure(kSelectOperation, ErrorCode::LimitExceeded,
                                    "an overlap selection relates at most 4096 frames");
    }
    if (selected.size() != candidates.size())
    {
        return parameterFailure(kSelectOperation, ErrorCode::InvalidArgument,
                                "the selection output must be exactly as long as the candidates", "selected");
    }
    for (const OverlapCandidate& candidate : candidates)
    {
        if (candidate.frameA >= frameCount || candidate.frameB >= frameCount || candidate.frameA == candidate.frameB)
        {
            return normalizationFailure(kSelectOperation, ErrorCode::InvalidArgument,
                                        "a candidate names a frame outside the set or relates a frame to itself");
        }
    }
    const std::uint64_t parentBytes = static_cast<std::uint64_t>(frameCount) * sizeof(std::uint32_t);
    const std::uint64_t rankedBytes = static_cast<std::uint64_t>(candidates.size()) * sizeof(std::uint32_t);
    Result<plane_detail::ScopedContextMemory> memory = plane_detail::ScopedContextMemory::take(
        execution, parentBytes + (std::max)(rankedBytes, std::uint64_t{sizeof(std::uint32_t)}), alignof(std::uint32_t),
        kSelectOperation);
    if (!memory.hasValue())
    {
        return memory.status();
    }
    auto* const parents = static_cast<std::uint32_t*>(memory->data());
    std::uint32_t* const ranked = parents + frameCount;
    FrameComponents components{parents, frameCount};
    OverlapPairSelection report;
    std::fill(selected.begin(), selected.end(), std::uint8_t{0});
    if (policy == OverlapGraphPolicy::AllPairs)
    {
        for (std::size_t index = 0u; index < candidates.size(); ++index)
        {
            selected[index] = 1u;
            ++report.selectedCount;
            if (candidates[index].frameA == referenceIndex || candidates[index].frameB == referenceIndex)
            {
                ++report.referenceEdges;
            }
            components.unite(candidates[index].frameA, candidates[index].frameB);
        }
        report.connected = components.allJoinedTo(referenceIndex);
        return report;
    }
    for (std::size_t index = 0u; index < candidates.size(); ++index)
    {
        ranked[index] = static_cast<std::uint32_t>(index);
        if (candidates[index].frameA == referenceIndex || candidates[index].frameB == referenceIndex)
        {
            selected[index] = 1u;
            ++report.referenceEdges;
        }
    }
    std::sort(ranked, ranked + candidates.size(), [&](std::uint32_t leftIndex, std::uint32_t rightIndex) {
        const OverlapCandidate& left = candidates[leftIndex];
        const OverlapCandidate& right = candidates[rightIndex];
        if (left.overlapPixels != right.overlapPixels)
        {
            return left.overlapPixels > right.overlapPixels;
        }
        if (left.frameA != right.frameA)
        {
            return left.frameA < right.frameA;
        }
        return left.frameB < right.frameB;
    });
    for (std::size_t position = 0u; position < candidates.size(); ++position)
    {
        const std::uint32_t index = ranked[position];
        if (!components.unite(candidates[index].frameA, candidates[index].frameB))
        {
            continue;
        }
        selected[index] = 1u;
        ++report.spanningTreeEdges;
        if (report.spanningTreeEdges + 1u == frameCount)
        {
            break;
        }
    }
    report.connected = report.spanningTreeEdges + 1u == frameCount;
    for (std::size_t index = 0u; index < candidates.size(); ++index)
    {
        report.selectedCount += selected[index] != 0u ? 1u : 0u;
    }
    return report;
}

Result<OverlapSolveReport> solveOverlapNormalization(const ExecutionContext& execution,
                                                     std::span<const OverlapMeasurement> pairs,
                                                     std::uint32_t referenceIndex, NormalizationMethod method,
                                                     OverlapDegeneracyPolicy degeneracy,
                                                     std::span<FrameNormalization> frames)
{
    if (!execution.valid())
    {
        return normalizationFailure(kSolveOperation, ErrorCode::NotInitialized,
                                    "the overlap solve needs an execution holding an engine-context lease");
    }
    const Status running = execution.checkCancellation();
    if (running.failed())
    {
        return running;
    }
    if (!isNormalizationMethod(method) || !isOverlapDegeneracyPolicy(degeneracy))
    {
        return normalizationFailure(kSolveOperation, ErrorCode::InvalidArgument,
                                    "the overlap solve names an unknown method or degeneracy policy");
    }
    if (frames.empty() || referenceIndex >= frames.size() || pairs.empty())
    {
        return normalizationFailure(kSolveOperation, ErrorCode::InvalidArgument,
                                    "the overlap solve needs a non-empty set, a reference inside it and at least "
                                    "one measured pair");
    }
    if (frames.size() > kMaxOverlapNormalizationFrames)
    {
        return normalizationFailure(kSolveOperation, ErrorCode::LimitExceeded,
                                    "an overlap solve relates at most 4096 frames");
    }
    const auto frameCount = static_cast<std::uint32_t>(frames.size());
    const std::uint32_t channels = pairs.front().channelCount;
    const ScaleEstimator estimator = pairs.front().estimator;
    if (channels == 0u || channels > kMaxNormalizationChannels || !isScaleEstimator(estimator))
    {
        return normalizationFailure(kSolveOperation, ErrorCode::InvalidArgument,
                                    "a measured pair holds no channels or names an unknown estimator");
    }
    OverlapSolveReport report;
    report.frameCount = frameCount;
    for (const OverlapMeasurement& pair : pairs)
    {
        if (pair.frameA >= frameCount || pair.frameB >= frameCount || pair.frameA == pair.frameB)
        {
            return normalizationFailure(kSolveOperation, ErrorCode::InvalidArgument,
                                        "a measured pair names a frame outside the set or relates a frame to itself");
        }
        if (pair.channelCount != channels || pair.estimator != estimator)
        {
            return normalizationFailure(kSolveOperation, ErrorCode::IncompatibleArguments,
                                        "measured pairs disagree in channel count or estimator");
        }
        report.usablePairs += pair.sharedSamples > 0u ? 1u : 0u;
    }
    const std::uint32_t variables = frameCount - 1u;
    const std::uint64_t matrixBytes =
        static_cast<std::uint64_t>(variables) * (static_cast<std::uint64_t>(variables) + 1u) * sizeof(double);
    const std::uint64_t vectorBytes = static_cast<std::uint64_t>(frameCount) * sizeof(double);
    const std::uint64_t indexBytes = static_cast<std::uint64_t>(frameCount) * sizeof(std::uint32_t);
    const std::uint64_t totalBytes = (std::max)(matrixBytes, std::uint64_t{sizeof(double)}) + 3u * vectorBytes +
                                     2u * ((indexBytes + sizeof(double) - 1u) / sizeof(double) * sizeof(double));
    Result<plane_detail::ScopedContextMemory> memory =
        plane_detail::ScopedContextMemory::take(execution, totalBytes, alignof(double), kSolveOperation);
    if (!memory.hasValue())
    {
        return memory.status();
    }
    auto staged = plane_detail::ScopedContextMemory::take(
        execution, static_cast<std::uint64_t>(frameCount) * sizeof(FrameNormalization), alignof(FrameNormalization),
        kSolveOperation);
    if (!staged.hasValue())
    {
        return staged.status();
    }
    const std::span<FrameNormalization> destination = frames;
    frames = {static_cast<FrameNormalization*>(staged->data()), frameCount};
    std::uninitialized_value_construct_n(frames.data(), frameCount);
    for (FrameNormalization& frame : frames)
    {
        frame = FrameNormalization{};
        frame.method = method;
        frame.estimator = estimator;
        frame.channelCount = channels;
    }

    auto* const doubles = static_cast<double*>(memory->data());
    SolveWorkspace workspace;
    workspace.matrix = doubles;
    workspace.offsets = doubles + (std::max)(matrixBytes, std::uint64_t{sizeof(double)}) / sizeof(double);
    workspace.factors = workspace.offsets + frameCount;
    workspace.dispersions = workspace.factors + frameCount;
    const std::uint64_t indexDoubles = (indexBytes + sizeof(double) - 1u) / sizeof(double);
    workspace.variableOf = reinterpret_cast<std::int32_t*>(workspace.dispersions + frameCount);
    workspace.parents = reinterpret_cast<std::uint32_t*>(workspace.dispersions + frameCount + indexDoubles);
    workspace.frameCount = frameCount;
    workspace.variableCount = variables;
    std::int32_t next = 0;
    for (std::uint32_t frame = 0u; frame < frameCount; ++frame)
    {
        workspace.variableOf[frame] = frame == referenceIndex ? -1 : next++;
    }

    const auto locationA = [](const OverlapMeasurement& pair, std::uint32_t channel) {
        return pair.a[channel].location;
    };
    const auto locationB = [](const OverlapMeasurement& pair, std::uint32_t channel) {
        return pair.b[channel].location;
    };
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        const Status stillRunning = execution.checkCancellation();
        if (stillRunning.failed())
        {
            return stillRunning;
        }
        const auto leftLocation = [&](const OverlapMeasurement& pair) { return locationA(pair, channel); };
        const auto rightLocation = [&](const OverlapMeasurement& pair) { return locationB(pair, channel); };
        const auto leftScale = [&](const OverlapMeasurement& pair) { return pair.a[channel].scale; };
        const auto rightScale = [&](const OverlapMeasurement& pair) { return pair.b[channel].scale; };
        const auto solveOrFail = [&](Result<bool> solved, const char* message) -> Status {
            if (!solved.hasValue())
            {
                return solved.status();
            }
            ++report.systemsSolved;
            if (!*solved)
            {
                return normalizationFailure(kSolveOperation, ErrorCode::InvalidData, message);
            }
            return Status::success();
        };

        bool needsDispersion =
            method == NormalizationMethod::AdditiveScaling || method == NormalizationMethod::MultiplicativeScaling;
        if (needsDispersion)
        {
            Result<bool> dispersion = solveMultiplicativeSystem(execution, workspace, pairs, referenceIndex, leftScale,
                                                                rightScale, workspace.dispersions);
            if (!dispersion.hasValue())
            {
                return dispersion.status();
            }
            ++report.systemsSolved;
            if (!*dispersion)
            {
                if (degeneracy == OverlapDegeneracyPolicy::Refuse)
                {
                    return normalizationFailure(kSolveOperation, ErrorCode::InvalidData,
                                                "the dispersion overlap system is disconnected or singular");
                }
                std::fill(workspace.dispersions, workspace.dispersions + frameCount, 1.0);
                ++report.dispersionSubstitutions;
            }
        }
        switch (method)
        {
        case NormalizationMethod::Additive: {
            const Status solved = solveOrFail(solveAdditiveSystem(execution, workspace, pairs, referenceIndex,
                                                                  leftLocation, rightLocation, workspace.offsets),
                                              "the additive overlap system is disconnected or singular");
            if (solved.failed())
            {
                return solved;
            }
            for (std::uint32_t frame = 0u; frame < frameCount; ++frame)
            {
                frames[frame].channels[channel] =
                    NormalizationChannelCoefficients{.scale = 1.0, .offset = workspace.offsets[frame]};
            }
            break;
        }
        case NormalizationMethod::Multiplicative: {
            const Status solved = solveOrFail(solveMultiplicativeSystem(execution, workspace, pairs, referenceIndex,
                                                                        leftLocation, rightLocation, workspace.factors),
                                              "the multiplicative overlap system is disconnected or singular");
            if (solved.failed())
            {
                return solved;
            }
            for (std::uint32_t frame = 0u; frame < frameCount; ++frame)
            {
                frames[frame].channels[channel] =
                    NormalizationChannelCoefficients{.scale = workspace.factors[frame], .offset = 0.0};
            }
            break;
        }
        case NormalizationMethod::AdditiveScaling: {
            const Status plain = solveOrFail(solveAdditiveSystem(execution, workspace, pairs, referenceIndex,
                                                                 leftLocation, rightLocation, workspace.offsets),
                                             "the additive overlap system is disconnected or singular");
            if (plain.failed())
            {
                return plain;
            }
            const double* const dispersions = workspace.dispersions;
            const auto leftScaled = [&](const OverlapMeasurement& pair) {
                return pair.a[channel].location * dispersions[pair.frameA];
            };
            const auto rightScaled = [&](const OverlapMeasurement& pair) {
                return pair.b[channel].location * dispersions[pair.frameB];
            };
            Result<bool> scaled = solveAdditiveSystem(execution, workspace, pairs, referenceIndex, leftScaled,
                                                      rightScaled, workspace.factors);
            if (!scaled.hasValue())
            {
                return scaled.status();
            }
            ++report.systemsSolved;
            const double* offsets = workspace.factors;
            if (!*scaled)
            {
                if (degeneracy == OverlapDegeneracyPolicy::Refuse)
                {
                    return normalizationFailure(kSolveOperation, ErrorCode::InvalidData,
                                                "the dispersion-scaled additive overlap system is singular");
                }
                offsets = workspace.offsets;
                ++report.additiveScalingSubstitutions;
            }
            for (std::uint32_t frame = 0u; frame < frameCount; ++frame)
            {
                frames[frame].channels[channel] =
                    NormalizationChannelCoefficients{.scale = dispersions[frame], .offset = offsets[frame]};
            }
            break;
        }
        case NormalizationMethod::MultiplicativeScaling: {
            const Status solved = solveOrFail(solveMultiplicativeSystem(execution, workspace, pairs, referenceIndex,
                                                                        leftLocation, rightLocation, workspace.factors),
                                              "the multiplicative overlap system is disconnected or singular");
            if (solved.failed())
            {
                return solved;
            }
            for (std::uint32_t frame = 0u; frame < frameCount; ++frame)
            {
                frames[frame].channels[channel] = NormalizationChannelCoefficients{
                    .scale = workspace.dispersions[frame] * workspace.factors[frame], .offset = 0.0};
            }
            break;
        }
        }
        for (std::uint32_t frame = 0u; frame < frameCount; ++frame)
        {
            const NormalizationChannelCoefficients& coefficients = frames[frame].channels[channel];
            if (!std::isfinite(coefficients.scale) || coefficients.scale == 0.0 || !std::isfinite(coefficients.offset))
            {
                return normalizationFailure(kSolveOperation, ErrorCode::InvalidData,
                                            "the overlap solve produced a zero or non-finite coefficient");
            }
        }
        execution.reportProgress(channel + 1u, channels, "solving overlap normalization");
    }
    if (const Status stopped = execution.checkCancellation(); stopped.failed())
    {
        return stopped;
    }
    std::copy(frames.begin(), frames.end(), destination.begin());
    return report;
}

Result<NormalizationApplyResult> applyGlobalNormalization(const ExecutionContext& execution,
                                                          const FrameNormalization& normalization,
                                                          const NormalizationFrameView& source, void* destination,
                                                          std::uint64_t destinationBytes,
                                                          const ImageLayout& destinationLayout)
{
    if (!execution.valid())
    {
        return normalizationFailure(kApplyOperation, ErrorCode::NotInitialized,
                                    "normalization needs an execution holding an engine-context lease");
    }
    if (!source.valid())
    {
        return parameterFailure(kApplyOperation, ErrorCode::InvalidArgument,
                                "normalization needs a checked source frame", "source");
    }
    const Status coefficientsReady = checkCoefficients(normalization, kApplyOperation);
    if (coefficientsReady.failed())
    {
        return coefficientsReady;
    }
    const ImageExtent& extent = source.layout().extent();
    if (normalization.channelCount != extent.channelCount())
    {
        return normalizationFailure(kApplyOperation, ErrorCode::IncompatibleArguments,
                                    "the normalization and the frame disagree in channel count");
    }
    if (source.validity().descriptor.rule() == ValidityRule::DomainRange)
    {
        return normalizationFailure(kApplyOperation, ErrorCode::Unsupported,
                                    "a stated value range describes the untransformed samples; restate it after "
                                    "normalizing");
    }
    const SampleType sampleType = source.descriptor().sampleType();
    if (destination == nullptr || !pointerIsAligned(destination, sampleType))
    {
        return parameterFailure(kApplyOperation, ErrorCode::InvalidArgument,
                                "normalization needs aligned destination storage", "destination");
    }
    if (destinationLayout.extent() != extent ||
        destinationLayout.elementSizeBytes() != sampleTypeSizeBytes(sampleType) ||
        storageArrangementOf(destinationLayout) == StorageArrangement::SemiPlanar)
    {
        return normalizationFailure(kApplyOperation, ErrorCode::IncompatibleArguments,
                                    "the destination layout does not describe the source frame");
    }
    if (destinationBytes < destinationLayout.totalBytes())
    {
        return parameterFailure(kApplyOperation, ErrorCode::InvalidArgument,
                                "the destination buffer does not cover every byte named by its layout",
                                "destinationBytes");
    }
    const bool inPlace = destination == source.samples();
    if (inPlace ? !sameStrides(source.layout(), destinationLayout)
                : rangesOverlap(destination, destinationLayout.totalBytes(), source.samples(),
                                source.layout().totalBytes()))
    {
        return normalizationFailure(kApplyOperation, ErrorCode::Aliasing,
                                    "normalization writes either the source in place or storage disjoint from it");
    }
    const Status running = execution.checkCancellation();
    if (running.failed())
    {
        return running;
    }

    const bool eligible =
        !source.validity().descriptor.needsMask() && sameArrangement(source.layout(), destinationLayout);
    if (sampleType == SampleType::Float32)
    {
        const NormalizationFloat32RunFn compiled = eligible ? normalizationFloat32RunFor(execution) : nullptr;
        if (compiled != nullptr)
        {
            Result<BackendSelection> selected = selectBackend(execution);
            if (!selected.hasValue())
            {
                return selected.status();
            }
            return runRows<float>(execution, normalization, source, destination, destinationLayout,
                                  NormalizationExecutionPath::CompiledRow, *selected, compiled);
        }
        if (execution.requestedBackend() == ComputeBackend::Cpu &&
            execution.backendFallback() == BackendFallback::Forbidden)
        {
            return normalizationFailure(kApplyOperation, ErrorCode::UnsupportedBackend,
                                        "the forced CPU normalization cannot serve this validity or layout");
        }
        return runScalar<float>(execution, normalization, source, destination, destinationLayout);
    }
    const NormalizationFloat64RunFn compiled = eligible ? normalizationFloat64RunFor(execution) : nullptr;
    if (compiled != nullptr)
    {
        Result<BackendSelection> selected = selectBackend(execution);
        if (!selected.hasValue())
        {
            return selected.status();
        }
        return runRows<double>(execution, normalization, source, destination, destinationLayout,
                               NormalizationExecutionPath::CompiledRow, *selected, compiled);
    }
    if (execution.requestedBackend() == ComputeBackend::Cpu &&
        execution.backendFallback() == BackendFallback::Forbidden)
    {
        return normalizationFailure(kApplyOperation, ErrorCode::UnsupportedBackend,
                                    "the forced CPU normalization cannot serve this validity or layout");
    }
    return runScalar<double>(execution, normalization, source, destination, destinationLayout);
}

Result<MetadataTransition> globalNormalizationTransition(const FrameNormalization& normalization,
                                                         const ImageSemantics& source)
{
    const Status coefficientsReady = checkCoefficients(normalization, kTransitionOperation);
    if (coefficientsReady.failed())
    {
        return coefficientsReady;
    }
    ImageSemanticsRequest request = source.toRequest();
    request.processingState.normalized = true;
    Result<ImageSemantics> semantics = ImageSemantics::create(request);
    if (!semantics.hasValue())
    {
        return semantics.status();
    }
    std::ostringstream detail;
    detail << "method=" << normalizationMethodName(normalization.method)
           << ",estimator=" << scaleEstimatorName(normalization.estimator)
           << ",channels=" << normalization.channelCount;
    for (std::uint32_t channel = 0u; channel < normalization.channelCount; ++channel)
    {
        detail << ",scale" << channel << '=' << normalization.channels[channel].scale << ",offset" << channel << '='
               << normalization.channels[channel].offset;
    }
    return operationTransition(
        kGlobalNormalizationEffect,
        OperationOutcome{.operation = kApplyOperation, .detail = detail.str(), .semantics = std::move(*semantics)});
}

} // namespace an
