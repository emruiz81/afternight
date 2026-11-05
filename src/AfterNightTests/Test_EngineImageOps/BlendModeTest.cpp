// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for every named blend mode.

#include <AfterNightEngine/Algorithms/Pointwise/BlendMode.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
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

[[nodiscard]] double scalarOracle(an::BlendMode mode, double base, double blend)
{
    switch (mode)
    {
    case an::BlendMode::ColorBurn:
        return blend == 0.0 ? 0.0 : 1.0 - (std::min)((1.0 - base) / blend, 1.0);
    case an::BlendMode::LinearBurn:
        return std::clamp(base + blend - 1.0, 0.0, 1.0);
    case an::BlendMode::DarkerColor:
        return (std::min)(base, blend);
    case an::BlendMode::LighterColor:
        return (std::max)(base, blend);
    case an::BlendMode::Screen:
        return 1.0 - (1.0 - base) * (1.0 - blend);
    case an::BlendMode::UnscreenStars:
        return std::clamp((base - blend) / (1.0 - blend), 0.0, 1.0);
    case an::BlendMode::ColorDodge:
        return std::clamp(base / (1.0 - blend), 0.0, 1.0);
    case an::BlendMode::Overlay:
        return base > 0.5 ? 1.0 - 2.0 * (1.0 - base) * (1.0 - blend) : 2.0 * base * blend;
    case an::BlendMode::SoftLight:
        return std::clamp((1.0 - 2.0 * blend) * base * base + 2.0 * base * blend, 0.0, 1.0);
    case an::BlendMode::HardLight:
        return blend > 0.5 ? 1.0 - 2.0 * (1.0 - base) * (1.0 - blend) : 2.0 * base * blend;
    case an::BlendMode::VividLight:
        return blend < 0.5 ? (std::max)(0.0, 1.0 - (1.0 - base) / (2.0 * blend))
                           : (std::min)(1.0, base / (2.0 * (1.0 - blend)));
    case an::BlendMode::LinearLight:
        return std::clamp(2.0 * blend + base - 1.0, 0.0, 1.0);
    case an::BlendMode::PinLight:
        return std::clamp((std::max)(2.0 * blend - 1.0, (std::min)(base, 2.0 * blend)), 0.0, 1.0);
    case an::BlendMode::Difference:
        return std::abs(base - blend);
    case an::BlendMode::Exclusion:
        return std::clamp(0.5 - 2.0 * (base - 0.5) * (blend - 0.5), 0.0, 1.0);
    case an::BlendMode::Power:
        return std::pow(base, blend);
    case an::BlendMode::ArcTangent:
        return std::clamp(std::atan2(base, blend), 0.0, 1.0);
    case an::BlendMode::Hue:
    case an::BlendMode::Saturation:
    case an::BlendMode::Color:
    case an::BlendMode::Lightness:
        break;
    }
    return -1.0;
}

TEST(BlendModeTest, EverySeparableModeMatchesItsFormula)
{
    constexpr std::array modes{an::BlendMode::ColorBurn,    an::BlendMode::LinearBurn, an::BlendMode::DarkerColor,
                               an::BlendMode::LighterColor, an::BlendMode::Screen,     an::BlendMode::UnscreenStars,
                               an::BlendMode::ColorDodge,   an::BlendMode::Overlay,    an::BlendMode::SoftLight,
                               an::BlendMode::HardLight,    an::BlendMode::VividLight, an::BlendMode::LinearLight,
                               an::BlendMode::PinLight,     an::BlendMode::Difference, an::BlendMode::Exclusion,
                               an::BlendMode::Power,        an::BlendMode::ArcTangent};
    constexpr double base = 0.63;
    constexpr double blend = 0.37;
    for (const an::BlendMode mode : modes)
    {
        const an::Result<double> value = an::evaluateBlendSample(mode, base, blend);
        ASSERT_TRUE(value.hasValue()) << static_cast<int>(mode);
        EXPECT_NEAR(*value, scalarOracle(mode, base, blend), 1.0e-14) << static_cast<int>(mode);
    }
    EXPECT_DOUBLE_EQ(*an::evaluateBlendSample(an::BlendMode::ColorDodge, 0.4, 1.0), 0.0);
    EXPECT_EQ(an::evaluateBlendSample(an::BlendMode::Hue, base, blend).status().code(), an::ErrorCode::InvalidArgument);
}

