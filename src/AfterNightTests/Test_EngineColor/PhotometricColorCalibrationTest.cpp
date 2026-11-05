// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the colour model, the gain fit, the sky estimate, the apply and the whole
// photometric colour calibration over a planted field with known truth.

#include <AfterNightEngine/Algorithms/Photometry/PhotometricColorCalibration.h>

#include <AfterNightEngine/Algorithms/Photometry/ColorCalibrationApply.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/OperationTransition.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/ProgressSink.h>
#include <AfterNightEngine/Runtime/TraceSink.h>

#include "ColorTestSupport.h"

#include <gtest/gtest.h>

#include "../HeapFailure.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace
{

using color_test::buildOrFail;
using color_test::catalogEntriesOf;
using color_test::ColorField;
using color_test::ColorFieldRequest;
using color_test::colorIndicesOf;
using color_test::frameOf;
using color_test::makeColorField;
using color_test::makeEngine;
using color_test::makeExecution;
using color_test::makeSolution;
using color_test::meanOver;
using color_test::planarLayout;
using color_test::rgbDescriptor;

[[nodiscard]] std::vector<an::PhotometricColorSample> perfectSamples(double redFactor, double blueFactor,
                                                                     bool colored = true, int count = 12)
{
    std::vector<an::PhotometricColorSample> samples;
    for (int index = 0; index < count; ++index)
    {
        const std::optional<double> color =
            colored ? std::optional<double>{-0.2 + 0.25 * index} : std::optional<double>{};
        const an::PhotometricColorRatios expected = an::predictPhotometricColorRatios(color);
        const double green = 10000.0 - 400.0 * index;
        samples.push_back(an::PhotometricColorSample{.position = {.x = 1.0 * index, .y = 1.0 * index},
                                                     .catalogMagnitude = 10.0 + 0.1 * index,
                                                     .colorIndexBpMinusRp = color,
                                                     .redFlux = green * expected.redToGreen * redFactor,
                                                     .greenFlux = green,
                                                     .blueFlux = green * expected.blueToGreen * blueFactor,
                                                     .separationArcseconds = 0.5,
                                                     .catalogEntryIndex = static_cast<std::uint32_t>(index)});
    }
    return samples;
}

TEST(PhotometricColorCalibrationTest, ColorModelTracksTheColorIndexAndClampsItsRange)
{
    const an::PhotometricColorRatios blue = an::predictPhotometricColorRatios(-0.2);
    const an::PhotometricColorRatios red = an::predictPhotometricColorRatios(2.0);
    EXPECT_LT(blue.redToGreen, red.redToGreen);
    EXPECT_GT(blue.blueToGreen, red.blueToGreen);
    EXPECT_DOUBLE_EQ(blue.greenToGreen, 1.0);
    EXPECT_NEAR(red.redToGreen, std::exp(0.32 * 2.0), 1.0e-12);
    EXPECT_NEAR(red.blueToGreen, std::exp(-0.40 * 2.0), 1.0e-12);
    EXPECT_EQ(an::predictPhotometricColorRatios(std::nullopt), an::predictPhotometricColorRatios(0.0));
    EXPECT_EQ(an::predictPhotometricColorRatios(std::numeric_limits<double>::quiet_NaN()),
              an::predictPhotometricColorRatios(0.0));
    EXPECT_EQ(an::predictPhotometricColorRatios(9.0), an::predictPhotometricColorRatios(3.5));
    EXPECT_EQ(an::predictPhotometricColorRatios(-4.0), an::predictPhotometricColorRatios(-0.5));
    const std::array<double, 3u> array = red.asArray();
    EXPECT_DOUBLE_EQ(array[0], red.redToGreen);
    EXPECT_DOUBLE_EQ(array[2], red.blueToGreen);
}

TEST(PhotometricColorCalibrationTest, NarrowbandNamesAreRecognisedWithoutFalsePositives)
{
    EXPECT_TRUE(an::isNarrowbandFilterName("Ha"));
    EXPECT_TRUE(an::isNarrowbandFilterName("  ha  "));
    EXPECT_TRUE(an::isNarrowbandFilterName("Ha 7nm"));
    EXPECT_TRUE(an::isNarrowbandFilterName("H-Alpha"));
    EXPECT_TRUE(an::isNarrowbandFilterName("Halpha"));
    EXPECT_TRUE(an::isNarrowbandFilterName("OIII"));
    EXPECT_TRUE(an::isNarrowbandFilterName("SII"));
    EXPECT_TRUE(an::isNarrowbandFilterName("S-II 3nm"));
    EXPECT_TRUE(an::isNarrowbandFilterName("Narrowband"));
    EXPECT_TRUE(an::isNarrowbandFilterName("Dualband"));
    EXPECT_TRUE(an::isNarrowbandFilterName("Triad Ultra"));
    EXPECT_FALSE(an::isNarrowbandFilterName(""));
    EXPECT_FALSE(an::isNarrowbandFilterName("Luminance"));
    EXPECT_FALSE(an::isNarrowbandFilterName("Sharp"));
    EXPECT_FALSE(an::isNarrowbandFilterName("channel"));
    EXPECT_FALSE(an::isNarrowbandFilterName("Red"));
    EXPECT_FALSE(an::isNarrowbandFilterName("Hα"));
}

TEST(PhotometricColorCalibrationTest, PerfectSamplesFitUnityGainsAndAShiftedRedChannelIsRecovered)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    const an::Result<an::PhotometricColorFitResult> unity =
        an::fitPhotometricColorGains(execution, perfectSamples(1.0, 1.0));
    ASSERT_TRUE(unity.hasValue()) << unity.status().message();
    EXPECT_NEAR(unity->gains[0], 1.0, 1.0e-9);
    EXPECT_DOUBLE_EQ(unity->gains[1], 1.0);
    EXPECT_NEAR(unity->gains[2], 1.0, 1.0e-9);
    EXPECT_EQ(unity->suppliedCount, 12u);
    EXPECT_EQ(unity->usableCount, 12u);
    EXPECT_EQ(unity->coloredCount, 12u);
    EXPECT_TRUE(unity->usedCatalogColorModel);
    EXPECT_NEAR(unity->rmsLogResidual, 0.0, 1.0e-9);
    EXPECT_EQ(unity->usedSampleIndices.size(), 12u);

    const an::Result<an::PhotometricColorFitResult> shifted =
        an::fitPhotometricColorGains(execution, perfectSamples(2.0, 1.0));
    ASSERT_TRUE(shifted.hasValue()) << shifted.status().message();
    EXPECT_NEAR(shifted->gains[0], 0.5, 1.0e-9);
    EXPECT_NEAR(shifted->gains[2], 1.0, 1.0e-9);

    const an::Result<an::PhotometricColorFitResult> neutral =
        an::fitPhotometricColorGains(execution, perfectSamples(1.0, 0.8, false));
    ASSERT_TRUE(neutral.hasValue()) << neutral.status().message();
    EXPECT_FALSE(neutral->usedCatalogColorModel);
    EXPECT_EQ(neutral->coloredCount, 0u);
    EXPECT_NEAR(neutral->gains[2], 1.25, 1.0e-9);
}

