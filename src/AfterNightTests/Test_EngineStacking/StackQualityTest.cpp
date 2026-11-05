// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the stack quality diagnostics: the summary of a weight table, the per-frame
// rejection rates and their medians, the robust detection of frames rejected far more often than
// the rest, and the connected-component analysis of a rejection count map. Every figure here is a
// number read from counts and weights the executor already holds, so every case is an oracle
// written in the test against small hand-built inputs.

#include <AfterNightEngine/Algorithms/Stacking/StackQuality.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/ExecutionPolicy.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

/// An engine with a stated concurrency.
[[nodiscard]] an::EngineContext makeEngine(std::uint32_t concurrency = 2u)
{
    an::EngineContextOptions options;
    options.maxConcurrency = concurrency;
    an::Result<an::EngineContext> made = an::EngineContext::create(options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::EngineContext{};
}

/// One operation in an engine, optionally stoppable.
[[nodiscard]] an::ExecutionContext makeExecution(const an::EngineContext& engine, std::stop_token stopToken = {})
{
    an::ExecutionOptions options;
    options.stopToken = std::move(stopToken);
    an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
}

/// The bytes an engine has outstanding right now.
[[nodiscard]] std::uint64_t liveBytes(const an::EngineContext& engine)
{
    return engine.memoryResource()->counters().liveBytes;
}

[[nodiscard]] bool messageNames(const an::Status& status, std::string_view name)
{
    return std::string_view(status.message()).find(name) != std::string_view::npos;
}

/// An extent of the stated shape.
[[nodiscard]] an::ImageExtent extentOf(std::uint32_t width, std::uint32_t height, std::uint32_t channels = 1u)
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, channels, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    return *extent;
}

/// The rate records of a set of frames, every one rated.
[[nodiscard]] std::vector<an::FrameRejectionRate> ratesOf(std::initializer_list<double> rates)
{
    std::vector<an::FrameRejectionRate> records;
    for (const double rate : rates)
    {
        records.push_back(an::FrameRejectionRate{.rate = rate, .rated = true});
    }
    return records;
}

TEST(StackQualityTest, WeightSummaryFollowsTheNormalizedMetrics)
{
    // The summary normalizes by the largest weight before taking moments, reports the deviation back
    // on the weights' own scale, and the efficiency is the Kish effective count over the positive
    // finite entries.
    const std::array<double, 6u> weights{2.0, 1.0, 0.5, 0.0, -1.0, std::numeric_limits<double>::quiet_NaN()};
    const an::StackWeightSummary summary = an::summarizeStackFrameWeights(weights);
    EXPECT_EQ(summary.positiveCount, 3u);
    EXPECT_DOUBLE_EQ(summary.minimum, 0.5);
    EXPECT_DOUBLE_EQ(summary.maximum, 2.0);

    const double scale = 2.0;
    const std::array<double, 3u> normalized{1.0, 0.5, 0.25};
    double sum = 0.0;
    double sumSquares = 0.0;
    for (const double value : normalized)
    {
        sum += value;
        sumSquares += value * value;
    }
    const double mean = sum / 3.0;
    double variance = 0.0;
    for (const double value : normalized)
    {
        variance += (value - mean) * (value - mean);
    }
    variance /= 3.0;
    EXPECT_NEAR(summary.standardDeviation, std::sqrt(variance) * scale, 1e-15);
    EXPECT_NEAR(summary.efficiency, (sum * sum) / (3.0 * sumSquares), 1e-15);

    const an::StackWeightSummary uniform = an::summarizeStackFrameWeights(std::array<double, 4u>{1.0, 1.0, 1.0, 1.0});
    EXPECT_EQ(uniform.positiveCount, 4u);
    EXPECT_DOUBLE_EQ(uniform.efficiency, 1.0);
    EXPECT_DOUBLE_EQ(uniform.standardDeviation, 0.0);

    const an::StackWeightSummary none = an::summarizeStackFrameWeights(std::array<double, 2u>{0.0, -3.0});
    EXPECT_EQ(none.positiveCount, 0u);
    EXPECT_DOUBLE_EQ(none.efficiency, 0.0);
    EXPECT_DOUBLE_EQ(none.maximum, 0.0);

    const an::StackWeightSummary empty = an::summarizeStackFrameWeights(std::span<const double>{});
    EXPECT_EQ(empty.positiveCount, 0u);
}

