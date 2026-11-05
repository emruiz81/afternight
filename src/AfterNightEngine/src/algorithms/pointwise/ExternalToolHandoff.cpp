// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the samples an image becomes on its way to an external tool, and back.
//
// The hand-off is a composition over one working image of the source's channels: Float64 for a Float64 source,
// Float32 otherwise, so a floating source's range is measured on its own values. The range is reduced from the
// working samples, the mapping into the unit interval is one walk, the statistics and the stretch are the
// published operations of their own domains, and the handed image is written by the shared writer. A pixel the
// source's validity rejects is a quiet NaN in the working image while the statistics and the stretch run - every
// channel of it, so the stretch's per-pixel rule and the statistics' per-sample rule agree - and zero in what the
// tool is handed.
//
// Bringing an answer back needs no working memory: a row is read a run of pixels at a time into doubles, taken
// through the inverse transfer and the range, and written once.

#include <AfterNightEngine/Algorithms/Pointwise/ExternalToolHandoff.h>

#include <AfterNightEngine/Algorithms/Pointwise/Stretch.h>
#include <AfterNightEngine/Algorithms/Statistics/RobustStatistics.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/SampleConversion.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "kernels/reference/CompositionPlanes.h"
#include "kernels/reference/PlaneSourceAccess.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <type_traits>

