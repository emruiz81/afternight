// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for global normalization: measurement, matching, overlap solving and application.

#include <AfterNightEngine/Algorithms/Normalization/GlobalNormalization.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Image/OperationTransition.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stop_token>
#include <utility>
#include <vector>

namespace
{

[[nodiscard]] an::EngineContext makeEngine(std::uint32_t concurrency = 4u)
{
    an::EngineContextOptions options;
    options.maxConcurrency = concurrency;
    an::Result<an::EngineContext> made = an::EngineContext::create(options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::EngineContext{};
}

[[nodiscard]] an::ExecutionContext makeExecution(const an::EngineContext& engine,
                                                 an::ComputeBackend backend = an::ComputeBackend::Automatic,
                                                 std::stop_token stopToken = {}, std::uint32_t concurrency = 3u)
{
    an::ExecutionOptions options;
    options.maxConcurrency = concurrency;
    options.backend = backend;
    options.stopToken = std::move(stopToken);
    an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
}

[[nodiscard]] an::ImageDescriptor descriptorOf(an::SampleType sampleType, std::uint32_t width, std::uint32_t height,
                                               an::ChannelSchema schema = an::ChannelSchema::Mono,
                                               an::NumericDomain domain = an::NumericDomain::NormalizedLinear,
                                               std::uint32_t namedChannels = 0u)
{
    const std::uint32_t channels =
        schema == an::ChannelSchema::Named ? namedChannels : an::channelCountOf(schema).value_or(1u);
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, channels, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    an::ColorSpace colorSpace = an::ColorSpace::Unspecified;
    if (schema == an::ChannelSchema::Rgb || schema == an::ChannelSchema::Rgba)
    {
        colorSpace =
            domain == an::NumericDomain::EncodedNonlinear ? an::ColorSpace::DisplayRgb : an::ColorSpace::LinearRgb;
    }
    const an::ImageDescriptorRequest request{.extent = *extent,
                                             .sampleType = sampleType,
                                             .channelSchema = schema,
                                             .colorSpace = colorSpace,
                                             .numericDomain = domain,
                                             .cfa = {},
                                             .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                             .pixelAspectRatio = 1.0};
    const std::optional<an::ImageDescriptor> descriptor = an::ImageDescriptor::create(request);
    EXPECT_TRUE(descriptor.has_value());
    return *descriptor;
}

[[nodiscard]] an::ImageLayout layoutOf(const an::ImageDescriptor& descriptor,
                                       an::StorageArrangement arrangement = an::StorageArrangement::Planar)
{
    const std::optional<an::ImageLayoutRequest> request = an::layoutRequestFor(descriptor, arrangement, 1u);
    EXPECT_TRUE(request.has_value());
    const std::optional<an::ImageLayout> layout = an::ImageLayout::create(*request, an::defaultEngineLimits());
    EXPECT_TRUE(layout.has_value());
    return *layout;
}

[[nodiscard]] an::ValidityDescriptor ruleOf(an::ValidityRule rule)
{
    const std::optional<an::ValidityDescriptor> validity =
        an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = rule});
    EXPECT_TRUE(validity.has_value());
    return *validity;
}

[[nodiscard]] an::NormalizationFrameView frameOf(const void* samples, const an::ImageLayout& layout,
                                                 const an::ImageDescriptor& descriptor,
                                                 an::ValidityRule rule = an::ValidityRule::AllValid)
{
    const an::KernelValidityView validity{.descriptor = ruleOf(rule)};
    const an::Result<an::NormalizationFrameView> made =
        an::NormalizationFrameView::create(samples, layout.totalBytes(), layout, descriptor, validity);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? *made : an::NormalizationFrameView{};
}

/// A test-side pseudo-random sample, so a fixture is deterministic and not a constant.
[[nodiscard]] float patterned(std::size_t index, std::uint32_t salt) noexcept
{
    const std::uint64_t mixed = (static_cast<std::uint64_t>(index) + 1u) * 2654435761ull + salt * 40503ull;
    return 0.2f + static_cast<float>((mixed >> 7u) % 4001u) / 10000.0f;
}

/// The stated estimator rule, spelled the obvious slow way over a copy.
struct OracleStatistics
{
    double median = 0.0;
    double mad = 0.0;
    double location = 0.0;
    double scale = 0.0;
    std::size_t clipped = 0u;
};

[[nodiscard]] double medianOf(std::vector<double> values)
{
    std::sort(values.begin(), values.end());
    const std::size_t count = values.size();
    if (count == 0u)
    {
        return 0.0;
    }
    if (count % 2u == 1u)
    {
        return values[count / 2u];
    }
    return values[count / 2u - 1u] + 0.5 * (values[count / 2u] - values[count / 2u - 1u]);
}

[[nodiscard]] OracleStatistics oracleOf(const std::vector<double>& admitted, bool iterativeKSigma)
{
    OracleStatistics oracle;
    oracle.median = medianOf(admitted);
    std::vector<double> deviations;
    for (double value : admitted)
    {
        deviations.push_back(std::abs(value - oracle.median));
    }
    oracle.mad = medianOf(deviations);
    if (!iterativeKSigma)
    {
        oracle.location = oracle.median;
        oracle.scale = oracle.mad;
        return oracle;
    }
    if (oracle.mad <= 0.0)
    {
        oracle.location = oracle.median;
        oracle.scale = 0.0;
        return oracle;
    }
    const double lowest = *std::min_element(admitted.begin(), admitted.end());
    const double highest = *std::max_element(admitted.begin(), admitted.end());
    const double low = (std::max)(lowest, oracle.median - an::kIterativeKSigmaClipWidth * oracle.mad);
    const double high = (std::min)(highest, oracle.median + an::kIterativeKSigmaClipWidth * oracle.mad);
    std::vector<double> clipped;
    for (double value : admitted)
    {
        if (value >= low && value <= high)
        {
            clipped.push_back(value);
        }
    }
    oracle.clipped = clipped.size();
    oracle.location = medianOf(clipped);
    std::vector<double> clippedDeviations;
    for (double value : clipped)
    {
        clippedDeviations.push_back(std::abs(value - oracle.location));
    }
    const double clippedMad = medianOf(clippedDeviations);
    if (clippedMad <= 0.0)
    {
        oracle.scale = 0.0;
        return oracle;
    }
    const double factor = 1.0 / (9.0 * clippedMad);
    double up = 0.0;
    double down = 0.0;
    for (double value : clipped)
    {
        const double distance = value - oracle.location;
        const double normalized = distance * factor;
        const double squared = std::abs(normalized) < 1.0 ? normalized * normalized : 1.0;
        const double retained = (1.0 - squared) * (1.0 - squared);
        const double weighted = distance * retained;
        up += weighted * weighted;
        down += (1.0 - squared) * (1.0 - 5.0 * squared);
    }
    const double variance = down != 0.0 ? static_cast<double>(clipped.size()) * up / (down * down) : 0.0;
    oracle.scale = std::sqrt(variance) * an::kIterativeKSigmaScaleCorrection;
    return oracle;
}

[[nodiscard]] an::NormalizationChannelStatistics channelStatistics(double median, double mad, double location,
                                                                   double scale)
{
    return an::NormalizationChannelStatistics{.selection = an::ValidityCount{.valid = 10u, .total = 12u},
                                              .median = median,
                                              .medianAbsoluteDeviation = mad,
                                              .location = location,
                                              .scale = scale};
}

[[nodiscard]] an::NormalizationFrameStatistics frameStatistics(an::ScaleEstimator estimator,
                                                               const an::NormalizationChannelStatistics& channel)
{
    an::NormalizationFrameStatistics statistics;
    statistics.estimator = estimator;
    statistics.channelCount = 1u;
    statistics.channels[0] = channel;
    return statistics;
}

[[nodiscard]] an::NormalizationReference referenceOf(const an::NormalizationFrameStatistics& member)
{
    const an::Result<an::NormalizationReference> combined =
        an::combineNormalizationReferences(std::span<const an::NormalizationFrameStatistics>{&member, 1u});
    EXPECT_TRUE(combined.hasValue()) << combined.status().message();
    return combined.hasValue() ? *combined : an::NormalizationReference{};
}

