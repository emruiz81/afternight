// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the image blend composition: twenty-seven modes, the transfers, the blend image's
// pre-filters, the kept lightness and the opacity.
//
// The composition is held to the operations it states it composes, called directly over the same Float32 images:
// `blendImages` for the twenty named modes, the exact Gaussian in passes for the blur and the high pass,
// `colorizeImage` for the colourizing filters, the colour transform for the lightness, and the midtones transfer for
// the stretches. The seven modes computed as arithmetic, the high pass's difference and the opacity are held to their
// formulas.

#include "SpatialFilterFixture.h"

#include <AfterNightEngine/Algorithms/Color/ColorAdjustment.h>
#include <AfterNightEngine/Algorithms/Color/ColorTransform.h>
#include <AfterNightEngine/Algorithms/Pointwise/BlendComposition.h>
#include <AfterNightEngine/Algorithms/Pointwise/BlendMode.h>
#include <AfterNightEngine/Algorithms/Pointwise/MidtonesTransfer.h>
#include <AfterNightEngine/Algorithms/Spatial/Gaussian.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/SampleConversion.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace
{

using spatial_test::makeEngine;
using spatial_test::makeExecution;
using spatial_test::planarLayout;
using spatial_test::Sequence;

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

/// A tightly packed planar image of one representation, with its descriptor.
template <typename SampleT> struct Planar
{
    an::ImageDescriptor descriptor;
    an::ImageLayout layout;
    std::vector<SampleT> samples;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;

    [[nodiscard]] an::BlendCompositionSource source(an::KernelValidityView validity = {}) const
    {
        return an::BlendCompositionSource{
            .descriptor = &descriptor, .samples = samples.data(), .layout = &layout, .validity = validity};
    }

    [[nodiscard]] SampleT& at(std::uint32_t c, std::uint32_t x, std::uint32_t y)
    {
        return samples[(static_cast<std::size_t>(c) * height + y) * width + x];
    }

    [[nodiscard]] SampleT at(std::uint32_t c, std::uint32_t x, std::uint32_t y) const
    {
        return samples[(static_cast<std::size_t>(c) * height + y) * width + x];
    }
};

template <typename SampleT>
[[nodiscard]] Planar<SampleT> empty(std::uint32_t width, std::uint32_t height, std::uint32_t channels,
                                    an::NumericDomain domain = an::NumericDomain::NormalizedLinear)
{
    const an::SampleType type = an::kSampleTypeOf<SampleT>;
    return Planar<SampleT>{describe(type, width, height, channels, domain), planarLayout(type, width, height, channels),
                           std::vector<SampleT>(static_cast<std::size_t>(width) * height * channels, SampleT{}), width,
                           height};
}

template <typename SampleT>
[[nodiscard]] Planar<SampleT> random(std::uint32_t width, std::uint32_t height, std::uint32_t channels,
                                     std::uint64_t seed, double low = 0.0, double high = 1.0,
                                     an::NumericDomain domain = an::NumericDomain::NormalizedLinear)
{
    Planar<SampleT> made = empty<SampleT>(width, height, channels, domain);
    Sequence sequence{seed};
    for (SampleT& sample : made.samples)
    {
        const double value = low + static_cast<double>(sequence.next()) * (high - low);
        if constexpr (std::is_floating_point_v<SampleT>)
        {
            sample = static_cast<SampleT>(value);
        }
        else
        {
            sample =
                static_cast<SampleT>(std::round(value * static_cast<double>((std::numeric_limits<SampleT>::max)())));
        }
    }
    return made;
}

[[nodiscard]] an::BlendCompositionPlan plan(const an::BlendCompositionRequest& request = {})
{
    const an::Result<an::BlendCompositionPlan> made = an::planBlendComposition(request);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return *made;
}

template <typename ResultT = float, typename TargetT, typename BlendT>
[[nodiscard]] Planar<ResultT> compose(const an::ExecutionContext& execution, const an::BlendCompositionRequest& request,
                                      const Planar<TargetT>& target, const Planar<BlendT>& blend,
                                      an::KernelValidityView targetValidity = {})
{
    const an::BlendCompositionPlan made = plan(request);
    const std::uint32_t channels = an::blendCompositionChannels(made, target.descriptor, blend.descriptor);
    Planar<ResultT> result = empty<ResultT>(target.width, target.height, channels);
    const an::Result<an::BlendCompositionResult> done = an::composeBlend(
        execution, made, target.source(targetValidity), blend.source(), result.samples.data(), result.layout);
    EXPECT_TRUE(done.hasValue()) << done.status().message();
    if (done.hasValue())
    {
        EXPECT_EQ(done->run.outputsWritten, static_cast<std::uint64_t>(target.width) * target.height * channels);
    }
    return result;
}

/// What `blendImages` writes for two Float32 images of one shape.
[[nodiscard]] Planar<float> named(const an::ExecutionContext& execution, an::BlendMode mode, const Planar<float>& base,
                                  const Planar<float>& blend)
{
    Planar<float> result = empty<float>(base.width, base.height, base.descriptor.extent().channelCount());
    const an::Result<an::BlendModeResult> done =
        an::blendImages(execution, an::SampleType::Float32, base.samples.data(), base.layout, blend.samples.data(),
                        blend.layout, result.samples.data(), result.layout,
                        an::BlendModeRequest{.mode = mode,
                                             .colorEncoding = {.schema = an::ChannelSchema::Rgb,
                                                               .primaries = an::RgbPrimaries::Srgb,
                                                               .transfer = an::ColorTransferFunction::Srgb}});
    EXPECT_TRUE(done.hasValue()) << done.status().message();
    return result;
}

/// The exact Gaussian of a Float32 image, in passes.
[[nodiscard]] Planar<float> gaussian(const an::ExecutionContext& execution, const Planar<float>& source, double sigma,
                                     std::uint32_t passes)
{
    const an::Result<an::GaussianPlan> made =
        an::planGaussianBlur({.sigmaX = sigma / std::sqrt(static_cast<double>(passes)),
                              .sigmaY = sigma / std::sqrt(static_cast<double>(passes))});
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    Planar<float> current = source;
    for (std::uint32_t pass = 0u; pass < passes; ++pass)
    {
        Planar<float> next = empty<float>(source.width, source.height, source.descriptor.extent().channelCount());
        EXPECT_TRUE(an::blurImageGaussian(execution, an::SampleType::Float32, current.samples.data(), current.layout,
                                          next.samples.data(), next.layout, *made)
                        .hasValue());
        current = next;
    }
    return current;
}

[[nodiscard]] float transferred(float value, const an::MidtonesTransferParameters& transfer)
{
    const an::Result<double> mapped =
        an::evaluateMidtonesTransfer(std::clamp(static_cast<double>(value), 0.0, 1.0), transfer);
    EXPECT_TRUE(mapped.hasValue());
    return static_cast<float>(*mapped);
}

[[nodiscard]] double largestDifference(const Planar<float>& left, const Planar<float>& right)
{
    double largest = 0.0;
    for (std::size_t index = 0u; index < left.samples.size(); ++index)
    {
        largest = (std::max)(largest, std::abs(static_cast<double>(left.samples[index]) -
                                               static_cast<double>(right.samples[index])));
    }
    return largest;
}

constexpr std::array<an::BlendCompositionMode, 27u> kEveryMode{
    an::BlendCompositionMode::Replace,      an::BlendCompositionMode::Darken,
    an::BlendCompositionMode::Multiply,     an::BlendCompositionMode::ColorBurn,
    an::BlendCompositionMode::LinearBurn,   an::BlendCompositionMode::DarkerColor,
    an::BlendCompositionMode::Lighten,      an::BlendCompositionMode::Screen,
    an::BlendCompositionMode::ColorDodge,   an::BlendCompositionMode::LinearDodge,
    an::BlendCompositionMode::LighterColor, an::BlendCompositionMode::Overlay,
    an::BlendCompositionMode::SoftLight,    an::BlendCompositionMode::HardLight,
    an::BlendCompositionMode::VividLight,   an::BlendCompositionMode::LinearLight,
    an::BlendCompositionMode::PinLight,     an::BlendCompositionMode::Difference,
    an::BlendCompositionMode::Exclusion,    an::BlendCompositionMode::Subtract,
    an::BlendCompositionMode::Divide,       an::BlendCompositionMode::Power,
    an::BlendCompositionMode::ArcTangent,   an::BlendCompositionMode::Hue,
    an::BlendCompositionMode::Saturation,   an::BlendCompositionMode::Color,
    an::BlendCompositionMode::Lightness};

/// The named formula a mode runs, or nothing for the seven arithmetic modes.
[[nodiscard]] std::optional<an::BlendMode> namedFormula(an::BlendCompositionMode mode)
{
    switch (mode)
    {
    case an::BlendCompositionMode::ColorBurn:
        return an::BlendMode::ColorBurn;
    case an::BlendCompositionMode::LinearBurn:
        return an::BlendMode::LinearBurn;
    case an::BlendCompositionMode::DarkerColor:
        return an::BlendMode::DarkerColor;
    case an::BlendCompositionMode::Screen:
        return an::BlendMode::Screen;
    case an::BlendCompositionMode::ColorDodge:
        return an::BlendMode::ColorDodge;
    case an::BlendCompositionMode::LighterColor:
        return an::BlendMode::LighterColor;
    case an::BlendCompositionMode::Overlay:
        return an::BlendMode::Overlay;
    case an::BlendCompositionMode::SoftLight:
        return an::BlendMode::SoftLight;
    case an::BlendCompositionMode::HardLight:
        return an::BlendMode::HardLight;
    case an::BlendCompositionMode::VividLight:
        return an::BlendMode::VividLight;
    case an::BlendCompositionMode::LinearLight:
        return an::BlendMode::LinearLight;
    case an::BlendCompositionMode::PinLight:
        return an::BlendMode::PinLight;
    case an::BlendCompositionMode::Difference:
        return an::BlendMode::Difference;
    case an::BlendCompositionMode::Exclusion:
        return an::BlendMode::Exclusion;
    case an::BlendCompositionMode::Power:
        return an::BlendMode::Power;
    case an::BlendCompositionMode::ArcTangent:
        return an::BlendMode::ArcTangent;
    case an::BlendCompositionMode::Hue:
        return an::BlendMode::Hue;
    case an::BlendCompositionMode::Saturation:
        return an::BlendMode::Saturation;
    case an::BlendCompositionMode::Color:
        return an::BlendMode::Color;
    case an::BlendCompositionMode::Lightness:
        return an::BlendMode::Lightness;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] double arithmetic(an::BlendCompositionMode mode, double a, double b)
{
    switch (mode)
    {
    case an::BlendCompositionMode::Replace:
        return b;
    case an::BlendCompositionMode::Darken:
        return b < a ? b : a;
    case an::BlendCompositionMode::Lighten:
        return a < b ? b : a;
    case an::BlendCompositionMode::Multiply:
        return a * b;
    case an::BlendCompositionMode::LinearDodge:
        return (std::min)(a + b, 1.0);
    case an::BlendCompositionMode::Subtract:
        return (std::max)(a - b, 0.0);
    case an::BlendCompositionMode::Divide:
        return b == 0.0 ? 0.0 : std::clamp(a / b, 0.0, 1.0);
    default:
        return std::numeric_limits<double>::quiet_NaN();
    }
}

[[nodiscard]] an::ColorEncoding packedLab()
{
    an::ColorEncoding lab;
    lab.schema = an::ChannelSchema::Lab;
    lab.perceptual = an::PerceptualEncoding::UnitPacked;
    return lab;
}

[[nodiscard]] an::ColorEncoding srgb()
{
    return an::ColorEncoding{.schema = an::ChannelSchema::Rgb,
                             .primaries = an::RgbPrimaries::Srgb,
                             .transfer = an::ColorTransferFunction::Srgb};
}

} // namespace

TEST(BlendCompositionTest, ThePlanAndTheChannelsStateWhatIsWritten)
{
    EXPECT_EQ(an::planBlendComposition({.mode = static_cast<an::BlendCompositionMode>(27u)}).status().code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::planBlendComposition({.filter = static_cast<an::BlendPreFilter>(5u)}).status().code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::planBlendComposition({.targetTransfer = {.midtones = 0.5, .shadows = 0.6, .highlights = 0.4}})
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(
        an::planBlendComposition({.filter = an::BlendPreFilter::GaussianBlur, .filterSigma = 0.0}).status().code(),
        an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::planBlendComposition({.filterSaturation = 1.5}).status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::planBlendComposition({.opacity = -0.1}).status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(plan({.filter = an::BlendPreFilter::GaussianBlur, .filterSigma = 10.0}).gaussianPasses(), 1u);
    EXPECT_EQ(plan({.filter = an::BlendPreFilter::HighPass, .filterSigma = 40.0}).gaussianPasses(), 2u);
    EXPECT_EQ(plan({.filter = an::BlendPreFilter::None, .filterSigma = 40.0}).gaussianPasses(), 0u);
    EXPECT_GT(plan({.filter = an::BlendPreFilter::GaussianBlur, .filterSigma = 1000.0}).gaussianPasses(), 900u)
        << "the dialog's widest blur is kept, at the cost of its passes";

    const an::ImageDescriptor grey = describe(an::SampleType::Float32, 5u, 4u, 1u);
    const an::ImageDescriptor color = describe(an::SampleType::Float32, 5u, 4u, 3u);
    EXPECT_EQ(an::blendCompositionChannels(plan(), grey, grey), 1u);
    EXPECT_EQ(an::blendCompositionChannels(plan(), grey, color), 3u);
    EXPECT_EQ(an::blendCompositionChannels(plan(), color, grey), 3u);
    EXPECT_EQ(an::blendCompositionChannels(plan({.mode = an::BlendCompositionMode::Hue}), grey, grey), 3u);
    EXPECT_EQ(an::blendCompositionChannels(plan({.mode = an::BlendCompositionMode::Lightness}), grey, grey), 3u);
    EXPECT_EQ(an::blendCompositionChannels(plan({.filter = an::BlendPreFilter::ColorizeChannels}), grey, grey), 3u);
    const an::Result<an::ImageLayoutRequest> layout =
        an::blendCompositionLayout(plan({.outputSampleType = an::SampleType::UInt8}), grey, color);
    ASSERT_TRUE(layout.hasValue());
    EXPECT_EQ(layout->planeCount, 3u);
    EXPECT_EQ(layout->elementSizeBytes, 1u);
    EXPECT_EQ(an::blendCompositionLayout(plan(), grey, describe(an::SampleType::Float32, 6u, 4u, 1u)).status().code(),
              an::ErrorCode::IncompatibleArguments);
}

// Every one of the twenty-seven modes over two colour images: the twenty named ones are `blendImages`, bit for bit,
// and the seven computed as arithmetic are their formulas.
TEST(BlendCompositionTest, EveryModeIsItsFormula)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    Planar<float> target = random<float>(23u, 11u, 3u, 3u);
    Planar<float> blend = random<float>(23u, 11u, 3u, 5u);
    // The divisor's and the endpoints' special cases are reached.
    blend.at(0u, 1u, 1u) = 0.0f;
    blend.at(1u, 2u, 1u) = 1.0f;
    target.at(2u, 3u, 1u) = 0.0f;
    for (const an::BlendCompositionMode mode : kEveryMode)
    {
        const Planar<float> result = compose(execution, {.mode = mode}, target, blend);
        const std::optional<an::BlendMode> formula = namedFormula(mode);
        if (formula.has_value())
        {
            const Planar<float> expected = named(execution, *formula, target, blend);
            EXPECT_EQ(result.samples, expected.samples) << static_cast<int>(mode);
            continue;
        }
        for (std::size_t index = 0u; index < result.samples.size(); ++index)
        {
            ASSERT_EQ(result.samples[index],
                      static_cast<float>(arithmetic(mode, static_cast<double>(target.samples[index]),
                                                    static_cast<double>(blend.samples[index]))))
                << static_cast<int>(mode) << " at " << index;
        }
    }
}

// A grey image meets a colour one as three equal channels, except a grey blend image under Lightness, whose samples
// are the lightness itself.
TEST(BlendCompositionTest, AGreyImageMeetsAColourOneAsThreeChannelsExceptAsLightness)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const Planar<float> color = random<float>(13u, 7u, 3u, 7u, 0.05, 0.95);
    const Planar<float> grey = random<float>(13u, 7u, 1u, 9u, 0.05, 0.95);
    const Planar<float> multiplied = compose(execution, {.mode = an::BlendCompositionMode::Multiply}, color, grey);
    const Planar<float> overGrey = compose(execution, {.mode = an::BlendCompositionMode::Multiply}, grey, color);
    for (std::uint32_t c = 0u; c < 3u; ++c)
    {
        for (std::uint32_t y = 0u; y < 7u; ++y)
        {
            for (std::uint32_t x = 0u; x < 13u; ++x)
            {
                const double product = static_cast<double>(color.at(c, x, y)) * static_cast<double>(grey.at(0u, x, y));
                ASSERT_EQ(multiplied.at(c, x, y), static_cast<float>(product));
                ASSERT_EQ(overGrey.at(c, x, y), static_cast<float>(product));
            }
        }
    }

    const Planar<float> lightness = compose(execution, {.mode = an::BlendCompositionMode::Lightness}, color, grey);
    const an::Result<an::ColorConversionPlan> toLab = an::planColorConversion(srgb(), packedLab());
    const an::Result<an::ColorConversionPlan> fromLab = an::planColorConversion(packedLab(), srgb());
    ASSERT_TRUE(toLab.hasValue() && fromLab.hasValue());
    double largest = 0.0;
    for (std::uint32_t y = 0u; y < 7u; ++y)
    {
        for (std::uint32_t x = 0u; x < 13u; ++x)
        {
            const std::array<double, 3u> rgb{color.at(0u, x, y), color.at(1u, x, y), color.at(2u, x, y)};
            std::array<double, 3u> lab{};
            ASSERT_TRUE(an::convertColorPixel(*toLab, rgb, lab).ok());
            lab[0] = static_cast<double>(grey.at(0u, x, y));
            std::array<double, 3u> back{};
            ASSERT_TRUE(an::convertColorPixel(*fromLab, lab, back).ok());
            for (std::uint32_t c = 0u; c < 3u; ++c)
            {
                largest = (std::max)(largest, std::abs(static_cast<double>(lightness.at(c, x, y)) -
                                                       std::clamp(back[c], 0.0, 1.0)));
            }
        }
    }
    RecordProperty("largest", std::to_string(largest));
    EXPECT_LE(largest, 2.0e-6);
}

// Each image is stretched by its own transfer before the mode.
TEST(BlendCompositionTest, EachImageIsTransferredBeforeTheMode)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const Planar<float> target = random<float>(17u, 9u, 3u, 11u);
    const Planar<float> blend = random<float>(17u, 9u, 1u, 13u);
    const an::MidtonesTransferParameters targetTransfer{.midtones = 0.2, .shadows = 0.05, .highlights = 0.9};
    const an::MidtonesTransferParameters blendTransfer{.midtones = 0.7, .shadows = 0.0, .highlights = 0.8};
    const Planar<float> result = compose(
        execution,
        {.mode = an::BlendCompositionMode::Multiply, .targetTransfer = targetTransfer, .blendTransfer = blendTransfer},
        target, blend);
    for (std::uint32_t c = 0u; c < 3u; ++c)
    {
        for (std::uint32_t y = 0u; y < 9u; ++y)
        {
            for (std::uint32_t x = 0u; x < 17u; ++x)
            {
                const double expected = static_cast<double>(transferred(target.at(c, x, y), targetTransfer)) *
                                        static_cast<double>(transferred(blend.at(0u, x, y), blendTransfer));
                ASSERT_NEAR(result.at(c, x, y), expected, 1.0e-6) << c << "," << x << "," << y;
            }
        }
    }
}

