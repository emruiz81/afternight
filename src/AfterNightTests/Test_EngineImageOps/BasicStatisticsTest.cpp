// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for count, extrema and stable moments over image selections.

#include <AfterNightEngine/Algorithms/Statistics/BasicStatistics.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Image/ImageView.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/ExecutionPolicy.h>

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace
{

[[nodiscard]] an::EngineContext makeEngine(std::uint32_t concurrency)
{
    an::EngineContextOptions options;
    options.maxConcurrency = concurrency;
    an::Result<an::EngineContext> made = an::EngineContext::create(options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::EngineContext{};
}

[[nodiscard]] an::ExecutionContext
makeExecution(const an::EngineContext& engine, std::uint32_t concurrency,
              an::ReductionDeterminism determinism = an::ReductionDeterminism::DecisionFeeding)
{
    an::ExecutionOptions options;
    options.maxConcurrency = concurrency;
    options.reductionDeterminism = determinism;
    an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
}

[[nodiscard]] an::ImageLayout layoutOf(std::uint32_t width, std::uint32_t height, std::uint32_t channelCount,
                                       std::uint32_t samplesPerPixel, std::uint32_t rowPaddingSamples = 0u)
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, channelCount, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const an::ImageLayoutRequest request{.extent = *extent,
                                         .elementSizeBytes = sizeof(float),
                                         .samplesPerPixel = samplesPerPixel,
                                         .planeCount = channelCount / samplesPerPixel,
                                         .rowAlignmentBytes = 1u};
    const std::uint64_t rowStride =
        (static_cast<std::uint64_t>(width) * samplesPerPixel + rowPaddingSamples) * sizeof(float);
    const std::optional<an::ImageLayout> layout =
        an::ImageLayout::createWithStrides(request, rowStride, rowStride * height, an::defaultEngineLimits());
    EXPECT_TRUE(layout.has_value());
    return *layout;
}

[[nodiscard]] std::vector<float> channelPicture(const an::ImageLayout& layout)
{
    std::vector<float> samples(static_cast<std::size_t>(layout.totalBytes() / sizeof(float)), -1000.0f);
    for (std::uint32_t channel = 0u; channel < layout.extent().channelCount(); ++channel)
    {
        const std::uint32_t plane = channel / layout.samplesPerPixel();
        const std::uint32_t within = channel % layout.samplesPerPixel();
        for (std::uint32_t y = 0u; y < layout.extent().height(); ++y)
        {
            for (std::uint32_t x = 0u; x < layout.extent().width(); ++x)
            {
                const std::optional<std::uint64_t> offset = layout.sampleByteOffset(plane, y, x, within);
                EXPECT_TRUE(offset.has_value());
                samples[static_cast<std::size_t>(*offset / sizeof(float))] =
                    static_cast<float>(channel * 100u + y * 10u + x);
            }
        }
    }
    return samples;
}

[[nodiscard]] an::ValidityDescriptor explicitMaskValidity()
{
    an::ValidityDescriptorRequest request;
    request.rule = an::ValidityRule::ExplicitMask;
    const std::optional<an::ValidityDescriptor> made = an::ValidityDescriptor::create(request);
    EXPECT_TRUE(made.has_value());
    return *made;
}

[[nodiscard]] an::ValidityDescriptor finiteValidity()
{
    an::ValidityDescriptorRequest request;
    request.rule = an::ValidityRule::FiniteOnly;
    const std::optional<an::ValidityDescriptor> made = an::ValidityDescriptor::create(request);
    EXPECT_TRUE(made.has_value());
    return *made;
}

} // namespace

