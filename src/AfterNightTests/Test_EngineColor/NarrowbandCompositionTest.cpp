// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Algorithms/Color/NarrowbandColorMap.h>
#include <AfterNightEngine/Algorithms/Color/NarrowbandExtraction.h>
#include <AfterNightEngine/Algorithms/Color/NarrowbandPalette.h>
#include <AfterNightEngine/Algorithms/Color/NarrowbandRgbCombination.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/OperationTransition.h>
#include <AfterNightEngine/Image/StorageArrangement.h>

#include "ColorTestSupport.h"
#include "../../AfterNightEngine/src/algorithms/color/NarrowbandInternal.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stop_token>
#include <utility>
#include <vector>

namespace
{

[[nodiscard]] an::ImageDescriptor monoDescriptor(std::uint32_t width, std::uint32_t height,
                                                 an::SampleType sampleType = an::SampleType::Float32)
{
    return color_test::rgbDescriptor(width, height, sampleType, 1u);
}

[[nodiscard]] an::ImageDescriptor mosaicDescriptor(std::uint32_t width, std::uint32_t height, an::CfaPattern pattern,
                                                   an::SampleType sampleType = an::SampleType::Float32,
                                                   std::uint32_t phaseX = 0u, std::uint32_t phaseY = 0u)
{
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(width, height, 1u, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const std::optional<an::CfaDescriptor> cfa = an::CfaDescriptor::create(pattern, phaseX, phaseY);
    EXPECT_TRUE(cfa.has_value());
    const std::optional<an::ImageDescriptor> descriptor =
        an::ImageDescriptor::create(an::ImageDescriptorRequest{.extent = *extent,
                                                               .sampleType = sampleType,
                                                               .channelSchema = an::ChannelSchema::Mono,
                                                               .colorSpace = an::ColorSpace::Unspecified,
                                                               .numericDomain = an::NumericDomain::NormalizedLinear,
                                                               .cfa = *cfa,
                                                               .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                               .pixelAspectRatio = 1.0});
    EXPECT_TRUE(descriptor.has_value());
    return *descriptor;
}

template <typename SampleT>
[[nodiscard]] SampleT valueAt(const std::vector<SampleT>& samples, const an::ImageLayout& layout, std::uint32_t x,
                              std::uint32_t y, std::uint32_t channel = 0u)
{
    const std::uint8_t* bytes = reinterpret_cast<const std::uint8_t*>(samples.data());
    const SampleT* row =
        reinterpret_cast<const SampleT*>(bytes + *layout.rowByteOffset(channel / layout.samplesPerPixel(), y));
    return row[static_cast<std::size_t>(x) * layout.samplesPerPixel() + channel % layout.samplesPerPixel()];
}

[[nodiscard]] an::NarrowbandPaletteChannels paletteChannels(const std::vector<float>& hydrogenAlpha,
                                                            const std::vector<float>& oxygenIII,
                                                            const std::vector<float>& sulfurII,
                                                            const an::ImageLayout& monoLayout)
{
    return {.hydrogenAlpha = {.samples = hydrogenAlpha.data(), .layout = &monoLayout},
            .oxygenIII = {.samples = oxygenIII.data(), .layout = &monoLayout},
            .sulfurII = {.samples = sulfurII.data(), .layout = &monoLayout}};
}

[[nodiscard]] std::array<double, 3u> foraxxOracle(double hydrogenAlpha, double oxygenIII, double sulfurII)
{
    const double sulfurBlend = std::pow(std::clamp(oxygenIII, 1.0e-6, 1.0), 1.0 - std::clamp(oxygenIII, 1.0e-6, 1.0));
    const double product = hydrogenAlpha * oxygenIII;
    const double blend = std::pow(product, 1.0 - product);
    std::array<double, 3u> result{sulfurBlend * sulfurII + (1.0 - sulfurBlend) * hydrogenAlpha,
                                  blend * hydrogenAlpha + (1.0 - blend) * oxygenIII, oxygenIII};
    const double maximum = (std::max)({result[0], result[1], result[2]});
    for (double& value : result)
    {
        value /= maximum;
    }
    return result;
}

[[nodiscard]] double lanczosOracleWeight(double distance)
{
    if (std::abs(distance) >= 4.0)
    {
        return 0.0;
    }
    const auto sinc = [](double value) {
        if (std::abs(value) < 1.0e-12)
        {
            return 1.0;
        }
        constexpr double pi = 3.141592653589793238462643383279502884;
        return std::sin(pi * value) / (pi * value);
    };
    return sinc(distance) * sinc(distance / 4.0);
}

[[nodiscard]] double resizedLanczosOracle(const std::vector<float>& source, std::uint32_t sourceWidth,
                                          std::uint32_t sourceHeight, std::uint32_t destinationWidth,
                                          std::uint32_t destinationHeight, std::uint32_t destinationX,
                                          std::uint32_t destinationY)
{
    const double sourceX = (static_cast<double>(destinationX) + 0.5) * sourceWidth / destinationWidth - 0.5;
    const double sourceY = (static_cast<double>(destinationY) + 0.5) * sourceHeight / destinationHeight - 0.5;
    const std::int64_t centerX = static_cast<std::int64_t>(std::floor(sourceX));
    const std::int64_t centerY = static_cast<std::int64_t>(std::floor(sourceY));
    double sum = 0.0;
    double weights = 0.0;
    for (std::int64_t offsetY = -3; offsetY <= 4; ++offsetY)
    {
        const std::int64_t sampleY =
            std::clamp(centerY + offsetY, std::int64_t{0}, static_cast<std::int64_t>(sourceHeight) - 1);
        const double weightY = lanczosOracleWeight(sourceY - static_cast<double>(centerY + offsetY));
        for (std::int64_t offsetX = -3; offsetX <= 4; ++offsetX)
        {
            const std::int64_t sampleX =
                std::clamp(centerX + offsetX, std::int64_t{0}, static_cast<std::int64_t>(sourceWidth) - 1);
            const double weight = weightY * lanczosOracleWeight(sourceX - static_cast<double>(centerX + offsetX));
            sum += source[static_cast<std::uint64_t>(sampleY) * sourceWidth + static_cast<std::uint32_t>(sampleX)] *
                   weight;
            weights += weight;
        }
    }
    return sum / weights;
}

} // namespace

TEST(NarrowbandPaletteTest, CatalogueAndAllMappingsRetainTheirStableOrder)
{
    EXPECT_EQ(an::narrowbandPalettes().size(), 12u);
    EXPECT_STREQ(an::narrowbandPaletteName(an::NarrowbandPalette::Sho), "SHO");
    EXPECT_STREQ(an::narrowbandPaletteName(an::NarrowbandPalette::Realistic1), "Realistic1");
    EXPECT_STREQ(an::narrowbandPaletteName(an::NarrowbandPalette::Foraxx), "Foraxx");
    EXPECT_STREQ(an::narrowbandPalettes()[9u].description, "SetiAstro realistic blend with Ha/SII red and OIII blue.");
    EXPECT_EQ(an::narrowbandPaletteFromIndex(999u), an::NarrowbandPalette::Foraxx);
    EXPECT_EQ(an::narrowbandPaletteIndex(an::NarrowbandPalette::Realistic2), 10u);

    an::EngineContext engine = color_test::makeEngine();
    an::ExecutionContext execution = color_test::makeExecution(engine);
    const an::ImageDescriptor mono = monoDescriptor(1u, 1u);
    const an::ImageLayout monoLayout = color_test::planarLayout(mono);
    const an::Result<an::ImageDescriptor> rgb = an::narrowbandPaletteOutputDescriptor(mono);
    ASSERT_TRUE(rgb.hasValue()) << rgb.status().message();
    const an::ImageLayout rgbLayout = color_test::planarLayout(*rgb);
    const std::vector<float> hydrogenAlpha{0.2f};
    const std::vector<float> oxygenIII{0.4f};
    const std::vector<float> sulfurII{0.8f};
    const an::NarrowbandPaletteChannels sources = paletteChannels(hydrogenAlpha, oxygenIII, sulfurII, monoLayout);
    const std::array<std::array<double, 3u>, 11u> expected{{
        {1.0, 0.25, 0.5},
        {0.5, 1.0, 1.0},
        {0.25, 1.0, 0.5},
        {0.25, 0.5, 1.0},
        {0.5, 1.0, 1.0},
        {1.0, 0.5, 0.5},
        {0.5, 1.0, 0.25},
        {0.5, 0.25, 1.0},
        {0.25, 1.0, 1.0},
        {1.0, 0.68, 0.76},
        {0.7307692308, 1.0, 0.7692307692},
    }};
    for (std::uint32_t palette = 0u; palette < expected.size(); ++palette)
    {
        std::vector<float> output(3u, -1.0f);
        an::NarrowbandPaletteRequest request;
        request.palette = static_cast<an::NarrowbandPalette>(palette);
        const an::Result<an::NarrowbandPaletteResult> result = an::composeNarrowbandPalette(
            execution, an::SampleType::Float32, sources, output.data(), rgbLayout, request);
        ASSERT_TRUE(result.hasValue()) << result.status().message();
        for (std::uint32_t channel = 0u; channel < 3u; ++channel)
        {
            EXPECT_NEAR(valueAt(output, rgbLayout, 0u, 0u, channel), expected[palette][channel], 2.0e-6)
                << "palette=" << palette << " channel=" << channel;
        }
    }

    std::vector<float> output(3u, -1.0f);
    an::NarrowbandPaletteRequest request;
    request.palette = an::NarrowbandPalette::Foraxx;
    const an::Result<an::NarrowbandPaletteResult> result =
        an::composeNarrowbandPalette(execution, an::SampleType::Float32, sources, output.data(), rgbLayout, request);
    ASSERT_TRUE(result.hasValue()) << result.status().message();
    const std::array<double, 3u> expectedForaxx = foraxxOracle(0.2, 0.4, 0.8);
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        EXPECT_NEAR(valueAt(output, rgbLayout, 0u, 0u, channel), expectedForaxx[channel], 2.0e-6);
    }
}