// The blur and the high pass are the exact Gaussian, in passes whose variances add up to a standard deviation wider
// than one stencil holds.
TEST(BlendCompositionTest, TheBlurAndTheHighPassAreTheExactGaussianInPasses)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const Planar<float> target = random<float>(71u, 53u, 3u, 15u);
    const Planar<float> blend = random<float>(71u, 53u, 3u, 17u, 0.1, 0.9);
    for (const double sigma : {1.7, 40.0})
    {
        const std::uint32_t passes = sigma > 32.0 ? 2u : 1u;
        const Planar<float> blurred = gaussian(execution, blend, sigma, passes);
        const an::BlendCompositionPlan made = plan({.mode = an::BlendCompositionMode::Replace,
                                                    .filter = an::BlendPreFilter::GaussianBlur,
                                                    .filterSigma = sigma});
        Planar<float> result = empty<float>(71u, 53u, 3u);
        const an::Result<an::BlendCompositionResult> done =
            an::composeBlend(execution, made, target.source(), blend.source(), result.samples.data(), result.layout);
        ASSERT_TRUE(done.hasValue()) << done.status().message();
        EXPECT_EQ(done->gaussianPasses, passes);
        EXPECT_EQ(result.samples, blurred.samples) << sigma;

        const Planar<float> highPassed = compose(
            execution,
            {.mode = an::BlendCompositionMode::Replace, .filter = an::BlendPreFilter::HighPass, .filterSigma = sigma},
            target, blend);
        for (std::size_t index = 0u; index < highPassed.samples.size(); ++index)
        {
            ASSERT_EQ(highPassed.samples[index],
                      static_cast<float>(std::clamp(static_cast<double>(blend.samples[index]) -
                                                        static_cast<double>(blurred.samples[index]) + 0.5,
                                                    0.0, 1.0)))
                << sigma << " at " << index;
        }
    }
}

