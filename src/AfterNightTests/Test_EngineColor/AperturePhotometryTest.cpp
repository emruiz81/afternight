// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for aperture photometry over a checked three-channel frame.

#include <AfterNightEngine/Algorithms/Spectrophotometry/AperturePhotometry.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "ColorTestSupport.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stop_token>
#include <vector>

namespace
{

using color_test::frameOf;
using color_test::makeEngine;
using color_test::makeExecution;
using color_test::planarLayout;
using color_test::rgbDescriptor;

/// A flat three-plane frame of one background per channel.
template <typename SampleT>
[[nodiscard]] std::vector<SampleT> flatFrame(std::uint32_t width, std::uint32_t height,
                                             const std::array<double, 3u>& background)
{
    std::vector<SampleT> samples(static_cast<std::size_t>(width) * height * 3u);
    const std::size_t planeSize = static_cast<std::size_t>(width) * height;
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        std::fill(samples.begin() + static_cast<std::ptrdiff_t>(channel * planeSize),
                  samples.begin() + static_cast<std::ptrdiff_t>((channel + 1u) * planeSize),
                  static_cast<SampleT>(background[channel]));
    }
    return samples;
}

/// Adds a signal to one pixel of every channel.
template <typename SampleT>
void addSignal(std::vector<SampleT>& samples, std::uint32_t width, std::uint32_t height, std::uint32_t x,
               std::uint32_t y, const std::array<double, 3u>& signal)
{
    const std::size_t planeSize = static_cast<std::size_t>(width) * height;
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        samples[channel * planeSize + static_cast<std::size_t>(y) * width + x] += static_cast<SampleT>(signal[channel]);
    }
}

/// A fixed-radius request, in engine pixels.
[[nodiscard]] an::AperturePhotometryRequest fixedRequest()
{
    an::AperturePhotometryRequest request;
    request.radiusPolicy = an::ApertureRadiusPolicy::FixedPixels;
    request.annulusPolicy = an::AnnulusRadiusPolicy::FixedPixels;
    request.apertureRadiusPixels = 1.25;
    request.annulusInnerRadiusPixels = 4.0;
    request.annulusOuterRadiusPixels = 6.0;
    request.annulusClipSigmas = 3.0;
    request.minimumSignalToNoise = 0.0;
    request.rejectSaturated = true;
    request.rejectBlends = true;
    return request;
}

/// The target centred on pixel (10, 10) of a frame, whose centre is at (10.5, 10.5).
[[nodiscard]] an::ApertureTarget centeredTarget()
{
    return an::ApertureTarget{.position = {.x = 10.5, .y = 10.5}, .fullWidthPixels = 2.4};
}

TEST(AperturePhotometryTest, MeasuresBackgroundSubtractedFluxInEveryChannel)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kSize = 21u;
    std::vector<double> samples = flatFrame<double>(kSize, kSize, {1.0, 2.0, 3.0});
    addSignal(samples, kSize, kSize, 10u, 10u, {10.0, 20.0, 30.0});
    const an::ImageDescriptor descriptor =
        rgbDescriptor(kSize, kSize, an::SampleType::Float64, 3u, an::NumericDomain::PhysicalScaled);
    const an::ImageLayout layout = planarLayout(descriptor);
    const an::StarFrameView frame = frameOf(samples, layout, descriptor);

    const an::Result<an::AperturePhotometryMeasurement> measured =
        an::measureAperturePhotometry(execution, frame, centeredTarget(), {}, fixedRequest());
    ASSERT_TRUE(measured.hasValue()) << measured.status().message();
    EXPECT_EQ(measured->rejection, an::ApertureRejectionReason::None);
    EXPECT_NEAR(measured->apertureRadiusPixels, 1.25, 1.0e-12);
    EXPECT_NEAR(measured->annulusInnerRadiusPixels, 4.0, 1.0e-12);
    EXPECT_NEAR(measured->annulusOuterRadiusPixels, 6.0, 1.0e-12);
    EXPECT_GT(measured->backgroundSampleCount, 8u);
    EXPECT_EQ(measured->overlappingNeighborCount, 0u);
    EXPECT_NEAR(measured->channels[0].localBackground, 1.0, 1.0e-12);
    EXPECT_NEAR(measured->channels[1].localBackground, 2.0, 1.0e-12);
    EXPECT_NEAR(measured->channels[2].localBackground, 3.0, 1.0e-12);
    EXPECT_NEAR(measured->channels[0].flux, 10.0, 1.0e-9);
    EXPECT_NEAR(measured->channels[1].flux, 20.0, 1.0e-9);
    EXPECT_NEAR(measured->channels[2].flux, 30.0, 1.0e-9);
    EXPECT_GT(measured->channels[0].uncertainty, 0.0);
    EXPECT_GT(measured->channels[0].signalToNoise, 0.0);
    EXPECT_FALSE(measured->channels[0].saturated);
    EXPECT_GT(measured->apertureArea, 1.0);

    an::AperturePhotometryRequest whole = fixedRequest();
    whole.usePartialPixels = false;
    const an::Result<an::AperturePhotometryMeasurement> wholePixels =
        an::measureAperturePhotometry(execution, frame, centeredTarget(), {}, whole);
    ASSERT_TRUE(wholePixels.hasValue()) << wholePixels.status().message();
    EXPECT_NEAR(wholePixels->apertureArea, 5.0, 1.0e-12);
    EXPECT_NEAR(wholePixels->channels[2].flux, 30.0, 1.0e-9);
}

