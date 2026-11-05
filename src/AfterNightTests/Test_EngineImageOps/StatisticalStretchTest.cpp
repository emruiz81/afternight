// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the bounded statistical-stretch replacement.

#include <AfterNightEngine/Algorithms/Pointwise/StatisticalStretch.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
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

[[nodiscard]] an::ImageLayout layoutOf(std::uint32_t width, std::uint32_t height, std::uint32_t channels,
                                       bool interleaved = false)
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, channels, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const std::optional<an::ImageLayout> layout =
        an::ImageLayout::create(an::ImageLayoutRequest{.extent = *extent,
                                                       .elementSizeBytes = sizeof(float),
                                                       .samplesPerPixel = interleaved ? channels : 1u,
                                                       .planeCount = interleaved ? 1u : channels,
                                                       .rowAlignmentBytes = 1u},
                                an::defaultEngineLimits());
    EXPECT_TRUE(layout.has_value());
    return *layout;
}

[[nodiscard]] float& sampleAt(std::vector<float>& samples, const an::ImageLayout& layout, std::uint32_t channel,
                              std::uint32_t x, std::uint32_t y)
{
    const std::uint64_t offset =
        *layout.sampleByteOffset(channel / layout.samplesPerPixel(), y, x, channel % layout.samplesPerPixel());
    return *reinterpret_cast<float*>(reinterpret_cast<std::uint8_t*>(samples.data()) + offset);
}

[[nodiscard]] double referenceValue(double value, double blackPoint, double median, double target)
{
    const double blackDenominator = (std::max)(1.0 - blackPoint, 1.0e-12);
    const double medianRescaled = (median - blackPoint) / blackDenominator;
    const double rescaled = (value - blackPoint) / blackDenominator;
    const double numerator = (medianRescaled - 1.0) * target * rescaled;
    double denominator = medianRescaled * (target + rescaled - 1.0) - target * rescaled;
    if (std::abs(denominator) < 1.0e-12)
    {
        denominator = 1.0e-12;
    }
    return numerator / denominator;
}

} // namespace

TEST(StatisticalStretchTest, LinkedAndUnlinkedAnalysesNameTheirPopulations)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout layout = layoutOf(4u, 1u, 3u);
    std::vector<float> samples(layout.totalBytes() / sizeof(float), 0.0f);
    constexpr std::array<std::array<float, 4u>, 3u> values{
        {{0.1f, 0.2f, 0.3f, 0.4f}, {0.4f, 0.5f, 0.6f, 0.7f}, {0.7f, 0.8f, 0.9f, 1.0f}}};
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        for (std::uint32_t x = 0u; x < 4u; ++x)
        {
            sampleAt(samples, layout, channel, x, 0u) = values[channel][x];
        }
    }
    const an::Result<an::StatisticalStretchAnalysis> linked =
        an::analyzeImageStatisticalStretch(execution, an::SampleType::Float32, samples.data(), layout, true);
    const an::Result<an::StatisticalStretchAnalysis> unlinked =
        an::analyzeImageStatisticalStretch(execution, an::SampleType::Float32, samples.data(), layout, false);
    ASSERT_TRUE(linked.hasValue()) << linked.status().message();
    ASSERT_TRUE(unlinked.hasValue()) << unlinked.status().message();
    EXPECT_EQ(linked->selection.valid, 4u);
    EXPECT_EQ(linked->channels[0].sampledValues, 12u);
    EXPECT_NEAR(linked->channels[0].median, 0.55, 1.0e-7);
    EXPECT_EQ(unlinked->channels[0].sampledValues, 4u);
    EXPECT_NEAR(unlinked->channels[0].median, 0.25, 1.0e-7);
    EXPECT_NEAR(unlinked->channels[1].median, 0.55, 1.0e-7);
    EXPECT_NEAR(unlinked->channels[2].median, 0.85, 1.0e-7);
    EXPECT_LE(unlinked->peakScratchBytes, 3u * an::kMaxStatisticalStretchSamplesPerChannel * sizeof(double));
}

