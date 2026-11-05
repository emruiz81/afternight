// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the advanced image statistics.

#include <AfterNightEngine/Algorithms/Statistics/AdvancedStatistics.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <optional>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace
{

[[nodiscard]] an::EngineContext makeEngine()
{
    an::EngineContextOptions options;
    options.maxConcurrency = 4u;
    an::Result<an::EngineContext> made = an::EngineContext::create(options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::EngineContext{};
}

[[nodiscard]] an::ExecutionContext makeExecution(const an::EngineContext& engine)
{
    an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, {});
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
}

[[nodiscard]] an::ImageLayout layoutOf(std::uint32_t width, std::uint32_t height)
{
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(width, height, 1u, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const std::optional<an::ImageLayout> layout =
        an::ImageLayout::create(an::ImageLayoutRequest{.extent = *extent,
                                                       .elementSizeBytes = sizeof(float),
                                                       .samplesPerPixel = 1u,
                                                       .planeCount = 1u,
                                                       .rowAlignmentBytes = 1u},
                                an::defaultEngineLimits());
    EXPECT_TRUE(layout.has_value());
    return *layout;
}

TEST(AdvancedStatisticsTest, SquaresAreSquaresAndDeviationUsesTheArithmeticMean)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout layout = layoutOf(4u, 1u);
    const std::array<float, 4u> samples{1.0f, 2.0f, 3.0f, 4.0f};

    const an::Result<an::AdvancedStatistics> statistics =
        an::computeImageAdvancedStatistics(execution, an::SampleType::Float32, samples.data(), layout, 0u);
    ASSERT_TRUE(statistics.hasValue()) << statistics.status().message();
    EXPECT_EQ(statistics->selection.valid, 4u);
    EXPECT_DOUBLE_EQ(statistics->mean, 2.5);
    EXPECT_DOUBLE_EQ(statistics->median, 2.5);
    EXPECT_DOUBLE_EQ(statistics->medianAbsoluteDeviation, 1.0);
    EXPECT_DOUBLE_EQ(statistics->averageAbsoluteDeviation, 1.0);
    EXPECT_DOUBLE_EQ(statistics->sumOfSquares, 30.0);
    EXPECT_DOUBLE_EQ(statistics->meanOfSquares, 7.5);
    EXPECT_GT(statistics->biweightMidvariance, 0.0);
    EXPECT_DOUBLE_EQ(statistics->backgroundNoise, 0.0);
    EXPECT_GT(statistics->planeScans, 0u);
}

TEST(AdvancedStatisticsTest, BackgroundNoiseUsesTheMedianOfClippedRowDifferenceDeviations)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout layout = layoutOf(4u, 2u);
    const std::array<float, 8u> samples{0.0f, 1.0f, 3.0f, 6.0f, 10.0f, 11.0f, 13.0f, 16.0f};

    const an::Result<an::AdvancedStatistics> statistics =
        an::computeImageAdvancedStatistics(execution, an::SampleType::Float32, samples.data(), layout, 0u);
    ASSERT_TRUE(statistics.hasValue()) << statistics.status().message();
    EXPECT_NEAR(statistics->backgroundNoise, std::sqrt(1.0 / 3.0), 1.0e-12);
}

TEST(AdvancedStatisticsTest, RegionAndExplicitValiditySelectEveryEstimatorTogether)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout layout = layoutOf(5u, 1u);
    const std::array<float, 5u> samples{100.0f, 1.0f, 0.0f, 3.0f, 100.0f};
    const std::optional<an::ValidityDescriptor> validity =
        an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::ZeroIsNoData});
    ASSERT_TRUE(validity.has_value());
    an::StatisticsSelection selection;
    selection.validity = *validity;
    selection.region = an::StatisticsRegion{.originX = 1u, .originY = 0u, .width = 3u, .height = 1u};

    const an::Result<an::AdvancedStatistics> statistics =
        an::computeImageAdvancedStatistics(execution, an::SampleType::Float32, samples.data(), layout, 0u, selection);
    ASSERT_TRUE(statistics.hasValue()) << statistics.status().message();
    EXPECT_EQ(statistics->selection.total, 3u);
    EXPECT_EQ(statistics->selection.valid, 2u);
    EXPECT_DOUBLE_EQ(statistics->mean, 2.0);
    EXPECT_DOUBLE_EQ(statistics->sumOfSquares, 10.0);
    EXPECT_DOUBLE_EQ(statistics->averageAbsoluteDeviation, 1.0);
}

