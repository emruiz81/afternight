// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the color-adjustment surface.

#include <AfterNightEngine/Algorithms/Color/ColorAdjustment.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/CpuCapabilities.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
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

[[nodiscard]] an::ColorEncoding srgb()
{
    an::ColorEncoding encoding;
    encoding.transfer = an::ColorTransferFunction::Srgb;
    return encoding;
}

} // namespace

TEST(ColorAdjustmentTest, DirectAndPrecomputedHslPathsProduceTheSameRgb)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout planar = layoutOf(3u, 1u, 3u);
    const an::ImageLayout interleaved = layoutOf(3u, 1u, 3u, true);
    std::vector<float> rgb(planar.totalBytes() / sizeof(float), 0.0f);
    std::vector<float> hsl(interleaved.totalBytes() / sizeof(float), 0.0f);
    std::vector<float> direct(interleaved.totalBytes() / sizeof(float), 0.0f);
    std::vector<float> precomputed(planar.totalBytes() / sizeof(float), 0.0f);
    for (std::uint32_t x = 0u; x < 3u; ++x)
    {
        sampleAt(rgb, planar, 0u, x, 0u) = 0.15f + 0.2f * static_cast<float>(x);
        sampleAt(rgb, planar, 1u, x, 0u) = 0.65f - 0.1f * static_cast<float>(x);
        sampleAt(rgb, planar, 2u, x, 0u) = 0.30f + 0.1f * static_cast<float>(x);
    }

    const an::ColorEncoding rgbEncoding = srgb();
    an::ColorEncoding hslEncoding = rgbEncoding;
    hslEncoding.schema = an::ChannelSchema::Hsl;
    const an::Result<an::ColorConversionPlan> toHsl = an::planColorConversion(rgbEncoding, hslEncoding);
    ASSERT_TRUE(toHsl.hasValue()) << toHsl.status().message();
    ASSERT_TRUE(
        an::convertImageColor(execution, *toHsl, an::SampleType::Float32, rgb.data(), planar, hsl.data(), interleaved)
            .ok());

    const an::HslAdjustment adjustment{
        .hueOffsetTurns = -45.0 / 360.0, .saturationScale = 1.0 + 30.0 / 25.0, .lightnessOffset = -12.0 / 100.0};
    const an::Result<an::ColorAdjustmentResult> fromRgb =
        an::adjustImageHsl(execution, an::SampleType::Float32, rgb.data(), planar, direct.data(), interleaved,
                           rgbEncoding, rgbEncoding, adjustment);
    const an::Result<an::ColorAdjustmentResult> fromHsl =
        an::adjustImageHsl(execution, an::SampleType::Float32, hsl.data(), interleaved, precomputed.data(), planar,
                           hslEncoding, rgbEncoding, adjustment);
    ASSERT_TRUE(fromRgb.hasValue()) << fromRgb.status().message();
    ASSERT_TRUE(fromHsl.hasValue()) << fromHsl.status().message();
    EXPECT_EQ(fromRgb->run.outputsWritten, 9u);
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        for (std::uint32_t x = 0u; x < 3u; ++x)
        {
            EXPECT_NEAR(sampleAt(direct, interleaved, channel, x, 0u), sampleAt(precomputed, planar, channel, x, 0u),
                        2.0e-6);
        }
    }
}