TEST(StatisticalStretchTest, BlackpointSigmaAndNoClipProduceTheirDocumentedDecisions)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout layout = layoutOf(8u, 1u, 1u);
    std::vector<float> samples(layout.totalBytes() / sizeof(float), 0.0f);
    constexpr std::array<float, 8u> values{0.02f, 0.08f, 0.15f, 0.22f, 0.30f, 0.42f, 0.62f, 0.85f};
    for (std::uint32_t x = 0u; x < values.size(); ++x)
    {
        sampleAt(samples, layout, 0u, x, 0u) = values[x];
    }
    const an::Result<an::StatisticalStretchAnalysis> analysis =
        an::analyzeImageStatisticalStretch(execution, an::SampleType::Float32, samples.data(), layout, true);
    ASSERT_TRUE(analysis.hasValue()) << analysis.status().message();
    const an::Result<an::StatisticalStretchClipStatistics> aggressive = an::computeStatisticalStretchClipStatistics(
        execution, an::SampleType::Float32, samples.data(), layout, *analysis, 0.5, false);
    const an::Result<an::StatisticalStretchClipStatistics> gentle = an::computeStatisticalStretchClipStatistics(
        execution, an::SampleType::Float32, samples.data(), layout, *analysis, 5.0, false);
    const an::Result<an::StatisticalStretchClipStatistics> noClip = an::computeStatisticalStretchClipStatistics(
        execution, an::SampleType::Float32, samples.data(), layout, *analysis, 5.0, true);
    ASSERT_TRUE(aggressive.hasValue());
    ASSERT_TRUE(gentle.hasValue());
    ASSERT_TRUE(noClip.hasValue());
    EXPECT_GT(aggressive->clippedPixels, gentle->clippedPixels);
    EXPECT_NEAR(noClip->blackPoints[0], values.front(), 1.0e-7);
    EXPECT_EQ(noClip->clippedPixels, 1u);
    EXPECT_EQ(noClip->totalPixels, values.size());
}

TEST(StatisticalStretchTest, TheAnalyzedMedianMovesExactlyToTheRequestedTarget)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout layout = layoutOf(5u, 1u, 1u);
    std::vector<float> samples(layout.totalBytes() / sizeof(float), 0.0f);
    std::vector<float> output(samples.size(), -1.0f);
    constexpr std::array<float, 5u> values{0.1f, 0.2f, 0.4f, 0.8f, 1.0f};
    for (std::uint32_t x = 0u; x < values.size(); ++x)
    {
        sampleAt(samples, layout, 0u, x, 0u) = values[x];
    }
    an::StatisticalStretchParameters parameters;
    parameters.targetMedian = 0.3;
    parameters.noBlackClip = true;
    const an::Result<an::StatisticalStretchResult> result = an::stretchImageStatistically(
        execution, an::SampleType::Float32, samples.data(), layout, output.data(), layout, parameters);
    ASSERT_TRUE(result.hasValue()) << result.status().message();
    EXPECT_NEAR(sampleAt(output, layout, 0u, 2u, 0u), parameters.targetMedian, 1.0e-6);
    for (std::uint32_t x = 0u; x < values.size(); ++x)
    {
        const double expected =
            std::clamp(referenceValue(values[x], values.front(), 0.4, parameters.targetMedian), 0.0, 1.0);
        EXPECT_NEAR(sampleAt(output, layout, 0u, x, 0u), expected, 1.0e-6);
    }
}

TEST(StatisticalStretchTest, CurvesNormalizationMixedLayoutsAndExactInPlaceAreSupported)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout planar = layoutOf(4u, 2u, 3u);
    const an::ImageLayout interleaved = layoutOf(4u, 2u, 3u, true);
    std::vector<float> source(planar.totalBytes() / sizeof(float), 0.0f);
    std::vector<float> mixed(interleaved.totalBytes() / sizeof(float), -1.0f);
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        for (std::uint32_t y = 0u; y < 2u; ++y)
        {
            for (std::uint32_t x = 0u; x < 4u; ++x)
            {
                sampleAt(source, planar, channel, x, y) =
                    0.05f + 0.07f * static_cast<float>(x + y * 4u) + 0.03f * static_cast<float>(channel);
            }
        }
    }
    an::StatisticalStretchParameters parameters;
    parameters.linked = false;
    parameters.normalize = true;
    parameters.applyCurves = true;
    parameters.curvesBoost = 0.4;
    const an::Result<an::StatisticalStretchAnalysis> analysis =
        an::analyzeImageStatisticalStretch(execution, an::SampleType::Float32, source.data(), planar, false);
    ASSERT_TRUE(analysis.hasValue());
    const an::Result<an::StatisticalStretchResult> result = an::stretchImageStatistically(
        execution, an::SampleType::Float32, source.data(), planar, mixed.data(), interleaved, parameters, &*analysis);
    ASSERT_TRUE(result.hasValue()) << result.status().message();
    EXPECT_GT(result->normalizationDivisor, 0.0);
    float maximum = 0.0f;
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        for (std::uint32_t y = 0u; y < 2u; ++y)
        {
            for (std::uint32_t x = 0u; x < 4u; ++x)
            {
                maximum = (std::max)(maximum, sampleAt(mixed, interleaved, channel, x, y));
            }
        }
    }
    EXPECT_NEAR(maximum, 1.0, 1.0e-6);

    std::vector<float> inPlace = source;
    parameters.normalize = false;
    parameters.applyCurves = false;
    ASSERT_TRUE(an::stretchImageStatistically(execution, an::SampleType::Float32, inPlace.data(), planar,
                                              inPlace.data(), planar, parameters, &*analysis)
                    .hasValue());
    EXPECT_NE(inPlace, source);
}