TEST(AdvancedStatisticsTest, IterativeKsigmaUsesTheSixMadBiweightEstimator)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout layout = layoutOf(6u, 1u);
    const std::array<float, 6u> samples{0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 100.0f};

    const an::Result<an::AdvancedStatistics> statistics =
        an::computeImageAdvancedStatistics(execution, an::SampleType::Float32, samples.data(), layout, 0u);
    ASSERT_TRUE(statistics.hasValue()) << statistics.status().message();
    EXPECT_DOUBLE_EQ(statistics->median, 2.5);
    EXPECT_DOUBLE_EQ(statistics->medianAbsoluteDeviation, 1.5);
    EXPECT_EQ(statistics->iterativeKsigmaSelection.valid, 5u);
    EXPECT_EQ(statistics->iterativeKsigmaSelection.total, 6u);
    EXPECT_DOUBLE_EQ(statistics->iterativeKsigmaLocation, 2.0);

    double numerator = 0.0;
    double denominator = 0.0;
    for (double value : std::array<double, 5u>{0.0, 1.0, 2.0, 3.0, 4.0})
    {
        const double distance = value - 2.0;
        const double normalized = distance / 9.0;
        const double normalizedSquared = normalized * normalized;
        const double retained = 1.0 - normalizedSquared;
        const double weightedDistance = distance * retained * retained;
        numerator += weightedDistance * weightedDistance;
        denominator += retained * (1.0 - 5.0 * normalizedSquared);
    }
    const double expected = std::sqrt(5.0 * numerator / (denominator * denominator)) * 0.991;
    EXPECT_NEAR(statistics->iterativeKsigmaScale, expected, 1.0e-12);
}

TEST(AdvancedStatisticsTest, ParallelPartitionsPreserveEveryOutputAcrossWorkerCounts)
{
    constexpr std::uint32_t width = 257u;
    constexpr std::uint32_t height = 97u;
    const auto layout = layoutOf(width, height);
    std::vector<float> samples(static_cast<std::size_t>(width) * height);
    double expectedSquares = 0.0;
    for (std::size_t index = 0u; index < samples.size(); ++index)
    {
        samples[index] = static_cast<float>((index * 337u) % 257u) / 256.0f;
        expectedSquares += static_cast<double>(samples[index]) * samples[index];
    }
    const auto engine = makeEngine();
    std::optional<std::array<std::uint64_t, 10u>> baseline;
    for (auto workers : {1u, 2u, 4u})
    {
        auto execution = an::ExecutionContext::create(engine, an::ExecutionOptions{.maxConcurrency = workers});
        ASSERT_TRUE(execution.hasValue());
        const auto result =
            an::computeImageAdvancedStatistics(*execution, an::SampleType::Float32, samples.data(), layout, 0u);
        ASSERT_TRUE(result.hasValue()) << result.status().message();
        EXPECT_EQ(result->sumOfSquares, expectedSquares);
        EXPECT_EQ(result->selection.valid, samples.size());
        const std::array<double, 10u> values{result->mean,
                                             result->median,
                                             result->medianAbsoluteDeviation,
                                             result->averageAbsoluteDeviation,
                                             result->sumOfSquares,
                                             result->meanOfSquares,
                                             result->biweightMidvariance,
                                             result->backgroundNoise,
                                             result->iterativeKsigmaLocation,
                                             result->iterativeKsigmaScale};
        std::array<std::uint64_t, 10u> bits{};
        for (std::size_t index = 0u; index < bits.size(); ++index)
            bits[index] = std::bit_cast<std::uint64_t>(values[index]);
        if (baseline)
            EXPECT_EQ(bits, *baseline);
        else
            baseline = bits;
        EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
    }
}

