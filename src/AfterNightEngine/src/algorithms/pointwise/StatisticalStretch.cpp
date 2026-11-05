// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - statistical stretch from an image's exact order statistics.
//
// The minimum and the median a statistical stretch is defined by are the image's own. One channel
// selects them through the engine's exact order-statistic primitive, which narrows a rank by counting
// and holds memory bounded by its own ceilings. RGB selects them here the same way over whole pixels,
// pooling the three channels when they are linked, because a pixel participates only when every
// channel does. The lower-half spread is a noise estimate over at most
// `kMaxStatisticalStretchSamplesPerChannel` values per group, chosen by a stride fixed by the image's
// shape and selected in linear time rather than sorted. Every pass runs on the operation's threads and
// merges only counts and extremes, so no answer depends on the thread count. The transfer is planned
// once per group and applied a row at a time, with the clip count taken while it writes.

#include <AfterNightEngine/Algorithms/Pointwise/StatisticalStretch.h>

#include <AfterNightEngine/Algorithms/Kernel/HistogramOrderKernel.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/PlaneView.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "kernels/reference/PlaneSourceAccess.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace an
{
namespace
{

constexpr const char* kAnalysisOperation = "an::analyzeImageStatisticalStretch";
constexpr const char* kClippingOperation = "an::computeStatisticalStretchClipStatistics";
constexpr const char* kStretchOperation = "an::stretchImageStatistically";
constexpr double kLowerHalfMadToSigma = 1.4826;

// The exact RGB selection counts eleven bits of a sample's order key per pass, so a worker's table
// for three groups is 24 KiB, and gathers a bucket's samples once it holds no more than this many.
constexpr std::uint32_t kDigitBits = 11u;
constexpr std::uint32_t kDigitBuckets = 1u << kDigitBits;
constexpr std::uint32_t kKeyLevels = 6u;
constexpr std::uint64_t kSelectionGather = 4096u;

[[nodiscard]] Status stretchFailure(const char* operation, ErrorCode code, const char* message)
{
    return Status::failure(code,
                           StatusDetail{.message = message, .operation = operation, .domain = ErrorDomain::Algorithms});
}

[[nodiscard]] bool isSampleAligned(const void* samples, std::uint32_t elementBytes) noexcept
{
    return reinterpret_cast<std::uintptr_t>(samples) % elementBytes == 0u;
}

[[nodiscard]] bool maskAccepts(const KernelValidityView& validity, std::uint32_t x, std::uint32_t y) noexcept
{
    if (validity.mask == nullptr)
    {
        return true;
    }
    const ValidityMaskView<const std::uint8_t>& mask = *validity.mask;
    const std::uint64_t offset = static_cast<std::uint64_t>(validity.maskPlane) * mask.planeStrideBytes() +
                                 static_cast<std::uint64_t>(y) * mask.rowStrideBytes() + x / 8u;
    return (mask.data()[offset] & static_cast<std::uint8_t>(1u << (x % 8u))) != 0u;
}

// Whether every pixel participates, so a pass need not read a mask or apply a value rule.
[[nodiscard]] bool admitsEveryPixel(const KernelValidityView& validity) noexcept
{
    return validity.descriptor.rule() == ValidityRule::AllValid && validity.mask == nullptr;
}

[[nodiscard]] Status validateValidity(const KernelValidityView& validity, const ImageLayout& layout,
                                      const char* operation)
{
    if (!validity.descriptor.needsMask())
    {
        if (validity.mask != nullptr || validity.maskPlane != 0u)
        {
            return stretchFailure(operation, ErrorCode::InvalidArgument,
                                  "a statistical-stretch validity view supplies an unused mask");
        }
        return Status::success();
    }
    if (validity.mask == nullptr)
    {
        return stretchFailure(operation, ErrorCode::InvalidArgument,
                              "an explicit-mask statistical stretch supplies no mask");
    }
    if (validity.mask->width() != layout.extent().width() || validity.mask->height() != layout.extent().height() ||
        validity.maskPlane >= validity.mask->planeCount())
    {
        return stretchFailure(operation, ErrorCode::IncompatibleArguments,
                              "a statistical-stretch validity mask does not cover the source");
    }
    return Status::success();
}

[[nodiscard]] Status validateSource(const ExecutionContext& execution, SampleType sampleType, const void* samples,
                                    const ImageLayout& layout, const KernelValidityView& validity,
                                    const char* operation)
{
    if (!execution.valid())
    {
        return stretchFailure(operation, ErrorCode::NotInitialized,
                              "a statistical stretch was given an empty execution context");
    }
    const Status cancelled = execution.checkCancellation();
    if (cancelled.failed())
    {
        return cancelled;
    }
    const Result<BackendSelection> backend = selectBackend(execution);
    if (!backend.hasValue())
    {
        return backend.status();
    }
    if (sampleType != SampleType::Float32 && sampleType != SampleType::Float64)
    {
        return stretchFailure(operation, ErrorCode::IncompatibleArguments,
                              "a statistical stretch runs on a Float32 or Float64 working image");
    }
    if (samples == nullptr)
    {
        return stretchFailure(operation, ErrorCode::InvalidArgument,
                              "a statistical stretch was given a null source pointer");
    }
    const std::uint32_t elementBytes = sampleTypeSizeBytes(sampleType);
    if (layout.elementSizeBytes() != elementBytes || !isSampleAligned(samples, elementBytes))
    {
        return stretchFailure(operation, ErrorCode::InvalidArgument,
                              "a statistical-stretch layout or pointer disagrees with its sample type");
    }
    const std::uint32_t channels = layout.extent().channelCount();
    if (channels != 1u && channels != 3u)
    {
        return stretchFailure(operation, ErrorCode::IncompatibleArguments,
                              "a statistical stretch needs a mono or RGB source");
    }
    return validateValidity(validity, layout, operation);
}

[[nodiscard]] bool exactlyAliases(const void* sourceSamples, const ImageLayout& sourceLayout,
                                  const void* destinationSamples, const ImageLayout& destinationLayout) noexcept
{
    return sourceSamples == destinationSamples && sourceLayout == destinationLayout;
}

[[nodiscard]] Status validateDestination(SampleType sampleType, const void* sourceSamples,
                                         const ImageLayout& sourceLayout, void* destinationSamples,
                                         const ImageLayout& destinationLayout, const KernelValidityView& validity)
{
    if (destinationSamples == nullptr)
    {
        return stretchFailure(kStretchOperation, ErrorCode::InvalidArgument,
                              "a statistical stretch was given a null destination pointer");
    }
    const std::uint32_t elementBytes = sampleTypeSizeBytes(sampleType);
    if (destinationLayout.elementSizeBytes() != elementBytes || !isSampleAligned(destinationSamples, elementBytes))
    {
        return stretchFailure(kStretchOperation, ErrorCode::InvalidArgument,
                              "a statistical-stretch destination disagrees with its sample type");
    }
    if (!(sourceLayout.extent() == destinationLayout.extent()))
    {
        return stretchFailure(kStretchOperation, ErrorCode::IncompatibleArguments,
                              "a statistical stretch needs equal source and destination extents");
    }
    if (regionsOverlap(static_cast<const std::uint8_t*>(sourceSamples), sourceLayout.totalBytes(),
                       static_cast<const std::uint8_t*>(destinationSamples), destinationLayout.totalBytes()) &&
        !exactlyAliases(sourceSamples, sourceLayout, destinationSamples, destinationLayout))
    {
        return stretchFailure(kStretchOperation, ErrorCode::IncompatibleArguments,
                              "a statistical-stretch source partially overlaps its destination");
    }
    if (validity.mask != nullptr &&
        regionsOverlap(validity.mask->data(), validity.mask->spannedByteCount(),
                       static_cast<const std::uint8_t*>(destinationSamples), destinationLayout.totalBytes()))
    {
        return stretchFailure(kStretchOperation, ErrorCode::InvalidArgument,
                              "a statistical-stretch validity mask overlaps its destination");
    }
    return Status::success();
}

template <typename SampleT>
[[nodiscard]] const SampleT* channelRow(const void* samples, const ImageLayout& layout, std::uint32_t channel,
                                        std::uint32_t y) noexcept
{
    const std::uint8_t* const bytes = static_cast<const std::uint8_t*>(samples);
    return reinterpret_cast<const SampleT*>(bytes + *layout.rowByteOffset(channel / layout.samplesPerPixel(), y)) +
           channel % layout.samplesPerPixel();
}

template <typename SampleT>
[[nodiscard]] SampleT* channelRow(void* samples, const ImageLayout& layout, std::uint32_t channel,
                                  std::uint32_t y) noexcept
{
    std::uint8_t* const bytes = static_cast<std::uint8_t*>(samples);
    return reinterpret_cast<SampleT*>(bytes + *layout.rowByteOffset(channel / layout.samplesPerPixel(), y)) +
           channel % layout.samplesPerPixel();
}

// Reads every channel of one pixel and decides whether it participates: the mask and every
// channel's value rule must admit it. A participating pixel with a non-finite channel is refused.
template <ResidentSample SampleT>
[[nodiscard]] Status readPixel(const void* samples, const ImageLayout& layout, const KernelValidityView& validity,
                               std::uint32_t x, std::uint32_t y, std::array<double, 3u>& values, bool& accepted,
                               const char* operation)
{
    accepted = maskAccepts(validity, x, y);
    const std::uint32_t channels = layout.extent().channelCount();
    const std::size_t column = static_cast<std::size_t>(x) * layout.samplesPerPixel();
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        const SampleT sample = channelRow<SampleT>(samples, layout, channel, y)[column];
        values[channel] = static_cast<double>(sample);
        accepted = accepted && sampleIsValidByValue(validity.descriptor, sample);
    }
    if (!accepted)
    {
        return Status::success();
    }
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        if (!std::isfinite(values[channel]))
        {
            return stretchFailure(operation, ErrorCode::InvalidData,
                                  "a selected statistical-stretch sample is not finite");
        }
    }
    return Status::success();
}

