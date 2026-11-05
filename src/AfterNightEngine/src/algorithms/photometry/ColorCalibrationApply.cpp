// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the sky estimate, the offsets and the fused gain-and-offset apply that every
// colour calibration ends with.

#include <AfterNightEngine/Algorithms/Photometry/ColorCalibrationApply.h>

#include <AfterNightEngine/Algorithms/Statistics/RobustStatistics.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "algorithms/photometry/PhotometryInternal.h"
#include "kernels/reference/PlaneSourceAccess.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace an
{
namespace
{

using photometry_detail::failure;

constexpr const char* kEstimateOperation = "an::estimateColorBackground";
constexpr const char* kApplyOperation = "an::applyColorCalibration";
constexpr std::uint32_t kColorChannelCount = 3u;

/// Per-worker counts of what one pass saw, summed in slot order afterwards.
struct SlotCounters
{
    /// Samples the validity rule rejected.
    std::uint64_t copied = 0u;

    /// Samples whose computed value left the interval.
    std::uint64_t outside = 0u;
};

/// The row and channel one item of the apply loop names.
struct RowItem
{
    /// Which channel.
    std::uint32_t channel = 0u;

    /// Which row.
    std::uint32_t y = 0u;
};

/// Splits one loop item into its channel and row.
///
/// Channel-major, so the items of one channel are contiguous and a worker walks one plane at a time.
///
/// @param item the item index.
/// @param height the frame height.
/// @return the channel and row.
[[nodiscard]] RowItem rowItemOf(const std::uint64_t item, const std::uint32_t height) noexcept
{
    return RowItem{.channel = static_cast<std::uint32_t>(item / height),
                   .y = static_cast<std::uint32_t>(item % height)};
}

/// Whether the frame is one this operation writes.
///
/// @param frame the checked frame.
/// @param operation the entry point speaking.
/// @return success, or the refusal.
[[nodiscard]] Status checkColorFrame(const StarFrameView& frame, const char* const operation)
{
    if (!frame.valid())
    {
        return failure(ErrorCode::InvalidArgument, "a colour calibration needs a checked frame", operation);
    }
    if (!photometry_detail::isRgbFrame(frame))
    {
        return failure(ErrorCode::Unsupported, "a colour calibration needs a three-channel RGB frame", operation);
    }
    return Status::success();
}

/// Whether every field of an apply request is inside its stated range.
///
/// @param request the request.
/// @return success, or the first field that refused.
[[nodiscard]] Status validateApplyRequest(const ColorCalibrationApplyRequest& request)
{
    for (std::size_t channel = 0u; channel < kColorChannelCount; ++channel)
    {
        if (!std::isfinite(request.gains[channel]) || request.gains[channel] <= 0.0)
        {
            return failure(ErrorCode::InvalidArgument, "every channel gain is finite and positive", kApplyOperation);
        }
        if (!std::isfinite(request.offsets[channel]))
        {
            return failure(ErrorCode::InvalidArgument, "every channel offset is finite", kApplyOperation);
        }
    }
    if (!isColorCalibrationClipping(request.clipping))
    {
        return failure(ErrorCode::InvalidArgument, "the clipping policy is not one this build implements",
                       kApplyOperation);
    }
    return Status::success();
}

/// Writes one unit-stride row of admitted samples as `sample * gain + offset`, widened and rounded once, and
/// says whether any written sample reached an end of the interval.
///
/// The bound and the coefficients are values, so the compiler keeps them in registers. Read through a
/// capture they are memory that any store to the row might move, as far as a compiler without type-based
/// alias analysis can tell, and it then keeps the row scalar. Which way of looking for the ends is faster
/// depends on the compiler, and each asks the same question of the same written values. Microsoft's checks
/// the row just stored, which is still in the first level of cache; its row that looked as it wrote took
/// half as long again. Clang's looks at each value as it writes it, which on bench-linux takes a sixth
/// less than a plain row, where a second pass over the stored row takes a seventh more.
///
/// @tparam SampleT the frame's floating representation.
/// @param source the row's samples.
/// @param target where the row is written; disjoint from `source`.
/// @param width how many samples.
/// @param gain the channel's gain.
/// @param offset the channel's offset.
/// @param lowEnd the interval's lower endpoint in the written representation.
/// @param highEnd the interval's upper endpoint in the written representation.
/// @return true when a written sample lies at or beyond an end of the interval.
template <typename SampleT>
[[nodiscard]] bool writeAffineRow(const SampleT* const source, SampleT* const target, const std::uint32_t width,
                                  const double gain, const double offset, const SampleT lowEnd,
                                  const SampleT highEnd) noexcept
{
    // As wide as a sample, so each comparison lane lands in a flag lane of its own width.
    using Flags = std::conditional_t<sizeof(SampleT) == sizeof(std::uint64_t), std::uint64_t, std::uint32_t>;
    Flags reached = 0u;
#if defined(_MSC_VER) && !defined(__clang__)
    for (std::uint32_t x = 0u; x < width; ++x)
    {
        target[x] = static_cast<SampleT>(static_cast<double>(source[x]) * gain + offset);
    }
    for (std::uint32_t x = 0u; x < width; ++x)
    {
        reached |= static_cast<Flags>(target[x] <= lowEnd) | static_cast<Flags>(target[x] >= highEnd);
    }
#else
    for (std::uint32_t x = 0u; x < width; ++x)
    {
        const SampleT written = static_cast<SampleT>(static_cast<double>(source[x]) * gain + offset);
        target[x] = written;
        reached |= static_cast<Flags>(written <= lowEnd) | static_cast<Flags>(written >= highEnd);
    }
#endif
    return reached != 0u;
}

/// Counts the samples of one unit-stride row of admitted samples whose widened value leaves the interval
/// and, when writing, writes each held between the write bounds.
///
/// Its bound and coefficients are values for the reason `writeAffineRow` gives.
///
/// @tparam SampleT the frame's floating representation.
/// @tparam WriteT whether the row is written.
/// @param source the row's samples.
/// @param target where the row is written, which may be `source` itself; unused when not writing.
/// @param width how many samples.
/// @param gain the channel's gain.
/// @param offset the channel's offset.
/// @param low the interval's lower endpoint.
/// @param high the interval's upper endpoint.
/// @param writeLow the lowest value written.
/// @param writeHigh the highest value written.
/// @return how many samples left the interval.
template <typename SampleT, bool WriteT>
[[nodiscard]] std::uint64_t exactAffineRow(const SampleT* const source, SampleT* const target,
                                           const std::uint32_t width, const double gain, const double offset,
                                           const double low, const double high, const double writeLow,
                                           const double writeHigh) noexcept
{
    std::uint64_t outside = 0u;
    for (std::uint32_t x = 0u; x < width; ++x)
    {
        const double value = static_cast<double>(source[x]) * gain + offset;
        outside += static_cast<std::uint64_t>(value < low) + static_cast<std::uint64_t>(value > high);
        if constexpr (WriteT)
        {
            const double held = value < writeLow ? writeLow : (value > writeHigh ? writeHigh : value);
            target[x] = static_cast<SampleT>(held);
        }
    }
    return outside;
}

/// One pass over every channel row: counting only, or counting and writing.
///
/// @tparam SampleT the frame's floating representation.
/// @tparam WriteT whether the pass writes the destination.
/// @param execution the operation.
/// @param frame the source.
/// @param destination where calibrated samples go, or unused when not writing.
/// @param request the gains, offsets and policy.
/// @param low the interval's lower endpoint.
/// @param high the interval's upper endpoint.
/// @param counters one record per worker slot.
/// @return success, or the loop's failure.
template <typename SampleT, bool WriteT>
[[nodiscard]] Status applyPass(const ExecutionContext& execution, const StarFrameView& frame, void* const destination,
                               const ColorCalibrationApplyRequest& request, const double low, const double high,
                               SlotCounters* const counters)
{
    const ImageLayout& layout = frame.layout();
    const std::uint32_t width = layout.extent().width();
    const std::uint32_t height = layout.extent().height();
    const bool clamp = request.clipping == ColorCalibrationClipping::ClampToPixelRange;
    // The bounds the written value is held between: the interval under the clamping policy, and
    // nothing under the others, so one branch-free row serves every policy while the interval
    // itself still counts the samples that left it.
    const double writeLow = clamp ? low : -std::numeric_limits<double>::infinity();
    const double writeHigh = clamp ? high : std::numeric_limits<double>::infinity();
    const KernelValidityView& validity = frame.validity();
    // A frame whose rule admits every sample and carries no mask needs no admission call per
    // sample; that call is what keeps the row from vectorizing, and it is the common case.
    const bool everyAdmitted = validity.descriptor.rule() == ValidityRule::AllValid && validity.mask == nullptr;
    const std::uint64_t items = static_cast<std::uint64_t>(kColorChannelCount) * height;
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * sizeof(SampleT) * 2u,
                        .operationsPerItem = static_cast<std::uint64_t>(width) * 4u};

    const auto body = [&](const WorkerTask task) -> Status {
        SlotCounters& slot = counters[task.workerSlot];
        for (std::uint64_t item = task.range.begin; item < task.range.end; ++item)
        {
            const RowItem where = rowItemOf(item, height);
            const photometry_detail::ChannelReader<SampleT> reader(frame, where.channel);
            const SampleT* const source = reader.row(where.y);
            const std::uint32_t stride = reader.pixelStride();
            const double gain = request.gains[where.channel];
            const double offset = request.offsets[where.channel];
            SampleT* target = nullptr;
            if constexpr (WriteT)
            {
                const std::uint64_t rowOffset =
                    reinterpret_cast<const std::uint8_t*>(source) - static_cast<const std::uint8_t*>(frame.samples());
                target = reinterpret_cast<SampleT*>(static_cast<std::uint8_t*>(destination) + rowOffset);
            }
            if (everyAdmitted)
            {
                std::uint64_t outside = 0u;
                if (stride == 1u)
                {
                    // Unit stride is spelled as such: the compiler vectorizes these rows and does not
                    // vectorize the same bodies indexed through a runtime stride.
                    // A speculative write may only be reread from a disjoint source. In-place rows take
                    // the single exact pass, before any input is overwritten. The plain row first: the
                    // affine value, written once. A row whose written samples all lie
                    // strictly inside the interval needs neither a count nor a clamp, and that is
                    // decided on the written representation: rounding to nearest is monotone, so a
                    // value that left the interval lands at or beyond its end. Only a row that touched
                    // an end takes the exact counting pass.
                    if (WriteT && source != target &&
                        !writeAffineRow(source, target, width, gain, offset, static_cast<SampleT>(low),
                                        static_cast<SampleT>(high)))
                    {
                        continue;
                    }
                    outside = exactAffineRow<SampleT, WriteT>(source, target, width, gain, offset, low, high, writeLow,
                                                              writeHigh);
                }
                else
                {
                    for (std::uint32_t x = 0u; x < width; ++x)
                    {
                        const std::size_t at = static_cast<std::size_t>(x) * stride;
                        const double value = static_cast<double>(source[at]) * gain + offset;
                        outside += static_cast<std::uint64_t>(value < low) + static_cast<std::uint64_t>(value > high);
                        if constexpr (WriteT)
                        {
                            const double held = value < writeLow ? writeLow : (value > writeHigh ? writeHigh : value);
                            target[at] = static_cast<SampleT>(held);
                        }
                    }
                }
                slot.outside += outside;
                continue;
            }
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                const std::size_t at = static_cast<std::size_t>(x) * stride;
                const SampleT sample = source[at];
                if (!reader.admits(x, where.y, sample))
                {
                    ++slot.copied;
                    if constexpr (WriteT)
                    {
                        target[at] = sample;
                    }
                    continue;
                }
                double value = static_cast<double>(sample) * gain + offset;
                if (value < low || value > high)
                {
                    ++slot.outside;
                    if (clamp)
                    {
                        value = std::clamp(value, low, high);
                    }
                }
                if constexpr (WriteT)
                {
                    SampleT written = static_cast<SampleT>(value);
                    if (frame.validity().descriptor.rule() == ValidityRule::ZeroIsNoData && written == SampleT{0})
                    {
                        written = std::copysign((std::numeric_limits<SampleT>::min)(), written);
                    }
                    target[at] = written;
                }
            }
        }
        return Status::success();
    };
    return parallelForWorkers(execution, items, cost, body);
}