TEST(StatisticalStretchTest, ValidityAndPrecomputedShapeAreChecked)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout layout = layoutOf(3u, 1u, 1u);
    std::vector<float> samples(layout.totalBytes() / sizeof(float), 0.0f);
    std::vector<float> output(samples.size(), -1.0f);
    sampleAt(samples, layout, 0u, 0u, 0u) = 0.0f;
    sampleAt(samples, layout, 0u, 1u, 0u) = 0.4f;
    sampleAt(samples, layout, 0u, 2u, 0u) = 0.8f;
    an::KernelValidityView validity;
    validity.descriptor = *an::ValidityDescriptor::create({.rule = an::ValidityRule::ZeroIsNoData});
    const an::Result<an::StatisticalStretchAnalysis> analysis =
        an::analyzeImageStatisticalStretch(execution, an::SampleType::Float32, samples.data(), layout, true, validity);
    ASSERT_TRUE(analysis.hasValue()) << analysis.status().message();
    EXPECT_EQ(analysis->selection.valid, 2u);
    ASSERT_TRUE(an::stretchImageStatistically(execution, an::SampleType::Float32, samples.data(), layout, output.data(),
                                              layout, {}, &*analysis, validity)
                    .hasValue());
    EXPECT_FLOAT_EQ(output[0], -1.0f);

    an::StatisticalStretchAnalysis wrong = *analysis;
    wrong.width = 99u;
    EXPECT_EQ(an::stretchImageStatistically(execution, an::SampleType::Float32, samples.data(), layout, output.data(),
                                            layout, {}, &wrong, validity)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
}