TEST(ColorAdjustmentTest, DirectAndPrecomputedLchPathsUseOneFullPrecisionContract)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout layout = layoutOf(2u, 1u, 3u);
    std::vector<float> rgb(layout.totalBytes() / sizeof(float), 0.0f);
    std::vector<float> lch(layout.totalBytes() / sizeof(float), 0.0f);
    std::vector<float> direct(layout.totalBytes() / sizeof(float), 0.0f);
    std::vector<float> precomputed(layout.totalBytes() / sizeof(float), 0.0f);
    const std::array<std::array<float, 3u>, 2u> colors{{{0.15f, 0.35f, 0.75f}, {0.8f, 0.3f, 0.1f}}};
    for (std::uint32_t x = 0u; x < 2u; ++x)
    {
        for (std::uint32_t channel = 0u; channel < 3u; ++channel)
        {
            sampleAt(rgb, layout, channel, x, 0u) = colors[x][channel];
        }
    }

    const an::ColorEncoding rgbEncoding = srgb();
    an::ColorEncoding lchEncoding;
    lchEncoding.schema = an::ChannelSchema::Lch;
    const an::Result<an::ColorConversionPlan> toLch = an::planColorConversion(rgbEncoding, lchEncoding);
    ASSERT_TRUE(toLch.hasValue()) << toLch.status().message();
    ASSERT_TRUE(
        an::convertImageColor(execution, *toLch, an::SampleType::Float32, rgb.data(), layout, lch.data(), layout).ok());

    const an::LchAdjustment adjustment{.lightnessOffset = 8.0, .chromaScale = 1.4, .hueOffsetDegrees = 25.0};
    ASSERT_TRUE(an::adjustImageLch(execution, an::SampleType::Float32, rgb.data(), layout, direct.data(), layout,
                                   rgbEncoding, rgbEncoding, adjustment)
                    .hasValue());
    ASSERT_TRUE(an::adjustImageLch(execution, an::SampleType::Float32, lch.data(), layout, precomputed.data(), layout,
                                   lchEncoding, rgbEncoding, adjustment)
                    .hasValue());
    for (std::size_t index = 0u; index < direct.size(); ++index)
    {
        EXPECT_NEAR(direct[index], precomputed[index], 3.0e-5);
        EXPECT_GE(direct[index], 0.0f);
        EXPECT_LE(direct[index], 1.0f);
    }
}

TEST(ColorAdjustmentTest, BackgroundDerivationNamesTheMeanAndMedianChoices)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout layout = layoutOf(4u, 1u, 3u);
    std::vector<float> rgb(layout.totalBytes() / sizeof(float), 0.0f);
    constexpr std::array<float, 4u> green{0.1f, 0.2f, 0.3f, 0.9f};
    for (std::uint32_t x = 0u; x < green.size(); ++x)
    {
        sampleAt(rgb, layout, 1u, x, 0u) = green[x];
    }
    const double mean = 0.375;
    const double sigma = std::sqrt((0.075625 + 0.030625 + 0.005625 + 0.275625) / 4.0);
    const an::Result<double> fromMean = an::deriveSaturationEnhancementBackground(
        execution, an::SampleType::Float32, rgb.data(), layout, 0.5, an::SaturationBackgroundLocation::Mean);
    const an::Result<double> fromMedian = an::deriveSaturationEnhancementBackground(
        execution, an::SampleType::Float32, rgb.data(), layout, 0.5, an::SaturationBackgroundLocation::Median);
    ASSERT_TRUE(fromMean.hasValue()) << fromMean.status().message();
    ASSERT_TRUE(fromMedian.hasValue()) << fromMedian.status().message();
    EXPECT_NEAR(*fromMean, (mean + sigma) * 0.5, 1.0e-7);
    EXPECT_NEAR(*fromMedian, (0.25 + sigma) * 0.5, 1.0e-7);
    EXPECT_DOUBLE_EQ(*an::deriveSaturationEnhancementBackground(execution, an::SampleType::Float32, nullptr, layout,
                                                                0.0, an::SaturationBackgroundLocation::Median),
                     0.0);
}

