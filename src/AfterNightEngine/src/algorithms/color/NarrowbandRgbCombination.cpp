// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - NBRGB: continuum-corrected narrowband enhancement of RGB channels.
//
// The operation first snapshots whatever the destination is about to overwrite - a narrowband input
// that starts at the destination, or the whole broadband image when it is the destination - into
// the one context-accounted block, then copies broadband into the destination so every rejected
// pixel and every channel without an input is already final. Each enabled channel then runs its
// stages over two working planes of the operation's representation: the residual, its
// exact median, the boost, the least-squares fit and the greater-of choice. Those stages are serial
// loops on the calling thread in a fixed order, which is what keeps the median and the fit sums the
// same at every thread count; only the copy and each channel's final write are scheduled.
//
// Every stage reads admission through the snapshots, never through the destination, so a pixel's
// admission cannot change once the destination starts to be written.

#include <AfterNightEngine/Algorithms/Color/NarrowbandRgbCombination.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/OperationTransition.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "algorithms/color/NarrowbandInternal.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

namespace an
{
namespace
{

using narrowband_detail::failure;

constexpr const char* kDescriptorOperation = "an::narrowbandRgbOutputDescriptor";
constexpr const char* kCombineOperation = "an::combineNarrowbandRgb";
constexpr const char* kTransitionOperation = "an::narrowbandRgbTransition";
constexpr double kMinimumBandwidthDenominator = 1.0e-6;
constexpr double kMinimumFitDeviation = 1.0e-8;
constexpr std::uint64_t kMinimumFitSamples = 3u;

/// What validating a request established about its inputs and their overlaps.
struct RequestAnalysis
{
    /// Enabled narrowband inputs.
    std::uint32_t enabled = 0u;

    /// Enabled inputs that start at the destination and need a snapshot.
    std::uint32_t aliased = 0u;

    /// Whether the broadband source is exactly the destination.
    bool broadbandAliased = false;