TEST(AperturePhotometryTest, RadiiFollowTheStatedPolicies)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kSize = 41u;
    std::vector<float> samples = flatFrame<float>(kSize, kSize, {0.1f, 0.1f, 0.1f});
    addSignal(samples, kSize, kSize, 20u, 20u, {0.5, 0.5, 0.5});
    const an::ImageDescriptor descriptor = rgbDescriptor(kSize, kSize);
    const an::ImageLayout layout = planarLayout(descriptor);
    const an::StarFrameView frame = frameOf(samples, layout, descriptor);

    an::AperturePhotometryRequest scaled = fixedRequest();
    scaled.radiusPolicy = an::ApertureRadiusPolicy::FullWidthScaledClamped;
    scaled.fullWidthRadiusScale = 2.0;
    scaled.minimumApertureRadiusPixels = 2.0;
    scaled.maximumApertureRadiusPixels = 4.0;
    scaled.annulusInnerRadiusPixels = 6.0;
    scaled.annulusOuterRadiusPixels = 8.0;
    const an::Result<an::AperturePhotometryMeasurement> clamped = an::measureAperturePhotometry(
        execution, frame, {.position = {.x = 20.5, .y = 20.5}, .fullWidthPixels = 10.0}, {}, scaled);
    ASSERT_TRUE(clamped.hasValue()) << clamped.status().message();
    EXPECT_EQ(clamped->rejection, an::ApertureRejectionReason::None);
    EXPECT_NEAR(clamped->apertureRadiusPixels, 4.0, 1.0e-12);

    const an::AperturePhotometryRequest defaults{};
    const an::Result<an::AperturePhotometryMeasurement> scaledAnnulus = an::measureAperturePhotometry(
        execution, frame, {.position = {.x = 20.5, .y = 20.5}, .fullWidthPixels = 2.4}, {}, defaults);
    ASSERT_TRUE(scaledAnnulus.hasValue()) << scaledAnnulus.status().message();
    EXPECT_EQ(scaledAnnulus->rejection, an::ApertureRejectionReason::None);
    EXPECT_NEAR(scaledAnnulus->apertureRadiusPixels, 4.8, 1.0e-12);
    EXPECT_NEAR(scaledAnnulus->annulusInnerRadiusPixels, 7.2, 1.0e-12);
    EXPECT_NEAR(scaledAnnulus->annulusOuterRadiusPixels, 17.2, 1.0e-12);

    const an::Result<an::AperturePhotometryMeasurement> widthless = an::measureAperturePhotometry(
        execution, frame, {.position = {.x = 20.5, .y = 20.5}, .fullWidthPixels = 0.0}, {}, defaults);
    ASSERT_TRUE(widthless.hasValue());
    EXPECT_EQ(widthless->rejection, an::ApertureRejectionReason::InvalidGeometry);
    const an::Result<an::AperturePhotometryMeasurement> tooWide = an::measureAperturePhotometry(
        execution, frame, {.position = {.x = 20.5, .y = 20.5}, .fullWidthPixels = 30.0}, {}, defaults);
    ASSERT_TRUE(tooWide.hasValue());
    EXPECT_EQ(tooWide->rejection, an::ApertureRejectionReason::InvalidGeometry);

    an::AperturePhotometryRequest inverted = fixedRequest();
    inverted.annulusInnerRadiusPixels = 1.0;
    const an::Result<an::AperturePhotometryMeasurement> refused =
        an::measureAperturePhotometry(execution, frame, centeredTarget(), {}, inverted);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidArgument);
    an::AperturePhotometryRequest capped = fixedRequest();
    capped.annulusOuterRadiusPixels = 65.0;
    EXPECT_EQ(an::measureAperturePhotometry(execution, frame, centeredTarget(), {}, capped).status().code(),
              an::ErrorCode::InvalidArgument);
}