namespace an
{
namespace
{

using composition_detail::CompositionSource;
using composition_detail::WorkingBlock;
using composition_detail::WorkingPlanes;

constexpr const char* kPlanOperation = "an::planExternalToolHandoff";
constexpr const char* kLayoutOperation = "an::externalToolHandoffLayout";
constexpr const char* kPrepareOperation = "an::prepareExternalToolHandoff";
constexpr const char* kRestoreOperation = "an::restoreExternalToolAnswer";

/// How many pixels of a row a restore reads into its run at once.
constexpr std::uint32_t kRunPixels = 256u;

[[nodiscard]] Status handoffFailure(ErrorCode code, const char* message, const char* operation, const char* parameter)
{
    return Status::failure(code,
                           StatusDetail{.message = message,
                                        .operation = operation,
                                        .domain = ErrorDomain::Algorithms,
                                        .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
}

[[nodiscard]] constexpr bool handoffChannels(std::uint32_t channels) noexcept
{
    return channels == 1u || channels == 3u;
}

[[nodiscard]] constexpr SampleType formatSampleType(ExternalToolFormat format) noexcept
{
    return format == ExternalToolFormat::Float32 ? SampleType::Float32 : SampleType::UInt16;
}

[[nodiscard]] CompositionSource compositionOf(const ExternalToolSource& source) noexcept
{
    return CompositionSource{.descriptor = source.descriptor,
                             .samples = source.samples,
                             .layout = source.layout,
                             .validity = source.validity};
}

[[nodiscard]] Status checkRequest(const ExternalToolHandoffRequest& request, const char* operation)
{
    if (!isExternalToolFormat(request.format) || !isExternalToolPreStretch(request.preStretch))
    {
        return handoffFailure(ErrorCode::InvalidArgument, "a hand-off enumerator is outside its enumeration", operation,
                              "request");
    }
    if (!std::isfinite(request.shadowsClipping) || !std::isfinite(request.targetBackground) ||
        request.targetBackground < 0.0 || request.targetBackground > 1.0)
    {
        return handoffFailure(ErrorCode::InvalidArgument,
                              "the automatic stretch needs a finite clipping and a target within the unit interval",
                              operation, "request");
    }
    return Status::success();
}

[[nodiscard]] bool unitParameter(double value) noexcept
{
    return std::isfinite(value) && value >= 0.0 && value <= 1.0;
}

[[nodiscard]] Status checkRecord(const ExternalToolHandoff& handoff)
{
    if (!std::isfinite(handoff.rangeLow) || !std::isfinite(handoff.rangeHigh) || handoff.rangeLow > 0.0 ||
        handoff.rangeHigh < 1.0)
    {
        return handoffFailure(ErrorCode::InvalidArgument,
                              "the record's range is not finite and does not hold the unit interval", kRestoreOperation,
                              "handoff");
    }
    for (const MidtonesTransferParameters& transfer : handoff.transfer)
    {
        if (!unitParameter(transfer.midtones) || !unitParameter(transfer.shadows) ||
            !unitParameter(transfer.highlights) || transfer.highlights < transfer.shadows)
        {
            return handoffFailure(ErrorCode::InvalidArgument,
                                  "a transfer of the record is not finite, within the unit interval and ordered",
                                  kRestoreOperation, "handoff");
        }
    }
    if (!handoffChannels(handoff.channels) || !isSampleType(handoff.sourceSampleType) || handoff.width == 0u ||
        handoff.height == 0u)
    {
        return handoffFailure(ErrorCode::InvalidArgument, "the record does not describe a one- or three-channel source",
                              kRestoreOperation, "handoff");
    }
    return Status::success();
}

[[nodiscard]] Status checkExecution(const ExecutionContext& execution, const char* operation)
{
    if (!execution.valid())
    {
        return handoffFailure(ErrorCode::NotInitialized, "a hand-off was given an empty execution context", operation,
                              "execution");
    }
    const Status cancelled = execution.checkCancellation();
    if (cancelled.failed())
    {
        return cancelled;
    }
    const Result<BackendSelection> backend = selectBackend(execution);
    return backend.hasValue() ? Status::success() : backend.status();
}

/// Atomically lowers or raises a shared bound to a value.
void lowerTo(std::atomic<double>& bound, double value) noexcept
{
    double current = bound.load(std::memory_order_relaxed);
    while (value < current && !bound.compare_exchange_weak(current, value, std::memory_order_relaxed))
    {
    }
}

void raiseTo(std::atomic<double>& bound, double value) noexcept
{
    double current = bound.load(std::memory_order_relaxed);
    while (value > current && !bound.compare_exchange_weak(current, value, std::memory_order_relaxed))
    {
    }
}

/// The unit interval widened to the smallest and largest working sample. An invalid sample is zero in the
/// working image, inside the unit interval, so it cannot widen the range.
template <typename WorkingT>
[[nodiscard]] Status measureRange(const ExecutionContext& execution, const WorkingPlanes& working, double& low,
                                  double& high)
{
    std::atomic<double> lowest{0.0};
    std::atomic<double> highest{1.0};
    const std::uint32_t width = working.layout->extent().width();
    const std::uint32_t channels = working.channels();
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * channels * sizeof(WorkingT),
                        .operationsPerItem = static_cast<std::uint64_t>(width) * channels};
    const Status ran = parallelFor(execution, working.layout->extent().height(), cost, [&](TaskRange range) -> Status {
        double rangeLow = 0.0;
        double rangeHigh = 1.0;
        for (std::uint64_t y = range.begin; y < range.end; ++y)
        {
            for (std::uint32_t channel = 0u; channel < channels; ++channel)
            {
                const WorkingT* const row = working.row<WorkingT>(channel, static_cast<std::uint32_t>(y));
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    rangeLow = (std::min)(rangeLow, static_cast<double>(row[x]));
                    rangeHigh = (std::max)(rangeHigh, static_cast<double>(row[x]));
                }
            }
        }
        lowerTo(lowest, rangeLow);
        raiseTo(highest, rangeHigh);
        return Status::success();
    });
    if (ran.failed())
    {
        return ran;
    }
    low = lowest.load(std::memory_order_relaxed);
    high = highest.load(std::memory_order_relaxed);
    return Status::success();
}

/// Maps every valid working sample from the range into the unit interval, in double, and sets every channel of an
/// invalid pixel to `invalidValue`.
template <typename WorkingT>
[[nodiscard]] Status mapIntoUnitInterval(const ExecutionContext& execution, const WorkingPlanes& working,
                                         const std::uint8_t* valid, double low, double high, WorkingT invalidValue)
{
    const bool mapped = low != 0.0 || high != 1.0;
    if (!mapped && valid == nullptr)
    {
        return Status::success();
    }
    const double span = high - low;
    const std::uint32_t width = working.layout->extent().width();
    const std::uint32_t channels = working.channels();
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * channels * 2u * sizeof(WorkingT),
                        .operationsPerItem = static_cast<std::uint64_t>(width) * channels * 2u};
    return parallelFor(execution, working.layout->extent().height(), cost, [&](TaskRange range) -> Status {
        for (std::uint64_t y = range.begin; y < range.end; ++y)
        {
            const std::uint8_t* const rowValid = valid != nullptr ? valid + y * width : nullptr;
            for (std::uint32_t channel = 0u; channel < channels; ++channel)
            {
                WorkingT* const row = working.row<WorkingT>(channel, static_cast<std::uint32_t>(y));
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    if (rowValid != nullptr && rowValid[x] == 0u)
                    {
                        row[x] = invalidValue;
                    }
                    else if (mapped)
                    {
                        row[x] = static_cast<WorkingT>((static_cast<double>(row[x]) - low) / span);
                    }
                }
            }
        }
        return Status::success();
    });
}