TEST(PhotometricColorCalibrationTest, GainFitClipsAnOutlierAndRefusesTooFewSamples)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    std::vector<an::PhotometricColorSample> samples = perfectSamples(1.0, 1.0);
    samples[4].redFlux *= 6.0;
    samples.push_back(an::PhotometricColorSample{.redFlux = 0.0, .greenFlux = 10.0, .blueFlux = 10.0});
    samples.push_back(an::PhotometricColorSample{
        .redFlux = 10.0, .greenFlux = std::numeric_limits<double>::infinity(), .blueFlux = 10.0});

    const an::Result<an::PhotometricColorFitResult> fit = an::fitPhotometricColorGains(execution, samples);
    ASSERT_TRUE(fit.hasValue()) << fit.status().message();
    EXPECT_EQ(fit->suppliedCount, 14u);
    EXPECT_EQ(fit->usableCount, 12u);
    EXPECT_NEAR(fit->gains[0], 1.0, 1.0e-9);
    EXPECT_GT(fit->rmsLogResidual, 0.1);
    EXPECT_EQ(fit->usedSampleIndices.size(), 12u);
    EXPECT_EQ(fit->usedSampleIndices.back(), 11u);

    an::PhotometricColorFitRequest request;
    request.minimumSamples = 13u;
    const an::Result<an::PhotometricColorFitResult> few = an::fitPhotometricColorGains(execution, samples, request);
    ASSERT_FALSE(few.hasValue());
    EXPECT_EQ(few.status().code(), an::ErrorCode::EmptySelection);

    request = {};
    request.clipSigmas = -1.0;
    const an::Result<an::PhotometricColorFitResult> bad = an::fitPhotometricColorGains(execution, samples, request);
    ASSERT_FALSE(bad.hasValue());
    EXPECT_EQ(bad.status().code(), an::ErrorCode::InvalidArgument);

    const an::Result<an::PhotometricColorFitResult> uninitialized =
        an::fitPhotometricColorGains(an::ExecutionContext{}, samples);
    ASSERT_FALSE(uninitialized.hasValue());
    EXPECT_EQ(uninitialized.status().code(), an::ErrorCode::NotInitialized);
}

TEST(PhotometricColorCalibrationTest, OffsetsFollowTheStatedBackgroundMode)
{
    const an::ColorChannelGains gains{2.0, 1.0, 0.5};
    const an::ColorChannelOffsets levels{0.1, 0.2, 0.4};
    const double pedestal = an::meanBackgroundPedestal(levels);
    EXPECT_NEAR(pedestal, 0.7 / 3.0, 1.0e-15);
    const an::ColorChannelOffsets shared =
        an::colorCalibrationOffsets(gains, levels, an::ColorCalibrationBackgroundMode::SharedPedestal, pedestal);
    EXPECT_NEAR(shared[0], pedestal - 0.2, 1.0e-15);
    EXPECT_NEAR(shared[1], pedestal - 0.2, 1.0e-15);
    EXPECT_NEAR(shared[2], pedestal - 0.2, 1.0e-15);
    const an::ColorChannelOffsets zero =
        an::colorCalibrationOffsets(gains, levels, an::ColorCalibrationBackgroundMode::SkyToZero, pedestal);
    EXPECT_NEAR(zero[0], -0.2, 1.0e-15);
    EXPECT_NEAR(zero[2], -0.2, 1.0e-15);
    EXPECT_STREQ(an::colorCalibrationBackgroundModeName(an::ColorCalibrationBackgroundMode::SkyToZero), "SkyToZero");
    EXPECT_STREQ(an::colorCalibrationClippingName(an::ColorCalibrationClipping::RejectOnClip), "RejectOnClip");
    EXPECT_FALSE(an::isColorCalibrationBackgroundMode(static_cast<an::ColorCalibrationBackgroundMode>(5u)));
    EXPECT_FALSE(an::isColorCalibrationClipping(static_cast<an::ColorCalibrationClipping>(3u)));
    EXPECT_EQ(an::checkOperationEffect(an::kColorCalibrationApplyEffect), an::OperationEffectError::None);
    EXPECT_EQ(an::checkOperationEffect(an::kColorBackgroundEstimateEffect), an::OperationEffectError::None);
}

TEST(PhotometricColorCalibrationTest, BackgroundEstimateFindsEachChannelSkyUnderStars)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    ColorFieldRequest request;
    request.width = 256u;
    request.height = 256u;
    request.cell = 48u;
    const ColorField field = makeColorField(request);
    const an::ImageDescriptor descriptor = rgbDescriptor(request.width, request.height);
    const an::ImageLayout layout = planarLayout(descriptor);
    const an::StarFrameView frame = frameOf(field.samples, layout, descriptor);

    const an::Result<an::ColorBackgroundEstimate> estimate = an::estimateColorBackground(execution, frame);
    ASSERT_TRUE(estimate.hasValue()) << estimate.status().message();
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        EXPECT_NEAR(estimate->levels[channel], request.background[channel], 3.0 * request.noise / 10.0);
        EXPECT_NEAR(estimate->medians[channel], request.background[channel], request.noise);
        EXPECT_GT(estimate->robustScales[channel], 0.0);
        EXPECT_GT(estimate->retainedSamples[channel], 0u);
    }

    const an::Result<an::ColorBackgroundEstimate> region = an::estimateColorBackground(
        execution, frame, an::StatisticsRegion{.originX = 0u, .originY = 0u, .width = 40u, .height = 40u});
    ASSERT_TRUE(region.hasValue()) << region.status().message();
    EXPECT_NEAR(region->levels[1], request.background[1], request.noise);

    const an::Result<an::ColorBackgroundEstimate> outside = an::estimateColorBackground(
        execution, frame, an::StatisticsRegion{.originX = 250u, .originY = 0u, .width = 40u, .height = 40u});
    ASSERT_FALSE(outside.hasValue());
    EXPECT_EQ(outside.status().code(), an::ErrorCode::OutOfRange);

    const an::ImageDescriptor mono = rgbDescriptor(8u, 8u, an::SampleType::Float32, 1u);
    const an::ImageLayout monoLayout = planarLayout(mono);
    const std::vector<float> monoSamples(64u, 0.1f);
    const an::Result<an::ColorBackgroundEstimate> refused =
        an::estimateColorBackground(execution, frameOf(monoSamples, monoLayout, mono));
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::Unsupported);
}