TEST(NarrowbandPaletteTest, SubstitutionIntensityNonFiniteValidityAndGridAreExplicit)
{
    an::EngineContext engine = color_test::makeEngine();
    an::ExecutionContext execution = color_test::makeExecution(engine);
    const an::ImageDescriptor mono = monoDescriptor(2u, 1u);
    const an::ImageLayout monoLayout = color_test::planarLayout(mono);
    const an::Result<an::ImageDescriptor> rgb = an::narrowbandPaletteOutputDescriptor(mono);
    ASSERT_TRUE(rgb.hasValue());
    const an::ImageLayout rgbLayout = color_test::planarLayout(*rgb);
    const std::vector<float> hydrogenAlpha{0.2f, std::numeric_limits<float>::quiet_NaN()};
    const std::vector<float> oxygenIII{0.4f, std::numeric_limits<float>::infinity()};
    an::NarrowbandPaletteChannels sources;
    sources.hydrogenAlpha = {.samples = hydrogenAlpha.data(), .layout = &monoLayout};
    sources.oxygenIII = {.samples = oxygenIII.data(), .layout = &monoLayout};
    std::vector<float> output(6u, -1.0f);
    an::NarrowbandPaletteRequest request;
    request.palette = an::NarrowbandPalette::Sho;
    request.intensities = {.hydrogenAlpha = 2.0, .oxygenIII = 0.5, .sulfurII = 1.0};
    const an::Result<an::NarrowbandPaletteResult> result =
        an::composeNarrowbandPalette(execution, an::SampleType::Float32, sources, output.data(), rgbLayout, request);
    ASSERT_TRUE(result.hasValue()) << result.status().message();
    EXPECT_TRUE(result->substitutedSulfurII);
    EXPECT_EQ(result->nonFiniteSamples, 3u);
    EXPECT_NEAR(valueAt(output, rgbLayout, 0u, 0u, 0u), 0.5, 1.0e-6);
    EXPECT_NEAR(valueAt(output, rgbLayout, 0u, 0u, 1u), 1.0, 1.0e-6);
    EXPECT_NEAR(valueAt(output, rgbLayout, 0u, 0u, 2u), 0.5, 1.0e-6);

    std::vector<std::vector<float>> gridStorage(an::narrowbandPalettes().size(), std::vector<float>(6u, -1.0f));
    std::vector<an::NarrowbandPaletteDestination> destinations;
    destinations.reserve(gridStorage.size());
    for (std::size_t index = 0u; index < gridStorage.size(); ++index)
    {
        destinations.push_back({.palette = an::narrowbandPaletteFromIndex(index),
                                .samples = gridStorage[index].data(),
                                .layout = &rgbLayout});
    }
    std::vector<an::NarrowbandPaletteResult> gridResults(destinations.size());
    const an::Status grid = an::composeNarrowbandPaletteGrid(execution, an::SampleType::Float32, sources, destinations,
                                                             request.intensities, gridResults, true);
    ASSERT_TRUE(grid.ok()) << grid.message();
    EXPECT_EQ(gridResults.size(), 12u);
    EXPECT_EQ(gridResults.front().run.outputsWritten, 6u);

    const std::optional<an::ValidityDescriptor> nonFiniteInvalid =
        an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly});
    ASSERT_TRUE(nonFiniteInvalid.has_value());
    std::vector<float> preserved(6u, 0.73f);
    const an::Result<an::NarrowbandPaletteResult> selected =
        an::composeNarrowbandPalette(execution, an::SampleType::Float32, sources, preserved.data(), rgbLayout, request,
                                     {.descriptor = *nonFiniteInvalid});
    ASSERT_TRUE(selected.hasValue()) << selected.status().message();
    EXPECT_EQ(selected->run.selection.valid, 1u);
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        EXPECT_EQ(valueAt(preserved, rgbLayout, 1u, 0u, channel), 0.73f);
    }

    const std::vector<float> noFiniteSamples(2u, std::numeric_limits<float>::quiet_NaN());
    const an::NarrowbandPaletteChannels emptySources =
        paletteChannels(noFiniteSamples, noFiniteSamples, noFiniteSamples, monoLayout);
    std::vector<float> untouched(6u, 0.61f);
    const an::Result<an::NarrowbandPaletteResult> empty =
        an::composeNarrowbandPalette(execution, an::SampleType::Float32, emptySources, untouched.data(), rgbLayout,
                                     request, {.descriptor = *nonFiniteInvalid});
    EXPECT_EQ(empty.code(), an::ErrorCode::EmptySelection);
    EXPECT_TRUE(std::all_of(untouched.begin(), untouched.end(), [](float value) { return value == 0.61f; }));
}