TEST(StackQualityTest, FrameRejectionSummaryRatesEveryFrameWithSamplesAndTakesTheMedianOfThoseRates)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    // Frame 2 saw no valid sample and is not rated; the four rated frames have rates 0.1, 0.5, 0.0
    // and 0.25, whose sorted middle pair is 0.1 and 0.25.
    const std::array<std::uint64_t, 5u> valid{100u, 40u, 0u, 10u, 80u};
    const std::array<std::uint64_t, 5u> rejected{10u, 20u, 0u, 0u, 20u};
    std::vector<an::FrameRejectionRate> rates(5u);
    const an::Result<an::FrameRejectionSummary> summary =
        an::summarizeFrameRejection(execution, valid, rejected, 30u, 20u, rates);
    ASSERT_TRUE(summary.hasValue()) << summary.status().message();
    EXPECT_EQ(summary->frameCount, 5u);
    EXPECT_EQ(summary->ratedFrameCount, 4u);
    EXPECT_EQ(summary->validSamples, 230u);
    EXPECT_EQ(summary->rejectedLow, 30u);
    EXPECT_EQ(summary->rejectedHigh, 20u);
    EXPECT_DOUBLE_EQ(summary->globalRate, 50.0 / 230.0);
    EXPECT_DOUBLE_EQ(summary->minimumRate, 0.0);
    EXPECT_DOUBLE_EQ(summary->maximumRate, 0.5);
    EXPECT_DOUBLE_EQ(summary->meanRate, (0.1 + 0.5 + 0.0 + 0.25) / 4.0);
    EXPECT_DOUBLE_EQ(summary->medianRate, 0.5 * (0.1 + 0.25));
    EXPECT_TRUE(rates[0].rated);
    EXPECT_DOUBLE_EQ(rates[0].rate, 0.1);
    EXPECT_TRUE(rates[1].rated);
    EXPECT_DOUBLE_EQ(rates[1].rate, 0.5);
    EXPECT_FALSE(rates[2].rated);
    EXPECT_TRUE(rates[3].rated);
    EXPECT_DOUBLE_EQ(rates[3].rate, 0.0);
    EXPECT_TRUE(rates[4].rated);
    EXPECT_DOUBLE_EQ(rates[4].rate, 0.25);
    EXPECT_EQ(liveBytes(engine), 0u);

    // An odd number of rated frames takes the middle one.
    const std::array<std::uint64_t, 3u> oddValid{10u, 10u, 10u};
    const std::array<std::uint64_t, 3u> oddRejected{3u, 1u, 2u};
    std::vector<an::FrameRejectionRate> oddRates(3u);
    const an::Result<an::FrameRejectionSummary> odd =
        an::summarizeFrameRejection(execution, oddValid, oddRejected, 6u, 0u, oddRates);
    ASSERT_TRUE(odd.hasValue()) << odd.status().message();
    EXPECT_DOUBLE_EQ(odd->medianRate, 0.2);
    EXPECT_EQ(odd->ratedFrameCount, 3u);

    // No valid sample anywhere is an empty selection rather than a rate of nothing.
    const std::array<std::uint64_t, 2u> nothing{0u, 0u};
    std::vector<an::FrameRejectionRate> noRates(2u);
    const an::Result<an::FrameRejectionSummary> empty =
        an::summarizeFrameRejection(execution, nothing, nothing, 0u, 0u, noRates);
    ASSERT_FALSE(empty.hasValue());
    EXPECT_EQ(empty.status().code(), an::ErrorCode::EmptySelection);
}