// ---------------------------------------------------------------------------------------------
// The spread: a bounded deterministic sample, reduced in linear time.

// The lower middle value plus half the distance to the upper one, which is the order-statistic
// primitive's median. The values are reordered.
[[nodiscard]] double medianInPlace(double* values, std::uint64_t count) noexcept
{
    const std::uint64_t lower = (count - 1u) / 2u;
    std::nth_element(values, values + lower, values + count);
    const double low = values[lower];
    const double high = count % 2u != 0u ? low : *std::min_element(values + lower + 1u, values + count);
    return low + 0.5 * (high - low);
}

// 1.4826 times the median absolute deviation of every value at or below the sample's median, about
// that lower half's own median. The sample is reordered.
[[nodiscard]] double lowerHalfSigmaOf(double* values, std::uint64_t count) noexcept
{
    const double median = medianInPlace(values, count);
    const std::uint64_t lowerCount = static_cast<std::uint64_t>(
        std::partition(values, values + count, [median](double value) { return value <= median; }) - values);
    const double lowerMedian = medianInPlace(values, lowerCount);
    for (std::uint64_t index = 0u; index < lowerCount; ++index)
    {
        values[index] = std::abs(values[index] - lowerMedian);
    }
    return medianInPlace(values, lowerCount) * kLowerHalfMadToSigma;
}

// The candidates one group's sample is drawn from: every pixel's value for a mono or unlinked
// group, and every channel's in channel-major order for linked RGB.
struct SampleShape
{
    std::uint64_t candidates = 0u;
    std::uint64_t stride = 1u;
    std::uint64_t slots = 0u;
};

[[nodiscard]] SampleShape sampleShapeOf(const ImageLayout& layout, bool linked) noexcept
{
    const std::uint64_t pixels = static_cast<std::uint64_t>(layout.extent().width()) * layout.extent().height();
    SampleShape shape;
    shape.candidates = linked ? pixels * layout.extent().channelCount() : pixels;
    shape.stride = (std::max)(std::uint64_t{1}, (shape.candidates + kMaxStatisticalStretchSamplesPerChannel - 1u) /
                                                    kMaxStatisticalStretchSamplesPerChannel);
    shape.slots = (shape.candidates + shape.stride - 1u) / shape.stride;
    return shape;
}

// Gathers one group's sample: every participating candidate whose index is a multiple of the
// stride, and the first participating candidate when that is not one. The slots are read on the
// operation's threads into fixed places, so the sample is the same set at every width.
template <ResidentSample SampleT>
[[nodiscard]] Result<std::uint64_t> gatherSample(const ExecutionContext& execution, const void* samples,
                                                 const ImageLayout& layout, const KernelValidityView& validity,
                                                 bool linked, std::uint32_t group, const SampleShape& shape,
                                                 double* values)
{
    const std::uint32_t width = layout.extent().width();
    const std::uint64_t pixels = static_cast<std::uint64_t>(width) * layout.extent().height();
    const bool everyPixel = admitsEveryPixel(validity);
    constexpr double kAbsent = std::numeric_limits<double>::quiet_NaN();
    const TaskCost cost{.bytesPerItem = sizeof(SampleT) * layout.extent().channelCount() + sizeof(double),
                        .operationsPerItem = 16u};
    const auto valueAt = [&](std::uint64_t candidate, double& value) -> Status {
        const std::uint32_t channel = linked ? static_cast<std::uint32_t>(candidate / pixels) : group;
        const std::uint64_t pixel = linked ? candidate % pixels : candidate;
        const std::uint32_t x = static_cast<std::uint32_t>(pixel % width);
        const std::uint32_t y = static_cast<std::uint32_t>(pixel / width);
        if (everyPixel)
        {
            value = static_cast<double>(channelRow<SampleT>(samples, layout, channel,
                                                            y)[static_cast<std::size_t>(x) * layout.samplesPerPixel()]);
            return Status::success();
        }
        std::array<double, 3u> pixelValues{};
        bool accepted = false;
        const Status read =
            readPixel<SampleT>(samples, layout, validity, x, y, pixelValues, accepted, kAnalysisOperation);
        value = accepted ? pixelValues[channel] : kAbsent;
        return read;
    };
    const Status filled = parallelFor(execution, shape.slots, cost, [&](TaskRange range) -> Status {
        for (std::uint64_t slot = range.begin; slot < range.end; ++slot)
        {
            const Status read = valueAt(slot * shape.stride, values[slot]);
            if (read.failed())
            {
                return read;
            }
        }
        return Status::success();
    });
    if (filled.failed())
    {
        return filled;
    }
    const bool firstTaken = !std::isnan(values[0]);
    std::uint64_t count = 0u;
    for (std::uint64_t slot = 0u; slot < shape.slots; ++slot)
    {
        if (!std::isnan(values[slot]))
        {
            values[count++] = values[slot];
        }
    }
    if (!firstTaken)
    {
        // The first participating candidate, which keeps a sparse selection from sampling nothing.
        // Candidates run channel-major, so it is the first participating pixel's first channel.
        for (std::uint64_t candidate = 1u; candidate < pixels; ++candidate)
        {
            double value = kAbsent;
            const Status read = valueAt(candidate, value);
            if (read.failed())
            {
                return read;
            }
            if (!std::isnan(value))
            {
                if (candidate % shape.stride != 0u)
                {
                    values[count++] = value;
                }
                break;
            }
        }
    }
    return count;
}