TEST(NarrowbandColorMapTest, HsvScreenAndEveryModificationModeAreFused)
{
    an::EngineContext engine = color_test::makeEngine();
    an::ExecutionContext execution = color_test::makeExecution(engine);
    const an::ImageDescriptor mono = monoDescriptor(1u, 1u);
    const an::ImageLayout monoLayout = color_test::planarLayout(mono);
    const an::ImageDescriptor rgb = color_test::rgbDescriptor(
        1u, 1u, an::SampleType::Float32, 3u, an::NumericDomain::NormalizedLinear, an::ColorSpace::NarrowbandMapping);
    const an::ImageLayout rgbLayout = color_test::planarLayout(rgb);
    const std::vector<float> target{0.2f};
    const std::vector<float> modifier{0.5f};
    for (const std::pair<an::NarrowbandModificationMode, double> expected :
         std::array<std::pair<an::NarrowbandModificationMode, double>, 3u>{{
             {an::NarrowbandModificationMode::Add, 0.45},
             {an::NarrowbandModificationMode::Multiply, 0.05},
             {an::NarrowbandModificationMode::Boost, 0.25},
         }})
    {
        const std::array<an::NarrowbandColorLayer, 2u> layers{{
            {.samples = target.data(),
             .layout = &monoLayout,
             .saturation = 0.0,
             .modification = expected.first,
             .modificationSource = 1u,
             .modificationFactor = 0.5},
            {.samples = modifier.data(), .layout = &monoLayout, .included = false, .saturation = 0.0},
        }};
        const an::Result<std::uint32_t> channels = an::narrowbandColorMapChannelCount(layers);
        ASSERT_TRUE(channels.hasValue());
        EXPECT_EQ(*channels, 1u);
        std::vector<float> output(1u, -1.0f);
        const an::Result<an::NarrowbandColorMapResult> result =
            an::mapNarrowbandColor(execution, an::SampleType::Float32, layers, output.data(), monoLayout);
        ASSERT_TRUE(result.hasValue()) << result.status().message();
        EXPECT_EQ(result->workingBytes, 0u);
        EXPECT_NEAR(output[0], expected.second, 1.0e-6);
    }

    const std::vector<float> unusedTarget{0.3f};
    const std::vector<float> unusedModifier{std::numeric_limits<float>::quiet_NaN()};
    const std::array<an::NarrowbandColorLayer, 3u> unusedMedianLayers{{
        {.samples = target.data(), .layout = &monoLayout, .saturation = 0.0},
        {.samples = unusedTarget.data(),
         .layout = &monoLayout,
         .included = false,
         .saturation = 0.0,
         .modification = an::NarrowbandModificationMode::Add,
         .modificationSource = 2u,
         .subtractModificationMedian = true},
        {.samples = unusedModifier.data(), .layout = &monoLayout, .included = false, .saturation = 0.0},
    }};
    const std::optional<an::ValidityDescriptor> finiteOnly =
        an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly});
    ASSERT_TRUE(finiteOnly.has_value());
    std::vector<float> unusedMedianOutput(1u, -1.0f);
    const an::Result<an::NarrowbandColorMapResult> unusedMedian =
        an::mapNarrowbandColor(execution, an::SampleType::Float32, unusedMedianLayers, unusedMedianOutput.data(),
                               monoLayout, {}, {.descriptor = *finiteOnly});
    ASSERT_TRUE(unusedMedian.hasValue()) << unusedMedian.status().message();
    EXPECT_EQ(unusedMedian->run.selection.valid, 1u);
    EXPECT_EQ(unusedMedian->workingBytes, 0u);
    EXPECT_NEAR(unusedMedianOutput[0], 0.2, 1.0e-6);

    const std::vector<float> red{0.2f};
    const std::vector<float> green{0.5f};
    const std::array<an::NarrowbandColorLayer, 2u> colorLayers{{
        {.samples = red.data(), .layout = &monoLayout, .hueDegrees = 0.0, .saturation = 1.0},
        {.samples = green.data(), .layout = &monoLayout, .hueDegrees = 120.0, .saturation = 1.0},
    }};
    std::vector<float> colorOutput(3u, -1.0f);
    const an::Result<an::NarrowbandColorMapResult> colorResult =
        an::mapNarrowbandColor(execution, an::SampleType::Float32, colorLayers, colorOutput.data(), rgbLayout);
    ASSERT_TRUE(colorResult.hasValue()) << colorResult.status().message();
    EXPECT_NEAR(valueAt(colorOutput, rgbLayout, 0u, 0u, 0u), 0.2, 1.0e-6);
    EXPECT_NEAR(valueAt(colorOutput, rgbLayout, 0u, 0u, 1u), 0.5, 1.0e-6);
    EXPECT_NEAR(valueAt(colorOutput, rgbLayout, 0u, 0u, 2u), 0.0, 1.0e-6);
}

