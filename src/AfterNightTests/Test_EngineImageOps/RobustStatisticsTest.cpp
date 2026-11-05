// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for median, quantiles, deviation and clipped estimators over image selections.

#include <AfterNightEngine/Algorithms/Statistics/RobustStatistics.h>

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

#include <algorithm>
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

[[nodiscard]] an::EngineContext robustEngine(std::uint32_t concurrency)
{
    an::EngineContextOptions options;
    options.maxConcurrency = concurrency;
    an::Result<an::EngineContext> made = an::EngineContext::create(options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::EngineContext{};
}

[[nodiscard]] an::ExecutionContext
robustExecution(const an::EngineContext& engine, std::uint32_t concurrency,
                an::ReductionDeterminism determinism = an::ReductionDeterminism::DecisionFeeding)
{
    an::ExecutionOptions options;
    options.maxConcurrency = concurrency;
    options.reductionDeterminism = determinism;
    an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
}

[[nodiscard]] an::ImageLayout robustLayout(std::uint32_t width, std::uint32_t height, std::uint32_t channelCount,
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

[[nodiscard]] an::ValidityDescriptor ruleOf(an::ValidityRule rule)
{
    an::ValidityDescriptorRequest request;
    request.rule = rule;
    const std::optional<an::ValidityDescriptor> made = an::ValidityDescriptor::create(request);
    EXPECT_TRUE(made.has_value());
    return *made;
}

/// A deterministic spread with repeats, ties and negative samples.
[[nodiscard]] std::vector<float> spread(std::size_t count)
{
    std::vector<float> samples(count, 0.0f);
    std::uint64_t state = 0x243f6a8885a308d3ull;
    for (std::size_t index = 0u; index < count; ++index)
    {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        samples[index] = static_cast<float>(static_cast<std::uint32_t>(state >> 33u) % 2048u) / 1024.0f - 1.0f;
    }
    return samples;
}

/// The quantile convention the operation publishes, evaluated on an already sorted oracle.
[[nodiscard]] double oracleQuantile(const std::vector<double>& sorted, double quantile)
{
    const double position = quantile * static_cast<double>(sorted.size() - 1u);
    const double floored = std::floor(position);
    const std::size_t lower = static_cast<std::size_t>(floored);
    const double fraction = position - floored;
    if (fraction == 0.0)
    {
        return sorted[lower];
    }
    return sorted[lower] + fraction * (sorted[lower + 1u] - sorted[lower]);
}

/// The exact median absolute deviation of an already sorted oracle around its own median.
[[nodiscard]] double oracleDeviation(const std::vector<double>& sorted, double median)
{
    std::vector<double> deviations;
    deviations.reserve(sorted.size());
    for (const double value : sorted)
    {
        deviations.push_back(std::abs(value - median));
    }
    std::sort(deviations.begin(), deviations.end());
    return oracleQuantile(deviations, 0.5);
}

} // namespace

TEST(RobustStatisticsTest, AMaskedRegionOfAnInterleavedChannelIsRankedWhereItLies)
{
    constexpr std::uint32_t kWidth = 23u;
    constexpr std::uint32_t kHeight = 9u;
    constexpr std::uint32_t kChannels = 3u;
    const an::ImageLayout layout = robustLayout(kWidth, kHeight, kChannels, kChannels, 5u);
    std::vector<float> samples(static_cast<std::size_t>(layout.totalBytes() / sizeof(float)), -1000.0f);
    const std::vector<float> values = spread(static_cast<std::size_t>(kWidth) * kHeight);
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            const std::optional<std::uint64_t> offset = layout.sampleByteOffset(0u, y, x, 1u);
            ASSERT_TRUE(offset.has_value());
            samples[static_cast<std::size_t>(*offset / sizeof(float))] =
                values[static_cast<std::size_t>(y) * kWidth + x];
        }
    }

    const std::optional<an::ImageExtent> maskExtent =
        an::ImageExtent::create(kWidth, kHeight, 1u, an::defaultEngineLimits());
    ASSERT_TRUE(maskExtent.has_value());
    std::vector<std::uint8_t> maskBytes(static_cast<std::size_t>(an::packedMaskRowBytes(kWidth)) * kHeight, 0u);
    const std::optional<an::ValidityMaskView<std::uint8_t>> writableMask =
        an::ValidityMaskView<std::uint8_t>::createTight(maskBytes, *maskExtent);
    ASSERT_TRUE(writableMask.has_value());
    writableMask->fill(true);
    const an::StatisticsRegion region{.originX = 3u, .originY = 2u, .width = 13u, .height = 5u};
    // Every third pixel of the rectangle is excluded, so the mask is doing work the rectangle
    // cannot express and the origin is not a byte boundary.
    std::vector<double> expected;
    std::size_t ordinal = 0u;
    for (std::uint32_t y = region.originY; y < region.originY + region.height; ++y)
    {
        for (std::uint32_t x = region.originX; x < region.originX + region.width; ++x)
        {
            const bool keep = ordinal % 3u != 0u;
            ASSERT_TRUE(writableMask->setValid(0u, x, y, keep));
            if (keep)
            {
                expected.push_back(static_cast<double>(values[static_cast<std::size_t>(y) * kWidth + x]));
            }
            ++ordinal;
        }
    }
    std::sort(expected.begin(), expected.end());
    const an::ValidityMaskView<const std::uint8_t> mask = writableMask->constView();

    an::StatisticsSelection selection;
    selection.validity = ruleOf(an::ValidityRule::ExplicitMask);
    selection.mask = &mask;
    selection.region = region;

    an::RobustStatisticsRequest request;
    const std::array<double, 4u> quantiles{0.0, 0.25, 0.75, 1.0};
    std::array<double, 4u> quantileValues{};
    request.quantiles = quantiles;
    request.medianAbsoluteDeviation = true;
    request.roiStrategy = an::RoiSelectionStrategy::StridedSource;

    an::EngineContext engine = robustEngine(4u);
    an::ExecutionContext execution = robustExecution(engine, 4u);
    const an::Result<an::RobustStatistics> statistics = an::computeImageRobustStatistics(
        execution, an::SampleType::Float32, samples.data(), layout, 1u, selection, request, quantileValues);
    ASSERT_TRUE(statistics.hasValue()) << statistics.status().message();
    EXPECT_EQ(statistics->selection, (an::ValidityCount{.valid = expected.size(), .total = 65u}));
    EXPECT_DOUBLE_EQ(statistics->minimum, expected.front());
    EXPECT_DOUBLE_EQ(statistics->maximum, expected.back());
    EXPECT_DOUBLE_EQ(statistics->median, oracleQuantile(expected, 0.5));
    for (std::size_t index = 0u; index < quantiles.size(); ++index)
    {
        EXPECT_DOUBLE_EQ(quantileValues[index], oracleQuantile(expected, quantiles[index])) << "quantile " << index;
    }
    EXPECT_DOUBLE_EQ(statistics->medianAbsoluteDeviation, oracleDeviation(expected, statistics->median));
    EXPECT_DOUBLE_EQ(statistics->robustScale,
                     statistics->medianAbsoluteDeviation * an::kMedianAbsoluteDeviationToSigma);
    EXPECT_EQ(statistics->roiStrategy, an::RoiSelectionStrategy::StridedSource);
    EXPECT_FALSE(statistics->clipped.has_value());
}

