// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Algorithms/Demosaic/CfaWhiteBalance.h.
//
// Three questions, and each is answered against something written out here rather than read back from
// the operation: which gains a mode resolves to (against the record's own numbers), what a grey-world
// walk sums (against an independent block walk over the same samples), and what a multiplication stores
// (against the product rounded by the stated rule). The last case measures how far the composition sits
// from a single-precision, truncating load, because that distance is a bound a caller can rely on and a
// number is better evidence of it than a sentence.

#include <AfterNightEngine/Algorithms/Demosaic/CfaWhiteBalance.h>

#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "SpatialFilterFixture.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <vector>

namespace
{

using spatial_test::makeEngine;
using spatial_test::makeExecution;
using spatial_test::planarLayout;

/// One real camera table.
constexpr an::CfaColorEntries kCameraTable = {
    an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Red,   an::CfaColor::Green, an::CfaColor::Green,
    an::CfaColor::Blue,  an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Blue,  an::CfaColor::Green,
    an::CfaColor::Green, an::CfaColor::Red,   an::CfaColor::Blue,  an::CfaColor::Red,   an::CfaColor::Green,
    an::CfaColor::Red,   an::CfaColor::Blue,  an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Green,
    an::CfaColor::Blue,  an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Red,   an::CfaColor::Green,
    an::CfaColor::Green, an::CfaColor::Red,   an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Blue,
    an::CfaColor::Red,   an::CfaColor::Blue,  an::CfaColor::Green, an::CfaColor::Blue,  an::CfaColor::Red,
    an::CfaColor::Green};

/// A camera record with every multiplier distinct, so a swapped filter shows.
[[nodiscard]] an::RawCameraMetadata cameraRecord()
{
    an::RawCameraMetadata camera;
    camera.blackLevel = 100u;
    camera.channelBlack[0] = 4u;
    camera.channelBlack[1] = 0u;
    camera.channelBlack[2] = 2u;
    camera.channelBlack[3] = 0u;
    camera.whiteLevel = 4000u;
    camera.cameraMultipliers[0] = 2.4;
    camera.cameraMultipliers[1] = 1.0;
    camera.cameraMultipliers[2] = 1.2;
    camera.cameraMultipliers[3] = 1.0;
    camera.daylightMultipliers[0] = 2.0;
    camera.daylightMultipliers[1] = 1.0;
    camera.daylightMultipliers[2] = 1.5;
    camera.daylightMultipliers[3] = 1.0;
    return camera;
}

/// A named mosaic.
[[nodiscard]] an::CfaDescriptor bayer(an::CfaPattern pattern, std::uint32_t originX = 0u, std::uint32_t originY = 0u)
{
    const std::optional<an::CfaDescriptor> cfa = an::CfaDescriptor::create(pattern, originX, originY);
    EXPECT_TRUE(cfa.has_value());
    return cfa.value_or(an::CfaDescriptor{});
}

/// The value one filter colour takes in a set.
[[nodiscard]] double valueFor(const an::CfaChannelValues& values, an::CfaColor colour)
{
    switch (colour)
    {
    case an::CfaColor::Red:
        return values.red;
    case an::CfaColor::Green:
        return values.green;
    case an::CfaColor::Blue:
        return values.blue;
    }
    return 0.0;
}

/// A grey-world walk written out independently of the engine: blocks from the top-left, a block with
/// a sample above its filter's level left out whole.
[[nodiscard]] an::CfaGreyWorld walkGreyWorld(const std::vector<double>& samples, std::uint32_t width,
                                             std::uint32_t height, const an::CfaDescriptor& cfa,
                                             std::uint32_t blockSize, const an::CfaChannelValues& clippedAbove)
{
    an::CfaGreyWorld walked;
    for (std::uint32_t blockY = 0u; blockY < height; blockY += blockSize)
    {
        for (std::uint32_t blockX = 0u; blockX < width; blockX += blockSize)
        {
            an::CfaGreyWorld block;
            bool clipped = false;
            for (std::uint32_t y = blockY; y < (std::min)(height, blockY + blockSize) && !clipped; ++y)
            {
                for (std::uint32_t x = blockX; x < (std::min)(width, blockX + blockSize); ++x)
                {
                    const double value = samples[static_cast<std::size_t>(y) * width + x];
                    const an::CfaColor colour = cfa.colorAt(x, y);
                    if (!std::isfinite(value) || value > valueFor(clippedAbove, colour))
                    {
                        clipped = true;
                        break;
                    }
                    switch (colour)
                    {
                    case an::CfaColor::Red:
                        block.sums.red += value;
                        ++block.redSamples;
                        break;
                    case an::CfaColor::Green:
                        block.sums.green += value;
                        ++block.greenSamples;
                        break;
                    case an::CfaColor::Blue:
                        block.sums.blue += value;
                        ++block.blueSamples;
                        break;
                    }
                }
            }
            if (clipped)
            {
                ++walked.blocksSkipped;
                continue;
            }
            ++walked.blocksMeasured;
            walked.sums.red += block.sums.red;
            walked.sums.green += block.sums.green;
            walked.sums.blue += block.sums.blue;
            walked.redSamples += block.redSamples;
            walked.greenSamples += block.greenSamples;
            walked.blueSamples += block.blueSamples;
        }
    }
    return walked;
}

void expectSameMeasurement(const an::CfaGreyWorld& actual, const an::CfaGreyWorld& expected)
{
    EXPECT_DOUBLE_EQ(actual.sums.red, expected.sums.red);
    EXPECT_DOUBLE_EQ(actual.sums.green, expected.sums.green);
    EXPECT_DOUBLE_EQ(actual.sums.blue, expected.sums.blue);
    EXPECT_EQ(actual.redSamples, expected.redSamples);
    EXPECT_EQ(actual.greenSamples, expected.greenSamples);
    EXPECT_EQ(actual.blueSamples, expected.blueSamples);
    EXPECT_EQ(actual.blocksMeasured, expected.blocksMeasured);
    EXPECT_EQ(actual.blocksSkipped, expected.blocksSkipped);
}

/// A single-precision, truncating load of one mosaic sample: a single-precision gain divided by the
/// smallest, a double-precision stretch folded into a single-precision scale, a single-precision product
/// and a truncating cast clamped to sixteen bits.
[[nodiscard]] std::uint16_t truncatingScaled(std::uint16_t code, float gain, float smallestGain, double whiteSpan)
{
    const float normalized = gain / smallestGain;
    const auto scale = static_cast<float>(static_cast<double>(normalized) * (65535.0 / whiteSpan));
    const float product = scale * static_cast<float>(code);
    return static_cast<std::uint16_t>(std::clamp(static_cast<int>(product), 0, 65535));
}

TEST(CfaWhiteBalanceTest, EveryModeIsKnownByName)
{
    for (const an::CfaWhiteBalanceMode mode :
         {an::CfaWhiteBalanceMode::None, an::CfaWhiteBalanceMode::Daylight, an::CfaWhiteBalanceMode::Camera,
          an::CfaWhiteBalanceMode::Automatic, an::CfaWhiteBalanceMode::Custom})
    {
        EXPECT_TRUE(an::isCfaWhiteBalanceMode(mode));
    }
    EXPECT_FALSE(an::isCfaWhiteBalanceMode(static_cast<an::CfaWhiteBalanceMode>(5u)));
}

TEST(CfaWhiteBalanceTest, EachModeResolvesTheGainsItsRecordStates)
{
    const an::RawCameraMetadata camera = cameraRecord();
    // The common black is the record's black plus its smallest channel offset: 100 + 0.
    const double stretch = 65535.0 / (4000.0 - 100.0);

    const an::Result<an::CfaWhiteBalance> none = an::resolveCfaWhiteBalance(camera, {});
    ASSERT_TRUE(none.hasValue()) << none.status().message();
    EXPECT_EQ(none->gains, (an::CfaChannelValues{1.0, 1.0, 1.0}));
    EXPECT_EQ(none->source, an::CfaWhiteBalanceSource::Equal);
    EXPECT_DOUBLE_EQ(none->fullScale, stretch);

    const an::Result<an::CfaWhiteBalance> daylight =
        an::resolveCfaWhiteBalance(camera, {.mode = an::CfaWhiteBalanceMode::Daylight});
    ASSERT_TRUE(daylight.hasValue()) << daylight.status().message();
    EXPECT_EQ(daylight->gains, (an::CfaChannelValues{2.0, 1.0, 1.5}));
    EXPECT_EQ(daylight->source, an::CfaWhiteBalanceSource::Daylight);

    const an::Result<an::CfaWhiteBalance> asShot =
        an::resolveCfaWhiteBalance(camera, {.mode = an::CfaWhiteBalanceMode::Camera});
    ASSERT_TRUE(asShot.hasValue()) << asShot.status().message();
    EXPECT_EQ(asShot->gains, (an::CfaChannelValues{2.4, 1.0, 1.2}));
    EXPECT_EQ(asShot->source, an::CfaWhiteBalanceSource::CameraAsShot);
    EXPECT_DOUBLE_EQ(asShot->multipliers().red, 2.4 * stretch);
    EXPECT_DOUBLE_EQ(asShot->multipliers().green, stretch);
    EXPECT_DOUBLE_EQ(asShot->multipliers().blue, 1.2 * stretch);

    // A camera that recorded no as-shot gains falls back to daylight, and says so.
    an::RawCameraMetadata unshot = camera;
    std::fill(std::begin(unshot.cameraMultipliers), std::end(unshot.cameraMultipliers), 0.0);
    const an::Result<an::CfaWhiteBalance> fellBack =
        an::resolveCfaWhiteBalance(unshot, {.mode = an::CfaWhiteBalanceMode::Camera});
    ASSERT_TRUE(fellBack.hasValue()) << fellBack.status().message();
    EXPECT_EQ(fellBack->gains, (an::CfaChannelValues{2.0, 1.0, 1.5}));
    EXPECT_EQ(fellBack->source, an::CfaWhiteBalanceSource::Daylight);

    // Stated gains are divided by the smallest of them.
    const an::Result<an::CfaWhiteBalance> custom = an::resolveCfaWhiteBalance(
        camera, {.mode = an::CfaWhiteBalanceMode::Custom, .custom = an::CfaChannelValues{3.0, 1.5, 6.0}});
    ASSERT_TRUE(custom.hasValue()) << custom.status().message();
    EXPECT_EQ(custom->gains, (an::CfaChannelValues{2.0, 1.0, 4.0}));
    EXPECT_EQ(custom->source, an::CfaWhiteBalanceSource::Stated);

    // A measurement becomes the reciprocal of each filter's mean.
    an::CfaGreyWorld measured;
    measured.sums = an::CfaChannelValues{100.0, 400.0, 50.0};
    measured.redSamples = 10u;
    measured.greenSamples = 20u;
    measured.blueSamples = 10u;
    const an::Result<an::CfaWhiteBalance> automatic =
        an::resolveCfaWhiteBalance(camera, {.mode = an::CfaWhiteBalanceMode::Automatic, .measured = &measured});
    ASSERT_TRUE(automatic.hasValue()) << automatic.status().message();
    EXPECT_DOUBLE_EQ(automatic->gains.red, 2.0);
    EXPECT_DOUBLE_EQ(automatic->gains.green, 1.0);
    EXPECT_DOUBLE_EQ(automatic->gains.blue, 4.0);
    EXPECT_EQ(automatic->source, an::CfaWhiteBalanceSource::Measured);

    // A filter the measurement never saw takes daylight's multiplier, as the camera raw library does.
    an::CfaGreyWorld blind = measured;
    blind.sums.blue = 0.0;
    blind.blueSamples = 0u;
    const an::Result<an::CfaWhiteBalance> partly =
        an::resolveCfaWhiteBalance(camera, {.mode = an::CfaWhiteBalanceMode::Automatic, .measured = &blind});
    ASSERT_TRUE(partly.hasValue()) << partly.status().message();
    EXPECT_DOUBLE_EQ(partly->gains.red, 0.1 / 0.05);
    EXPECT_DOUBLE_EQ(partly->gains.green, 1.0);
    EXPECT_DOUBLE_EQ(partly->gains.blue, 1.5 / 0.05);

    // No stretch leaves the codes' scale alone; a stated one divides by the span.
    const an::Result<an::CfaWhiteBalance> unstretched =
        an::resolveCfaWhiteBalance(camera, {.mode = an::CfaWhiteBalanceMode::Daylight, .fullScaleCode = {}});
    ASSERT_TRUE(unstretched.hasValue()) << unstretched.status().message();
    EXPECT_EQ(unstretched->fullScale, 1.0);
    const an::Result<an::CfaWhiteBalance> unit =
        an::resolveCfaWhiteBalance(camera, {.mode = an::CfaWhiteBalanceMode::None, .fullScaleCode = 1.0});
    ASSERT_TRUE(unit.hasValue()) << unit.status().message();
    EXPECT_DOUBLE_EQ(unit->fullScale, 1.0 / 3900.0);
}

TEST(CfaWhiteBalanceTest, ARecordOrRequestThatCannotResolveIsRefusedByName)
{
    const an::RawCameraMetadata camera = cameraRecord();
    EXPECT_EQ(an::resolveCfaWhiteBalance(camera, {.mode = static_cast<an::CfaWhiteBalanceMode>(9u)}).status().code(),
              an::ErrorCode::InvalidArgument);
    for (const an::CfaChannelValues gains : {an::CfaChannelValues{0.0, 1.0, 1.0}, an::CfaChannelValues{1.0, -1.0, 1.0},
                                             an::CfaChannelValues{1.0, 1.0, std::numeric_limits<double>::quiet_NaN()},
                                             an::CfaChannelValues{std::numeric_limits<double>::infinity(), 1.0, 1.0}})
    {
        EXPECT_EQ(an::resolveCfaWhiteBalance(camera, {.mode = an::CfaWhiteBalanceMode::Custom, .custom = gains})
                      .status()
                      .code(),
                  an::ErrorCode::InvalidArgument);
    }
    EXPECT_EQ(an::resolveCfaWhiteBalance(camera, {.mode = an::CfaWhiteBalanceMode::Automatic}).status().code(),
              an::ErrorCode::InvalidArgument);
    for (const double scale : {0.0, -1.0, std::numeric_limits<double>::infinity()})
    {
        EXPECT_EQ(an::resolveCfaWhiteBalance(camera, {.fullScaleCode = scale}).status().code(),
                  an::ErrorCode::InvalidArgument);
    }

    an::RawCameraMetadata nothing = camera;
    std::fill(std::begin(nothing.cameraMultipliers), std::end(nothing.cameraMultipliers), 0.0);
    std::fill(std::begin(nothing.daylightMultipliers), std::end(nothing.daylightMultipliers), 0.0);
    EXPECT_EQ(an::resolveCfaWhiteBalance(nothing, {.mode = an::CfaWhiteBalanceMode::Daylight}).status().code(),
              an::ErrorCode::InvalidData);
    EXPECT_EQ(an::resolveCfaWhiteBalance(nothing, {.mode = an::CfaWhiteBalanceMode::Camera}).status().code(),
              an::ErrorCode::InvalidData);
    an::CfaGreyWorld blind;
    blind.sums = an::CfaChannelValues{10.0, 10.0, 0.0};
    blind.redSamples = 1u;
    blind.greenSamples = 1u;
    EXPECT_EQ(an::resolveCfaWhiteBalance(nothing, {.mode = an::CfaWhiteBalanceMode::Automatic, .measured = &blind})
                  .status()
                  .code(),
              an::ErrorCode::InvalidData);

    an::RawCameraMetadata unmeasured = camera;
    unmeasured.whiteLevel = 0u;
    EXPECT_EQ(an::resolveCfaWhiteBalance(unmeasured, {}).status().code(), an::ErrorCode::InvalidData);
    EXPECT_TRUE(an::resolveCfaWhiteBalance(unmeasured, {.fullScaleCode = {}}).hasValue());
    an::RawCameraMetadata inverted = camera;
    inverted.whiteLevel = 90u;
    EXPECT_EQ(an::resolveCfaWhiteBalance(inverted, {}).status().code(), an::ErrorCode::InvalidData);
}

TEST(CfaWhiteBalanceTest, TheStretchIsMeasuredFromTheCommonBlackAndTheClipLevelsFromEachFiltersOwn)
{
    an::RawCameraMetadata camera = cameraRecord();
    camera.channelBlack[0] = 5u;
    camera.channelBlack[1] = 3u;
    camera.channelBlack[2] = 7u;
    camera.channelBlack[3] = 3u;
    camera.blackPatternRows = 1u;
    camera.blackPatternColumns = 2u;
    camera.blackPattern = {6u, 2u};
    const an::Result<an::CfaWhiteBalance> resolved = an::resolveCfaWhiteBalance(camera, {});
    ASSERT_TRUE(resolved.hasValue()) << resolved.status().message();
    EXPECT_DOUBLE_EQ(resolved->fullScale, 65535.0 / (4000.0 - (100.0 + 3.0 + 2.0)));

    const an::Result<an::CfaChannelValues> levels = an::cfaGreyWorldClipLevels(cameraRecord());
    ASSERT_TRUE(levels.hasValue()) << levels.status().message();
    EXPECT_EQ(*levels, (an::CfaChannelValues{4000.0 - 25.0 - 104.0, 4000.0 - 25.0 - 100.0, 4000.0 - 25.0 - 102.0}));
    const an::Result<an::CfaChannelValues> tight = an::cfaGreyWorldClipLevels(cameraRecord(), 0.0);
    ASSERT_TRUE(tight.hasValue()) << tight.status().message();
    EXPECT_EQ(tight->green, 3900.0);
    EXPECT_EQ(an::cfaGreyWorldClipLevels(cameraRecord(), -1.0).status().code(), an::ErrorCode::InvalidArgument);
    an::RawCameraMetadata dark = cameraRecord();
    dark.whiteLevel = 0u;
    EXPECT_EQ(an::cfaGreyWorldClipLevels(dark).status().code(), an::ErrorCode::InvalidData);
}

TEST(CfaWhiteBalanceTest, AGreyWorldSumsWhatAnIndependentBlockWalkSums)
{
    const an::EngineContext engine = makeEngine(4u);
    const an::ExecutionContext execution = makeExecution(engine, 3u);
    constexpr std::uint32_t kWidth = 21u;
    constexpr std::uint32_t kHeight = 13u;
    const an::CfaChannelValues clippedAbove{900.0, 1200.0, 1100.0};

    for (const an::CfaDescriptor& cfa : {bayer(an::CfaPattern::Rggb, 1u, 0u), bayer(an::CfaPattern::Gbrg, 0u, 1u),
                                         an::CfaDescriptor::create(kCameraTable, 2u, 3u).value_or(an::CfaDescriptor{})})
    {
        ASSERT_TRUE(cfa.isMosaiced());
        std::vector<std::uint16_t> codes(static_cast<std::size_t>(kWidth) * kHeight);
        std::vector<double> widened(codes.size());
        for (std::size_t index = 0u; index < codes.size(); ++index)
        {
            codes[index] = static_cast<std::uint16_t>((index * 37u) % 1000u);
            widened[index] = static_cast<double>(codes[index]);
        }
        // Two clipped samples in two different blocks, one of each filter a named mosaic has at its
        // position, so a walk that skips too little or too much shows.
        codes[3u * kWidth + 5u] = 1500u;
        widened[3u * kWidth + 5u] = 1500.0;
        codes[10u * kWidth + 17u] = 1250u;
        widened[10u * kWidth + 17u] = 1250.0;

        for (const std::uint32_t blockSize : {8u, 3u, 1u, 64u})
        {
            const an::Result<an::CfaGreyWorld> measured = an::measureCfaGreyWorld(
                execution, an::SampleType::UInt16, codes.data(), planarLayout(an::SampleType::UInt16, kWidth, kHeight),
                cfa, {.blockSize = blockSize, .clippedAbove = clippedAbove});
            ASSERT_TRUE(measured.hasValue()) << measured.status().message();
            expectSameMeasurement(*measured, walkGreyWorld(widened, kWidth, kHeight, cfa, blockSize, clippedAbove));
        }
    }
}

TEST(CfaWhiteBalanceTest, AGreyWorldIsTheSameOnEveryThreadWidthAndHonoursItsSelection)
{
    const an::EngineContext engine = makeEngine(4u);
    const an::ExecutionContext narrow = makeExecution(engine, 1u);
    const an::ExecutionContext wide = makeExecution(engine, 3u);
    constexpr std::uint32_t kWidth = 301u;
    constexpr std::uint32_t kHeight = 257u;
    const std::vector<float> plane = spatial_test::randomPlane(kWidth, kHeight, 0xC0FFEEu);
    const an::CfaDescriptor cfa = bayer(an::CfaPattern::Grbg);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    const an::CfaGreyWorldRequest request{.blockSize = 8u, .clippedAbove = an::CfaChannelValues{0.97, 0.99, 0.98}};
    const an::Result<an::CfaGreyWorld> one =
        an::measureCfaGreyWorld(narrow, an::SampleType::Float32, plane.data(), layout, cfa, request);
    const an::Result<an::CfaGreyWorld> many =
        an::measureCfaGreyWorld(wide, an::SampleType::Float32, plane.data(), layout, cfa, request);
    ASSERT_TRUE(one.hasValue()) << one.status().message();
    ASSERT_TRUE(many.hasValue()) << many.status().message();
    EXPECT_EQ(one->sums, many->sums);
    EXPECT_EQ(one->blocksMeasured, many->blocksMeasured);
    EXPECT_GT(one->blocksSkipped, 0u);

    // A refused sample is left out of the sums and does not refuse its block, however clipped it is;
    // a non-finite sample the rule accepts refuses its block.
    constexpr std::uint32_t kSmall = 4u;
    std::vector<float> small(16u, 10.0f);
    small[0] = 5000.0f;
    small[5] = std::numeric_limits<float>::quiet_NaN();
    const an::ImageLayout smallLayout = planarLayout(an::SampleType::Float32, kSmall, kSmall);
    an::Result<an::ValidityMask> mask = an::ValidityMask::create(engine, smallLayout.extent());
    ASSERT_TRUE(mask.hasValue());
    std::optional<an::ValidityMaskView<std::uint8_t>> maskView = mask->view();
    ASSERT_TRUE(maskView.has_value());
    maskView->fill(true);
    static_cast<void>(maskView->setValid(0u, 0u, 0u, false));
    const an::ValidityMaskView<const std::uint8_t> maskConst = maskView->constView();
    const an::CfaGreyWorldRequest perPixel{.blockSize = 1u, .clippedAbove = an::CfaChannelValues{100.0, 100.0, 100.0}};
    const an::Result<an::CfaGreyWorld> selected = an::measureCfaGreyWorld(
        narrow, an::SampleType::Float32, small.data(), smallLayout, cfa, perPixel,
        {.validity = *an::ValidityDescriptor::create({.rule = an::ValidityRule::ExplicitMask}), .mask = &maskConst});
    ASSERT_TRUE(selected.hasValue()) << selected.status().message();
    // Sixteen one-pixel blocks: the masked one contributes nothing and is not skipped, the NaN is skipped.
    EXPECT_EQ(selected->blocksSkipped, 1u);
    EXPECT_EQ(selected->blocksMeasured, 15u);
    EXPECT_EQ(selected->redSamples + selected->greenSamples + selected->blueSamples, 14u);
    EXPECT_DOUBLE_EQ(selected->sums.red + selected->sums.green + selected->sums.blue, 140.0);

    const an::Result<an::CfaGreyWorld> finite =
        an::measureCfaGreyWorld(narrow, an::SampleType::Float32, small.data(), smallLayout, cfa, perPixel,
                                {.validity = *an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly})});
    ASSERT_TRUE(finite.hasValue()) << finite.status().message();
    // Under the finite rule the NaN is refused rather than clipped, and the 5000 clips its block.
    EXPECT_EQ(finite->blocksSkipped, 1u);
    EXPECT_EQ(finite->redSamples + finite->greenSamples + finite->blueSamples, 14u);
}