TEST(StackQualityTest, FrameRejectionSummaryRefusesMismatchedSpansAndImpossibleCounts)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::array<std::uint64_t, 3u> valid{10u, 10u, 10u};
    const std::array<std::uint64_t, 2u> shortRejected{1u, 1u};
    const std::array<std::uint64_t, 3u> rejected{1u, 1u, 1u};
    std::vector<an::FrameRejectionRate> rates(3u);
    std::vector<an::FrameRejectionRate> shortRates(2u);

    const an::Result<an::FrameRejectionSummary> lengths =
        an::summarizeFrameRejection(execution, valid, shortRejected, 2u, 0u, rates);
    ASSERT_FALSE(lengths.hasValue());
    EXPECT_EQ(lengths.status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_TRUE(messageNames(lengths.status(), "frameRejectedSamples")) << lengths.status().message();

    const an::Result<an::FrameRejectionSummary> output =
        an::summarizeFrameRejection(execution, valid, rejected, 2u, 1u, shortRates);
    ASSERT_FALSE(output.hasValue());
    EXPECT_EQ(output.status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_TRUE(messageNames(output.status(), "frameRates")) << output.status().message();

    // A frame cannot reject more samples than it had.
    const std::array<std::uint64_t, 3u> tooMany{1u, 11u, 1u};
    const an::Result<an::FrameRejectionSummary> impossible =
        an::summarizeFrameRejection(execution, valid, tooMany, 13u, 0u, rates);
    ASSERT_FALSE(impossible.hasValue());
    EXPECT_EQ(impossible.status().code(), an::ErrorCode::InvalidArgument);

    const an::ExecutionContext nothing;
    const an::Result<an::FrameRejectionSummary> unheld =
        an::summarizeFrameRejection(nothing, valid, rejected, 2u, 1u, rates);
    ASSERT_FALSE(unheld.hasValue());
    EXPECT_EQ(unheld.status().code(), an::ErrorCode::NotInitialized);
}

TEST(StackQualityTest, OutlierDetectionFlagsTheFrameRejectedFarAboveTheRobustBaseline)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    // Nine quiet frames and one that lost a fifth of its samples.
    const std::vector<an::FrameRejectionRate> rates =
        ratesOf({0.010, 0.012, 0.011, 0.009, 0.010, 0.013, 0.010, 0.011, 0.012, 0.200});
    std::vector<std::uint32_t> flagged(rates.size(), 99u);
    const an::Result<an::RejectionFrameOutlierReport> report =
        an::detectRejectionFrameOutliers(execution, rates, flagged);
    ASSERT_TRUE(report.hasValue()) << report.status().message();
    EXPECT_EQ(report->frameCount, 10u);
    EXPECT_EQ(report->ratedFrameCount, 10u);
    EXPECT_EQ(report->flaggedFrameCount, 1u);
    EXPECT_EQ(flagged[0], 9u);

    // The rule, written out: a median-and-deviation threshold, then a mean-and-
    // deviation threshold over the frames below it, and the larger of the two.
    std::vector<double> sorted;
    for (const an::FrameRejectionRate& rate : rates)
    {
        sorted.push_back(rate.rate);
    }
    std::sort(sorted.begin(), sorted.end());
    const double median = 0.5 * (sorted[4] + sorted[5]);
    std::vector<double> deviations;
    for (const double rate : sorted)
    {
        deviations.push_back(std::abs(rate - median));
    }
    std::sort(deviations.begin(), deviations.end());
    const double mad = 0.5 * (deviations[4] + deviations[5]);
    const double robustSigma = (std::max)(1.4826 * mad, 0.0025);
    const double robustThreshold = (std::max)({0.015, median + 4.0 * robustSigma, median * 2.5});
    // The baseline moments are accumulated in frame order, which is the order the report states.
    std::vector<double> baseline;
    for (const an::FrameRejectionRate& rate : rates)
    {
        if (rate.rate <= robustThreshold)
        {
            baseline.push_back(rate.rate);
        }
    }
    double mean = 0.0;
    for (const double rate : baseline)
    {
        mean += rate;
    }
    mean /= static_cast<double>(baseline.size());
    double variance = 0.0;
    for (const double rate : baseline)
    {
        variance += (rate - mean) * (rate - mean);
    }
    variance /= static_cast<double>(baseline.size());
    const double threshold = (std::max)(robustThreshold, mean + 2.5 * (std::max)(std::sqrt(variance), 0.0025));
    EXPECT_DOUBLE_EQ(report->medianRate, median);
    EXPECT_DOUBLE_EQ(report->robustSigma, robustSigma);
    EXPECT_DOUBLE_EQ(report->robustThreshold, robustThreshold);
    EXPECT_DOUBLE_EQ(report->threshold, threshold);
    EXPECT_EQ(report->baselineFrameCount, 9u);
    EXPECT_EQ(liveBytes(engine), 0u);

    // Every frame alike flags nothing.
    const std::vector<an::FrameRejectionRate> quiet = ratesOf({0.01, 0.01, 0.01, 0.01});
    std::vector<std::uint32_t> none(quiet.size());
    const an::Result<an::RejectionFrameOutlierReport> calm = an::detectRejectionFrameOutliers(execution, quiet, none);
    ASSERT_TRUE(calm.hasValue()) << calm.status().message();
    EXPECT_EQ(calm->flaggedFrameCount, 0u);

    // Fewer than three frames below the robust threshold falls back to every rated frame.
    const std::vector<an::FrameRejectionRate> few = ratesOf({0.30, 0.31, 0.0});
    std::vector<std::uint32_t> fewFlags(few.size());
    const an::Result<an::RejectionFrameOutlierReport> fallback =
        an::detectRejectionFrameOutliers(execution, few, fewFlags);
    ASSERT_TRUE(fallback.hasValue()) << fallback.status().message();
    EXPECT_EQ(fallback->baselineFrameCount, 3u);
}

TEST(StackQualityTest, OutlierDetectionSkipsUnratedFramesAndRefusesNothingRatedOrAShortOutput)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    std::vector<an::FrameRejectionRate> rates = ratesOf({0.01, 0.01, 0.01, 0.01, 0.01, 0.4});
    rates.insert(rates.begin() + 2, an::FrameRejectionRate{.rate = 0.0, .rated = false});
    std::vector<std::uint32_t> flagged(rates.size());
    const an::Result<an::RejectionFrameOutlierReport> report =
        an::detectRejectionFrameOutliers(execution, rates, flagged);
    ASSERT_TRUE(report.hasValue()) << report.status().message();
    EXPECT_EQ(report->frameCount, 7u);
    EXPECT_EQ(report->ratedFrameCount, 6u);
    EXPECT_EQ(report->flaggedFrameCount, 1u);
    EXPECT_EQ(flagged[0], 6u);

    std::vector<std::uint32_t> tooShort(rates.size() - 1u);
    const an::Result<an::RejectionFrameOutlierReport> shortOutput =
        an::detectRejectionFrameOutliers(execution, rates, tooShort);
    ASSERT_FALSE(shortOutput.hasValue());
    EXPECT_EQ(shortOutput.status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_TRUE(messageNames(shortOutput.status(), "flaggedFrames")) << shortOutput.status().message();

    const std::vector<an::FrameRejectionRate> unrated(3u);
    std::vector<std::uint32_t> unratedFlags(3u);
    const an::Result<an::RejectionFrameOutlierReport> nothing =
        an::detectRejectionFrameOutliers(execution, unrated, unratedFlags);
    ASSERT_FALSE(nothing.hasValue());
    EXPECT_EQ(nothing.status().code(), an::ErrorCode::EmptySelection);

    std::vector<an::FrameRejectionRate> malformed = ratesOf({0.01, 0.02, 0.03});
    malformed[1].rate = -0.5;
    const an::Result<an::RejectionFrameOutlierReport> negative =
        an::detectRejectionFrameOutliers(execution, malformed, unratedFlags);
    ASSERT_FALSE(negative.hasValue());
    EXPECT_EQ(negative.status().code(), an::ErrorCode::InvalidArgument);
}

