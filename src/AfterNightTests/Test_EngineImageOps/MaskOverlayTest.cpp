// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the mask overlay: the layer of red, green, blue and alpha bytes a viewer paints over an
// image to show its mask.
//
// Oracles are written from the header's definitions: a sample read as a fraction of full scale by the engine's
// own conversion, a mask value held to the unit interval with a value that is not a number read as zero, the
// mode's value, and one rounding to a byte with ties to even. A pixel any read sample of which is invalid is the
// request's invalid-pixel colour.

#include "SpatialFilterFixture.h"

#include <AfterNightEngine/Algorithms/Masks/MaskOverlay.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/SampleConversion.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <stop_token>
#include <type_traits>
#include <vector>

namespace
{

using spatial_test::interleavedLayout;
using spatial_test::makeEngine;
using spatial_test::makeExecution;
using spatial_test::planarLayout;
using spatial_test::Sequence;

constexpr std::uint8_t kSentinelByte = 0xA5u;

[[nodiscard]] an::ImageDescriptor describe(an::SampleType type, std::uint32_t width, std::uint32_t height,
                                           std::uint32_t channels,
                                           an::NumericDomain domain = an::NumericDomain::NormalizedLinear)
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, channels, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const std::optional<an::ImageDescriptor> descriptor = an::ImageDescriptor::create(
        an::ImageDescriptorRequest{.extent = *extent,
                                   .sampleType = type,
                                   .channelSchema = channels == 1u ? an::ChannelSchema::Mono : an::ChannelSchema::Rgb,
                                   .colorSpace = an::ColorSpace::Unspecified,
                                   .numericDomain = domain,
                                   .cfa = {},
                                   .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                   .pixelAspectRatio = 1.0});
    EXPECT_TRUE(descriptor.has_value());
    return *descriptor;
}

[[nodiscard]] an::MaskOverlayPlan plan(const an::MaskOverlayRequest& request = {})
{
    const an::Result<an::MaskOverlayPlan> made = an::planMaskOverlay(request);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return *made;
}

/// Samples of one representation in a layout, with a descriptor, owning their storage.
template <typename SampleT> struct Owned
{
    an::ImageDescriptor descriptor;
    an::ImageLayout layout;
    std::vector<SampleT> samples;

    [[nodiscard]] an::MaskOverlaySource source(an::KernelValidityView validity = {}) const
    {
        return an::MaskOverlaySource{
            .descriptor = &descriptor, .samples = samples.data(), .layout = &layout, .validity = validity};
    }

    [[nodiscard]] SampleT& at(std::uint32_t c, std::uint32_t x, std::uint32_t y)
    {
        const std::optional<std::uint64_t> offset =
            layout.sampleByteOffset(c / layout.samplesPerPixel(), y, x, c % layout.samplesPerPixel());
        return samples[static_cast<std::size_t>(*offset / sizeof(SampleT))];
    }

    [[nodiscard]] SampleT at(std::uint32_t c, std::uint32_t x, std::uint32_t y) const
    {
        const std::optional<std::uint64_t> offset =
            layout.sampleByteOffset(c / layout.samplesPerPixel(), y, x, c % layout.samplesPerPixel());
        return samples[static_cast<std::size_t>(*offset / sizeof(SampleT))];
    }
};