TEST(BlendModeTest, SeparableExecutionSupportsMixedLayoutsAndExactInPlaceOutput)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout planar = layoutOf(3u, 2u, 3u);
    const an::ImageLayout interleaved = layoutOf(3u, 2u, 3u, true);
    std::vector<float> base(planar.totalBytes() / sizeof(float), 0.25f);
    std::vector<float> blend(interleaved.totalBytes() / sizeof(float), 0.5f);

    const an::Result<an::BlendModeResult> result =
        an::blendImages(execution, an::SampleType::Float32, base.data(), planar, blend.data(), interleaved, base.data(),
                        planar, an::BlendModeRequest{.mode = an::BlendMode::Screen});
    ASSERT_TRUE(result.hasValue()) << result.status().message();
    EXPECT_EQ(result->run.selection.valid, 6u);
    EXPECT_EQ(result->run.outputsWritten, 18u);
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        for (std::uint32_t y = 0u; y < 2u; ++y)
        {
            for (std::uint32_t x = 0u; x < 3u; ++x)
            {
                EXPECT_FLOAT_EQ(sampleAt(base, planar, channel, x, y), 0.625f);
            }
        }
    }
}

TEST(BlendModeTest, WholeColorSelectionCopiesOneCompleteRgbPixel)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout layout = layoutOf(1u, 1u, 3u);
    std::vector<float> dark(layout.totalBytes() / sizeof(float), 0.0f);
    std::vector<float> light(layout.totalBytes() / sizeof(float), 0.0f);
    std::vector<float> output(layout.totalBytes() / sizeof(float), 0.0f);
    sampleAt(dark, layout, 0u, 0u, 0u) = 0.1f;
    sampleAt(dark, layout, 1u, 0u, 0u) = 0.2f;
    sampleAt(dark, layout, 2u, 0u, 0u) = 0.3f;
    sampleAt(light, layout, 0u, 0u, 0u) = 0.8f;
    sampleAt(light, layout, 1u, 0u, 0u) = 0.7f;
    sampleAt(light, layout, 2u, 0u, 0u) = 0.6f;

    ASSERT_TRUE(an::blendImages(execution, an::SampleType::Float32, dark.data(), layout, light.data(), layout,
                                output.data(), layout, an::BlendModeRequest{.mode = an::BlendMode::DarkerColor})
                    .hasValue());
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        EXPECT_FLOAT_EQ(sampleAt(output, layout, channel, 0u, 0u), sampleAt(dark, layout, channel, 0u, 0u));
    }
    ASSERT_TRUE(an::blendImages(execution, an::SampleType::Float32, dark.data(), layout, light.data(), layout,
                                output.data(), layout, an::BlendModeRequest{.mode = an::BlendMode::LighterColor})
                    .hasValue());
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        EXPECT_FLOAT_EQ(sampleAt(output, layout, channel, 0u, 0u), sampleAt(light, layout, channel, 0u, 0u));
    }
}