TEST(PhotometricColorCalibrationTest, ApplyWritesGainAndOffsetUnderEveryClippingPolicy)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageDescriptor descriptor = rgbDescriptor(6u, 3u);
    const an::ImageLayout layout = planarLayout(descriptor);
    std::vector<float> source(18u * 3u, 0.0f);
    for (std::size_t index = 0u; index < source.size(); ++index)
    {
        source[index] = 0.05f * static_cast<float>(index % 18u);
    }
    source[5] = std::numeric_limits<float>::quiet_NaN();
    const std::optional<an::ValidityDescriptor> finite =
        an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly});
    ASSERT_TRUE(finite.has_value());
    const an::StarFrameView frame = frameOf(source, layout, descriptor, {.descriptor = *finite});

    std::vector<float> destination(source.size(), -7.0f);
    an::ColorCalibrationApplyRequest request;
    request.gains = {2.0, 1.0, 0.5};
    request.offsets = {-0.1, 0.0, 0.2};
    request.clipping = an::ColorCalibrationClipping::ClampToPixelRange;
    const an::Result<an::ColorCalibrationApplyResult> clamped =
        an::applyColorCalibration(execution, frame, destination.data(), destination.size() * sizeof(float), request);
    ASSERT_TRUE(clamped.hasValue()) << clamped.status().message();
    EXPECT_EQ(clamped->samplesWritten, 54u);
    EXPECT_EQ(clamped->samplesCopiedUnchanged, 1u);
    EXPECT_GT(clamped->samplesOutsideInterval, 0u);
    EXPECT_TRUE(std::isnan(destination[5]));
    for (std::size_t index = 0u; index < 18u; ++index)
    {
        if (index == 5u)
        {
            continue;
        }
        const double expected = std::clamp(source[index] * 2.0 - 0.1, 0.0, 1.0);
        EXPECT_NEAR(destination[index], expected, 1.0e-6) << index;
        EXPECT_NEAR(destination[18u + index], source[18u + index], 1.0e-6) << index;
        EXPECT_NEAR(destination[36u + index], std::clamp(source[36u + index] * 0.5 + 0.2, 0.0, 1.0), 1.0e-6) << index;
    }

    request.clipping = an::ColorCalibrationClipping::PreserveSignedFloat;
    const an::Result<an::ColorCalibrationApplyResult> preserved =
        an::applyColorCalibration(execution, frame, destination.data(), destination.size() * sizeof(float), request);
    ASSERT_TRUE(preserved.hasValue()) << preserved.status().message();
    EXPECT_NEAR(destination[0], -0.1, 1.0e-6);
    EXPECT_EQ(preserved->samplesOutsideInterval, clamped->samplesOutsideInterval);

    request.clipping = an::ColorCalibrationClipping::RejectOnClip;
    std::vector<float> untouched(source.size(), 3.0f);
    const an::Result<an::ColorCalibrationApplyResult> rejected =
        an::applyColorCalibration(execution, frame, untouched.data(), untouched.size() * sizeof(float), request);
    ASSERT_FALSE(rejected.hasValue());
    EXPECT_EQ(rejected.status().code(), an::ErrorCode::InvalidData);
    EXPECT_FLOAT_EQ(untouched[0], 3.0f);

    request.gains = {1.0, 1.0, 1.0};
    request.offsets = {0.0, 0.0, 0.0};
    std::vector<float> inPlace(source.size() + 8u, 0.0f);
    std::copy(source.begin(), source.end(), inPlace.begin());
    const an::StarFrameView aliasFrame = frameOf(inPlace, layout, descriptor, {.descriptor = *finite});
    const an::Result<an::ColorCalibrationApplyResult> identity =
        an::applyColorCalibration(execution, aliasFrame, inPlace.data(), inPlace.size() * sizeof(float), request);
    ASSERT_TRUE(identity.hasValue()) << identity.status().message();
    EXPECT_EQ(identity->samplesOutsideInterval, 0u);
    EXPECT_FLOAT_EQ(inPlace[7], source[7]);

    const an::Result<an::ColorCalibrationApplyResult> shifted = an::applyColorCalibration(
        execution, aliasFrame, inPlace.data() + 1, (inPlace.size() - 1u) * sizeof(float), request);
    ASSERT_FALSE(shifted.hasValue());
    EXPECT_EQ(shifted.status().code(), an::ErrorCode::Aliasing);

    const an::Result<an::ColorCalibrationApplyResult> small =
        an::applyColorCalibration(execution, frame, destination.data(), 8u, request);
    ASSERT_FALSE(small.hasValue());
    EXPECT_EQ(small.status().code(), an::ErrorCode::InvalidArgument);

    request.gains = {0.0, 1.0, 1.0};
    const an::Result<an::ColorCalibrationApplyResult> zeroGain =
        an::applyColorCalibration(execution, frame, destination.data(), destination.size() * sizeof(float), request);
    ASSERT_FALSE(zeroGain.hasValue());
    EXPECT_EQ(zeroGain.status().code(), an::ErrorCode::InvalidArgument);

    const an::ImageDescriptor integer = rgbDescriptor(6u, 3u, an::SampleType::UInt16);
    const an::ImageLayout integerLayout = planarLayout(integer);
    const std::vector<std::uint16_t> integerSamples(54u, 1000u);
    request.gains = {1.0, 1.0, 1.0};
    std::vector<std::uint16_t> integerOut(54u, 0u);
    const an::Result<an::ColorCalibrationApplyResult> integerRefused =
        an::applyColorCalibration(execution, frameOf(integerSamples, integerLayout, integer), integerOut.data(),
                                  integerOut.size() * sizeof(std::uint16_t), request);
    ASSERT_FALSE(integerRefused.hasValue());
    EXPECT_EQ(integerRefused.status().code(), an::ErrorCode::Unsupported);
}