/// An image of random samples over the representation's whole range, planar or interleaved, padded or not.
template <typename SampleT>
[[nodiscard]] Owned<SampleT> randomImage(std::uint32_t width, std::uint32_t height, std::uint32_t channels,
                                         std::uint64_t seed, bool interleaved = false, std::uint32_t padding = 0u,
                                         double low = 0.0, double high = 1.0)
{
    const an::SampleType type = an::kSampleTypeOf<SampleT>;
    const an::ImageLayout layout = interleaved ? interleavedLayout(type, width, height, channels)
                                               : planarLayout(type, width, height, channels, padding);
    Owned<SampleT> image{describe(type, width, height, channels), layout,
                         std::vector<SampleT>(layout.totalBytes() / sizeof(SampleT), SampleT{})};
    Sequence sequence{seed};
    for (std::uint32_t c = 0u; c < channels; ++c)
    {
        for (std::uint32_t y = 0u; y < height; ++y)
        {
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                const double unit = low + static_cast<double>(sequence.next()) * (high - low);
                if constexpr (std::is_floating_point_v<SampleT>)
                {
                    image.at(c, x, y) = static_cast<SampleT>(unit);
                }
                else
                {
                    const double lowest = static_cast<double>((std::numeric_limits<SampleT>::min)());
                    const double highest = static_cast<double>((std::numeric_limits<SampleT>::max)());
                    image.at(c, x, y) = static_cast<SampleT>(std::floor(lowest + unit * (highest - lowest)));
                }
            }
        }
    }
    return image;
}

/// A sample as a fraction of full scale, by the engine's own conversion.
template <typename SampleT> [[nodiscard]] double fraction(SampleT value)
{
    if constexpr (std::is_floating_point_v<SampleT>)
    {
        return static_cast<double>(value);
    }
    else
    {
        an::ConversionPolicy policy{};
        policy.domain = an::DomainMapping::FullScale;
        return an::convertSample<SampleT, double>(value, policy).value;
    }
}

/// A mask value held to the unit interval, not-a-number read as zero.
[[nodiscard]] double held(double value)
{
    if (std::isnan(value) || value < 0.0)
    {
        return 0.0;
    }
    return value > 1.0 ? 1.0 : value;
}

/// A value of the unit interval as a byte, rounded to nearest with ties to even.
[[nodiscard]] std::uint8_t byteOf(double value)
{
    if (std::isnan(value) || value <= 0.0)
    {
        return 0u;
    }
    if (value >= 1.0)
    {
        return 255u;
    }
    return static_cast<std::uint8_t>(std::nearbyint(value * 255.0));
}

/// An overlay destination of the plan's layout, padded, filled with a sentinel.
struct Layer
{
    an::ImageLayout layout;
    std::vector<std::uint8_t> bytes;

    [[nodiscard]] std::array<std::uint8_t, 4u> pixel(std::uint32_t x, std::uint32_t y) const
    {
        const std::size_t offset = static_cast<std::size_t>(y * layout.rowStrideBytes() + x * 4u);
        return {bytes[offset], bytes[offset + 1u], bytes[offset + 2u], bytes[offset + 3u]};
    }
};

[[nodiscard]] Layer layerFor(const an::MaskOverlayPlan& made, const an::ImageDescriptor& mask,
                             std::uint32_t paddingBytes = 0u)
{
    const an::Result<an::ImageLayoutRequest> request = an::maskOverlayLayout(made, mask);
    EXPECT_TRUE(request.hasValue()) << request.status().message();
    const std::uint64_t stride = static_cast<std::uint64_t>(mask.extent().width()) * 4u + paddingBytes;
    const std::optional<an::ImageLayout> layout = an::ImageLayout::createWithStrides(
        *request, stride, stride * mask.extent().height(), an::defaultEngineLimits());
    EXPECT_TRUE(layout.has_value());
    return Layer{*layout, std::vector<std::uint8_t>(layout->totalBytes(), kSentinelByte)};
}

[[nodiscard]] an::MaskOverlayResult render(const an::ExecutionContext& execution, const an::MaskOverlayPlan& made,
                                           const an::MaskOverlaySource& mask, const an::MaskOverlaySource* image,
                                           Layer& layer)
{
    const an::Result<an::MaskOverlayResult> result =
        an::renderMaskOverlay(execution, made, mask, image, layer.bytes.data(), layer.layout);
    EXPECT_TRUE(result.hasValue()) << result.status().message();
    return result.hasValue() ? *result : an::MaskOverlayResult{};
}