TEST(NarrowbandColorMapTest, ModifierMedianBackgroundAndOutputStretchRetainFramePasses)
{
    an::EngineContext engine = color_test::makeEngine();
    an::ExecutionContext execution = color_test::makeExecution(engine);
    const an::ImageDescriptor mono = monoDescriptor(3u, 1u);
    const an::ImageLayout monoLayout = color_test::planarLayout(mono);
    const std::vector<float> target{0.1f, 0.1f, 0.1f};
    const std::vector<float> modifier{0.2f, 0.6f, 0.8f};
    const std::array<an::NarrowbandColorLayer, 2u> layers{{
        {.samples = target.data(),
         .layout = &monoLayout,
         .saturation = 0.0,
         .modification = an::NarrowbandModificationMode::Add,
         .modificationSource = 1u,
         .invertModification = true,
         .subtractModificationMedian = true},
        {.samples = modifier.data(), .layout = &monoLayout, .included = false, .saturation = 0.0},
    }};
    std::vector<float> output(3u, -1.0f);
    const an::Result<an::NarrowbandColorMapResult> result =
        an::mapNarrowbandColor(execution, an::SampleType::Float32, layers, output.data(), monoLayout);
    ASSERT_TRUE(result.hasValue()) << result.status().message();
    EXPECT_NEAR(output[0], 0.5, 1.0e-6);
    EXPECT_NEAR(output[1], 0.1, 1.0e-6);
    EXPECT_NEAR(output[2], 0.1, 1.0e-6);

    const an::ImageDescriptor rgb = color_test::rgbDescriptor(2u, 1u);
    const an::ImageLayout rgbLayout = color_test::planarLayout(rgb);
    const std::vector<float> rgbSource{0.2f, 0.4f, 0.4f, 0.6f, 0.6f, 0.8f};
    const std::array<an::NarrowbandColorLayer, 1u> rgbLayer{{
        {.samples = rgbSource.data(), .layout = &rgbLayout},
    }};
    std::vector<float> backgroundOutput(6u, -1.0f);
    an::NarrowbandColorMapRequest background;
    background.background.enabled = true;
    background.background.region = an::NarrowbandColorRegion{.x = 0u, .y = 0u, .width = 1u, .height = 1u};
    const an::Result<an::NarrowbandColorMapResult> backgroundResult = an::mapNarrowbandColor(
        execution, an::SampleType::Float32, rgbLayer, backgroundOutput.data(), rgbLayout, background);
    ASSERT_TRUE(backgroundResult.hasValue()) << backgroundResult.status().message();
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        EXPECT_NEAR(valueAt(backgroundOutput, rgbLayout, 0u, 0u, channel), 0.0, 1.0e-6);
        EXPECT_NEAR(valueAt(backgroundOutput, rgbLayout, 1u, 0u, channel), 0.2, 1.0e-6);
        EXPECT_DOUBLE_EQ(backgroundResult->backgroundGains[channel], 1.0);
        EXPECT_DOUBLE_EQ(backgroundResult->backgroundOffsets[channel], -backgroundResult->backgroundLevels[channel]);
    }

    const std::vector<float> stretchSource{0.05f, 0.1f, 0.4f};
    const std::array<an::NarrowbandColorLayer, 1u> stretchLayer{{
        {.samples = stretchSource.data(), .layout = &monoLayout, .saturation = 0.0},
    }};
    std::vector<float> stretched(3u, -1.0f);
    an::NarrowbandColorMapRequest stretch;
    stretch.applyOutputStretch = true;
    stretch.targetBackground = 0.25;
    const an::Result<an::NarrowbandColorMapResult> stretchResult =
        an::mapNarrowbandColor(execution, an::SampleType::Float32, stretchLayer, stretched.data(), monoLayout, stretch);
    ASSERT_TRUE(stretchResult.hasValue()) << stretchResult.status().message();
    EXPECT_TRUE(stretchResult->outputStretchApplied);
    EXPECT_NEAR(stretched[1], 0.25, 1.0e-5);

    const std::vector<float> partlyInvalid{0.05f, std::numeric_limits<float>::quiet_NaN(), 0.4f};
    const std::array<an::NarrowbandColorLayer, 1u> partlyInvalidLayer{{
        {.samples = partlyInvalid.data(), .layout = &monoLayout, .saturation = 0.0},
    }};
    const std::optional<an::ValidityDescriptor> nonFiniteInvalid =
        an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly});
    ASSERT_TRUE(nonFiniteInvalid.has_value());
    std::vector<float> preserved{0.9f, 0.73f, 0.9f};
    const an::Result<an::NarrowbandColorMapResult> selectedStretch =
        an::mapNarrowbandColor(execution, an::SampleType::Float32, partlyInvalidLayer, preserved.data(), monoLayout,
                               stretch, {.descriptor = *nonFiniteInvalid});
    ASSERT_TRUE(selectedStretch.hasValue()) << selectedStretch.status().message();
    EXPECT_EQ(selectedStretch->run.selection.valid, 2u);
    EXPECT_EQ(preserved[1], 0.73f);
}