TEST(GlobalNormalizationTest, AFrameViewKeepsWhatDescribesTheFrameRatherThanPointingAtIt)
{
    const std::array<float, 16> samples{};
    an::ImageDescriptor descriptor = descriptorOf(an::SampleType::Float32, 4u, 4u);
    an::ImageLayout layout = layoutOf(descriptor);
    const an::NormalizationFrameView view = frameOf(samples.data(), layout, descriptor);

    // The samples are borrowed and the description is copied, so the only lifetime a caller keeps
    // is the buffer's. Both of these used to be what the view read through.
    descriptor = descriptorOf(an::SampleType::Float64, 2u, 8u);
    layout = layoutOf(descriptor);

    EXPECT_EQ(view.descriptor().sampleType(), an::SampleType::Float32);
    EXPECT_EQ(view.layout().extent().width(), 4u);
    EXPECT_EQ(view.layout().extent().height(), 4u);
    EXPECT_EQ(view.samples(), samples.data());
}

TEST(GlobalNormalizationTest, FrameViewRefusesWhatNormalizationCannotMatch)
{
    const an::ImageDescriptor integerDescriptor = descriptorOf(an::SampleType::UInt16, 4u, 4u);
    const an::ImageLayout integerLayout = layoutOf(integerDescriptor);
    const std::array<std::uint16_t, 16> integers{};
    EXPECT_EQ(an::NormalizationFrameView::create(integers.data(), sizeof(integers), integerLayout, integerDescriptor)
                  .status()
                  .code(),
              an::ErrorCode::Unsupported);

    const std::array<float, 80> floats{};
    const an::ImageDescriptor encoded =
        descriptorOf(an::SampleType::Float32, 4u, 4u, an::ChannelSchema::Rgb, an::NumericDomain::EncodedNonlinear);
    const an::ImageLayout encodedLayout = layoutOf(encoded);
    EXPECT_EQ(an::NormalizationFrameView::create(floats.data(), sizeof(floats), encodedLayout, encoded).status().code(),
              an::ErrorCode::Unsupported);

    const an::ImageDescriptor wide = descriptorOf(an::SampleType::Float32, 4u, 4u, an::ChannelSchema::Named,
                                                  an::NumericDomain::NormalizedLinear, 5u);
    const an::ImageLayout wideLayout = layoutOf(wide);
    EXPECT_EQ(an::NormalizationFrameView::create(floats.data(), sizeof(floats), wideLayout, wide).status().code(),
              an::ErrorCode::Unsupported);

    const an::ImageDescriptor rgba = descriptorOf(an::SampleType::Float32, 4u, 4u, an::ChannelSchema::Rgba);
    const an::ImageLayout rgbaLayout = layoutOf(rgba);
    EXPECT_TRUE(an::NormalizationFrameView::create(floats.data(), sizeof(floats), rgbaLayout, rgba).hasValue());

    const an::ImageDescriptor mono = descriptorOf(an::SampleType::Float32, 4u, 4u);
    const an::ImageLayout monoLayout = layoutOf(mono);
    EXPECT_EQ(an::NormalizationFrameView::create(floats.data(), monoLayout.totalBytes() - 1u, monoLayout, mono)
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
    an::KernelValidityView shifted{.descriptor = ruleOf(an::ValidityRule::AllValid)};
    shifted.maskPlane = 1u;
    EXPECT_EQ(
        an::NormalizationFrameView::create(floats.data(), sizeof(floats), monoLayout, mono, shifted).status().code(),
        an::ErrorCode::InvalidArgument);
    an::KernelValidityView maskless{.descriptor = ruleOf(an::ValidityRule::ExplicitMask)};
    EXPECT_EQ(
        an::NormalizationFrameView::create(floats.data(), sizeof(floats), monoLayout, mono, maskless).status().code(),
        an::ErrorCode::InvalidArgument);
}

TEST(GlobalNormalizationTest, MeasurementExcludesTheStatedNoDataAndReportsBothEstimators)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageDescriptor descriptor = descriptorOf(an::SampleType::Float32, 6u, 4u);
    const an::ImageLayout layout = layoutOf(descriptor);
    std::vector<float> frame(24u, 0.0f);
    std::vector<double> admitted;
    for (std::uint32_t y = 1u; y < 3u; ++y)
    {
        for (std::uint32_t x = 1u; x < 5u; ++x)
        {
            const float value = patterned(static_cast<std::size_t>(y) * 6u + x, 3u);
            frame[static_cast<std::size_t>(y) * 6u + x] = value;
            admitted.push_back(static_cast<double>(value));
        }
    }
    const an::NormalizationFrameView covered =
        frameOf(frame.data(), layout, descriptor, an::ValidityRule::ZeroIsNoData);

    const an::Result<an::NormalizationFrameStatistics> deviation =
        an::measureGlobalNormalization(execution, covered, an::ScaleEstimator::MedianAbsoluteDeviation);
    ASSERT_TRUE(deviation.hasValue()) << deviation.status().message();
    const OracleStatistics deviationOracle = oracleOf(admitted, false);
    EXPECT_EQ(deviation->channelCount, 1u);
    EXPECT_EQ(deviation->estimator, an::ScaleEstimator::MedianAbsoluteDeviation);
    EXPECT_EQ(deviation->channels[0].selection.valid, 8u);
    EXPECT_EQ(deviation->channels[0].selection.total, 24u);
    EXPECT_DOUBLE_EQ(deviation->channels[0].median, deviationOracle.median);
    EXPECT_DOUBLE_EQ(deviation->channels[0].medianAbsoluteDeviation, deviationOracle.mad);
    EXPECT_DOUBLE_EQ(deviation->channels[0].location, deviationOracle.median);
    EXPECT_DOUBLE_EQ(deviation->channels[0].scale, deviationOracle.mad);
    EXPECT_EQ(deviation->channels[0].clippedSelection.valid, 0u);
    EXPECT_GE(deviation->planeScans, 1u);
    EXPECT_TRUE(deviation->compacted);
    EXPECT_GE(deviation->peakScratchBytes, 24u * sizeof(float));

    const an::Result<an::NormalizationFrameStatistics> clipped =
        an::measureGlobalNormalization(execution, covered, an::ScaleEstimator::IterativeKSigma);
    ASSERT_TRUE(clipped.hasValue()) << clipped.status().message();
    const OracleStatistics clippedOracle = oracleOf(admitted, true);
    EXPECT_DOUBLE_EQ(clipped->channels[0].median, clippedOracle.median);
    EXPECT_DOUBLE_EQ(clipped->channels[0].location, clippedOracle.location);
    EXPECT_NEAR(clipped->channels[0].scale, clippedOracle.scale, 1e-12 * (1.0 + clippedOracle.scale));
    EXPECT_GT(clipped->channels[0].scale, 0.0);
    EXPECT_EQ(clipped->channels[0].clippedSelection.valid, clippedOracle.clipped);
    EXPECT_EQ(clipped->channels[0].clippedSelection.total, 8u);

    const an::NormalizationFrameView everything = frameOf(frame.data(), layout, descriptor);
    const an::Result<an::NormalizationFrameStatistics> counted =
        an::measureGlobalNormalization(execution, everything, an::ScaleEstimator::MedianAbsoluteDeviation);
    ASSERT_TRUE(counted.hasValue()) << counted.status().message();
    EXPECT_EQ(counted->channels[0].selection.valid, 24u);
    EXPECT_DOUBLE_EQ(counted->channels[0].median, 0.0);

    const an::Result<an::NormalizationFrameStatistics> partial =
        an::measureGlobalNormalization(execution, covered, an::ScaleEstimator::MedianAbsoluteDeviation,
                                       an::StatisticsRegion{.originX = 1u, .originY = 1u, .width = 2u, .height = 1u});
    ASSERT_TRUE(partial.hasValue()) << partial.status().message();
    EXPECT_EQ(partial->channels[0].selection.valid, 2u);
    EXPECT_DOUBLE_EQ(partial->channels[0].median, 0.5 * (admitted[0] + admitted[1]));
    EXPECT_EQ(
        an::measureGlobalNormalization(execution, covered, an::ScaleEstimator::MedianAbsoluteDeviation,
                                       an::StatisticsRegion{.originX = 5u, .originY = 0u, .width = 2u, .height = 1u})
            .status()
            .code(),
        an::ErrorCode::InvalidArgument);

    std::vector<float> blank(24u, 0.0f);
    const an::NormalizationFrameView uncovered =
        frameOf(blank.data(), layout, descriptor, an::ValidityRule::ZeroIsNoData);
    EXPECT_EQ(an::measureGlobalNormalization(execution, uncovered, an::ScaleEstimator::IterativeKSigma).status().code(),
              an::ErrorCode::EmptySelection);

    std::vector<float> constant(24u, 0.25f);
    const an::NormalizationFrameView flat = frameOf(constant.data(), layout, descriptor);
    const an::Result<an::NormalizationFrameStatistics> flatStatistics =
        an::measureGlobalNormalization(execution, flat, an::ScaleEstimator::IterativeKSigma);
    ASSERT_TRUE(flatStatistics.hasValue()) << flatStatistics.status().message();
    EXPECT_DOUBLE_EQ(flatStatistics->channels[0].location, 0.25);
    EXPECT_DOUBLE_EQ(flatStatistics->channels[0].scale, 0.0);

    std::stop_source stopped;
    stopped.request_stop();
    const an::ExecutionContext cancelled = makeExecution(engine, an::ComputeBackend::Automatic, stopped.get_token());
    EXPECT_EQ(an::measureGlobalNormalization(cancelled, covered, an::ScaleEstimator::IterativeKSigma).status().code(),
              an::ErrorCode::Cancelled);
}