// ---------------------------------------------------------------------------------------------
// The exact minimum and median of RGB groups, selected by counting over whole pixels.

// An unsigned key whose order is the order of finite doubles; both zeros share one key.
[[nodiscard]] std::uint64_t orderKey(double value) noexcept
{
    const std::uint64_t bits = std::bit_cast<std::uint64_t>(value == 0.0 ? 0.0 : value);
    return (bits >> 63u) != 0u ? ~bits : bits | (std::uint64_t{1} << 63u);
}

[[nodiscard]] double valueOfKey(std::uint64_t key) noexcept
{
    return std::bit_cast<double>((key >> 63u) != 0u ? key & ~(std::uint64_t{1} << 63u) : ~key);
}

// A key is fixed in five digits of eleven bits and a last one of nine, from the top.
[[nodiscard]] constexpr std::uint32_t digitWidth(std::uint32_t level) noexcept
{
    return level + 1u < kKeyLevels ? kDigitBits : 64u - kDigitBits * (kKeyLevels - 1u);
}

// How many key bits lie below one level's digit.
[[nodiscard]] constexpr std::uint32_t digitShift(std::uint32_t level) noexcept
{
    return 64u - kDigitBits * level - digitWidth(level);
}

[[nodiscard]] std::uint32_t digitOf(std::uint64_t key, std::uint32_t level) noexcept
{
    return static_cast<std::uint32_t>(key >> digitShift(level)) & ((1u << digitWidth(level)) - 1u);
}

// The whole key of a value whose key bits above `shift` are `prefix` and whose lower bits carry no
// information: a Float32 value widened to double has its lowest 29 bits clear, which a negative
// value's key holds as ones.
[[nodiscard]] std::uint64_t completedKey(std::uint64_t prefix, std::uint32_t shift) noexcept
{
    const std::uint64_t key = shift < 64u ? prefix << shift : 0u;
    return (key >> 63u) != 0u || shift == 0u ? key : key | ((std::uint64_t{1} << shift) - 1u);
}

// Where one group's median descent stands: the rank it looks for, the key bits fixed so far and how
// many samples share them, and what the last pass is to do for it.
struct GroupDescent
{
    std::uint64_t count = 0u;
    double minimum = std::numeric_limits<double>::infinity();
    std::uint64_t rank = 0u;
    std::uint64_t prefix = 0u;
    std::uint32_t level = 0u;
    bool gathering = false;
    bool resolved = false;
    double lower = 0.0;
    std::uint64_t atOrBelow = 0u;
    double above = std::numeric_limits<double>::infinity();
};

template <ResidentSample SampleT> struct RgbSelection
{
    const ExecutionContext& execution;
    const void* samples;
    const ImageLayout& layout;
    const KernelValidityView& validity;
    bool linked;
    std::uint32_t groups;
    std::uint32_t workers;
    std::uint32_t* tables;
    double* gathered;
    std::array<GroupDescent, 3u>& descents;

    [[nodiscard]] TaskCost cost() const noexcept
    {
        const std::uint64_t width = layout.extent().width();
        return TaskCost{.bytesPerItem = width * layout.extent().channelCount() * sizeof(SampleT),
                        .operationsPerItem = width * layout.extent().channelCount() * 12u};
    }

    // Calls `visit(slot, group, value)` for every value of every participating pixel.
    template <typename Visit> [[nodiscard]] Status forEachValue(Visit&& visit) const
    {
        const std::uint32_t width = layout.extent().width();
        const std::uint32_t channels = layout.extent().channelCount();
        return parallelForWorkers(execution, layout.extent().height(), cost(), [&](WorkerTask task) -> Status {
            for (std::uint64_t y64 = task.range.begin; y64 < task.range.end; ++y64)
            {
                const std::uint32_t y = static_cast<std::uint32_t>(y64);
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    std::array<double, 3u> pixel{};
                    bool accepted = false;
                    const Status read =
                        readPixel<SampleT>(samples, layout, validity, x, y, pixel, accepted, kAnalysisOperation);
                    if (read.failed())
                    {
                        return read;
                    }
                    if (!accepted)
                    {
                        continue;
                    }
                    for (std::uint32_t channel = 0u; channel < channels; ++channel)
                    {
                        visit(task.workerSlot, linked ? 0u : channel, pixel[channel]);
                    }
                }
            }
            return Status::success();
        });
    }

    [[nodiscard]] std::uint32_t* table(std::uint32_t worker, std::uint32_t group) const noexcept
    {
        return tables + (static_cast<std::uint64_t>(worker) * groups + group) * kDigitBuckets;
    }

    // Merges every worker's table for `group` into worker zero's and fixes the next digit.
    void descend(std::uint32_t group)
    {
        GroupDescent& descent = descents[group];
        std::uint32_t* const merged = table(0u, group);
        for (std::uint32_t worker = 1u; worker < workers; ++worker)
        {
            const std::uint32_t* const partial = table(worker, group);
            for (std::uint32_t bucket = 0u; bucket < kDigitBuckets; ++bucket)
            {
                merged[bucket] += partial[bucket];
            }
        }
        std::uint32_t bucket = 0u;
        while (descent.rank >= merged[bucket])
        {
            descent.rank -= merged[bucket];
            ++bucket;
        }
        descent.prefix = descent.prefix << digitWidth(descent.level) | bucket;
        const std::uint32_t shift = digitShift(descent.level);
        // Once the digits above a Float32 sample's clear bits are fixed, every sample left holds the
        // same value.
        const bool determined = shift == 0u || (std::is_same_v<SampleT, float> && shift <= 29u);
        if (determined)
        {
            descent.lower = valueOfKey(completedKey(descent.prefix, shift));
            descent.resolved = true;
            return;
        }
        descent.gathering = merged[bucket] <= kSelectionGather;
        ++descent.level;
    }

    [[nodiscard]] bool matches(const GroupDescent& descent, std::uint64_t key) const noexcept
    {
        return descent.level == 0u || (key >> digitShift(descent.level - 1u)) == descent.prefix;
    }
};