TEST(NarrowbandRgbTest, ContinuumBoostFitFallbackRescaleAndAliasMatchTheirEquations)
{
    an::EngineContext engine = color_test::makeEngine();
    an::ExecutionContext execution = color_test::makeExecution(engine);
    const an::ImageDescriptor rgb = color_test::rgbDescriptor(3u, 1u);
    const an::ImageDescriptor mono = monoDescriptor(3u, 1u);
    const an::ImageLayout rgbLayout = color_test::planarLayout(rgb);
    const an::ImageLayout monoLayout = color_test::planarLayout(mono);
    const std::vector<float> broadband{0.2f, 0.4f, 0.6f, 0.25f, 0.35f, 0.45f, 0.7f, 0.6f, 0.5f};
    const std::vector<float> narrowband{0.29f, 0.4f, 0.51f};
    an::NarrowbandRgbRequest request;
    request.fitHigh = 1.0;
    request.channels[0] = {.samples = narrowband.data(),
                           .layout = &monoLayout,
                           .enabled = true,
                           .bandwidthNanometres = 10.0,
                           .scale = 1.2};
    std::vector<float> output(9u, -1.0f);
    const an::Result<an::NarrowbandRgbResult> result = an::combineNarrowbandRgb(
        execution, an::SampleType::Float32, broadband.data(), rgbLayout, output.data(), rgbLayout, request);
    ASSERT_TRUE(result.hasValue()) << result.status().message();
    EXPECT_TRUE(result->fitApplied[0]);
    EXPECT_NEAR(valueAt(output, rgbLayout, 0u, 0u, 0u), 0.08, 1.0e-5);
    EXPECT_NEAR(valueAt(output, rgbLayout, 1u, 0u, 0u), 0.4, 1.0e-5);
    EXPECT_NEAR(valueAt(output, rgbLayout, 2u, 0u, 0u), 0.72, 1.0e-5);
    EXPECT_EQ(valueAt(output, rgbLayout, 1u, 0u, 1u), 0.35f);

    request.fitHigh = 0.1;
    const an::Result<an::NarrowbandRgbResult> fallback = an::combineNarrowbandRgb(
        execution, an::SampleType::Float32, broadband.data(), rgbLayout, output.data(), rgbLayout, request);
    ASSERT_TRUE(fallback.hasValue());
    EXPECT_FALSE(fallback->fitApplied[0]);
    EXPECT_NEAR(valueAt(output, rgbLayout, 2u, 0u, 0u), 0.72, 1.0e-5);

    request.fitHigh = 0.92;
    request.rescaleNonlinear = true;
    request.channels[0].scale = 6.0;
    const an::Result<an::NarrowbandRgbResult> rescaled = an::combineNarrowbandRgb(
        execution, an::SampleType::Float32, broadband.data(), rgbLayout, output.data(), rgbLayout, request);
    ASSERT_TRUE(rescaled.hasValue());
    EXPECT_NEAR(valueAt(output, rgbLayout, 0u, 0u, 0u), 0.0, 1.0e-5);
    EXPECT_NEAR(valueAt(output, rgbLayout, 2u, 0u, 0u), 1.0, 1.0e-5);

    std::vector<float> aliased(9u, -7.0f);
    std::copy(narrowband.begin(), narrowband.end(), aliased.begin());
    request.rescaleNonlinear = false;
    request.fitHigh = 1.0;
    request.channels[0].scale = 1.2;
    request.channels[0].samples = aliased.data();
    const an::Result<an::NarrowbandRgbResult> alias = an::combineNarrowbandRgb(
        execution, an::SampleType::Float32, broadband.data(), rgbLayout, aliased.data(), rgbLayout, request);
    ASSERT_TRUE(alias.hasValue()) << alias.status().message();
    const std::array<double, 3u> expectedRed{0.08, 0.4, 0.72};
    for (std::size_t index = 0u; index < aliased.size(); ++index)
    {
        EXPECT_NEAR(aliased[index], index < 3u ? expectedRed[index] : broadband[index], 1.0e-5);
    }

    std::vector<float> narrowbandWithGap = narrowband;
    narrowbandWithGap[1] = std::numeric_limits<float>::quiet_NaN();
    request.channels[0].samples = narrowbandWithGap.data();
    const std::optional<an::ValidityDescriptor> nonFiniteInvalid =
        an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly});
    ASSERT_TRUE(nonFiniteInvalid.has_value());
    const an::Result<an::NarrowbandRgbResult> selected =
        an::combineNarrowbandRgb(execution, an::SampleType::Float32, broadband.data(), rgbLayout, output.data(),
                                 rgbLayout, request, {.descriptor = *nonFiniteInvalid});
    ASSERT_TRUE(selected.hasValue()) << selected.status().message();
    EXPECT_EQ(selected->run.selection.valid, 2u);
    EXPECT_EQ(valueAt(output, rgbLayout, 1u, 0u, 0u), valueAt(broadband, rgbLayout, 1u, 0u, 0u));

    const an::Result<an::NarrowbandRgbResult> mappedNonFinite = an::combineNarrowbandRgb(
        execution, an::SampleType::Float32, broadband.data(), rgbLayout, output.data(), rgbLayout, request);
    ASSERT_TRUE(mappedNonFinite.hasValue()) << mappedNonFinite.status().message();
    EXPECT_EQ(mappedNonFinite->nonFiniteSamples, 1u);
}