/// Every representation's mask, tinted, replaced and inverted, against the definition.
template <typename SampleT> void holdEveryModeOfOneRepresentation(const an::ExecutionContext& execution)
{
    const Owned<SampleT> mask = randomImage<SampleT>(19u, 7u, 1u, 31u + sizeof(SampleT));
    for (const bool invert : {false, true})
    {
        const an::MaskOverlayPlan tint = plan(
            {.mode = an::MaskOverlayMode::Tint, .tint = {.red = 12u, .green = 200u, .blue = 7u}, .invertMask = invert});
        const an::MaskOverlayPlan replace = plan({.mode = an::MaskOverlayMode::Replace, .invertMask = invert});
        Layer tinted = layerFor(tint, mask.descriptor);
        Layer replaced = layerFor(replace, mask.descriptor);
        const an::MaskOverlayResult tintResult = render(execution, tint, mask.source(), nullptr, tinted);
        (void)render(execution, replace, mask.source(), nullptr, replaced);
        EXPECT_EQ(tintResult.run.selection.total, 19u * 7u);
        EXPECT_EQ(tintResult.run.selection.valid, 19u * 7u);
        EXPECT_EQ(tintResult.run.outputsWritten, 19u * 7u);
        for (std::uint32_t y = 0u; y < 7u; ++y)
        {
            for (std::uint32_t x = 0u; x < 19u; ++x)
            {
                const double value = held(fraction(mask.at(0u, x, y)));
                const double shown = invert ? 1.0 - value : value;
                const std::array<std::uint8_t, 4u> expectedTint{12u, 200u, 7u, byteOf(1.0 - shown)};
                ASSERT_EQ(tinted.pixel(x, y), expectedTint) << x << "," << y << " invert " << invert;
                const std::uint8_t grey = byteOf(shown);
                const std::array<std::uint8_t, 4u> expectedReplace{grey, grey, grey, 255u};
                ASSERT_EQ(replaced.pixel(x, y), expectedReplace) << x << "," << y << " invert " << invert;
            }
        }
    }
}

[[nodiscard]] an::ExecutionContext stoppedExecution(const an::EngineContext& engine)
{
    std::stop_source stop;
    stop.request_stop();
    an::ExecutionOptions options;
    options.stopToken = stop.get_token();
    an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(made.hasValue());
    return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
}

} // namespace

TEST(MaskOverlayTest, ThePlanAndTheLayoutStateWhatTheLayerIs)
{
    EXPECT_EQ(an::planMaskOverlay({.mode = static_cast<an::MaskOverlayMode>(3u)}).status().code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_TRUE(an::isMaskOverlayMode(an::MaskOverlayMode::Multiply));
    const an::MaskOverlayPlan made = plan();
    // The default is a red tint of the mask as it is, invalid pixels clear.
    EXPECT_EQ(made.request().mode, an::MaskOverlayMode::Tint);
    EXPECT_EQ(made.request().tint.red, 255u);
    EXPECT_EQ(made.request().tint.green, 0u);
    EXPECT_FALSE(made.request().invertMask);
    EXPECT_EQ(made.request().invalidPixel.alpha, 0u);
    const an::Result<an::ImageLayoutRequest> layout =
        an::maskOverlayLayout(made, describe(an::SampleType::Float32, 9u, 5u, 3u));
    ASSERT_TRUE(layout.hasValue());
    EXPECT_EQ(layout->extent.width(), 9u);
    EXPECT_EQ(layout->extent.height(), 5u);
    EXPECT_EQ(layout->extent.channelCount(), 4u);
    EXPECT_EQ(layout->elementSizeBytes, 1u);
    EXPECT_EQ(layout->samplesPerPixel, 4u);
    EXPECT_EQ(layout->planeCount, 1u);
    EXPECT_EQ(layout->rowAlignmentBytes, 4u);
    const std::optional<an::ImageExtent> two = an::ImageExtent::create(9u, 5u, 2u, an::defaultEngineLimits());
    const std::optional<an::ImageDescriptor> pair =
        an::ImageDescriptor::create(an::ImageDescriptorRequest{.extent = *two,
                                                               .sampleType = an::SampleType::Float32,
                                                               .channelSchema = an::ChannelSchema::Named,
                                                               .colorSpace = an::ColorSpace::Unspecified,
                                                               .numericDomain = an::NumericDomain::NormalizedLinear,
                                                               .cfa = {},
                                                               .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                               .pixelAspectRatio = 1.0});
    if (pair.has_value())
    {
        EXPECT_EQ(an::maskOverlayLayout(made, *pair).status().code(), an::ErrorCode::IncompatibleArguments);
    }
}

TEST(MaskOverlayTest, EveryRepresentationIsReadAtFullScaleInEveryModeAndInverted)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    holdEveryModeOfOneRepresentation<std::uint8_t>(execution);
    holdEveryModeOfOneRepresentation<std::int8_t>(execution);
    holdEveryModeOfOneRepresentation<std::uint16_t>(execution);
    holdEveryModeOfOneRepresentation<std::int16_t>(execution);
    holdEveryModeOfOneRepresentation<std::uint32_t>(execution);
    holdEveryModeOfOneRepresentation<std::int32_t>(execution);
    holdEveryModeOfOneRepresentation<float>(execution);
    holdEveryModeOfOneRepresentation<double>(execution);
}