// The colourizing filters are the published colourize: of each channel, or of the CIE lightness.
TEST(BlendCompositionTest, TheColourizingFiltersAreThePublishedColourize)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const Planar<float> target = random<float>(15u, 8u, 3u, 19u);
    const Planar<float> blend = random<float>(15u, 8u, 3u, 21u);
    const an::ColorizationParameters colorize{
        .hueDegrees = 210.0, .saturation = 0.6, .model = an::ColorizationModel::Hsv};

    const Planar<float> channels = compose(execution,
                                           {.mode = an::BlendCompositionMode::Replace,
                                            .filter = an::BlendPreFilter::ColorizeChannels,
                                            .filterHueDegrees = 210.0,
                                            .filterSaturation = 0.6},
                                           target, blend);
    Planar<float> expected = empty<float>(15u, 8u, 3u);
    ASSERT_TRUE(an::colorizeImage(execution, an::SampleType::Float32, blend.samples.data(), blend.layout,
                                  expected.samples.data(), expected.layout, colorize)
                    .hasValue());
    EXPECT_EQ(channels.samples, expected.samples);

    const Planar<float> lightness = compose(execution,
                                            {.mode = an::BlendCompositionMode::Replace,
                                             .filter = an::BlendPreFilter::ColorizeLightness,
                                             .filterHueDegrees = 210.0,
                                             .filterSaturation = 0.6},
                                            target, blend);
    const an::Result<an::ColorConversionPlan> toLab = an::planColorConversion(srgb(), packedLab());
    ASSERT_TRUE(toLab.hasValue());
    Planar<float> lab = blend;
    ASSERT_TRUE(an::convertImageColor(execution, *toLab, an::SampleType::Float32, lab.samples.data(), lab.layout,
                                      lab.samples.data(), lab.layout)
                    .ok());
    Planar<float> plane = empty<float>(15u, 8u, 1u);
    std::memcpy(plane.samples.data(), lab.samples.data(), plane.samples.size() * sizeof(float));
    Planar<float> colorized = empty<float>(15u, 8u, 3u);
    ASSERT_TRUE(an::colorizeImage(execution, an::SampleType::Float32, plane.samples.data(), plane.layout,
                                  colorized.samples.data(), colorized.layout, colorize)
                    .hasValue());
    EXPECT_EQ(lightness.samples, colorized.samples);
}