TEST(NarrowbandExtractionTest, EveryBayerPhaseProducesMeasuredHaGreenAndEqualizedOiii)
{
    an::EngineContext engine = color_test::makeEngine();
    an::ExecutionContext execution = color_test::makeExecution(engine);
    for (const an::CfaPattern pattern : std::array<an::CfaPattern, 4u>{an::CfaPattern::Rggb, an::CfaPattern::Bggr,
                                                                       an::CfaPattern::Grbg, an::CfaPattern::Gbrg})
    {
        const an::ImageDescriptor sourceDescriptor = mosaicDescriptor(5u, 5u, pattern);
        const an::ImageLayout sourceLayout = color_test::planarLayout(sourceDescriptor);
        std::vector<float> source(25u, 0.0f);
        std::array<std::uint32_t, 2u> greenPositions{};
        std::uint32_t greenCount = 0u;
        for (std::uint32_t position = 0u; position < 4u; ++position)
        {
            if (sourceDescriptor.cfa().colorAt(position % 2u, position / 2u) == an::CfaColor::Green)
            {
                greenPositions[greenCount++] = position;
            }
        }
        for (std::uint32_t y = 0u; y < 5u; ++y)
        {
            for (std::uint32_t x = 0u; x < 5u; ++x)
            {
                const an::CfaColor color = sourceDescriptor.cfa().colorAt(x, y);
                const std::uint32_t position = (y & 1u) * 2u + (x & 1u);
                source[static_cast<std::size_t>(y) * 5u + x] = color == an::CfaColor::Red      ? 0.9f
                                                               : color == an::CfaColor::Blue   ? 0.6f
                                                               : position == greenPositions[0] ? 0.2f
                                                                                               : 0.4f;
            }
        }
        const an::Result<an::ImageDescriptor> measured = an::measuredBayerChannelDescriptor(sourceDescriptor);
        ASSERT_TRUE(measured.hasValue());
        const an::ImageLayout measuredLayout = color_test::planarLayout(*measured);
        std::vector<float> hydrogen(4u, -1.0f);
        std::vector<float> green(4u, -1.0f);
        const an::Result<an::NarrowbandExtractionResult> hydrogenResult = an::extractBayerHydrogenAlpha(
            execution, sourceDescriptor, source.data(), sourceLayout, hydrogen.data(), measuredLayout);
        const an::Result<an::NarrowbandExtractionResult> greenResult = an::extractBayerGreen(
            execution, sourceDescriptor, source.data(), sourceLayout, green.data(), measuredLayout);
        ASSERT_TRUE(hydrogenResult.hasValue()) << hydrogenResult.status().message();
        ASSERT_TRUE(greenResult.hasValue()) << greenResult.status().message();
        EXPECT_EQ(hydrogenResult->trailingColumns, 1u);
        EXPECT_EQ(hydrogenResult->trailingRows, 1u);
        for (float value : hydrogen)
        {
            EXPECT_NEAR(value, 0.9, 1.0e-6);
        }
        for (float value : green)
        {
            EXPECT_NEAR(value, 0.3, 1.0e-6);
        }

        for (const an::NarrowbandExtractionScaling scaling : std::array<an::NarrowbandExtractionScaling, 3u>{
                 an::NarrowbandExtractionScaling::Native, an::NarrowbandExtractionScaling::UpsampleHydrogenAlpha,
                 an::NarrowbandExtractionScaling::DownsampleOxygenIII})
        {
            const an::Result<an::NarrowbandExtractionPlan> plan =
                an::planNarrowbandExtraction(sourceDescriptor, scaling);
            ASSERT_TRUE(plan.hasValue()) << plan.status().message();
            const an::ImageLayout hydrogenLayout = color_test::planarLayout(plan->hydrogenAlphaDescriptor());
            const an::ImageLayout oxygenLayout = color_test::planarLayout(plan->oxygenIIIDescriptor());
            std::vector<float> pairedHydrogen(hydrogenLayout.totalBytes() / sizeof(float), -1.0f);
            std::vector<float> oxygen(oxygenLayout.totalBytes() / sizeof(float), -1.0f);
            const an::Result<an::NarrowbandExtractionResult> extracted = an::extractBayerHydrogenAlphaAndOxygenIII(
                execution, *plan, source.data(), sourceLayout, pairedHydrogen.data(), hydrogenLayout, oxygen.data(),
                oxygenLayout);
            ASSERT_TRUE(extracted.hasValue()) << extracted.status().message();
            EXPECT_EQ(extracted->workingBytes, scaling == an::NarrowbandExtractionScaling::Native ? 0u : 64u);
            for (float value : pairedHydrogen)
            {
                EXPECT_NEAR(value, 0.9, 2.0e-5);
            }
            for (float value : oxygen)
            {
                EXPECT_NEAR(value, 0.4, 2.0e-5)
                    << "pattern=" << static_cast<int>(pattern) << " scaling=" << static_cast<int>(scaling);
            }
        }
    }
}

TEST(NarrowbandExtractionTest, NonFiniteOiiiNeighboursCarryNoInterpolationWeight)
{
    an::EngineContext engine = color_test::makeEngine();
    an::ExecutionContext execution = color_test::makeExecution(engine);
    const an::ImageDescriptor sourceDescriptor = mosaicDescriptor(4u, 4u, an::CfaPattern::Rggb);
    const an::ImageLayout sourceLayout = color_test::planarLayout(sourceDescriptor);
    std::vector<float> source(16u, 0.4f);
    for (std::uint32_t y = 0u; y < 4u; y += 2u)
    {
        for (std::uint32_t x = 0u; x < 4u; x += 2u)
        {
            source[static_cast<std::size_t>(y) * 4u + x] = 0.9f;
        }
    }
    source[1u] = std::numeric_limits<float>::quiet_NaN();

    const an::Result<an::NarrowbandExtractionPlan> plan =
        an::planNarrowbandExtraction(sourceDescriptor, an::NarrowbandExtractionScaling::Native);
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();
    const an::ImageLayout hydrogenLayout = color_test::planarLayout(plan->hydrogenAlphaDescriptor());
    const an::ImageLayout oxygenLayout = color_test::planarLayout(plan->oxygenIIIDescriptor());
    std::vector<float> hydrogen(hydrogenLayout.totalBytes() / sizeof(float), -1.0f);
    std::vector<float> oxygen(oxygenLayout.totalBytes() / sizeof(float), -1.0f);
    const an::Result<an::NarrowbandExtractionResult> result = an::extractBayerHydrogenAlphaAndOxygenIII(
        execution, *plan, source.data(), sourceLayout, hydrogen.data(), hydrogenLayout, oxygen.data(), oxygenLayout);
    ASSERT_TRUE(result.hasValue()) << result.status().message();
    EXPECT_EQ(result->nonFiniteSamples, 1u);
    EXPECT_NEAR(valueAt(oxygen, oxygenLayout, 0u, 0u), 0.4, 1.0e-6);
    EXPECT_EQ(valueAt(oxygen, oxygenLayout, 1u, 0u), 0.0f);
}