TEST(RobustStatisticsTest, BothSelectionStrategiesRankTheSameSamplesToTheSameBits)
{
    constexpr std::uint32_t kWidth = 191u;
    constexpr std::uint32_t kHeight = 7u;
    const an::ImageLayout layout = robustLayout(kWidth, kHeight, 1u, 1u, 3u);
    std::vector<float> samples(static_cast<std::size_t>(layout.totalBytes() / sizeof(float)), 99.0f);
    const std::vector<float> values = spread(static_cast<std::size_t>(kWidth) * kHeight);
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            const std::optional<std::uint64_t> offset = layout.sampleByteOffset(0u, y, x, 0u);
            ASSERT_TRUE(offset.has_value());
            samples[static_cast<std::size_t>(*offset / sizeof(float))] =
                values[static_cast<std::size_t>(y) * kWidth + x];
        }
    }

    an::StatisticsSelection selection;
    selection.region = an::StatisticsRegion{.originX = 7u, .originY = 1u, .width = 101u, .height = 5u};

    an::EngineContext engine = robustEngine(4u);
    an::ExecutionContext execution = robustExecution(engine, 4u);
    const std::array<double, 2u> quantiles{0.1, 0.9};

    std::optional<an::RobustStatistics> strided;
    std::array<double, 2u> stridedValues{};
    std::array<double, 2u> compactValues{};
    for (const an::RoiSelectionStrategy strategy :
         {an::RoiSelectionStrategy::StridedSource, an::RoiSelectionStrategy::CompactSelection})
    {
        an::RobustStatisticsRequest request;
        request.quantiles = quantiles;
        request.medianAbsoluteDeviation = true;
        request.sigmaClipping = an::SigmaClippingRequest{.lowSigma = 2.0, .highSigma = 2.0, .maximumIterations = 4u};
        request.roiStrategy = strategy;
        const std::span<double> output = strategy == an::RoiSelectionStrategy::StridedSource
                                             ? std::span<double>{stridedValues}
                                             : std::span<double>{compactValues};
        const an::Result<an::RobustStatistics> statistics = an::computeImageRobustStatistics(
            execution, an::SampleType::Float32, samples.data(), layout, 0u, selection, request, output);
        ASSERT_TRUE(statistics.hasValue()) << statistics.status().message();
        EXPECT_EQ(statistics->roiStrategy, strategy);
        if (!strided.has_value())
        {
            strided = *statistics;
            continue;
        }
        EXPECT_EQ(statistics->selection, strided->selection);
        EXPECT_EQ(std::bit_cast<std::uint64_t>(statistics->median), std::bit_cast<std::uint64_t>(strided->median));
        EXPECT_EQ(std::bit_cast<std::uint64_t>(statistics->medianAbsoluteDeviation),
                  std::bit_cast<std::uint64_t>(strided->medianAbsoluteDeviation));
        EXPECT_EQ(std::bit_cast<std::uint64_t>(statistics->minimum), std::bit_cast<std::uint64_t>(strided->minimum));
        EXPECT_EQ(std::bit_cast<std::uint64_t>(statistics->maximum), std::bit_cast<std::uint64_t>(strided->maximum));
        ASSERT_TRUE(statistics->clipped.has_value());
        ASSERT_TRUE(strided->clipped.has_value());
        EXPECT_EQ(statistics->clipped->selection, strided->clipped->selection);
        EXPECT_EQ(statistics->clipped->iterations, strided->clipped->iterations);
        EXPECT_EQ(std::bit_cast<std::uint64_t>(statistics->clipped->location),
                  std::bit_cast<std::uint64_t>(strided->clipped->location));
        EXPECT_EQ(std::bit_cast<std::uint64_t>(statistics->clipped->scale),
                  std::bit_cast<std::uint64_t>(strided->clipped->scale));
        // A rank is a sample and is therefore identical; a sum is an accumulation, and a compacted
        // plane is a different shape from the rectangle it was gathered from, so its deterministic
        // partition associates the same terms differently.
        EXPECT_NEAR(statistics->clipped->mean, strided->clipped->mean, 1e-12);
        EXPECT_NEAR(statistics->clipped->standardDeviation, strided->clipped->standardDeviation, 1e-12);
        // The compacted plane is read where a compacted plane is cheap to read, so it costs more
        // memory and fewer strided passes; neither of those may change a reported value.
        EXPECT_GT(statistics->peakScratchBytes, strided->peakScratchBytes);
    }
    EXPECT_EQ(std::bit_cast<std::uint64_t>(compactValues[0]), std::bit_cast<std::uint64_t>(stridedValues[0]));
    EXPECT_EQ(std::bit_cast<std::uint64_t>(compactValues[1]), std::bit_cast<std::uint64_t>(stridedValues[1]));
}