// The kept lightness is the transferred target's CIE lightness under the composite's opponent channels, the colour
// held to the unit interval.
TEST(BlendCompositionTest, TheKeptLightnessIsTheTargets)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const Planar<float> target = random<float>(19u, 9u, 3u, 23u, 0.2, 0.6);
    const Planar<float> blend = random<float>(19u, 9u, 3u, 25u, 0.2, 0.6);
    const Planar<float> kept =
        compose(execution, {.mode = an::BlendCompositionMode::Screen, .preserveLightness = true}, target, blend);
    const Planar<float> screened = compose(execution, {.mode = an::BlendCompositionMode::Screen}, target, blend);
    const an::Result<an::ColorConversionPlan> toLab = an::planColorConversion(srgb(), packedLab());
    const an::Result<an::ColorConversionPlan> fromLab = an::planColorConversion(packedLab(), srgb());
    ASSERT_TRUE(toLab.hasValue() && fromLab.hasValue());
    double largest = 0.0;
    double lightnessMoved = 0.0;
    for (std::uint32_t y = 0u; y < 9u; ++y)
    {
        for (std::uint32_t x = 0u; x < 19u; ++x)
        {
            std::array<double, 3u> fromTarget{};
            std::array<double, 3u> fromScreen{};
            ASSERT_TRUE(an::convertColorPixel(*toLab, {{target.at(0u, x, y), target.at(1u, x, y), target.at(2u, x, y)}},
                                              fromTarget)
                            .ok());
            ASSERT_TRUE(an::convertColorPixel(
                            *toLab, {{screened.at(0u, x, y), screened.at(1u, x, y), screened.at(2u, x, y)}}, fromScreen)
                            .ok());
            lightnessMoved = (std::max)(lightnessMoved, std::abs(fromScreen[0] - fromTarget[0]));
            std::array<double, 3u> expected{};
            ASSERT_TRUE(
                an::convertColorPixel(*fromLab, {{fromTarget[0], fromScreen[1], fromScreen[2]}}, expected).ok());
            for (std::uint32_t c = 0u; c < 3u; ++c)
            {
                largest = (std::max)(largest, std::abs(static_cast<double>(kept.at(c, x, y)) -
                                                       std::clamp(expected[c], 0.0, 1.0)));
            }
        }
    }
    RecordProperty("largest", std::to_string(largest));
    EXPECT_LE(largest, 5.0e-6);
    EXPECT_GT(lightnessMoved, 0.05) << "the mode moves the lightness the option keeps";
}