namespace
{

[[nodiscard]] an::EngineContext makeEngineWith(std::uint32_t workers)
{
    an::EngineContextOptions options;
    options.maxConcurrency = workers;
    an::Result<an::EngineContext> made = an::EngineContext::create(options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::EngineContext{};
}

/// A deterministic image in the unit interval: a non-quantized spread, one value in four drawn from
/// 64 repeated codes, and a minimum placed at a pixel the bounded sample's stride never visits.
[[nodiscard]] std::vector<float> spreadImage(const an::ImageLayout& layout, std::uint64_t seed)
{
    std::vector<float> samples(layout.totalBytes() / sizeof(float), 0.0f);
    const std::uint32_t width = layout.extent().width();
    const std::uint32_t height = layout.extent().height();
    std::uint64_t state = seed;
    for (std::uint32_t channel = 0u; channel < layout.extent().channelCount(); ++channel)
    {
        for (std::uint32_t y = 0u; y < height; ++y)
        {
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                state = state * 6364136223846793005ull + 1442695040888963407ull;
                const float unit = static_cast<float>(static_cast<double>(state >> 11) * 0x1p-53);
                sampleAt(samples, layout, channel, x, y) =
                    (state >> 7) % 4u == 0u ? std::round(unit * 64.0f) / 64.0f + 0.03125f : 0.02f + 0.9f * unit;
            }
        }
    }
    sampleAt(samples, layout, 0u, width / 3u + 1u, height / 2u) = 0.0078125f;
    return samples;
}

/// The lower middle value plus half the distance to the upper one.
[[nodiscard]] double sortedMedian(std::vector<double> values)
{
    std::sort(values.begin(), values.end());
    const double lower = values[(values.size() - 1u) / 2u];
    const double upper = values[values.size() / 2u];
    return lower + 0.5 * (upper - lower);
}

/// The analysis the stretch is defined by, from the whole image by sorting: the exact minimum and
/// median of a group's admitted samples, and the spread of the stated stride sample.
struct ReferenceAnalysis
{
    double minimum = 0.0;
    double median = 0.0;
    double lowerHalfSigma = 0.0;
    std::uint64_t sampledValues = 0u;
    std::uint64_t admittedPixels = 0u;
};

[[nodiscard]] ReferenceAnalysis referenceAnalysis(const std::vector<float>& samples, const an::ImageLayout& layout,
                                                  bool linked, std::uint32_t group, bool zeroIsNoData)
{
    const std::uint32_t width = layout.extent().width();
    const std::uint32_t channels = layout.extent().channelCount();
    const std::uint64_t pixels = static_cast<std::uint64_t>(width) * layout.extent().height();
    std::vector<float> image = samples;
    const auto admitted = [&](std::uint64_t pixel) {
        for (std::uint32_t channel = 0u; channel < channels; ++channel)
            if (zeroIsNoData && sampleAt(image, layout, channel, static_cast<std::uint32_t>(pixel % width),
                                         static_cast<std::uint32_t>(pixel / width)) == 0.0f)
                return false;
        return true;
    };
    const std::uint32_t first = linked ? 0u : group;
    const std::uint32_t last = linked ? channels : group + 1u;
    ReferenceAnalysis result;
    std::vector<double> all;
    for (std::uint64_t pixel = 0u; pixel < pixels; ++pixel)
    {
        if (!admitted(pixel))
            continue;
        ++result.admittedPixels;
        for (std::uint32_t channel = first; channel < last; ++channel)
            all.push_back(sampleAt(image, layout, channel, static_cast<std::uint32_t>(pixel % width),
                                   static_cast<std::uint32_t>(pixel / width)));
    }
    result.minimum = *std::min_element(all.begin(), all.end());
    result.median = sortedMedian(all);
    // Every admitted candidate whose index is a multiple of the stride, plus the first admitted one.
    const std::uint64_t candidates = linked ? pixels * channels : pixels;
    const std::uint64_t stride =
        (std::max)(std::uint64_t{1}, (candidates + an::kMaxStatisticalStretchSamplesPerChannel - 1u) /
                                         an::kMaxStatisticalStretchSamplesPerChannel);
    std::vector<double> sample;
    bool firstTaken = false;
    for (std::uint64_t candidate = 0u; candidate < candidates; ++candidate)
    {
        const std::uint32_t channel = linked ? static_cast<std::uint32_t>(candidate / pixels) : group;
        const std::uint64_t pixel = linked ? candidate % pixels : candidate;
        if (!admitted(pixel) || (firstTaken && candidate % stride != 0u))
            continue;
        firstTaken = true;
        sample.push_back(sampleAt(image, layout, channel, static_cast<std::uint32_t>(pixel % width),
                                  static_cast<std::uint32_t>(pixel / width)));
    }
    result.sampledValues = sample.size();
    const double sampleMedian = sortedMedian(sample);
    std::vector<double> lower;
    for (const double value : sample)
        if (value <= sampleMedian)
            lower.push_back(value);
    const double lowerMedian = sortedMedian(lower);
    for (double& value : lower)
        value = std::abs(value - lowerMedian);
    result.lowerHalfSigma = sortedMedian(lower) * 1.4826;
    return result;
}

/// The stretch of one sample from its definition: everything at or below the black point is cut to
/// zero and nothing passes one, then the transfer, then the clamp.
[[nodiscard]] double definedValue(double value, double blackPoint, double median, double target)
{
    const double scale = (std::max)(1.0 - blackPoint, 1.0e-12);
    const double m = (median - blackPoint) / scale;
    const double r = std::clamp((value - blackPoint) / scale, 0.0, 1.0);
    const double denominator = m * (1.0 - target) + r * (target - m);
    const double transferred =
        std::abs(denominator) < 1.0e-12 ? -(1.0 - m) * target * r / 1.0e-12 : (1.0 - m) * target * r / denominator;
    return std::clamp(transferred, 0.0, 1.0);
}

void expectAnalysis(const an::StatisticalStretchChannelAnalysis& actual, const ReferenceAnalysis& expected,
                    const char* label)
{
    EXPECT_EQ(actual.minimum, expected.minimum) << label;
    EXPECT_EQ(actual.median, expected.median) << label;
    EXPECT_EQ(actual.lowerHalfSigma, expected.lowerHalfSigma) << label;
    EXPECT_EQ(actual.sampledValues, expected.sampledValues) << label;
}

} // namespace