TEST(NarrowbandExtractionTest, ScalingMatchesTheDeclaredLanczosKernelAndUsesBudgetedMemory)
{
    an::EngineContext engine = color_test::makeEngine();
    an::ExecutionContext execution = color_test::makeExecution(engine);
    const an::ImageDescriptor sourceDescriptor = mosaicDescriptor(8u, 6u, an::CfaPattern::Rggb);
    const an::ImageLayout sourceLayout = color_test::planarLayout(sourceDescriptor);
    std::vector<float> source(48u);
    for (std::uint32_t y = 0u; y < 6u; ++y)
    {
        for (std::uint32_t x = 0u; x < 8u; ++x)
        {
            const an::CfaColor color = sourceDescriptor.cfa().colorAt(x, y);
            const double base = color == an::CfaColor::Red ? 0.25 : color == an::CfaColor::Green ? 0.1 : 0.18;
            source[static_cast<std::size_t>(y) * 8u + x] = static_cast<float>(base + 0.025 * x + 0.015 * y);
        }
    }

    const auto extract = [&](an::NarrowbandExtractionScaling scaling, std::vector<float>& hydrogen,
                             std::vector<float>& oxygen) {
        const an::Result<an::NarrowbandExtractionPlan> plan = an::planNarrowbandExtraction(sourceDescriptor, scaling);
        EXPECT_TRUE(plan.hasValue()) << plan.status().message();
        const an::ImageLayout hydrogenLayout = color_test::planarLayout(plan->hydrogenAlphaDescriptor());
        const an::ImageLayout oxygenLayout = color_test::planarLayout(plan->oxygenIIIDescriptor());
        hydrogen.resize(hydrogenLayout.totalBytes() / sizeof(float));
        oxygen.resize(oxygenLayout.totalBytes() / sizeof(float));
        return an::extractBayerHydrogenAlphaAndOxygenIII(execution, *plan, source.data(), sourceLayout, hydrogen.data(),
                                                         hydrogenLayout, oxygen.data(), oxygenLayout);
    };

    std::vector<float> nativeHydrogen;
    std::vector<float> nativeOxygen;
    std::vector<float> upsampledHydrogen;
    std::vector<float> upsampledOxygen;
    std::vector<float> downsampledHydrogen;
    std::vector<float> downsampledOxygen;
    ASSERT_TRUE(extract(an::NarrowbandExtractionScaling::Native, nativeHydrogen, nativeOxygen).hasValue());
    ASSERT_TRUE(
        extract(an::NarrowbandExtractionScaling::UpsampleHydrogenAlpha, upsampledHydrogen, upsampledOxygen).hasValue());
    ASSERT_TRUE(extract(an::NarrowbandExtractionScaling::DownsampleOxygenIII, downsampledHydrogen, downsampledOxygen)
                    .hasValue());
    EXPECT_EQ(upsampledOxygen, nativeOxygen);
    EXPECT_EQ(downsampledHydrogen, nativeHydrogen);
    for (std::uint32_t y = 0u; y < 6u; ++y)
    {
        for (std::uint32_t x = 0u; x < 8u; ++x)
        {
            EXPECT_NEAR(upsampledHydrogen[static_cast<std::size_t>(y) * 8u + x],
                        resizedLanczosOracle(nativeHydrogen, 4u, 3u, 8u, 6u, x, y), 2.0e-6);
        }
    }
    for (std::uint32_t y = 0u; y < 3u; ++y)
    {
        for (std::uint32_t x = 0u; x < 4u; ++x)
        {
            EXPECT_NEAR(downsampledOxygen[static_cast<std::size_t>(y) * 4u + x],
                        resizedLanczosOracle(nativeOxygen, 8u, 6u, 4u, 3u, x, y), 2.0e-6);
        }
    }

    an::EngineContext constrainedEngine = color_test::makeEngine(2u, 128u);
    an::ExecutionContext constrainedExecution = color_test::makeExecution(constrainedEngine, 2u);
    const an::Result<an::NarrowbandExtractionPlan> upsamplePlan =
        an::planNarrowbandExtraction(sourceDescriptor, an::NarrowbandExtractionScaling::UpsampleHydrogenAlpha);
    ASSERT_TRUE(upsamplePlan.hasValue());
    const an::ImageLayout hydrogenLayout = color_test::planarLayout(upsamplePlan->hydrogenAlphaDescriptor());
    const an::ImageLayout oxygenLayout = color_test::planarLayout(upsamplePlan->oxygenIIIDescriptor());
    std::vector<float> hydrogen(hydrogenLayout.totalBytes() / sizeof(float));
    std::vector<float> oxygen(oxygenLayout.totalBytes() / sizeof(float));
    const an::Result<an::NarrowbandExtractionResult> refused =
        an::extractBayerHydrogenAlphaAndOxygenIII(constrainedExecution, *upsamplePlan, source.data(), sourceLayout,
                                                  hydrogen.data(), hydrogenLayout, oxygen.data(), oxygenLayout);
    EXPECT_EQ(refused.code(), an::ErrorCode::MemoryBudgetExceeded);
}