    /// Each channel's snapshot plane among the aliased inputs, or -1 when it is read in place.
    std::array<std::int32_t, 3u> snapshotSlot{-1, -1, -1};
};

/// Tightly packed copies of inputs the destination overlaps, or null where an input is read in
/// place.
///
/// @tparam SampleT the resident representation.
template <typename SampleT> struct SourceSnapshots
{
    /// Each channel's narrowband snapshot.
    std::array<const SampleT*, 3u> narrowband{};

    /// Each broadband channel's snapshot, all three present exactly when broadband is the
    /// destination.
    std::array<const SampleT*, 3u> broadband{};
};

[[nodiscard]] Status validateScalarRequest(const NarrowbandRgbRequest& request, const char* operation)
{
    if (!std::isfinite(request.broadbandBandwidthNanometres) || request.broadbandBandwidthNanometres <= 0.0)
    {
        return failure(operation, ErrorCode::InvalidArgument, "the broadband filter width is finite and positive");
    }
    if (!std::isfinite(request.fitLow) || !std::isfinite(request.fitHigh) || request.fitLow < 0.0 ||
        request.fitHigh > 1.0 || request.fitHigh <= request.fitLow)
    {
        return failure(operation, ErrorCode::InvalidArgument,
                       "the NBRGB fit interval is an ordered nonempty part of the unit interval");
    }
    std::uint32_t enabled = 0u;
    for (const NarrowbandRgbChannel& channel : request.channels)
    {
        if (!channel.enabled)
        {
            continue;
        }
        ++enabled;
        if (!std::isfinite(channel.bandwidthNanometres) || channel.bandwidthNanometres <= 0.0 ||
            !std::isfinite(channel.scale) || channel.scale < 0.0 ||
            1.0 - channel.bandwidthNanometres / request.broadbandBandwidthNanometres <= kMinimumBandwidthDenominator)
        {
            return failure(operation, ErrorCode::InvalidArgument,
                           "an enabled narrowband filter has a valid width and nonnegative scale");
        }
    }
    return enabled == 0u
               ? failure(operation, ErrorCode::InvalidArgument, "an NBRGB combination needs an enabled channel")
               : Status::success();
}

[[nodiscard]] Result<RequestAnalysis> validateInvocation(const ExecutionContext& execution, SampleType sampleType,
                                                         const void* broadband, const ImageLayout& broadbandLayout,
                                                         void* destination, const ImageLayout& destinationLayout,
                                                         const NarrowbandRgbRequest& request,
                                                         const KernelValidityView& validity)
{
    if (!execution.valid())
    {
        return failure(kCombineOperation, ErrorCode::NotInitialized, "an NBRGB combination has no execution context");
    }
    if (const Status cancelled = execution.checkCancellation(); cancelled.failed())
    {
        return cancelled;
    }
    const Result<BackendSelection> selected = selectBackend(execution);
    if (!selected.hasValue())
    {
        return selected.status();
    }
    if (const Status scalar = validateScalarRequest(request, kCombineOperation); scalar.failed())
    {
        return scalar;
    }
    if (!narrowband_detail::supportedWorkingType(sampleType))
    {
        return failure(kCombineOperation, ErrorCode::Unsupported,
                       "NBRGB combination runs on a Float32 or Float64 working image");
    }
    const std::uint32_t elementBytes = sampleTypeSizeBytes(sampleType);
    if (!narrowband_detail::alignedFor(broadband, sampleType) ||
        !narrowband_detail::alignedFor(destination, sampleType) || broadbandLayout.elementSizeBytes() != elementBytes ||
        destinationLayout.elementSizeBytes() != elementBytes)
    {
        return failure(kCombineOperation, ErrorCode::InvalidArgument,
                       "an NBRGB pointer or layout disagrees with its sample type");
    }
    if (broadbandLayout.extent().channelCount() != 3u || destinationLayout.extent().channelCount() != 3u ||
        !narrowband_detail::sameGeometry(broadbandLayout, destinationLayout))
    {
        return failure(kCombineOperation, ErrorCode::IncompatibleArguments,
                       "NBRGB source and destination are aligned RGB images");
    }
    if (narrowband_detail::overlaps(broadband, broadbandLayout, destination, destinationLayout) &&
        !narrowband_detail::exactlyAliases(broadband, broadbandLayout, destination, destinationLayout))
    {
        return failure(kCombineOperation, ErrorCode::IncompatibleArguments,
                       "the broadband source partially overlaps its destination");
    }
    RequestAnalysis analysis{};
    analysis.broadbandAliased =
        narrowband_detail::exactlyAliases(broadband, broadbandLayout, destination, destinationLayout);
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        const NarrowbandRgbChannel& input = request.channels[channel];
        if (!input.enabled)
        {
            continue;
        }
        ++analysis.enabled;
        if (input.samples == nullptr || input.layout == nullptr ||
            !narrowband_detail::alignedFor(input.samples, sampleType) ||
            input.layout->elementSizeBytes() != elementBytes)
        {
            return failure(kCombineOperation, ErrorCode::InvalidArgument,
                           "an enabled narrowband input has aligned samples and a layout");
        }
        if (input.layout->extent().channelCount() != 1u ||
            !narrowband_detail::sameGeometry(*input.layout, broadbandLayout))
        {
            return failure(kCombineOperation, ErrorCode::IncompatibleArguments,
                           "an enabled narrowband input is one aligned monochrome image");
        }
        if (narrowband_detail::overlaps(input.samples, *input.layout, destination, destinationLayout))
        {
            if (input.samples != destination)
            {
                return failure(kCombineOperation, ErrorCode::IncompatibleArguments,
                               "a narrowband input partially overlaps its destination");
            }
            analysis.snapshotSlot[channel] = static_cast<std::int32_t>(analysis.aliased++);
        }
    }
    if (const Status valid = narrowband_detail::validateValidity(validity, broadbandLayout, destination,
                                                                 destinationLayout, kCombineOperation);
        valid.failed())
    {
        return valid;
    }
    return analysis;
}

template <typename SampleT>
[[nodiscard]] double broadbandAt(const void* broadband, const ImageLayout& broadbandLayout,
                                 const SourceSnapshots<SampleT>& snapshots, std::uint32_t x, std::uint32_t y,
                                 std::uint32_t channel) noexcept
{
    return snapshots.broadband[channel] != nullptr
               ? snapshots.broadband[channel][static_cast<std::uint64_t>(y) * broadbandLayout.extent().width() + x]
               : static_cast<double>(narrowband_detail::sampleAt<SampleT>(broadband, broadbandLayout, x, y, channel));
}

template <typename SampleT>
[[nodiscard]] bool pixelAdmitted(const NarrowbandRgbRequest& request, const KernelValidityView& validity,
                                 const void* broadband, const ImageLayout& broadbandLayout,
                                 const SourceSnapshots<SampleT>& snapshots, std::uint32_t x, std::uint32_t y) noexcept
{
    if (validity.descriptor.isAllValid())
    {
        return true;
    }
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        const double sample = broadbandAt<SampleT>(broadband, broadbandLayout, snapshots, x, y, channel);
        if (!sampleIsValidByValue(validity.descriptor, sample) || !narrowband_detail::maskAccepts(validity, x, y))
        {
            return false;
        }
        const NarrowbandRgbChannel& input = request.channels[channel];
        if (!input.enabled)
        {
            continue;
        }
        const double narrowband =
            snapshots.narrowband[channel] != nullptr
                ? snapshots.narrowband[channel][static_cast<std::uint64_t>(y) * broadbandLayout.extent().width() + x]
                : static_cast<double>(narrowband_detail::sampleAt<SampleT>(input.samples, *input.layout, x, y, 0u));
        if (!sampleIsValidByValue(validity.descriptor, narrowband) || !narrowband_detail::maskAccepts(validity, x, y))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] double finiteOrZero(double value, std::uint64_t& nonFinite) noexcept
{
    if (std::isfinite(value))
    {
        return value;
    }
    ++nonFinite;
    return 0.0;
}

template <typename SampleT>
[[nodiscard]] double narrowbandAt(const NarrowbandRgbChannel& input, const SampleT* snapshot, std::uint32_t width,
                                  std::uint32_t x, std::uint32_t y) noexcept
{
    return snapshot != nullptr
               ? snapshot[static_cast<std::uint64_t>(y) * width + x]
               : static_cast<double>(narrowband_detail::sampleAt<SampleT>(input.samples, *input.layout, x, y, 0u));
}

template <typename SampleT>
[[nodiscard]] Status copyBroadband(const ExecutionContext& execution, const void* broadband,
                                   const ImageLayout& broadbandLayout, void* destination,
                                   const ImageLayout& destinationLayout, const NarrowbandRgbRequest& request,
                                   const KernelValidityView& validity, const SourceSnapshots<SampleT>& snapshots,
                                   NarrowbandRgbResult& result)
{
    const std::uint32_t width = broadbandLayout.extent().width();
    const std::uint32_t height = broadbandLayout.extent().height();
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * 3u * sizeof(SampleT) * 2u,
                        .operationsPerItem = static_cast<std::uint64_t>(width) * 3u};
    std::atomic<std::uint64_t> admitted{0u};
    const auto body = [&](TaskRange rows) -> Status {
        std::uint64_t localAdmitted = 0u;
        for (std::uint64_t row = rows.begin; row < rows.end; ++row)
        {
            const std::uint32_t y = static_cast<std::uint32_t>(row);
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                if (pixelAdmitted<SampleT>(request, validity, broadband, broadbandLayout, snapshots, x, y))
                {
                    ++localAdmitted;
                }
                if (!narrowband_detail::exactlyAliases(broadband, broadbandLayout, destination, destinationLayout))
                {
                    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
                    {
                        narrowband_detail::writeAt<SampleT>(
                            destination, destinationLayout, x, y, channel,
                            broadbandAt<SampleT>(broadband, broadbandLayout, snapshots, x, y, channel));
                    }
                }
            }
        }
        admitted.fetch_add(localAdmitted, std::memory_order_relaxed);
        return Status::success();
    };
    const Status copied = parallelFor(execution, height, cost, body);
    if (copied.failed())
    {
        return copied;
    }
    result.run.selection.total = static_cast<std::uint64_t>(width) * height;
    result.run.selection.valid = admitted.load(std::memory_order_relaxed);
    result.run.outputsWritten = result.run.selection.total * 3u;
    result.plannedWorkers = plannedWorkerCount(execution, height, cost);
    return Status::success();
}