TEST(BlendModeTest, HsiAndLabModesTransferTheNamedComponents)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout layout = layoutOf(1u, 1u, 3u);
    std::vector<float> base(layout.totalBytes() / sizeof(float), 0.0f);
    std::vector<float> blend(layout.totalBytes() / sizeof(float), 0.0f);
    std::vector<float> output(layout.totalBytes() / sizeof(float), 0.0f);
    sampleAt(base, layout, 0u, 0u, 0u) = 1.0f;
    sampleAt(blend, layout, 1u, 0u, 0u) = 1.0f;

    ASSERT_TRUE(an::blendImages(execution, an::SampleType::Float32, base.data(), layout, blend.data(), layout,
                                output.data(), layout, an::BlendModeRequest{.mode = an::BlendMode::Hue})
                    .hasValue());
    EXPECT_NEAR(sampleAt(output, layout, 0u, 0u, 0u), 0.0, 1.0e-6);
    EXPECT_NEAR(sampleAt(output, layout, 1u, 0u, 0u), 1.0, 1.0e-6);
    EXPECT_NEAR(sampleAt(output, layout, 2u, 0u, 0u), 0.0, 1.0e-6);

    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        sampleAt(blend, layout, channel, 0u, 0u) = 0.4f;
    }
    ASSERT_TRUE(an::blendImages(execution, an::SampleType::Float32, base.data(), layout, blend.data(), layout,
                                output.data(), layout, an::BlendModeRequest{.mode = an::BlendMode::Saturation})
                    .hasValue());
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        EXPECT_NEAR(sampleAt(output, layout, channel, 0u, 0u), 1.0 / 3.0, 1.0e-6);
    }

    for (const an::BlendMode mode : {an::BlendMode::Color, an::BlendMode::Lightness})
    {
        ASSERT_TRUE(an::blendImages(execution, an::SampleType::Float32, base.data(), layout, base.data(), layout,
                                    output.data(), layout, an::BlendModeRequest{.mode = mode})
                        .hasValue());
        for (std::uint32_t channel = 0u; channel < 3u; ++channel)
        {
            EXPECT_NEAR(sampleAt(output, layout, channel, 0u, 0u), sampleAt(base, layout, channel, 0u, 0u), 2.0e-6);
        }
    }
}