TEST(StatisticalStretchTest, MinimumAndMedianAreEveryAdmittedSamplesOrderStatisticsAndTheSpreadIsTheStrideSample)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    // More samples than the bounded sample holds, and an even count, so the median averages a pair.
    const an::ImageLayout mono = layoutOf(1001u, 1000u, 1u);
    const std::vector<float> monoSamples = spreadImage(mono, 11u);
    const auto monoAnalysis =
        an::analyzeImageStatisticalStretch(execution, an::SampleType::Float32, monoSamples.data(), mono, true);
    ASSERT_TRUE(monoAnalysis.hasValue()) << monoAnalysis.status().message();
    const ReferenceAnalysis monoReference = referenceAnalysis(monoSamples, mono, true, 0u, false);
    expectAnalysis(monoAnalysis->channels[0], monoReference, "mono");
    EXPECT_EQ(monoAnalysis->channels[0].minimum, 0.0078125) << "the minimum sits off every sampled index";
    EXPECT_EQ(monoAnalysis->selection.valid, monoReference.admittedPixels);

    // Linked RGB pools its three channels; unlinked RGB takes each channel over the pixels whose every
    // channel is admitted.
    const an::ImageLayout rgb = layoutOf(640u, 480u, 3u, true);
    std::vector<float> rgbSamples = spreadImage(rgb, 23u);
    for (std::uint32_t x = 0u; x < 640u; x += 7u)
        sampleAt(rgbSamples, rgb, 1u, x, (x * 13u) % 480u) = 0.0f;
    an::KernelValidityView noData;
    noData.descriptor = *an::ValidityDescriptor::create({.rule = an::ValidityRule::ZeroIsNoData});
    for (const bool linked : {true, false})
    {
        const auto analysis = an::analyzeImageStatisticalStretch(execution, an::SampleType::Float32, rgbSamples.data(),
                                                                 rgb, linked, noData);
        ASSERT_TRUE(analysis.hasValue()) << analysis.status().message();
        for (std::uint32_t group = 0u; group < (linked ? 1u : 3u); ++group)
        {
            const ReferenceAnalysis reference = referenceAnalysis(rgbSamples, rgb, linked, group, true);
            expectAnalysis(analysis->channels[group], reference, linked ? "linked" : "unlinked");
            EXPECT_EQ(analysis->selection.valid, reference.admittedPixels);
        }
    }
}

TEST(StatisticalStretchTest, Float64ImagesAreAnalyzedExactlyAndStretchedByTheSameDefinition)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout floats = layoutOf(1003u, 997u, 1u);
    const std::vector<float> narrow = spreadImage(floats, 31u);
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(1003u, 997u, 1u, an::defaultEngineLimits());
    const std::optional<an::ImageLayout> layout =
        an::ImageLayout::create(an::ImageLayoutRequest{.extent = *extent,
                                                       .elementSizeBytes = sizeof(double),
                                                       .samplesPerPixel = 1u,
                                                       .planeCount = 1u,
                                                       .rowAlignmentBytes = 1u},
                                an::defaultEngineLimits());
    ASSERT_TRUE(layout.has_value());
    std::vector<double> samples(narrow.size());
    for (std::size_t i = 0u; i < samples.size(); ++i)
        samples[i] = static_cast<double>(narrow[i]) + 0x1p-40 * static_cast<double>(i % 977u);
    std::vector<double> sorted(samples);
    std::sort(sorted.begin(), sorted.end());
    const auto analysis =
        an::analyzeImageStatisticalStretch(execution, an::SampleType::Float64, samples.data(), *layout, true);
    ASSERT_TRUE(analysis.hasValue()) << analysis.status().message();
    EXPECT_EQ(analysis->channels[0].minimum, sorted.front());
    EXPECT_EQ(analysis->channels[0].median, sorted[sorted.size() / 2u]) << "an odd count's middle value";
    std::vector<double> output(samples.size(), -1.0);
    an::StatisticalStretchParameters parameters;
    const auto result = an::stretchImageStatistically(execution, an::SampleType::Float64, samples.data(), *layout,
                                                      output.data(), *layout, parameters, &*analysis);
    ASSERT_TRUE(result.hasValue()) << result.status().message();
    double worst = 0.0;
    for (std::size_t i = 0u; i < samples.size(); ++i)
        worst = (std::max)(worst,
                           std::abs(output[i] - definedValue(samples[i], result->clipping.blackPoints[0],
                                                             analysis->channels[0].median, parameters.targetMedian)));
    EXPECT_LE(worst, 1.0e-12);
}