TEST(RobustStatisticsTest, TheAutomaticStrategyReadsTheSamplesWhereTheyLie)
{
    constexpr std::uint32_t kWidth = 64u;
    const an::ImageLayout layout = robustLayout(kWidth, 1u, 1u, 1u);
    const std::vector<float> samples = spread(kWidth);
    an::EngineContext engine = robustEngine(2u);
    an::ExecutionContext execution = robustExecution(engine, 2u);

    // The measured choice, whatever the request asks for beyond a single descent: gathering a
    // selection first was slower on both shapes measured, so it is asked for by name rather than
    // arrived at, and the result says which one ran.
    for (const bool wantsDeviation : {false, true})
    {
        an::RobustStatisticsRequest request;
        request.roiStrategy = an::RoiSelectionStrategy::Automatic;
        request.medianAbsoluteDeviation = wantsDeviation;
        const an::Result<an::RobustStatistics> statistics = an::computeImageRobustStatistics(
            execution, an::SampleType::Float32, samples.data(), layout, 0u, an::StatisticsSelection{}, request);
        ASSERT_TRUE(statistics.hasValue()) << statistics.status().message();
        EXPECT_EQ(statistics->roiStrategy, an::RoiSelectionStrategy::StridedSource);
    }

    an::RobustStatisticsRequest asked;
    asked.medianAbsoluteDeviation = true;
    asked.roiStrategy = an::RoiSelectionStrategy::CompactSelection;
    const an::Result<an::RobustStatistics> gathered = an::computeImageRobustStatistics(
        execution, an::SampleType::Float32, samples.data(), layout, 0u, an::StatisticsSelection{}, asked);
    ASSERT_TRUE(gathered.hasValue()) << gathered.status().message();
    EXPECT_EQ(gathered->roiStrategy, an::RoiSelectionStrategy::CompactSelection);
    EXPECT_GT(gathered->peakScratchBytes, 0u);
}

TEST(RobustStatisticsTest, EveryAdmittedWidthAndPolicyProducesTheSameBits)
{
    constexpr std::uint32_t kWidth = 3079u;
    const an::ImageLayout layout = robustLayout(kWidth, 1u, 1u, 1u);
    const std::vector<float> samples = spread(kWidth);

    std::optional<an::RobustStatistics> baseline;
    for (const an::ReductionDeterminism determinism :
         {an::ReductionDeterminism::DecisionFeeding, an::ReductionDeterminism::All})
    {
        for (const std::uint32_t threads : {1u, 2u, 4u})
        {
            an::EngineContext engine = robustEngine(threads);
            an::ExecutionContext execution = robustExecution(engine, threads, determinism);
            an::RobustStatisticsRequest request;
            request.medianAbsoluteDeviation = true;
            request.sigmaClipping = an::SigmaClippingRequest{};
            request.roiStrategy = an::RoiSelectionStrategy::StridedSource;
            const an::Result<an::RobustStatistics> statistics = an::computeImageRobustStatistics(
                execution, an::SampleType::Float32, samples.data(), layout, 0u, an::StatisticsSelection{}, request);
            ASSERT_TRUE(statistics.hasValue()) << statistics.status().message();
            ASSERT_TRUE(statistics->clipped.has_value());
            if (!baseline.has_value())
            {
                baseline = *statistics;
                continue;
            }
            EXPECT_EQ(std::bit_cast<std::uint64_t>(statistics->median), std::bit_cast<std::uint64_t>(baseline->median));
            EXPECT_EQ(std::bit_cast<std::uint64_t>(statistics->medianAbsoluteDeviation),
                      std::bit_cast<std::uint64_t>(baseline->medianAbsoluteDeviation));
            EXPECT_EQ(statistics->clipped->selection, baseline->clipped->selection);
            EXPECT_EQ(statistics->clipped->iterations, baseline->clipped->iterations);
            EXPECT_EQ(std::bit_cast<std::uint64_t>(statistics->clipped->location),
                      std::bit_cast<std::uint64_t>(baseline->clipped->location));
            EXPECT_EQ(std::bit_cast<std::uint64_t>(statistics->clipped->mean),
                      std::bit_cast<std::uint64_t>(baseline->clipped->mean));
            EXPECT_EQ(std::bit_cast<std::uint64_t>(statistics->clipped->standardDeviation),
                      std::bit_cast<std::uint64_t>(baseline->clipped->standardDeviation));
        }
    }
}

