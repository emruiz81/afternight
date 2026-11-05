// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "controls/ImageStatisticsReport.h"

#include "widgets/DisplayNormalization.h"

#include <AfterNightEngine/Algorithms/Masks/MaskOperations.h>
#include <AfterNightEngine/Algorithms/Statistics/AdvancedStatistics.h>
#include <AfterNightEngine/Algorithms/Statistics/BasicStatistics.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>

namespace an::ui
{
namespace
{

/// The samples one channel's figures are over: the selection, and the mask it points at when it needs one.
struct ReportSelection
{
    an::StatisticsSelection selection;
    std::optional<an::ValidityMask> combined;
    std::optional<an::ValidityMaskView<const std::uint8_t>> mask;
    bool admitsNothing = false;
};

/// The interval strictly inside the representation's display range, in its own numbers: an integer's ends are whole
/// codes, so half a code inside them separates them, and a float's are the nearest doubles inside zero and one.
[[nodiscard]] std::pair<double, double> unclippedInterval(const DisplayNormalization& normalization) noexcept
{
    if (normalization.integer)
    {
        return {normalization.lowest + 0.5, normalization.lowest + normalization.range - 0.5};
    }
    return {std::nextafter(0.0, 1.0), std::nextafter(1.0, 0.0)};
}

[[nodiscard]] an::Status refusal(const an::ErrorCode code, const char* message)
{
    return an::Status::failure(code, message);
}

/// A mask that admits a sample only where the image's own mask, or its zero-is-no-data rule, admits it and it lies
/// strictly inside the display range - one plane per channel, because the range is decided sample by sample.
[[nodiscard]] an::Result<an::ValidityMask>
combinedUnclippedMask(const an::consumer::OperationScope& scope, const an::SampleType sampleType, const void* samples,
                      const an::ImageLayout& layout, const an::ValidityDescriptor& validity,
                      const an::ValidityMask* imageMask, const std::pair<double, double>& interval)
{
    const an::ImageExtent extent = layout.extent();
    an::Result<an::ValidityMask> combined = an::ValidityMask::create(scope.engine(), extent);
    if (!combined.hasValue())
    {
        return combined.status();
    }
    std::optional<an::ValidityMaskView<std::uint8_t>> combinedView = combined->view();
    if (!combinedView.has_value())
    {
        return refusal(an::ErrorCode::Internal, "the combined mask holds no bits");
    }

    an::MaskThresholdRequest inside;
    inside.threshold.predicate = an::MaskPredicate::InsideClosedRange;
    inside.threshold.lowerBound = interval.first;
    inside.threshold.upperBound = interval.second;
    inside.threshold.nonFiniteSelected = false;
    if (const an::Status built =
            an::buildImageMask(scope.execution(), sampleType, samples, layout, inside, *combinedView);
        built.failed())
    {
        return built;
    }

    if (validity.rule() == an::ValidityRule::ExplicitMask)
    {
        if (imageMask == nullptr)
        {
            return refusal(an::ErrorCode::InvalidData, "the image's validity names a mask it does not carry");
        }
        const std::optional<an::ValidityMaskView<const std::uint8_t>> own = imageMask->view();
        if (!own.has_value())
        {
            return refusal(an::ErrorCode::InvalidData, "the image's mask holds no bits");
        }
        if (own->planeCount() == extent.channelCount())
        {
            if (const an::Status joined =
                    an::combineImageMasks(scope.execution(), *combinedView, combinedView->constView(), *own,
                                          an::ValidityCombination::Intersection);
                joined.failed())
            {
                return joined;
            }
            return combined;
        }
        // One plane every channel shares: each channel's plane is joined with it.
        const std::optional<an::ImageExtent> plane =
            an::ImageExtent::create(extent.width(), extent.height(), 1u, scope.engine().limits());
        if (!plane.has_value())
        {
            return refusal(an::ErrorCode::Internal, "a plane of the image has no extent");
        }
        const std::uint64_t planeBytes = combinedView->planeStrideBytes();
        for (std::uint32_t channel = 0u; channel < extent.channelCount(); ++channel)
        {
            const std::optional<an::ValidityMaskView<std::uint8_t>> channelPlane =
                an::ValidityMaskView<std::uint8_t>::create(
                    std::span<std::uint8_t>(combinedView->data() + channel * planeBytes, planeBytes), *plane,
                    combinedView->rowStrideBytes(), planeBytes);
            if (!channelPlane.has_value())
            {
                return refusal(an::ErrorCode::Internal, "a plane of the combined mask has no view");
            }
            if (const an::Status joined = an::combineValidity(*channelPlane, channelPlane->constView(), *own,
                                                              an::ValidityCombination::Intersection);
                joined.failed())
            {
                return joined;
            }
        }
        return combined;
    }

    // A value rule: the samples it admits are marked, then joined with the interval.
    an::Result<an::ValidityMask> admitted = an::ValidityMask::create(scope.engine(), extent);
    if (!admitted.hasValue())
    {
        return admitted.status();
    }
    std::optional<an::ValidityMaskView<std::uint8_t>> admittedView = admitted->view();
    if (!admittedView.has_value())
    {
        return refusal(an::ErrorCode::Internal, "the admitted mask holds no bits");
    }
    if (const an::Status marked =
            an::markImageValidity(scope.execution(), sampleType, samples, layout, validity, *admittedView);
        marked.failed())
    {
        return marked;
    }
    if (const an::Status joined =
            an::combineImageMasks(scope.execution(), *combinedView, combinedView->constView(),
                                  admittedView->constView(), an::ValidityCombination::Intersection);
        joined.failed())
    {
        return joined;
    }
    return combined;
}

/// Decides which samples count: the image's validity and, when clipped samples are left out, the interval inside the
/// display range as well.
[[nodiscard]] an::Status selectSamples(const an::consumer::OperationScope& scope, const an::SampleType sampleType,
                                       const void* samples, const an::ImageLayout& layout,
                                       const an::ValidityDescriptor& validity, const an::ValidityMask* imageMask,
                                       const bool excludeClipped, ReportSelection& out)
{
    if (!excludeClipped)
    {
        // The figures are over finite samples, as the window's always were: a floating image that states every sample
        // valid is read over the finite ones rather than refused for a not-a-number.
        out.selection.validity = validity;
        if (validity.rule() == an::ValidityRule::AllValid && an::isFloatingSampleType(sampleType))
        {
            const std::optional<an::ValidityDescriptor> finite =
                an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::FiniteOnly});
            if (finite.has_value())
            {
                out.selection.validity = *finite;
            }
        }
        if (imageMask != nullptr && validity.rule() == an::ValidityRule::ExplicitMask)
        {
            out.mask = imageMask->view();
            out.selection.mask = out.mask.has_value() ? &*out.mask : nullptr;
        }
        return an::Status::success();
    }