/// Sums the per-slot counters in slot order.
///
/// @param counters the records.
/// @param slots how many.
/// @return the total.
[[nodiscard]] SlotCounters sumCounters(const SlotCounters* const counters, const std::uint32_t slots) noexcept
{
    SlotCounters total{};
    for (std::uint32_t slot = 0u; slot < slots; ++slot)
    {
        total.copied += counters[slot].copied;
        total.outside += counters[slot].outside;
    }
    return total;
}

} // namespace

const char* colorCalibrationBackgroundModeName(const ColorCalibrationBackgroundMode mode) noexcept
{
    switch (mode)
    {
    case ColorCalibrationBackgroundMode::SharedPedestal:
        return "SharedPedestal";
    case ColorCalibrationBackgroundMode::SkyToZero:
        return "SkyToZero";
    }
    return "UnknownColorCalibrationBackgroundMode";
}

const char* colorCalibrationClippingName(const ColorCalibrationClipping clipping) noexcept
{
    switch (clipping)
    {
    case ColorCalibrationClipping::ClampToPixelRange:
        return "ClampToPixelRange";
    case ColorCalibrationClipping::PreserveSignedFloat:
        return "PreserveSignedFloat";
    case ColorCalibrationClipping::RejectOnClip:
        return "RejectOnClip";
    }
    return "UnknownColorCalibrationClipping";
}