TEST(StackQualityTest, ClusterAnalysisFindsConnectedComponentsWithTheirExtentCentroidAndCounts)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 8u;
    constexpr std::uint32_t kHeight = 6u;

    // A 2x3 block at the top left counted twice per pixel, a diagonal pair at the bottom right, and
    // one isolated pixel that no minimum admits.
    std::vector<std::uint32_t> map(static_cast<std::size_t>(kWidth) * kHeight, 0u);
    const auto at = [&map](std::uint32_t x, std::uint32_t y) -> std::uint32_t& {
        return map[static_cast<std::size_t>(y) * kWidth + x];
    };
    for (std::uint32_t y = 0u; y < 3u; ++y)
    {
        for (std::uint32_t x = 0u; x < 2u; ++x)
        {
            at(x, y) = 2u;
        }
    }
    at(1u, 2u) = 5u;
    at(6u, 4u) = 1u;
    at(7u, 5u) = 3u;
    at(4u, 0u) = 1u;

    an::RejectionClusterAnalysisRequest request;
    request.minimumClusterPixels = 2u;
    std::vector<an::RejectionClusterDetection> clusters(8u);
    const an::Result<an::RejectionClusterReport> report =
        an::analyzeRejectionClusters(execution, map, extentOf(kWidth, kHeight), request, clusters);
    ASSERT_TRUE(report.hasValue()) << report.status().message();
    EXPECT_EQ(report->source, an::RejectionClusterSource::Merged);
    EXPECT_EQ(report->width, kWidth);
    EXPECT_EQ(report->height, kHeight);
    EXPECT_EQ(report->channels, 1u);
    EXPECT_EQ(report->clustersFound, 2u);
    EXPECT_EQ(report->clustersReported, 2u);
    EXPECT_EQ(report->reportedClusterPixels, 8u);
    EXPECT_EQ(report->largestClusterPixels, 6u);
    EXPECT_DOUBLE_EQ(report->clusteredPixelFraction, 8.0 / 48.0);
    EXPECT_DOUBLE_EQ(report->largestClusterFraction, 6.0 / 48.0);
    EXPECT_GT(report->workingBytes, 0u);
    EXPECT_EQ(liveBytes(engine), 0u);

    const an::RejectionClusterDetection& block = clusters[0];
    EXPECT_EQ(block.source, an::RejectionClusterSource::Merged);
    EXPECT_EQ(block.channel, 0u);
    EXPECT_EQ(block.pixelCount, 6u);
    EXPECT_EQ(block.minX, 0u);
    EXPECT_EQ(block.maxX, 1u);
    EXPECT_EQ(block.minY, 0u);
    EXPECT_EQ(block.maxY, 2u);
    EXPECT_EQ(block.integratedRejections, 15u);
    EXPECT_EQ(block.peakRejections, 5u);
    EXPECT_DOUBLE_EQ(block.meanRejections, 15.0 / 6.0);
    // The centroid is weighted by the counts: five pixels of two and one of five at (1, 2).
    EXPECT_DOUBLE_EQ(block.centroidX, (2.0 * (0.0 + 1.0 + 0.0 + 1.0 + 0.0) + 5.0 * 1.0) / 15.0);
    EXPECT_DOUBLE_EQ(block.centroidY, (2.0 * (0.0 + 0.0 + 1.0 + 1.0 + 2.0) + 5.0 * 2.0) / 15.0);

    const an::RejectionClusterDetection& pair = clusters[1];
    EXPECT_EQ(pair.pixelCount, 2u);
    EXPECT_EQ(pair.minX, 6u);
    EXPECT_EQ(pair.maxX, 7u);
    EXPECT_EQ(pair.minY, 4u);
    EXPECT_EQ(pair.maxY, 5u);
    EXPECT_EQ(pair.integratedRejections, 4u);
    EXPECT_EQ(pair.peakRejections, 3u);

    // Without diagonal neighbours the pair splits into singletons below the minimum.
    request.includeDiagonalNeighbours = false;
    const an::Result<an::RejectionClusterReport> fourConnected =
        an::analyzeRejectionClusters(execution, map, extentOf(kWidth, kHeight), request, clusters);
    ASSERT_TRUE(fourConnected.hasValue()) << fourConnected.status().message();
    EXPECT_EQ(fourConnected->clustersFound, 1u);
    EXPECT_EQ(fourConnected->reportedClusterPixels, 6u);

    // A higher minimum count keeps only the pixels rejected that often.
    request.includeDiagonalNeighbours = true;
    request.minimumRejections = 2u;
    request.minimumClusterPixels = 1u;
    const an::Result<an::RejectionClusterReport> frequent =
        an::analyzeRejectionClusters(execution, map, extentOf(kWidth, kHeight), request, clusters);
    ASSERT_TRUE(frequent.hasValue()) << frequent.status().message();
    EXPECT_EQ(frequent->clustersFound, 2u);
    EXPECT_EQ(frequent->reportedClusterPixels, 7u);
    EXPECT_EQ(clusters[1].pixelCount, 1u);
    EXPECT_EQ(clusters[1].minX, 7u);
}

