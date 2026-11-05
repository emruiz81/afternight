// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the cosmetic-correction domain: derive per-channel thresholds, find what
// they select, and repair what a map marks.
//
// Detection reads the image where it lies: one typed pass per channel reads the resident samples
// directly, counts both polarities and composes the packed map bytes in the same walk, on the
// operation's workers. The statistics the thresholds come from are the
// engine's own exact kernels rather than a third derivation of a median, so a detection and a
// caller asking `selectPlaneOrderStatistics` the same question get the same number.
//
// Correction is deliberately shaped as "copy, then repair the marked few" rather than as a filter
// over every pixel: the marked set is sparse on any sensor worth keeping, so the cost is one pass
// plus work proportional to the defect count, and a window buffer per worker rather than anything
// per pixel.

#include <AfterNightEngine/Algorithms/CosmeticCorrection/CosmeticCorrection.h>

#include <AfterNightEngine/Algorithms/Kernel/ReductionKernel.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/SampleConversion.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "kernels/reference/PlaneSourceAccess.h"
#include "kernels/reference/SpatialPlane.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace an
{
namespace
{

using spatial_detail::channelValidity;
using spatial_detail::filterEveryChannel;
using spatial_detail::spatialFailure;
using spatial_detail::SpatialGeometry;
using spatial_detail::spatialLimitFailure;

constexpr const char* kScanOperation = "an::scanImageDeviantPixels";
constexpr const char* kMapOperation = "an::mapImageDeviantPixels";
constexpr const char* kCorrectOperation = "an::correctImageDeviantPixels";

/// What one scanned row costs, for the grain policy.
///
/// The loop walks rows, so the item is a row: a compare or two per sample plus the byte the map
/// write touches. Stating it per sample would put the whole picture in one chunk and leave the scan
/// on a single thread.
///
/// @param width how many pixels one row holds.
/// @return the cost of one row.
[[nodiscard]] TaskCost detectionRowCost(std::uint32_t width) noexcept
{
    const std::uint64_t samples = static_cast<std::uint64_t>(width);
    return TaskCost{.bytesPerItem = samples * 4u, .operationsPerItem = samples * 3u};
}

/// What one repaired row costs, for the grain policy of the copy-and-repair pass.
///
/// @param width how many pixels one row holds.
/// @return the cost of one row.
[[nodiscard]] TaskCost repairRowCost(std::uint32_t width) noexcept
{
    const std::uint64_t samples = static_cast<std::uint64_t>(width);
    return TaskCost{.bytesPerItem = samples * 8u, .operationsPerItem = samples * 4u};
}

/// One polarity's derived threshold and running counts.
struct PolarityState
{
    /// Whether this polarity was asked for.
    bool enabled = false;

    /// The threshold in the comparison domain, read only when enabled.
    float threshold = 0.0f;

    /// Samples this polarity marked, across every channel.
    std::atomic<std::uint64_t> marked{0u};
};

/// Whether a packed map is shaped as one bit-plane per channel of this picture.
///
/// @param map the caller's map view.
/// @param geometry the picture the detection or correction covers.
/// @return true when the extents agree.
template <typename ByteT>
[[nodiscard]] bool mapCoversImage(const ValidityMaskView<ByteT>& map, const SpatialGeometry& geometry) noexcept
{
    return map.extent().width() == geometry.width && map.extent().height() == geometry.height &&
           map.extent().channelCount() == geometry.channelCount;
}

/// The first byte of one channel's row of a packed map.
///
/// @param map the map view.
/// @param channel which bit-plane.
/// @param y which row.
/// @return the byte pointer.
template <typename ByteT>
[[nodiscard]] ByteT* mapRow(const ValidityMaskView<ByteT>& map, std::uint32_t channel, std::uint32_t y) noexcept
{
    return map.data() + static_cast<std::uint64_t>(channel) * map.planeStrideBytes() +
           static_cast<std::uint64_t>(y) * map.rowStrideBytes();
}

/// Derives one channel's thresholds from its exact statistics.
///
/// @param execution the operation policy and engine-context lease.
/// @param source the resident channel to read.
/// @param validity which of its samples participate.
/// @param options which polarities are enabled and how far out each threshold sits.
/// @param cold the cold threshold state, written when enabled.
/// @param hot the hot threshold state, written when enabled.
/// @param record where the derived numbers go, or null to decline them.
/// @param answer the scan result whose pass, worker and scratch counters this call raises.
/// @return success, or whatever the statistics kernels refused.
[[nodiscard]] Status deriveChannelThresholds(const ExecutionContext& execution, const KernelPlaneSource& source,
                                             const KernelValidityView& validity,
                                             const CosmeticDetectionOptions& options, PolarityState& cold,
                                             PolarityState& hot, CosmeticChannelThresholds* record,
                                             CosmeticScanResult& answer)
{
    const Result<PlaneOrderStatistics> ordered = selectPlaneOrderStatistics(
        execution, source, std::span<const double>{}, std::span<double>{}, PlaneOrderRequest{}, validity);
    if (!ordered.hasValue())
    {
        return ordered.status();
    }
    const Result<ReductionStatistics> reduced = reducePlaneStatistics(execution, source, validity);
    if (!reduced.hasValue())
    {
        return reduced.status();
    }

    const double median = ordered->median;
    const double deviation = std::sqrt(reduced->populationVariance);
    if (cold.enabled)
    {
        cold.threshold = static_cast<float>(median - *options.coldSigma * deviation);
    }
    if (hot.enabled)
    {
        hot.threshold = static_cast<float>(median + *options.hotSigma * deviation);
    }
    if (record != nullptr)
    {
        record->median = median;
        record->deviation = deviation;
        record->coldThreshold = cold.enabled ? std::optional<float>{cold.threshold} : std::nullopt;
        record->hotThreshold = hot.enabled ? std::optional<float>{hot.threshold} : std::nullopt;
        record->coldCount = 0u;
        record->hotCount = 0u;
    }

    answer.run.selection.total += ordered->run.selection.total;
    answer.run.selection.valid += ordered->run.selection.valid;
    // The rank passes, the reduction's fixed two, and the direct scan that follows.
    answer.imagePasses += ordered->planeScans + 2u + 1u;
    answer.workingBytes = (std::max)(answer.workingBytes, ordered->peakScratchBytes);
    answer.plannedWorkers = (std::max)(answer.plannedWorkers, ordered->plannedWorkers);
    answer.plannedWorkers = (std::max)(answer.plannedWorkers, reduced->plan.plannedWorkers);
    return Status::success();
}

/// Scans one channel against its thresholds, counting and optionally composing map bytes.
///
/// One typed pass over the resident samples: each row's comparisons are made where the samples lie,
/// and when maps are wanted the row's packed bytes are composed bit by bit and stored whole, so the
/// bits past the last pixel stay zero and nothing the buffer held before survives. Rows belong to
/// one worker each, so two workers never share a map byte.
///
/// @tparam SampleT the resident representation.
/// @param execution the operation policy and engine-context lease.
/// @param source the checked resident channel.
/// @param validity which of its samples participate.
/// @param cold the cold threshold and count, marked bits going to `coldMap` when present.
/// @param hot the hot threshold and count.
/// @param coldMap where cold bits go, or null for the counting scan.
/// @param hotMap where hot bits go, or null for the counting scan.
/// @param channel which bit-plane of the maps this channel owns.
/// @param channelCold this channel's cold count, added to.
/// @param channelHot this channel's hot count, added to.
/// @return success, or an execution or cancellation failure.
template <ResidentSample SampleT>
[[nodiscard]] Status scanChannel(const ExecutionContext& execution, const KernelPlaneSource& source,
                                 const KernelValidityView& validity, PolarityState& cold, PolarityState& hot,
                                 const ValidityMaskView<std::uint8_t>* coldMap,
                                 const ValidityMaskView<std::uint8_t>* hotMap, std::uint32_t channel,
                                 std::atomic<std::uint64_t>& channelCold, std::atomic<std::uint64_t>& channelHot)
{
    const plane_detail::PlaneSampleReader<SampleT> reader{source, validity};
    const std::uint32_t width = reader.width();
    const std::uint32_t columnStride = reader.columnStride();
    const std::uint64_t rowBytes = packedMaskRowBytes(width);
    return parallelFor(execution, reader.height(), detectionRowCost(width), [&](TaskRange range) {
        std::uint64_t coldFound = 0u;
        std::uint64_t hotFound = 0u;
        for (std::uint64_t index = range.begin; index < range.end; ++index)
        {
            const auto y = static_cast<std::uint32_t>(index);
            const SampleT* sample = reader.rowStart(y);
            plane_detail::PlaneMaskCursor mask = reader.maskCursor(y, 0u);
            std::uint8_t* const coldRow = coldMap != nullptr ? mapRow(*coldMap, channel, y) : nullptr;
            std::uint8_t* const hotRow = hotMap != nullptr ? mapRow(*hotMap, channel, y) : nullptr;
            std::uint8_t coldByte = 0u;
            std::uint8_t hotByte = 0u;
            for (std::uint32_t x = 0u; x < width; ++x, sample += columnStride)
            {
                const std::uint32_t bit = x & 7u;
                const bool selected =
                    mask.allowsAndAdvance() && sampleIsValidByValue(validity.descriptor, static_cast<float>(*sample));
                if (selected)
                {
                    const float value = static_cast<float>(*sample);
                    // Hot is tested first, so a sample meeting both thresholds - which needs them
                    // to meet or cross - is marked hot alone and counted once.
                    if (hot.enabled && value >= hot.threshold)
                    {
                        ++hotFound;
                        hotByte = static_cast<std::uint8_t>(hotByte | (1u << bit));
                    }
                    else if (cold.enabled && value <= cold.threshold)
                    {
                        ++coldFound;
                        coldByte = static_cast<std::uint8_t>(coldByte | (1u << bit));
                    }
                }
                if (bit == 7u)
                {
                    const std::uint64_t byteIndex = x >> 3u;
                    if (coldRow != nullptr)
                    {
                        coldRow[byteIndex] = coldByte;
                    }
                    if (hotRow != nullptr)
                    {
                        hotRow[byteIndex] = hotByte;
                    }
                    coldByte = 0u;
                    hotByte = 0u;
                }
            }
            if ((width & 7u) != 0u)
            {
                if (coldRow != nullptr)
                {
                    coldRow[rowBytes - 1u] = coldByte;
                }
                if (hotRow != nullptr)
                {
                    hotRow[rowBytes - 1u] = hotByte;
                }
            }
        }
        cold.marked.fetch_add(coldFound, std::memory_order_relaxed);
        hot.marked.fetch_add(hotFound, std::memory_order_relaxed);
        channelCold.fetch_add(coldFound, std::memory_order_relaxed);
        channelHot.fetch_add(hotFound, std::memory_order_relaxed);
        return Status::success();
    });
}

/// The shared body of the counting scan and the map-writing detection.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType what the samples are.
/// @param source where the image's samples start.
/// @param sourceLayout where they sit.
/// @param options which polarities to look for.
/// @param coldMap where cold bits go, or null for the counting scan.
/// @param hotMap where hot bits go, or null for the counting scan.
/// @param wantsMaps whether this is the recording detection, whose maps must pair with the enabled
///        polarities; the counting scan carries none by construction.
/// @param channelThresholds where each channel's derived numbers go, or empty.
/// @param selection which samples mean something.
/// @param operation the name carried by any failure.
/// @return the counts, or the first refusal.
[[nodiscard]] Result<CosmeticScanResult>
detectDeviantPixels(const ExecutionContext& execution, SampleType sampleType, const void* source,
                    const ImageLayout& sourceLayout, const CosmeticDetectionOptions& options,
                    const ValidityMaskView<std::uint8_t>* coldMap, const ValidityMaskView<std::uint8_t>* hotMap,
                    bool wantsMaps, std::span<CosmeticChannelThresholds> channelThresholds,
                    const SpatialSelection& selection, const char* operation)
{
    SpatialGeometry geometry{};
    const Status ready = spatial_detail::checkSpatialImage(execution, sampleType, source, sourceLayout,
                                                           SpatialOutputPolicy{}, selection, operation, geometry);
    if (ready.failed())
    {
        return ready;
    }
    if (!options.coldSigma.has_value() && !options.hotSigma.has_value())
    {
        return spatialFailure(ErrorCode::InvalidArgument, operation,
                              "a detection with neither polarity enabled is a request to find nothing; state a cold "
                              "sigma, a hot sigma, or both");
    }
    for (const std::optional<double>& sigma : {options.coldSigma, options.hotSigma})
    {
        if (sigma.has_value() && (!std::isfinite(*sigma) || *sigma < 0.0))
        {
            return spatialFailure(ErrorCode::InvalidArgument, operation,
                                  "a detection sigma is a finite non-negative number of deviations from the median");
        }
    }
    if (!channelThresholds.empty() && channelThresholds.size() < geometry.channelCount)
    {
        return spatialLimitFailure(ErrorCode::IncompatibleArguments, operation,
                                   "the threshold span holds fewer records than the image holds channels",
                                   channelThresholds.size(), geometry.channelCount);
    }
    if (wantsMaps &&
        ((coldMap != nullptr) != options.coldSigma.has_value() || (hotMap != nullptr) != options.hotSigma.has_value()))
    {
        return spatialFailure(ErrorCode::IncompatibleArguments, operation,
                              "each enabled polarity needs its map and a disabled one must not have one; a map "
                              "nothing will write is a caller expecting bits this call does not produce");
    }
    for (const ValidityMaskView<std::uint8_t>* map : {coldMap, hotMap})
    {
        if (map != nullptr && !mapCoversImage(*map, geometry))
        {
            return spatialFailure(ErrorCode::IncompatibleArguments, operation,
                                  "a deviant map is one bit-plane per channel in the image's own extent");
        }
    }

    CosmeticScanResult answer{};
    answer.channelCount = geometry.channelCount;
    PolarityState cold{};
    cold.enabled = options.coldSigma.has_value();
    PolarityState hot{};
    hot.enabled = options.hotSigma.has_value();

    for (std::uint32_t channel = 0u; channel < geometry.channelCount; ++channel)
    {
        const KernelValidityView validity = channelValidity(selection, channel);
        const KernelPlaneSource channelSource{.sampleType = sampleType,
                                              .samples = source,
                                              .layout = &sourceLayout,
                                              .plane = channel / sourceLayout.samplesPerPixel(),
                                              .sampleInPixel = channel % sourceLayout.samplesPerPixel()};
        CosmeticChannelThresholds* record =
            channelThresholds.empty() ? nullptr : &channelThresholds[static_cast<std::size_t>(channel)];
        const Status derived =
            deriveChannelThresholds(execution, channelSource, validity, options, cold, hot, record, answer);
        if (derived.failed())
        {
            return derived;
        }

        const Status checked = plane_detail::checkPlaneSource(execution, channelSource, validity, operation);
        if (checked.failed())
        {
            return checked;
        }
        std::atomic<std::uint64_t> channelCold{0u};
        std::atomic<std::uint64_t> channelHot{0u};
        Status scanned = Status::success();
        const bool known = dispatchSampleType(sampleType, [&]<typename SampleT>() {
            scanned = scanChannel<SampleT>(execution, channelSource, validity, cold, hot, coldMap, hotMap, channel,
                                           channelCold, channelHot);
        });
        if (!known)
        {
            return spatialFailure(ErrorCode::InvalidArgument, operation,
                                  "the representation is not one of the eight this engine stores");
        }
        if (scanned.failed())
        {
            return scanned;
        }
        if (record != nullptr)
        {
            record->coldCount = channelCold.load(std::memory_order_relaxed);
            record->hotCount = channelHot.load(std::memory_order_relaxed);
        }
        answer.plannedWorkers = (std::max)(answer.plannedWorkers, plannedWorkerCount(execution, geometry.height,
                                                                                     detectionRowCost(geometry.width)));
    }

    answer.coldCount = cold.marked.load(std::memory_order_relaxed);
    answer.hotCount = hot.marked.load(std::memory_order_relaxed);
    answer.run.outputsWritten = coldMap != nullptr || hotMap != nullptr ? answer.coldCount + answer.hotCount : 0u;
    answer.precision = ComputePrecision::Float32;
    return answer;
}

/// Gathers one marked sample's surviving window and produces the requested rank.
///
/// @param input the channel's working plane.
/// @param validity which samples participate.
/// @param x the marked column.
/// @param y the marked row.
/// @param reach how far the window extends in pixels.
/// @param step the distance between sampled sites.
/// @param interpolation which rule resolves a rank between two samples.
/// @param window the per-worker gather buffer, holding at least the window's samples.
/// @param replacement the produced rank, written only on a survivor.
/// @param poisoned set when a surviving sample is not finite, which refuses the operation.
/// @return true when at least one window sample survived.
[[nodiscard]] bool rankWindow(PlaneView<const float> input, const KernelValidityView& validity, std::uint32_t x,
                              std::uint32_t y, std::uint32_t reach, std::uint32_t step,
                              QuantileInterpolation interpolation, std::span<float> window, float& replacement,
                              bool& poisoned) noexcept
{
    const std::uint32_t width = input.width();
    const std::uint32_t height = input.height();
    const std::uint32_t firstY = y >= reach ? y - reach : (reach - y) % step == 0u ? 0u : step - (reach - y) % step;
    const std::uint32_t firstX = x >= reach ? x - reach : (reach - x) % step == 0u ? 0u : step - (reach - x) % step;
    const std::uint32_t lastY = (std::min)(y + reach, height - 1u);
    const std::uint32_t lastX = (std::min)(x + reach, width - 1u);
    std::uint32_t held = 0u;
    for (std::uint32_t wy = firstY; wy <= lastY; wy += step)
    {
        const std::span<const float> row = *input.row(wy);
        for (std::uint32_t wx = firstX; wx <= lastX; wx += step)
        {
            if (wx == x && wy == y)
            {
                continue;
            }
            const float value = row[wx];
            if (!sampleIsValidByValue(validity.descriptor, value) || !plane_detail::maskAllows(validity, wx, wy))
            {
                continue;
            }
            if (!sampleIsFinite(value))
            {
                poisoned = true;
                return false;
            }
            window[held] = value;
            ++held;
        }
    }
    if (held == 0u)
    {
        return false;
    }

    const std::uint32_t upper = held / 2u;
    switch (interpolation)
    {
    case QuantileInterpolation::LowerRank: {
        const std::uint32_t lower = (held - 1u) / 2u;
        std::nth_element(window.begin(), window.begin() + lower, window.begin() + held);
        replacement = window[lower];
        return true;
    }
    case QuantileInterpolation::NearestRank:
        // The half-rank of an even window rounds to the higher middle, which for every count is the
        // sample at index `held / 2`.
        std::nth_element(window.begin(), window.begin() + upper, window.begin() + held);
        replacement = window[upper];
        return true;
    case QuantileInterpolation::Linear:
        std::nth_element(window.begin(), window.begin() + upper, window.begin() + held);
        if ((held & 1u) != 0u)
        {
            replacement = window[upper];
            return true;
        }
        replacement = static_cast<float>(
            (static_cast<double>(*std::max_element(window.begin(), window.begin() + upper)) + window[upper]) * 0.5);
        return true;
    }
    poisoned = true;
    return false;
}

/// What the repair pass replaced and carried, summed over every channel and every parallel chunk.
struct RepairCounters
{
    /// Marked cold samples replaced.
    std::atomic<std::uint64_t> coldReplaced{0u};

    /// Marked hot samples replaced.
    std::atomic<std::uint64_t> hotReplaced{0u};

    /// Marked samples whose window kept nothing.
    std::atomic<std::uint64_t> unresolved{0u};

    /// Destination samples written, which is every centre the validity rule accepted.
    std::atomic<std::uint64_t> written{0u};
};

/// Copies one channel and repairs its marked samples.
///
/// @param execution the operation policy and engine-context lease.
/// @param input the channel's source plane.
/// @param output the channel's destination plane, disjoint from the input.
/// @param validity which samples participate.
/// @param coldRowOf the channel's cold map rows, or null.
/// @param hotRowOf the channel's hot map rows, or null.
/// @param request the radii, spacing and rank rule.
/// @param counters what was replaced, added to.
/// @return success, or an execution, data or cancellation failure.
template <typename ColdRowOfT, typename HotRowOfT>
[[nodiscard]] Status repairChannelPlane(const ExecutionContext& execution, PlaneView<const float> input,
                                        PlaneView<float> output, const KernelValidityView& validity,
                                        const ColdRowOfT& coldRowOf, const HotRowOfT& hotRowOf,
                                        const CosmeticCorrectionRequest& request, RepairCounters& counters)
{
    const std::uint32_t width = input.width();
    const std::uint32_t step = request.spacing == CosmeticSiteSpacing::SameCfaFilter ? 2u : 1u;
    const bool gated = validity.mask != nullptr || validity.descriptor.rule() != ValidityRule::AllValid;
    const std::uint64_t rowBytes = packedMaskRowBytes(width);
    return parallelFor(execution, input.height(), repairRowCost(width), [&](TaskRange range) {
        std::array<float, kMaxOrderWindowSamples> window;
        std::uint64_t coldDone = 0u;
        std::uint64_t hotDone = 0u;
        std::uint64_t skipped = 0u;
        std::uint64_t carried = 0u;
        for (std::uint64_t index = range.begin; index < range.end; ++index)
        {
            const auto y = static_cast<std::uint32_t>(index);
            const std::span<const float> from = *input.row(y);
            const std::span<float> to = *output.row(y);
            if (!gated)
            {
                std::copy(from.begin(), from.end(), to.begin());
                carried += width;
            }
            else
            {
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    if (sampleIsValidByValue(validity.descriptor, from[x]) && plane_detail::maskAllows(validity, x, y))
                    {
                        to[x] = from[x];
                        ++carried;
                    }
                }
            }

            const std::uint8_t* const coldRow = coldRowOf(y);
            const std::uint8_t* const hotRow = hotRowOf(y);
            for (std::uint64_t byteIndex = 0u; byteIndex < rowBytes; ++byteIndex)
            {
                const std::uint8_t coldBits = coldRow != nullptr ? coldRow[byteIndex] : std::uint8_t{0u};
                const std::uint8_t hotBits = hotRow != nullptr ? hotRow[byteIndex] : std::uint8_t{0u};
                std::uint32_t marked = coldBits | hotBits;
                if (byteIndex == rowBytes - 1u)
                {
                    // A caller's map is contracted to hold its row padding at zero, but reading
                    // past the row on a broken one would turn that caller mistake into an
                    // out-of-bounds read here, so the padding bits are dropped rather than trusted.
                    marked &= packedMaskLastByteMask(width);
                }
                while (marked != 0u)
                {
                    const auto bit = static_cast<std::uint32_t>(std::countr_zero(marked));
                    marked &= marked - 1u;
                    const auto x = static_cast<std::uint32_t>(byteIndex * 8u + bit);
                    if (!sampleIsValidByValue(validity.descriptor, from[x]) ||
                        !plane_detail::maskAllows(validity, x, y))
                    {
                        continue;
                    }
                    // A sample marked by both maps repairs as cold, whose window is the wider one.
                    const bool asCold = (coldBits & (1u << bit)) != 0u;
                    const std::uint32_t radius = asCold ? request.coldRadius : request.hotRadius;
                    float replacement = 0.0f;
                    bool poisoned = false;
                    if (rankWindow(input, validity, x, y, radius * step, step, request.interpolation,
                                   std::span<float>{window}, replacement, poisoned))
                    {
                        to[x] = replacement;
                        (asCold ? coldDone : hotDone) += 1u;
                    }
                    else if (poisoned)
                    {
                        return spatialFailure(
                            ErrorCode::InvalidData, kCorrectOperation,
                            "a replacement has no defined rank over a sample that is not a number; state a "
                            "validity rule that rejects it");
                    }
                    else
                    {
                        ++skipped;
                    }
                }
            }
        }
        counters.coldReplaced.fetch_add(coldDone, std::memory_order_relaxed);
        counters.hotReplaced.fetch_add(hotDone, std::memory_order_relaxed);
        counters.unresolved.fetch_add(skipped, std::memory_order_relaxed);
        counters.written.fetch_add(carried, std::memory_order_relaxed);
        return Status::success();
    });
}

} // namespace