TEST(GlobalNormalizationTest, MeasurementMatchesTheOracleAndIsThreadCountIndependent)
{
    const an::EngineContext engine = makeEngine(8u);
    const an::ImageDescriptor descriptor = descriptorOf(an::SampleType::Float32, 300u, 64u, an::ChannelSchema::Rgb);
    const an::ImageLayout layout = layoutOf(descriptor, an::StorageArrangement::Interleaved);
    std::vector<float> frame(static_cast<std::size_t>(300u) * 64u * 3u, 0.0f);
    std::array<std::vector<double>, 3> admitted;
    for (std::uint32_t y = 0u; y < 64u; ++y)
    {
        for (std::uint32_t x = 0u; x < 300u; ++x)
        {
            for (std::uint32_t channel = 0u; channel < 3u; ++channel)
            {
                const std::size_t index = (static_cast<std::size_t>(y) * 300u + x) * 3u + channel;
                const bool border = x < 20u || x >= 280u || y < 4u || y >= 60u;
                float value = border ? 0.0f : patterned(index, channel + 11u);
                if (!border && index % 97u == 0u)
                {
                    value = 0.9f + patterned(index, 5u);
                }
                frame[index] = value;
                if (value != 0.0f)
                {
                    admitted[channel].push_back(static_cast<double>(value));
                }
            }
        }
    }
    const an::NormalizationFrameView view = frameOf(frame.data(), layout, descriptor, an::ValidityRule::ZeroIsNoData);

    const an::ExecutionContext narrow = makeExecution(engine, an::ComputeBackend::Automatic, {}, 1u);
    const an::ExecutionContext wide = makeExecution(engine, an::ComputeBackend::Automatic, {}, 8u);
    const an::Result<an::NormalizationFrameStatistics> serial =
        an::measureGlobalNormalization(narrow, view, an::ScaleEstimator::IterativeKSigma);
    const an::Result<an::NormalizationFrameStatistics> parallel =
        an::measureGlobalNormalization(wide, view, an::ScaleEstimator::IterativeKSigma);
    ASSERT_TRUE(serial.hasValue()) << serial.status().message();
    ASSERT_TRUE(parallel.hasValue()) << parallel.status().message();
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        const OracleStatistics oracle = oracleOf(admitted[channel], true);
        EXPECT_EQ(serial->channels[channel].selection.valid, admitted[channel].size());
        EXPECT_DOUBLE_EQ(serial->channels[channel].median, oracle.median);
        EXPECT_DOUBLE_EQ(serial->channels[channel].medianAbsoluteDeviation, oracle.mad);
        EXPECT_DOUBLE_EQ(serial->channels[channel].location, oracle.location);
        EXPECT_NEAR(serial->channels[channel].scale, oracle.scale, 1e-9 * oracle.scale);
        EXPECT_EQ(serial->channels[channel].location, parallel->channels[channel].location);
        EXPECT_EQ(serial->channels[channel].scale, parallel->channels[channel].scale);
        EXPECT_EQ(serial->channels[channel].clippedSelection.valid, parallel->channels[channel].clippedSelection.valid);
    }
}

/// A test-side uniform sample in [0, 1) with twenty-four random bits, so a megapixel frame holds
/// nearly as many distinct values as samples; a salt draws another independent sequence.
[[nodiscard]] float uniformSample(std::size_t index, std::uint64_t salt = 0u) noexcept
{
    std::uint64_t mixed = (static_cast<std::uint64_t>(index) + 0x9E3779B97F4A7C15ull + salt * 0x632BE59BD9B4E019ull) *
                          0xBF58476D1CE4E5B9ull;
    mixed ^= mixed >> 31u;
    mixed *= 0x94D049BB133111EBull;
    mixed ^= mixed >> 29u;
    return static_cast<float>((mixed >> 40u) & 0xFFFFFFu) / 16777216.0f;
}

/// Measures a 1024 x 1024 frame at one, three, eight and sixteen workers, holding every width to
/// the oracle and to the one-worker result bit for bit.
void expectMegapixelMeasurementMatchesTheOracle(const std::vector<float>& frame)
{
    constexpr std::uint32_t kSide = 1024u;
    ASSERT_EQ(frame.size(), static_cast<std::size_t>(kSide) * kSide);
    const an::EngineContext engine = makeEngine(16u);
    const an::ImageDescriptor descriptor = descriptorOf(an::SampleType::Float32, kSide, kSide);
    const an::ImageLayout layout = layoutOf(descriptor);
    const an::NormalizationFrameView view = frameOf(frame.data(), layout, descriptor, an::ValidityRule::ZeroIsNoData);
    std::vector<double> admitted;
    admitted.reserve(frame.size());
    for (const float value : frame)
    {
        if (value != 0.0f)
        {
            admitted.push_back(static_cast<double>(value));
        }
    }
    const OracleStatistics oracle = oracleOf(admitted, true);
    std::optional<an::NormalizationFrameStatistics> first;
    for (const std::uint32_t workers : {1u, 3u, 8u, 16u})
    {
        const an::ExecutionContext execution = makeExecution(engine, an::ComputeBackend::Automatic, {}, workers);
        const an::Result<an::NormalizationFrameStatistics> measured =
            an::measureGlobalNormalization(execution, view, an::ScaleEstimator::IterativeKSigma);
        ASSERT_TRUE(measured.hasValue()) << measured.status().message();
        const an::NormalizationChannelStatistics& channel = measured->channels[0];
        EXPECT_TRUE(measured->compacted) << workers;
        EXPECT_EQ(channel.selection.valid, admitted.size()) << workers;
        EXPECT_EQ(channel.median, oracle.median) << workers;
        EXPECT_EQ(channel.medianAbsoluteDeviation, oracle.mad) << workers;
        EXPECT_EQ(channel.location, oracle.location) << workers;
        EXPECT_EQ(channel.clippedSelection.valid, oracle.clipped) << workers;
        EXPECT_NEAR(channel.scale, oracle.scale, 1e-12 * oracle.scale) << workers;
        if (!first.has_value())
        {
            first = *measured;
            continue;
        }
        const an::NormalizationChannelStatistics& serial = first->channels[0];
        EXPECT_EQ(channel.median, serial.median) << workers;
        EXPECT_EQ(channel.medianAbsoluteDeviation, serial.medianAbsoluteDeviation) << workers;
        EXPECT_EQ(channel.location, serial.location) << workers;
        EXPECT_EQ(channel.scale, serial.scale) << workers;
        EXPECT_EQ(measured->planeScans, first->planeScans) << workers;
    }
}

TEST(GlobalNormalizationTest, AMegapixelMeasurementMatchesTheOracleAtEveryWidth)
{
    // Nearly every sample distinct: the deviation descents hold more candidates in their first bin
    // than one gather takes, so they count a second digit before gathering.
    std::vector<float> uniform(1024u * 1024u);
    for (std::size_t index = 0u; index < uniform.size(); ++index)
    {
        uniform[index] = 0.3f + 0.3f * uniformSample(index);
    }
    expectMegapixelMeasurementMatchesTheOracle(uniform);
}

TEST(GlobalNormalizationTest, AMeasurementWhoseMiddleIsOneRepeatedValueResolvesEveryDigit)
{
    // A third of the samples are the median exactly: its bin never shrinks below what a gather
    // takes, so the descent resolves the key digit by digit.
    std::vector<float> repeated(1024u * 1024u);
    for (std::size_t index = 0u; index < repeated.size(); ++index)
    {
        repeated[index] = index % 3u == 0u ? 0.45f : 0.3f + 0.3f * uniformSample(index);
    }
    expectMegapixelMeasurementMatchesTheOracle(repeated);
}