TEST(MaskOverlayTest, AnEightBitMaskThroughItsTableIsWhatTheGeneralPathWrites)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    // Every code, so every entry of the table is read; a rule that admits every integer sends the same mask
    // down the general path.
    Owned<std::uint8_t> mask{
        describe(an::SampleType::UInt8, 64u, 4u, 1u), planarLayout(an::SampleType::UInt8, 64u, 4u, 1u, 3u), {}};
    mask.samples.assign(mask.layout.totalBytes(), 0u);
    for (std::uint32_t code = 0u; code < 256u; ++code)
    {
        mask.at(0u, code % 64u, code / 64u) = static_cast<std::uint8_t>(code);
    }
    const std::optional<an::ValidityDescriptor> finite =
        an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly});
    ASSERT_TRUE(finite.has_value());
    for (const an::MaskOverlayMode mode : {an::MaskOverlayMode::Tint, an::MaskOverlayMode::Replace})
    {
        for (const bool invert : {false, true})
        {
            const an::MaskOverlayPlan made = plan({.mode = mode, .invertMask = invert});
            Layer table = layerFor(made, mask.descriptor);
            Layer general = layerFor(made, mask.descriptor);
            (void)render(execution, made, mask.source(), nullptr, table);
            (void)render(execution, made, mask.source(an::KernelValidityView{.descriptor = *finite}), nullptr, general);
            EXPECT_EQ(table.bytes, general.bytes) << static_cast<int>(mode) << " invert " << invert;
            // An eight-bit tint is exactly the complement of the code.
            if (mode == an::MaskOverlayMode::Tint && !invert)
            {
                for (std::uint32_t code = 0u; code < 256u; ++code)
                {
                    EXPECT_EQ(table.pixel(code % 64u, code / 64u)[3], 255u - code);
                }
            }
        }
    }
}

TEST(MaskOverlayTest, AMaskOutsideTheUnitIntervalOrNotANumberIsHeldBeforeItIsShown)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    Owned<float> mask = randomImage<float>(5u, 1u, 1u, 3u);
    const std::array<float, 5u> values{-0.5f, 1.5f, std::numeric_limits<float>::quiet_NaN(),
                                       std::numeric_limits<float>::infinity(), 0.5f};
    for (std::uint32_t x = 0u; x < 5u; ++x)
    {
        mask.at(0u, x, 0u) = values[x];
    }
    const std::array<std::uint8_t, 5u> plainAlpha{255u, 0u, 255u, 0u, 128u};
    const std::array<std::uint8_t, 5u> invertedAlpha{0u, 255u, 0u, 255u, 128u};
    for (const bool invert : {false, true})
    {
        const an::MaskOverlayPlan made = plan({.invertMask = invert});
        Layer layer = layerFor(made, mask.descriptor);
        (void)render(execution, made, mask.source(), nullptr, layer);
        for (std::uint32_t x = 0u; x < 5u; ++x)
        {
            EXPECT_EQ(layer.pixel(x, 0u)[3], invert ? invertedAlpha[x] : plainAlpha[x]) << x;
        }
    }
}