TEST(StackQualityTest, ClusterAnalysisKeepsTheLargestClustersWhenTheOutputIsShortAndOrdersTiesStably)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 12u;
    constexpr std::uint32_t kHeight = 4u;

    // Three horizontal runs separated by empty columns: four, two and three pixels, then a second
    // run of two whose integrated count is larger than the first pair's.
    std::vector<std::uint32_t> map(static_cast<std::size_t>(kWidth) * kHeight, 0u);
    const auto set = [&map](std::uint32_t x, std::uint32_t y, std::uint32_t value) {
        map[static_cast<std::size_t>(y) * kWidth + x] = value;
    };
    for (std::uint32_t x = 0u; x < 4u; ++x)
    {
        set(x, 0u, 1u);
    }
    set(6u, 0u, 1u);
    set(7u, 0u, 1u);
    for (std::uint32_t x = 9u; x < 12u; ++x)
    {
        set(x, 0u, 1u);
    }
    set(0u, 3u, 4u);
    set(1u, 3u, 4u);

    an::RejectionClusterAnalysisRequest request;
    request.minimumClusterPixels = 1u;
    request.source = an::RejectionClusterSource::High;

    std::vector<an::RejectionClusterDetection> all(4u);
    const an::Result<an::RejectionClusterReport> everything =
        an::analyzeRejectionClusters(execution, map, extentOf(kWidth, kHeight), request, all);
    ASSERT_TRUE(everything.hasValue()) << everything.status().message();
    EXPECT_EQ(everything->clustersFound, 4u);
    EXPECT_EQ(everything->clustersReported, 4u);
    EXPECT_EQ(everything->source, an::RejectionClusterSource::High);
    EXPECT_EQ(all[0].pixelCount, 4u);
    EXPECT_EQ(all[1].pixelCount, 3u);
    // The two pairs tie on size; the larger integrated count comes first.
    EXPECT_EQ(all[2].pixelCount, 2u);
    EXPECT_EQ(all[2].integratedRejections, 8u);
    EXPECT_EQ(all[3].pixelCount, 2u);
    EXPECT_EQ(all[3].integratedRejections, 2u);
    EXPECT_EQ(all[3].source, an::RejectionClusterSource::High);

    std::vector<an::RejectionClusterDetection> two(2u);
    const an::Result<an::RejectionClusterReport> truncated =
        an::analyzeRejectionClusters(execution, map, extentOf(kWidth, kHeight), request, two);
    ASSERT_TRUE(truncated.hasValue()) << truncated.status().message();
    EXPECT_EQ(truncated->clustersFound, 4u);
    EXPECT_EQ(truncated->clustersReported, 2u);
    EXPECT_EQ(truncated->reportedClusterPixels, 7u);
    EXPECT_EQ(truncated->largestClusterPixels, 4u);
    EXPECT_EQ(two[0].pixelCount, 4u);
    EXPECT_EQ(two[1].pixelCount, 3u);

    // Two channels are analysed separately and the channel is on every detection.
    std::vector<std::uint32_t> planes(map.size() * 2u, 0u);
    std::copy(map.begin(), map.end(), planes.begin() + static_cast<std::ptrdiff_t>(map.size()));
    std::vector<an::RejectionClusterDetection> perChannel(8u);
    const an::Result<an::RejectionClusterReport> second =
        an::analyzeRejectionClusters(execution, planes, extentOf(kWidth, kHeight, 2u), request, perChannel);
    ASSERT_TRUE(second.hasValue()) << second.status().message();
    EXPECT_EQ(second->clustersFound, 4u);
    EXPECT_EQ(perChannel[0].channel, 1u);
    EXPECT_DOUBLE_EQ(second->clusteredPixelFraction, 11.0 / 96.0);

    // An empty map reports no clusters and no fraction.
    std::vector<std::uint32_t> clean(map.size(), 0u);
    const an::Result<an::RejectionClusterReport> nothing =
        an::analyzeRejectionClusters(execution, clean, extentOf(kWidth, kHeight), request, two);
    ASSERT_TRUE(nothing.hasValue()) << nothing.status().message();
    EXPECT_EQ(nothing->clustersFound, 0u);
    EXPECT_DOUBLE_EQ(nothing->clusteredPixelFraction, 0.0);
}