TEST(StatisticalStretchTest, EverythingAtOrBelowTheBlackPointIsCutToPositiveZero)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    // A background of 0.05 with a black margin and cold pixels: far enough below the black point
    // that the uncut transfer passes its pole and maps them above one.
    const an::ImageLayout layout = layoutOf(512u, 512u, 1u);
    std::vector<float> samples(layout.totalBytes() / sizeof(float), 0.0f);
    std::uint64_t state = 5u;
    for (std::uint32_t y = 0u; y < 512u; ++y)
    {
        for (std::uint32_t x = 0u; x < 512u; ++x)
        {
            state = state * 6364136223846793005ull + 1442695040888963407ull;
            const bool margin = x < 8u || y < 8u || x >= 504u || y >= 504u;
            const float noise = static_cast<float>(static_cast<double>(state >> 11) * 0x1p-53) - 0.5f;
            sampleAt(samples, layout, 0u, x, y) = margin                       ? 0.0f
                                                  : (x * 31u + y) % 997u == 3u ? 0.01f
                                                                               : 0.05f + 0.008f * noise;
        }
    }
    std::vector<float> output(samples.size(), -1.0f);
    an::StatisticalStretchParameters parameters;
    const auto result = an::stretchImageStatistically(execution, an::SampleType::Float32, samples.data(), layout,
                                                      output.data(), layout, parameters);
    ASSERT_TRUE(result.hasValue()) << result.status().message();
    const double blackPoint = result->clipping.blackPoints[0];
    ASSERT_GT(blackPoint, 0.03);
    std::uint64_t cut = 0u;
    for (std::size_t i = 0u; i < samples.size(); ++i)
    {
        if (samples[i] <= blackPoint)
        {
            EXPECT_EQ(output[i], 0.0f) << i;
            EXPECT_FALSE(std::signbit(output[i])) << i;
            ++cut;
        }
        EXPECT_NEAR(output[i],
                    definedValue(samples[i], blackPoint, result->analysis.channels[0].median, parameters.targetMedian),
                    1.0e-6)
            << i;
    }
    EXPECT_EQ(result->clipping.clippedPixels, cut);

    // With no spread allowed below it, the black point is the median: everything at or below it is
    // cut to positive zero and everything above it reaches one.
    parameters.blackpointSigma = 0.0;
    const auto median = an::stretchImageStatistically(execution, an::SampleType::Float32, samples.data(), layout,
                                                      output.data(), layout, parameters);
    ASSERT_TRUE(median.hasValue()) << median.status().message();
    ASSERT_EQ(median->clipping.blackPoints[0], median->analysis.channels[0].median);
    for (std::size_t i = 0u; i < samples.size(); ++i)
    {
        if (samples[i] <= median->clipping.blackPoints[0])
        {
            EXPECT_EQ(output[i], 0.0f) << i;
            EXPECT_FALSE(std::signbit(output[i])) << i;
        }
        else
        {
            EXPECT_EQ(output[i], 1.0f) << i;
        }
    }
}

TEST(StatisticalStretchTest, NoBlackClipCutsOnlyTheImagesMinimumAndTheMedianReachesTheTarget)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    // An odd count, so the median is one sample and the stretch sends it to the target exactly.
    const an::ImageLayout layout = layoutOf(1001u, 1001u, 1u);
    const std::vector<float> samples = spreadImage(layout, 47u);
    std::vector<float> output(samples.size(), -1.0f);
    an::StatisticalStretchParameters parameters;
    parameters.noBlackClip = true;
    const auto result = an::stretchImageStatistically(execution, an::SampleType::Float32, samples.data(), layout,
                                                      output.data(), layout, parameters);
    ASSERT_TRUE(result.hasValue()) << result.status().message();
    EXPECT_EQ(result->clipping.blackPoints[0], 0.0078125);
    EXPECT_EQ(result->clipping.clippedPixels, 1u) << "only the minimum itself is at the black point";
    std::size_t positive = 0u;
    for (const float value : output)
        positive += value > 0.0f ? 1u : 0u;
    EXPECT_EQ(positive, output.size() - 1u);
    std::vector<float> sorted(output);
    std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(sorted.size() / 2u), sorted.end());
    EXPECT_EQ(sorted[sorted.size() / 2u], static_cast<float>(parameters.targetMedian));
}