TEST(RobustStatisticsTest, ClippingRejectsTheOutliersAndReportsTheIntervalItStoppedAt)
{
    // Ninety-nine samples on an integer ramp and three that are nowhere near it. The median and the
    // deviation of the whole set barely move; a mean and a standard deviation of it do not survive.
    std::vector<float> samples;
    for (std::uint32_t index = 0u; index < 99u; ++index)
    {
        samples.push_back(static_cast<float>(index));
    }
    samples.push_back(10000.0f);
    samples.push_back(20000.0f);
    samples.push_back(-5000.0f);
    const an::ImageLayout layout = robustLayout(static_cast<std::uint32_t>(samples.size()), 1u, 1u, 1u);

    an::RobustStatisticsRequest request;
    request.medianAbsoluteDeviation = true;
    request.sigmaClipping = an::SigmaClippingRequest{.lowSigma = 3.0, .highSigma = 3.0, .maximumIterations = 8u};
    request.roiStrategy = an::RoiSelectionStrategy::StridedSource;

    an::EngineContext engine = robustEngine(4u);
    an::ExecutionContext execution = robustExecution(engine, 4u);
    const an::Result<an::RobustStatistics> statistics = an::computeImageRobustStatistics(
        execution, an::SampleType::Float32, samples.data(), layout, 0u, an::StatisticsSelection{}, request);
    ASSERT_TRUE(statistics.hasValue()) << statistics.status().message();
    EXPECT_EQ(statistics->selection, (an::ValidityCount{.valid = 102u, .total = 102u}));
    EXPECT_DOUBLE_EQ(statistics->minimum, -5000.0);
    EXPECT_DOUBLE_EQ(statistics->maximum, 20000.0);

    ASSERT_TRUE(statistics->clipped.has_value());
    const an::SigmaClippedStatistics& clipped = *statistics->clipped;
    EXPECT_TRUE(clipped.converged);
    EXPECT_GE(clipped.iterations, 1u);
    EXPECT_LE(clipped.iterations, 8u);
    EXPECT_EQ(clipped.selection.valid, 99u);
    EXPECT_EQ(clipped.selection.total, 102u);
    EXPECT_GT(clipped.lowest, -5000.0);
    EXPECT_LT(clipped.highest, 10000.0);
    EXPECT_DOUBLE_EQ(clipped.location, 49.0);
    EXPECT_DOUBLE_EQ(clipped.mean, 49.0);
    // The ramp 0..98 has population variance (99*99 - 1) / 12.
    EXPECT_NEAR(clipped.standardDeviation, std::sqrt((99.0 * 99.0 - 1.0) / 12.0), 1e-9);
    EXPECT_DOUBLE_EQ(clipped.medianAbsoluteDeviation, 25.0);
    EXPECT_DOUBLE_EQ(clipped.scale, 25.0 * an::kMedianAbsoluteDeviationToSigma);
}

TEST(RobustStatisticsTest, ClippingStopsAtTheStatedIterationCeilingAndSaysItDidNotSettle)
{
    std::vector<float> samples;
    for (std::uint32_t index = 0u; index < 200u; ++index)
    {
        samples.push_back(static_cast<float>(index));
    }
    const an::ImageLayout layout = robustLayout(static_cast<std::uint32_t>(samples.size()), 1u, 1u, 1u);

    an::RobustStatisticsRequest request;
    // A tight interval keeps discarding samples, so one iteration cannot be a fixed point.
    request.sigmaClipping = an::SigmaClippingRequest{.lowSigma = 0.9, .highSigma = 0.9, .maximumIterations = 1u};
    request.roiStrategy = an::RoiSelectionStrategy::StridedSource;

    an::EngineContext engine = robustEngine(2u);
    an::ExecutionContext execution = robustExecution(engine, 2u);
    const an::Result<an::RobustStatistics> statistics = an::computeImageRobustStatistics(
        execution, an::SampleType::Float32, samples.data(), layout, 0u, an::StatisticsSelection{}, request);
    ASSERT_TRUE(statistics.hasValue()) << statistics.status().message();
    ASSERT_TRUE(statistics->clipped.has_value());
    EXPECT_EQ(statistics->clipped->iterations, 1u);
    EXPECT_FALSE(statistics->clipped->converged);
    EXPECT_LT(statistics->clipped->selection.valid, statistics->selection.valid);
}