TEST(PhotometricColorCalibrationTest, InPlaceBoundaryRowsMatchDisjointCalibration)
{
    const auto engine = makeEngine();
    const auto execution = makeExecution(engine);
    const auto descriptor = rgbDescriptor(3u, 1u);
    const auto layout = planarLayout(descriptor);
    for (const auto clipping :
         {an::ColorCalibrationClipping::ClampToPixelRange, an::ColorCalibrationClipping::PreserveSignedFloat,
          an::ColorCalibrationClipping::RejectOnClip})
    {
        const std::vector<float> source = clipping == an::ColorCalibrationClipping::RejectOnClip
                                              ? std::vector<float>{0.f, .2f, .5f, 0.f, .2f, .5f, 0.f, .2f, .5f}
                                              : std::vector<float>{-.2f, .2f, .6f, -.2f, .2f, .6f, -.2f, .2f, .6f};
        std::vector<float> inplace = source, destination(source.size());
        const an::ColorCalibrationApplyRequest request{.gains = {2., 2., 2.}, .clipping = clipping};
        const auto disjoint =
            an::applyColorCalibration(execution, frameOf(source, layout, descriptor), destination.data(),
                                      destination.size() * sizeof(float), request);
        const auto aliased = an::applyColorCalibration(execution, frameOf(inplace, layout, descriptor), inplace.data(),
                                                       inplace.size() * sizeof(float), request);
        ASSERT_TRUE(disjoint.hasValue());
        ASSERT_TRUE(aliased.hasValue());
        EXPECT_EQ(inplace, destination);
        EXPECT_EQ(aliased->samplesOutsideInterval, disjoint->samplesOutsideInterval);
    }
}

// A disjoint row is written once and only a row whose written samples reached an end of the interval
// is counted and clamped again, so a row that leaves the interval at one end alone must still reach
// that exact pass - at either end, under every policy that writes.
TEST(PhotometricColorCalibrationTest, ARowThatLeavesTheIntervalAtOneEndAloneIsCountedAndHeld)
{
    const auto engine = makeEngine();
    const auto execution = makeExecution(engine);
    const auto descriptor = rgbDescriptor(4u, 1u);
    const auto layout = planarLayout(descriptor);
    const std::vector<float> overTheTop{.2f, .3f, .6f, .4f, .2f, .3f, .6f, .4f, .2f, .3f, .6f, .4f};
    const std::vector<float> underTheBottom{.2f, .3f, -.1f, .4f, .2f, .3f, -.1f, .4f, .2f, .3f, -.1f, .4f};
    for (const auto clipping :
         {an::ColorCalibrationClipping::ClampToPixelRange, an::ColorCalibrationClipping::PreserveSignedFloat})
    {
        for (const std::vector<float>* source : {&overTheTop, &underTheBottom})
        {
            std::vector<float> destination(source->size(), -9.0f);
            const an::ColorCalibrationApplyRequest request{.gains = {2., 2., 2.}, .clipping = clipping};
            const auto applied =
                an::applyColorCalibration(execution, frameOf(*source, layout, descriptor), destination.data(),
                                          destination.size() * sizeof(float), request);
            ASSERT_TRUE(applied.hasValue()) << applied.status().message();
            EXPECT_EQ(applied->samplesOutsideInterval, 3u);
            const bool top = source == &overTheTop;
            const float held = clipping == an::ColorCalibrationClipping::ClampToPixelRange ? (top ? 1.0f : 0.0f)
                                                                                           : (top ? 1.2f : -0.2f);
            for (std::size_t channel = 0u; channel < 3u; ++channel)
            {
                EXPECT_FLOAT_EQ(destination[channel * 4u + 2u], held) << (top ? "top" : "bottom");
                EXPECT_FLOAT_EQ(destination[channel * 4u + 0u], .4f);
                EXPECT_FLOAT_EQ(destination[channel * 4u + 3u], .8f);
            }
        }
    }
}

TEST(PhotometricColorCalibrationTest, ApplyIsBitIdenticalAcrossThreadCountsAndInFloat64)
{
    const an::EngineContext engine = makeEngine(8u);
    const an::ImageDescriptor descriptor = rgbDescriptor(300u, 90u, an::SampleType::Float64);
    const an::ImageLayout layout = planarLayout(descriptor);
    std::vector<double> source(300u * 90u * 3u);
    for (std::size_t index = 0u; index < source.size(); ++index)
    {
        source[index] = 0.3 + 0.2 * std::sin(0.01 * static_cast<double>(index));
    }
    const an::StarFrameView frame = frameOf(source, layout, descriptor);
    an::ColorCalibrationApplyRequest request;
    request.gains = {1.7, 1.0, 0.9};
    request.offsets = {-0.05, 0.02, 0.01};

    std::vector<double> one(source.size(), 0.0);
    std::vector<double> many(source.size(), 0.0);
    const an::ExecutionContext single = makeExecution(engine, 1u);
    const an::ExecutionContext wide = makeExecution(engine, 6u);
    ASSERT_TRUE(an::applyColorCalibration(single, frame, one.data(), one.size() * sizeof(double), request).hasValue());
    ASSERT_TRUE(an::applyColorCalibration(wide, frame, many.data(), many.size() * sizeof(double), request).hasValue());
    EXPECT_EQ(one, many);
    EXPECT_NEAR(one[10], source[10] * 1.7 - 0.05, 1.0e-15);
}

/// Whether two samples are the same: the same bits, or both not-a-number.
template <typename SampleT> [[nodiscard]] bool sameSample(const SampleT left, const SampleT right)
{
    return (std::isnan(left) && std::isnan(right)) || std::memcmp(&left, &right, sizeof(SampleT)) == 0;
}