TEST(ColorAdjustmentTest, SelectiveSaturationRetainsTheWrappingHueInterval)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout layout = layoutOf(2u, 1u, 3u, true);
    std::vector<float> pixels(layout.totalBytes() / sizeof(float), 0.0f);
    std::vector<float> output(pixels.size(), 0.0f);
    const std::array<std::array<float, 3u>, 2u> input{{{0.7f, 0.4f, 0.4f}, {0.4f, 0.7f, 0.4f}}};
    for (std::uint32_t x = 0u; x < 2u; ++x)
    {
        for (std::uint32_t channel = 0u; channel < 3u; ++channel)
        {
            sampleAt(pixels, layout, channel, x, 0u) = input[x][channel];
        }
    }
    const an::SaturationEnhancementParameters request{
        .coefficient = 1.0, .background = 0.0, .hueMinimumDegrees = 330.0, .hueMaximumDegrees = 30.0};
    ASSERT_TRUE(an::enhanceImageSaturation(execution, an::SampleType::Float32, pixels.data(), layout, output.data(),
                                           layout, request)
                    .hasValue());
    EXPECT_GT(sampleAt(output, layout, 0u, 0u, 0u), input[0][0]);
    EXPECT_LT(sampleAt(output, layout, 1u, 0u, 0u), input[0][1]);
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        EXPECT_NEAR(sampleAt(output, layout, channel, 1u, 0u), input[1][channel], 1.0e-6);
    }
}

TEST(ColorAdjustmentTest, ColorizationSupportsBothModelsAndChannelBehavior)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout monoLayout = layoutOf(2u, 1u, 1u);
    const an::ImageLayout rgbLayout = layoutOf(2u, 1u, 3u, true);
    std::vector<float> mono(monoLayout.totalBytes() / sizeof(float), 0.0f);
    std::vector<float> output(rgbLayout.totalBytes() / sizeof(float), 0.0f);
    sampleAt(mono, monoLayout, 0u, 0u, 0u) = 0.25f;
    sampleAt(mono, monoLayout, 0u, 1u, 0u) = 0.75f;

    const an::ColorizationParameters green{.hueDegrees = 120.0, .saturation = 1.0, .model = an::ColorizationModel::Hsv};
    ASSERT_TRUE(
        an::colorizeImage(execution, an::SampleType::Float32, mono.data(), monoLayout, output.data(), rgbLayout, green)
            .hasValue());
    for (std::uint32_t x = 0u; x < 2u; ++x)
    {
        EXPECT_NEAR(sampleAt(output, rgbLayout, 0u, x, 0u), 0.0, 1.0e-6);
        EXPECT_FLOAT_EQ(sampleAt(output, rgbLayout, 1u, x, 0u), sampleAt(mono, monoLayout, 0u, x, 0u));
        EXPECT_NEAR(sampleAt(output, rgbLayout, 2u, x, 0u), 0.0, 1.0e-6);
    }

    std::vector<float> color(rgbLayout.totalBytes() / sizeof(float), 0.0f);
    sampleAt(color, rgbLayout, 0u, 0u, 0u) = 0.2f;
    sampleAt(color, rgbLayout, 1u, 0u, 0u) = 0.4f;
    sampleAt(color, rgbLayout, 2u, 0u, 0u) = 0.6f;
    const an::ColorizationParameters red{.hueDegrees = 0.0, .saturation = 1.0, .model = an::ColorizationModel::Hsi};
    ASSERT_TRUE(
        an::colorizeImage(execution, an::SampleType::Float32, color.data(), rgbLayout, color.data(), rgbLayout, red)
            .hasValue());
    EXPECT_NEAR(sampleAt(color, rgbLayout, 0u, 0u, 0u), 0.6, 1.0e-5);
    EXPECT_NEAR(sampleAt(color, rgbLayout, 1u, 0u, 0u), 0.0, 1.0e-6);
    EXPECT_NEAR(sampleAt(color, rgbLayout, 2u, 0u, 0u), 0.0, 1.0e-6);
}