TEST(RobustStatisticsTest, ASelectionWithNoSpreadConvergesOnItsOwnValue)
{
    const std::vector<float> samples(64u, 0.75f);
    const an::ImageLayout layout = robustLayout(static_cast<std::uint32_t>(samples.size()), 1u, 1u, 1u);

    an::RobustStatisticsRequest request;
    request.sigmaClipping = an::SigmaClippingRequest{};
    request.roiStrategy = an::RoiSelectionStrategy::StridedSource;

    an::EngineContext engine = robustEngine(2u);
    an::ExecutionContext execution = robustExecution(engine, 2u);
    const an::Result<an::RobustStatistics> statistics = an::computeImageRobustStatistics(
        execution, an::SampleType::Float32, samples.data(), layout, 0u, an::StatisticsSelection{}, request);
    ASSERT_TRUE(statistics.hasValue()) << statistics.status().message();
    EXPECT_DOUBLE_EQ(statistics->median, 0.75);
    EXPECT_DOUBLE_EQ(statistics->medianAbsoluteDeviation, 0.0);
    EXPECT_DOUBLE_EQ(statistics->robustScale, 0.0);
    ASSERT_TRUE(statistics->clipped.has_value());
    EXPECT_TRUE(statistics->clipped->converged);
    EXPECT_EQ(statistics->clipped->selection.valid, 64u);
    EXPECT_DOUBLE_EQ(statistics->clipped->lowest, 0.75);
    EXPECT_DOUBLE_EQ(statistics->clipped->highest, 0.75);
    EXPECT_DOUBLE_EQ(statistics->clipped->location, 0.75);
    EXPECT_DOUBLE_EQ(statistics->clipped->scale, 0.0);
    EXPECT_DOUBLE_EQ(statistics->clipped->mean, 0.75);
    EXPECT_DOUBLE_EQ(statistics->clipped->standardDeviation, 0.0);
}

TEST(RobustStatisticsTest, AnEmptySelectionAndAnEmptyRegionBothCarryTheirCounts)
{
    const an::ImageLayout layout = robustLayout(7u, 1u, 1u, 1u);
    const std::array<float, 7u> samples{};
    an::EngineContext engine = robustEngine(2u);
    an::ExecutionContext execution = robustExecution(engine, 2u);

    an::StatisticsSelection filtered;
    filtered.validity = ruleOf(an::ValidityRule::ZeroIsNoData);
    const an::Result<an::RobustStatistics> nothing = an::computeImageRobustStatistics(
        execution, an::SampleType::Float32, samples.data(), layout, 0u, filtered, an::RobustStatisticsRequest{});
    ASSERT_FALSE(nothing.hasValue());
    EXPECT_EQ(nothing.status().code(), an::ErrorCode::EmptySelection);

    an::StatisticsSelection emptyRegion;
    emptyRegion.region = an::StatisticsRegion{.originX = 3u, .originY = 0u, .width = 0u, .height = 1u};
    const an::Result<an::RobustStatistics> none = an::computeImageRobustStatistics(
        execution, an::SampleType::Float32, samples.data(), layout, 0u, emptyRegion, an::RobustStatisticsRequest{});
    ASSERT_FALSE(none.hasValue());
    EXPECT_EQ(none.status().code(), an::ErrorCode::EmptySelection);
    const an::StatusContextField* completed = none.status().findContext(an::ContextKey::Completed);
    const an::StatusContextField* total = none.status().findContext(an::ContextKey::Total);
    ASSERT_NE(completed, nullptr);
    ASSERT_NE(total, nullptr);
    EXPECT_EQ(completed->count, 0u);
    EXPECT_EQ(total->count, 0u);
}

TEST(RobustStatisticsTest, NonFiniteSamplesAreEitherRefusedOrExcludedByTheNamedRule)
{
    const an::ImageLayout layout = robustLayout(5u, 1u, 1u, 1u);
    const std::array<float, 5u> samples{1.0f, std::numeric_limits<float>::quiet_NaN(), 3.0f,
                                        std::numeric_limits<float>::infinity(), 5.0f};
    an::EngineContext engine = robustEngine(2u);
    an::ExecutionContext execution = robustExecution(engine, 2u);

    const an::Result<an::RobustStatistics> refused =
        an::computeImageRobustStatistics(execution, an::SampleType::Float32, samples.data(), layout, 0u,
                                         an::StatisticsSelection{}, an::RobustStatisticsRequest{});
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidData);

    an::StatisticsSelection selection;
    selection.validity = ruleOf(an::ValidityRule::FiniteOnly);
    an::RobustStatisticsRequest request;
    request.medianAbsoluteDeviation = true;
    const an::Result<an::RobustStatistics> excluded = an::computeImageRobustStatistics(
        execution, an::SampleType::Float32, samples.data(), layout, 0u, selection, request);
    ASSERT_TRUE(excluded.hasValue()) << excluded.status().message();
    EXPECT_EQ(excluded->selection, (an::ValidityCount{.valid = 3u, .total = 5u}));
    EXPECT_DOUBLE_EQ(excluded->median, 3.0);
    EXPECT_DOUBLE_EQ(excluded->medianAbsoluteDeviation, 2.0);
}