TEST(BasicStatisticsTest, AMaskedRegionOfAnInterleavedChannelNeedsNoCompactedImage)
{
    constexpr std::uint32_t kWidth = 5u;
    constexpr std::uint32_t kHeight = 4u;
    constexpr std::uint32_t kChannels = 3u;
    const an::ImageLayout layout = layoutOf(kWidth, kHeight, kChannels, kChannels, 2u);
    const std::vector<float> samples = channelPicture(layout);

    const std::optional<an::ImageExtent> maskExtent =
        an::ImageExtent::create(kWidth, kHeight, kChannels, an::defaultEngineLimits());
    ASSERT_TRUE(maskExtent.has_value());
    std::array<std::uint8_t, 12u> maskBytes{};
    const std::optional<an::ValidityMaskView<std::uint8_t>> writableMask =
        an::ValidityMaskView<std::uint8_t>::createTight(maskBytes, *maskExtent);
    ASSERT_TRUE(writableMask.has_value());
    writableMask->fill(true);
    ASSERT_TRUE(writableMask->setValid(1u, 2u, 1u, false));
    ASSERT_TRUE(writableMask->setValid(1u, 2u, 2u, false));
    const an::ValidityMaskView<const std::uint8_t> mask = writableMask->constView();

    an::StatisticsSelection selection;
    selection.validity = explicitMaskValidity();
    selection.mask = &mask;
    selection.region = an::StatisticsRegion{.originX = 1u, .originY = 1u, .width = 3u, .height = 2u};

    an::EngineContext engine = makeEngine(4u);
    an::ExecutionContext execution = makeExecution(engine, 4u);
    const an::Result<an::BasicStatistics> statistics =
        an::computeImageBasicStatistics(execution, an::SampleType::Float32, samples.data(), layout, 1u, selection);
    ASSERT_TRUE(statistics.hasValue()) << statistics.status().message();
    EXPECT_EQ(statistics->selection, (an::ValidityCount{.valid = 4u, .total = 6u}));
    EXPECT_DOUBLE_EQ(statistics->minimum, 111.0);
    EXPECT_DOUBLE_EQ(statistics->maximum, 123.0);
    EXPECT_DOUBLE_EQ(statistics->sum, 468.0);
    EXPECT_DOUBLE_EQ(statistics->mean, 117.0);
    EXPECT_DOUBLE_EQ(statistics->sumSquaredDeviations, 104.0);
    EXPECT_DOUBLE_EQ(statistics->populationVariance, 26.0);
    EXPECT_DOUBLE_EQ(statistics->populationStandardDeviation, std::sqrt(26.0));
}

TEST(BasicStatisticsTest, ExtremaAreInitializedFromDataOutsideTheNormalizedRange)
{
    const an::ImageLayout layout = layoutOf(3u, 1u, 1u, 1u);
    an::EngineContext engine = makeEngine(1u);
    an::ExecutionContext execution = makeExecution(engine, 1u);

    const std::array<float, 3u> positive{2.0f, 7.0f, 4.0f};
    const an::Result<an::BasicStatistics> positiveStatistics = an::computeImageBasicStatistics(
        execution, an::SampleType::Float32, positive.data(), layout, 0u, an::StatisticsSelection{});
    ASSERT_TRUE(positiveStatistics.hasValue()) << positiveStatistics.status().message();
    EXPECT_DOUBLE_EQ(positiveStatistics->minimum, 2.0);
    EXPECT_DOUBLE_EQ(positiveStatistics->maximum, 7.0);

    const std::array<float, 3u> negative{-2.0f, -7.0f, -4.0f};
    const an::Result<an::BasicStatistics> negativeStatistics = an::computeImageBasicStatistics(
        execution, an::SampleType::Float32, negative.data(), layout, 0u, an::StatisticsSelection{});
    ASSERT_TRUE(negativeStatistics.hasValue()) << negativeStatistics.status().message();
    EXPECT_DOUBLE_EQ(negativeStatistics->minimum, -7.0);
    EXPECT_DOUBLE_EQ(negativeStatistics->maximum, -2.0);
}

TEST(BasicStatisticsTest, AnEmptySelectionCarriesBothCountsInsteadOfPublishingSeedValues)
{
    const an::ImageLayout layout = layoutOf(7u, 1u, 1u, 1u);
    const std::array<float, 7u> samples{};
    an::ValidityDescriptorRequest request;
    request.rule = an::ValidityRule::ZeroIsNoData;
    an::StatisticsSelection selection;
    selection.validity = *an::ValidityDescriptor::create(request);

    an::EngineContext engine = makeEngine(2u);
    an::ExecutionContext execution = makeExecution(engine, 2u);
    const an::Result<an::BasicStatistics> statistics =
        an::computeImageBasicStatistics(execution, an::SampleType::Float32, samples.data(), layout, 0u, selection);
    ASSERT_FALSE(statistics.hasValue());
    EXPECT_EQ(statistics.status().code(), an::ErrorCode::EmptySelection);
    const an::StatusContextField* completed = statistics.status().findContext(an::ContextKey::Completed);
    const an::StatusContextField* total = statistics.status().findContext(an::ContextKey::Total);
    ASSERT_NE(completed, nullptr);
    ASSERT_NE(total, nullptr);
    EXPECT_EQ(completed->count, 0u);
    EXPECT_EQ(total->count, 7u);
}

TEST(BasicStatisticsTest, AnEmptyRegionIsTheSameEmptySelectionWithZeroTotal)
{
    const an::ImageLayout layout = layoutOf(7u, 1u, 1u, 1u);
    const std::array<float, 7u> samples{};
    an::StatisticsSelection selection;
    selection.region = an::StatisticsRegion{.originX = 3u, .originY = 0u, .width = 0u, .height = 1u};

    an::EngineContext engine = makeEngine(2u);
    an::ExecutionContext execution = makeExecution(engine, 2u);
    const an::Result<an::BasicStatistics> statistics =
        an::computeImageBasicStatistics(execution, an::SampleType::Float32, samples.data(), layout, 0u, selection);
    ASSERT_FALSE(statistics.hasValue());
    EXPECT_EQ(statistics.status().code(), an::ErrorCode::EmptySelection);
    const an::StatusContextField* completed = statistics.status().findContext(an::ContextKey::Completed);
    const an::StatusContextField* total = statistics.status().findContext(an::ContextKey::Total);
    ASSERT_NE(completed, nullptr);
    ASSERT_NE(total, nullptr);
    EXPECT_EQ(completed->count, 0u);
    EXPECT_EQ(total->count, 0u);
}