TEST(StackQualityTest, ClusterTraversalBudgetIncludesFullWidthIndices)
{
    an::EngineContextOptions options;
    options.limits.memoryBudgetBytes = 400u;
    const auto engine = an::EngineContext::create(options);
    ASSERT_TRUE(engine.hasValue());
    const auto execution = makeExecution(*engine);
    std::vector<std::uint32_t> map(64u, 1u);
    std::array<an::RejectionClusterDetection, 1u> clusters{};
    // A 32-bit traversal stack would fit; the full-width one needs 520 bytes.
    const auto result = an::analyzeRejectionClusters(execution, map, extentOf(8u, 8u), {}, clusters);
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.status().code(), an::ErrorCode::MemoryBudgetExceeded);
    EXPECT_EQ(liveBytes(*engine), 0u);
    EXPECT_EQ(clusters[0].pixelCount, 0u);
}

TEST(StackQualityTest, ClusterAnalysisRefusesMismatchedShapesAndHonoursCancellation)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    std::vector<std::uint32_t> map(24u, 1u);
    std::vector<an::RejectionClusterDetection> clusters(2u);
    an::RejectionClusterAnalysisRequest request;

    const an::Result<an::RejectionClusterReport> shape =
        an::analyzeRejectionClusters(execution, map, extentOf(5u, 5u), request, clusters);
    ASSERT_FALSE(shape.hasValue());
    EXPECT_EQ(shape.status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_TRUE(messageNames(shape.status(), "map")) << shape.status().message();

    request.minimumRejections = 0u;
    const an::Result<an::RejectionClusterReport> zero =
        an::analyzeRejectionClusters(execution, map, extentOf(6u, 4u), request, clusters);
    ASSERT_FALSE(zero.hasValue());
    EXPECT_EQ(zero.status().code(), an::ErrorCode::InvalidArgument);
    request.minimumRejections = 1u;

    request.source = static_cast<an::RejectionClusterSource>(9u);
    const an::Result<an::RejectionClusterReport> unknown =
        an::analyzeRejectionClusters(execution, map, extentOf(6u, 4u), request, clusters);
    ASSERT_FALSE(unknown.hasValue());
    EXPECT_EQ(unknown.status().code(), an::ErrorCode::InvalidArgument);
    request.source = an::RejectionClusterSource::Low;

    std::vector<an::RejectionClusterDetection> none;
    const an::Result<an::RejectionClusterReport> noRoom =
        an::analyzeRejectionClusters(execution, map, extentOf(6u, 4u), request, none);
    ASSERT_FALSE(noRoom.hasValue());
    EXPECT_EQ(noRoom.status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_TRUE(messageNames(noRoom.status(), "clusters")) << noRoom.status().message();

    const an::ExecutionContext unheld;
    const an::Result<an::RejectionClusterReport> nothing =
        an::analyzeRejectionClusters(unheld, map, extentOf(6u, 4u), request, clusters);
    ASSERT_FALSE(nothing.hasValue());
    EXPECT_EQ(nothing.status().code(), an::ErrorCode::NotInitialized);

    // A stopped operation on an engine of its own, because the live execution above has this
    // engine's whole thread budget admitted.
    const an::EngineContext other = makeEngine();
    std::stop_source stop;
    stop.request_stop();
    const an::ExecutionContext stopped = makeExecution(other, stop.get_token());
    const an::Result<an::RejectionClusterReport> cancelled =
        an::analyzeRejectionClusters(stopped, map, extentOf(6u, 4u), request, clusters);
    ASSERT_FALSE(cancelled.hasValue());
    EXPECT_EQ(cancelled.status().code(), an::ErrorCode::Cancelled);
    EXPECT_EQ(liveBytes(engine), 0u);
    EXPECT_EQ(liveBytes(other), 0u);
}

/// The workflow's count: the elements of one map whose value reaches the threshold.
[[nodiscard]] std::uint64_t elementsReaching(std::span<const std::uint32_t> map, std::uint32_t minimum)
{
    std::uint64_t counted = 0u;
    for (const std::uint32_t value : map)
    {
        counted += value >= minimum ? 1u : 0u;
    }
    return counted;
}

/// The same count over the union of two maps, each element counted once.
[[nodiscard]] std::uint64_t elementsReachingEither(std::span<const std::uint32_t> low,
                                                   std::span<const std::uint32_t> high, std::uint32_t minimum)
{
    std::uint64_t counted = 0u;
    for (std::size_t index = 0u; index < low.size(); ++index)
    {
        counted += (low[index] >= minimum || high[index] >= minimum) ? 1u : 0u;
    }
    return counted;
}

[[nodiscard]] std::uint64_t sumOf(std::span<const std::uint32_t> map)
{
    std::uint64_t sum = 0u;
    for (const std::uint32_t value : map)
    {
        sum += value;
    }
    return sum;
}

TEST(StackQualityTest, RejectionMapCountsAreTheElementsReachingTheMinimumOnEachSideAndEither)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    // Two channels of a 4 x 3 map: some elements rejected low, some high, some both, most neither.
    const std::vector<std::uint32_t> low{0u, 1u, 0u, 2u, 0u, 0u, 3u, 0u, 0u, 0u, 1u, 0u,
                                         0u, 0u, 0u, 0u, 4u, 0u, 0u, 1u, 0u, 0u, 0u, 0u};
    const std::vector<std::uint32_t> high{0u, 0u, 1u, 2u, 0u, 0u, 0u, 0u, 5u, 0u, 1u, 0u,
                                          1u, 0u, 0u, 0u, 1u, 0u, 0u, 0u, 0u, 0u, 2u, 0u};
    const an::ImageExtent extent = extentOf(4u, 3u, 2u);

    for (const std::uint32_t minimum : {1u, 2u, 4u, 6u})
    {
        const an::Result<an::RejectionMapCounts> counted = an::countRejectionMaps(
            execution, low, high, extent, an::RejectionMapCountRequest{.minimumRejections = minimum});
        ASSERT_TRUE(counted.hasValue()) << counted.status().message();
        EXPECT_EQ(counted->elements, 24u);
        EXPECT_EQ(counted->lowElements, elementsReaching(low, minimum)) << minimum;
        EXPECT_EQ(counted->highElements, elementsReaching(high, minimum)) << minimum;
        EXPECT_EQ(counted->eitherElements, elementsReachingEither(low, high, minimum)) << minimum;
        EXPECT_EQ(counted->lowRejections, sumOf(low));
        EXPECT_EQ(counted->highRejections, sumOf(high));
        EXPECT_DOUBLE_EQ(counted->eitherFraction, static_cast<double>(counted->eitherElements) / 24.0) << minimum;
    }

    // The hand-checked answer at the default minimum of one rejection.
    const an::Result<an::RejectionMapCounts> defaults = an::countRejectionMaps(execution, low, high, extent);
    ASSERT_TRUE(defaults.hasValue()) << defaults.status().message();
    EXPECT_EQ(defaults->lowElements, 6u);
    EXPECT_EQ(defaults->highElements, 7u);
    EXPECT_EQ(defaults->eitherElements, 10u);
    EXPECT_EQ(defaults->lowRejections, 12u);
    EXPECT_EQ(defaults->highRejections, 13u);
    EXPECT_EQ(liveBytes(engine), 0u);
}