/// Replaces every not-a-number of the working image with zero.
template <typename WorkingT>
[[nodiscard]] Status zeroInvalid(const ExecutionContext& execution, const WorkingPlanes& working)
{
    const std::uint32_t width = working.layout->extent().width();
    const std::uint32_t channels = working.channels();
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * channels * sizeof(WorkingT),
                        .operationsPerItem = static_cast<std::uint64_t>(width) * channels};
    return parallelFor(execution, working.layout->extent().height(), cost, [&](TaskRange range) -> Status {
        for (std::uint64_t y = range.begin; y < range.end; ++y)
        {
            for (std::uint32_t channel = 0u; channel < channels; ++channel)
            {
                WorkingT* const row = working.row<WorkingT>(channel, static_cast<std::uint32_t>(y));
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    if (std::isnan(row[x]))
                    {
                        row[x] = WorkingT{0};
                    }
                }
            }
        }
        return Status::success();
    });
}

[[nodiscard]] ValidityDescriptor finiteRule() noexcept
{
    return ValidityDescriptor::create(ValidityDescriptorRequest{.rule = ValidityRule::FiniteOnly})
        .value_or(ValidityDescriptor{});
}

/// Each channel's automatic transfer, from the mapped working image's exact statistics.
[[nodiscard]] Result<std::array<MidtonesTransferParameters, 3u>>
deriveTransfers(const ExecutionContext& execution, const ExternalToolHandoffRequest& request,
                const WorkingPlanes& working, const ValidityDescriptor& rule)
{
    const std::uint32_t channels = working.channels();
    std::array<StretchChannelStatistics, 3u> statistics{};
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        const Result<RobustStatistics> measured = computeImageRobustStatistics(
            execution, working.sampleType, working.samples, *working.layout, channel,
            StatisticsSelection{.validity = rule}, RobustStatisticsRequest{.medianAbsoluteDeviation = true});
        if (!measured.hasValue())
        {
            return measured.status();
        }
        statistics[channel] = StretchChannelStatistics{.median = measured->median,
                                                       .medianAbsoluteDeviation = measured->medianAbsoluteDeviation};
    }
    const std::span<const StretchChannelStatistics> taken(statistics.data(), channels);
    std::array<MidtonesTransferParameters, 3u> transfer{};
    if (request.preStretch == ExternalToolPreStretch::Linked)
    {
        const Result<MidtonesTransferParameters> linked =
            deriveLinkedAutoStretch(taken, request.shadowsClipping, request.targetBackground);
        if (!linked.hasValue())
        {
            return linked.status();
        }
        transfer.fill(*linked);
        return transfer;
    }
    const Status unlinked = deriveUnlinkedAutoStretch(taken, request.shadowsClipping, request.targetBackground,
                                                      std::span<MidtonesTransferParameters>(transfer.data(), channels));
    if (unlinked.failed())
    {
        return unlinked;
    }
    for (std::uint32_t channel = channels; channel < 3u; ++channel)
    {
        transfer[channel] = transfer[channels - 1u];
    }
    return transfer;
}