Result<ColorBackgroundEstimate> estimateColorBackground(const ExecutionContext& execution, const StarFrameView& frame,
                                                        const std::optional<StatisticsRegion>& region)
{
    if (!execution.valid())
    {
        return failure(ErrorCode::NotInitialized, "a background estimate needs an execution context",
                       kEstimateOperation);
    }
    if (const Status stopped = execution.checkCancellation(); stopped.failed())
    {
        return stopped;
    }
    if (const Status checked = checkColorFrame(frame, kEstimateOperation); checked.failed())
    {
        return checked;
    }
    if (region.has_value())
    {
        const ImageExtent& extent = frame.layout().extent();
        if (region->width == 0u || region->height == 0u || region->originX >= extent.width() ||
            region->originY >= extent.height() || extent.width() - region->originX < region->width ||
            extent.height() - region->originY < region->height)
        {
            return failure(ErrorCode::OutOfRange, "the background region lies inside the frame", kEstimateOperation);
        }
    }

    ColorBackgroundEstimate estimate{};
    const StatisticsSelection selection{
        .validity = frame.validity().descriptor, .mask = frame.validity().mask, .region = region};
    const RobustStatisticsRequest request{.medianAbsoluteDeviation = true,
                                          .sigmaClipping =
                                              SigmaClippingRequest{.lowSigma = kColorBackgroundLowerSigmas,
                                                                   .highSigma = kColorBackgroundUpperSigmas,
                                                                   .maximumIterations = 1u}};
    for (std::uint32_t channel = 0u; channel < kColorChannelCount; ++channel)
    {
        const Result<RobustStatistics> statistics = computeImageRobustStatistics(
            execution, frame.descriptor().sampleType(), frame.samples(), frame.layout(), channel, selection, request);
        if (!statistics.hasValue())
        {
            return statistics.status();
        }
        estimate.medians[channel] = statistics->median;
        estimate.robustScales[channel] = statistics->robustScale;
        estimate.levels[channel] = statistics->clipped.has_value() ? statistics->clipped->location : statistics->median;
        estimate.retainedSamples[channel] =
            statistics->clipped.has_value() ? statistics->clipped->selection.valid : statistics->selection.valid;
    }
    return estimate;
}