TEST(ColorAdjustmentTest, InvalidModelsEncodingsAndIntegerWorkingImagesAreRefused)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout layout = layoutOf(1u, 1u, 3u);
    std::vector<float> samples(layout.totalBytes() / sizeof(float), 0.25f);
    an::ColorizationParameters invalid;
    invalid.model = static_cast<an::ColorizationModel>(9);
    EXPECT_EQ(
        an::colorizeImage(execution, an::SampleType::Float32, samples.data(), layout, samples.data(), layout, invalid)
            .status()
            .code(),
        an::ErrorCode::InvalidArgument);

    an::ColorEncoding wrong = srgb();
    wrong.schema = an::ChannelSchema::Hsv;
    EXPECT_EQ(an::adjustImageHsl(execution, an::SampleType::Float32, samples.data(), layout, samples.data(), layout,
                                 wrong, srgb(), {})
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(an::enhanceImageSaturation(execution, an::SampleType::UInt16, samples.data(), layout, samples.data(),
                                         layout, {})
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
}

TEST(ColorAdjustmentTest, ColorizationDeclaresTheMonoToRgbDescriptionChange)
{
    EXPECT_EQ(an::kColorAdjustmentEffect.description, an::DescriptionEffect::Restates);
}

namespace
{

/// An execution forced to a backend and, on the CPU, to an instruction set, with no fallback, on one thread so that
/// several may be open on one context at once; fails the case when it cannot be made.
[[nodiscard]] an::ExecutionContext forcedExecution(const an::EngineContext& engine, an::ComputeBackend backend,
                                                   an::InstructionSet instructionSet = an::InstructionSet::Automatic)
{
    an::ExecutionOptions options;
    options.maxConcurrency = 1u;
    options.backend = backend;
    options.instructionSet = instructionSet;
    options.backendFallback = an::BackendFallback::Forbidden;
    an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
}

/// The vector dialects this build compiled and this host runs.
[[nodiscard]] std::vector<an::InstructionSet> runnableTargets()
{
    std::vector<an::InstructionSet> targets;
    const an::CpuCapabilities capabilities = an::cpuCapabilities();
    for (const an::InstructionSet candidate : {an::InstructionSet::Avx2, an::InstructionSet::Avx512Skylake})
    {
        if (an::instructionSetIsCompiledIn(candidate) && an::supportsInstructionSet(capabilities, candidate))
        {
            targets.push_back(candidate);
        }
    }
    return targets;
}

/// One sample of an image, read.
[[nodiscard]] float sampleOf(const std::vector<float>& samples, const an::ImageLayout& layout, std::uint32_t channel,
                             std::uint32_t x, std::uint32_t y)
{
    const std::uint64_t offset =
        *layout.sampleByteOffset(channel / layout.samplesPerPixel(), y, x, channel % layout.samplesPerPixel());
    return *reinterpret_cast<const float*>(reinterpret_cast<const std::uint8_t*>(samples.data()) + offset);
}

/// A colour image whose pixels sweep the unit cube and step past it: every eighth pixel grey, every ninth beyond one,
/// every tenth below zero, every eleventh at the sRGB curve's break.
[[nodiscard]] std::vector<float> sweptColours(const an::ImageLayout& layout)
{
    const std::uint32_t width = layout.extent().width();
    const std::uint32_t height = layout.extent().height();
    std::vector<float> samples(layout.totalBytes() / sizeof(float), 0.0f);
    for (std::uint32_t y = 0u; y < height; ++y)
    {
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            const std::uint32_t index = y * width + x;
            const float u = static_cast<float>(x) / static_cast<float>(width - 1u);
            const float v = static_cast<float>(y) / static_cast<float>(height - 1u);
            float r = u;
            float g = v;
            float b = 0.5f + 0.5f * std::sin(7.0f * u + 3.0f * v);
            if (index % 8u == 0u)
            {
                g = r;
                b = r;
            }
            if (index % 9u == 0u)
            {
                r = 1.0f + 0.2f * u;
            }
            if (index % 10u == 0u)
            {
                b = -0.05f * v;
            }
            if (index % 11u == 0u)
            {
                r = 0.04045f + 0.0001f * (u - 0.5f);
            }
            sampleAt(samples, layout, 0u, x, y) = r;
            sampleAt(samples, layout, 1u, x, y) = g;
            sampleAt(samples, layout, 2u, x, y) = b;
        }
    }
    return samples;
}

/// What the LCh adjustment makes of an image under an execution, into a fresh image; fails the case when it is refused.
[[nodiscard]] std::vector<float> adjustedLch(const an::ExecutionContext& execution, const an::ImageLayout& layout,
                                             const std::vector<float>& source, const an::ColorEncoding& encoding,
                                             const an::LchAdjustment& adjustment,
                                             an::ColorAdjustmentResult* counts = nullptr)
{
    std::vector<float> destination(source.size(), 0.0f);
    const an::Result<an::ColorAdjustmentResult> ran =
        an::adjustImageLch(execution, an::SampleType::Float32, source.data(), layout, destination.data(), layout,
                           encoding, encoding, adjustment);
    EXPECT_TRUE(ran.hasValue()) << ran.status().message();
    if (ran.hasValue() && counts != nullptr)
    {
        *counts = *ran;
    }
    return destination;
}

/// The distance the compiled LCh row may be from its scalar authority, of full scale. The Float32 lanes' rounding
/// through a dozen transcendental steps is a few millionths; what reaches this bound is a channel near zero of a
/// strongly saturated colour, the small difference of large terms in the matrix back from XYZ, raised by the sRGB
/// curve's slope near black - under a fortieth of a level of an eight-bit display.
constexpr double kCompiledLchBound = 1.0e-4;

} // namespace