TEST(StackQualityTest, AMergedRejectionMapIsCountedAloneAndItsSumIsBothSides)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<std::uint32_t> merged{0u, 2u, 1u, 0u, 7u, 0u, 1u, 0u, 3u};
    const an::Result<an::RejectionMapCounts> counted = an::countRejectionMaps(execution, merged, {}, extentOf(3u, 3u));
    ASSERT_TRUE(counted.hasValue()) << counted.status().message();
    EXPECT_EQ(counted->elements, 9u);
    EXPECT_EQ(counted->lowElements, 5u);
    EXPECT_EQ(counted->highElements, 0u);
    EXPECT_EQ(counted->eitherElements, 5u);
    EXPECT_EQ(counted->lowRejections, 14u);
    EXPECT_EQ(counted->highRejections, 0u);
    EXPECT_DOUBLE_EQ(counted->eitherFraction, 5.0 / 9.0);
}

TEST(StackQualityTest, RejectionMapCountsDoNotDependOnTheThreadCount)
{
    // A map large enough to be split across workers, its counts drawn from a fixed generator.
    constexpr std::uint32_t kWidth = 613u;
    constexpr std::uint32_t kHeight = 389u;
    constexpr std::uint32_t kChannels = 3u;
    const std::size_t elements = static_cast<std::size_t>(kWidth) * kHeight * kChannels;
    std::vector<std::uint32_t> low(elements);
    std::vector<std::uint32_t> high(elements);
    std::uint64_t state = 0x9E3779B97F4A7C15ull;
    for (std::size_t index = 0u; index < elements; ++index)
    {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        const std::uint32_t draw = static_cast<std::uint32_t>(state >> 33u);
        low[index] = (draw % 11u) < 2u ? draw % 5u : 0u;
        high[index] = (draw % 13u) < 3u ? (draw >> 8u) % 7u : 0u;
    }
    const an::ImageExtent extent = extentOf(kWidth, kHeight, kChannels);
    for (const std::uint32_t threads : {1u, 3u, 8u})
    {
        const an::EngineContext engine = makeEngine(threads);
        const an::ExecutionContext execution = makeExecution(engine);
        const an::Result<an::RejectionMapCounts> counted =
            an::countRejectionMaps(execution, low, high, extent, an::RejectionMapCountRequest{.minimumRejections = 2u});
        ASSERT_TRUE(counted.hasValue()) << counted.status().message();
        EXPECT_EQ(counted->elements, elements) << threads;
        EXPECT_EQ(counted->lowElements, elementsReaching(low, 2u)) << threads;
        EXPECT_EQ(counted->highElements, elementsReaching(high, 2u)) << threads;
        EXPECT_EQ(counted->eitherElements, elementsReachingEither(low, high, 2u)) << threads;
        EXPECT_EQ(counted->lowRejections, sumOf(low)) << threads;
        EXPECT_EQ(counted->highRejections, sumOf(high)) << threads;
        EXPECT_EQ(liveBytes(engine), 0u) << threads;
    }
}