TEST(StatisticalStretchTest, EveryWidthGivesTheSameAnalysisAndOutput)
{
    const an::ImageLayout mono = layoutOf(1024u, 700u, 1u);
    const an::ImageLayout rgb = layoutOf(500u, 400u, 3u);
    const std::vector<float> monoSamples = spreadImage(mono, 3u);
    const std::vector<float> rgbSamples = spreadImage(rgb, 4u);
    struct Case
    {
        const an::ImageLayout* layout;
        const std::vector<float>* samples;
        bool linked;
    };
    for (const Case& item :
         {Case{&mono, &monoSamples, true}, Case{&rgb, &rgbSamples, true}, Case{&rgb, &rgbSamples, false}})
    {
        std::vector<float> first;
        an::StatisticalStretchAnalysis firstAnalysis;
        for (const std::uint32_t workers : {1u, 4u, 16u})
        {
            const an::EngineContext engine = makeEngineWith(workers);
            const an::ExecutionContext execution = makeExecution(engine);
            std::vector<float> output(item.samples->size(), -1.0f);
            an::StatisticalStretchParameters parameters;
            parameters.linked = item.linked;
            parameters.normalize = true;
            const auto result = an::stretchImageStatistically(execution, an::SampleType::Float32, item.samples->data(),
                                                              *item.layout, output.data(), *item.layout, parameters);
            ASSERT_TRUE(result.hasValue()) << result.status().message();
            if (workers == 1u)
            {
                first = output;
                firstAnalysis = result->analysis;
                continue;
            }
            EXPECT_EQ(output, first) << workers;
            for (std::uint32_t group = 0u; group < 3u; ++group)
            {
                EXPECT_EQ(result->analysis.channels[group].median, firstAnalysis.channels[group].median);
                EXPECT_EQ(result->analysis.channels[group].lowerHalfSigma,
                          firstAnalysis.channels[group].lowerHalfSigma);
            }
        }
    }
}

TEST(StatisticalStretchTest, PreparedAnalysisMatchesTheCompleteCallAndItsClipCount)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout layout = layoutOf(900u, 800u, 1u);
    const std::vector<float> samples = spreadImage(layout, 9u);
    an::StatisticalStretchParameters parameters;
    parameters.blackpointSigma = 1.0;
    std::vector<float> complete(samples.size(), -1.0f);
    std::vector<float> prepared(samples.size(), -1.0f);
    const auto whole = an::stretchImageStatistically(execution, an::SampleType::Float32, samples.data(), layout,
                                                     complete.data(), layout, parameters);
    ASSERT_TRUE(whole.hasValue()) << whole.status().message();
    const auto analysis =
        an::analyzeImageStatisticalStretch(execution, an::SampleType::Float32, samples.data(), layout, true);
    ASSERT_TRUE(analysis.hasValue());
    const auto reused = an::stretchImageStatistically(execution, an::SampleType::Float32, samples.data(), layout,
                                                      prepared.data(), layout, parameters, &*analysis);
    ASSERT_TRUE(reused.hasValue()) << reused.status().message();
    EXPECT_EQ(prepared, complete);
    const auto clipping = an::computeStatisticalStretchClipStatistics(
        execution, an::SampleType::Float32, samples.data(), layout, *analysis, parameters.blackpointSigma, false);
    ASSERT_TRUE(clipping.hasValue());
    for (const auto* result : {&*whole, &*reused})
    {
        EXPECT_EQ(result->clipping.clippedPixels, clipping->clippedPixels);
        EXPECT_EQ(result->clipping.totalPixels, clipping->totalPixels);
        EXPECT_EQ(result->clipping.blackPoints, clipping->blackPoints);
    }
    std::uint64_t cut = 0u;
    for (const float value : samples)
        cut += value <= clipping->blackPoints[0] ? 1u : 0u;
    EXPECT_EQ(clipping->clippedPixels, cut);
    EXPECT_GT(cut, 1000u);
}

TEST(StatisticalStretchTest, AStalePreparedAnalysisIsRefusedBeforeAnyWrite)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout layout = layoutOf(300u, 200u, 1u);
    std::vector<float> samples = spreadImage(layout, 13u);
    for (std::uint32_t x = 0u; x < 300u; x += 5u)
        sampleAt(samples, layout, 0u, x, 7u) = 0.0f;
    an::KernelValidityView noData;
    noData.descriptor = *an::ValidityDescriptor::create({.rule = an::ValidityRule::ZeroIsNoData});
    const auto analysis =
        an::analyzeImageStatisticalStretch(execution, an::SampleType::Float32, samples.data(), layout, true, noData);
    ASSERT_TRUE(analysis.hasValue());
    sampleAt(samples, layout, 0u, 5u, 7u) = 0.5f; // one more pixel now means something
    std::vector<float> output(samples.size(), -1.0f);
    const auto stale = an::stretchImageStatistically(execution, an::SampleType::Float32, samples.data(), layout,
                                                     output.data(), layout, {}, &*analysis, noData);
    EXPECT_EQ(stale.status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_TRUE(std::all_of(output.begin(), output.end(), [](float value) { return value == -1.0f; }))
        << "nothing is written before the analysis is found stale";
}

