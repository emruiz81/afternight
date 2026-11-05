// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "widgets/HistogramComputation.h"
#include "widgets/DisplayNormalization.h"

#include <AfterNightEngine/Algorithms/Statistics/Histogram.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace an::ui
{

an::Result<HistogramDisplayData> computeHistogramDisplayData(const an::ExecutionContext& execution,
                                                             const an::SampleType sampleType, const void* samples,
                                                             const an::ImageLayout& layout,
                                                             const HistogramResolution resolution,
                                                             const an::StatisticsSelection& selection)
{
    const std::size_t binCount = histogramBinCount(resolution);
    const DisplayNormalization normalization = displayNormalization(sampleType);

    // Bin i of n holds [i / (n - 1), (i + 1) / (n - 1)) of the range and the last bin full scale alone. The engine's n
    // bins over [0, n / (n - 1)] of the range are exactly those, the last closed at the top.
    an::ImageHistogramRequest binned;
    binned.lowest = normalization.lowest;
    binned.highest =
        normalization.lowest + normalization.range * static_cast<double>(binCount) / static_cast<double>(binCount - 1u);

    // A sample at or below zero, or at or above full scale, is clipped: one bin over the open interval between the two
    // counts both ends as outside it. An integer's ends are whole codes, so half a code inside them separates them.
    an::ImageHistogramRequest clipping;
    clipping.lowest = normalization.integer ? normalization.lowest + 0.5 : std::nextafter(0.0, 1.0);
    clipping.highest =
        normalization.integer ? normalization.lowest + normalization.range - 0.5 : std::nextafter(1.0, 0.0);

    HistogramDisplayData data;
    const std::uint32_t channels = layout.extent().channelCount();
    data.channels.reserve(channels);
    std::vector<std::uint64_t> clippingBin(1u, 0u);
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        HistogramDisplayChannel counts;
        counts.bins.assign(binCount, 0u);
        const an::Result<an::ImageHistogram> histogram =
            an::computeImageHistogram(execution, sampleType, samples, layout, channel, selection, binned, counts.bins);
        if (!histogram.hasValue())
        {
            if (histogram.status().code() == an::ErrorCode::EmptySelection)
            {
                // Nothing of this channel means anything: it is drawn as nothing.
                std::fill(counts.bins.begin(), counts.bins.end(), std::uint64_t{0});
                data.channels.push_back(std::move(counts));
                continue;
            }
            return histogram.status();
        }
        counts.bins.front() += histogram->belowRange;
        counts.bins.back() += histogram->aboveRange;
        counts.notANumber = histogram->notANumber;
        counts.samples = histogram->selection.valid;

        clippingBin.front() = 0u;
        const an::Result<an::ImageHistogram> clipped = an::computeImageHistogram(
            execution, sampleType, samples, layout, channel, selection, clipping, clippingBin);
        if (!clipped.hasValue())
        {
            return clipped.status();
        }
        counts.clippedLow = clipped->belowRange;
        counts.clippedHigh = clipped->aboveRange;
        data.channels.push_back(std::move(counts));
    }
    return data;
}

an::Result<HistogramDisplayData> computeHistogramDisplayData(const an::ExecutionContext& execution,
                                                             const an::consumer::AssetReadLease& image,
                                                             const HistogramResolution resolution)
{
    std::optional<an::ValidityMaskView<const std::uint8_t>> mask;
    if (const an::ValidityMask* validity = image.validity(); validity != nullptr)
    {
        mask = validity->view();
    }
    return image.visit([&](const auto& typed) -> an::Result<HistogramDisplayData> {
        using ImageT = std::decay_t<decltype(typed)>;
        const auto view = typed.view();
        if (!view.has_value())
        {
            return an::Status::failure(an::ErrorCode::NotInitialized, "the image holds no samples to count");
        }
        an::StatisticsSelection selection;
        selection.validity = typed.metadata().semantics().validity();
        selection.mask = mask.has_value() ? &*mask : nullptr;
        const an::ImageLayout layout = view->layout();
        return computeHistogramDisplayData(execution, ImageT::kSampleType, view->data(), layout, resolution, selection);
    });
}

std::uint32_t codeCountOf(const an::SampleType sampleType) noexcept
{
    switch (sampleType)
    {
    case an::SampleType::UInt8:
    case an::SampleType::Int8:
        return 256u;
    case an::SampleType::UInt16:
    case an::SampleType::Int16:
        return 65536u;
    case an::SampleType::UInt32:
    case an::SampleType::Int32:
    case an::SampleType::Float32:
    case an::SampleType::Float64:
        return 0u;
    }
    return 0u;
}