template <typename SampleT>
[[nodiscard]] Status rescaleOrClamp(const ExecutionContext& execution, SampleT* values,
                                    const NarrowbandRgbRequest& request, const KernelValidityView& validity,
                                    const void* broadband, const ImageLayout& broadbandLayout,
                                    const SourceSnapshots<SampleT>& snapshots)
{
    const std::uint32_t width = broadbandLayout.extent().width();
    const std::uint32_t height = broadbandLayout.extent().height();
    double minimum = (std::numeric_limits<double>::max)();
    double maximum = (std::numeric_limits<double>::lowest)();
    if (request.rescaleNonlinear)
    {
        for (std::uint32_t y = 0u; y < height; ++y)
        {
            if ((y & 31u) == 0u)
            {
                const Status cancelled = execution.checkCancellation();
                if (cancelled.failed())
                {
                    return cancelled;
                }
            }
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                if (pixelAdmitted<SampleT>(request, validity, broadband, broadbandLayout, snapshots, x, y))
                {
                    const double value = values[static_cast<std::uint64_t>(y) * width + x];
                    minimum = (std::min)(minimum, value);
                    maximum = (std::max)(maximum, value);
                }
            }
        }
    }
    const double range = maximum - minimum;
    for (std::uint32_t y = 0u; y < height; ++y)
    {
        if ((y & 31u) == 0u)
        {
            const Status cancelled = execution.checkCancellation();
            if (cancelled.failed())
            {
                return cancelled;
            }
        }
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            const std::uint64_t index = static_cast<std::uint64_t>(y) * width + x;
            if (!pixelAdmitted<SampleT>(request, validity, broadband, broadbandLayout, snapshots, x, y))
            {
                continue;
            }
            if (request.rescaleNonlinear && range > std::numeric_limits<double>::epsilon())
            {
                values[index] = static_cast<SampleT>((static_cast<double>(values[index]) - minimum) / range);
            }
            values[index] = static_cast<SampleT>(std::clamp(static_cast<double>(values[index]), 0.0, 1.0));
        }
    }
    return Status::success();
}