// The LCh adjustment of a Float32 image runs a row compiled per instruction set, the scalar authority's arithmetic in
// Float32 lanes: on every dialect this host runs, under a linear and under the sRGB transfer, it stays within the
// stated bound of the reference backend's answer, holds every channel to the unit interval, counts what the authority
// counts, and is another implementation - not every sample is the authority's bits.
TEST(ColorAdjustmentTest, TheCompiledLchRowStaysWithinItsBoundOfTheScalarAuthorityOnEveryTarget)
{
    const std::vector<an::InstructionSet> targets = runnableTargets();
    ASSERT_FALSE(targets.empty()) << "this host runs no compiled dialect";
    const an::EngineContext engine = makeEngine();
    const an::ImageLayout layout = layoutOf(257u, 61u, 3u);
    const std::vector<float> source = sweptColours(layout);
    const an::ExecutionContext reference = forcedExecution(engine, an::ComputeBackend::Reference);
    const an::LchAdjustment adjustments[] = {{.lightnessOffset = 8.0, .chromaScale = 1.4, .hueOffsetDegrees = 25.0},
                                             {.lightnessOffset = -30.0, .chromaScale = 0.5, .hueOffsetDegrees = -200.0},
                                             {.lightnessOffset = 60.0, .chromaScale = 2.0, .hueOffsetDegrees = 359.5}};
    for (const an::ColorEncoding& encoding : {srgb(), an::ColorEncoding{}})
    {
        for (const an::LchAdjustment& adjustment : adjustments)
        {
            an::ColorAdjustmentResult referenceCounts;
            const std::vector<float> expected =
                adjustedLch(reference, layout, source, encoding, adjustment, &referenceCounts);
            for (const an::InstructionSet target : targets)
            {
                SCOPED_TRACE(::testing::Message()
                             << "transfer " << static_cast<int>(encoding.transfer) << ", lightness "
                             << adjustment.lightnessOffset << ", hue " << adjustment.hueOffsetDegrees
                             << ", instruction set " << static_cast<int>(target));
                const an::ExecutionContext compiled = forcedExecution(engine, an::ComputeBackend::Cpu, target);
                an::ColorAdjustmentResult counts;
                const std::vector<float> actual = adjustedLch(compiled, layout, source, encoding, adjustment, &counts);
                EXPECT_EQ(counts.run, referenceCounts.run);
                double largest = 0.0;
                std::size_t sameBits = 0u;
                for (std::size_t index = 0u; index < actual.size(); ++index)
                {
                    EXPECT_GE(actual[index], 0.0f);
                    EXPECT_LE(actual[index], 1.0f);
                    largest = (std::max)(largest, std::abs(static_cast<double>(actual[index]) - expected[index]));
                    sameBits +=
                        std::bit_cast<std::uint32_t>(actual[index]) == std::bit_cast<std::uint32_t>(expected[index]);
                }
                EXPECT_LE(largest, kCompiledLchBound);
                EXPECT_LT(sameBits, actual.size())
                    << "the compiled row ran: its lanes round apart from the doubles somewhere";
            }
        }
    }
}