/// Applies two requests under both writing policies to frames of every row width from one to past two
/// vector registers, disjoint and in place, and holds each written sample and the count to the widened
/// arithmetic worked out here one sample at a time.
template <typename SampleT> void expectWidenedApplyAtEveryRowWidth(const an::SampleType sampleType)
{
    const auto engine = makeEngine(4u);
    const auto execution = makeExecution(engine, 3u);
    const SampleT one{1};
    const SampleT specials[] = {std::numeric_limits<SampleT>::quiet_NaN(), std::numeric_limits<SampleT>::infinity(),
                                -std::numeric_limits<SampleT>::infinity(), std::numeric_limits<SampleT>::denorm_min(),
                                -std::numeric_limits<SampleT>::denorm_min()};
    // Gains single precision cannot hold; a gain a hair above one, which carries a sample of exactly one to a
    // value past the interval that Float32 still rounds to one; and an offset below zero that Float32 rounds
    // to negative zero.
    const an::ColorCalibrationApplyRequest requests[] = {
        {.gains = {0.64, 0.8, 1.0 + std::ldexp(1.0, -30)}, .offsets = {0.0, 0.01, 0.0}},
        {.gains = {1.0 / 3.0, 1.25, 0.999}, .offsets = {-1.0e-50, -0.1, 1.0e-3}}};
    std::uint64_t state = 0x9E3779B97F4A7C15ull;
    for (const std::uint32_t width : {1u,  2u,  3u,  4u,  5u,  6u,  7u,  8u,  9u,  10u, 11u, 12u, 13u, 14u,
                                      15u, 16u, 17u, 18u, 19u, 20u, 23u, 24u, 25u, 31u, 32u, 33u, 67u, 257u})
    {
        const auto descriptor = rgbDescriptor(width, 2u, sampleType);
        const auto layout = planarLayout(descriptor);
        std::vector<SampleT> source(std::size_t{width} * 2u * 3u);
        for (SampleT& sample : source)
        {
            state = state * 6364136223846793005ull + 1442695040888963407ull;
            const std::uint64_t draw = state >> 33u;
            switch (draw % 16u)
            {
            case 10u:
                sample = one;
                break;
            case 11u:
                sample = SampleT{0};
                break;
            case 12u:
                sample = -SampleT{0};
                break;
            case 13u:
                sample = std::nextafter(one, SampleT{2});
                break;
            case 14u:
                sample = std::nextafter(one, SampleT{0});
                break;
            case 15u:
                sample = specials[(draw >> 4u) % 5u];
                break;
            default:
                sample = static_cast<SampleT>(-0.25 + 1.5 * static_cast<double>(draw >> 8u) / 8388608.0);
                break;
            }
        }
        for (const an::ColorCalibrationApplyRequest& base : requests)
        {
            for (const auto clipping :
                 {an::ColorCalibrationClipping::ClampToPixelRange, an::ColorCalibrationClipping::PreserveSignedFloat})
            {
                an::ColorCalibrationApplyRequest request = base;
                request.clipping = clipping;
                std::vector<SampleT> expected(source.size());
                std::uint64_t outside = 0u;
                for (std::size_t index = 0u; index < source.size(); ++index)
                {
                    const std::size_t channel = index / (std::size_t{width} * 2u);
                    double value =
                        static_cast<double>(source[index]) * request.gains[channel] + request.offsets[channel];
                    outside += static_cast<std::uint64_t>(value < 0.0) + static_cast<std::uint64_t>(value > 1.0);
                    if (clipping == an::ColorCalibrationClipping::ClampToPixelRange)
                    {
                        value = value < 0.0 ? 0.0 : (value > 1.0 ? 1.0 : value);
                    }
                    expected[index] = static_cast<SampleT>(value);
                }
                // Inside the interval, so a sample the apply failed to write cannot send its row to the
                // exact pass that would write it.
                std::vector<SampleT> destination(source.size(), static_cast<SampleT>(0.5));
                std::vector<SampleT> inPlace = source;
                const auto disjoint =
                    an::applyColorCalibration(execution, frameOf(source, layout, descriptor), destination.data(),
                                              destination.size() * sizeof(SampleT), request);
                const auto aliased =
                    an::applyColorCalibration(execution, frameOf(inPlace, layout, descriptor), inPlace.data(),
                                              inPlace.size() * sizeof(SampleT), request);
                ASSERT_TRUE(disjoint.hasValue()) << disjoint.status().message();
                ASSERT_TRUE(aliased.hasValue()) << aliased.status().message();
                EXPECT_EQ(disjoint->samplesWritten, source.size());
                EXPECT_EQ(disjoint->samplesCopiedUnchanged, 0u);
                EXPECT_EQ(disjoint->samplesOutsideInterval, outside) << "width " << width;
                EXPECT_EQ(aliased->samplesOutsideInterval, outside) << "width " << width;
                for (std::size_t index = 0u; index < source.size(); ++index)
                {
                    EXPECT_TRUE(sameSample(destination[index], expected[index]))
                        << "width " << width << " sample " << index << " wrote " << destination[index] << " for "
                        << source[index] << ", expected " << expected[index];
                    EXPECT_TRUE(sameSample(inPlace[index], expected[index]))
                        << "in place, width " << width << " sample " << index;
                }
            }
        }
    }
}

// The apply's arithmetic is widened: each sample is `sample * gain + offset` in double precision, rounded once
// to the frame's representation, and it is outside the interval by that widened value rather than by the one
// stored. Rows narrower and wider than a vector register, gains single precision cannot hold, samples that land
// on or just past an end, and the non-finite values all have to agree with that, in either representation.
TEST(PhotometricColorCalibrationTest, ApplyWritesTheWidenedValueAndCountsItsEndsAtEveryRowWidth)
{
    expectWidenedApplyAtEveryRowWidth<float>(an::SampleType::Float32);
    expectWidenedApplyAtEveryRowWidth<double>(an::SampleType::Float64);
}