an::Result<CodeHistogram> countCodes(const an::ExecutionContext& execution, const an::consumer::AssetReadLease& image)
{
    std::optional<an::ValidityMaskView<const std::uint8_t>> mask;
    if (const an::ValidityMask* validity = image.validity(); validity != nullptr)
    {
        mask = validity->view();
    }
    return image.visit([&](const auto& typed) -> an::Result<CodeHistogram> {
        using ImageT = std::decay_t<decltype(typed)>;
        const std::uint32_t codes = codeCountOf(ImageT::kSampleType);
        if (codes == 0u)
        {
            return an::Status::failure(an::ErrorCode::Unsupported, "only an eight- or sixteen-bit image's codes are "
                                                                   "counted one by one");
        }
        const auto view = typed.view();
        if (!view.has_value())
        {
            return an::Status::failure(an::ErrorCode::NotInitialized, "the image holds no samples to count");
        }
        an::StatisticsSelection selection;
        selection.validity = typed.metadata().semantics().validity();
        selection.mask = mask.has_value() ? &*mask : nullptr;
        // Over the representation's own extremes as many bins as codes give every code a bin of its own.
        an::ImageHistogramRequest everyCode;
        everyCode.domain = an::HistogramDomain::RepresentationFullScale;
        CodeHistogram counted;
        counted.sampleType = ImageT::kSampleType;
        const an::ImageLayout layout = view->layout();
        for (std::uint32_t channel = 0u; channel < layout.extent().channelCount(); ++channel)
        {
            std::vector<std::uint64_t> bins(codes, 0u);
            const an::Result<an::ImageHistogram> histogram = an::computeImageHistogram(
                execution, ImageT::kSampleType, view->data(), layout, channel, selection, everyCode, bins);
            if (!histogram.hasValue() && histogram.status().code() != an::ErrorCode::EmptySelection)
            {
                return histogram.status();
            }
            counted.channels.push_back(std::move(bins));
        }
        return counted;
    });
}

an::Result<HistogramDisplayData> remapHistogramDisplayData(const an::EngineContext& engine,
                                                           const an::ExecutionContext& execution,
                                                           const CodeHistogram& source,
                                                           const an::consumer::AssetReadLease& mapped,
                                                           const HistogramResolution resolution)
{
    const std::uint32_t codes = codeCountOf(source.sampleType);
    if (codes == 0u || source.channels.empty())
    {
        return an::Status::failure(an::ErrorCode::IncompatibleArguments, "there are no codes to remap");
    }
    return mapped.visit([&](const auto& typed) -> an::Result<HistogramDisplayData> {
        using ImageT = std::decay_t<decltype(typed)>;
        using SampleT = typename ImageT::Sample;
        const auto view = typed.view();
        if constexpr (!std::is_integral_v<SampleT> || sizeof(SampleT) > 2u)
        {
            return an::Status::failure(an::ErrorCode::IncompatibleArguments,
                                       "only an eight- or sixteen-bit image's codes are remapped");
        }
        else
        {
            if (ImageT::kSampleType != source.sampleType || !view.has_value() || view->width() != codes ||
                view->height() != 1u || view->layout().extent().channelCount() != source.channels.size())
            {
                return an::Status::failure(an::ErrorCode::IncompatibleArguments,
                                           "the mapped codes are not one row of every code in each channel");
            }
            const auto lowest = static_cast<std::int64_t>(std::numeric_limits<SampleT>::lowest());

            // The bins every code falls in, as the display count itself decides them: one row of every code in order,
            // counted. The bins partition the codes in order, so their counts are the lengths of consecutive runs, and
            // the clipped counts are how many of the lowest and highest codes count as clipped.
            std::vector<SampleT> everyCode(codes);
            for (std::uint32_t index = 0u; index < codes; ++index)
            {
                everyCode[index] = static_cast<SampleT>(lowest + static_cast<std::int64_t>(index));
            }
            const std::optional<an::ImageExtent> rowExtent = an::ImageExtent::create(codes, 1u, 1u, engine.limits());
            const std::optional<an::ImageLayout> rowLayout =
                rowExtent.has_value()
                    ? an::ImageLayout::create(an::ImageLayoutRequest{.extent = *rowExtent,
                                                                     .elementSizeBytes = sizeof(SampleT),
                                                                     .samplesPerPixel = 1u,
                                                                     .planeCount = 1u,
                                                                     .rowAlignmentBytes = 1u},
                                              engine.limits())
                    : std::nullopt;
            if (!rowLayout.has_value())
            {
                return an::Status::failure(an::ErrorCode::IncompatibleArguments, "the row of codes cannot be laid out");
            }
            an::Result<HistogramDisplayData> partition =
                computeHistogramDisplayData(execution, ImageT::kSampleType, everyCode.data(), *rowLayout, resolution);
            if (!partition.hasValue())
            {
                return partition.status();
            }
            const HistogramDisplayChannel& runs = partition->channels.front();
            std::vector<std::uint32_t> binOfCode(codes, 0u);
            std::uint64_t assigned = 0u;
            for (std::size_t bin = 0u; bin < runs.bins.size(); ++bin)
            {
                for (std::uint64_t run = 0u; run < runs.bins[bin] && assigned < codes; ++run)
                {
                    binOfCode[assigned++] = static_cast<std::uint32_t>(bin);
                }
            }
            if (assigned != codes)
            {
                return an::Status::failure(an::ErrorCode::Internal, "the display bins do not hold every code once");
            }
            const std::uint64_t clippedLowCodes = runs.clippedLow;
            const std::uint64_t clippedHighCodes = runs.clippedHigh;

            // Every code's count goes to the bin its image falls in.
            HistogramDisplayData data;
            const std::uint32_t perPixel = view->samplesPerPixel();
            data.channels.reserve(source.channels.size());
            for (std::uint32_t channel = 0u; channel < source.channels.size(); ++channel)
            {
                const std::vector<std::uint64_t>& counts = source.channels[channel];
                if (counts.size() != codes)
                {
                    return an::Status::failure(an::ErrorCode::IncompatibleArguments,
                                               "a channel's codes are not all counted");
                }
                const auto row = view->row(perPixel == 1u ? channel : 0u, 0u);
                if (!row.has_value())
                {
                    return an::Status::failure(an::ErrorCode::Internal, "a row of the mapped codes is missing");
                }
                HistogramDisplayChannel remapped;
                remapped.bins.assign(runs.bins.size(), 0u);
                for (std::uint32_t index = 0u; index < codes; ++index)
                {
                    const std::uint64_t count = counts[index];
                    if (count == 0u)
                    {
                        continue;
                    }
                    const SampleT image =
                        (*row)[static_cast<std::size_t>(index) * perPixel + (perPixel == 1u ? 0u : channel)];
                    const auto code = static_cast<std::uint32_t>(static_cast<std::int64_t>(image) - lowest);
                    remapped.bins[binOfCode[code]] += count;
                    if (code < clippedLowCodes)
                    {
                        remapped.clippedLow += count;
                    }
                    if (code >= codes - clippedHighCodes)
                    {
                        remapped.clippedHigh += count;
                    }
                    remapped.samples += count;
                }
                data.channels.push_back(std::move(remapped));
            }
            return data;
        }
    });
}