    const DisplayNormalization normalization = displayNormalization(sampleType);
    std::pair<double, double> interval = unclippedInterval(normalization);
    bool asRange = false;
    switch (validity.rule())
    {
    case an::ValidityRule::AllValid:
    case an::ValidityRule::FiniteOnly:
        // A range admits no sample that is not a finite number.
        asRange = true;
        break;
    case an::ValidityRule::DomainRange:
        interval.first = (std::max)(interval.first, validity.lowestValid().value_or(interval.first));
        interval.second = (std::min)(interval.second, validity.highestValid().value_or(interval.second));
        if (interval.first > interval.second)
        {
            out.admitsNothing = true;
            return an::Status::success();
        }
        asRange = true;
        break;
    case an::ValidityRule::ZeroIsNoData:
        // Zero is at the bottom of an unsigned or floating range, outside the interval; a signed one holds it inside.
        asRange = normalization.lowest >= 0.0;
        break;
    case an::ValidityRule::ExplicitMask:
        break;
    }

    if (asRange)
    {
        const std::optional<an::ValidityDescriptor> range =
            an::ValidityDescriptor::create(an::ValidityDescriptorRequest{
                .rule = an::ValidityRule::DomainRange, .lowestValid = interval.first, .highestValid = interval.second});
        if (!range.has_value())
        {
            return refusal(an::ErrorCode::Internal, "the unclipped interval is not a valid range");
        }
        out.selection.validity = *range;
        return an::Status::success();
    }

    an::Result<an::ValidityMask> combined =
        combinedUnclippedMask(scope, sampleType, samples, layout, validity, imageMask, interval);
    if (!combined.hasValue())
    {
        return combined.status();
    }
    out.combined.emplace(std::move(*combined));
    out.mask = std::as_const(*out.combined).view();
    const std::optional<an::ValidityDescriptor> masked =
        an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::ExplicitMask});
    if (!masked.has_value() || !out.mask.has_value())
    {
        return refusal(an::ErrorCode::Internal, "the combined mask cannot be read");
    }
    out.selection.validity = *masked;
    out.selection.mask = &*out.mask;
    return an::Status::success();
}