/// The inverse of a midtones transfer: the complementary balance between the same endpoints, an output at or
/// below zero answering the shadow point and one at or above one the highlight point. Both terms of the
/// denominator are non-negative and one of them is positive, so nothing cancels and nothing divides by zero.
[[nodiscard]] double inverseTransfer(double value, const MidtonesTransferParameters& transfer) noexcept
{
    if (!(value > 0.0))
    {
        return transfer.shadows;
    }
    if (value >= 1.0)
    {
        return transfer.highlights;
    }
    const double balance = transfer.midtones;
    const double numerator = balance * value;
    const double denominator = (1.0 - balance) * (1.0 - value) + numerator;
    const double fraction = denominator > 0.0 ? numerator / denominator : value;
    return transfer.shadows + (transfer.highlights - transfer.shadows) * fraction;
}

/// One run of one channel of a row of a source, as fractions of full scale; a sample the source's validity rejects
/// reads as zero.
void readRun(const ExternalToolSource& source, std::uint32_t channel, std::uint32_t y, std::uint32_t x0,
             std::uint32_t count, bool ruled, bool ownPlanes, double* values) noexcept
{
    const ImageLayout& layout = *source.layout;
    const std::uint32_t stride = layout.samplesPerPixel();
    const std::uint8_t* const row =
        static_cast<const std::uint8_t*>(source.samples) + plane_detail::channelRowByteOffset(layout, channel, y);
    const KernelValidityView& validity = source.validity;
    const std::uint32_t maskPlane = ownPlanes ? channel : validity.maskPlane;
    const ConversionPolicy policy{.domain = DomainMapping::FullScale};
    // A checked descriptor names one of the eight representations, so the dispatch always runs.
    (void)dispatchSampleType(source.descriptor->sampleType(), [&]<typename SampleT>() noexcept {
        const SampleT* const samples = reinterpret_cast<const SampleT*>(row) + static_cast<std::size_t>(x0) * stride;
        for (std::uint32_t index = 0u; index < count; ++index)
        {
            const SampleT raw = samples[static_cast<std::size_t>(index) * stride];
            if (ruled && !(sampleIsValidByValue(validity.descriptor, raw) &&
                           plane_detail::maskAllows(KernelValidityView{.descriptor = validity.descriptor,
                                                                       .mask = validity.mask,
                                                                       .maskPlane = maskPlane},
                                                    x0 + index, y)))
            {
                values[index] = 0.0;
                continue;
            }
            if constexpr (std::is_floating_point_v<SampleT>)
            {
                values[index] = static_cast<double>(raw);
            }
            else
            {
                values[index] = convertSample<SampleT, double>(raw, policy).value;
            }
        }
    });
}

/// One run of values into one channel of a row of a destination, converted once.
void writeRun(const double* values, SampleType sampleType, std::uint8_t* destination, const ImageLayout& layout,
              std::uint32_t channel, std::uint32_t y, std::uint32_t x0, std::uint32_t count) noexcept
{
    const std::uint32_t stride = layout.samplesPerPixel();
    std::uint8_t* const row = destination + plane_detail::channelRowByteOffset(layout, channel, y);
    const ConversionPolicy policy{.domain = DomainMapping::FullScale};
    // A checked record names one of the eight representations, so the dispatch always runs.
    (void)dispatchSampleType(sampleType, [&]<typename SampleT>() noexcept {
        SampleT* const samples = reinterpret_cast<SampleT*>(row) + static_cast<std::size_t>(x0) * stride;
        for (std::uint32_t index = 0u; index < count; ++index)
        {
            samples[static_cast<std::size_t>(index) * stride] =
                convertSample<double, SampleT>(values[index], policy).value;
        }
    });
}