an::Result<HistogramDisplayData> replacedPartHistogram(const HistogramDisplayData& whole,
                                                       const HistogramDisplayData& part,
                                                       const HistogramDisplayData& replacement)
{
    const auto refused = [](const char* message) {
        return an::Status::failure(an::ErrorCode::IncompatibleArguments, message);
    };
    if (part.channels.size() != whole.channels.size() || replacement.channels.size() != whole.channels.size())
    {
        return refused("the counts are of different channels");
    }
    HistogramDisplayData replaced;
    replaced.channels.reserve(whole.channels.size());
    for (std::size_t channel = 0u; channel < whole.channels.size(); ++channel)
    {
        const HistogramDisplayChannel& from = whole.channels[channel];
        const HistogramDisplayChannel& out = part.channels[channel];
        const HistogramDisplayChannel& in = replacement.channels[channel];
        if (out.bins.size() != from.bins.size() || in.bins.size() != from.bins.size())
        {
            return refused("the counts are of different resolutions");
        }
        if (out.clippedLow > from.clippedLow || out.clippedHigh > from.clippedHigh ||
            out.notANumber > from.notANumber || out.samples > from.samples)
        {
            return refused("the part holds more samples than the whole image");
        }
        HistogramDisplayChannel counted;
        counted.bins.resize(from.bins.size());
        for (std::size_t bin = 0u; bin < from.bins.size(); ++bin)
        {
            if (out.bins[bin] > from.bins[bin])
            {
                return refused("the part holds more samples than the whole image");
            }
            counted.bins[bin] = from.bins[bin] - out.bins[bin] + in.bins[bin];
        }
        counted.clippedLow = from.clippedLow - out.clippedLow + in.clippedLow;
        counted.clippedHigh = from.clippedHigh - out.clippedHigh + in.clippedHigh;
        counted.notANumber = from.notANumber - out.notANumber + in.notANumber;
        counted.samples = from.samples - out.samples + in.samples;
        replaced.channels.push_back(std::move(counted));
    }
    return replaced;
}

} // namespace an::ui