const char* cosmeticSiteSpacingName(CosmeticSiteSpacing spacing) noexcept
{
    switch (spacing)
    {
    case CosmeticSiteSpacing::EveryPixel:
        return "EveryPixel";
    case CosmeticSiteSpacing::SameCfaFilter:
        return "SameCfaFilter";
    }
    return "UnknownCosmeticSiteSpacing";
}

Result<CosmeticScanResult> scanImageDeviantPixels(const ExecutionContext& execution, SampleType sampleType,
                                                  const void* source, const ImageLayout& sourceLayout,
                                                  const CosmeticDetectionOptions& options,
                                                  std::span<CosmeticChannelThresholds> channelThresholds,
                                                  const SpatialSelection& selection)
{
    return detectDeviantPixels(execution, sampleType, source, sourceLayout, options, nullptr, nullptr, false,
                               channelThresholds, selection, kScanOperation);
}

Result<CosmeticScanResult>
mapImageDeviantPixels(const ExecutionContext& execution, SampleType sampleType, const void* source,
                      const ImageLayout& sourceLayout, const CosmeticDetectionOptions& options,
                      const ValidityMaskView<std::uint8_t>* coldMap, const ValidityMaskView<std::uint8_t>* hotMap,
                      std::span<CosmeticChannelThresholds> channelThresholds, const SpatialSelection& selection)
{
    if (coldMap == nullptr && hotMap == nullptr)
    {
        return spatialFailure(ErrorCode::IncompatibleArguments, kMapOperation,
                              "a detection that records nothing is the counting scan; ask that entry point for it");
    }
    return detectDeviantPixels(execution, sampleType, source, sourceLayout, options, coldMap, hotMap, true,
                               channelThresholds, selection, kMapOperation);
}