TEST(StackQualityTest, RejectionMapCountsRefuseMismatchedShapesAZeroMinimumAndHonourCancellation)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<std::uint32_t> map(24u, 1u);
    const std::vector<std::uint32_t> shortMap(23u, 1u);

    const an::Result<an::RejectionMapCounts> lowShape =
        an::countRejectionMaps(execution, shortMap, map, extentOf(6u, 4u));
    ASSERT_FALSE(lowShape.hasValue());
    EXPECT_EQ(lowShape.status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_TRUE(messageNames(lowShape.status(), "low")) << lowShape.status().message();

    const an::Result<an::RejectionMapCounts> highShape =
        an::countRejectionMaps(execution, map, shortMap, extentOf(6u, 4u));
    ASSERT_FALSE(highShape.hasValue());
    EXPECT_EQ(highShape.status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_TRUE(messageNames(highShape.status(), "high")) << highShape.status().message();

    const an::Result<an::RejectionMapCounts> zero = an::countRejectionMaps(
        execution, map, map, extentOf(6u, 4u), an::RejectionMapCountRequest{.minimumRejections = 0u});
    ASSERT_FALSE(zero.hasValue());
    EXPECT_EQ(zero.status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_TRUE(messageNames(zero.status(), "minimumRejections")) << zero.status().message();

    const an::ExecutionContext unheld;
    const an::Result<an::RejectionMapCounts> nothing = an::countRejectionMaps(unheld, map, map, extentOf(6u, 4u));
    ASSERT_FALSE(nothing.hasValue());
    EXPECT_EQ(nothing.status().code(), an::ErrorCode::NotInitialized);

    // A stopped operation on an engine of its own, because the live execution above has this
    // engine's whole thread budget admitted.
    const an::EngineContext other = makeEngine();
    std::stop_source stop;
    stop.request_stop();
    const an::ExecutionContext stopped = makeExecution(other, stop.get_token());
    const an::Result<an::RejectionMapCounts> cancelled = an::countRejectionMaps(stopped, map, map, extentOf(6u, 4u));
    ASSERT_FALSE(cancelled.hasValue());
    EXPECT_EQ(cancelled.status().code(), an::ErrorCode::Cancelled);
    EXPECT_EQ(liveBytes(engine), 0u);
    EXPECT_EQ(liveBytes(other), 0u);
}

TEST(StackQualityTest, RejectionMapTotalsFeedTheFrameRejectionSummary)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    // Four frames over a 3 x 2 plane, each sample kept, rejected low or rejected high: the maps
    // count each pixel's rejections, and a frame's own count is its rejections over every pixel.
    constexpr std::uint32_t kFrames = 4u;
    constexpr std::uint32_t kPixels = 6u;
    constexpr std::array<std::array<int, kPixels>, kFrames> decision{
        {{0, -1, 0, 1, 0, 0}, {0, 0, 0, 1, 1, 0}, {-1, -1, 0, 0, 0, 0}, {0, 0, 1, 1, 0, -1}}};
    std::vector<std::uint32_t> low(kPixels, 0u);
    std::vector<std::uint32_t> high(kPixels, 0u);
    std::vector<std::uint64_t> valid(kFrames, kPixels);
    std::vector<std::uint64_t> rejected(kFrames, 0u);
    for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
    {
        for (std::uint32_t pixel = 0u; pixel < kPixels; ++pixel)
        {
            low[pixel] += decision[frame][pixel] < 0 ? 1u : 0u;
            high[pixel] += decision[frame][pixel] > 0 ? 1u : 0u;
            rejected[frame] += decision[frame][pixel] != 0 ? 1u : 0u;
        }
    }
    const an::Result<an::RejectionMapCounts> counted = an::countRejectionMaps(execution, low, high, extentOf(3u, 2u));
    ASSERT_TRUE(counted.hasValue()) << counted.status().message();
    EXPECT_EQ(counted->lowRejections, 4u);
    EXPECT_EQ(counted->highRejections, 5u);

    std::vector<an::FrameRejectionRate> rates(kFrames);
    const an::Result<an::FrameRejectionSummary> summary =
        an::summarizeFrameRejection(execution, valid, rejected, counted->lowRejections, counted->highRejections, rates);
    ASSERT_TRUE(summary.hasValue()) << summary.status().message();
    EXPECT_EQ(summary->rejectedLow, 4u);
    EXPECT_EQ(summary->rejectedHigh, 5u);
    EXPECT_DOUBLE_EQ(summary->globalRate, 9.0 / 24.0);
}

TEST(StackQualityTest, NameTablesAreClosed)
{
    EXPECT_STREQ(an::rejectionClusterSourceName(an::RejectionClusterSource::Merged), "Merged");
    EXPECT_STREQ(an::rejectionClusterSourceName(an::RejectionClusterSource::Low), "Low");
    EXPECT_STREQ(an::rejectionClusterSourceName(an::RejectionClusterSource::High), "High");
    EXPECT_STREQ(an::rejectionClusterSourceName(static_cast<an::RejectionClusterSource>(7u)),
                 "UnknownRejectionClusterSource");
    EXPECT_TRUE(an::isRejectionClusterSource(an::RejectionClusterSource::High));
    EXPECT_FALSE(an::isRejectionClusterSource(static_cast<an::RejectionClusterSource>(3u)));
    static_assert(an::isRejectionClusterSource(an::RejectionClusterSource::Merged));
}

} // namespace