TEST(AdvancedStatisticsTest, PartitionMergeRejectsSquareOverflowAndReleasesScratch)
{
    const auto engine = makeEngine();
    const auto execution = makeExecution(engine);
    const auto extent = an::ImageExtent::create(1u, 2u, 1u, an::defaultEngineLimits());
    ASSERT_TRUE(extent.has_value());
    const auto layout = an::ImageLayout::create({*extent, sizeof(double), 1u, 1u, 1u}, an::defaultEngineLimits());
    ASSERT_TRUE(layout.has_value());
    const double value = std::sqrt((std::numeric_limits<double>::max)()) * 0.9;
    const std::array<double, 2u> samples{value, value};
    const auto result =
        an::computeImageAdvancedStatistics(execution, an::SampleType::Float64, samples.data(), *layout, 0u);
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.status().code(), an::ErrorCode::InvalidData);
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
    EXPECT_EQ(engine.memoryResource()->counters().liveAllocations, 0u);
}

TEST(AdvancedStatisticsTest, TheBasicStatisticsTheCentresComeFromAreReturnedWithThem)
{
    // A caller that shows the extremes and the spread beside the advanced estimators reads them from this one result
    // rather than measuring the selection a second time, so they are exactly what the basic statistics report.
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout layout = layoutOf(5u, 2u);
    const std::array<float, 10u> samples{100.0f, 1.0f, 0.0f, 3.0f, 100.0f, 7.0f, 0.5f, 0.0f, 2.25f, 9.0f};
    const std::optional<an::ValidityDescriptor> validity =
        an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::ZeroIsNoData});
    ASSERT_TRUE(validity.has_value());
    an::StatisticsSelection selection;
    selection.validity = *validity;
    selection.region = an::StatisticsRegion{.originX = 1u, .originY = 0u, .width = 4u, .height = 2u};

    const an::Result<an::BasicStatistics> basic =
        an::computeImageBasicStatistics(execution, an::SampleType::Float32, samples.data(), layout, 0u, selection);
    const an::Result<an::AdvancedStatistics> advanced =
        an::computeImageAdvancedStatistics(execution, an::SampleType::Float32, samples.data(), layout, 0u, selection);
    ASSERT_TRUE(basic.hasValue()) << basic.status().message();
    ASSERT_TRUE(advanced.hasValue()) << advanced.status().message();
    EXPECT_EQ(advanced->basic.selection.valid, 6u);
    EXPECT_EQ(advanced->basic.selection.valid, basic->selection.valid);
    EXPECT_EQ(advanced->basic.selection.total, basic->selection.total);
    EXPECT_EQ(advanced->basic.minimum, basic->minimum);
    EXPECT_EQ(advanced->basic.maximum, basic->maximum);
    EXPECT_EQ(advanced->basic.sum, basic->sum);
    EXPECT_EQ(advanced->basic.mean, basic->mean);
    EXPECT_EQ(advanced->basic.sumSquaredDeviations, basic->sumSquaredDeviations);
    EXPECT_EQ(advanced->basic.populationVariance, basic->populationVariance);
    EXPECT_EQ(advanced->basic.populationStandardDeviation, basic->populationStandardDeviation);
    EXPECT_EQ(advanced->basic.sumError.maximumAdditionDepth, basic->sumError.maximumAdditionDepth);
    EXPECT_EQ(advanced->basic.sumError.absoluteError, basic->sumError.absoluteError);
    EXPECT_EQ(advanced->basic.centeredMomentError.maximumAdditionDepth,
              basic->centeredMomentError.maximumAdditionDepth);
    EXPECT_EQ(advanced->basic.centeredMomentError.absoluteError, basic->centeredMomentError.absoluteError);
    EXPECT_EQ(advanced->mean, advanced->basic.mean);
    EXPECT_EQ(advanced->selection.valid, advanced->basic.selection.valid);
}