TEST(AperturePhotometryTest, RejectionsNameTheirReason)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kSize = 21u;
    std::vector<double> samples = flatFrame<double>(kSize, kSize, {1.0, 1.0, 1.0});
    addSignal(samples, kSize, kSize, 10u, 10u, {10.0, 10.0, 10.0});
    const an::ImageDescriptor descriptor =
        rgbDescriptor(kSize, kSize, an::SampleType::Float64, 3u, an::NumericDomain::PhysicalScaled);
    const an::ImageLayout layout = planarLayout(descriptor);
    const an::StarFrameView frame = frameOf(samples, layout, descriptor);

    const an::Result<an::AperturePhotometryMeasurement> edge = an::measureAperturePhotometry(
        execution, frame, {.position = {.x = 3.5, .y = 10.5}, .fullWidthPixels = 2.4}, {}, fixedRequest());
    ASSERT_TRUE(edge.hasValue());
    EXPECT_EQ(edge->rejection, an::ApertureRejectionReason::EdgeClipped);
    an::AperturePhotometryRequest tolerant = fixedRequest();
    tolerant.rejectEdgeClipped = false;
    const an::Result<an::AperturePhotometryMeasurement> clippedAnyway = an::measureAperturePhotometry(
        execution, frame, {.position = {.x = 3.5, .y = 10.5}, .fullWidthPixels = 2.4}, {}, tolerant);
    ASSERT_TRUE(clippedAnyway.hasValue());
    EXPECT_EQ(clippedAnyway->rejection, an::ApertureRejectionReason::None);

    an::AperturePhotometryRequest saturating = fixedRequest();
    saturating.saturationThresholds = {5.0, 5.0, 5.0};
    const an::Result<an::AperturePhotometryMeasurement> saturated =
        an::measureAperturePhotometry(execution, frame, centeredTarget(), {}, saturating);
    ASSERT_TRUE(saturated.hasValue());
    EXPECT_EQ(saturated->rejection, an::ApertureRejectionReason::Saturated);
    EXPECT_TRUE(saturated->channels[0].saturated);
    saturating.rejectSaturated = false;
    const an::Result<an::AperturePhotometryMeasurement> flaggedOnly =
        an::measureAperturePhotometry(execution, frame, centeredTarget(), {}, saturating);
    ASSERT_TRUE(flaggedOnly.hasValue());
    EXPECT_EQ(flaggedOnly->rejection, an::ApertureRejectionReason::None);
    EXPECT_TRUE(flaggedOnly->channels[1].saturated);
    EXPECT_NEAR(flaggedOnly->channels[1].flux, 10.0, 1.0e-9);

    an::AperturePhotometryRequest demanding = fixedRequest();
    demanding.minimumSignalToNoise = 1.0e6;
    const an::Result<an::AperturePhotometryMeasurement> faint =
        an::measureAperturePhotometry(execution, frame, centeredTarget(), {}, demanding);
    ASSERT_TRUE(faint.hasValue());
    EXPECT_EQ(faint->rejection, an::ApertureRejectionReason::LowSignalToNoise);
    an::AperturePhotometryRequest ungated = fixedRequest();
    ungated.minimumSignalToNoise = 0.0;
    const an::Result<an::AperturePhotometryMeasurement> accepted =
        an::measureAperturePhotometry(execution, frame, centeredTarget(), {}, ungated);
    ASSERT_TRUE(accepted.hasValue());
    EXPECT_EQ(accepted->rejection, an::ApertureRejectionReason::None);

    const std::vector<an::ApertureNeighbor> neighbors{
        an::ApertureNeighbor{.position = {.x = 13.5, .y = 10.5}, .maskRadiusPixels = std::nullopt}};
    const an::Result<an::AperturePhotometryMeasurement> blended =
        an::measureAperturePhotometry(execution, frame, centeredTarget(), neighbors, fixedRequest());
    ASSERT_TRUE(blended.hasValue());
    EXPECT_EQ(blended->rejection, an::ApertureRejectionReason::Blended);
    EXPECT_EQ(blended->overlappingNeighborCount, 1u);
    an::AperturePhotometryRequest blendTolerant = fixedRequest();
    blendTolerant.rejectBlends = false;
    const an::Result<an::AperturePhotometryMeasurement> masked =
        an::measureAperturePhotometry(execution, frame, centeredTarget(), neighbors, blendTolerant);
    ASSERT_TRUE(masked.hasValue());
    EXPECT_EQ(masked->rejection, an::ApertureRejectionReason::None);
    EXPECT_LT(masked->backgroundSampleCount, accepted->backgroundSampleCount);

    std::vector<double> holed = samples;
    holed[static_cast<std::size_t>(10u) * kSize + 11u] = std::numeric_limits<double>::quiet_NaN();
    const std::optional<an::ValidityDescriptor> finite =
        an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly});
    ASSERT_TRUE(finite.has_value());
    const an::StarFrameView holedFrame = frameOf(holed, layout, descriptor, {.descriptor = *finite});
    const an::Result<an::AperturePhotometryMeasurement> bad =
        an::measureAperturePhotometry(execution, holedFrame, centeredTarget(), {}, fixedRequest());
    ASSERT_TRUE(bad.hasValue());
    EXPECT_EQ(bad->rejection, an::ApertureRejectionReason::BadPixel);
    an::AperturePhotometryRequest skipping = fixedRequest();
    skipping.rejectBadPixels = false;
    const an::Result<an::AperturePhotometryMeasurement> skipped =
        an::measureAperturePhotometry(execution, holedFrame, centeredTarget(), {}, skipping);
    ASSERT_TRUE(skipped.hasValue());
    EXPECT_EQ(skipped->rejection, an::ApertureRejectionReason::None);
    EXPECT_LT(skipped->apertureArea, accepted->apertureArea);
    EXPECT_NEAR(skipped->channels[0].flux, 10.0, 1.0e-9);
    const an::StarFrameView unstatedFrame = frameOf(holed, layout, descriptor);
    const an::Result<an::AperturePhotometryMeasurement> nonFinite =
        an::measureAperturePhotometry(execution, unstatedFrame, centeredTarget(), {}, skipping);
    ASSERT_TRUE(nonFinite.hasValue());
    EXPECT_EQ(nonFinite->rejection, an::ApertureRejectionReason::BadPixel);

    an::AperturePhotometryRequest hungry = fixedRequest();
    hungry.annulusInnerRadiusPixels = 4.01;
    hungry.annulusOuterRadiusPixels = 4.1;
    const an::Result<an::AperturePhotometryMeasurement> starved =
        an::measureAperturePhotometry(execution, frame, centeredTarget(), {}, hungry);
    ASSERT_TRUE(starved.hasValue());
    EXPECT_EQ(starved->rejection, an::ApertureRejectionReason::InsufficientBackgroundSamples);

    EXPECT_STREQ(an::apertureRejectionReasonName(an::ApertureRejectionReason::Blended), "Blended");
    EXPECT_STREQ(an::apertureRejectionReasonName(static_cast<an::ApertureRejectionReason>(8u)),
                 "UnknownApertureRejectionReason");
    EXPECT_FALSE(an::isApertureRejectionReason(static_cast<an::ApertureRejectionReason>(8u)));
    EXPECT_EQ(an::checkOperationEffect(an::kAperturePhotometryEffect), an::OperationEffectError::None);
}