// The opacity lays the composite over the target as it was before its transfer.
TEST(BlendCompositionTest, TheOpacityLaysTheCompositeOverTheUntransferredTarget)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const Planar<float> target = random<float>(14u, 6u, 1u, 27u);
    const Planar<float> blend = random<float>(14u, 6u, 3u, 29u);
    const an::MidtonesTransferParameters transfer{.midtones = 0.3};
    const Planar<float> full =
        compose(execution, {.mode = an::BlendCompositionMode::Difference, .targetTransfer = transfer}, target, blend);
    const Planar<float> laid =
        compose(execution, {.mode = an::BlendCompositionMode::Difference, .targetTransfer = transfer, .opacity = 0.35},
                target, blend);
    for (std::uint32_t c = 0u; c < 3u; ++c)
    {
        for (std::uint32_t y = 0u; y < 6u; ++y)
        {
            for (std::uint32_t x = 0u; x < 14u; ++x)
            {
                ASSERT_EQ(laid.at(c, x, y), static_cast<float>(static_cast<double>(target.at(0u, x, y)) * (1.0 - 0.35) +
                                                               static_cast<double>(full.at(c, x, y)) * 0.35));
            }
        }
    }
}

// A blend that needs no working copy - both images already the result's floating-point representation and channel
// count, nothing transferred, filtered, kept or laid over, every sample selected - runs straight from its images into
// the destination. The finite-only rule selects the same samples of these images through the working route, so the
// two must write one answer, bit for bit, for every mode, for grey and colour images, in Float32 and Float64,
// including samples at both ends of the range.
TEST(BlendCompositionTest, TheDirectRouteIsTheWorkingRoute)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::optional<an::ValidityDescriptor> finite =
        an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::FiniteOnly});
    ASSERT_TRUE(finite.has_value());
    const auto compare = [&]<typename SampleT>(std::uint32_t channels, std::uint64_t seed) {
        Planar<SampleT> target = random<SampleT>(19u, 7u, channels, seed);
        Planar<SampleT> blend = random<SampleT>(19u, 7u, channels, seed + 1u);
        for (std::size_t index = 0u; index < target.samples.size(); index += 5u)
        {
            target.samples[index] = SampleT{0};
            blend.samples[(index + 2u) % blend.samples.size()] = SampleT{1};
        }
        for (std::uint32_t mode = 0u; mode < 27u; ++mode)
        {
            const an::BlendCompositionRequest request{.mode = static_cast<an::BlendCompositionMode>(mode),
                                                      .outputSampleType = an::kSampleTypeOf<SampleT>};
            const Planar<SampleT> direct = compose<SampleT>(execution, request, target, blend);
            const Planar<SampleT> working =
                compose<SampleT>(execution, request, target, blend, an::KernelValidityView{.descriptor = *finite});
            ASSERT_EQ(direct.samples.size(), working.samples.size());
            std::uint32_t differing = 0u;
            for (std::size_t index = 0u; index < direct.samples.size(); ++index)
            {
                differing +=
                    std::memcmp(&direct.samples[index], &working.samples[index], sizeof(SampleT)) != 0 ? 1u : 0u;
            }
            EXPECT_EQ(differing, 0u) << "mode " << mode << ", " << channels << " channels, " << sizeof(SampleT)
                                     << "-byte samples";
        }
    };
    for (const std::uint32_t channels : {1u, 3u})
    {
        compare.template operator()<float>(channels, 61u + channels);
        compare.template operator()<double>(channels, 71u + channels);
    }
}

