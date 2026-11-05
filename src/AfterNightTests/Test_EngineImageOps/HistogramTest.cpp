// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for image histograms, cumulative counts and percentile lookups.

#include <AfterNightEngine/Algorithms/Statistics/Histogram.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Image/ImageView.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <limits>
#include <numeric>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace
{

[[nodiscard]] an::EngineContext histogramEngine(std::uint32_t concurrency)
{
    an::EngineContextOptions options;
    options.maxConcurrency = concurrency;
    an::Result<an::EngineContext> made = an::EngineContext::create(options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::EngineContext{};
}

[[nodiscard]] an::ExecutionContext histogramExecution(const an::EngineContext& engine, std::uint32_t concurrency)
{
    an::ExecutionOptions options;
    options.maxConcurrency = concurrency;
    an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
}

[[nodiscard]] an::ImageLayout histogramLayout(an::SampleType type, std::uint32_t width, std::uint32_t height,
                                              std::uint32_t channelCount, std::uint32_t samplesPerPixel,
                                              std::uint32_t rowPaddingSamples = 0u)
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, channelCount, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const std::uint32_t elementBytes = an::sampleTypeSizeBytes(type);
    const an::ImageLayoutRequest request{.extent = *extent,
                                         .elementSizeBytes = elementBytes,
                                         .samplesPerPixel = samplesPerPixel,
                                         .planeCount = channelCount / samplesPerPixel,
                                         .rowAlignmentBytes = 1u};
    const std::uint64_t rowStride =
        (static_cast<std::uint64_t>(width) * samplesPerPixel + rowPaddingSamples) * elementBytes;
    const std::optional<an::ImageLayout> layout =
        an::ImageLayout::createWithStrides(request, rowStride, rowStride * height, an::defaultEngineLimits());
    EXPECT_TRUE(layout.has_value());
    return *layout;
}

[[nodiscard]] an::ValidityDescriptor histogramRule(an::ValidityRule rule)
{
    an::ValidityDescriptorRequest request;
    request.rule = rule;
    const std::optional<an::ValidityDescriptor> made = an::ValidityDescriptor::create(request);
    EXPECT_TRUE(made.has_value());
    return *made;
}

} // namespace

TEST(HistogramTest, AFullScaleSixteenBitHistogramGivesEveryCodeABinOfItsOwn)
{
    // The autostretch shape: sixteen-bit codes counted into a bin each, asked for by naming the
    // representation rather than by writing 65535 into the call.
    constexpr std::uint32_t kWidth = 6u;
    const std::array<std::uint16_t, kWidth> samples{0u, 1u, 1u, 32768u, 65534u, 65535u};
    const an::ImageLayout layout = histogramLayout(an::SampleType::UInt16, kWidth, 1u, 1u, 1u);
    std::vector<std::uint64_t> bins(65536u, 7u);

    an::EngineContext engine = histogramEngine(4u);
    an::ExecutionContext execution = histogramExecution(engine, 4u);
    an::ImageHistogramRequest request;
    request.domain = an::HistogramDomain::RepresentationFullScale;
    const an::Result<an::ImageHistogram> histogram = an::computeImageHistogram(
        execution, an::SampleType::UInt16, samples.data(), layout, 0u, an::StatisticsSelection{}, request, bins);
    ASSERT_TRUE(histogram.hasValue()) << histogram.status().message();
    EXPECT_DOUBLE_EQ(histogram->lowest, 0.0);
    EXPECT_DOUBLE_EQ(histogram->highest, 65535.0);
    EXPECT_EQ(histogram->binCount, 65536u);
    EXPECT_EQ(histogram->counted, 6u);
    EXPECT_EQ(histogram->belowRange, 0u);
    EXPECT_EQ(histogram->aboveRange, 0u);
    EXPECT_EQ(histogram->notANumber, 0u);
    EXPECT_EQ(histogram->selection, (an::ValidityCount{.valid = 6u, .total = 6u}));
    for (const std::uint16_t code : samples)
    {
        EXPECT_GE(bins[code], 1u) << "code " << code;
    }
    EXPECT_EQ(bins[1], 2u);
    EXPECT_EQ(bins[0], 1u);
    EXPECT_EQ(bins[65535], 1u);
    EXPECT_EQ(std::accumulate(bins.begin(), bins.end(), std::uint64_t{0}), 6u);
}