// The exact minimum and median of each group and the number of participating pixels.
template <ResidentSample SampleT>
[[nodiscard]] Status selectRgbStatistics(const ExecutionContext& execution, const void* samples,
                                         const ImageLayout& layout, const KernelValidityView& validity, bool linked,
                                         std::array<StatisticalStretchChannelAnalysis, 3u>& channels,
                                         std::uint64_t& participating, std::uint64_t& scratchBytes)
{
    const std::uint32_t groups = linked ? 1u : 3u;
    std::array<GroupDescent, 3u> descents{};
    RgbSelection<SampleT> selection{.execution = execution,
                                    .samples = samples,
                                    .layout = layout,
                                    .validity = validity,
                                    .linked = linked,
                                    .groups = groups,
                                    .workers = 0u,
                                    .tables = nullptr,
                                    .gathered = nullptr,
                                    .descents = descents};
    selection.workers = (std::max)(1u, plannedWorkerCount(execution, layout.extent().height(), selection.cost()));
    const std::uint64_t tableBytes =
        static_cast<std::uint64_t>(selection.workers) * groups * kDigitBuckets * sizeof(std::uint32_t);
    const std::uint64_t gatherBytes = groups * kSelectionGather * sizeof(double);
    Result<plane_detail::ScopedContextMemory> tables =
        plane_detail::ScopedContextMemory::take(execution, tableBytes, alignof(std::uint64_t), kAnalysisOperation);
    if (!tables.hasValue())
    {
        return tables.status();
    }
    Result<plane_detail::ScopedContextMemory> gathered =
        plane_detail::ScopedContextMemory::take(execution, gatherBytes, alignof(double), kAnalysisOperation);
    if (!gathered.hasValue())
    {
        return gathered.status();
    }
    scratchBytes = tableBytes + gatherBytes;
    selection.tables = static_cast<std::uint32_t*>(tables->data());
    selection.gathered = static_cast<double*>(gathered->data());
    std::array<std::atomic<std::uint64_t>, 3u> gatheredCounts{};

    // The first pass counts, finds the minimum and fixes the first digit; each later one fixes the
    // next digit of every group still descending, or gathers the few samples a bucket holds.
    std::fill_n(selection.tables, tableBytes / sizeof(std::uint32_t), 0u);
    std::atomic<std::uint64_t> pixels{0u};
    std::array<std::atomic<std::uint64_t>, 3u> counts{};
    std::array<std::atomic<std::uint64_t>, 3u> minimumKeys{};
    for (auto& key : minimumKeys)
    {
        key.store(~std::uint64_t{0}, std::memory_order_relaxed);
    }
    {
        const std::uint32_t width = layout.extent().width();
        const Status counted =
            parallelForWorkers(execution, layout.extent().height(), selection.cost(), [&](WorkerTask task) -> Status {
                std::array<std::uint64_t, 3u> localCounts{};
                std::array<std::uint64_t, 3u> localMinimum{~std::uint64_t{0}, ~std::uint64_t{0}, ~std::uint64_t{0}};
                std::uint64_t localPixels = 0u;
                for (std::uint64_t y64 = task.range.begin; y64 < task.range.end; ++y64)
                {
                    const std::uint32_t y = static_cast<std::uint32_t>(y64);
                    for (std::uint32_t x = 0u; x < width; ++x)
                    {
                        std::array<double, 3u> pixel{};
                        bool accepted = false;
                        const Status read =
                            readPixel<SampleT>(samples, layout, validity, x, y, pixel, accepted, kAnalysisOperation);
                        if (read.failed())
                        {
                            return read;
                        }
                        if (!accepted)
                        {
                            continue;
                        }
                        ++localPixels;
                        for (std::uint32_t channel = 0u; channel < 3u; ++channel)
                        {
                            const std::uint32_t group = linked ? 0u : channel;
                            const std::uint64_t key = orderKey(pixel[channel]);
                            ++localCounts[group];
                            localMinimum[group] = (std::min)(localMinimum[group], key);
                            ++selection.table(task.workerSlot, group)[digitOf(key, 0u)];
                        }
                    }
                }
                pixels.fetch_add(localPixels, std::memory_order_relaxed);
                for (std::uint32_t group = 0u; group < groups; ++group)
                {
                    counts[group].fetch_add(localCounts[group], std::memory_order_relaxed);
                    std::uint64_t seen = minimumKeys[group].load(std::memory_order_relaxed);
                    while (localMinimum[group] < seen && !minimumKeys[group].compare_exchange_weak(
                                                             seen, localMinimum[group], std::memory_order_relaxed))
                    {
                    }
                }
                return Status::success();
            });
        if (counted.failed())
        {
            return counted;
        }
    }
    participating = pixels.load(std::memory_order_relaxed);
    if (participating == 0u)
    {
        return stretchFailure(kAnalysisOperation, ErrorCode::EmptySelection,
                              "no pixel participates in this statistical-stretch analysis");
    }
    for (std::uint32_t group = 0u; group < groups; ++group)
    {
        descents[group].count = counts[group].load(std::memory_order_relaxed);
        descents[group].minimum = valueOfKey(minimumKeys[group].load(std::memory_order_relaxed));
        descents[group].rank = (descents[group].count - 1u) / 2u;
        selection.descend(group);
    }

    const auto pending = [&] {
        return std::any_of(descents.begin(), descents.begin() + groups,
                           [](const GroupDescent& descent) { return !descent.resolved; });
    };
    while (pending())
    {
        std::fill_n(selection.tables, tableBytes / sizeof(std::uint32_t), 0u);
        for (auto& count : gatheredCounts)
        {
            count.store(0u, std::memory_order_relaxed);
        }
        const Status walked = selection.forEachValue([&](std::uint32_t slot, std::uint32_t group, double value) {
            const GroupDescent& descent = descents[group];
            if (descent.resolved)
            {
                return;
            }
            const std::uint64_t key = orderKey(value);
            if (!selection.matches(descent, key))
            {
                return;
            }
            if (descent.gathering)
            {
                const std::uint64_t place = gatheredCounts[group].fetch_add(1u, std::memory_order_relaxed);
                selection.gathered[group * kSelectionGather + place] = value;
                return;
            }
            ++selection.table(slot, group)[digitOf(key, descent.level)];
        });
        if (walked.failed())
        {
            return walked;
        }
        for (std::uint32_t group = 0u; group < groups; ++group)
        {
            GroupDescent& descent = descents[group];
            if (descent.resolved)
            {
                continue;
            }
            if (descent.gathering)
            {
                double* const values = selection.gathered + group * kSelectionGather;
                const std::uint64_t held = gatheredCounts[group].load(std::memory_order_relaxed);
                std::nth_element(values, values + descent.rank, values + held);
                descent.lower = values[descent.rank];
                descent.resolved = true;
                continue;
            }
            selection.descend(group);
        }
    }

    // The upper middle value: the lower one again when enough samples equal it, otherwise the
    // smallest sample above it.
    std::array<std::atomic<std::uint64_t>, 3u> atOrBelow{};
    std::array<std::atomic<std::uint64_t>, 3u> aboveKeys{};
    for (auto& key : aboveKeys)
    {
        key.store(~std::uint64_t{0}, std::memory_order_relaxed);
    }
    const Status bracketed = selection.forEachValue([&](std::uint32_t, std::uint32_t group, double value) {
        if (value <= descents[group].lower)
        {
            atOrBelow[group].fetch_add(1u, std::memory_order_relaxed);
            return;
        }
        const std::uint64_t key = orderKey(value);
        std::uint64_t seen = aboveKeys[group].load(std::memory_order_relaxed);
        while (key < seen && !aboveKeys[group].compare_exchange_weak(seen, key, std::memory_order_relaxed))
        {
        }
    });
    if (bracketed.failed())
    {
        return bracketed;
    }
    for (std::uint32_t group = 0u; group < groups; ++group)
    {
        const GroupDescent& descent = descents[group];
        const std::uint64_t lowerRank = (descent.count - 1u) / 2u;
        const double upper =
            descent.count % 2u != 0u || atOrBelow[group].load(std::memory_order_relaxed) > lowerRank + 1u
                ? descent.lower
                : valueOfKey(aboveKeys[group].load(std::memory_order_relaxed));
        channels[group].minimum = descent.minimum;
        channels[group].median = descent.lower + 0.5 * (upper - descent.lower);
    }
    return Status::success();
}