TEST(MaskOverlayTest, AThreeChannelMaskTintsAsItsChannelsStackedAndReplacesInColour)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    for (const bool interleaved : {false, true})
    {
        const Owned<std::uint16_t> mask = randomImage<std::uint16_t>(13u, 6u, 3u, 41u, interleaved);
        const an::MaskOverlayPlan tint = plan({.tint = {.red = 0u, .green = 255u, .blue = 255u}});
        const an::MaskOverlayPlan replace = plan({.mode = an::MaskOverlayMode::Replace});
        Layer tinted = layerFor(tint, mask.descriptor, 8u);
        Layer replaced = layerFor(replace, mask.descriptor);
        (void)render(execution, tint, mask.source(), nullptr, tinted);
        (void)render(execution, replace, mask.source(), nullptr, replaced);
        for (std::uint32_t y = 0u; y < 6u; ++y)
        {
            for (std::uint32_t x = 0u; x < 13u; ++x)
            {
                const double r = held(fraction(mask.at(0u, x, y)));
                const double g = held(fraction(mask.at(1u, x, y)));
                const double b = held(fraction(mask.at(2u, x, y)));
                const std::array<std::uint8_t, 4u> expectedTint{0u, 255u, 255u, byteOf(1.0 - r * g * b)};
                ASSERT_EQ(tinted.pixel(x, y), expectedTint) << x << "," << y;
                const std::array<std::uint8_t, 4u> expectedReplace{byteOf(r), byteOf(g), byteOf(b), 255u};
                ASSERT_EQ(replaced.pixel(x, y), expectedReplace) << x << "," << y;
            }
            // The padding after each tinted row but the last, which the layout does not hold, is never written.
            for (std::uint32_t pad = 0u; y + 1u < 6u && pad < 8u; ++pad)
            {
                ASSERT_EQ(tinted.bytes[static_cast<std::size_t>(y * tinted.layout.rowStrideBytes() + 52u + pad)],
                          kSentinelByte);
            }
        }
    }
}

TEST(MaskOverlayTest, AMultiplyShowsTheImageTimesTheMaskOpaque)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    // An image beyond the unit interval, so the product is held only once it is formed.
    Owned<float> colour = randomImage<float>(11u, 5u, 3u, 51u, false, 0u, -0.2, 1.6);
    colour.at(1u, 2u, 3u) = std::numeric_limits<float>::quiet_NaN();
    const Owned<float> grey = randomImage<float>(11u, 5u, 1u, 52u, false, 0u, -0.2, 1.6);
    const Owned<std::uint16_t> monoMask = randomImage<std::uint16_t>(11u, 5u, 1u, 53u);
    const Owned<std::uint8_t> colourMask = randomImage<std::uint8_t>(11u, 5u, 3u, 54u, true);
    for (const bool invert : {false, true})
    {
        const an::MaskOverlayPlan made = plan({.mode = an::MaskOverlayMode::Multiply, .invertMask = invert});
        const auto shown = [invert](double value) { return invert ? 1.0 - held(value) : held(value); };

        Layer colourMono = layerFor(made, monoMask.descriptor);
        const an::MaskOverlaySource colourSource = colour.source();
        (void)render(execution, made, monoMask.source(), &colourSource, colourMono);
        Layer colourColour = layerFor(made, colourMask.descriptor);
        (void)render(execution, made, colourMask.source(), &colourSource, colourColour);
        Layer greyMono = layerFor(made, monoMask.descriptor);
        const an::MaskOverlaySource greySource = grey.source();
        (void)render(execution, made, monoMask.source(), &greySource, greyMono);
        for (std::uint32_t y = 0u; y < 5u; ++y)
        {
            for (std::uint32_t x = 0u; x < 11u; ++x)
            {
                const double m = shown(fraction(monoMask.at(0u, x, y)));
                std::array<std::uint8_t, 4u> expected{};
                for (std::uint32_t c = 0u; c < 3u; ++c)
                {
                    expected[c] = byteOf(static_cast<double>(colour.at(c, x, y)) * m);
                }
                expected[3] = 255u;
                ASSERT_EQ(colourMono.pixel(x, y), expected) << x << "," << y;
                for (std::uint32_t c = 0u; c < 3u; ++c)
                {
                    expected[c] =
                        byteOf(static_cast<double>(colour.at(c, x, y)) * shown(fraction(colourMask.at(c, x, y))));
                }
                ASSERT_EQ(colourColour.pixel(x, y), expected) << x << "," << y;
                const std::uint8_t value = byteOf(static_cast<double>(grey.at(0u, x, y)) * m);
                const std::array<std::uint8_t, 4u> expectedGrey{value, value, value, 255u};
                ASSERT_EQ(greyMono.pixel(x, y), expectedGrey) << x << "," << y;
            }
        }
    }
}