template <typename WorkingT>
[[nodiscard]] Result<ExternalToolHandoff>
prepareTyped(const ExecutionContext& execution, const ExternalToolHandoffRequest& request,
             const ExternalToolSource& source, const WorkingPlanes& working, std::uint8_t* valid, void* handed,
             const ImageLayout& handedLayout)
{
    const ImageDescriptor& descriptor = *source.descriptor;
    const std::uint32_t width = descriptor.extent().width();
    const std::uint32_t height = descriptor.extent().height();
    const std::uint32_t channels = descriptor.extent().channelCount();

    const Status read =
        composition_detail::readWorking(execution, compositionOf(source), working, 0u, valid, kPrepareOperation);
    if (read.failed())
    {
        return read;
    }

    ExternalToolHandoff handoff;
    handoff.sourceSampleType = descriptor.sampleType();
    handoff.width = width;
    handoff.height = height;
    handoff.channels = channels;
    const std::uint64_t pixels = static_cast<std::uint64_t>(width) * height;
    handoff.run.selection.total = pixels;
    handoff.run.selection.valid = pixels;
    handoff.run.outputsWritten = pixels * channels;
    if (valid != nullptr)
    {
        std::uint64_t accepted = 0u;
        for (std::uint64_t index = 0u; index < pixels; ++index)
        {
            accepted += valid[index];
        }
        handoff.run.selection.valid = accepted;
    }

    if (isFloatingSampleType(descriptor.sampleType()))
    {
        const Status measured = measureRange<WorkingT>(execution, working, handoff.rangeLow, handoff.rangeHigh);
        if (measured.failed())
        {
            return measured;
        }
    }

    const bool stretching = request.preStretch != ExternalToolPreStretch::None;
    const bool anyInvalid = handoff.run.selection.valid != pixels;
    const WorkingT invalidValue = stretching && anyInvalid ? std::numeric_limits<WorkingT>::quiet_NaN() : WorkingT{0};
    const Status mapped = mapIntoUnitInterval<WorkingT>(execution, working, anyInvalid ? valid : nullptr,
                                                        handoff.rangeLow, handoff.rangeHigh, invalidValue);
    if (mapped.failed())
    {
        return mapped;
    }

    if (stretching)
    {
        const ValidityDescriptor rule = anyInvalid ? finiteRule() : ValidityDescriptor{};
        const Result<std::array<MidtonesTransferParameters, 3u>> transfer =
            deriveTransfers(execution, request, working, rule);
        if (!transfer.hasValue())
        {
            return transfer.status();
        }
        handoff.stretched = true;
        handoff.transfer = *transfer;
        StretchImageRequest stretch;
        stretch.stretch.function = StretchFunction::ScreenTransfer;
        stretch.stretch.screenTransfer = handoff.transfer;
        stretch.colorMode = StretchColorMode::Independent;
        stretch.channelEnabled = {true, true, true, false};
        const Result<StretchImageResult> stretched =
            stretchImage(execution, working.sampleType, working.samples, *working.layout, working.samples,
                         *working.layout, stretch, KernelValidityView{.descriptor = rule});
        if (!stretched.hasValue())
        {
            return stretched.status();
        }
        if (anyInvalid)
        {
            const Status zeroed = zeroInvalid<WorkingT>(execution, working);
            if (zeroed.failed())
            {
                return zeroed;
            }
        }
    }

    const Status written = composition_detail::writeWorking(execution, working, formatSampleType(request.format),
                                                            handed, handedLayout, kPrepareOperation);
    if (written.failed())
    {
        return written;
    }
    return handoff;
}

} // namespace