TEST(BasicStatisticsTest, NonFiniteSamplesAreEitherRefusedOrExcludedByTheNamedRule)
{
    const an::ImageLayout layout = layoutOf(5u, 1u, 1u, 1u);
    const std::array<float, 5u> samples{1.0f, std::numeric_limits<float>::quiet_NaN(), 3.0f,
                                        std::numeric_limits<float>::infinity(), 5.0f};
    an::EngineContext engine = makeEngine(2u);
    an::ExecutionContext execution = makeExecution(engine, 2u);

    const an::Result<an::BasicStatistics> refused = an::computeImageBasicStatistics(
        execution, an::SampleType::Float32, samples.data(), layout, 0u, an::StatisticsSelection{});
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidData);

    an::StatisticsSelection selection;
    selection.validity = finiteValidity();
    const an::Result<an::BasicStatistics> excluded =
        an::computeImageBasicStatistics(execution, an::SampleType::Float32, samples.data(), layout, 0u, selection);
    ASSERT_TRUE(excluded.hasValue()) << excluded.status().message();
    EXPECT_EQ(excluded->selection, (an::ValidityCount{.valid = 3u, .total = 5u}));
    EXPECT_DOUBLE_EQ(excluded->sum, 9.0);
    EXPECT_DOUBLE_EQ(excluded->mean, 3.0);
}

TEST(BasicStatisticsTest, BothExecutionPoliciesAreBitIdenticalAtEveryAdmittedWidth)
{
    constexpr std::uint32_t kWidth = 4099u;
    const an::ImageLayout layout = layoutOf(kWidth, 1u, 1u, 1u);
    std::array<float, kWidth> samples{};
    for (std::size_t index = 0u; index < samples.size(); ++index)
    {
        samples[index] = static_cast<float>(static_cast<int>(index % 29u) - 14) * 0.03125f;
    }

    std::optional<an::BasicStatistics> baseline;
    for (const an::ReductionDeterminism determinism :
         {an::ReductionDeterminism::DecisionFeeding, an::ReductionDeterminism::All})
    {
        for (const std::uint32_t threads : {1u, 2u, 4u})
        {
            an::EngineContext engine = makeEngine(threads);
            an::ExecutionContext execution = makeExecution(engine, threads, determinism);
            const an::Result<an::BasicStatistics> statistics = an::computeImageBasicStatistics(
                execution, an::SampleType::Float32, samples.data(), layout, 0u, an::StatisticsSelection{});
            ASSERT_TRUE(statistics.hasValue()) << statistics.status().message();
            if (!baseline.has_value())
            {
                baseline = *statistics;
                continue;
            }
            EXPECT_EQ(std::bit_cast<std::uint64_t>(statistics->sum), std::bit_cast<std::uint64_t>(baseline->sum));
            EXPECT_EQ(std::bit_cast<std::uint64_t>(statistics->mean), std::bit_cast<std::uint64_t>(baseline->mean));
            EXPECT_EQ(std::bit_cast<std::uint64_t>(statistics->populationVariance),
                      std::bit_cast<std::uint64_t>(baseline->populationVariance));
            EXPECT_EQ(std::bit_cast<std::uint64_t>(statistics->populationStandardDeviation),
                      std::bit_cast<std::uint64_t>(baseline->populationStandardDeviation));
        }
    }
}

TEST(BasicStatisticsTest, TheTypedSpellingReadsItsRepresentationAndLayoutFromTheView)
{
    const an::ImageLayout layout = layoutOf(4u, 1u, 1u, 1u);
    const std::array<float, 4u> samples{1.0f, 2.0f, 3.0f, 4.0f};
    const std::optional<an::ImageView<const float>> view = an::ImageView<const float>::create(samples, layout);
    ASSERT_TRUE(view.has_value());
    an::EngineContext engine = makeEngine(2u);
    an::ExecutionContext execution = makeExecution(engine, 2u);

    const an::Result<an::BasicStatistics> statistics =
        an::computeBasicStatistics(execution, *view, 0u, an::StatisticsSelection{});
    ASSERT_TRUE(statistics.hasValue()) << statistics.status().message();
    EXPECT_EQ(statistics->selection.valid, 4u);
    EXPECT_DOUBLE_EQ(statistics->mean, 2.5);
    EXPECT_DOUBLE_EQ(statistics->populationVariance, 1.25);
}