// A pixel either image rejects keeps the target's samples, and is counted.
TEST(BlendCompositionTest, AnInvalidPixelKeepsTheTarget)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    Planar<float> target = random<float>(9u, 5u, 3u, 31u);
    target.at(2u, 4u, 3u) = std::numeric_limits<float>::infinity();
    const Planar<float> blend = random<float>(9u, 5u, 3u, 33u);
    const std::optional<an::ValidityDescriptor> finite =
        an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::FiniteOnly});
    ASSERT_TRUE(finite.has_value());
    const an::BlendCompositionPlan made = plan({.mode = an::BlendCompositionMode::Overlay, .opacity = 0.5});
    Planar<float> result = empty<float>(9u, 5u, 3u);
    const an::Result<an::BlendCompositionResult> done =
        an::composeBlend(execution, made, target.source(an::KernelValidityView{.descriptor = *finite}), blend.source(),
                         result.samples.data(), result.layout);
    ASSERT_TRUE(done.hasValue()) << done.status().message();
    EXPECT_EQ(done->run.selection.valid, 44u);
    EXPECT_EQ(result.at(0u, 4u, 3u), target.at(0u, 4u, 3u));
    EXPECT_EQ(result.at(1u, 4u, 3u), target.at(1u, 4u, 3u));
    EXPECT_EQ(result.at(2u, 4u, 3u), 0.0f) << "a sample the target rejects is zero";
    EXPECT_EQ(an::composeBlend(execution, made, target.source(), blend.source(), result.samples.data(), result.layout)
                  .status()
                  .code(),
              an::ErrorCode::InvalidData);
}