template <ResidentSample SampleT>
[[nodiscard]] Result<StatisticalStretchAnalysis>
analyzeTyped(const ExecutionContext& execution, const void* sourceSamples, const ImageLayout& sourceLayout, bool linked,
             const KernelValidityView& validity)
{
    const std::uint32_t width = sourceLayout.extent().width();
    const std::uint32_t height = sourceLayout.extent().height();
    const std::uint32_t channels = sourceLayout.extent().channelCount();
    const std::uint64_t pixels = static_cast<std::uint64_t>(width) * height;
    if (pixels == 0u)
    {
        return stretchFailure(kAnalysisOperation, ErrorCode::EmptySelection,
                              "a statistical stretch was asked to analyze an empty image");
    }
    StatisticalStretchAnalysis result;
    result.linked = linked;
    result.sampleType = kSampleTypeOf<SampleT>;
    result.width = width;
    result.height = height;
    result.channelCount = channels;
    std::uint64_t participating = 0u;
    std::uint64_t selectionBytes = 0u;
    if (channels == 1u)
    {
        const KernelPlaneSource source{.sampleType = kSampleTypeOf<SampleT>,
                                       .samples = sourceSamples,
                                       .layout = &sourceLayout,
                                       .plane = 0u,
                                       .sampleInPixel = 0u};
        const Result<PlaneOrderStatistics> order =
            selectPlaneOrderStatistics(execution, source, {}, {}, PlaneOrderRequest{}, validity);
        if (!order.hasValue())
        {
            return order.status();
        }
        participating = order->run.selection.valid;
        selectionBytes = order->peakScratchBytes;
        result.channels[0].minimum = order->minimum;
        result.channels[0].median = order->median;
    }
    else
    {
        const Status selected = selectRgbStatistics<SampleT>(execution, sourceSamples, sourceLayout, validity, linked,
                                                             result.channels, participating, selectionBytes);
        if (selected.failed())
        {
            return selected;
        }
    }
    result.selection = ValidityCount{.valid = participating, .total = pixels};

    // One group's sample at a time, in one buffer the size of the largest.
    const SampleShape shape = sampleShapeOf(sourceLayout, linked || channels == 1u);
    const std::uint64_t sampleBytes = shape.slots * sizeof(double);
    Result<plane_detail::ScopedContextMemory> scratch =
        plane_detail::ScopedContextMemory::take(execution, sampleBytes, alignof(double), kAnalysisOperation);
    if (!scratch.hasValue())
    {
        return scratch.status();
    }
    double* const values = static_cast<double*>(scratch->data());
    const std::uint32_t groups = linked || channels == 1u ? 1u : channels;
    for (std::uint32_t group = 0u; group < groups; ++group)
    {
        const Result<std::uint64_t> gathered = gatherSample<SampleT>(execution, sourceSamples, sourceLayout, validity,
                                                                     linked && channels == 3u, group, shape, values);
        if (!gathered.hasValue())
        {
            return gathered.status();
        }
        if (*gathered == 0u)
        {
            return stretchFailure(kAnalysisOperation, ErrorCode::EmptySelection,
                                  "the deterministic statistical-stretch sample retained no value");
        }
        result.channels[group].sampledValues = *gathered;
        result.channels[group].lowerHalfSigma = lowerHalfSigmaOf(values, *gathered);
    }
    if (linked && channels == 3u)
    {
        result.channels[1] = result.channels[0];
        result.channels[2] = result.channels[0];
    }
    result.peakScratchBytes = (std::max)(selectionBytes, sampleBytes);
    return result;
}

[[nodiscard]] Status validateAnalysis(const StatisticalStretchAnalysis& analysis, SampleType sampleType,
                                      const ImageLayout& layout, bool linked, const char* operation)
{
    if (analysis.sampleType != sampleType || analysis.width != layout.extent().width() ||
        analysis.height != layout.extent().height() || analysis.channelCount != layout.extent().channelCount() ||
        analysis.linked != linked)
    {
        return stretchFailure(operation, ErrorCode::IncompatibleArguments,
                              "a statistical-stretch analysis does not describe this source and request");
    }
    const std::uint32_t groups = linked || analysis.channelCount == 1u ? 1u : analysis.channelCount;
    for (std::uint32_t group = 0u; group < groups; ++group)
    {
        const StatisticalStretchChannelAnalysis& channel = analysis.channels[group];
        if (channel.sampledValues == 0u || !std::isfinite(channel.minimum) || !std::isfinite(channel.median) ||
            !std::isfinite(channel.lowerHalfSigma) || channel.lowerHalfSigma < 0.0)
        {
            return stretchFailure(operation, ErrorCode::InvalidArgument,
                                  "a statistical-stretch analysis contains no valid channel statistics");
        }
    }
    return Status::success();
}

[[nodiscard]] std::array<double, 3u> blackPointsOf(const StatisticalStretchAnalysis& analysis, double blackpointSigma,
                                                   bool noBlackClip) noexcept
{
    std::array<double, 3u> result{};
    const std::uint32_t groups = analysis.linked || analysis.channelCount == 1u ? 1u : analysis.channelCount;
    for (std::uint32_t group = 0u; group < groups; ++group)
    {
        const StatisticalStretchChannelAnalysis& channel = analysis.channels[group];
        result[group] =
            noBlackClip
                ? channel.minimum
                : (std::min)((std::max)(channel.minimum, channel.median - blackpointSigma * channel.lowerHalfSigma),
                             0.99);
    }
    if (analysis.linked && analysis.channelCount == 3u)
    {
        result[1] = result[0];
        result[2] = result[0];
    }
    return result;
}

// Participating pixels, and those with at least one channel at or below its black point.
struct PixelCounts
{
    std::uint64_t participating = 0u;
    std::uint64_t clipped = 0u;
};