// When both images select every sample the blend takes walks of its own, written for speed: a separable mode runs a
// channel's row with its formula fixed at compile time, and a colour-aware mode runs the planned conversions with
// their constants read once. The finite-only rule selects the same samples of these images through the general walk,
// so the two runs must write one answer, bit for bit, for every mode, channel count and layout - including grey
// pixels, black, ties between the two images and samples at both ends of the range.
TEST(BlendModeTest, TheWalkOverEverySelectedSampleIsTheGeneralWalk)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::optional<an::ValidityDescriptor> finite =
        an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::FiniteOnly});
    ASSERT_TRUE(finite.has_value());
    constexpr std::array modes{an::BlendMode::ColorBurn,    an::BlendMode::LinearBurn, an::BlendMode::DarkerColor,
                               an::BlendMode::LighterColor, an::BlendMode::Screen,     an::BlendMode::UnscreenStars,
                               an::BlendMode::ColorDodge,   an::BlendMode::Overlay,    an::BlendMode::SoftLight,
                               an::BlendMode::HardLight,    an::BlendMode::VividLight, an::BlendMode::LinearLight,
                               an::BlendMode::PinLight,     an::BlendMode::Difference, an::BlendMode::Exclusion,
                               an::BlendMode::Power,        an::BlendMode::ArcTangent, an::BlendMode::Hue,
                               an::BlendMode::Saturation,   an::BlendMode::Color,      an::BlendMode::Lightness};
    constexpr std::uint32_t width = 37u;
    constexpr std::uint32_t height = 11u;
    for (const std::uint32_t channels : {1u, 3u, 4u})
    {
        for (const bool interleaved : {false, true})
        {
            const an::ImageLayout layout = layoutOf(width, height, channels, interleaved);
            std::vector<float> base(layout.totalBytes() / sizeof(float), 0.0f);
            std::vector<float> blend(layout.totalBytes() / sizeof(float), 0.0f);
            std::uint32_t state = 12345u + channels * 2u + (interleaved ? 1u : 0u);
            const auto next = [&state] {
                state = state * 1664525u + 1013904223u;
                return static_cast<float>(state >> 8u) / 16777216.0f;
            };
            for (std::uint32_t y = 0u; y < height; ++y)
            {
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    const std::uint32_t pixel = y * width + x;
                    const float grey = next();
                    for (std::uint32_t channel = 0u; channel < channels; ++channel)
                    {
                        float baseValue = pixel % 7u == 3u ? grey : next();
                        float blendValue = next();
                        if (pixel == 0u)
                        {
                            baseValue = 0.0f;
                        }
                        else if (pixel == 1u)
                        {
                            baseValue = 1.0f;
                            blendValue = 1.0f;
                        }
                        else if (pixel % 5u == 2u)
                        {
                            blendValue = baseValue;
                        }
                        else if (pixel % 11u == 4u)
                        {
                            blendValue = std::nextafter(baseValue, 1.0f);
                        }
                        sampleAt(base, layout, channel, x, y) = baseValue;
                        sampleAt(blend, layout, channel, x, y) = blendValue;
                    }
                }
            }
            // Linear, sRGB and Rec. 709 encodings: the colour walks take a curve, skip it where a clamp decides,
            // and order darker and lighter colour by luminance bounds only under sRGB.
            for (const an::ColorTransferFunction transfer :
                 {an::ColorTransferFunction::Linear, an::ColorTransferFunction::Srgb,
                  an::ColorTransferFunction::Rec709})
            {
                an::ColorEncoding encoding{};
                encoding.schema = channels == 4u ? an::ChannelSchema::Rgba : an::ChannelSchema::Rgb;
                encoding.transfer = transfer;
                for (const an::BlendMode mode : modes)
                {
                    const bool component = mode == an::BlendMode::Hue || mode == an::BlendMode::Saturation ||
                                           mode == an::BlendMode::Color || mode == an::BlendMode::Lightness;
                    if (component && channels == 1u)
                    {
                        continue;
                    }
                    const an::BlendModeRequest request{.mode = mode, .colorEncoding = encoding};
                    std::vector<float> selected(base.size(), -1.0f);
                    std::vector<float> general(base.size(), -2.0f);
                    const an::Result<an::BlendModeResult> fast =
                        an::blendImages(execution, an::SampleType::Float32, base.data(), layout, blend.data(), layout,
                                        selected.data(), layout, request);
                    ASSERT_TRUE(fast.hasValue()) << fast.status().message();
                    const an::Result<an::BlendModeResult> walked =
                        an::blendImages(execution, an::SampleType::Float32, base.data(), layout, blend.data(), layout,
                                        general.data(), layout, request, an::KernelValidityView{.descriptor = *finite},
                                        an::KernelValidityView{.descriptor = *finite});
                    ASSERT_TRUE(walked.hasValue()) << walked.status().message();
                    EXPECT_EQ(fast->run.selection.valid, walked->run.selection.valid);
                    EXPECT_EQ(fast->run.outputsWritten, walked->run.outputsWritten);
                    std::uint32_t differing = 0u;
                    for (std::size_t index = 0u; index < selected.size(); ++index)
                    {
                        differing += std::bit_cast<std::uint32_t>(selected[index]) !=
                                             std::bit_cast<std::uint32_t>(general[index])
                                         ? 1u
                                         : 0u;
                    }
                    EXPECT_EQ(differing, 0u)
                        << "mode " << static_cast<int>(mode) << ", " << channels << " channels, "
                        << (interleaved ? "interleaved" : "planar") << ", transfer " << static_cast<int>(transfer);
                }
            }
        }
    }

    // Both walks refuse a sample that is not finite.
    const an::ImageLayout layout = layoutOf(4u, 3u, 3u);
    std::vector<float> base(layout.totalBytes() / sizeof(float), 0.5f);
    std::vector<float> blend(layout.totalBytes() / sizeof(float), 0.25f);
    std::vector<float> output(layout.totalBytes() / sizeof(float), 0.0f);
    sampleAt(blend, layout, 1u, 2u, 1u) = std::numeric_limits<float>::quiet_NaN();
    for (const an::BlendMode mode : {an::BlendMode::Screen, an::BlendMode::DarkerColor, an::BlendMode::Lightness})
    {
        EXPECT_EQ(an::blendImages(execution, an::SampleType::Float32, base.data(), layout, blend.data(), layout,
                                  output.data(), layout, an::BlendModeRequest{.mode = mode})
                      .status()
                      .code(),
                  an::ErrorCode::InvalidData)
            << static_cast<int>(mode);
    }
}

} // namespace