// Integer images are read at full scale and the result is written in the stated representation.
TEST(BlendCompositionTest, IntegerImagesAreReadAtFullScaleAndWrittenAsStated)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const Planar<std::uint16_t> target = random<std::uint16_t>(12u, 5u, 3u, 35u);
    const Planar<std::uint8_t> blend = random<std::uint8_t>(12u, 5u, 3u, 37u);
    const Planar<std::uint16_t> result = compose<std::uint16_t>(
        execution, {.mode = an::BlendCompositionMode::Screen, .outputSampleType = an::SampleType::UInt16}, target,
        blend);
    for (std::uint32_t c = 0u; c < 3u; ++c)
    {
        for (std::uint32_t y = 0u; y < 5u; ++y)
        {
            for (std::uint32_t x = 0u; x < 12u; ++x)
            {
                const auto a = static_cast<float>(static_cast<double>(target.at(c, x, y)) / 65535.0);
                const auto b = static_cast<float>(static_cast<double>(blend.at(c, x, y)) / 255.0);
                const double screen =
                    *an::evaluateBlendSample(an::BlendMode::Screen, static_cast<double>(a), static_cast<double>(b));
                ASSERT_EQ(result.at(c, x, y),
                          (an::convertSample<float, std::uint16_t>(static_cast<float>(screen), an::ConversionPolicy{})
                               .value));
            }
        }
    }
}