template <ResidentSample SampleT>
[[nodiscard]] Result<PixelCounts>
countPixels(const ExecutionContext& execution, const void* sourceSamples, const ImageLayout& sourceLayout,
            const std::array<double, 3u>& blackPoints, const KernelValidityView& validity, const char* operation)
{
    const std::uint32_t width = sourceLayout.extent().width();
    const std::uint32_t channels = sourceLayout.extent().channelCount();
    const bool fast = channels == 1u && admitsEveryPixel(validity);
    std::atomic<std::uint64_t> participating{0u};
    std::atomic<std::uint64_t> clipped{0u};
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * channels * sizeof(SampleT),
                        .operationsPerItem = static_cast<std::uint64_t>(width) * channels * 2u};
    const Status walked = parallelFor(execution, sourceLayout.extent().height(), cost, [&](TaskRange range) -> Status {
        PixelCounts local;
        for (std::uint64_t y64 = range.begin; y64 < range.end; ++y64)
        {
            const std::uint32_t y = static_cast<std::uint32_t>(y64);
            if (fast)
            {
                const SampleT* const row = channelRow<SampleT>(sourceSamples, sourceLayout, 0u, y);
                const std::size_t step = sourceLayout.samplesPerPixel();
                std::uint64_t below = 0u;
                bool finite = true;
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    const double value = static_cast<double>(row[static_cast<std::size_t>(x) * step]);
                    finite = finite && std::isfinite(value);
                    below += value <= blackPoints[0] ? 1u : 0u;
                }
                if (!finite)
                {
                    return stretchFailure(operation, ErrorCode::InvalidData,
                                          "a selected statistical-stretch sample is not finite");
                }
                local.participating += width;
                local.clipped += below;
                continue;
            }
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                std::array<double, 3u> pixel{};
                bool accepted = false;
                const Status read =
                    readPixel<SampleT>(sourceSamples, sourceLayout, validity, x, y, pixel, accepted, operation);
                if (read.failed())
                {
                    return read;
                }
                if (!accepted)
                {
                    continue;
                }
                ++local.participating;
                bool isClipped = false;
                for (std::uint32_t channel = 0u; channel < channels; ++channel)
                {
                    isClipped = isClipped || pixel[channel] <= blackPoints[channel];
                }
                local.clipped += isClipped ? 1u : 0u;
            }
        }
        participating.fetch_add(local.participating, std::memory_order_relaxed);
        clipped.fetch_add(local.clipped, std::memory_order_relaxed);
        return Status::success();
    });
    if (walked.failed())
    {
        return walked;
    }
    return PixelCounts{.participating = participating.load(std::memory_order_relaxed),
                       .clipped = clipped.load(std::memory_order_relaxed)};
}

// ---------------------------------------------------------------------------------------------
// The transfer, planned once per group.

struct HighlightCurve
{
    std::array<double, 6u> x{};
    std::array<double, 6u> y{};
};

// The six-point highlight curve: the identity up to the target median, then two
// points above it lifted by raising them to `1 - boost` (the upper one twice), then one. It is read
// by linear interpolation between the points, so a boost of zero leaves every value in the unit
// interval unchanged; a value above one reads the last point.
[[nodiscard]] HighlightCurve makeHighlightCurve(double targetMedian, double boost) noexcept
{
    HighlightCurve curve;
    curve.x = {0.0,
               0.5 * targetMedian,
               targetMedian,
               0.25 * (1.0 - targetMedian) + targetMedian,
               0.75 * (1.0 - targetMedian) + targetMedian,
               1.0};
    curve.y = {0.0,
               0.5 * targetMedian,
               targetMedian,
               std::pow(curve.x[3], 1.0 - boost),
               std::pow(std::pow(curve.x[4], 1.0 - boost), 1.0 - boost),
               1.0};
    return curve;
}

[[nodiscard]] double applyHighlightCurve(double value, const HighlightCurve& curve) noexcept
{
    if (value <= curve.x.front())
    {
        return curve.y.front();
    }
    for (std::size_t index = 0u; index + 1u < curve.x.size(); ++index)
    {
        if (value < curve.x[index + 1u])
        {
            const double ratio = (value - curve.x[index]) / (curve.x[index + 1u] - curve.x[index]);
            return curve.y[index] + ratio * (curve.y[index + 1u] - curve.y[index]);
        }
    }
    return curve.y.back();
}

// One group's transfer. For the black point `b`, the median rescaled to `m = (median - b) / s` with
// `s = max(1 - b, 1e-12)`, and the target `t`, the rescaled sample `r = (v - b) / s` held to the unit
// interval maps to `(m - 1) t r / (m (t + r - 1) - t r)`. Negating both terms and multiplying them by
// `s` gives `(1 - m) t w / ((median - b)(1 - t) + (t - m) w)` over the excess `w = v - b` held to
// `[0, s]`, one division per sample. For a median below the target every term is nonnegative. The
// defining denominator's guard, which replaces it by 1e-12 when it is nearer zero than that, is the
// same test scaled by `s`.
struct StretchPlan
{
    double blackPoint = 0.0;
    double excessLimit = 1.0;
    double rise = 0.0;
    double base = 0.0;
    double slope = 0.0;
    double guard = 1.0e-12;
    // Whether the denominator can come within the guard for some excess in `[0, s]`. It is linear in
    // the excess, so its two ends decide.
    bool guarded = true;
};

[[nodiscard]] StretchPlan planStretch(double median, double blackPoint, double target) noexcept
{
    StretchPlan plan;
    plan.blackPoint = blackPoint;
    plan.excessLimit = (std::max)(1.0 - blackPoint, 1.0e-12);
    const double rescaledMedian = (median - blackPoint) / plan.excessLimit;
    plan.rise = (1.0 - rescaledMedian) * target;
    plan.base = (median - blackPoint) * (1.0 - target);
    plan.slope = target - rescaledMedian;
    plan.guard = 1.0e-12 * plan.excessLimit;
    const double nearest = (std::min)(plan.base, plan.base + plan.slope * plan.excessLimit);
    plan.guarded = !(nearest >= plan.guard) || !(plan.rise >= 0.0);
    return plan;
}

[[nodiscard]] double transferSample(double value, const StretchPlan& plan) noexcept
{
    const double excess = value - plan.blackPoint > 0.0 ? (std::min)(value - plan.blackPoint, plan.excessLimit)
                                                        : (value == value ? 0.0 : value);
    const double denominator = plan.base + plan.slope * excess;
    return std::abs(denominator) < plan.guard ? -plan.rise * excess / plan.guard : plan.rise * excess / denominator;
}

// The finishing passes of one transferred sample: the optional highlight curve, the normalization
// divisor and the clamp, which writes positive zero for anything at or below zero.
struct StretchFinish
{
    bool applyCurves = false;
    HighlightCurve curve{};
    double divisor = 1.0;
};

[[nodiscard]] double finishSample(double transferred, const StretchFinish& finish) noexcept
{
    double value = finish.applyCurves ? applyHighlightCurve(transferred, finish.curve) : transferred;
    if (finish.divisor != 1.0)
    {
        value /= finish.divisor;
    }
    return value > 0.0 ? (value < 1.0 ? value : 1.0) : 0.0;
}

template <ResidentSample SampleT>
[[nodiscard]] Result<double> transformedMaximum(const ExecutionContext& execution, const void* sourceSamples,
                                                const ImageLayout& sourceLayout,
                                                const std::array<StretchPlan, 3u>& plans,
                                                const StretchFinish& curveOnly, const KernelValidityView& validity)
{
    const std::uint32_t width = sourceLayout.extent().width();
    const std::uint32_t channels = sourceLayout.extent().channelCount();
    const bool fast = channels == 1u && admitsEveryPixel(validity);
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * channels * sizeof(SampleT),
                        .operationsPerItem = static_cast<std::uint64_t>(width) * channels * 18u};
    const auto transformed = [&](double value, std::uint32_t channel) {
        const double transfer = transferSample(value, plans[channel]);
        return curveOnly.applyCurves ? applyHighlightCurve(transfer, curveOnly.curve) : transfer;
    };
    const auto body = [&](TaskRange range, double& partial) -> Status {
        for (std::uint64_t y64 = range.begin; y64 < range.end; ++y64)
        {
            const std::uint32_t y = static_cast<std::uint32_t>(y64);
            if (fast)
            {
                const SampleT* const row = channelRow<SampleT>(sourceSamples, sourceLayout, 0u, y);
                const std::size_t step = sourceLayout.samplesPerPixel();
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    const double value = static_cast<double>(row[static_cast<std::size_t>(x) * step]);
                    const double result = transformed(value, 0u);
                    if (!std::isfinite(result))
                    {
                        return stretchFailure(kStretchOperation, ErrorCode::InvalidData,
                                              "a statistical-stretch transform produced a non-finite value");
                    }
                    partial = (std::max)(partial, result);
                }
                continue;
            }
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                std::array<double, 3u> pixel{};
                bool accepted = false;
                const Status read =
                    readPixel<SampleT>(sourceSamples, sourceLayout, validity, x, y, pixel, accepted, kStretchOperation);
                if (read.failed())
                {
                    return read;
                }
                if (!accepted)
                {
                    continue;
                }
                for (std::uint32_t channel = 0u; channel < channels; ++channel)
                {
                    const double result = transformed(pixel[channel], channel);
                    if (!std::isfinite(result))
                    {
                        return stretchFailure(kStretchOperation, ErrorCode::InvalidData,
                                              "a statistical-stretch transform produced a non-finite value");
                    }
                    partial = (std::max)(partial, result);
                }
            }
        }
        return Status::success();
    };
    return parallelReduce(execution, sourceLayout.extent().height(), cost, -(std::numeric_limits<double>::infinity)(),
                          body, [](double left, double right) noexcept { return (std::max)(left, right); });
}

