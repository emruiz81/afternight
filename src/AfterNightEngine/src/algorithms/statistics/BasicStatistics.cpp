// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - public basic statistics over an image selection.
//
// The selection is checked here with the same rules and failures as the shared translation in
// `algorithms/statistics/StatisticsSelectionInternal.h`, and the reduction itself is
// `reducePlaneStatisticsOver`, whose input-derived partition and fixed pairwise merge are what make
// the result the same at every thread count. The standard deviation is the only value computed here.

#include <AfterNightEngine/Algorithms/Statistics/BasicStatistics.h>

#include <AfterNightEngine/Algorithms/Kernel/ReductionKernel.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <cmath>
#include <cstdint>

namespace an
{
namespace
{

constexpr const char* kOperation = "an::computeImageBasicStatistics";

[[nodiscard]] Status statisticsFailure(ErrorCode code, const char* message)
{
    return Status::failure(
        code, StatusDetail{.message = message, .operation = kOperation, .domain = ErrorDomain::Algorithms});
}

[[nodiscard]] bool regionFits(const StatisticsRegion& region, const ImageLayout& layout) noexcept
{
    const std::uint32_t imageWidth = layout.extent().width();
    const std::uint32_t imageHeight = layout.extent().height();
    return region.originX <= imageWidth && region.originY <= imageHeight &&
           region.width <= imageWidth - region.originX && region.height <= imageHeight - region.originY;
}

[[nodiscard]] StatisticsAccumulatorErrorBound publicBound(const ReductionErrorBound& bound) noexcept
{
    return StatisticsAccumulatorErrorBound{.maximumAdditionDepth = bound.maximumAdditionDepth,
                                           .sumAbsoluteTerms = bound.sumAbsoluteTerms,
                                           .absoluteError = bound.absoluteError};
}

} // namespace

Result<BasicStatistics> computeImageBasicStatistics(const ExecutionContext& execution, SampleType sampleType,
                                                    const void* samples, const ImageLayout& layout,
                                                    std::uint32_t channelIndex, const StatisticsSelection& selection)
{
    if (!execution.valid())
    {
        return statisticsFailure(ErrorCode::NotInitialized, "basic statistics were given an empty execution context");
    }
    const Status cancellation = execution.checkCancellation();
    if (cancellation.failed())
    {
        return cancellation;
    }
    if (samples == nullptr || !isSampleType(sampleType) || layout.elementSizeBytes() != sampleTypeSizeBytes(sampleType))
    {
        return statisticsFailure(ErrorCode::InvalidArgument,
                                 "the statistics source has a null pointer, unknown sample type, or mismatched layout");
    }
    if (channelIndex >= layout.extent().channelCount())
    {
        return Status::failure(
            ErrorCode::InvalidArgument,
            StatusDetail{
                .message = "the statistics channel is outside the source image",
                .operation = kOperation,
                .domain = ErrorDomain::Algorithms,
                .context = {StatusContextField::ofText(ContextKey::Parameter, "channelIndex"),
                            StatusContextField::ofCount(ContextKey::Requested, channelIndex),
                            StatusContextField::ofCount(ContextKey::Available, layout.extent().channelCount())}});
    }

    const StatisticsRegion region = selection.region.value_or(StatisticsRegion{
        .originX = 0u, .originY = 0u, .width = layout.extent().width(), .height = layout.extent().height()});
    if (!regionFits(region, layout))
    {
        return statisticsFailure(ErrorCode::InvalidArgument, "the statistics region extends outside the source image");
    }

    std::uint32_t maskPlane = 0u;
    if (!selection.validity.needsMask())
    {
        if (selection.mask != nullptr)
        {
            return statisticsFailure(ErrorCode::InvalidArgument,
                                     "a packed mask was supplied for a statistics validity rule that does not use one");
        }
    }
    else
    {
        if (selection.mask == nullptr)
        {
            return statisticsFailure(ErrorCode::InvalidArgument, "explicit-mask statistics were given no packed mask");
        }
        if (selection.mask->width() != layout.extent().width() ||
            selection.mask->height() != layout.extent().height() ||
            (selection.mask->planeCount() != 1u && selection.mask->planeCount() != layout.extent().channelCount()))
        {
            return statisticsFailure(
                ErrorCode::IncompatibleArguments,
                "the statistics mask does not cover the source image with one shared plane or one plane per channel");
        }
        maskPlane = selection.mask->planeCount() == 1u ? 0u : channelIndex;
    }

    const std::uint32_t plane = channelIndex / layout.samplesPerPixel();
    const std::uint32_t sampleInPixel = channelIndex % layout.samplesPerPixel();
    const KernelPlaneSource source{.sampleType = sampleType,
                                   .samples = samples,
                                   .layout = &layout,
                                   .plane = plane,
                                   .sampleInPixel = sampleInPixel};
    const KernelValidityView validity{.descriptor = selection.validity, .mask = selection.mask, .maskPlane = maskPlane};
    const Result<ReductionStatistics> reduced = reducePlaneStatisticsOver(
        execution, source, validity,
        KernelPlaneSelection{
            .originX = region.originX, .originY = region.originY, .width = region.width, .height = region.height});
    if (!reduced.hasValue())
    {
        return reduced.status();
    }

    BasicStatistics statistics;
    statistics.selection = reduced->run.selection;
    statistics.minimum = reduced->minimum;
    statistics.maximum = reduced->maximum;
    statistics.sum = reduced->sum;
    statistics.mean = reduced->mean;
    statistics.sumSquaredDeviations = reduced->sumSquaredDeviations;
    statistics.populationVariance = reduced->populationVariance;
    statistics.populationStandardDeviation = std::sqrt(reduced->populationVariance);
    statistics.sumError = publicBound(reduced->sumError);
    statistics.centeredMomentError = publicBound(reduced->centeredMomentError);
    return statistics;
}

} // namespace an