template <typename SampleT>
[[nodiscard]] Status combineChannel(const ExecutionContext& execution, std::uint32_t channel, const void* broadband,
                                    const ImageLayout& broadbandLayout, void* destination,
                                    const ImageLayout& destinationLayout, const NarrowbandRgbRequest& request,
                                    const KernelValidityView& validity, const SourceSnapshots<SampleT>& snapshots,
                                    SampleT* continuum, SampleT* other, NarrowbandRgbResult& result)
{
    const std::uint32_t width = broadbandLayout.extent().width();
    const std::uint32_t height = broadbandLayout.extent().height();
    const std::uint64_t pixels = static_cast<std::uint64_t>(width) * height;
    const NarrowbandRgbChannel& input = request.channels[channel];
    const double ratio = input.bandwidthNanometres / request.broadbandBandwidthNanometres;
    const double denominator = 1.0 - ratio;
    std::uint64_t localNonFinite = 0u;
    for (std::uint32_t y = 0u; y < height; ++y)
    {
        if ((y & 31u) == 0u)
        {
            const Status cancelled = execution.checkCancellation();
            if (cancelled.failed())
            {
                return cancelled;
            }
        }
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            const std::uint64_t index = static_cast<std::uint64_t>(y) * width + x;
            if (!pixelAdmitted<SampleT>(request, validity, broadband, broadbandLayout, snapshots, x, y))
            {
                continuum[index] = 0.0;
                continue;
            }
            const double rgb = finiteOrZero(broadbandAt<SampleT>(broadband, broadbandLayout, snapshots, x, y, channel),
                                            localNonFinite);
            const double narrowband =
                finiteOrZero(narrowbandAt<SampleT>(input, snapshots.narrowband[channel], width, x, y), localNonFinite);
            continuum[index] = static_cast<SampleT>((narrowband - rgb * ratio) / denominator);
            if (!request.rescaleNonlinear)
            {
                continuum[index] = static_cast<SampleT>(std::clamp(static_cast<double>(continuum[index]), 0.0, 1.0));
            }
        }
    }
    if (request.rescaleNonlinear)
    {
        if (const Status rescaled =
                rescaleOrClamp<SampleT>(execution, continuum, request, validity, broadband, broadbandLayout, snapshots);
            rescaled.failed())
        {
            return rescaled;
        }
    }
    std::uint64_t selected = 0u;
    for (std::uint32_t y = 0u; y < height; ++y)
    {
        if ((y & 31u) == 0u)
        {
            const Status cancelled = execution.checkCancellation();
            if (cancelled.failed())
            {
                return cancelled;
            }
        }
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            const std::uint64_t index = static_cast<std::uint64_t>(y) * width + x;
            if (pixelAdmitted<SampleT>(request, validity, broadband, broadbandLayout, snapshots, x, y))
            {
                other[selected++] = continuum[index];
            }
        }
    }
    if (selected == 0u)
    {
        return failure(kCombineOperation, ErrorCode::EmptySelection, "an NBRGB channel has no valid sample");
    }
    const double median = narrowband_detail::exactMedian(other, selected);
    for (std::uint32_t y = 0u; y < height; ++y)
    {
        if ((y & 31u) == 0u)
        {
            const Status cancelled = execution.checkCancellation();
            if (cancelled.failed())
            {
                return cancelled;
            }
        }
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            const std::uint64_t index = static_cast<std::uint64_t>(y) * width + x;
            if (!pixelAdmitted<SampleT>(request, validity, broadband, broadbandLayout, snapshots, x, y))
            {
                other[index] = 0.0;
                continue;
            }
            const double broadbandValue = broadbandAt<SampleT>(broadband, broadbandLayout, snapshots, x, y, channel);
            const double rgb = std::isfinite(broadbandValue) ? broadbandValue : 0.0;
            other[index] = static_cast<SampleT>(rgb + (continuum[index] - median) * input.scale);
            if (!request.rescaleNonlinear)
            {
                other[index] = static_cast<SampleT>(std::clamp(static_cast<double>(other[index]), 0.0, 1.0));
            }
        }
    }
    if (request.rescaleNonlinear)
    {
        if (const Status rescaled =
                rescaleOrClamp<SampleT>(execution, other, request, validity, broadband, broadbandLayout, snapshots);
            rescaled.failed())
        {
            return rescaled;
        }
    }

    std::uint64_t fitCount = 0u;
    double sumX = 0.0;
    double sumY = 0.0;
    double sumXX = 0.0;
    double sumXY = 0.0;
    for (std::uint32_t y = 0u; y < height; ++y)
    {
        if ((y & 31u) == 0u)
        {
            const Status cancelled = execution.checkCancellation();
            if (cancelled.failed())
            {
                return cancelled;
            }
        }
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            const std::uint64_t index = static_cast<std::uint64_t>(y) * width + x;
            if (!pixelAdmitted<SampleT>(request, validity, broadband, broadbandLayout, snapshots, x, y))
            {
                continue;
            }
            const double xValue = continuum[index];
            const double yValue = other[index];
            if (xValue == 0.0 || yValue == 0.0 || yValue < request.fitLow || yValue > request.fitHigh)
            {
                continue;
            }
            ++fitCount;
            sumX += xValue;
            sumY += yValue;
            sumXX += xValue * xValue;
            sumXY += xValue * yValue;
        }
    }
    double slope = 1.0;
    double intercept = 0.0;
    if (fitCount >= kMinimumFitSamples)
    {
        const double meanX = sumX / static_cast<double>(fitCount);
        const double variance = sumXX / static_cast<double>(fitCount) - meanX * meanX;
        if (std::sqrt((std::max)(0.0, variance)) > kMinimumFitDeviation)
        {
            const double denominatorFit = static_cast<double>(fitCount) * sumXX - sumX * sumX;
            if (std::abs(denominatorFit) > std::numeric_limits<double>::epsilon())
            {
                slope = (static_cast<double>(fitCount) * sumXY - sumX * sumY) / denominatorFit;
                intercept = (sumY - slope * sumX) / static_cast<double>(fitCount);
                result.fitApplied[channel] = true;
            }
        }
    }
    for (std::uint64_t index = 0u; index < pixels; ++index)
    {
        if (index % (static_cast<std::uint64_t>(width) * 32u) == 0u)
        {
            const Status cancelled = execution.checkCancellation();
            if (cancelled.failed())
            {
                return cancelled;
            }
        }
        const std::uint32_t x = static_cast<std::uint32_t>(index % width);
        const std::uint32_t y = static_cast<std::uint32_t>(index / width);
        if (!pixelAdmitted<SampleT>(request, validity, broadband, broadbandLayout, snapshots, x, y))
        {
            continue;
        }
        const SampleT matched =
            static_cast<SampleT>(result.fitApplied[channel] ? static_cast<double>(continuum[index]) * slope + intercept
                                                            : static_cast<double>(continuum[index]));
        continuum[index] = (std::max)(other[index], matched);
        if (!request.rescaleNonlinear)
        {
            continuum[index] = static_cast<SampleT>(std::clamp(static_cast<double>(continuum[index]), 0.0, 1.0));
        }
    }
    if (request.rescaleNonlinear)
    {
        if (const Status rescaled =
                rescaleOrClamp<SampleT>(execution, continuum, request, validity, broadband, broadbandLayout, snapshots);
            rescaled.failed())
        {
            return rescaled;
        }
    }
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * sizeof(SampleT) * 2u,
                        .operationsPerItem = static_cast<std::uint64_t>(width) * 2u};
    const auto write = [&](TaskRange rows) -> Status {
        for (std::uint64_t row = rows.begin; row < rows.end; ++row)
        {
            const std::uint32_t y = static_cast<std::uint32_t>(row);
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                if (!pixelAdmitted<SampleT>(request, validity, broadband, broadbandLayout, snapshots, x, y))
                {
                    continue;
                }
                narrowband_detail::writeAt<SampleT>(destination, destinationLayout, x, y, channel,
                                                    continuum[static_cast<std::uint64_t>(y) * width + x]);
            }
        }
        return Status::success();
    };
    const Status written = parallelFor(execution, height, cost, write);
    if (written.failed())
    {
        return written;
    }
    result.channelCombined[channel] = true;
    result.nonFiniteSamples += localNonFinite;
    return Status::success();
}