TEST(CfaWhiteBalanceTest, GainsMultiplyEachSampleByItsFilterAndStoreAsTheWriteBackSays)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 7u;
    constexpr std::uint32_t kHeight = 5u;
    const an::CfaDescriptor cfa = bayer(an::CfaPattern::Grbg, 1u, 1u);
    const an::CfaChannelValues multipliers{2.5, 1.25, 3.0};
    std::vector<std::uint16_t> codes(static_cast<std::size_t>(kWidth) * kHeight);
    for (std::size_t index = 0u; index < codes.size(); ++index)
    {
        codes[index] = static_cast<std::uint16_t>(index * 1931u + 3u);
    }
    codes[4] = 65000u; // saturates under every multiplier above one
    const an::ImageLayout layout = planarLayout(an::SampleType::UInt16, kWidth, kHeight);

    for (const an::RoundingMode rounding : {an::RoundingMode::TowardZero, an::RoundingMode::NearestEven})
    {
        std::vector<std::uint16_t> stored(codes.size(), 7u);
        const an::ConversionPolicy policy{.rounding = rounding, .domain = an::DomainMapping::CodeValue};
        const an::Result<an::CfaChannelResult> applied = an::applyCfaChannelGains(
            execution, an::SampleType::UInt16, codes.data(), layout, cfa, stored.data(), layout, multipliers, policy);
        ASSERT_TRUE(applied.hasValue()) << applied.status().message();
        EXPECT_EQ(applied->run.outputsWritten, codes.size());
        EXPECT_EQ(applied->conversion.samplesConverted, codes.size());
        EXPECT_GE(applied->conversion.saturatedSamples, 1u);
        EXPECT_EQ(applied->outputValidity, an::ValidityDescriptor{});
        std::uint64_t saturated = 0u;
        for (std::uint32_t y = 0u; y < kHeight; ++y)
        {
            for (std::uint32_t x = 0u; x < kWidth; ++x)
            {
                const std::size_t index = static_cast<std::size_t>(y) * kWidth + x;
                const double product = static_cast<double>(codes[index]) * valueFor(multipliers, cfa.colorAt(x, y));
                // The real value is clamped before it is rounded, so a product past full scale saturates
                // whatever the rounding rule would have made of it.
                double rounded =
                    rounding == an::RoundingMode::TowardZero ? std::trunc(product) : std::nearbyint(product);
                if (product > 65535.0)
                {
                    rounded = 65535.0;
                    ++saturated;
                }
                EXPECT_EQ(stored[index], static_cast<std::uint16_t>(rounded)) << x << "," << y;
            }
        }
        EXPECT_EQ(applied->conversion.saturatedSamples, saturated);
    }

    // A floating mosaic keeps its non-finite samples non-finite.
    std::vector<float> floating{0.25f, std::numeric_limits<float>::quiet_NaN(), -0.5f,
                                std::numeric_limits<float>::infinity()};
    std::vector<float> scaled(4u, 9.0f);
    const an::ImageLayout floatLayout = planarLayout(an::SampleType::Float32, 2u, 2u);
    const an::CfaDescriptor rggb = bayer(an::CfaPattern::Rggb);
    const an::Result<an::CfaChannelResult> floated =
        an::applyCfaChannelGains(execution, an::SampleType::Float32, floating.data(), floatLayout, rggb, scaled.data(),
                                 floatLayout, multipliers, {.domain = an::DomainMapping::CodeValue},
                                 {.validity = *an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly})});
    ASSERT_TRUE(floated.hasValue()) << floated.status().message();
    EXPECT_FLOAT_EQ(scaled[0], 0.25f * 2.5f);
    EXPECT_TRUE(std::isnan(scaled[1]));
    EXPECT_FLOAT_EQ(scaled[2], -0.5f * 1.25f);
    EXPECT_TRUE(std::isinf(scaled[3]));
    EXPECT_EQ(floated->run.selection.valid, 2u);
}