Result<ColorCalibrationApplyResult> applyColorCalibration(const ExecutionContext& execution, const StarFrameView& frame,
                                                          void* const destination, const std::uint64_t destinationBytes,
                                                          const ColorCalibrationApplyRequest& request)
{
    if (!execution.valid())
    {
        return failure(ErrorCode::NotInitialized, "a colour calibration needs an execution context", kApplyOperation);
    }
    if (const Status stopped = execution.checkCancellation(); stopped.failed())
    {
        return stopped;
    }
    if (const Status checked = checkColorFrame(frame, kApplyOperation); checked.failed())
    {
        return checked;
    }
    if (frame.validity().descriptor.rule() == ValidityRule::DomainRange)
    {
        return failure(ErrorCode::Unsupported,
                       "colour calibration needs an explicit mask instead of a value-range validity rule",
                       kApplyOperation);
    }
    const SampleType sampleType = frame.descriptor().sampleType();
    if (sampleType != SampleType::Float32 && sampleType != SampleType::Float64)
    {
        return failure(ErrorCode::Unsupported,
                       "a colour calibration writes a floating frame; an integer frame converts first",
                       kApplyOperation);
    }
    if (const Status valid = validateApplyRequest(request); valid.failed())
    {
        return valid;
    }
    const ImageLayout& layout = frame.layout();
    if (destination == nullptr || destinationBytes < layout.totalBytes())
    {
        return failure(ErrorCode::InvalidArgument, "the destination holds every byte the layout names",
                       kApplyOperation);
    }
    if (reinterpret_cast<std::uintptr_t>(destination) % sampleTypeSizeBytes(sampleType) != 0u)
    {
        return failure(ErrorCode::IncompatibleArguments,
                       "the destination must be aligned for the frame's sample representation", kApplyOperation);
    }
    const auto* const sourceBegin = static_cast<const std::uint8_t*>(frame.samples());
    const auto* const targetBegin = static_cast<const std::uint8_t*>(destination);
    if (sourceBegin != targetBegin)
    {
        const auto* const sourceEnd = sourceBegin + layout.totalBytes();
        const auto* const targetEnd = targetBegin + layout.totalBytes();
        if (sourceBegin < targetEnd && targetBegin < sourceEnd)
        {
            return failure(ErrorCode::Aliasing,
                           "the destination is the source or disjoint from it, never a shifted copy", kApplyOperation);
        }
    }

    ColorCalibrationApplyResult result{};
    result.intervalLow = 0.0;
    result.intervalHigh = frame.descriptor().numericDomain() == NumericDomain::NormalizedLinear
                              ? 1.0
                              : (std::numeric_limits<double>::infinity)();

    const std::uint32_t height = layout.extent().height();
    const std::uint32_t width = layout.extent().width();
    const std::uint64_t items = static_cast<std::uint64_t>(kColorChannelCount) * height;
    const std::uint32_t elementBytes = sampleType == SampleType::Float32 ? 4u : 8u;
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * elementBytes * 2u,
                        .operationsPerItem = static_cast<std::uint64_t>(width) * 4u};
    const std::uint32_t slots = (std::max)(1u, plannedWorkerCount(execution, items, cost));
    Result<plane_detail::ScopedContextMemory> memory = plane_detail::ScopedContextMemory::take(
        execution, static_cast<std::uint64_t>(slots) * sizeof(SlotCounters), alignof(SlotCounters), kApplyOperation);
    if (!memory.hasValue())
    {
        return memory.status();
    }
    SlotCounters* const counters = static_cast<SlotCounters*>(memory->data());

    const auto runPass = [&](const bool write) -> Status {
        std::fill(counters, counters + slots, SlotCounters{});
        if (sampleType == SampleType::Float32)
        {
            return write ? applyPass<float, true>(execution, frame, destination, request, result.intervalLow,
                                                  result.intervalHigh, counters)
                         : applyPass<float, false>(execution, frame, destination, request, result.intervalLow,
                                                   result.intervalHigh, counters);
        }
        return write ? applyPass<double, true>(execution, frame, destination, request, result.intervalLow,
                                               result.intervalHigh, counters)
                     : applyPass<double, false>(execution, frame, destination, request, result.intervalLow,
                                                result.intervalHigh, counters);
    };

    if (request.clipping == ColorCalibrationClipping::RejectOnClip)
    {
        if (const Status counted = runPass(false); counted.failed())
        {
            return counted;
        }
        const SlotCounters total = sumCounters(counters, slots);
        if (total.outside > 0u)
        {
            return Status::failure(
                ErrorCode::InvalidData,
                StatusDetail{.message = "the calibration would move samples outside the representable interval",
                             .operation = kApplyOperation,
                             .domain = ErrorDomain::Algorithms,
                             .context = {StatusContextField::ofCount(ContextKey::Completed, total.outside)}});
        }
    }
    if (const Status written = runPass(true); written.failed())
    {
        return written;
    }
    const SlotCounters total = sumCounters(counters, slots);
    result.samplesWritten = static_cast<std::uint64_t>(kColorChannelCount) * height * width;
    result.samplesCopiedUnchanged = total.copied;
    result.samplesOutsideInterval = total.outside;
    return result;
}

Result<ImageSemantics> colorCalibratedSemantics(const ImageSemantics& input)
{
    ImageSemanticsRequest request = input.toRequest();
    request.processingState.colorCalibrated = true;
    return ImageSemantics::create(request);
}

} // namespace an