template <typename SampleT>
[[nodiscard]] Result<NarrowbandRgbResult>
combineTyped(const ExecutionContext& execution, const void* broadband, const ImageLayout& broadbandLayout,
             void* destination, const ImageLayout& destinationLayout, const NarrowbandRgbRequest& request,
             const KernelValidityView& validity, const RequestAnalysis& analysis)
{
    const std::uint64_t pixels =
        static_cast<std::uint64_t>(broadbandLayout.extent().width()) * broadbandLayout.extent().height();
    const std::uint64_t planes = 2u + analysis.aliased + (analysis.broadbandAliased ? 3u : 0u);
    if (pixels > (std::numeric_limits<std::uint64_t>::max)() / (planes * sizeof(SampleT)))
    {
        return failure(kCombineOperation, ErrorCode::LimitExceeded, "the NBRGB workspace size overflows");
    }
    const std::uint64_t bytes = pixels * planes * sizeof(SampleT);
    narrowband_detail::ContextBlock block;
    if (const Status allocated = block.take(execution, bytes, kCombineOperation); allocated.failed())
    {
        return allocated;
    }
    SampleT* continuum = block.values<SampleT>();
    SampleT* other = continuum + pixels;
    SourceSnapshots<SampleT> snapshots{};
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        if (analysis.snapshotSlot[channel] < 0)
        {
            continue;
        }
        SampleT* snapshot = other + pixels * (1u + static_cast<std::uint32_t>(analysis.snapshotSlot[channel]));
        snapshots.narrowband[channel] = snapshot;
        for (std::uint32_t y = 0u; y < broadbandLayout.extent().height(); ++y)
        {
            if ((y & 31u) == 0u)
            {
                const Status cancelled = execution.checkCancellation();
                if (cancelled.failed())
                {
                    return cancelled;
                }
            }
            for (std::uint32_t x = 0u; x < broadbandLayout.extent().width(); ++x)
            {
                snapshot[static_cast<std::uint64_t>(y) * broadbandLayout.extent().width() + x] =
                    narrowband_detail::sampleAt<SampleT>(request.channels[channel].samples,
                                                         *request.channels[channel].layout, x, y, 0u);
            }
        }
    }
    if (analysis.broadbandAliased)
    {
        SampleT* snapshotBase = other + pixels * (1u + analysis.aliased);
        for (std::uint32_t channel = 0u; channel < 3u; ++channel)
        {
            SampleT* snapshot = snapshotBase + pixels * channel;
            snapshots.broadband[channel] = snapshot;
            for (std::uint32_t y = 0u; y < broadbandLayout.extent().height(); ++y)
            {
                if ((y & 31u) == 0u)
                {
                    const Status cancelled = execution.checkCancellation();
                    if (cancelled.failed())
                    {
                        return cancelled;
                    }
                }
                for (std::uint32_t x = 0u; x < broadbandLayout.extent().width(); ++x)
                {
                    snapshot[static_cast<std::uint64_t>(y) * broadbandLayout.extent().width() + x] =
                        narrowband_detail::sampleAt<SampleT>(broadband, broadbandLayout, x, y, channel);
                }
            }
        }
    }
    NarrowbandRgbResult result{};
    result.workingBytes = bytes;
    if (const Status copied = copyBroadband<SampleT>(execution, broadband, broadbandLayout, destination,
                                                     destinationLayout, request, validity, snapshots, result);
        copied.failed())
    {
        return copied;
    }
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        if (!request.channels[channel].enabled)
        {
            continue;
        }
        if (const Status combined =
                combineChannel<SampleT>(execution, channel, broadband, broadbandLayout, destination, destinationLayout,
                                        request, validity, snapshots, continuum, other, result);
            combined.failed())
        {
            return combined;
        }
    }
    return result;
}

} // namespace