TEST(MaskOverlayTest, APixelWithAnInvalidReadSampleIsTheStatedColourAndIsCounted)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    Owned<float> mask = randomImage<float>(10u, 4u, 3u, 61u);
    // The mask rejects its non-finite samples, and one is planted in the blue of pixel (2, 1).
    mask.at(2u, 2u, 1u) = std::numeric_limits<float>::quiet_NaN();
    const std::optional<an::ValidityDescriptor> finite =
        an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly});
    ASSERT_TRUE(finite.has_value());
    // The image carries one shared mask plane, rejecting pixel (7, 3).
    const Owned<float> image = randomImage<float>(10u, 4u, 3u, 62u);
    const std::optional<an::ImageExtent> maskExtent = an::ImageExtent::create(10u, 4u, 1u, an::defaultEngineLimits());
    std::vector<std::uint8_t> bits(static_cast<std::size_t>(2u) * 4u, 0u);
    const std::optional<an::ValidityMaskView<std::uint8_t>> writable =
        an::ValidityMaskView<std::uint8_t>::createTight(bits, *maskExtent);
    ASSERT_TRUE(writable.has_value());
    writable->fill(true);
    ASSERT_TRUE(writable->setValid(0u, 7u, 3u, false));
    const an::ValidityMaskView<const std::uint8_t> validity = writable->constView();
    const std::optional<an::ValidityDescriptor> explicitMask =
        an::ValidityDescriptor::create({.rule = an::ValidityRule::ExplicitMask});
    ASSERT_TRUE(explicitMask.has_value());

    const an::MaskOverlayColor invalid{.red = 1u, .green = 2u, .blue = 3u, .alpha = 4u};
    const an::MaskOverlayPlan tint = plan({.invalidPixel = invalid});
    Layer tinted = layerFor(tint, mask.descriptor);
    const an::MaskOverlayResult tintResult =
        render(execution, tint, mask.source(an::KernelValidityView{.descriptor = *finite}), nullptr, tinted);
    EXPECT_EQ(tintResult.run.selection.total, 40u);
    EXPECT_EQ(tintResult.run.selection.valid, 39u);
    EXPECT_EQ(tintResult.run.outputsWritten, 40u);
    const std::array<std::uint8_t, 4u> invalidBytes{1u, 2u, 3u, 4u};
    EXPECT_EQ(tinted.pixel(2u, 1u), invalidBytes);
    EXPECT_NE(tinted.pixel(3u, 1u), invalidBytes);

    const an::MaskOverlayPlan multiply = plan({.mode = an::MaskOverlayMode::Multiply, .invalidPixel = invalid});
    Layer multiplied = layerFor(multiply, mask.descriptor);
    const an::MaskOverlaySource imageSource =
        image.source(an::KernelValidityView{.descriptor = *explicitMask, .mask = &validity});
    const an::MaskOverlayResult multiplyResult = render(
        execution, multiply, mask.source(an::KernelValidityView{.descriptor = *finite}), &imageSource, multiplied);
    EXPECT_EQ(multiplyResult.run.selection.valid, 38u);
    EXPECT_EQ(multiplied.pixel(2u, 1u), invalidBytes);
    EXPECT_EQ(multiplied.pixel(7u, 3u), invalidBytes);
}