TEST(CfaWhiteBalanceTest, ResolveThenApplyIsATruncatingSinglePrecisionLoadToWithinOneCode)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    an::RawCameraMetadata camera = cameraRecord();
    camera.whiteLevel = 16383u;
    camera.blackLevel = 600u;
    camera.daylightMultipliers[0] = 2.0385;
    camera.daylightMultipliers[1] = 1.0;
    camera.daylightMultipliers[2] = 1.4296;
    const an::Result<an::CfaWhiteBalance> resolved =
        an::resolveCfaWhiteBalance(camera, {.mode = an::CfaWhiteBalanceMode::Daylight});
    ASSERT_TRUE(resolved.hasValue()) << resolved.status().message();

    // Every code a sixteen-bit mosaic can hold, once under each filter of a named block.
    constexpr std::uint32_t kWidth = 256u;
    constexpr std::uint32_t kHeight = 512u;
    const an::CfaDescriptor cfa = bayer(an::CfaPattern::Rggb);
    std::vector<std::uint16_t> codes(static_cast<std::size_t>(kWidth) * kHeight);
    for (std::size_t index = 0u; index < codes.size(); ++index)
    {
        codes[index] = static_cast<std::uint16_t>((index / 2u) % 65536u);
    }
    std::vector<std::uint16_t> stored(codes.size(), 0u);
    const an::ImageLayout layout = planarLayout(an::SampleType::UInt16, kWidth, kHeight);
    const an::Result<an::CfaChannelResult> applied = an::applyCfaChannelGains(
        execution, an::SampleType::UInt16, codes.data(), layout, cfa, stored.data(), layout, resolved->multipliers(),
        {.rounding = an::RoundingMode::TowardZero, .domain = an::DomainMapping::CodeValue});
    ASSERT_TRUE(applied.hasValue()) << applied.status().message();

    const double whiteSpan = 16383.0 - 600.0;
    const auto smallest = static_cast<float>(1.0);
    std::uint64_t differing = 0u;
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            const std::size_t index = static_cast<std::size_t>(y) * kWidth + x;
            const auto gain =
                static_cast<float>(camera.daylightMultipliers[static_cast<std::size_t>(cfa.colorAt(x, y))]);
            const std::uint16_t truncated = truncatingScaled(codes[index], gain, smallest, whiteSpan);
            const int difference = static_cast<int>(stored[index]) - static_cast<int>(truncated);
            ASSERT_LE(std::abs(difference), 1) << "code " << codes[index] << " at " << x << "," << y;
            differing += difference != 0 ? 1u : 0u;
        }
    }
    // The single-precision product moves a value across a whole code only near a boundary; the
    // double-precision product is exact there. The count is how many samples that moved.
    RecordProperty("differingSamples", static_cast<int>(differing));
    EXPECT_LT(differing, codes.size() / 50u);
}