/// One channel's figures over a selection, as fractions of the display range.
[[nodiscard]] an::Result<ImageStatisticsChannel>
channelFigures(const an::ExecutionContext& execution, const an::SampleType sampleType, const void* samples,
               const an::ImageLayout& layout, const std::uint32_t channel, const ReportSelection& selected)
{
    ImageStatisticsChannel figures;
    figures.pixels = layout.extent().pixelCount();
    if (selected.admitsNothing)
    {
        return figures;
    }
    // One measurement gives every figure: the basic statistics come back with the advanced ones, and the window shows
    // no iterative k-sigma estimate, so its ranking is not asked for.
    const an::Result<an::AdvancedStatistics> advanced =
        an::computeImageAdvancedStatistics(execution, sampleType, samples, layout, channel, selected.selection,
                                           an::AdvancedStatisticsRequest{.iterativeKsigma = false});
    if (!advanced.hasValue())
    {
        if (advanced.status().code() == an::ErrorCode::EmptySelection)
        {
            return figures;
        }
        return advanced.status();
    }
    const an::BasicStatistics& basic = advanced->basic;

    const DisplayNormalization normalization = displayNormalization(sampleType);
    const double range = normalization.range;
    figures.count = basic.selection.valid;
    figures.mean = displayFraction(normalization, basic.mean);
    figures.minimum = displayFraction(normalization, basic.minimum);
    figures.maximum = displayFraction(normalization, basic.maximum);
    figures.median = displayFraction(normalization, advanced->median);
    figures.standardDeviation = basic.populationStandardDeviation / range;
    figures.variance = basic.populationVariance / (range * range);
    figures.averageDeviation = advanced->averageAbsoluteDeviation / range;
    figures.medianAbsoluteDeviation = advanced->medianAbsoluteDeviation / range;
    figures.biweightMidvariance = advanced->biweightMidvariance / (range * range);
    figures.backgroundNoise = advanced->backgroundNoise / range;
    return figures;
}

} // namespace

std::array<double, kStatisticsFigureRows> scaledStatisticsFigures(const ImageStatisticsChannel& channel,
                                                                  const StatisticsScale scale) noexcept
{
    double fullScale = 1.0;
    switch (scale)
    {
    case StatisticsScale::Normalized:
        fullScale = 1.0;
        break;
    case StatisticsScale::Bits8:
        fullScale = 255.0;
        break;
    case StatisticsScale::Bits16:
        fullScale = 65535.0;
        break;
    case StatisticsScale::Bits32:
        fullScale = 4294967295.0;
        break;
    }
    const double squared = fullScale * fullScale;
    return {channel.mean * fullScale,
            channel.standardDeviation * fullScale,
            channel.median * fullScale,
            channel.minimum * fullScale,
            channel.maximum * fullScale,
            channel.standardDeviation * fullScale,
            channel.averageDeviation * fullScale,
            channel.biweightMidvariance * squared,
            channel.variance * squared,
            channel.medianAbsoluteDeviation * fullScale,
            channel.backgroundNoise * fullScale};
}

an::Result<ImageStatisticsReport> computeImageStatisticsReport(const an::consumer::OperationScope& scope,
                                                               const an::consumer::AssetReadLease& image,
                                                               const bool excludeClipped)
{
    return image.visit([&](const auto& typed) -> an::Result<ImageStatisticsReport> {
        using ImageT = std::decay_t<decltype(typed)>;
        const auto view = typed.view();
        if (!view.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the image holds no samples to measure");
        }
        const an::ImageLayout layout = view->layout();
        ReportSelection selected;
        if (const an::Status chosen =
                selectSamples(scope, ImageT::kSampleType, view->data(), layout, typed.metadata().semantics().validity(),
                              image.validity(), excludeClipped, selected);
            chosen.failed())
        {
            return chosen;
        }
        ImageStatisticsReport report;
        const std::uint32_t channels = layout.extent().channelCount();
        report.channels.reserve(channels);
        for (std::uint32_t channel = 0u; channel < channels; ++channel)
        {
            an::Result<ImageStatisticsChannel> figures =
                channelFigures(scope.execution(), ImageT::kSampleType, view->data(), layout, channel, selected);
            if (!figures.hasValue())
            {
                return figures.status();
            }
            report.channels.push_back(*figures);
        }
        return report;
    });
}

} // namespace an::ui