Result<ExternalToolHandoffPlan> planExternalToolHandoff(const ExternalToolHandoffRequest& request)
{
    const Status checked = checkRequest(request, kPlanOperation);
    if (checked.failed())
    {
        return checked;
    }
    return ExternalToolHandoffPlan(request);
}

Result<ImageLayoutRequest> externalToolHandoffLayout(const ExternalToolHandoffPlan& plan, const ImageDescriptor& source)
{
    const std::uint32_t channels = source.extent().channelCount();
    if (!handoffChannels(channels))
    {
        return handoffFailure(ErrorCode::IncompatibleArguments, "a hand-off reads one channel or three",
                              kLayoutOperation, "source");
    }
    const std::uint32_t elementBytes = sampleTypeSizeBytes(formatSampleType(plan.request().format));
    return ImageLayoutRequest{.extent = source.extent(),
                              .elementSizeBytes = elementBytes,
                              .samplesPerPixel = 1u,
                              .planeCount = channels,
                              .rowAlignmentBytes = elementBytes};
}

Result<ExternalToolHandoff> prepareExternalToolHandoff(const ExecutionContext& execution,
                                                       const ExternalToolHandoffPlan& plan,
                                                       const ExternalToolSource& source, void* handed,
                                                       const ImageLayout& handedLayout)
{
    const Status ready = checkExecution(execution, kPrepareOperation);
    if (ready.failed())
    {
        return ready;
    }
    const ExternalToolHandoffRequest& request = plan.request();
    const CompositionSource composition = compositionOf(source);
    const Status sourceChecked = composition_detail::checkCompositionSource(composition, kPrepareOperation, "source");
    if (sourceChecked.failed())
    {
        return sourceChecked;
    }
    const ImageExtent& extent = source.descriptor->extent();
    if (!handoffChannels(extent.channelCount()))
    {
        return handoffFailure(ErrorCode::IncompatibleArguments, "a hand-off reads one channel or three",
                              kPrepareOperation, "source");
    }
    const Status destinationChecked = composition_detail::checkCompositionDestination(
        formatSampleType(request.format), handed, handedLayout, extent, kPrepareOperation);
    if (destinationChecked.failed())
    {
        return destinationChecked;
    }
    const Status separate = composition_detail::checkCompositionSeparate(composition, handed, handedLayout.totalBytes(),
                                                                         kPrepareOperation, "source");
    if (separate.failed())
    {
        return separate;
    }

    const SampleType workingType =
        source.descriptor->sampleType() == SampleType::Float64 ? SampleType::Float64 : SampleType::Float32;
    WorkingBlock planesBlock;
    const Result<WorkingPlanes> working = composition_detail::takeWorkingPlanes(
        execution, planesBlock, extent.width(), extent.height(), extent.channelCount(), workingType, kPrepareOperation);
    if (!working.hasValue())
    {
        return working.status();
    }
    WorkingBlock validBlock;
    std::uint8_t* valid = nullptr;
    if (composition_detail::compositionSourceRuled(composition))
    {
        const std::uint64_t pixels = static_cast<std::uint64_t>(extent.width()) * extent.height();
        const Status taken = validBlock.take(execution, pixels, kPrepareOperation);
        if (taken.failed())
        {
            return taken;
        }
        valid = validBlock.bytes();
        std::memset(valid, 1, static_cast<std::size_t>(pixels));
    }
    return workingType == SampleType::Float64
               ? prepareTyped<double>(execution, request, source, *working, valid, handed, handedLayout)
               : prepareTyped<float>(execution, request, source, *working, valid, handed, handedLayout);
}