TEST(MaskOverlayTest, TheLayerDoesNotDependOnHowManyThreadsWriteIt)
{
    const an::EngineContext engine = makeEngine(8u);
    const an::ExecutionContext one = makeExecution(engine, 1u);
    const an::ExecutionContext eight = makeExecution(engine, 8u);
    const Owned<float> mask = randomImage<float>(613u, 211u, 3u, 71u, false, 4u);
    const Owned<std::uint16_t> image = randomImage<std::uint16_t>(613u, 211u, 3u, 72u, true);
    const an::MaskOverlaySource imageSource = image.source();
    for (const an::MaskOverlayMode mode :
         {an::MaskOverlayMode::Tint, an::MaskOverlayMode::Replace, an::MaskOverlayMode::Multiply})
    {
        const an::MaskOverlayPlan made = plan({.mode = mode, .invertMask = true});
        const an::MaskOverlaySource* const second = mode == an::MaskOverlayMode::Multiply ? &imageSource : nullptr;
        Layer serial = layerFor(made, mask.descriptor);
        Layer parallel = layerFor(made, mask.descriptor);
        (void)render(one, made, mask.source(), second, serial);
        (void)render(eight, made, mask.source(), second, parallel);
        EXPECT_EQ(serial.bytes, parallel.bytes) << static_cast<int>(mode);
        // Every pixel is written, so the layer holds no sentinel alpha: agreeing is not two untouched layers agreeing.
        std::size_t untouched = 0u;
        for (std::uint32_t y = 0u; y < 211u; ++y)
        {
            for (std::uint32_t x = 0u; x < 613u; ++x)
            {
                untouched += serial.pixel(x, y) == std::array<std::uint8_t, 4u>{kSentinelByte, kSentinelByte,
                                                                                kSentinelByte, kSentinelByte}
                                 ? 1u
                                 : 0u;
            }
        }
        EXPECT_EQ(untouched, 0u) << static_cast<int>(mode);
    }
}