Result<ImageDescriptor> narrowbandRgbOutputDescriptor(const ImageDescriptor& broadband)
{
    if (!narrowband_detail::supportedWorkingType(broadband.sampleType()) ||
        broadband.channelSchema() != ChannelSchema::Rgb || broadband.cfa().isMosaiced() ||
        (broadband.numericDomain() != NumericDomain::NormalizedLinear &&
         broadband.numericDomain() != NumericDomain::EncodedNonlinear))
    {
        return failure(kDescriptorOperation, ErrorCode::Unsupported,
                       "an NBRGB source is a resolved normalized floating RGB image");
    }
    ImageDescriptorRequest request = broadband.toRequest();
    request.colorSpace = ColorSpace::NarrowbandMapping;
    request.cfa = {};
    const std::optional<ImageDescriptor> output = ImageDescriptor::create(request);
    return output.has_value() ? Result<ImageDescriptor>{*output}
                              : Result<ImageDescriptor>{failure(kDescriptorOperation, ErrorCode::IncompatibleArguments,
                                                                "the NBRGB output description is inconsistent")};
}

Result<NarrowbandRgbResult> combineNarrowbandRgb(const ExecutionContext& execution, SampleType sampleType,
                                                 const void* broadband, const ImageLayout& broadbandLayout,
                                                 void* destination, const ImageLayout& destinationLayout,
                                                 const NarrowbandRgbRequest& request,
                                                 const KernelValidityView& validity)
{
    const Result<RequestAnalysis> analysis = validateInvocation(execution, sampleType, broadband, broadbandLayout,
                                                                destination, destinationLayout, request, validity);
    if (!analysis.hasValue())
    {
        return analysis.status();
    }
    return sampleType == SampleType::Float32 ? combineTyped<float>(execution, broadband, broadbandLayout, destination,
                                                                   destinationLayout, request, validity, *analysis)
                                             : combineTyped<double>(execution, broadband, broadbandLayout, destination,
                                                                    destinationLayout, request, validity, *analysis);
}

Result<MetadataTransition> narrowbandRgbTransition(const ImageSemantics& source, const NarrowbandRgbRequest& request)
{
    if (const Status valid = validateScalarRequest(request, kTransitionOperation); valid.failed())
    {
        return valid;
    }
    std::string detail = "channels=";
    detail += request.channels[0].enabled ? "R" : "-";
    detail += request.channels[1].enabled ? "G" : "-";
    detail += request.channels[2].enabled ? "B" : "-";
    return operationTransition(
        kNarrowbandRgbEffect,
        OperationOutcome{.operation = kCombineOperation, .detail = std::move(detail), .semantics = source});
}

} // namespace an