TEST(AperturePhotometryTest, AnnulusOutlierIsClippedBeforeTheSkyIsEstimated)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kSize = 21u;
    std::vector<float> samples = flatFrame<float>(kSize, kSize, {1.0f, 1.0f, 1.0f});
    addSignal(samples, kSize, kSize, 10u, 10u, {10.0, 10.0, 10.0});
    for (std::uint32_t index = 0u; index < 6u; ++index)
    {
        samples[static_cast<std::size_t>(5u + index) * kSize + 5u] += 0.01f * static_cast<float>(index + 1u);
    }
    addSignal(samples, kSize, kSize, 15u, 10u, {50.0, 50.0, 50.0});
    const an::ImageDescriptor descriptor =
        rgbDescriptor(kSize, kSize, an::SampleType::Float32, 3u, an::NumericDomain::PhysicalScaled);
    const an::ImageLayout layout = planarLayout(descriptor);
    const an::StarFrameView frame = frameOf(samples, layout, descriptor);
    const an::Result<an::AperturePhotometryMeasurement> measured =
        an::measureAperturePhotometry(execution, frame, centeredTarget(), {}, fixedRequest());
    ASSERT_TRUE(measured.hasValue()) << measured.status().message();
    EXPECT_EQ(measured->rejection, an::ApertureRejectionReason::None);
    EXPECT_NEAR(measured->channels[0].localBackground, 1.0, 1.0e-6);
    EXPECT_NEAR(measured->channels[0].flux, 10.0, 1.0e-4);
}