TEST(PhotometricColorCalibrationTest, CalibratedSemanticsSetOnlyTheColorCalibratedFlag)
{
    an::ImageSemanticsRequest request;
    request.transferFunction = an::TransferFunction::Linear;
    request.processingState.flatFielded = true;
    const an::Result<an::ImageSemantics> input = an::ImageSemantics::create(request);
    ASSERT_TRUE(input.hasValue());
    const an::Result<an::ImageSemantics> output = an::colorCalibratedSemantics(*input);
    ASSERT_TRUE(output.hasValue()) << output.status().message();
    EXPECT_TRUE(output->processingState().colorCalibrated);
    EXPECT_TRUE(output->processingState().flatFielded);
    EXPECT_FALSE(output->processingState().registered);
    EXPECT_EQ(output->transferFunction(), an::TransferFunction::Linear);

    const an::Result<an::MetadataTransition> transition =
        an::operationTransition(an::kColorCalibrationApplyEffect,
                                an::OperationOutcome{.operation = "an::applyColorCalibration",
                                                     .detail = an::kPhotometricColorCalibrationAlgorithmVersion,
                                                     .semantics = *output});
    ASSERT_TRUE(transition.hasValue()) << transition.status().message();
    EXPECT_TRUE(an::changesMeaning(*transition));
}

TEST(PhotometricColorCalibrationTest, NestedDetectionReportsOnTheCalibrationScale)
{
    class Sink final : public an::ProgressSink
    {
    public:
        void onProgress(const an::ProgressReport& report) noexcept override
        {
            if (reportCount < reports.size())
            {
                reports[reportCount++] = report.total * 100u + report.completed;
            }
            wrongTotal = wrongTotal || report.total != 6u;
            if (report.completed < stages.size())
            {
                stages[report.completed] = true;
            }
        }
        bool wrongTotal = false;
        std::array<bool, 7u> stages{};
        std::size_t reportCount = 0u;
        std::array<std::uint64_t, 32u> reports{};
    } sink;
    const auto engine = makeEngine();
    an::ExecutionOptions options;
    options.maxConcurrency = 1u;
    options.progressSink = &sink;
    const auto execution = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(execution.hasValue());
    const ColorFieldRequest request;
    const auto field = makeColorField(request);
    const auto descriptor = rgbDescriptor(request.width, request.height);
    const auto layout = planarLayout(descriptor);
    const auto solution = makeSolution(request.width, request.height);
    const auto catalog = buildOrFail(*execution, catalogEntriesOf(field, solution));
    const auto colors = colorIndicesOf(field);
    std::vector<float> destination(field.samples.size());
    an::StarDetectionRequest detection;
    detection.relaxedChecks = true;
    const auto result = an::calibratePhotometricColor(
        *execution, frameOf(field.samples, layout, descriptor), destination.data(), destination.size() * sizeof(float),
        solution, {.catalog = &catalog, .colorIndexBpMinusRp = colors}, detection, {});
    ASSERT_TRUE(result.hasValue()) << result.status().message();
    EXPECT_FALSE(sink.wrongTotal) << ::testing::PrintToString(sink.reports);
    for (const bool delivered : sink.stages)
    {
        EXPECT_TRUE(delivered);
    }
    EXPECT_EQ(execution->operationCounters().discardedProgressReports, 0u);
}

TEST(PhotometricColorCalibrationTest, WholeCalibrationRecoversPlantedChannelFactorsAndNeutralizesTheSky)
{
    const an::EngineContext engine = makeEngine(8u);
    const an::ExecutionContext execution = makeExecution(engine, 6u);
    ColorFieldRequest fieldRequest;
    fieldRequest.redFactor = 1.6;
    fieldRequest.blueFactor = 0.7;
    fieldRequest.everyStarColored = false;
    const ColorField field = makeColorField(fieldRequest);
    ASSERT_GE(field.stars.size(), 30u);
    const an::ImageDescriptor descriptor = rgbDescriptor(fieldRequest.width, fieldRequest.height);
    const an::ImageLayout layout = planarLayout(descriptor);
    const an::StarFrameView frame = frameOf(field.samples, layout, descriptor);
    const an::WcsSolution solution = makeSolution(fieldRequest.width, fieldRequest.height);
    const an::SkyCatalog catalog = buildOrFail(execution, catalogEntriesOf(field, solution));
    const std::vector<std::optional<double>> colors = colorIndicesOf(field);
    const an::PhotometricReferenceCatalog reference{.catalog = &catalog, .colorIndexBpMinusRp = colors};

    std::vector<float> destination(field.samples.size(), 0.0f);
    an::PhotometricColorCalibrationRequest request;
    an::StarDetectionRequest detection;
    detection.relaxedChecks = true;
    const an::Result<an::PhotometricColorCalibrationResult> calibrated =
        an::calibratePhotometricColor(execution, frame, destination.data(), destination.size() * sizeof(float),
                                      solution, reference, detection, request);
    ASSERT_TRUE(calibrated.hasValue()) << calibrated.status().message();

    EXPECT_GE(calibrated->detectedStars[1], 30u);
    EXPECT_GE(calibrated->crossMatchedStars, 30u);
    EXPECT_GE(calibrated->fit.usableCount, 30u);
    EXPECT_TRUE(calibrated->fit.usedCatalogColorModel);
    EXPECT_NEAR(calibrated->fit.gains[0], 1.0 / fieldRequest.redFactor, 0.03);
    EXPECT_DOUBLE_EQ(calibrated->fit.gains[1], 1.0);
    EXPECT_NEAR(calibrated->fit.gains[2], 1.0 / fieldRequest.blueFactor, 0.03);
    EXPECT_LT(calibrated->fit.rmsLogResidual, 0.08);
    ASSERT_TRUE(calibrated->background.has_value());
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        EXPECT_NEAR(calibrated->background->levels[channel], fieldRequest.background[channel], 0.001);
    }
    EXPECT_EQ(calibrated->apply.samplesWritten, destination.size());

    const std::array<double, 3u> skies{
        meanOver(destination, fieldRequest.width, fieldRequest.height, 0u, 4u, 4u, 24u, 24u),
        meanOver(destination, fieldRequest.width, fieldRequest.height, 1u, 4u, 4u, 24u, 24u),
        meanOver(destination, fieldRequest.width, fieldRequest.height, 2u, 4u, 4u, 24u, 24u)};
    const double pedestal = an::meanBackgroundPedestal(calibrated->background->levels);
    EXPECT_NEAR(skies[0], pedestal, 0.002);
    EXPECT_NEAR(skies[1], pedestal, 0.002);
    EXPECT_NEAR(skies[2], pedestal, 0.002);

    const std::size_t planeSize = static_cast<std::size_t>(fieldRequest.width) * fieldRequest.height;
    const std::size_t at = static_cast<std::size_t>(200u) * fieldRequest.width + 200u;
    EXPECT_NEAR(destination[at], field.samples[at] * calibrated->fit.gains[0] + calibrated->offsets[0], 1.0e-6);
    EXPECT_NEAR(destination[planeSize + at], field.samples[planeSize + at] + calibrated->offsets[1], 1.0e-6);

    std::vector<float> alone(field.samples.size(), 0.0f);
    request.background.neutralize = false;
    request.clipping = an::ColorCalibrationClipping::PreserveSignedFloat;
    const an::Result<an::PhotometricColorCalibrationResult> plain = an::calibratePhotometricColor(
        execution, frame, alone.data(), alone.size() * sizeof(float), solution, reference, detection, request);
    ASSERT_TRUE(plain.hasValue()) << plain.status().message();
    EXPECT_FALSE(plain->background.has_value());
    EXPECT_DOUBLE_EQ(plain->offsets[0], 0.0);
    EXPECT_NEAR(plain->fit.gains[0], calibrated->fit.gains[0], 1.0e-12);
    const auto physical = rgbDescriptor(fieldRequest.width, fieldRequest.height, an::SampleType::Float32, 3u,
                                        an::NumericDomain::PhysicalScaled, an::ColorSpace::LinearRgb);
    const auto physicalResult =
        an::calibratePhotometricColor(execution, frameOf(field.samples, layout, physical), destination.data(),
                                      destination.size() * sizeof(float), solution, reference, detection, request);
    ASSERT_TRUE(physicalResult.hasValue()) << physicalResult.status().message();
    EXPECT_EQ(destination, alone);
}