// An image not in a normalized domain, two of different widths, and an overlapping destination are refused.
TEST(BlendCompositionTest, MismatchedInputsAreRefused)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::BlendCompositionPlan made = plan();
    const Planar<float> target = random<float>(8u, 5u, 3u, 39u);
    Planar<float> result = empty<float>(8u, 5u, 3u);
    const Planar<float> physical = random<float>(8u, 5u, 3u, 41u, 0.0, 1.0, an::NumericDomain::PhysicalScaled);
    EXPECT_EQ(
        an::composeBlend(execution, made, target.source(), physical.source(), result.samples.data(), result.layout)
            .status()
            .code(),
        an::ErrorCode::IncompatibleArguments);
    const Planar<float> wide = random<float>(9u, 5u, 3u, 43u);
    EXPECT_EQ(an::composeBlend(execution, made, target.source(), wide.source(), result.samples.data(), result.layout)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
    const Planar<float> four = random<float>(8u, 5u, 1u, 45u);
    Planar<float> wrongChannels = empty<float>(8u, 5u, 1u);
    EXPECT_EQ(an::composeBlend(execution, made, target.source(), four.source(), wrongChannels.samples.data(),
                               wrongChannels.layout)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(an::composeBlend(execution, made, target.source(), target.source(),
                               const_cast<float*>(target.samples.data()), target.layout)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
}

// The result does not depend on the number of threads.
TEST(BlendCompositionTest, TheResultDoesNotDependOnTheNumberOfThreads)
{
    const an::EngineContext engine = makeEngine(8u);
    const an::ExecutionContext one = makeExecution(engine, 1u);
    const an::ExecutionContext eight = makeExecution(engine, 8u);
    const Planar<float> target = random<float>(233u, 101u, 3u, 47u);
    const Planar<float> blend = random<float>(233u, 101u, 3u, 49u);
    const an::BlendCompositionRequest request{.mode = an::BlendCompositionMode::SoftLight,
                                              .targetTransfer = {.midtones = 0.4},
                                              .filter = an::BlendPreFilter::HighPass,
                                              .filterSigma = 3.0,
                                              .preserveLightness = true,
                                              .opacity = 0.8};
    const Planar<float> serial = compose(one, request, target, blend);
    const Planar<float> parallel = compose(eight, request, target, blend);
    EXPECT_EQ(std::memcmp(serial.samples.data(), parallel.samples.data(), serial.samples.size() * sizeof(float)), 0);
    EXPECT_GT(largestDifference(serial, target), 0.01) << "the comparison is of real work";
}