TEST(AdvancedStatisticsTest, ACallerThatReadsNoIterativeKsigmaIsSparedItsRanking)
{
    // The six-MAD population is ranked again only for the iterative k-sigma estimators. A caller that reads neither
    // says so, and that ranking is not run: both estimators and their selection are left at zero, the plane is read
    // fewer times, and every other estimator is the same number, bit for bit, through either spelling of the call.
    constexpr std::uint32_t width = 257u;
    constexpr std::uint32_t height = 97u;
    const auto layout = layoutOf(width, height);
    std::vector<float> samples(static_cast<std::size_t>(width) * height);
    for (std::size_t index = 0u; index < samples.size(); ++index)
    {
        samples[index] = index % 61u == 0u ? 40.0f : static_cast<float>((index * 337u) % 257u) / 256.0f;
    }
    const auto engine = makeEngine();
    const auto execution = makeExecution(engine);
    const auto with =
        an::computeImageAdvancedStatistics(execution, an::SampleType::Float32, samples.data(), layout, 0u);
    const auto without =
        an::computeImageAdvancedStatistics(execution, an::SampleType::Float32, samples.data(), layout, 0u, {},
                                           an::AdvancedStatisticsRequest{.iterativeKsigma = false});
    ASSERT_TRUE(with.hasValue()) << with.status().message();
    ASSERT_TRUE(without.hasValue()) << without.status().message();
    EXPECT_GT(with->iterativeKsigmaSelection.valid, 0u);
    EXPECT_LT(with->iterativeKsigmaSelection.valid, with->selection.valid);
    EXPECT_GT(with->iterativeKsigmaScale, 0.0);
    EXPECT_EQ(without->iterativeKsigmaLocation, 0.0);
    EXPECT_EQ(without->iterativeKsigmaScale, 0.0);
    EXPECT_EQ(without->iterativeKsigmaSelection.valid, 0u);
    EXPECT_EQ(without->iterativeKsigmaSelection.total, 0u);
    EXPECT_LT(without->planeScans, with->planeScans);

    const auto bitsOf = [](const an::AdvancedStatistics& statistics) {
        const std::array<double, 12u> values{statistics.mean,
                                             statistics.median,
                                             statistics.medianAbsoluteDeviation,
                                             statistics.averageAbsoluteDeviation,
                                             statistics.sumOfSquares,
                                             statistics.meanOfSquares,
                                             statistics.biweightMidvariance,
                                             statistics.backgroundNoise,
                                             statistics.basic.minimum,
                                             statistics.basic.maximum,
                                             statistics.basic.populationVariance,
                                             static_cast<double>(statistics.selection.valid)};
        std::array<std::uint64_t, 12u> bits{};
        for (std::size_t index = 0u; index < bits.size(); ++index)
            bits[index] = std::bit_cast<std::uint64_t>(values[index]);
        return bits;
    };
    EXPECT_EQ(bitsOf(*without), bitsOf(*with));

    const std::optional<an::ImageView<const float>> view =
        an::ImageView<const float>::create(std::span<const float>{samples.data(), samples.size()}, layout);
    ASSERT_TRUE(view.has_value());
    const auto typed = an::computeAdvancedStatistics(execution, *view, 0u, {},
                                                     an::AdvancedStatisticsRequest{.iterativeKsigma = false});
    ASSERT_TRUE(typed.hasValue()) << typed.status().message();
    EXPECT_EQ(typed->iterativeKsigmaSelection.valid, 0u);
    EXPECT_EQ(typed->planeScans, without->planeScans);
    EXPECT_EQ(bitsOf(*typed), bitsOf(*without));
}

} // namespace