TEST(NarrowbandCompositionTest, DescriptorsTransitionsValidationAndCancellationAreExplicit)
{
    const an::ImageDescriptor mono = monoDescriptor(2u, 1u);
    const an::Result<an::ImageDescriptor> palette = an::narrowbandPaletteOutputDescriptor(mono);
    ASSERT_TRUE(palette.hasValue());
    EXPECT_EQ(palette->channelSchema(), an::ChannelSchema::Rgb);
    EXPECT_EQ(palette->colorSpace(), an::ColorSpace::NarrowbandMapping);

    const an::ImageSemantics semantics =
        *an::ImageSemantics::create(an::ImageSemanticsRequest{.transferFunction = an::TransferFunction::Linear});
    EXPECT_TRUE(an::narrowbandPaletteTransition(semantics, an::NarrowbandPalette::Hoo).hasValue());
    EXPECT_TRUE(an::narrowbandColorMapTransition(semantics, 2u, 3u).hasValue());

    an::NarrowbandRgbRequest noChannels;
    EXPECT_EQ(an::narrowbandRgbTransition(semantics, noChannels).code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::narrowbandColorMapChannelCount(std::span<const an::NarrowbandColorLayer>{}).code(),
              an::ErrorCode::InvalidArgument);

    an::EngineContext engine = color_test::makeEngine();
    std::stop_source stop;
    stop.request_stop();
    an::ExecutionContext cancelled = color_test::makeExecution(engine, 2u, stop.get_token());
    const an::ImageLayout monoLayout = color_test::planarLayout(mono);
    const std::vector<float> hydrogen{0.2f, 0.3f};
    const std::vector<float> oxygen{0.4f, 0.5f};
    const std::vector<float> sulfur{0.6f, 0.7f};
    const an::NarrowbandPaletteChannels sources = paletteChannels(hydrogen, oxygen, sulfur, monoLayout);
    const an::ImageLayout paletteLayout = color_test::planarLayout(*palette);
    std::vector<float> destination(6u, 0.75f);
    const an::Result<an::NarrowbandPaletteResult> result =
        an::composeNarrowbandPalette(cancelled, an::SampleType::Float32, sources, destination.data(), paletteLayout);
    EXPECT_EQ(result.code(), an::ErrorCode::Cancelled);
    EXPECT_TRUE(std::all_of(destination.begin(), destination.end(), [](float value) { return value == 0.75f; }));
}

TEST(NarrowbandColorMapTest, InPlacePostProcessingUsesOriginalAdmission)
{
    const auto engine = color_test::makeEngine();
    const auto execution = color_test::makeExecution(engine);
    const auto descriptor = color_test::rgbDescriptor(5u, 1u);
    const auto layout = color_test::planarLayout(descriptor);
    const std::vector<float> source{0.1f, 0.2f, 0.4f, 0.6f, 0.0f, 0.15f, 0.3f, 0.5f,
                                    0.7f, 0.0f, 0.2f, 0.4f, 0.6f, 0.8f,  0.0f};
    for (const auto rule : {an::ValidityRule::ZeroIsNoData, an::ValidityRule::DomainRange})
    {
        const auto validity = an::ValidityDescriptor::create(
            rule == an::ValidityRule::DomainRange
                ? an::ValidityDescriptorRequest{.rule = rule, .lowestValid = 0.05, .highestValid = 1.0}
                : an::ValidityDescriptorRequest{.rule = rule});
        ASSERT_TRUE(validity.has_value());
        for (const bool background : {false, true})
        {
            std::vector<float> inPlace = source;
            std::vector<float> separate = source;
            std::array<an::NarrowbandColorLayer, 1u> layers{
                {{.samples = source.data(), .layout = &layout, .blackPoint = 0.25}}};
            an::NarrowbandColorMapRequest request;
            request.background.enabled = background;
            request.applyOutputStretch = true;
            const auto disjoint = an::mapNarrowbandColor(execution, an::SampleType::Float32, layers, separate.data(),
                                                         layout, request, {.descriptor = *validity});
            ASSERT_TRUE(disjoint.hasValue()) << disjoint.status().message();
            layers[0].samples = inPlace.data();
            const auto aliased = an::mapNarrowbandColor(execution, an::SampleType::Float32, layers, inPlace.data(),
                                                        layout, request, {.descriptor = *validity});
            ASSERT_TRUE(aliased.hasValue()) << aliased.status().message();
            EXPECT_EQ(inPlace, separate);
            EXPECT_EQ(aliased->workingBytes, disjoint->workingBytes + 5u);
            EXPECT_EQ(aliased->run.selection.valid, disjoint->run.selection.valid);
        }
    }
}

TEST(NarrowbandColorMapTest, AliasedAdmissionStorageIsBudgetedBeforeWriting)
{
    const auto engine = color_test::makeEngine(1u, 5u * sizeof(double));
    const auto execution = color_test::makeExecution(engine, 1u);
    const auto descriptor = monoDescriptor(5u, 1u);
    const auto layout = color_test::planarLayout(descriptor);
    std::vector<float> source{0.1f, 0.2f, 0.4f, 0.6f, 0.8f};
    const auto original = source;
    const std::array<an::NarrowbandColorLayer, 1u> layers{
        {{.samples = source.data(), .layout = &layout, .saturation = 0.0}}};
    const auto validity = an::ValidityDescriptor::create({.rule = an::ValidityRule::ZeroIsNoData});
    ASSERT_TRUE(validity.has_value());
    an::NarrowbandColorMapRequest request;
    request.applyOutputStretch = true;
    const auto refused = an::mapNarrowbandColor(execution, an::SampleType::Float32, layers, source.data(), layout,
                                                request, {.descriptor = *validity});
    EXPECT_EQ(refused.status().code(), an::ErrorCode::MemoryBudgetExceeded);
    EXPECT_EQ(source, original);
}

TEST(NarrowbandColorMapTest, AContextBlockRefusesASecondTakeWithoutLosingItsAllocation)
{
    const auto engine = color_test::makeEngine();
    const auto execution = color_test::makeExecution(engine);
    const auto before = engine.memoryResource()->counters();
    {
        an::narrowband_detail::ContextBlock block;
        ASSERT_TRUE(block.take(execution, 128u, "test").ok());
        auto* first = block.doubles();
        const auto held = engine.memoryResource()->counters();
        EXPECT_EQ(block.take(execution, 256u, "test").code(), an::ErrorCode::InvalidState);
        EXPECT_EQ(block.doubles(), first);
        EXPECT_EQ(engine.memoryResource()->counters().liveBytes, held.liveBytes);
        EXPECT_EQ(engine.memoryResource()->counters().totalAllocations, held.totalAllocations);
    }
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, before.liveBytes);
}