// One mono row with nothing to finish and a denominator that stays clear of its guard: the whole
// transfer is a subtraction, two clamps, one multiply-add and one division per sample.
template <ResidentSample SampleT>
[[nodiscard]] bool writePlainRow(const SampleT* source, SampleT* destination, std::uint32_t width,
                                 const StretchPlan& plan, std::uint64_t& clipped) noexcept
{
    constexpr double kLargest = (std::numeric_limits<double>::max)();
    std::uint64_t below = 0u;
    std::uint32_t nonFinite = 0u;
    for (std::uint32_t x = 0u; x < width; ++x)
    {
        const double value = static_cast<double>(source[x]);
        nonFinite |= std::abs(value) <= kLargest ? 0u : 1u;
        const double excess = value - plan.blackPoint;
        below += excess <= 0.0 ? 1u : 0u;
        const double held = excess > 0.0 ? (std::min)(excess, plan.excessLimit) : 0.0;
        destination[x] = static_cast<SampleT>((std::min)(plan.rise * held / (plan.base + plan.slope * held), 1.0));
    }
    clipped += below;
    return nonFinite == 0u;
}

template <ResidentSample SampleT>
[[nodiscard]] Result<PixelCounts>
writeStretch(const ExecutionContext& execution, const void* sourceSamples, const ImageLayout& sourceLayout,
             void* destinationSamples, const ImageLayout& destinationLayout, const std::array<StretchPlan, 3u>& plans,
             const StretchFinish& finish, const KernelValidityView& validity)
{
    const std::uint32_t width = sourceLayout.extent().width();
    const std::uint32_t height = sourceLayout.extent().height();
    const std::uint32_t channels = sourceLayout.extent().channelCount();
    const bool mono = channels == 1u && admitsEveryPixel(validity);
    const bool plain = mono && !finish.applyCurves && finish.divisor == 1.0 && !plans[0].guarded &&
                       sourceLayout.samplesPerPixel() == 1u && destinationLayout.samplesPerPixel() == 1u;
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * channels * sizeof(SampleT) * 2u,
                        .operationsPerItem = static_cast<std::uint64_t>(width) * channels * 20u};
    std::atomic<std::uint64_t> participating{0u};
    std::atomic<std::uint64_t> clipped{0u};
    const Status nonFinite = stretchFailure(kStretchOperation, ErrorCode::InvalidData,
                                            "a selected statistical-stretch sample is not finite");
    const Status walked = parallelFor(execution, height, cost, [&](TaskRange range) -> Status {
        PixelCounts local;
        for (std::uint64_t y64 = range.begin; y64 < range.end; ++y64)
        {
            const std::uint32_t y = static_cast<std::uint32_t>(y64);
            if (plain)
            {
                if (!writePlainRow(channelRow<SampleT>(sourceSamples, sourceLayout, 0u, y),
                                   channelRow<SampleT>(destinationSamples, destinationLayout, 0u, y), width, plans[0],
                                   local.clipped))
                {
                    return nonFinite;
                }
                local.participating += width;
                continue;
            }
            if (mono)
            {
                const SampleT* const source = channelRow<SampleT>(sourceSamples, sourceLayout, 0u, y);
                SampleT* const destination = channelRow<SampleT>(destinationSamples, destinationLayout, 0u, y);
                const std::size_t sourceStep = sourceLayout.samplesPerPixel();
                const std::size_t destinationStep = destinationLayout.samplesPerPixel();
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    const double value = static_cast<double>(source[static_cast<std::size_t>(x) * sourceStep]);
                    if (!std::isfinite(value))
                    {
                        return nonFinite;
                    }
                    local.clipped += value <= plans[0].blackPoint ? 1u : 0u;
                    destination[static_cast<std::size_t>(x) * destinationStep] =
                        static_cast<SampleT>(finishSample(transferSample(value, plans[0]), finish));
                }
                local.participating += width;
                continue;
            }
            std::array<SampleT*, 3u> rows{};
            for (std::uint32_t channel = 0u; channel < channels; ++channel)
            {
                rows[channel] = channelRow<SampleT>(destinationSamples, destinationLayout, channel, y);
            }
            const std::size_t step = destinationLayout.samplesPerPixel();
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                std::array<double, 3u> pixel{};
                bool accepted = false;
                const Status read =
                    readPixel<SampleT>(sourceSamples, sourceLayout, validity, x, y, pixel, accepted, kStretchOperation);
                if (read.failed())
                {
                    return read;
                }
                if (!accepted)
                {
                    continue;
                }
                bool isClipped = false;
                std::array<double, 3u> output{};
                for (std::uint32_t channel = 0u; channel < channels; ++channel)
                {
                    isClipped = isClipped || pixel[channel] <= plans[channel].blackPoint;
                    output[channel] = finishSample(transferSample(pixel[channel], plans[channel]), finish);
                }
                for (std::uint32_t channel = 0u; channel < channels; ++channel)
                {
                    rows[channel][static_cast<std::size_t>(x) * step] = static_cast<SampleT>(output[channel]);
                }
                ++local.participating;
                local.clipped += isClipped ? 1u : 0u;
            }
        }
        participating.fetch_add(local.participating, std::memory_order_relaxed);
        clipped.fetch_add(local.clipped, std::memory_order_relaxed);
        return Status::success();
    });
    if (walked.failed())
    {
        return walked;
    }
    return PixelCounts{.participating = participating.load(std::memory_order_relaxed),
                       .clipped = clipped.load(std::memory_order_relaxed)};
}