TEST(PhotometricColorCalibrationTest, WholeCalibrationRefusesWhatItCannotCalibrate)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    ColorFieldRequest fieldRequest;
    fieldRequest.width = 256u;
    fieldRequest.height = 256u;
    const ColorField field = makeColorField(fieldRequest);
    const an::ImageDescriptor descriptor = rgbDescriptor(fieldRequest.width, fieldRequest.height);
    const an::ImageLayout layout = planarLayout(descriptor);
    const an::StarFrameView frame = frameOf(field.samples, layout, descriptor);
    const an::WcsSolution solution = makeSolution(fieldRequest.width, fieldRequest.height);
    const an::SkyCatalog catalog = buildOrFail(execution, catalogEntriesOf(field, solution));
    const std::vector<std::optional<double>> colors = colorIndicesOf(field);
    std::vector<float> destination(field.samples.size(), 0.0f);
    const std::uint64_t bytes = destination.size() * sizeof(float);

    const an::StarDetectionRequest detection{};
    an::PhotometricColorCalibrationRequest request;
    request.filterName = "Ha 7nm";
    const an::Result<an::PhotometricColorCalibrationResult> narrowband =
        an::calibratePhotometricColor(execution, frame, destination.data(), bytes, solution,
                                      {.catalog = &catalog, .colorIndexBpMinusRp = colors}, detection, request);
    ASSERT_FALSE(narrowband.hasValue());
    EXPECT_EQ(narrowband.status().code(), an::ErrorCode::Unsupported);

    request = {};
    const std::vector<std::optional<double>> misaligned(3u, std::optional<double>{0.5});
    const an::Result<an::PhotometricColorCalibrationResult> aligned =
        an::calibratePhotometricColor(execution, frame, destination.data(), bytes, solution,
                                      {.catalog = &catalog, .colorIndexBpMinusRp = misaligned}, detection, request);
    ASSERT_FALSE(aligned.hasValue());
    EXPECT_EQ(aligned.status().code(), an::ErrorCode::InvalidArgument);

    const an::Result<an::PhotometricColorCalibrationResult> noCatalog =
        an::calibratePhotometricColor(execution, frame, destination.data(), bytes, solution, {}, detection, request);
    ASSERT_FALSE(noCatalog.hasValue());
    EXPECT_EQ(noCatalog.status().code(), an::ErrorCode::NotInitialized);

    const an::ImageDescriptor display =
        rgbDescriptor(fieldRequest.width, fieldRequest.height, an::SampleType::Float32, 3u,
                      an::NumericDomain::NormalizedLinear, an::ColorSpace::DisplayRgb);
    const an::Result<an::PhotometricColorCalibrationResult> displayRefused = an::calibratePhotometricColor(
        execution, frameOf(field.samples, layout, display), destination.data(), bytes, solution,
        {.catalog = &catalog, .colorIndexBpMinusRp = colors}, detection, request);
    ASSERT_FALSE(displayRefused.hasValue());
    EXPECT_EQ(displayRefused.status().code(), an::ErrorCode::Unsupported);

    const auto nonlinear = rgbDescriptor(fieldRequest.width, fieldRequest.height, an::SampleType::Float32, 3u,
                                         an::NumericDomain::EncodedNonlinear, an::ColorSpace::LinearRgb);
    const auto invalidFrame = an::StarFrameView::create(field.samples.data(), bytes, layout, nonlinear);
    EXPECT_EQ(invalidFrame.code(), an::ErrorCode::Unsupported);
    request.channelMatchRadiusPixels = 0.0;
    const an::Result<an::PhotometricColorCalibrationResult> badRadius =
        an::calibratePhotometricColor(execution, frame, destination.data(), bytes, solution,
                                      {.catalog = &catalog, .colorIndexBpMinusRp = colors}, detection, request);
    ASSERT_FALSE(badRadius.hasValue());
    EXPECT_EQ(badRadius.status().code(), an::ErrorCode::InvalidArgument);

    request = {};
    request.fit.minimumSamples = 100000u;
    const an::Result<an::PhotometricColorCalibrationResult> tooFew =
        an::calibratePhotometricColor(execution, frame, destination.data(), bytes, solution,
                                      {.catalog = &catalog, .colorIndexBpMinusRp = colors}, detection, request);
    ASSERT_FALSE(tooFew.hasValue());
    EXPECT_EQ(tooFew.status().code(), an::ErrorCode::EmptySelection);

    std::stop_source stop;
    stop.request_stop();
    const an::ExecutionContext cancelled = makeExecution(engine, 2u, stop.get_token());
    const an::Result<an::PhotometricColorCalibrationResult> stopped =
        an::calibratePhotometricColor(cancelled, frame, destination.data(), bytes, solution,
                                      {.catalog = &catalog, .colorIndexBpMinusRp = colors}, detection);
    ASSERT_FALSE(stopped.hasValue());
    EXPECT_EQ(stopped.status().code(), an::ErrorCode::Cancelled);
    EXPECT_FLOAT_EQ(destination[0], 0.0f);

    const std::vector<an::GaiaStarRecord> records{
        an::GaiaStarRecord{.magnitudeBp = 12.5, .magnitudeRp = 11.0},
        an::GaiaStarRecord{.magnitudeBp = 12.5},
    };
    const std::vector<std::optional<double>> indices = an::gaiaColorIndices(records);
    ASSERT_EQ(indices.size(), 2u);
    ASSERT_TRUE(indices[0].has_value());
    EXPECT_NEAR(*indices[0], 1.5, 1.0e-12);
    EXPECT_FALSE(indices[1].has_value());
}

} // namespace