TEST(MaskOverlayTest, AnOverlayRefusesWhatItCannotReadAndAStoppedExecution)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::MaskOverlayPlan tint = plan();
    const an::MaskOverlayPlan multiply = plan({.mode = an::MaskOverlayMode::Multiply});
    const Owned<float> mask = randomImage<float>(8u, 6u, 1u, 81u);
    Layer layer = layerFor(tint, mask.descriptor);
    const auto refusal = [&](const an::MaskOverlayPlan& made, const an::MaskOverlaySource& source,
                             const an::MaskOverlaySource* image) {
        return an::renderMaskOverlay(execution, made, source, image, layer.bytes.data(), layer.layout).status().code();
    };

    // A mask in a domain that is not a fraction of full scale.
    for (const an::NumericDomain domain : {an::NumericDomain::RawSensorCode, an::NumericDomain::PhysicalScaled})
    {
        Owned<float> other = mask;
        other.descriptor = describe(an::SampleType::Float32, 8u, 6u, 1u, domain);
        EXPECT_EQ(refusal(tint, other.source(), nullptr), an::ErrorCode::IncompatibleArguments);
    }
    // A null member, and a layout that does not describe the descriptor.
    an::MaskOverlaySource broken = mask.source();
    broken.samples = nullptr;
    EXPECT_EQ(refusal(tint, broken, nullptr), an::ErrorCode::InvalidArgument);
    const an::ImageLayout wider = planarLayout(an::SampleType::Float32, 9u, 6u, 1u);
    broken = mask.source();
    broken.layout = &wider;
    EXPECT_EQ(refusal(tint, broken, nullptr), an::ErrorCode::InvalidArgument);
    // An image beside a tint, none beside a multiply, and one of another size or of one channel under three.
    const an::MaskOverlaySource self = mask.source();
    EXPECT_EQ(refusal(tint, mask.source(), &self), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(refusal(multiply, mask.source(), nullptr), an::ErrorCode::InvalidArgument);
    const Owned<float> taller = randomImage<float>(8u, 7u, 1u, 82u);
    const an::MaskOverlaySource tallerSource = taller.source();
    EXPECT_EQ(refusal(multiply, mask.source(), &tallerSource), an::ErrorCode::IncompatibleArguments);
    const Owned<float> colourMask = randomImage<float>(8u, 6u, 3u, 83u);
    Layer colourLayer = layerFor(multiply, colourMask.descriptor);
    EXPECT_EQ(an::renderMaskOverlay(execution, multiply, colourMask.source(), &self, colourLayer.bytes.data(),
                                    colourLayer.layout)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
    // A destination of three bytes per pixel, and one over the mask's own samples.
    const an::ImageLayout threeBytes = interleavedLayout(an::SampleType::UInt8, 8u, 6u, 3u);
    std::vector<std::uint8_t> rgb(threeBytes.totalBytes(), kSentinelByte);
    EXPECT_EQ(an::renderMaskOverlay(execution, tint, mask.source(), nullptr, rgb.data(), threeBytes).status().code(),
              an::ErrorCode::IncompatibleArguments);
    Owned<float> overlapped = randomImage<float>(8u, 6u, 1u, 84u);
    EXPECT_EQ(
        an::renderMaskOverlay(execution, tint, overlapped.source(), nullptr, overlapped.samples.data(), layer.layout)
            .status()
            .code(),
        an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(an::renderMaskOverlay(execution, tint, mask.source(), nullptr, nullptr, layer.layout).status().code(),
              an::ErrorCode::InvalidArgument);
    for (const std::uint8_t byte : layer.bytes)
    {
        ASSERT_EQ(byte, kSentinelByte);
    }

    EXPECT_EQ(
        an::renderMaskOverlay(an::ExecutionContext{}, tint, mask.source(), nullptr, layer.bytes.data(), layer.layout)
            .status()
            .code(),
        an::ErrorCode::NotInitialized);
    EXPECT_EQ(
        an::renderMaskOverlay(stoppedExecution(engine), tint, mask.source(), nullptr, layer.bytes.data(), layer.layout)
            .status()
            .code(),
        an::ErrorCode::Cancelled);
}

TEST(MaskOverlayTest, TheDeclaredEffectIsANewLayerOfDisplayBytesOnTheMasksGrid)
{
    EXPECT_EQ(an::checkOperationEffect(an::kMaskOverlayEffect), an::OperationEffectError::None);
    EXPECT_EQ(an::kMaskOverlayEffect.samples, an::SampleEffect::Writes);
    EXPECT_EQ(an::kMaskOverlayEffect.description, an::DescriptionEffect::Restates);
    EXPECT_EQ(an::kMaskOverlayEffect.semantics, an::SemanticEffect::Restates);
    EXPECT_EQ(an::kMaskOverlayEffect.solution, an::SolutionEffect::Preserves);
    EXPECT_EQ(an::kMaskOverlayEffect.codecMetadata, an::CodecMetadataEffect::Drops);
    EXPECT_EQ(an::kMaskOverlayEffect.outputIntent, an::OutputIntentEffect::Withdraws);
    EXPECT_EQ(an::kMaskOverlayEffect.validity.policy, an::ValidityPolicy::Regenerate);
    EXPECT_EQ(an::kMaskOverlayEffect.validity.produced.rule(), an::ValidityRule::AllValid);
}