TEST(StatisticalStretchTest, AnalysisHoldsOneBoundedSampleAtATime)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout rgb = layoutOf(1000u, 900u, 3u);
    const std::vector<float> samples = spreadImage(rgb, 17u);
    const auto analysis =
        an::analyzeImageStatisticalStretch(execution, an::SampleType::Float32, samples.data(), rgb, false);
    ASSERT_TRUE(analysis.hasValue()) << analysis.status().message();
    for (std::uint32_t group = 0u; group < 3u; ++group)
        EXPECT_EQ(analysis->channels[group].sampledValues, 300000u) << "every third of 900,000 candidates";
    // The channels' samples are taken one after another, and the exact selection's working memory is
    // bounded by its own ceiling rather than by the image.
    EXPECT_LE(analysis->peakScratchBytes, an::kMaxStatisticalStretchSamplesPerChannel * sizeof(double) + (2u << 20u));
}

namespace
{

/// The exact minimum and median of every value of `channels` over all pixels, by sorting.
template <typename SampleT>
[[nodiscard]] std::pair<double, double> sortedOrderStatistics(const std::vector<SampleT>& planes, std::uint64_t pixels,
                                                              std::uint32_t first, std::uint32_t last)
{
    std::vector<double> values;
    for (std::uint32_t channel = first; channel < last; ++channel)
        for (std::uint64_t pixel = 0u; pixel < pixels; ++pixel)
            values.push_back(static_cast<double>(planes[channel * pixels + pixel]));
    return {*std::min_element(values.begin(), values.end()), sortedMedian(values)};
}

template <typename SampleT> void checkRepeatedRgb(an::SampleType sampleType)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 301u;
    constexpr std::uint32_t kHeight = 200u;
    constexpr std::uint64_t kPixels = std::uint64_t{kWidth} * kHeight;
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(kWidth, kHeight, 3u, an::defaultEngineLimits());
    const std::optional<an::ImageLayout> layout =
        an::ImageLayout::create(an::ImageLayoutRequest{.extent = *extent,
                                                       .elementSizeBytes = sizeof(SampleT),
                                                       .samplesPerPixel = 1u,
                                                       .planeCount = 3u,
                                                       .rowAlignmentBytes = 1u},
                                an::defaultEngineLimits());
    ASSERT_TRUE(layout.has_value());
    // A few thousand copies of each of a handful of values, negative ones included, so every median
    // lands in a run far larger than a gather and must be fixed from its key.
    std::vector<SampleT> planes(3u * kPixels);
    std::uint64_t state = 99u;
    for (std::size_t i = 0u; i < planes.size(); ++i)
    {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        planes[i] = static_cast<SampleT>(static_cast<double>(static_cast<int>((state >> 33) % 9u) - 3) / 16.0);
    }
    planes[kPixels + 17u] = static_cast<SampleT>(-0.75);
    for (const bool linked : {true, false})
    {
        const auto analysis = an::analyzeImageStatisticalStretch(execution, sampleType, planes.data(), *layout, linked);
        ASSERT_TRUE(analysis.hasValue()) << analysis.status().message();
        for (std::uint32_t group = 0u; group < (linked ? 1u : 3u); ++group)
        {
            const auto [minimum, median] =
                sortedOrderStatistics(planes, kPixels, linked ? 0u : group, linked ? 3u : group + 1u);
            EXPECT_EQ(analysis->channels[group].minimum, minimum) << linked << ' ' << group;
            EXPECT_EQ(analysis->channels[group].median, median) << linked << ' ' << group;
        }
    }
    // An even count whose middle pair straddles two runs: the lower and upper values differ.
    for (std::size_t i = 0u; i < planes.size(); ++i)
        planes[i] = static_cast<SampleT>(i < planes.size() / 2u ? -0.125 : 0.25);
    const auto straddled = an::analyzeImageStatisticalStretch(execution, sampleType, planes.data(), *layout, true);
    ASSERT_TRUE(straddled.hasValue()) << straddled.status().message();
    EXPECT_EQ(straddled->channels[0].median, 0.0625);
    EXPECT_EQ(straddled->channels[0].minimum, -0.125);
}

} // namespace

TEST(StatisticalStretchTest, RgbSelectionResolvesRepeatedAndNegativeValuesInBothRepresentations)
{
    checkRepeatedRgb<float>(an::SampleType::Float32);
    checkRepeatedRgb<double>(an::SampleType::Float64);
}