TEST(AperturePhotometryTest, BatchMatchesSingleMeasurementsAndIsThreadCountIndependent)
{
    const an::EngineContext engine = makeEngine(8u);
    constexpr std::uint32_t kSize = 96u;
    std::vector<float> samples = flatFrame<float>(kSize, kSize, {0.05f, 0.06f, 0.04f});
    std::vector<an::ApertureTarget> targets;
    for (std::uint32_t row = 0u; row < 3u; ++row)
    {
        for (std::uint32_t column = 0u; column < 3u; ++column)
        {
            const std::uint32_t x = 16u + 30u * column;
            const std::uint32_t y = 16u + 30u * row;
            addSignal(samples, kSize, kSize, x, y, {0.3 + 0.05 * column, 0.4, 0.2 + 0.05 * row});
            targets.push_back(an::ApertureTarget{.position = {.x = x + 0.5, .y = y + 0.5}, .fullWidthPixels = 2.0});
        }
    }
    targets.push_back(an::ApertureTarget{.position = {.x = 2.5, .y = 2.5}, .fullWidthPixels = 2.0});
    const an::ImageDescriptor descriptor = rgbDescriptor(kSize, kSize);
    const an::ImageLayout layout = planarLayout(descriptor);
    const an::StarFrameView frame = frameOf(samples, layout, descriptor);
    an::AperturePhotometryRequest request = fixedRequest();
    request.rejectBlends = false;

    std::vector<an::AperturePhotometryMeasurement> narrow(targets.size());
    std::vector<an::AperturePhotometryMeasurement> wide(targets.size());
    const an::ExecutionContext single = makeExecution(engine, 1u);
    const an::ExecutionContext parallel = makeExecution(engine, 6u);
    const an::Result<an::AperturePhotometryBatchResult> one = an::measureAperturePhotometryBatch(
        single, frame, targets, request, an::ApertureNeighborPolicy::OtherTargets, narrow);
    ASSERT_TRUE(one.hasValue()) << one.status().message();
    const an::Result<an::AperturePhotometryBatchResult> many = an::measureAperturePhotometryBatch(
        parallel, frame, targets, request, an::ApertureNeighborPolicy::OtherTargets, wide);
    ASSERT_TRUE(many.hasValue()) << many.status().message();
    EXPECT_EQ(one->acceptedCount, 9u);
    EXPECT_EQ(one->rejectedCount, 1u);
    EXPECT_EQ(one->rejections[static_cast<std::size_t>(an::ApertureRejectionReason::EdgeClipped)], 1u);
    EXPECT_EQ(many->acceptedCount, one->acceptedCount);
    EXPECT_GT(many->workingBytes, 0u);
    for (std::size_t index = 0u; index < targets.size(); ++index)
    {
        EXPECT_EQ(narrow[index].rejection, wide[index].rejection);
        for (std::uint32_t channel = 0u; channel < 3u; ++channel)
        {
            EXPECT_EQ(narrow[index].channels[channel].flux, wide[index].channels[channel].flux);
            EXPECT_EQ(narrow[index].channels[channel].localBackground, wide[index].channels[channel].localBackground);
        }
    }
    std::vector<an::ApertureNeighbor> others;
    for (std::size_t index = 1u; index < targets.size(); ++index)
    {
        others.push_back(an::ApertureNeighbor{.position = targets[index].position});
    }
    const an::Result<an::AperturePhotometryMeasurement> alone =
        an::measureAperturePhotometry(single, frame, targets[0], others, request);
    ASSERT_TRUE(alone.hasValue());
    EXPECT_EQ(alone->channels[0].flux, narrow[0].channels[0].flux);
    EXPECT_EQ(alone->backgroundSampleCount, narrow[0].backgroundSampleCount);
    EXPECT_NEAR(narrow[0].channels[1].flux, 0.4, 1.0e-5);
    EXPECT_NEAR(narrow[4].channels[0].flux, 0.35, 1.0e-5);

    std::vector<an::AperturePhotometryMeasurement> wrong(targets.size() - 1u);
    EXPECT_EQ(
        an::measureAperturePhotometryBatch(single, frame, targets, request, an::ApertureNeighborPolicy::None, wrong)
            .status()
            .code(),
        an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(an::measureAperturePhotometryBatch(single, frame, {}, request, an::ApertureNeighborPolicy::None, {})
                  .status()
                  .code(),
              an::ErrorCode::EmptySelection);
    EXPECT_EQ(an::measureAperturePhotometryBatch(single, frame, targets, request,
                                                 static_cast<an::ApertureNeighborPolicy>(2u), narrow)
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::measureAperturePhotometryBatch(an::ExecutionContext{}, frame, targets, request,
                                                 an::ApertureNeighborPolicy::None, narrow)
                  .status()
                  .code(),
              an::ErrorCode::NotInitialized);
    std::stop_source stop;
    stop.request_stop();
    EXPECT_EQ(an::measureAperturePhotometryBatch(makeExecution(engine, 1u, stop.get_token()), frame, targets, request,
                                                 an::ApertureNeighborPolicy::None, narrow)
                  .status()
                  .code(),
              an::ErrorCode::Cancelled);

    const an::ImageDescriptor mono = rgbDescriptor(8u, 8u, an::SampleType::Float32, 1u);
    const an::ImageLayout monoLayout = planarLayout(mono);
    const std::vector<float> monoSamples(64u, 0.1f);
    EXPECT_EQ(
        an::measureAperturePhotometry(single, frameOf(monoSamples, monoLayout, mono), centeredTarget(), {}, request)
            .status()
            .code(),
        an::ErrorCode::Unsupported);
}

TEST(AperturePhotometryTest, IntegerFramesAreReadInTheirOwnDomain)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kSize = 21u;
    std::vector<std::uint16_t> samples = flatFrame<std::uint16_t>(kSize, kSize, {100.0, 200.0, 300.0});
    addSignal(samples, kSize, kSize, 10u, 10u, {1000.0, 2000.0, 3000.0});
    const an::ImageDescriptor descriptor = rgbDescriptor(kSize, kSize, an::SampleType::UInt16);
    const an::ImageLayout layout = planarLayout(descriptor);
    const an::StarFrameView frame = frameOf(samples, layout, descriptor);
    an::AperturePhotometryRequest request = fixedRequest();
    request.saturationThresholds = {65535.0, 65535.0, 65535.0};
    const an::Result<an::AperturePhotometryMeasurement> measured =
        an::measureAperturePhotometry(execution, frame, centeredTarget(), {}, request);
    ASSERT_TRUE(measured.hasValue()) << measured.status().message();
    EXPECT_EQ(measured->rejection, an::ApertureRejectionReason::None);
    EXPECT_NEAR(measured->channels[0].localBackground, 100.0, 1.0e-12);
    EXPECT_NEAR(measured->channels[2].flux, 3000.0, 1.0e-6);
}

} // namespace