// A pixel with a sample that is not finite is left as its destination holds it and not counted under the finite-only
// rule, by the compiled row as by the authority; under the all-valid rule both refuse the run.
TEST(ColorAdjustmentTest, TheCompiledLchRowSkipsAndCountsNonFinitePixelsAsTheAuthorityDoes)
{
    const an::EngineContext engine = makeEngine();
    const an::ImageLayout layout = layoutOf(19u, 3u, 3u);
    std::vector<float> source = sweptColours(layout);
    sampleAt(source, layout, 1u, 5u, 1u) = std::numeric_limits<float>::quiet_NaN();
    sampleAt(source, layout, 0u, 18u, 2u) = std::numeric_limits<float>::infinity();
    const an::LchAdjustment adjustment{.lightnessOffset = 8.0, .chromaScale = 1.4, .hueOffsetDegrees = 25.0};
    an::KernelValidityView finiteOnly;
    finiteOnly.descriptor =
        an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::FiniteOnly}).value();
    std::vector<an::ExecutionContext> executions;
    executions.push_back(forcedExecution(engine, an::ComputeBackend::Reference));
    for (const an::InstructionSet target : runnableTargets())
    {
        executions.push_back(forcedExecution(engine, an::ComputeBackend::Cpu, target));
    }
    for (const an::ExecutionContext& execution : executions)
    {
        SCOPED_TRACE(::testing::Message() << "backend " << static_cast<int>(execution.requestedBackend()));
        std::vector<float> destination(source.size(), 0.75f);
        const an::Result<an::ColorAdjustmentResult> ran =
            an::adjustImageLch(execution, an::SampleType::Float32, source.data(), layout, destination.data(), layout,
                               srgb(), srgb(), adjustment, finiteOnly);
        ASSERT_TRUE(ran.hasValue()) << ran.status().message();
        EXPECT_EQ(ran->run.selection.total, 57u);
        EXPECT_EQ(ran->run.selection.valid, 55u);
        EXPECT_EQ(ran->run.outputsWritten, 165u);
        for (std::uint32_t channel = 0u; channel < 3u; ++channel)
        {
            EXPECT_EQ(sampleOf(destination, layout, channel, 5u, 1u), 0.75f);
            EXPECT_EQ(sampleOf(destination, layout, channel, 18u, 2u), 0.75f);
            EXPECT_NE(sampleOf(destination, layout, channel, 4u, 1u), 0.75f);
        }
        std::vector<float> refused(source.size(), 0.0f);
        EXPECT_EQ(an::adjustImageLch(execution, an::SampleType::Float32, source.data(), layout, refused.data(), layout,
                                     srgb(), srgb(), adjustment)
                      .status()
                      .code(),
                  an::ErrorCode::InvalidData);
    }
}

// The compiled row adjusts an image in place as it adjusts it into another image, bit for bit.
TEST(ColorAdjustmentTest, TheCompiledLchRowAdjustsInPlaceAsItDoesIntoAnotherImage)
{
    const an::EngineContext engine = makeEngine();
    const an::ImageLayout layout = layoutOf(33u, 5u, 3u);
    const std::vector<float> source = sweptColours(layout);
    const an::LchAdjustment adjustment{.lightnessOffset = -5.0, .chromaScale = 1.2, .hueOffsetDegrees = 40.0};
    for (const an::InstructionSet target : runnableTargets())
    {
        const an::ExecutionContext compiled = forcedExecution(engine, an::ComputeBackend::Cpu, target);
        const std::vector<float> apart = adjustedLch(compiled, layout, source, srgb(), adjustment);
        std::vector<float> inPlace = source;
        ASSERT_TRUE(an::adjustImageLch(compiled, an::SampleType::Float32, inPlace.data(), layout, inPlace.data(),
                                       layout, srgb(), srgb(), adjustment)
                        .hasValue());
        for (std::size_t index = 0u; index < apart.size(); ++index)
        {
            EXPECT_EQ(std::bit_cast<std::uint32_t>(inPlace[index]), std::bit_cast<std::uint32_t>(apart[index]));
        }
    }
}