TEST(RobustStatisticsTest, AHoldRanksEveryAdmittedSampleAsHeldUnderEitherStrategy)
{
    // Every admitted sample is read as held to the request's interval: the rule decides on the stored value - a
    // not-a-number the finite-only rule leaves out stays out - and an admitted sample outside the hold is ordered as
    // its nearer end. The strided read and the compaction rank the held values to the same bits, and to an oracle's;
    // a plane of sixteen-bit codes with samples below zero and above one, held to the unit interval, takes the same
    // reads as its held copy, because the held ends lie on the grid; and a hold is refused when it is not a finite
    // ordered interval or stands beside a clipping rule.
    constexpr std::uint32_t kWidth = 640u;
    constexpr std::uint32_t kHeight = 480u;
    constexpr std::size_t kCount = static_cast<std::size_t>(kWidth) * kHeight;
    std::vector<float> samples = spread(kCount);
    samples[kCount / 3u] = std::numeric_limits<float>::quiet_NaN();
    samples[kCount / 2u] = -std::numeric_limits<float>::infinity();
    const an::ImageLayout layout = robustLayout(kWidth, kHeight, 1u, 1u);
    an::EngineContext engine = robustEngine(4u);
    an::ExecutionContext execution = robustExecution(engine, 4u);
    an::StatisticsSelection selection;
    selection.validity = ruleOf(an::ValidityRule::FiniteOnly);
    const an::KernelValueWindow hold{.lowest = 0.0, .highest = 0.5};
    constexpr std::array<double, 3u> kQuantiles{0.1, 0.5, 0.9};
    const auto same = [](double left, double right) {
        return std::bit_cast<std::uint64_t>(left) == std::bit_cast<std::uint64_t>(right);
    };

    std::vector<double> held;
    for (const float sample : samples)
    {
        if (std::isfinite(sample))
        {
            held.push_back((std::min)((std::max)(static_cast<double>(sample), hold.lowest), hold.highest));
        }
    }
    std::sort(held.begin(), held.end());
    const double median = oracleQuantile(held, 0.5);
    for (const an::RoiSelectionStrategy strategy :
         {an::RoiSelectionStrategy::Automatic, an::RoiSelectionStrategy::CompactSelection})
    {
        SCOPED_TRACE(static_cast<int>(strategy));
        std::array<double, 3u> values{};
        an::RobustStatisticsRequest request;
        request.quantiles = kQuantiles;
        request.medianAbsoluteDeviation = true;
        request.roiStrategy = strategy;
        request.hold = hold;
        const an::Result<an::RobustStatistics> ranked = an::computeImageRobustStatistics(
            execution, an::SampleType::Float32, samples.data(), layout, 0u, selection, request, values);
        ASSERT_TRUE(ranked.hasValue()) << ranked.status().message();
        EXPECT_EQ(ranked->selection, (an::ValidityCount{.valid = kCount - 2u, .total = kCount}));
        EXPECT_TRUE(same(ranked->minimum, held.front()));
        EXPECT_TRUE(same(ranked->maximum, held.back()));
        EXPECT_TRUE(same(ranked->median, median));
        EXPECT_TRUE(same(ranked->medianAbsoluteDeviation, oracleDeviation(held, median)));
        for (std::size_t index = 0u; index < kQuantiles.size(); ++index)
        {
            EXPECT_TRUE(same(values[index], oracleQuantile(held, kQuantiles[index]))) << index;
        }
    }

    // Sixteen-bit codes on every fourth step, with outliers the hold brings onto the grid's ends: zero below, and
    // above the largest code on that step, which a Float32 holds exactly, so the held plane stays on the grid and
    // the shortcut's one histogram read answers it, as it answers the same plane held before it is read.
    constexpr std::uint32_t kGridWidth = 2048u;
    constexpr std::uint32_t kGridHeight = 1100u;
    constexpr std::size_t kGridCount = static_cast<std::size_t>(kGridWidth) * kGridHeight;
    const float gridTop = 65532.0f / 65535.0f;
    std::vector<float> grid(kGridCount, 0.0f);
    std::uint64_t state = 0x9e3779b97f4a7c15ull;
    for (std::size_t index = 0u; index < kGridCount; ++index)
    {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        const std::uint32_t code = (static_cast<std::uint32_t>(state >> 40u) % 14000u + 1000u) * 4u;
        grid[index] = static_cast<float>(code) / 65535.0f;
        if (index % 5003u == 0u)
            grid[index] = -0.25f;
        else if (index % 7001u == 0u)
            grid[index] = 1.5f;
    }
    std::vector<float> gridHeld = grid;
    for (float& value : gridHeld)
        value = std::clamp(value, 0.0f, gridTop);
    const an::ImageLayout gridLayout = robustLayout(kGridWidth, kGridHeight, 1u, 1u);
    an::RobustStatisticsRequest gridHold;
    gridHold.medianAbsoluteDeviation = true;
    gridHold.hold = an::KernelValueWindow{.lowest = 0.0, .highest = static_cast<double>(gridTop)};
    const an::Result<an::RobustStatistics> ofGrid = an::computeImageRobustStatistics(
        execution, an::SampleType::Float32, grid.data(), gridLayout, 0u, an::StatisticsSelection{}, gridHold);
    const an::Result<an::RobustStatistics> ofHeldGrid = an::computeImageRobustStatistics(
        execution, an::SampleType::Float32, gridHeld.data(), gridLayout, 0u, an::StatisticsSelection{},
        an::RobustStatisticsRequest{.medianAbsoluteDeviation = true});
    ASSERT_TRUE(ofGrid.hasValue()) << ofGrid.status().message();
    ASSERT_TRUE(ofHeldGrid.hasValue()) << ofHeldGrid.status().message();
    EXPECT_TRUE(same(ofGrid->median, ofHeldGrid->median));
    EXPECT_TRUE(same(ofGrid->medianAbsoluteDeviation, ofHeldGrid->medianAbsoluteDeviation));
    EXPECT_TRUE(same(ofGrid->minimum, 0.0));
    EXPECT_TRUE(same(ofGrid->maximum, static_cast<double>(gridTop)));
    EXPECT_EQ(ofGrid->planeScans, ofHeldGrid->planeScans);
    EXPECT_EQ(ofGrid->planeScans, 2u);

    an::RobustStatisticsRequest inverted;
    inverted.hold = an::KernelValueWindow{.lowest = 1.0, .highest = 0.0};
    EXPECT_EQ(an::computeImageRobustStatistics(execution, an::SampleType::Float32, samples.data(), layout, 0u,
                                               selection, inverted)
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
    an::RobustStatisticsRequest unbounded;
    unbounded.hold = an::KernelValueWindow{.lowest = 0.0, .highest = std::numeric_limits<double>::infinity()};
    EXPECT_EQ(an::computeImageRobustStatistics(execution, an::SampleType::Float32, samples.data(), layout, 0u,
                                               selection, unbounded)
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
    an::RobustStatisticsRequest clippedAndHeld;
    clippedAndHeld.hold = hold;
    clippedAndHeld.sigmaClipping = an::SigmaClippingRequest{};
    EXPECT_EQ(an::computeImageRobustStatistics(execution, an::SampleType::Float32, samples.data(), layout, 0u,
                                               selection, clippedAndHeld)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
}

TEST(RobustStatisticsTest, TheRequestIsRefusedWhenItNamesRulesThisBuildDoesNotImplement)
{
    const an::ImageLayout layout = robustLayout(8u, 1u, 1u, 1u);
    const std::array<float, 8u> samples{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    an::EngineContext engine = robustEngine(2u);
    an::ExecutionContext execution = robustExecution(engine, 2u);

    const auto run = [&](const an::RobustStatisticsRequest& request, std::span<double> output) {
        return an::computeImageRobustStatistics(execution, an::SampleType::Float32, samples.data(), layout, 0u,
                                                an::StatisticsSelection{}, request, output);
    };

    const std::array<double, 2u> two{0.25, 0.75};
    std::array<double, 1u> one{};
    an::RobustStatisticsRequest mismatched;
    mismatched.quantiles = two;
    EXPECT_EQ(run(mismatched, one).status().code(), an::ErrorCode::IncompatibleArguments);

    an::RobustStatisticsRequest unknownInterpolation;
    unknownInterpolation.interpolation = static_cast<an::QuantileInterpolation>(9u);
    EXPECT_EQ(run(unknownInterpolation, {}).status().code(), an::ErrorCode::InvalidArgument);

    an::RobustStatisticsRequest unknownStrategy;
    unknownStrategy.roiStrategy = static_cast<an::RoiSelectionStrategy>(7u);
    EXPECT_EQ(run(unknownStrategy, {}).status().code(), an::ErrorCode::InvalidArgument);

    an::RobustStatisticsRequest negativeWidth;
    negativeWidth.sigmaClipping = an::SigmaClippingRequest{.lowSigma = -1.0};
    EXPECT_EQ(run(negativeWidth, {}).status().code(), an::ErrorCode::InvalidArgument);

    an::RobustStatisticsRequest noIterations;
    noIterations.sigmaClipping = an::SigmaClippingRequest{.maximumIterations = 0u};
    EXPECT_EQ(run(noIterations, {}).status().code(), an::ErrorCode::OutOfRange);

    an::RobustStatisticsRequest tooManyIterations;
    tooManyIterations.sigmaClipping =
        an::SigmaClippingRequest{.maximumIterations = an::kMaxSigmaClippingIterations + 1u};
    EXPECT_EQ(run(tooManyIterations, {}).status().code(), an::ErrorCode::OutOfRange);

    const std::array<double, 1u> outsideUnit{1.5};
    std::array<double, 1u> outsideValues{};
    an::RobustStatisticsRequest outside;
    outside.quantiles = outsideUnit;
    EXPECT_EQ(run(outside, outsideValues).status().code(), an::ErrorCode::OutOfRange);

    const std::vector<double> tooMany(an::kMaxOrderStatisticRanks + 1u, 0.5);
    std::vector<double> tooManyValues(tooMany.size(), 0.0);
    an::RobustStatisticsRequest many;
    many.quantiles = tooMany;
    EXPECT_EQ(run(many, tooManyValues).status().code(), an::ErrorCode::OutOfRange);

    an::StatisticsSelection outsideRegion;
    outsideRegion.region = an::StatisticsRegion{.originX = 4u, .originY = 0u, .width = 6u, .height = 1u};
    const an::Result<an::RobustStatistics> beyond = an::computeImageRobustStatistics(
        execution, an::SampleType::Float32, samples.data(), layout, 0u, outsideRegion, an::RobustStatisticsRequest{});
    EXPECT_EQ(beyond.status().code(), an::ErrorCode::InvalidArgument);
}

TEST(RobustStatisticsTest, StridedScratchDoesNotGrowWithTheSampleCount)
{
    an::EngineContext engine = robustEngine(4u);
    an::ExecutionContext execution = robustExecution(engine, 4u);
    an::RobustStatisticsRequest request;
    request.medianAbsoluteDeviation = true;
    request.roiStrategy = an::RoiSelectionStrategy::StridedSource;

    const auto peakFor = [&](std::uint32_t width) {
        const an::ImageLayout layout = robustLayout(width, 1u, 1u, 1u);
        const std::vector<float> samples = spread(width);
        const an::Result<an::RobustStatistics> statistics = an::computeImageRobustStatistics(
            execution, an::SampleType::Float32, samples.data(), layout, 0u, an::StatisticsSelection{}, request);
        EXPECT_TRUE(statistics.hasValue()) << statistics.status().message();
        return statistics.hasValue() ? statistics->peakScratchBytes : 0u;
    };

    const std::uint64_t small = peakFor(4096u);
    const std::uint64_t large = peakFor(409600u);
    ASSERT_GT(small, 0u);
    // A hundredfold more samples may cost more chunks; it may not cost a hundredfold more memory.
    EXPECT_LT(large, small * 4u);
}

TEST(RobustStatisticsTest, TheCompactionCeilingRefusesASelectionLargerThanItsStatedBound)
{
    constexpr std::uint32_t kWidth = 4100u;
    constexpr std::uint32_t kHeight = 2100u;
    static_assert(static_cast<std::uint64_t>(kWidth) * kHeight > an::kMaxCompactSelectionBytes / sizeof(double),
                  "the case only bites while the selection is above the compaction ceiling");
    const an::ImageLayout layout = robustLayout(kWidth, kHeight, 1u, 1u);
    const std::vector<float> samples(static_cast<std::size_t>(kWidth) * kHeight, 1.0f);

    an::EngineContext engine = robustEngine(4u);
    an::ExecutionContext execution = robustExecution(engine, 4u);
    an::RobustStatisticsRequest request;
    request.medianAbsoluteDeviation = true;
    request.roiStrategy = an::RoiSelectionStrategy::CompactSelection;
    const an::Result<an::RobustStatistics> refused = an::computeImageRobustStatistics(
        execution, an::SampleType::Float32, samples.data(), layout, 0u, an::StatisticsSelection{}, request);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::OutOfRange);

    // The same selection is served without complaint by the strategy the engine chooses on its own.
    request.roiStrategy = an::RoiSelectionStrategy::Automatic;
    const an::Result<an::RobustStatistics> served = an::computeImageRobustStatistics(
        execution, an::SampleType::Float32, samples.data(), layout, 0u, an::StatisticsSelection{}, request);
    ASSERT_TRUE(served.hasValue()) << served.status().message();
    EXPECT_EQ(served->roiStrategy, an::RoiSelectionStrategy::StridedSource);
    EXPECT_DOUBLE_EQ(served->median, 1.0);
}

TEST(RobustStatisticsTest, TheTypedSpellingReadsItsRepresentationAndLayoutFromTheView)
{
    const an::ImageLayout layout = robustLayout(5u, 1u, 1u, 1u);
    const std::array<float, 5u> samples{4.0f, 1.0f, 5.0f, 2.0f, 3.0f};
    const std::optional<an::ImageView<const float>> view = an::ImageView<const float>::create(samples, layout);
    ASSERT_TRUE(view.has_value());
    an::EngineContext engine = robustEngine(2u);
    an::ExecutionContext execution = robustExecution(engine, 2u);

    an::RobustStatisticsRequest request;
    request.medianAbsoluteDeviation = true;
    const an::Result<an::RobustStatistics> statistics =
        an::computeRobustStatistics(execution, *view, 0u, an::StatisticsSelection{}, request);
    ASSERT_TRUE(statistics.hasValue()) << statistics.status().message();
    EXPECT_EQ(statistics->selection.valid, 5u);
    EXPECT_DOUBLE_EQ(statistics->median, 3.0);
    EXPECT_DOUBLE_EQ(statistics->medianAbsoluteDeviation, 1.0);
}