Result<ExternalToolRestoreResult> restoreExternalToolAnswer(const ExecutionContext& execution,
                                                            const ExternalToolHandoffPlan& plan,
                                                            const ExternalToolHandoff& handoff, ExternalToolLayer layer,
                                                            const ExternalToolSource& answer, void* destination,
                                                            const ImageLayout& destinationLayout)
{
    (void)plan;
    const Status ready = checkExecution(execution, kRestoreOperation);
    if (ready.failed())
    {
        return ready;
    }
    if (!isExternalToolLayer(layer))
    {
        return handoffFailure(ErrorCode::InvalidArgument, "the answer's layer is outside its enumeration",
                              kRestoreOperation, "layer");
    }
    const Status recordChecked = checkRecord(handoff);
    if (recordChecked.failed())
    {
        return recordChecked;
    }
    const CompositionSource composition = compositionOf(answer);
    const Status answerChecked = composition_detail::checkCompositionSource(composition, kRestoreOperation, "answer");
    if (answerChecked.failed())
    {
        return answerChecked;
    }
    const ImageExtent& extent = answer.descriptor->extent();
    if (extent.width() != handoff.width || extent.height() != handoff.height ||
        extent.channelCount() != handoff.channels)
    {
        return handoffFailure(ErrorCode::IncompatibleArguments,
                              "the answer is not the width, height and channels of the source it was handed from",
                              kRestoreOperation, "answer");
    }
    const Status destinationChecked = composition_detail::checkCompositionDestination(
        handoff.sourceSampleType, destination, destinationLayout, extent, kRestoreOperation);
    if (destinationChecked.failed())
    {
        return destinationChecked;
    }
    const Status separate = composition_detail::checkCompositionSeparate(
        composition, destination, destinationLayout.totalBytes(), kRestoreOperation, "answer");
    if (separate.failed())
    {
        return separate;
    }

    const bool ruled = composition_detail::compositionSourceRuled(composition);
    const bool ownPlanes = answer.validity.mask != nullptr && answer.validity.mask->planeCount() == handoff.channels &&
                           handoff.channels > 1u;
    const double offset = layer == ExternalToolLayer::Image ? handoff.rangeLow : 0.0;
    const double span = handoff.rangeHigh - handoff.rangeLow;
    const bool mapped = handoff.rangeLow != 0.0 || handoff.rangeHigh != 1.0;
    std::uint8_t* const bytes = static_cast<std::uint8_t*>(destination);
    const std::uint32_t width = extent.width();
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * handoff.channels * 16u,
                        .operationsPerItem = static_cast<std::uint64_t>(width) * handoff.channels * 8u};
    const Status ran = parallelFor(execution, extent.height(), cost, [&](TaskRange range) -> Status {
        std::array<double, kRunPixels> values{};
        for (std::uint64_t y = range.begin; y < range.end; ++y)
        {
            const auto row = static_cast<std::uint32_t>(y);
            for (std::uint32_t channel = 0u; channel < handoff.channels; ++channel)
            {
                const MidtonesTransferParameters& transfer = handoff.transfer[channel];
                for (std::uint32_t x0 = 0u; x0 < width; x0 += kRunPixels)
                {
                    const std::uint32_t count = (std::min)(kRunPixels, width - x0);
                    readRun(answer, channel, row, x0, count, ruled, ownPlanes, values.data());
                    for (std::uint32_t index = 0u; index < count; ++index)
                    {
                        double value = values[index];
                        if (handoff.stretched)
                        {
                            value = inverseTransfer(value, transfer);
                        }
                        if (mapped)
                        {
                            value = offset + value * span;
                        }
                        values[index] = value;
                    }
                    writeRun(values.data(), handoff.sourceSampleType, bytes, destinationLayout, channel, row, x0,
                             count);
                }
            }
        }
        return Status::success();
    });
    if (ran.failed())
    {
        return ran;
    }
    ExternalToolRestoreResult result;
    const std::uint64_t pixels = static_cast<std::uint64_t>(width) * extent.height();
    result.run.selection.total = pixels;
    result.run.selection.valid = pixels;
    result.run.outputsWritten = pixels * handoff.channels;
    return result;
}

} // namespace an
