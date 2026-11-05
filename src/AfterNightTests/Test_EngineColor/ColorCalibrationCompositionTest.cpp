// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The manual colour calibrations, spelled as compositions.
//
// Three manual calibrations over an RGB image: neutralize the background against a reference image's
// channel levels, calibrate against a white reference and a background reference, and linearly match
// one image to another. Each is a measurement the statistics domain already makes under a stated
// selection, followed by the shared colour calibration apply or the global normalization apply. These
// cases prove that the compositions give the answers each calibration states, so no facade has to be
// written for them. The linear match's composition is proved beside the normalization it is built
// from, in the calibration suite.

#include <AfterNightEngine/Algorithms/Photometry/ColorCalibrationApply.h>
#include <AfterNightEngine/Algorithms/Statistics/BasicStatistics.h>
#include <AfterNightEngine/Algorithms/Statistics/RobustStatistics.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>

#include "ColorTestSupport.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace
{

using color_test::frameOf;
using color_test::makeEngine;
using color_test::makeExecution;
using color_test::planarLayout;
using color_test::rgbDescriptor;

constexpr std::uint32_t kWidth = 96u;
constexpr std::uint32_t kHeight = 96u;
constexpr std::size_t kPixels = static_cast<std::size_t>(kWidth) * kHeight;

/// The sky each channel sits on: deliberately unequal, which is what the calibrations remove.
constexpr std::array<double, 3u> kSky{0.10, 0.14, 0.08};

/// What the white patch adds above the sky in each channel: a warm cast a white reference removes.
constexpr std::array<double, 3u> kWhiteAboveSky{0.70, 0.48, 0.37};

/// The white patch and the sky patch the calibration measures.
constexpr an::StatisticsRegion kWhiteRegion{.originX = 56u, .originY = 56u, .width = 32u, .height = 32u};
constexpr an::StatisticsRegion kSkyRegion{.originX = 4u, .originY = 4u, .width = 40u, .height = 40u};

/// Deterministic zero-mean noise in [-0.004, 0.004).
[[nodiscard]] double noiseAt(const std::size_t index, const std::uint32_t channel) noexcept
{
    const std::uint64_t mixed = (static_cast<std::uint64_t>(index) + 1u) * 2654435761ull + channel * 40503ull;
    return -0.004 + static_cast<double>((mixed >> 7u) % 8001u) / 1000000.0;
}

/// A planar RGB frame: three skies, one white patch, a few hot pixels that every range excludes.
[[nodiscard]] std::vector<float> makeFrame()
{
    std::vector<float> samples(3u * kPixels, 0.0f);
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        for (std::uint32_t row = 0u; row < kHeight; ++row)
        {
            for (std::uint32_t column = 0u; column < kWidth; ++column)
            {
                const std::size_t index = static_cast<std::size_t>(row) * kWidth + column;
                double value = kSky[channel] + noiseAt(index, channel);
                const bool inWhite = row >= kWhiteRegion.originY && row < kWhiteRegion.originY + kWhiteRegion.height &&
                                     column >= kWhiteRegion.originX &&
                                     column < kWhiteRegion.originX + kWhiteRegion.width;
                if (inWhite)
                {
                    value += kWhiteAboveSky[channel];
                }
                if ((row * 31u + column * 17u) % 613u == 0u)
                {
                    value = 0.98;
                }
                samples[channel * kPixels + index] = static_cast<float>(value);
            }
        }
    }
    return samples;
}

/// The stated closed range a calibration's lower and upper limits become.
[[nodiscard]] an::ValidityDescriptor rangeRule(const double lowest, const double highest)
{
    const std::optional<an::ValidityDescriptor> rule = an::ValidityDescriptor::create(an::ValidityDescriptorRequest{
        .rule = an::ValidityRule::DomainRange, .lowestValid = lowest, .highestValid = highest});
    EXPECT_TRUE(rule.has_value());
    return rule.value_or(an::ValidityDescriptor{});
}

[[nodiscard]] double channelMedian(const an::ExecutionContext& execution, const std::vector<float>& samples,
                                   const an::ImageLayout& layout, const std::uint32_t channel,
                                   const an::StatisticsSelection& selection)
{
    const an::Result<an::RobustStatistics> statistics = an::computeImageRobustStatistics(
        execution, an::SampleType::Float32, samples.data(), layout, channel, selection, an::RobustStatisticsRequest{});
    EXPECT_TRUE(statistics.hasValue()) << statistics.status().message();
    return statistics.hasValue() ? statistics->median : 0.0;
}

[[nodiscard]] double channelMean(const an::ExecutionContext& execution, const std::vector<float>& samples,
                                 const an::ImageLayout& layout, const std::uint32_t channel,
                                 const an::StatisticsSelection& selection)
{
    const an::Result<an::BasicStatistics> statistics =
        an::computeImageBasicStatistics(execution, an::SampleType::Float32, samples.data(), layout, channel, selection);
    EXPECT_TRUE(statistics.hasValue()) << statistics.status().message();
    return statistics.hasValue() ? statistics->mean : 0.0;
}

} // namespace