TEST(CfaWhiteBalanceTest, WhatAScaleWouldBreakIsRefusedBeforeASampleIsWritten)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout layout = planarLayout(an::SampleType::UInt16, 4u, 4u);
    std::vector<std::uint16_t> codes(16u, 100u);
    std::vector<std::uint16_t> stored(16u, 7u);
    const an::CfaDescriptor cfa = bayer(an::CfaPattern::Bggr);
    const auto apply = [&](const an::CfaDescriptor& mosaic, const an::CfaChannelValues& multipliers,
                           const an::ConversionPolicy& policy, const an::CfaChannelSelection& selection) {
        return an::applyCfaChannelGains(execution, an::SampleType::UInt16, codes.data(), layout, mosaic, stored.data(),
                                        layout, multipliers, policy, selection)
            .status()
            .code();
    };
    const an::ConversionPolicy codeValues{.domain = an::DomainMapping::CodeValue};

    EXPECT_EQ(apply(an::CfaDescriptor{}, {2.0, 2.0, 2.0}, codeValues, {}), an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(apply(cfa, {-1.0, 1.0, 1.0}, codeValues, {}), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(apply(cfa, {1.0, std::numeric_limits<double>::infinity(), 1.0}, codeValues, {}),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(apply(cfa, {1.0, 1.0, 1.0}, an::ConversionPolicy{.domain = an::DomainMapping::FullScale}, {}),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(apply(cfa, {1.0, 1.0, 1.0}, codeValues,
                    {.validity = *an::ValidityDescriptor::create(
                         {.rule = an::ValidityRule::DomainRange, .lowestValid = 0.0, .highestValid = 200.0})}),
              an::ErrorCode::IncompatibleArguments);
    const an::ValidityDescriptor zeroIsNoData =
        *an::ValidityDescriptor::create({.rule = an::ValidityRule::ZeroIsNoData});
    EXPECT_EQ(apply(cfa, {0.5, 1.0, 1.0}, codeValues, {.validity = zeroIsNoData}),
              an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(stored, std::vector<std::uint16_t>(16u, 7u));
    EXPECT_EQ(apply(cfa, {1.5, 1.0, 2.0}, codeValues, {.validity = zeroIsNoData}), an::ErrorCode::Ok);

    an::Result<an::ValidityMask> produced = an::ValidityMask::create(engine, layout.extent());
    ASSERT_TRUE(produced.hasValue());
    std::optional<an::ValidityMaskView<std::uint8_t>> producedView = produced->view();
    ASSERT_TRUE(producedView.has_value());
    EXPECT_EQ(apply(cfa, {1.0, 1.0, 1.0}, codeValues, {.produced = &*producedView}),
              an::ErrorCode::IncompatibleArguments);

    const an::ImageLayout smaller = planarLayout(an::SampleType::UInt16, 4u, 3u);
    EXPECT_EQ(an::applyCfaChannelGains(execution, an::SampleType::UInt16, codes.data(), layout, cfa, stored.data(),
                                       smaller, {1.0, 1.0, 1.0})
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
    EXPECT_NE(an::applyCfaChannelGains(execution, an::SampleType::UInt16, codes.data(), layout, cfa, codes.data(),
                                       layout, {1.0, 1.0, 1.0})
                  .status()
                  .code(),
              an::ErrorCode::Ok);

    EXPECT_EQ(an::measureCfaGreyWorld(execution, an::SampleType::UInt16, codes.data(), layout, an::CfaDescriptor{})
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(an::measureCfaGreyWorld(execution, an::SampleType::UInt16, codes.data(), layout, cfa, {.blockSize = 0u})
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::measureCfaGreyWorld(
                  execution, an::SampleType::UInt16, codes.data(), layout, cfa,
                  {.clippedAbove = an::CfaChannelValues{1.0, std::numeric_limits<double>::quiet_NaN(), 1.0}})
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::measureCfaGreyWorld(execution, an::SampleType::UInt16, codes.data(), layout, cfa, {},
                                      {.produced = &*producedView})
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
}

} // namespace