TEST(GlobalNormalizationTest, BothMiddleKeysOfOneGatherAreSelectedAndNotAssumedAdjacent)
{
    // Every sample of a 128 x 100 frame lies in one sixteen-bit bin of [0.25, 0.5), which one gather
    // takes whole, so the two middle keys of the even count come from the same candidates. On one
    // worker the candidates arrive in row-major order. A selection places only the key it was asked
    // for, and across two hundred orders some leave the next position holding another key.
    constexpr std::uint32_t kWidth = 128u;
    constexpr std::uint32_t kHeight = 100u;
    const an::EngineContext engine = makeEngine(1u);
    const an::ExecutionContext execution = makeExecution(engine, an::ComputeBackend::Automatic, {}, 1u);
    const an::ImageDescriptor descriptor = descriptorOf(an::SampleType::Float32, kWidth, kHeight);
    const an::ImageLayout layout = layoutOf(descriptor);
    std::vector<float> frame(static_cast<std::size_t>(kWidth) * kHeight);
    std::vector<double> admitted(frame.size());
    for (std::uint64_t salt = 1u; salt <= 200u; ++salt)
    {
        for (std::size_t index = 0u; index < frame.size(); ++index)
        {
            frame[index] = 0.3008f + 0.0019f * uniformSample(index, salt);
            admitted[index] = static_cast<double>(frame[index]);
        }
        const an::Result<an::NormalizationFrameStatistics> measured = an::measureGlobalNormalization(
            execution, frameOf(frame.data(), layout, descriptor), an::ScaleEstimator::MedianAbsoluteDeviation);
        ASSERT_TRUE(measured.hasValue()) << measured.status().message();
        const OracleStatistics oracle = oracleOf(admitted, false);
        EXPECT_EQ(measured->channels[0].median, oracle.median) << salt;
        EXPECT_EQ(measured->channels[0].medianAbsoluteDeviation, oracle.mad) << salt;
    }
}

TEST(GlobalNormalizationTest, AMeasurementWhoseMiddleRanksLieApartSettlesEachOnItsOwn)
{
    // Two clusters with nothing between them and an even count: the two middle ranks fall in
    // different bins and are descended separately.
    std::vector<float> clusters(1024u * 1024u);
    for (std::size_t index = 0u; index < clusters.size(); ++index)
    {
        clusters[index] = (index % 2u == 0u ? 0.2f : 0.7f) + 0.1f * uniformSample(index);
    }
    expectMegapixelMeasurementMatchesTheOracle(clusters);
}