Result<CosmeticCorrectionResult> correctImageDeviantPixels(const ExecutionContext& execution, SampleType sampleType,
                                                           const void* source, const ImageLayout& sourceLayout,
                                                           void* destination, const ImageLayout& destinationLayout,
                                                           const ValidityMaskView<const std::uint8_t>* coldMap,
                                                           const ValidityMaskView<const std::uint8_t>* hotMap,
                                                           const CosmeticCorrectionRequest& request,
                                                           const SpatialSelection& selection)
{
    SpatialGeometry geometry{};
    const Status ready =
        spatial_detail::checkSpatialImages(execution, sampleType, source, sourceLayout, destination, destinationLayout,
                                           request.output, selection, kCorrectOperation, geometry);
    if (ready.failed())
    {
        return ready;
    }
    if (coldMap == nullptr && hotMap == nullptr)
    {
        return spatialFailure(ErrorCode::IncompatibleArguments, kCorrectOperation,
                              "a correction with no map marks nothing and would only copy; hand it what a detection "
                              "produced");
    }
    if (!isCosmeticSiteSpacing(request.spacing))
    {
        return spatialFailure(ErrorCode::InvalidArgument, kCorrectOperation,
                              "the site spacing is not one this build implements");
    }
    if (!isQuantileInterpolation(request.interpolation))
    {
        return spatialFailure(ErrorCode::InvalidArgument, kCorrectOperation,
                              "the interpolation rule is not one this build implements");
    }
    if (request.hotRadius == 0u || request.coldRadius == 0u)
    {
        return spatialFailure(ErrorCode::InvalidArgument, kCorrectOperation,
                              "a replacement window of radius zero holds no neighbour to rank");
    }
    const std::uint32_t step = request.spacing == CosmeticSiteSpacing::SameCfaFilter ? 2u : 1u;
    const std::uint32_t widestRadius = (std::max)(request.hotRadius, request.coldRadius);
    if (static_cast<std::uint64_t>(widestRadius) * step > kMaxStencilRadius)
    {
        return spatialLimitFailure(ErrorCode::OutOfRange, kCorrectOperation,
                                   "a window reaches further than one filter may",
                                   static_cast<std::uint64_t>(widestRadius) * step, kMaxStencilRadius);
    }
    const std::uint64_t widestSide = 2u * static_cast<std::uint64_t>(widestRadius) + 1u;
    const std::uint64_t widestSamples = widestSide * widestSide - 1u;
    if (widestSamples > kMaxOrderWindowSamples)
    {
        return spatialLimitFailure(ErrorCode::OutOfRange, kCorrectOperation,
                                   "the window holds more samples than one order filter may rank", widestSamples,
                                   kMaxOrderWindowSamples);
    }
    for (const ValidityMaskView<const std::uint8_t>* map : {coldMap, hotMap})
    {
        if (map != nullptr && !mapCoversImage(*map, geometry))
        {
            return spatialFailure(ErrorCode::IncompatibleArguments, kCorrectOperation,
                                  "a deviant map is one bit-plane per channel in the image's own extent");
        }
    }

    CosmeticCorrectionResult answer{};
    const std::uint64_t hotSide = 2u * static_cast<std::uint64_t>(request.hotRadius) + 1u;
    const std::uint64_t coldSide = 2u * static_cast<std::uint64_t>(request.coldRadius) + 1u;
    answer.hotWindowSamples = static_cast<std::uint32_t>(hotSide * hotSide - 1u);
    answer.coldWindowSamples = static_cast<std::uint32_t>(coldSide * coldSide - 1u);
    // The reported reach is what a window could actually have: a stated radius whose polarity has
    // no map never reads a sample.
    std::uint32_t appliedRadius = 0u;
    if (hotMap != nullptr)
    {
        appliedRadius = (std::max)(appliedRadius, request.hotRadius);
    }
    if (coldMap != nullptr)
    {
        appliedRadius = (std::max)(appliedRadius, request.coldRadius);
    }
    answer.filter.horizontalRadius = appliedRadius * step;
    answer.filter.verticalRadius = appliedRadius * step;
    answer.filter.precision = ComputePrecision::Float32;

    RepairCounters counters{};
    std::uint32_t channelIndex = 0u;
    const Status repaired = filterEveryChannel(
        execution, sampleType, source, sourceLayout, destination, destinationLayout, selection,
        request.output.writeBack, geometry, kCorrectOperation, answer.filter,
        [&](PlaneView<const float> input, PlaneView<float> output, const KernelValidityView& validity) -> Status {
            const std::uint32_t channel = channelIndex;
            ++channelIndex;
            const auto coldRowOf = [&, channel](std::uint32_t y) -> const std::uint8_t* {
                return coldMap != nullptr ? mapRow(*coldMap, channel, y) : nullptr;
            };
            const auto hotRowOf = [&, channel](std::uint32_t y) -> const std::uint8_t* {
                return hotMap != nullptr ? mapRow(*hotMap, channel, y) : nullptr;
            };
            const Status ran =
                repairChannelPlane(execution, input, output, validity, coldRowOf, hotRowOf, request, counters);
            if (ran.failed())
            {
                return ran;
            }
            answer.filter.imagePasses += 1u;
            answer.filter.plannedWorkers =
                (std::max)(answer.filter.plannedWorkers,
                           plannedWorkerCount(execution, input.height(), repairRowCost(input.width())));
            answer.filter.run.selection.total += static_cast<std::uint64_t>(input.width()) * input.height();
            return Status::success();
        });
    if (repaired.failed())
    {
        return repaired;
    }

    answer.coldReplaced = counters.coldReplaced.load(std::memory_order_relaxed);
    answer.hotReplaced = counters.hotReplaced.load(std::memory_order_relaxed);
    answer.unresolved = counters.unresolved.load(std::memory_order_relaxed);
    // Every accepted centre was written: the marked few as ranks and the rest as themselves.
    answer.filter.run.selection.valid = counters.written.load(std::memory_order_relaxed);
    answer.filter.run.outputsWritten = answer.filter.run.selection.valid;
    return answer;
}

} // namespace an
