// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Algorithms/Normalization/LocalNormalization.h>
#include <AfterNightEngine/Algorithms/Stars/StarProfile.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

namespace
{
// A consumer selects its reference from measured values; the normalization kernel takes the
// selected background explicitly. These recipes exercise every ranking choice.
std::vector<std::size_t> rankReferences(std::span<const std::optional<double>> scores)
{
    std::vector<std::size_t> indices;
    for (std::size_t i = 0u; i < scores.size(); ++i)
        if (scores[i] && std::isfinite(*scores[i]))
            indices.push_back(i);
    std::sort(indices.begin(), indices.end(),
              [&](std::size_t a, std::size_t b) { return *scores[a] == *scores[b] ? a < b : *scores[a] > *scores[b]; });
    return indices;
}

TEST(StackReferenceCompositionTest, AllSixRankingCriteriaUseMeasuredValuesAndStableFrameTies)
{
    auto engine = an::EngineContext::create({.maxConcurrency = 1u});
    auto execution = an::ExecutionContext::create(*engine, {});
    std::array<std::array<std::optional<double>, 3u>, 6u> scores;
    const std::array<double, 3u> widths{1.0, 2.0, 4.0}, roundness{0.5, 0.8, 1.0};
    const std::array<double, 3u> amplitudes{4.0, 8.0, 6.0}, backgrounds{0.3, 0.1, 0.2};
    const std::array<double, 3u> deviations{2.0, 4.0, 1.0}, magnitudes{-5.0, -3.0, -4.0};
    for (std::size_t frame = 0u; frame < 3u; ++frame)
    {
        std::vector<an::DetectedStar> stars(frame + 2u);
        for (auto& star : stars)
        {
            star.psf.amplitude = amplitudes[frame];
            star.psf.fullWidthX = star.psf.fullWidthY = widths[frame];
            star.metrics.meanFullWidth = widths[frame];
            star.metrics.roundness = roundness[frame];
            star.instrumentalMagnitude = magnitudes[frame];
        }
        an::StarChannelStatistics channel{
            .background = backgrounds[frame], .backgroundNoise = 1.0, .medianAbsoluteDeviation = deviations[frame]};
        const auto summary = an::summarizeStarField(*execution, stars, channel);
        ASSERT_TRUE(summary.hasValue());
        scores[0][frame] = summary->quality; // PSF signal weight.
        channel.backgroundNoise = *channel.medianAbsoluteDeviation * 1.4826 + 1e-12;
        const auto robustSnr = an::summarizeStarField(*execution, stars, channel);
        ASSERT_TRUE(robustSnr.hasValue());
        scores[1][frame] = robustSnr->medianSignalToNoise; // PSF SNR against scaled MAD.
        std::vector<double> acceptedMagnitudes;
        for (const auto& star : stars)
            if (!star.psf.saturated && star.instrumentalMagnitude && std::isfinite(*star.instrumentalMagnitude))
                acceptedMagnitudes.push_back(*star.instrumentalMagnitude);
        std::sort(acceptedMagnitudes.begin(), acceptedMagnitudes.end());
        const auto middle = acceptedMagnitudes.size() / 2u;
        scores[2][frame] =
            -(acceptedMagnitudes[middle] + acceptedMagnitudes[(acceptedMagnitudes.size() - 1u) / 2u]) / 2.0; // M-star.
        scores[3][frame] = summary->channel.background;
        scores[4][frame] = static_cast<double>(summary->starCount);
        ASSERT_TRUE(summary->medianSignalToNoise && summary->medianFullWidth && summary->medianRoundness);
        scores[5][frame] = std::log1p(static_cast<double>(summary->starCount)) * 10.0 +
                           std::log1p((std::max)(0.0, *summary->medianSignalToNoise)) * 2.0 -
                           (std::max)(0.0, *summary->medianFullWidth) * 5.0 -
                           std::clamp(1.0 - *summary->medianRoundness, 0.0, 1.0) * 10.0;
    }
    const std::array<std::size_t, 6u> winners{1u, 2u, 0u, 0u, 2u, 1u};
    for (std::size_t criterion = 0u; criterion < scores.size(); ++criterion)
        EXPECT_EQ(rankReferences(scores[criterion]).front(), winners[criterion]);
    const std::array<std::optional<double>, 4u> tied{0.0, std::nullopt, -1.0, 0.0};
    EXPECT_EQ(rankReferences(tied), (std::vector<std::size_t>{0u, 3u, 2u}));
    const std::array<std::optional<double>, 2u> missing{};
    EXPECT_TRUE(rankReferences(missing).empty()); // The caller retains its explicit reference.
}

TEST(StackReferenceCompositionTest, SingleAndIntegratedBackgroundsKeepTheAnalysisReferenceResidual)
{
    auto engine = an::EngineContext::create({.maxConcurrency = 2u});
    auto execution = an::ExecutionContext::create(*engine, {});
    const auto extent = *an::ImageExtent::create(9u, 7u, 1u, execution->limits());
    const auto descriptor = *an::ImageDescriptor::create({.extent = extent,
                                                          .sampleType = an::SampleType::Float32,
                                                          .channelSchema = an::ChannelSchema::Mono,
                                                          .colorSpace = an::ColorSpace::Unspecified,
                                                          .numericDomain = an::NumericDomain::NormalizedLinear,
                                                          .cfa = {},
                                                          .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                          .pixelAspectRatio = 1.0});
    const auto layout = *an::ImageLayout::create({extent, 4u, 1u, 1u, 1u}, execution->limits());
    const an::LocalNormalizationGridShape shape{.gridSize = 1u, .width = 9u, .height = 7u, .channelCount = 1u};
    const std::array<double, 4u> best{0.5, 0.5, 0.5, 0.5}, next{0.3, 0.3, 0.3, 0.3}, last{0.1, 0.1, 0.1, 0.1};
    const std::array<std::span<const double>, 3u> ranked{best, next, last};
    std::array<float, 63u> samples, adjusted;
    for (std::size_t i = 0u; i < samples.size(); ++i)
        samples[i] = 0.5f + static_cast<float>(i) / 100.0f;
    const auto view = an::NormalizationFrameView::create(samples.data(), sizeof(samples), layout, descriptor);
    ASSERT_TRUE(view.hasValue()) << view.status().message();
    const std::array<double, 1u> unity{1.0};
    for (std::size_t count = 1u; count <= ranked.size(); ++count)
    {
        std::array<double, 4u> combined{};
        ASSERT_FALSE(an::combineLocalGrids(shape, std::span{ranked}.first(count), combined).failed());
        const double expected = count == 1u ? 0.5 : (count == 2u ? 0.4 : 0.3);
        for (double value : combined)
            EXPECT_DOUBLE_EQ(value, expected);
        const an::LocalNormalizationApplyRequest apply{.shape = shape,
                                                       .interpolation = an::LocalGridInterpolation::Bilinear,
                                                       .targetBackground = best,
                                                       .referenceBackground = combined,
                                                       .scale = unity};
        const auto result =
            an::applyLocalNormalization(*execution, apply, *view, adjusted.data(), sizeof(adjusted), layout);
        ASSERT_TRUE(result.hasValue()) << result.status().message();
        for (std::size_t i = 0u; i < samples.size(); ++i)
            EXPECT_NEAR(adjusted[i] - expected, static_cast<double>(samples[i]) - 0.5, 6e-8);
        an::LocalNormalizationDocument document;
        document.referenceIndex = count == 1u ? std::optional<std::uint32_t>{2u} : std::nullopt;
        EXPECT_EQ(document.referenceIndex.has_value(), count == 1u); // An integrated reference skips no frame.
    }
}
} // namespace