TEST(PhotometricColorCalibrationTest, ApplyPreservesZeroNoDataAndRefusesValueRange)
{
    const auto engine = makeEngine();
    const auto execution = makeExecution(engine);
    const auto verify = [&]<typename SampleT>(an::SampleType type) {
        const auto descriptor = rgbDescriptor(3u, 1u, type);
        const auto layout = planarLayout(descriptor);
        const std::vector<SampleT> source{0, SampleT{0.25}, SampleT{0.125}, 0, SampleT{0.25}, SampleT{0.125},
                                          0, SampleT{0.25}, SampleT{0.125}};
        const auto zero = an::ValidityDescriptor::create({.rule = an::ValidityRule::ZeroIsNoData});
        const auto frame = frameOf(source, layout, descriptor, {.descriptor = *zero});
        for (const auto clipping :
             {an::ColorCalibrationClipping::ClampToPixelRange, an::ColorCalibrationClipping::PreserveSignedFloat,
              an::ColorCalibrationClipping::RejectOnClip})
        {
            an::ColorCalibrationApplyRequest request;
            request.offsets = {-0.25, -0.25, -0.25};
            request.clipping = clipping;
            if (clipping == an::ColorCalibrationClipping::RejectOnClip)
            {
                request.offsets = {-0.125, -0.125, -0.125};
            }
            auto destination = source;
            const auto result =
                an::applyColorCalibration(execution, frame, destination.data(), layout.totalBytes(), request);
            ASSERT_TRUE(result.hasValue()) << result.status().message();
            for (std::size_t i = 0u; i < source.size(); ++i)
            {
                EXPECT_EQ(an::sampleIsValidByValue(*zero, destination[i]), an::sampleIsValidByValue(*zero, source[i]));
                if (source[i] == SampleT{0})
                {
                    EXPECT_EQ(destination[i], SampleT{0});
                }
            }
            const auto inplace = frameOf(destination, layout, descriptor, {.descriptor = *zero});
            ASSERT_TRUE(an::applyColorCalibration(execution, inplace, destination.data(), layout.totalBytes(),
                                                  {.offsets = {0.0, 0.0, 0.0}})
                            .hasValue());
        }
        const auto range = an::ValidityDescriptor::create(
            {.rule = an::ValidityRule::DomainRange, .lowestValid = 0.0, .highestValid = 0.25});
        const auto ranged = frameOf(source, layout, descriptor, {.descriptor = *range});
        auto unchanged = source;
        EXPECT_EQ(an::applyColorCalibration(execution, ranged, unchanged.data(), layout.totalBytes(), {}).code(),
                  an::ErrorCode::Unsupported);
        EXPECT_EQ(unchanged, source);
    };
    verify.operator()<float>(an::SampleType::Float32);
    verify.operator()<double>(an::SampleType::Float64);
}

TEST(PhotometricColorCalibrationTest, ApplyRefusesMisalignedDestinationBeforeWriting)
{
    const auto engine = makeEngine();
    const auto execution = makeExecution(engine);
    for (const auto type : {an::SampleType::Float32, an::SampleType::Float64})
    {
        const auto descriptor = rgbDescriptor(8u, 8u, type);
        const auto layout = planarLayout(descriptor);
        std::vector<double> source(layout.totalBytes() / sizeof(double) + 1u, 0.0);
        std::vector<double> destination(source.size() + 1u, 1.0);
        const auto before = destination;
        const auto frame = an::StarFrameView::create(source.data(), layout.totalBytes(), layout, descriptor);
        ASSERT_TRUE(frame.hasValue());
        auto* misaligned = reinterpret_cast<std::byte*>(destination.data()) + 1u;
        const auto refused = an::applyColorCalibration(execution, *frame, misaligned, layout.totalBytes(), {});
        EXPECT_EQ(refused.code(), an::ErrorCode::IncompatibleArguments);
        EXPECT_EQ(destination, before);
    }
}

TEST(PhotometricColorCalibrationTest, TransitionRecordsTheRecipeAndCoefficients)
{
    const auto input = an::ImageSemantics::create(an::ImageSemanticsRequest{});
    ASSERT_TRUE(input.hasValue());
    an::PhotometricColorCalibrationResult result;
    result.fit.gains = {1.25, 1.0, 0.75};
    result.offsets = {0.01, 0.0, -0.02};
    const auto transition = an::photometricCalibrationTransition(*input, result);
    ASSERT_TRUE(transition.hasValue()) << transition.status().message();
    ASSERT_TRUE(transition->record.has_value());
    EXPECT_NE(transition->record->detail().find(an::kPhotometricColorCalibrationAlgorithmVersion), std::string::npos);
    EXPECT_NE(transition->record->detail().find("gains=1.25,1,0.75"), std::string::npos);
    EXPECT_NE(transition->record->detail().find("offsets=0.01,0,-0.02"), std::string::npos);
    ASSERT_TRUE(transition->semantics.has_value());
    EXPECT_TRUE(transition->semantics->processingState().colorCalibrated);
    if constexpr (an::test::HeapFailure::kAvailable)
    {
        an::Status status;
        std::size_t refused = 0u;
        {
            an::test::HeapFailure heap(0u);
            status = an::photometricCalibrationTransition(*input, result).status();
            refused = heap.refused();
        }
        EXPECT_GT(refused, 0u);
        EXPECT_EQ(status.code(), an::ErrorCode::AllocationFailed);
    }
}