TEST(ColorCalibrationCompositionTest, BackgroundNeutralizationIsARangedMedianAndASharedPedestalApply)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageDescriptor descriptor = rgbDescriptor(kWidth, kHeight);
    const an::ImageLayout layout = planarLayout(descriptor);
    const std::vector<float> samples = makeFrame();
    const an::StarFrameView frame = frameOf(samples, layout, descriptor);

    // The lower and upper limits are a stated closed range; the white patch and the
    // hot pixels fall outside it, so each channel's level is its sky.
    const an::StatisticsSelection window{.validity = rangeRule(0.0, 0.5)};
    an::ColorChannelOffsets levels{};
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        levels[channel] = channelMedian(execution, samples, layout, channel, window);
        EXPECT_NEAR(levels[channel], kSky[channel], 5.0e-4);
    }

    const double pedestal = an::meanBackgroundPedestal(levels);
    const an::ColorChannelOffsets offsets = an::colorCalibrationOffsets(
        {1.0, 1.0, 1.0}, levels, an::ColorCalibrationBackgroundMode::SharedPedestal, pedestal);
    std::vector<float> neutral(samples.size(), 0.0f);
    const an::Result<an::ColorCalibrationApplyResult> applied = an::applyColorCalibration(
        execution, frame, neutral.data(), neutral.size() * sizeof(float),
        {.gains = {1.0, 1.0, 1.0}, .offsets = offsets, .clipping = an::ColorCalibrationClipping::ClampToPixelRange});
    ASSERT_TRUE(applied.hasValue()) << applied.status().message();
    EXPECT_EQ(applied->samplesWritten, samples.size());

    // Every channel's sky now sits on the one shared level, and each sample moved by its channel's
    // offset and nothing else.
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        EXPECT_NEAR(channelMedian(execution, neutral, layout, channel, window), pedestal, 5.0e-4);
        const std::size_t probe = channel * kPixels + static_cast<std::size_t>(20u) * kWidth + 20u;
        EXPECT_NEAR(neutral[probe], samples[probe] + offsets[channel], 1.0e-6);
    }
}

TEST(ColorCalibrationCompositionTest, AWhiteReferenceBecomesGainsAboutTheBackgroundAndAManualOneIsGainsAlone)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageDescriptor descriptor = rgbDescriptor(kWidth, kHeight);
    const an::ImageLayout layout = planarLayout(descriptor);
    const std::vector<float> samples = makeFrame();
    const an::StarFrameView frame = frameOf(samples, layout, descriptor);

    // The white reference is the mean of the white region within the stated limits; the background
    // reference is the median of the sky region.
    const an::StatisticsSelection whiteSelection{.validity = rangeRule(0.05, 0.95), .region = kWhiteRegion};
    const an::StatisticsSelection skySelection{.validity = rangeRule(0.0, 0.5), .region = kSkyRegion};
    std::array<double, 3u> white{};
    an::ColorChannelOffsets background{};
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        white[channel] = channelMean(execution, samples, layout, channel, whiteSelection);
        background[channel] = channelMedian(execution, samples, layout, channel, skySelection);
        EXPECT_NEAR(white[channel], kSky[channel] + kWhiteAboveSky[channel], 5.0e-4);
        EXPECT_NEAR(background[channel], kSky[channel], 5.0e-4);
    }

    // The gain rule: each channel's white above its background, normalized to the
    // strongest channel, and an offset that keeps every background where it was.
    std::array<double, 3u> aboveSky{};
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        aboveSky[channel] = std::abs(white[channel] - background[channel]);
    }
    const auto strongest =
        static_cast<std::uint32_t>(std::distance(aboveSky.begin(), std::max_element(aboveSky.begin(), aboveSky.end())));
    EXPECT_EQ(strongest, 0u);
    an::ColorChannelGains gains{};
    an::ColorChannelOffsets offsets{};
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        gains[channel] = aboveSky[strongest] / aboveSky[channel];
        offsets[channel] = background[channel] * (1.0 - gains[channel]);
    }
    EXPECT_DOUBLE_EQ(gains[strongest], 1.0);

    std::vector<float> calibrated(samples.size(), 0.0f);
    const an::Result<an::ColorCalibrationApplyResult> applied = an::applyColorCalibration(
        execution, frame, calibrated.data(), calibrated.size() * sizeof(float),
        {.gains = gains, .offsets = offsets, .clipping = an::ColorCalibrationClipping::ClampToPixelRange});
    ASSERT_TRUE(applied.hasValue()) << applied.status().message();

    // The white patch is now the same height above every channel's sky, and the sky itself has not
    // moved.
    const an::StatisticsSelection whiteAfter{.validity = rangeRule(0.0, 1.0), .region = kWhiteRegion};
    const an::StatisticsSelection skyAfter{.validity = rangeRule(0.0, 0.5), .region = kSkyRegion};
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        const double whiteNow = channelMean(execution, calibrated, layout, channel, whiteAfter);
        EXPECT_NEAR(whiteNow - background[channel], aboveSky[strongest], 2.0e-3);
        EXPECT_NEAR(channelMedian(execution, calibrated, layout, channel, skyAfter), background[channel], 1.0e-3);
    }

    // A manual white is the stated gains and nothing else: a multiplication by them, with no
    // background subtracted.
    const an::ColorChannelGains manual{1.0, 0.9, 1.1};
    std::vector<float> manualOut(samples.size(), 0.0f);
    const an::Result<an::ColorCalibrationApplyResult> manualApplied = an::applyColorCalibration(
        execution, frame, manualOut.data(), manualOut.size() * sizeof(float),
        {.gains = manual, .offsets = {0.0, 0.0, 0.0}, .clipping = an::ColorCalibrationClipping::ClampToPixelRange});
    ASSERT_TRUE(manualApplied.hasValue()) << manualApplied.status().message();
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        const std::size_t probe = channel * kPixels + static_cast<std::size_t>(70u) * kWidth + 70u;
        EXPECT_NEAR(manualOut[probe], static_cast<float>(samples[probe] * manual[channel]), 1.0e-6);
    }
}