TEST(HistogramTest, AMaskedRegionOfAnInterleavedChannelCountsOnlyWhatItSelects)
{
    constexpr std::uint32_t kWidth = 5u;
    constexpr std::uint32_t kHeight = 4u;
    constexpr std::uint32_t kChannels = 3u;
    const an::ImageLayout layout = histogramLayout(an::SampleType::Float32, kWidth, kHeight, kChannels, kChannels, 2u);
    std::vector<float> samples(static_cast<std::size_t>(layout.totalBytes() / sizeof(float)), -1.0f);
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            const std::optional<std::uint64_t> offset = layout.sampleByteOffset(0u, y, x, 1u);
            ASSERT_TRUE(offset.has_value());
            samples[static_cast<std::size_t>(*offset / sizeof(float))] = static_cast<float>(y * 10u + x) / 100.0f;
        }
    }

    const std::optional<an::ImageExtent> maskExtent =
        an::ImageExtent::create(kWidth, kHeight, 1u, an::defaultEngineLimits());
    ASSERT_TRUE(maskExtent.has_value());
    std::array<std::uint8_t, 4u> maskBytes{};
    const std::optional<an::ValidityMaskView<std::uint8_t>> writableMask =
        an::ValidityMaskView<std::uint8_t>::createTight(maskBytes, *maskExtent);
    ASSERT_TRUE(writableMask.has_value());
    writableMask->fill(true);
    ASSERT_TRUE(writableMask->setValid(0u, 2u, 1u, false));
    const an::ValidityMaskView<const std::uint8_t> mask = writableMask->constView();

    an::StatisticsSelection selection;
    selection.validity = histogramRule(an::ValidityRule::ExplicitMask);
    selection.mask = &mask;
    selection.region = an::StatisticsRegion{.originX = 1u, .originY = 1u, .width = 3u, .height = 2u};

    // Six pixels in the region, one masked away: 0.11, 0.13, 0.21, 0.22, 0.23 remain.
    an::ImageHistogramRequest request;
    request.lowest = 0.0;
    request.highest = 1.0;
    std::array<std::uint64_t, 10u> bins{};
    an::EngineContext engine = histogramEngine(3u);
    an::ExecutionContext execution = histogramExecution(engine, 3u);
    const an::Result<an::ImageHistogram> histogram = an::computeImageHistogram(
        execution, an::SampleType::Float32, samples.data(), layout, 1u, selection, request, bins);
    ASSERT_TRUE(histogram.hasValue()) << histogram.status().message();
    EXPECT_EQ(histogram->selection, (an::ValidityCount{.valid = 5u, .total = 6u}));
    EXPECT_EQ(histogram->counted, 5u);
    EXPECT_EQ(bins[1], 2u);
    EXPECT_EQ(bins[2], 3u);
    EXPECT_EQ(std::accumulate(bins.begin(), bins.end(), std::uint64_t{0}), 5u);
}