template <ResidentSample SampleT>
[[nodiscard]] Result<StatisticalStretchResult>
stretchTyped(const ExecutionContext& execution, const void* sourceSamples, const ImageLayout& sourceLayout,
             void* destinationSamples, const ImageLayout& destinationLayout,
             const StatisticalStretchParameters& parameters, const StatisticalStretchAnalysis* precomputed,
             const KernelValidityView& validity)
{
    StatisticalStretchAnalysis analysis;
    if (precomputed == nullptr)
    {
        Result<StatisticalStretchAnalysis> made =
            analyzeTyped<SampleT>(execution, sourceSamples, sourceLayout, parameters.linked, validity);
        if (!made.hasValue())
        {
            return made.status();
        }
        analysis = *made;
    }
    else
    {
        const Status valid =
            validateAnalysis(*precomputed, kSampleTypeOf<SampleT>, sourceLayout, parameters.linked, kStretchOperation);
        if (valid.failed())
        {
            return valid;
        }
        analysis = *precomputed;
        // A reused analysis must still describe this source's selection, and a stale one is refused
        // before anything is written. Every pixel participates when no rule or mask applies.
        const std::uint64_t pixels =
            static_cast<std::uint64_t>(sourceLayout.extent().width()) * sourceLayout.extent().height();
        std::uint64_t participating = pixels;
        if (!admitsEveryPixel(validity))
        {
            const Result<PixelCounts> counted =
                countPixels<SampleT>(execution, sourceSamples, sourceLayout, {}, validity, kStretchOperation);
            if (!counted.hasValue())
            {
                return counted.status();
            }
            participating = counted->participating;
        }
        if (participating != analysis.selection.valid)
        {
            return stretchFailure(kStretchOperation, ErrorCode::IncompatibleArguments,
                                  "source validity no longer agrees with the precomputed statistical analysis");
        }
    }

    const std::array<double, 3u> blackPoints =
        blackPointsOf(analysis, parameters.blackpointSigma, parameters.noBlackClip);
    std::array<StretchPlan, 3u> plans{};
    for (std::uint32_t channel = 0u; channel < sourceLayout.extent().channelCount(); ++channel)
    {
        const std::uint32_t group = parameters.linked ? 0u : channel;
        plans[channel] = planStretch(analysis.channels[group].median, blackPoints[channel], parameters.targetMedian);
    }
    StretchFinish finish;
    finish.applyCurves = parameters.applyCurves;
    finish.curve = makeHighlightCurve(parameters.targetMedian, parameters.curvesBoost);
    if (parameters.normalize)
    {
        const Result<double> maximum =
            transformedMaximum<SampleT>(execution, sourceSamples, sourceLayout, plans, finish, validity);
        if (!maximum.hasValue())
        {
            return maximum.status();
        }
        if (*maximum > 0.0)
        {
            finish.divisor = *maximum;
        }
    }

    const Result<PixelCounts> written = writeStretch<SampleT>(
        execution, sourceSamples, sourceLayout, destinationSamples, destinationLayout, plans, finish, validity);
    if (!written.hasValue())
    {
        return written.status();
    }
    StatisticalStretchResult result;
    result.run.selection.total =
        static_cast<std::uint64_t>(sourceLayout.extent().width()) * sourceLayout.extent().height();
    result.run.selection.valid = written->participating;
    result.run.outputsWritten = written->participating * sourceLayout.extent().channelCount();
    result.analysis = analysis;
    result.clipping.blackPoints = blackPoints;
    result.clipping.clippedPixels = written->clipped;
    result.clipping.totalPixels = written->participating;
    result.clipping.linked = analysis.linked;
    result.clipping.noBlackClip = parameters.noBlackClip;
    result.normalizationDivisor = finish.divisor;
    return result;
}

} // namespace

Result<StatisticalStretchAnalysis> analyzeImageStatisticalStretch(const ExecutionContext& execution,
                                                                  SampleType sampleType, const void* sourceSamples,
                                                                  const ImageLayout& sourceLayout, bool linked,
                                                                  const KernelValidityView& validity)
{
    const Status valid =
        validateSource(execution, sampleType, sourceSamples, sourceLayout, validity, kAnalysisOperation);
    if (valid.failed())
    {
        return valid;
    }
    if (sampleType == SampleType::Float32)
    {
        return analyzeTyped<float>(execution, sourceSamples, sourceLayout, linked, validity);
    }
    return analyzeTyped<double>(execution, sourceSamples, sourceLayout, linked, validity);
}

Result<StatisticalStretchClipStatistics>
computeStatisticalStretchClipStatistics(const ExecutionContext& execution, SampleType sampleType,
                                        const void* sourceSamples, const ImageLayout& sourceLayout,
                                        const StatisticalStretchAnalysis& analysis, double blackpointSigma,
                                        bool noBlackClip, const KernelValidityView& validity)
{
    const Status valid =
        validateSource(execution, sampleType, sourceSamples, sourceLayout, validity, kClippingOperation);
    if (valid.failed())
    {
        return valid;
    }
    if (!std::isfinite(blackpointSigma) || blackpointSigma < 0.0)
    {
        return stretchFailure(kClippingOperation, ErrorCode::InvalidArgument,
                              "a statistical-stretch black-point sigma is not finite and non-negative");
    }
    const Status analysisValid =
        validateAnalysis(analysis, sampleType, sourceLayout, analysis.linked, kClippingOperation);
    if (analysisValid.failed())
    {
        return analysisValid;
    }
    const std::array<double, 3u> blackPoints = blackPointsOf(analysis, blackpointSigma, noBlackClip);
    const Result<PixelCounts> counted =
        sampleType == SampleType::Float32
            ? countPixels<float>(execution, sourceSamples, sourceLayout, blackPoints, validity, kClippingOperation)
            : countPixels<double>(execution, sourceSamples, sourceLayout, blackPoints, validity, kClippingOperation);
    if (!counted.hasValue())
    {
        return counted.status();
    }
    if (counted->participating != analysis.selection.valid)
    {
        return stretchFailure(kClippingOperation, ErrorCode::IncompatibleArguments,
                              "source validity no longer agrees with the precomputed statistical analysis");
    }
    StatisticalStretchClipStatistics result;
    result.blackPoints = blackPoints;
    result.clippedPixels = counted->clipped;
    result.totalPixels = counted->participating;
    result.linked = analysis.linked;
    result.noBlackClip = noBlackClip;
    return result;
}

Result<StatisticalStretchResult>
stretchImageStatistically(const ExecutionContext& execution, SampleType sampleType, const void* sourceSamples,
                          const ImageLayout& sourceLayout, void* destinationSamples,
                          const ImageLayout& destinationLayout, const StatisticalStretchParameters& parameters,
                          const StatisticalStretchAnalysis* precomputed, const KernelValidityView& validity)
{
    Status valid = validateSource(execution, sampleType, sourceSamples, sourceLayout, validity, kStretchOperation);
    if (valid.failed())
    {
        return valid;
    }
    valid =
        validateDestination(sampleType, sourceSamples, sourceLayout, destinationSamples, destinationLayout, validity);
    if (valid.failed())
    {
        return valid;
    }
    if (!std::isfinite(parameters.targetMedian) || parameters.targetMedian <= 0.0 || parameters.targetMedian >= 1.0 ||
        !std::isfinite(parameters.curvesBoost) || parameters.curvesBoost < 0.0 || parameters.curvesBoost > 1.0 ||
        !std::isfinite(parameters.blackpointSigma) || parameters.blackpointSigma < 0.0)
    {
        return stretchFailure(kStretchOperation, ErrorCode::InvalidArgument,
                              "a statistical-stretch target, curve boost or black-point sigma is invalid");
    }
    if (sampleType == SampleType::Float32)
    {
        return stretchTyped<float>(execution, sourceSamples, sourceLayout, destinationSamples, destinationLayout,
                                   parameters, precomputed, validity);
    }
    return stretchTyped<double>(execution, sourceSamples, sourceLayout, destinationSamples, destinationLayout,
                                parameters, precomputed, validity);
}

} // namespace an