TEST(GlobalNormalizationTest, CombinedReferencesTakeThePerChannelMedianOfMembers)
{
    const std::array<an::NormalizationFrameStatistics, 3> members{
        frameStatistics(an::ScaleEstimator::IterativeKSigma, channelStatistics(0.5, 0.10, 0.52, 0.12)),
        frameStatistics(an::ScaleEstimator::IterativeKSigma, channelStatistics(0.7, 0.05, 0.71, 0.06)),
        frameStatistics(an::ScaleEstimator::IterativeKSigma, channelStatistics(0.4, 0.30, 0.44, 0.33))};
    const an::Result<an::NormalizationReference> odd = an::combineNormalizationReferences(members);
    ASSERT_TRUE(odd.hasValue()) << odd.status().message();
    EXPECT_EQ(odd->memberCount, 3u);
    EXPECT_EQ(odd->channelCount, 1u);
    EXPECT_DOUBLE_EQ(odd->channels[0].median, 0.5);
    EXPECT_DOUBLE_EQ(odd->channels[0].medianAbsoluteDeviation, 0.10);
    EXPECT_DOUBLE_EQ(odd->channels[0].location, 0.52);
    EXPECT_DOUBLE_EQ(odd->channels[0].scale, 0.12);
    EXPECT_EQ(odd->channels[0].selection.valid, 30u);

    const an::Result<an::NormalizationReference> even =
        an::combineNormalizationReferences(std::span<const an::NormalizationFrameStatistics>{members.data(), 2u});
    ASSERT_TRUE(even.hasValue()) << even.status().message();
    EXPECT_DOUBLE_EQ(even->channels[0].median, 0.6);
    EXPECT_DOUBLE_EQ(even->channels[0].scale, 0.09);

    std::array<an::NormalizationFrameStatistics, 2> mixed{members[0], members[1]};
    mixed[1].estimator = an::ScaleEstimator::MedianAbsoluteDeviation;
    EXPECT_EQ(an::combineNormalizationReferences(mixed).status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(an::combineNormalizationReferences({}).status().code(), an::ErrorCode::InvalidArgument);
}

TEST(GlobalNormalizationTest, EstimationFollowsTheStatedFormulasForEveryMethod)
{
    const an::NormalizationFrameStatistics referenceFrame =
        frameStatistics(an::ScaleEstimator::IterativeKSigma, channelStatistics(0.5, 0.10, 0.52, 0.12));
    const an::NormalizationFrameStatistics frame =
        frameStatistics(an::ScaleEstimator::IterativeKSigma, channelStatistics(0.4, 0.05, 0.41, 0.06));
    const an::NormalizationReference reference = referenceOf(referenceFrame);

    const an::Result<an::FrameNormalization> additive =
        an::estimateGlobalNormalization(reference, frame, an::NormalizationMethod::Additive);
    ASSERT_TRUE(additive.hasValue()) << additive.status().message();
    EXPECT_DOUBLE_EQ(additive->channels[0].scale, 1.0);
    EXPECT_DOUBLE_EQ(additive->channels[0].offset, 0.5 - 0.4);
    EXPECT_EQ(additive->estimator, an::ScaleEstimator::IterativeKSigma);

    const an::Result<an::FrameNormalization> multiplicative =
        an::estimateGlobalNormalization(reference, frame, an::NormalizationMethod::Multiplicative);
    ASSERT_TRUE(multiplicative.hasValue()) << multiplicative.status().message();
    EXPECT_DOUBLE_EQ(multiplicative->channels[0].scale, 0.5 / 0.4);
    EXPECT_DOUBLE_EQ(multiplicative->channels[0].offset, 0.0);

    const an::Result<an::FrameNormalization> additiveScaling =
        an::estimateGlobalNormalization(reference, frame, an::NormalizationMethod::AdditiveScaling);
    ASSERT_TRUE(additiveScaling.hasValue()) << additiveScaling.status().message();
    EXPECT_DOUBLE_EQ(additiveScaling->channels[0].scale, 0.12 / 0.06);
    EXPECT_DOUBLE_EQ(additiveScaling->channels[0].offset, 0.52 - (0.12 / 0.06) * 0.41);

    const an::Result<an::FrameNormalization> multiplicativeScaling =
        an::estimateGlobalNormalization(reference, frame, an::NormalizationMethod::MultiplicativeScaling);
    ASSERT_TRUE(multiplicativeScaling.hasValue()) << multiplicativeScaling.status().message();
    EXPECT_DOUBLE_EQ(multiplicativeScaling->channels[0].scale, (0.12 / 0.06) * (0.52 / 0.41));
    EXPECT_DOUBLE_EQ(multiplicativeScaling->channels[0].offset, 0.0);

    const an::NormalizationFrameStatistics deviationFrame =
        frameStatistics(an::ScaleEstimator::MedianAbsoluteDeviation, channelStatistics(0.4, 0.05, 0.4, 0.05));
    EXPECT_EQ(
        an::estimateGlobalNormalization(reference, deviationFrame, an::NormalizationMethod::Additive).status().code(),
        an::ErrorCode::IncompatibleArguments);

    const an::NormalizationFrameStatistics flat =
        frameStatistics(an::ScaleEstimator::IterativeKSigma, channelStatistics(0.4, 0.0, 0.4, 0.0));
    const an::Result<an::FrameNormalization> fallback =
        an::estimateGlobalNormalization(reference, flat, an::NormalizationMethod::AdditiveScaling);
    ASSERT_TRUE(fallback.hasValue()) << fallback.status().message();
    EXPECT_DOUBLE_EQ(fallback->channels[0].scale, 1.0);
    EXPECT_DOUBLE_EQ(fallback->channels[0].offset, 0.52 - 0.4);
    EXPECT_EQ(fallback->dispersionFallbacks, 1u);

    const an::NormalizationFrameStatistics dark =
        frameStatistics(an::ScaleEstimator::IterativeKSigma, channelStatistics(0.0, 0.05, 0.0, 0.06));
    const an::Result<an::FrameNormalization> darkMultiplicative =
        an::estimateGlobalNormalization(reference, dark, an::NormalizationMethod::MultiplicativeScaling);
    ASSERT_TRUE(darkMultiplicative.hasValue()) << darkMultiplicative.status().message();
    EXPECT_DOUBLE_EQ(darkMultiplicative->channels[0].scale, 0.12 / 0.06);
    EXPECT_EQ(darkMultiplicative->locationFallbacks, 1u);

    const an::NormalizationReference darkReference = referenceOf(dark);
    EXPECT_EQ(
        an::estimateGlobalNormalization(darkReference, frame, an::NormalizationMethod::Multiplicative).status().code(),
        an::ErrorCode::InvalidData);

    const an::NormalizationReference self = referenceOf(frame);
    for (const an::NormalizationMethod method :
         {an::NormalizationMethod::Additive, an::NormalizationMethod::Multiplicative,
          an::NormalizationMethod::AdditiveScaling, an::NormalizationMethod::MultiplicativeScaling})
    {
        const an::Result<an::FrameNormalization> identity = an::estimateGlobalNormalization(self, frame, method);
        ASSERT_TRUE(identity.hasValue()) << identity.status().message();
        EXPECT_EQ(identity->channels[0].scale, 1.0) << an::normalizationMethodName(method);
        EXPECT_EQ(identity->channels[0].offset, 0.0) << an::normalizationMethodName(method);
        const an::Result<an::FrameNormalization> stated = an::identityNormalization(method, 1u);
        ASSERT_TRUE(stated.hasValue());
        EXPECT_EQ(stated->channels[0].scale, 1.0);
        EXPECT_EQ(stated->channels[0].offset, 0.0);
    }
    EXPECT_EQ(an::identityNormalization(an::NormalizationMethod::Additive, an::kMaxNormalizationChannels + 1u)
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
}

TEST(GlobalNormalizationTest, ApplyTransformsAdmittedSamplesAndCopiesTheRest)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageDescriptor descriptor = descriptorOf(an::SampleType::Float32, 4u, 1u);
    const an::ImageLayout layout = layoutOf(descriptor);
    const std::array<float, 4> source{0.0f, 0.25f, 0.5f, -0.125f};
    std::array<float, 4> output{};
    an::FrameNormalization match;
    match.method = an::NormalizationMethod::AdditiveScaling;
    match.channelCount = 1u;
    match.channels[0] = an::NormalizationChannelCoefficients{.scale = 2.0, .offset = -0.05};

    const an::NormalizationFrameView canvas =
        frameOf(source.data(), layout, descriptor, an::ValidityRule::ZeroIsNoData);
    const an::Result<an::NormalizationApplyResult> applied =
        an::applyGlobalNormalization(execution, match, canvas, output.data(), layout.totalBytes(), layout);
    ASSERT_TRUE(applied.hasValue()) << applied.status().message();
    EXPECT_EQ(output[0], 0.0f);
    EXPECT_EQ(output[1], static_cast<float>(static_cast<double>(0.25f) * 2.0 + -0.05));
    EXPECT_EQ(output[2], static_cast<float>(static_cast<double>(0.5f) * 2.0 + -0.05));
    EXPECT_EQ(output[3], static_cast<float>(static_cast<double>(-0.125f) * 2.0 + -0.05));
    EXPECT_EQ(applied->samplesWritten, 4u);
    EXPECT_EQ(applied->samplesTransformed, 3u);
    EXPECT_EQ(applied->samplesPreserved, 1u);
    EXPECT_EQ(applied->noDataCollisions, 0u);
    EXPECT_EQ(applied->imagePasses, 1u);
    EXPECT_NE(applied->backend.instructionSet, an::InstructionSet::Automatic);

    match.method = an::NormalizationMethod::Multiplicative;
    match.channels[0] = an::NormalizationChannelCoefficients{.scale = 1.5, .offset = 0.0};
    ASSERT_TRUE(
        an::applyGlobalNormalization(execution, match, canvas, output.data(), layout.totalBytes(), layout).hasValue());
    EXPECT_EQ(output[1], 0.375f);

    const std::array<float, 4> holed{0.5f, std::numeric_limits<float>::quiet_NaN(), 0.25f, 1.0f};
    const an::NormalizationFrameView finite = frameOf(holed.data(), layout, descriptor, an::ValidityRule::FiniteOnly);
    match.method = an::NormalizationMethod::Additive;
    match.channels[0] = an::NormalizationChannelCoefficients{.scale = 1.0, .offset = 0.125};
    const an::Result<an::NormalizationApplyResult> finiteApplied =
        an::applyGlobalNormalization(execution, match, finite, output.data(), layout.totalBytes(), layout);
    ASSERT_TRUE(finiteApplied.hasValue()) << finiteApplied.status().message();
    EXPECT_TRUE(std::isnan(output[1]));
    EXPECT_EQ(output[0], 0.625f);
    EXPECT_EQ(finiteApplied->samplesPreserved, 1u);

    std::array<std::uint8_t, 1> maskBytes{};
    const std::optional<an::ValidityMaskView<std::uint8_t>> writableMask =
        an::ValidityMaskView<std::uint8_t>::createTight(maskBytes, descriptor.extent());
    ASSERT_TRUE(writableMask.has_value());
    writableMask->fill(true);
    ASSERT_TRUE(writableMask->setValid(0u, 2u, 0u, false));
    const an::ValidityMaskView<const std::uint8_t> mask = writableMask->constView();
    const an::KernelValidityView masked{.descriptor = ruleOf(an::ValidityRule::ExplicitMask), .mask = &mask};
    const an::Result<an::NormalizationFrameView> maskedView =
        an::NormalizationFrameView::create(source.data(), layout.totalBytes(), layout, descriptor, masked);
    ASSERT_TRUE(maskedView.hasValue()) << maskedView.status().message();
    const an::Result<an::NormalizationApplyResult> maskedApplied =
        an::applyGlobalNormalization(execution, match, *maskedView, output.data(), layout.totalBytes(), layout);
    ASSERT_TRUE(maskedApplied.hasValue()) << maskedApplied.status().message();
    EXPECT_EQ(output[0], 0.125f);
    EXPECT_EQ(output[2], 0.5f);
    EXPECT_EQ(maskedApplied->samplesPreserved, 1u);
    EXPECT_EQ(maskedApplied->path, an::NormalizationExecutionPath::Reference);

    match.channels[0] = an::NormalizationChannelCoefficients{.scale = 1.0, .offset = -0.25};
    const an::Result<an::NormalizationApplyResult> collided =
        an::applyGlobalNormalization(execution, match, canvas, output.data(), layout.totalBytes(), layout);
    ASSERT_TRUE(collided.hasValue()) << collided.status().message();
    EXPECT_EQ(output[1], (std::numeric_limits<float>::min)());
    EXPECT_EQ(collided->noDataCollisions, 1u);
    const an::NormalizationFrameView plain = frameOf(source.data(), layout, descriptor);
    const an::Result<an::NormalizationApplyResult> uncollided =
        an::applyGlobalNormalization(execution, match, plain, output.data(), layout.totalBytes(), layout);
    ASSERT_TRUE(uncollided.hasValue()) << uncollided.status().message();
    EXPECT_EQ(output[1], 0.0f);
    EXPECT_EQ(uncollided->noDataCollisions, 0u);

    std::array<float, 4> inPlace = source;
    const an::NormalizationFrameView resident =
        frameOf(inPlace.data(), layout, descriptor, an::ValidityRule::ZeroIsNoData);
    ASSERT_TRUE(an::applyGlobalNormalization(execution, match, resident, inPlace.data(), layout.totalBytes(), layout)
                    .hasValue());
    EXPECT_EQ(inPlace[0], 0.0f);
    EXPECT_EQ(inPlace[2], 0.25f);
    std::array<float, 8> overlapping{};
    std::copy(source.begin(), source.end(), overlapping.begin());
    const an::NormalizationFrameView front = frameOf(overlapping.data(), layout, descriptor);
    EXPECT_EQ(
        an::applyGlobalNormalization(execution, match, front, overlapping.data() + 1u, layout.totalBytes(), layout)
            .status()
            .code(),
        an::ErrorCode::Aliasing);
    EXPECT_EQ(an::applyGlobalNormalization(execution, match, front, output.data(), layout.totalBytes() - 1u, layout)
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);

    const std::optional<an::ValidityDescriptor> ranged = an::ValidityDescriptor::create(
        an::ValidityDescriptorRequest{.rule = an::ValidityRule::DomainRange, .lowestValid = 0.0, .highestValid = 1.0});
    ASSERT_TRUE(ranged.has_value());
    const an::Result<an::NormalizationFrameView> rangedView = an::NormalizationFrameView::create(
        source.data(), layout.totalBytes(), layout, descriptor, an::KernelValidityView{.descriptor = *ranged});
    ASSERT_TRUE(rangedView.hasValue());
    EXPECT_EQ(an::applyGlobalNormalization(execution, match, *rangedView, output.data(), layout.totalBytes(), layout)
                  .status()
                  .code(),
              an::ErrorCode::Unsupported);

    an::FrameNormalization wide = match;
    wide.channelCount = 3u;
    EXPECT_EQ(an::applyGlobalNormalization(execution, wide, plain, output.data(), layout.totalBytes(), layout)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
    an::FrameNormalization erased = match;
    erased.channels[0].scale = 0.0;
    EXPECT_EQ(an::applyGlobalNormalization(execution, erased, plain, output.data(), layout.totalBytes(), layout)
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
}

TEST(GlobalNormalizationTest, ReferenceAndCompiledRowsStoreTheSameBits)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext referenceExecution = makeExecution(engine, an::ComputeBackend::Reference);
    const an::ExecutionContext compiledExecution = makeExecution(engine);
    const an::ImageDescriptor descriptor = descriptorOf(an::SampleType::Float32, 129u, 5u, an::ChannelSchema::Rgb);
    const an::ImageLayout layout = layoutOf(descriptor, an::StorageArrangement::Interleaved);
    std::vector<float> frame(static_cast<std::size_t>(129u) * 5u * 3u, 0.0f);
    for (std::size_t index = 0u; index < frame.size(); ++index)
    {
        frame[index] = index % 31u == 0u ? 0.0f : patterned(index, 2u) - 0.35f;
        if (index % 101u == 0u)
        {
            frame[index] = std::numeric_limits<float>::quiet_NaN();
        }
    }
    an::FrameNormalization match;
    match.method = an::NormalizationMethod::AdditiveScaling;
    match.channelCount = 3u;
    match.channels[0] = an::NormalizationChannelCoefficients{.scale = 1.03125, .offset = -0.0078125};
    match.channels[1] = an::NormalizationChannelCoefficients{.scale = 0.875, .offset = 0.015625};
    match.channels[2] = an::NormalizationChannelCoefficients{.scale = 1.25, .offset = -0.03125};
    const an::NormalizationFrameView view = frameOf(frame.data(), layout, descriptor, an::ValidityRule::ZeroIsNoData);
    std::vector<float> reference(frame.size(), 0.0f);
    std::vector<float> compiled(frame.size(), 0.0f);
    const an::Result<an::NormalizationApplyResult> referenceRun =
        an::applyGlobalNormalization(referenceExecution, match, view, reference.data(), layout.totalBytes(), layout);
    const an::Result<an::NormalizationApplyResult> compiledRun =
        an::applyGlobalNormalization(compiledExecution, match, view, compiled.data(), layout.totalBytes(), layout);
    ASSERT_TRUE(referenceRun.hasValue()) << referenceRun.status().message();
    ASSERT_TRUE(compiledRun.hasValue()) << compiledRun.status().message();
    for (std::size_t index = 0u; index < frame.size(); ++index)
    {
        if (std::isnan(reference[index]))
        {
            EXPECT_TRUE(std::isnan(compiled[index])) << index;
        }
        else
        {
            EXPECT_EQ(compiled[index], reference[index]) << index;
        }
    }
    EXPECT_EQ(referenceRun->path, an::NormalizationExecutionPath::Reference);
    EXPECT_EQ(referenceRun->samplesTransformed, compiledRun->samplesTransformed);
    EXPECT_EQ(referenceRun->samplesPreserved, compiledRun->samplesPreserved);
    if (compiledRun->backend.instructionSet != an::InstructionSet::Scalar)
    {
        EXPECT_EQ(compiledRun->path, an::NormalizationExecutionPath::CompiledRow);
    }
}

TEST(GlobalNormalizationTest, OverlapMeasurementUsesOnlySamplesValidInBothFrames)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageDescriptor descriptor = descriptorOf(an::SampleType::Float32, 6u, 3u);
    const an::ImageLayout layout = layoutOf(descriptor);
    std::vector<float> a(18u, 0.0f);
    std::vector<float> b(18u, 0.0f);
    for (std::size_t index = 0u; index < 18u; ++index)
    {
        a[index] = patterned(index, 21u);
        b[index] = patterned(index, 22u);
    }
    a[static_cast<std::size_t>(1u) * 6u + 3u] = 0.0f;
    b[static_cast<std::size_t>(0u) * 6u + 1u] = 0.0f;
    const an::NormalizationFrameView viewA = frameOf(a.data(), layout, descriptor, an::ValidityRule::ZeroIsNoData);
    const an::NormalizationFrameView viewB = frameOf(b.data(), layout, descriptor, an::ValidityRule::ZeroIsNoData);
    const an::StatisticsRegion regionA{.originX = 2u, .originY = 0u, .width = 4u, .height = 3u};
    const an::StatisticsRegion regionB{.originX = 0u, .originY = 0u, .width = 4u, .height = 3u};

    const an::Result<an::OverlapMeasurement> measured = an::measureOverlapStatistics(
        execution, 0u, viewA, regionA, 1u, viewB, regionB, an::ScaleEstimator::MedianAbsoluteDeviation);
    ASSERT_TRUE(measured.hasValue()) << measured.status().message();
    std::vector<double> sharedA;
    std::vector<double> sharedB;
    for (std::uint32_t y = 0u; y < 3u; ++y)
    {
        for (std::uint32_t dx = 0u; dx < 4u; ++dx)
        {
            const float valueA = a[static_cast<std::size_t>(y) * 6u + 2u + dx];
            const float valueB = b[static_cast<std::size_t>(y) * 6u + dx];
            if (valueA != 0.0f && valueB != 0.0f)
            {
                sharedA.push_back(static_cast<double>(valueA));
                sharedB.push_back(static_cast<double>(valueB));
            }
        }
    }
    EXPECT_EQ(sharedA.size(), 10u);
    EXPECT_EQ(measured->sharedSamples, 10u);
    EXPECT_EQ(measured->frameA, 0u);
    EXPECT_EQ(measured->frameB, 1u);
    EXPECT_EQ(measured->channelCount, 1u);
    EXPECT_DOUBLE_EQ(measured->a[0].median, oracleOf(sharedA, false).median);
    EXPECT_DOUBLE_EQ(measured->b[0].median, oracleOf(sharedB, false).median);
    EXPECT_DOUBLE_EQ(measured->a[0].scale, oracleOf(sharedA, false).mad);
    EXPECT_EQ(measured->a[0].selection.valid, 10u);
    EXPECT_EQ(measured->b[0].selection.valid, 10u);
    EXPECT_GT(measured->peakScratchBytes, 0u);

    EXPECT_EQ(
        an::measureOverlapStatistics(execution, 0u, viewA, regionA, 1u, viewB,
                                     an::StatisticsRegion{.originX = 0u, .originY = 0u, .width = 3u, .height = 3u},
                                     an::ScaleEstimator::MedianAbsoluteDeviation)
            .status()
            .code(),
        an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(an::measureOverlapStatistics(execution, 1u, viewA, regionA, 1u, viewB, regionB,
                                           an::ScaleEstimator::MedianAbsoluteDeviation)
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
}

[[nodiscard]] an::OverlapMeasurement pairOf(std::uint32_t frameA, double locationA, double scaleA, std::uint32_t frameB,
                                            double locationB, double scaleB, std::uint64_t shared = 100u)
{
    an::OverlapMeasurement pair;
    pair.frameA = frameA;
    pair.frameB = frameB;
    pair.channelCount = 1u;
    pair.estimator = an::ScaleEstimator::MedianAbsoluteDeviation;
    pair.sharedSamples = shared;
    pair.a[0] = channelStatistics(locationA, scaleA, locationA, scaleA);
    pair.b[0] = channelStatistics(locationB, scaleB, locationB, scaleB);
    return pair;
}

TEST(GlobalNormalizationTest, OverlapSolverRecoversOffsetsAndFactorsFromPairs)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::array<double, 3> levels{0.5, 0.4, 0.55};
    const std::array<double, 3> spreads{0.10, 0.20, 0.05};
    std::vector<an::OverlapMeasurement> pairs{pairOf(0u, levels[0], spreads[0], 1u, levels[1], spreads[1]),
                                              pairOf(0u, levels[0], spreads[0], 2u, levels[2], spreads[2]),
                                              pairOf(1u, levels[1], spreads[1], 2u, levels[2], spreads[2], 40u)};
    std::array<an::FrameNormalization, 3> frames{};

    const an::Result<an::OverlapSolveReport> additive = an::solveOverlapNormalization(
        execution, pairs, 0u, an::NormalizationMethod::Additive, an::OverlapDegeneracyPolicy::Refuse, frames);
    ASSERT_TRUE(additive.hasValue()) << additive.status().message();
    EXPECT_EQ(additive->frameCount, 3u);
    EXPECT_EQ(additive->usablePairs, 3u);
    EXPECT_EQ(frames[0].channels[0].scale, 1.0);
    EXPECT_EQ(frames[0].channels[0].offset, 0.0);
    EXPECT_NEAR(frames[1].channels[0].offset, levels[0] - levels[1], 1e-12);
    EXPECT_NEAR(frames[2].channels[0].offset, levels[0] - levels[2], 1e-12);
    EXPECT_EQ(frames[1].method, an::NormalizationMethod::Additive);

    const an::Result<an::OverlapSolveReport> multiplicative = an::solveOverlapNormalization(
        execution, pairs, 0u, an::NormalizationMethod::Multiplicative, an::OverlapDegeneracyPolicy::Refuse, frames);
    ASSERT_TRUE(multiplicative.hasValue()) << multiplicative.status().message();
    EXPECT_NEAR(frames[1].channels[0].scale, levels[0] / levels[1], 1e-12);
    EXPECT_NEAR(frames[2].channels[0].scale, levels[0] / levels[2], 1e-12);
    EXPECT_EQ(frames[1].channels[0].offset, 0.0);

    const an::Result<an::OverlapSolveReport> additiveScaling = an::solveOverlapNormalization(
        execution, pairs, 0u, an::NormalizationMethod::AdditiveScaling, an::OverlapDegeneracyPolicy::Refuse, frames);
    ASSERT_TRUE(additiveScaling.hasValue()) << additiveScaling.status().message();
    EXPECT_EQ(additiveScaling->dispersionSubstitutions, 0u);
    for (const an::OverlapMeasurement& pair : pairs)
    {
        const an::NormalizationChannelCoefficients& left = frames[pair.frameA].channels[0];
        const an::NormalizationChannelCoefficients& right = frames[pair.frameB].channels[0];
        EXPECT_NEAR(pair.a[0].location * left.scale + left.offset, pair.b[0].location * right.scale + right.offset,
                    1e-12);
        EXPECT_NEAR(pair.a[0].scale * left.scale, pair.b[0].scale * right.scale, 1e-12);
    }

    const an::Result<an::OverlapSolveReport> multiplicativeScaling =
        an::solveOverlapNormalization(execution, pairs, 0u, an::NormalizationMethod::MultiplicativeScaling,
                                      an::OverlapDegeneracyPolicy::Refuse, frames);
    ASSERT_TRUE(multiplicativeScaling.hasValue()) << multiplicativeScaling.status().message();
    EXPECT_NEAR(frames[1].channels[0].scale, (spreads[0] / spreads[1]) * (levels[0] / levels[1]), 1e-12);
    EXPECT_EQ(frames[1].channels[0].offset, 0.0);

    std::vector<an::OverlapMeasurement> flat = pairs;
    flat[0].b[0].scale = 0.0;
    flat[2].a[0].scale = 0.0;
    EXPECT_EQ(an::solveOverlapNormalization(execution, flat, 0u, an::NormalizationMethod::AdditiveScaling,
                                            an::OverlapDegeneracyPolicy::Refuse, frames)
                  .status()
                  .code(),
              an::ErrorCode::InvalidData);
    const an::Result<an::OverlapSolveReport> substituted =
        an::solveOverlapNormalization(execution, flat, 0u, an::NormalizationMethod::AdditiveScaling,
                                      an::OverlapDegeneracyPolicy::SubstituteIdentity, frames);
    ASSERT_TRUE(substituted.hasValue()) << substituted.status().message();
    EXPECT_EQ(substituted->dispersionSubstitutions, 1u);
    EXPECT_EQ(frames[1].channels[0].scale, 1.0);
    EXPECT_NEAR(frames[1].channels[0].offset, levels[0] - levels[1], 1e-12);

    const std::vector<an::OverlapMeasurement> disconnected{pairs[0]};
    EXPECT_EQ(an::solveOverlapNormalization(execution, disconnected, 0u, an::NormalizationMethod::Additive,
                                            an::OverlapDegeneracyPolicy::Refuse, frames)
                  .status()
                  .code(),
              an::ErrorCode::InvalidData);
    EXPECT_EQ(an::solveOverlapNormalization(execution, pairs, 3u, an::NormalizationMethod::Additive,
                                            an::OverlapDegeneracyPolicy::Refuse, frames)
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
    std::vector<an::FrameNormalization> tooMany(an::kMaxOverlapNormalizationFrames + 1u);
    EXPECT_EQ(an::solveOverlapNormalization(execution, pairs, 0u, an::NormalizationMethod::Additive,
                                            an::OverlapDegeneracyPolicy::Refuse, tooMany)
                  .status()
                  .code(),
              an::ErrorCode::LimitExceeded);
}

TEST(GlobalNormalizationTest, SpanningTreeSelectionKeepsReferenceEdgesAndTheHeaviestTree)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::array<an::OverlapCandidate, 5> candidates{
        an::OverlapCandidate{.frameA = 0u, .frameB = 1u, .overlapPixels = 100u},
        an::OverlapCandidate{.frameA = 1u, .frameB = 2u, .overlapPixels = 50u},
        an::OverlapCandidate{.frameA = 2u, .frameB = 3u, .overlapPixels = 80u},
        an::OverlapCandidate{.frameA = 1u, .frameB = 3u, .overlapPixels = 60u},
        an::OverlapCandidate{.frameA = 0u, .frameB = 3u, .overlapPixels = 10u}};
    std::array<std::uint8_t, 5> selected{};
    const an::Result<an::OverlapPairSelection> sparse = an::selectOverlapPairs(
        execution, candidates, 4u, 0u, an::OverlapGraphPolicy::ReferenceStarSpanningTree, selected);
    ASSERT_TRUE(sparse.hasValue()) << sparse.status().message();
    EXPECT_EQ(selected, (std::array<std::uint8_t, 5>{1u, 0u, 1u, 1u, 1u}));
    EXPECT_EQ(sparse->selectedCount, 4u);
    EXPECT_EQ(sparse->referenceEdges, 2u);
    EXPECT_EQ(sparse->spanningTreeEdges, 3u);
    EXPECT_TRUE(sparse->connected);

    const an::Result<an::OverlapPairSelection> all =
        an::selectOverlapPairs(execution, candidates, 4u, 0u, an::OverlapGraphPolicy::AllPairs, selected);
    ASSERT_TRUE(all.hasValue()) << all.status().message();
    EXPECT_EQ(selected, (std::array<std::uint8_t, 5>{1u, 1u, 1u, 1u, 1u}));
    EXPECT_EQ(all->selectedCount, 5u);
    EXPECT_TRUE(all->connected);

    const std::array<an::OverlapCandidate, 1> island{
        an::OverlapCandidate{.frameA = 0u, .frameB = 1u, .overlapPixels = 100u}};
    std::array<std::uint8_t, 1> islandSelected{};
    const an::Result<an::OverlapPairSelection> disconnected = an::selectOverlapPairs(
        execution, island, 3u, 0u, an::OverlapGraphPolicy::ReferenceStarSpanningTree, islandSelected);
    ASSERT_TRUE(disconnected.hasValue()) << disconnected.status().message();
    EXPECT_FALSE(disconnected->connected);
    EXPECT_EQ(an::selectOverlapPairs(execution, candidates, 4u, 4u, an::OverlapGraphPolicy::AllPairs, selected)
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
    std::array<std::uint8_t, 4> short_{};
    EXPECT_EQ(
        an::selectOverlapPairs(execution, candidates, 4u, 0u, an::OverlapGraphPolicy::AllPairs, short_).status().code(),
        an::ErrorCode::InvalidArgument);
}

TEST(GlobalNormalizationTest, TransitionMarksNormalizedAndKeepsValidity)
{
    an::FrameNormalization match;
    match.method = an::NormalizationMethod::AdditiveScaling;
    match.estimator = an::ScaleEstimator::IterativeKSigma;
    match.channelCount = 1u;
    match.channels[0] = an::NormalizationChannelCoefficients{.scale = 1.5, .offset = -0.25};
    an::ImageSemanticsRequest request;
    request.transferFunction = an::TransferFunction::Linear;
    request.processingState.registered = true;
    request.validity = ruleOf(an::ValidityRule::ZeroIsNoData);
    const an::Result<an::ImageSemantics> source = an::ImageSemantics::create(request);
    ASSERT_TRUE(source.hasValue());
    const an::Result<an::MetadataTransition> transition = an::globalNormalizationTransition(match, *source);
    ASSERT_TRUE(transition.hasValue()) << transition.status().message();
    ASSERT_TRUE(transition->semantics.has_value());
    EXPECT_TRUE(transition->semantics->processingState().normalized);
    EXPECT_TRUE(transition->semantics->processingState().registered);
    EXPECT_EQ(transition->semantics->validity().rule(), an::ValidityRule::ZeroIsNoData);
    EXPECT_EQ(transition->semantics->transferFunction(), an::TransferFunction::Linear);
    ASSERT_TRUE(transition->record.has_value());
    EXPECT_EQ(transition->record->operation(), "an::applyGlobalNormalization");
    EXPECT_NE(transition->record->detail().find("AdditiveScaling"), std::string::npos);
    EXPECT_EQ(an::checkOperationEffect(an::kGlobalNormalizationEffect), an::OperationEffectError::None);

    an::FrameNormalization empty;
    EXPECT_EQ(an::globalNormalizationTransition(empty, *source).status().code(), an::ErrorCode::InvalidArgument);
}

// A linear match of one image to another is a least-squares line through the paired samples when the
// two share a geometry, and a match of means and standard deviations when they do not. Both are the
// scaling match this domain already states - the paired route measured over the positions the frames
// share, the independent route measured frame by frame - followed by the apply. This case proves the
// two routes recover a planted line and that the apply writes the reference back, so the match is a
// composition rather than a second estimator.
TEST(GlobalNormalizationTest, ALinearMatchIsAScalingMatchOverTheSharedSamples)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t width = 64u;
    constexpr std::uint32_t height = 48u;
    constexpr double plantedScale = 1.5;
    constexpr double plantedOffset = 0.02;
    const an::ImageDescriptor descriptor = descriptorOf(an::SampleType::Float32, width, height);
    const an::ImageLayout layout = layoutOf(descriptor);

    std::vector<float> target(static_cast<std::size_t>(width) * height, 0.0f);
    std::vector<float> reference(target.size(), 0.0f);
    for (std::size_t index = 0u; index < target.size(); ++index)
    {
        target[index] = patterned(index, 5u);
        reference[index] = static_cast<float>(plantedScale * target[index] + plantedOffset);
    }
    const an::NormalizationFrameView targetView = frameOf(target.data(), layout, descriptor);
    const an::NormalizationFrameView referenceView = frameOf(reference.data(), layout, descriptor);
    const an::StatisticsRegion whole{.originX = 0u, .originY = 0u, .width = width, .height = height};

    // The paired route: both frames measured over the positions they share, then the reference's
    // statistics stand as the reference and the target's as the frame.
    const an::Result<an::OverlapMeasurement> overlap = an::measureOverlapStatistics(
        execution, 0u, referenceView, whole, 1u, targetView, whole, an::ScaleEstimator::MedianAbsoluteDeviation);
    ASSERT_TRUE(overlap.hasValue()) << overlap.status().message();
    EXPECT_EQ(overlap->sharedSamples, target.size());
    an::NormalizationReference pairedReference;
    pairedReference.estimator = overlap->estimator;
    pairedReference.channelCount = overlap->channelCount;
    pairedReference.memberCount = 1u;
    pairedReference.channels[0] = overlap->a[0];
    an::NormalizationFrameStatistics pairedTarget;
    pairedTarget.estimator = overlap->estimator;
    pairedTarget.channelCount = overlap->channelCount;
    pairedTarget.channels[0] = overlap->b[0];
    const an::Result<an::FrameNormalization> paired =
        an::estimateGlobalNormalization(pairedReference, pairedTarget, an::NormalizationMethod::AdditiveScaling);
    ASSERT_TRUE(paired.hasValue()) << paired.status().message();
    EXPECT_NEAR(paired->channels[0].scale, plantedScale, 1.0e-4);
    EXPECT_NEAR(paired->channels[0].offset, plantedOffset, 1.0e-4);

    // The independent route: each frame measured on its own, which is what a reference of another
    // geometry allows, and here the same numbers because the overlap was the whole frame.
    const an::Result<an::NormalizationFrameStatistics> referenceStatistics =
        an::measureGlobalNormalization(execution, referenceView, an::ScaleEstimator::MedianAbsoluteDeviation);
    ASSERT_TRUE(referenceStatistics.hasValue()) << referenceStatistics.status().message();
    const an::Result<an::NormalizationFrameStatistics> targetStatistics =
        an::measureGlobalNormalization(execution, targetView, an::ScaleEstimator::MedianAbsoluteDeviation);
    ASSERT_TRUE(targetStatistics.hasValue()) << targetStatistics.status().message();
    const an::Result<an::NormalizationReference> combined = an::combineNormalizationReferences(
        std::span<const an::NormalizationFrameStatistics>{&*referenceStatistics, 1u});
    ASSERT_TRUE(combined.hasValue()) << combined.status().message();
    const an::Result<an::FrameNormalization> independent =
        an::estimateGlobalNormalization(*combined, *targetStatistics, an::NormalizationMethod::AdditiveScaling);
    ASSERT_TRUE(independent.hasValue()) << independent.status().message();
    EXPECT_NEAR(independent->channels[0].scale, paired->channels[0].scale, 1.0e-9);
    EXPECT_NEAR(independent->channels[0].offset, paired->channels[0].offset, 1.0e-9);

    // The apply writes the reference back from the target.
    std::vector<float> matched(target.size(), 0.0f);
    const an::Result<an::NormalizationApplyResult> applied =
        an::applyGlobalNormalization(execution, *paired, targetView, matched.data(), layout.totalBytes(), layout);
    ASSERT_TRUE(applied.hasValue()) << applied.status().message();
    for (std::size_t index = 0u; index < matched.size(); ++index)
    {
        ASSERT_NEAR(matched[index], reference[index], 1.0e-4) << "sample " << index;
    }
}

} // namespace

TEST(GlobalNormalizationTest, OverlapFailureLeavesEveryCoefficientUnchanged)
{
    an::OverlapMeasurement pair;
    pair.frameA = 0u;
    pair.frameB = 1u;
    pair.channelCount = 2u;
    pair.sharedSamples = 10u;
    pair.a[0].location = 2.0;
    pair.b[0].location = 1.0;
    pair.a[1].location = 0.0;
    pair.b[1].location = 0.0; // The second multiplicative system is singular, after the first succeeded.
    for (const std::uint64_t budget : {1ull, 1024ull * 1024ull})
    {
        an::EngineContextOptions options;
        options.maxConcurrency = 1u;
        options.limits.memoryBudgetBytes = budget;
        auto engine = an::EngineContext::create(options);
        ASSERT_TRUE(engine.hasValue());
        auto execution = an::ExecutionContext::create(*engine, {});
        ASSERT_TRUE(execution.hasValue());
        std::array<an::FrameNormalization, 2u> frames{};
        for (auto& frame : frames)
        {
            frame.channelCount = 2u;
            frame.channels[0] = {.scale = 9.0, .offset = 7.0};
            frame.channels[1] = {.scale = 8.0, .offset = 6.0};
        }
        const auto refused = an::solveOverlapNormalization(
            *execution, std::span<const an::OverlapMeasurement>(&pair, 1u), 0u, an::NormalizationMethod::Multiplicative,
            an::OverlapDegeneracyPolicy::Refuse, frames);
        EXPECT_EQ(refused.code(), budget == 1u ? an::ErrorCode::MemoryBudgetExceeded : an::ErrorCode::InvalidData);
        for (const auto& frame : frames)
        {
            EXPECT_EQ(frame.channelCount, 2u);
            EXPECT_DOUBLE_EQ(frame.channels[0].scale, 9.0);
            EXPECT_DOUBLE_EQ(frame.channels[0].offset, 7.0);
            EXPECT_DOUBLE_EQ(frame.channels[1].scale, 8.0);
            EXPECT_DOUBLE_EQ(frame.channels[1].offset, 6.0);
        }
    }
}

TEST(GlobalNormalizationTest, IdentityRetainsTheChosenEstimator)
{
    for (const auto estimator : {an::ScaleEstimator::MedianAbsoluteDeviation, an::ScaleEstimator::IterativeKSigma})
    {
        const auto identity = an::identityNormalization(an::NormalizationMethod::Additive, 3u, estimator);
        ASSERT_TRUE(identity.hasValue());
        EXPECT_EQ(identity->estimator, estimator);
    }
    EXPECT_EQ(
        an::identityNormalization(an::NormalizationMethod::Additive, 1u, static_cast<an::ScaleEstimator>(255)).code(),
        an::ErrorCode::InvalidArgument);
}