TEST(HistogramTest, SamplesOutsideTheIntervalAreCountedRatherThanFoldedIntoAnEndBin)
{
    constexpr std::uint32_t kWidth = 7u;
    const std::array<float, kWidth> samples{
        -5.0f, 0.0f, 0.5f, 1.0f, 5.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()};
    const an::ImageLayout layout = histogramLayout(an::SampleType::Float32, kWidth, 1u, 1u, 1u);
    std::array<std::uint64_t, 4u> bins{};
    an::EngineContext engine = histogramEngine(2u);
    an::ExecutionContext execution = histogramExecution(engine, 2u);
    const an::Result<an::ImageHistogram> histogram =
        an::computeImageHistogram(execution, an::SampleType::Float32, samples.data(), layout, 0u,
                                  an::StatisticsSelection{}, an::ImageHistogramRequest{}, bins);
    ASSERT_TRUE(histogram.hasValue()) << histogram.status().message();
    EXPECT_EQ(histogram->counted, 3u);
    EXPECT_EQ(histogram->belowRange, 1u);
    EXPECT_EQ(histogram->aboveRange, 2u);
    EXPECT_EQ(histogram->notANumber, 1u);
    // The upper endpoint belongs to the last bin rather than to one past the end, and nothing
    // outside the interval joined an end bin.
    EXPECT_EQ(bins[0], 1u);
    EXPECT_EQ(bins[2], 1u);
    EXPECT_EQ(bins[3], 1u);
    EXPECT_EQ(std::accumulate(bins.begin(), bins.end(), std::uint64_t{0}), 3u);
}

TEST(HistogramTest, TheBinsAreIdenticalAtEveryAdmittedWidth)
{
    constexpr std::uint32_t kWidth = 4099u;
    std::vector<float> samples(kWidth, 0.0f);
    std::uint64_t state = 0x9e3779b97f4a7c15ull;
    for (std::size_t index = 0u; index < samples.size(); ++index)
    {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        samples[index] = static_cast<float>(static_cast<std::uint32_t>(state >> 33u) % 4096u) / 4096.0f;
    }
    const an::ImageLayout layout = histogramLayout(an::SampleType::Float32, kWidth, 1u, 1u, 1u);

    std::vector<std::uint64_t> baseline;
    for (const std::uint32_t threads : {1u, 2u, 4u, 8u})
    {
        an::EngineContext engine = histogramEngine(threads);
        an::ExecutionContext execution = histogramExecution(engine, threads);
        std::vector<std::uint64_t> bins(1024u, 0u);
        const an::Result<an::ImageHistogram> histogram =
            an::computeImageHistogram(execution, an::SampleType::Float32, samples.data(), layout, 0u,
                                      an::StatisticsSelection{}, an::ImageHistogramRequest{}, bins);
        ASSERT_TRUE(histogram.hasValue()) << histogram.status().message();
        EXPECT_EQ(histogram->counted, kWidth);
        if (baseline.empty())
        {
            baseline = bins;
            continue;
        }
        EXPECT_EQ(bins, baseline) << "at " << threads << " threads";
    }
}

TEST(HistogramTest, ARefusedCallLeavesTheCallersCountersAsTheyWere)
{
    const an::ImageLayout layout = histogramLayout(an::SampleType::Float32, 4u, 1u, 1u, 1u);
    const std::array<float, 4u> samples{0.1f, 0.2f, 0.3f, 0.4f};
    an::EngineContext engine = histogramEngine(2u);
    an::ExecutionContext execution = histogramExecution(engine, 2u);

    std::array<std::uint64_t, 4u> bins{11u, 12u, 13u, 14u};
    an::ImageHistogramRequest unknownDomain;
    unknownDomain.domain = static_cast<an::HistogramDomain>(9u);
    const an::Result<an::ImageHistogram> refused = an::computeImageHistogram(
        execution, an::SampleType::Float32, samples.data(), layout, 0u, an::StatisticsSelection{}, unknownDomain, bins);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(bins, (std::array<std::uint64_t, 4u>{11u, 12u, 13u, 14u}));

    an::ImageHistogramRequest inverted;
    inverted.lowest = 1.0;
    inverted.highest = 0.0;
    const an::Result<an::ImageHistogram> backwards = an::computeImageHistogram(
        execution, an::SampleType::Float32, samples.data(), layout, 0u, an::StatisticsSelection{}, inverted, bins);
    ASSERT_FALSE(backwards.hasValue());
    EXPECT_EQ(backwards.status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(bins, (std::array<std::uint64_t, 4u>{11u, 12u, 13u, 14u}));

    const an::Result<an::ImageHistogram> noBins =
        an::computeImageHistogram(execution, an::SampleType::Float32, samples.data(), layout, 0u,
                                  an::StatisticsSelection{}, an::ImageHistogramRequest{}, std::span<std::uint64_t>{});
    ASSERT_FALSE(noBins.hasValue());
    EXPECT_EQ(noBins.status().code(), an::ErrorCode::InvalidArgument);
}

TEST(HistogramTest, AnEmptySelectionAndAnEmptyRegionBothCarryTheirCounts)
{
    const an::ImageLayout layout = histogramLayout(an::SampleType::Float32, 7u, 1u, 1u, 1u);
    const std::array<float, 7u> samples{};
    std::array<std::uint64_t, 4u> bins{};
    an::EngineContext engine = histogramEngine(2u);
    an::ExecutionContext execution = histogramExecution(engine, 2u);

    an::StatisticsSelection filtered;
    filtered.validity = histogramRule(an::ValidityRule::ZeroIsNoData);
    const an::Result<an::ImageHistogram> nothing = an::computeImageHistogram(
        execution, an::SampleType::Float32, samples.data(), layout, 0u, filtered, an::ImageHistogramRequest{}, bins);
    ASSERT_FALSE(nothing.hasValue());
    EXPECT_EQ(nothing.status().code(), an::ErrorCode::EmptySelection);

    an::StatisticsSelection emptyRegion;
    emptyRegion.region = an::StatisticsRegion{.originX = 3u, .originY = 0u, .width = 0u, .height = 1u};
    const an::Result<an::ImageHistogram> none = an::computeImageHistogram(
        execution, an::SampleType::Float32, samples.data(), layout, 0u, emptyRegion, an::ImageHistogramRequest{}, bins);
    ASSERT_FALSE(none.hasValue());
    EXPECT_EQ(none.status().code(), an::ErrorCode::EmptySelection);
    const an::StatusContextField* completed = none.status().findContext(an::ContextKey::Completed);
    const an::StatusContextField* total = none.status().findContext(an::ContextKey::Total);
    ASSERT_NE(completed, nullptr);
    ASSERT_NE(total, nullptr);
    EXPECT_EQ(completed->count, 0u);
    EXPECT_EQ(total->count, 0u);
}

TEST(HistogramTest, CumulativeCountsAccumulateInPlaceAndRefuseAPartialOverlap)
{
    std::array<std::uint64_t, 5u> bins{2u, 0u, 3u, 1u, 4u};
    std::array<std::uint64_t, 5u> cumulative{};
    ASSERT_FALSE(an::accumulateCumulativeCounts(bins, cumulative).failed());
    EXPECT_EQ(cumulative, (std::array<std::uint64_t, 5u>{2u, 2u, 5u, 6u, 10u}));

    std::array<std::uint64_t, 5u> inPlace{2u, 0u, 3u, 1u, 4u};
    ASSERT_FALSE(an::accumulateCumulativeCounts(inPlace, inPlace).failed());
    EXPECT_EQ(inPlace, cumulative);

    EXPECT_EQ(an::accumulateCumulativeCounts(std::span<const std::uint64_t>{}, std::span<std::uint64_t>{}).code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::accumulateCumulativeCounts(bins, std::span<std::uint64_t>{cumulative}.first(4u)).code(),
              an::ErrorCode::IncompatibleArguments);
    const std::span<std::uint64_t> shifted{bins.data() + 1u, 4u};
    EXPECT_EQ(an::accumulateCumulativeCounts(std::span<const std::uint64_t>{bins}.first(4u), shifted).code(),
              an::ErrorCode::InvalidArgument);

    std::array<std::uint64_t, 2u> huge{(std::numeric_limits<std::uint64_t>::max)(), 1u};
    std::array<std::uint64_t, 2u> hugeCumulative{};
    EXPECT_EQ(an::accumulateCumulativeCounts(huge, hugeCumulative).code(), an::ErrorCode::InvalidData);
}

TEST(HistogramTest, AQuantileResolvesToTheBinTheRankFallsInUnderEachStatedRule)
{
    an::ImageHistogram histogram;
    histogram.lowest = 0.0;
    histogram.highest = 1.0;
    histogram.binCount = 4u;
    // Ten samples: two in [0, 0.25], none in the second bin, three in the third, five in the last.
    const std::array<std::uint64_t, 4u> bins{2u, 0u, 3u, 5u};
    std::array<std::uint64_t, 4u> cumulative{};
    ASSERT_FALSE(an::accumulateCumulativeCounts(bins, cumulative).failed());

    // Rank 5 of 10 falls in the third bin, which holds ranks 3 through 5.
    const an::Result<double> lower =
        an::histogramQuantileValue(histogram, cumulative, 0.5, an::HistogramQuantileRule::LowerEdge);
    ASSERT_TRUE(lower.hasValue()) << lower.status().message();
    EXPECT_DOUBLE_EQ(*lower, 0.5);
    const an::Result<double> centre =
        an::histogramQuantileValue(histogram, cumulative, 0.5, an::HistogramQuantileRule::BinCenter);
    ASSERT_TRUE(centre.hasValue()) << centre.status().message();
    EXPECT_DOUBLE_EQ(*centre, 0.625);
    const an::Result<double> interpolated =
        an::histogramQuantileValue(histogram, cumulative, 0.5, an::HistogramQuantileRule::Interpolated);
    ASSERT_TRUE(interpolated.hasValue()) << interpolated.status().message();
    EXPECT_DOUBLE_EQ(*interpolated, 0.5 + 0.25 * (5.0 - 2.0) / 3.0);

    for (const an::HistogramQuantileRule rule :
         {an::HistogramQuantileRule::LowerEdge, an::HistogramQuantileRule::BinCenter,
          an::HistogramQuantileRule::Interpolated})
    {
        const an::Result<double> bottom = an::histogramQuantileValue(histogram, cumulative, 0.0, rule);
        const an::Result<double> top = an::histogramQuantileValue(histogram, cumulative, 1.0, rule);
        ASSERT_TRUE(bottom.hasValue());
        ASSERT_TRUE(top.hasValue());
        EXPECT_DOUBLE_EQ(*bottom, 0.0);
        EXPECT_DOUBLE_EQ(*top, 1.0);
    }

    EXPECT_EQ(
        an::histogramQuantileValue(histogram, cumulative, 1.5, an::HistogramQuantileRule::LowerEdge).status().code(),
        an::ErrorCode::OutOfRange);
    EXPECT_EQ(an::histogramQuantileValue(histogram, cumulative, 0.5, static_cast<an::HistogramQuantileRule>(9u))
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::histogramQuantileValue(histogram, std::span<const std::uint64_t>{cumulative}.first(3u), 0.5,
                                         an::HistogramQuantileRule::LowerEdge)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);

    const std::array<std::uint64_t, 4u> nothing{};
    EXPECT_EQ(an::histogramQuantileValue(histogram, nothing, 0.5, an::HistogramQuantileRule::LowerEdge).status().code(),
              an::ErrorCode::EmptySelection);
}

TEST(HistogramTest, TheTypedSpellingReadsItsRepresentationAndLayoutFromTheView)
{
    const an::ImageLayout layout = histogramLayout(an::SampleType::Float32, 4u, 1u, 1u, 1u);
    const std::array<float, 4u> samples{0.1f, 0.2f, 0.6f, 0.9f};
    const std::optional<an::ImageView<const float>> view = an::ImageView<const float>::create(samples, layout);
    ASSERT_TRUE(view.has_value());
    an::EngineContext engine = histogramEngine(2u);
    an::ExecutionContext execution = histogramExecution(engine, 2u);

    std::array<std::uint64_t, 2u> bins{};
    const an::Result<an::ImageHistogram> histogram =
        an::computeHistogram(execution, *view, 0u, an::StatisticsSelection{}, an::ImageHistogramRequest{}, bins);
    ASSERT_TRUE(histogram.hasValue()) << histogram.status().message();
    EXPECT_EQ(histogram->counted, 4u);
    EXPECT_EQ(bins[0], 2u);
    EXPECT_EQ(bins[1], 2u);
}
