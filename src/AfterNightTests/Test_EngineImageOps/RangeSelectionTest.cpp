// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the retained soft range-selection mask operation.

#include <AfterNightEngine/Algorithms/Masks/RangeSelection.h>

#include <AfterNightEngine/Algorithms/Spatial/Gaussian.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
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

[[nodiscard]] an::ImageLayout layoutOf(std::uint32_t width, std::uint32_t channels = 1u,
                                       std::uint32_t samplesPerPixel = 1u)
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, 1u, channels, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const std::optional<an::ImageLayout> layout =
        an::ImageLayout::create(an::ImageLayoutRequest{.extent = *extent,
                                                       .elementSizeBytes = sizeof(float),
                                                       .samplesPerPixel = samplesPerPixel,
                                                       .planeCount = channels / samplesPerPixel,
                                                       .rowAlignmentBytes = 1u},
                                an::defaultEngineLimits());
    EXPECT_TRUE(layout.has_value());
    return *layout;
}

} // namespace

TEST(RangeSelectionTest, FuzzinessBuildsLinearEdgeRamps)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout layout = layoutOf(7u);
    const std::array<float, 7u> source{0.1f, 0.2f, 0.275f, 0.5f, 0.725f, 0.8f, 0.9f};
    std::array<float, 7u> destination{};
    const an::RangeSelectionRequest request{.lowerLimit = 0.2, .upperLimit = 0.8, .fuzziness = 0.5};

    const an::Result<an::RangeSelectionResult> selected = an::rangeSelectImage(
        execution, an::SampleType::Float32, source.data(), layout, destination.data(), layout, request);
    ASSERT_TRUE(selected.hasValue()) << selected.status().message();
    const std::array<float, 7u> expected{0.0f, 0.0f, 0.5f, 1.0f, 0.5f, 0.0f, 0.0f};
    for (std::size_t index = 0u; index < expected.size(); ++index)
    {
        EXPECT_NEAR(destination[index], expected[index], 2.0e-7f) << index;
    }
    EXPECT_EQ(selected->run.selection.valid, source.size());
    EXPECT_EQ(selected->run.outputsWritten, source.size());
}

TEST(RangeSelectionTest, ScreeningPrecedesInversionAndExactInPlaceIsSupported)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout layout = layoutOf(3u);
    std::array<float, 3u> samples{0.25f, 0.5f, 0.75f};
    const an::RangeSelectionRequest request{
        .lowerLimit = 0.0, .upperLimit = 1.0, .fuzziness = 0.0, .screening = true, .invert = true};

    const an::Result<an::RangeSelectionResult> selected = an::rangeSelectImage(
        execution, an::SampleType::Float32, samples.data(), layout, samples.data(), layout, request);
    ASSERT_TRUE(selected.hasValue()) << selected.status().message();
    EXPECT_FLOAT_EQ(samples[0], 0.75f);
    EXPECT_FLOAT_EQ(samples[1], 0.5f);
    EXPECT_FLOAT_EQ(samples[2], 0.25f);
}

TEST(RangeSelectionTest, ProductionSmoothnessIsTheVisibleRangeThenExactGaussianThenComplementComposition)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout layout = layoutOf(9u);
    const std::array<float, 9u> source{0.0f, 0.15f, 0.25f, 0.35f, 0.5f, 0.65f, 0.75f, 0.85f, 1.0f};
    std::array<float, 9u> selected{};
    std::array<float, 9u> smoothed{};
    std::array<float, 9u> invertedBeforeSmoothing{};

    const an::RangeSelectionRequest selection{
        .lowerLimit = 0.2, .upperLimit = 0.8, .fuzziness = 0.5, .screening = true};
    ASSERT_TRUE(an::rangeSelectImage(execution, an::SampleType::Float32, source.data(), layout, selected.data(), layout,
                                     selection)
                    .hasValue());

    const an::Result<an::GaussianPlan> smoothing =
        an::planGaussianBlur(an::GaussianRequest{.sigmaX = 1.0, .sigmaY = 1.0});
    ASSERT_TRUE(smoothing.hasValue()) << smoothing.status().message();
    ASSERT_TRUE(an::blurImageGaussian(execution, an::SampleType::Float32, selected.data(), layout, smoothed.data(),
                                      layout, *smoothing)
                    .hasValue());

    an::RangeSelectionRequest earlyInversion = selection;
    earlyInversion.invert = true;
    ASSERT_TRUE(an::rangeSelectImage(execution, an::SampleType::Float32, source.data(), layout,
                                     invertedBeforeSmoothing.data(), layout, earlyInversion)
                    .hasValue());
    bool orderIsObservable = false;
    for (std::size_t index = 0u; index < smoothed.size(); ++index)
    {
        const float completed = 1.0f - smoothed[index];
        EXPECT_GE(completed, 0.0f);
        EXPECT_LE(completed, 1.0f);
        orderIsObservable = orderIsObservable || std::abs(completed - invertedBeforeSmoothing[index]) > 1.0e-5f;
    }
    EXPECT_TRUE(orderIsObservable);
}

TEST(RangeSelectionTest, MixedLayoutsAndValidityAreHandledPerChannel)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout interleaved = layoutOf(2u, 2u, 2u);
    const an::ImageLayout planar = layoutOf(2u, 2u, 1u);
    const std::array<float, 4u> source{0.0f, 0.25f, 0.75f, 1.0f};
    std::array<float, 4u> destination{-1.0f, -1.0f, -1.0f, -1.0f};
    const std::optional<an::ValidityDescriptor> validity =
        an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::ZeroIsNoData});
    ASSERT_TRUE(validity.has_value());

    const an::Result<an::RangeSelectionResult> selected =
        an::rangeSelectImage(execution, an::SampleType::Float32, source.data(), interleaved, destination.data(), planar,
                             an::RangeSelectionRequest{.lowerLimit = 0.0, .upperLimit = 1.0},
                             an::KernelValidityView{.descriptor = *validity});
    ASSERT_TRUE(selected.hasValue()) << selected.status().message();
    EXPECT_FLOAT_EQ(destination[0], -1.0f);
    EXPECT_FLOAT_EQ(destination[1], 1.0f);
    EXPECT_FLOAT_EQ(destination[2], 1.0f);
    EXPECT_FLOAT_EQ(destination[3], 1.0f);
    EXPECT_EQ(selected->run.selection.valid, 3u);
    EXPECT_EQ(selected->run.selection.total, 4u);
}

TEST(RangeSelectionTest, InvalidRequestsAndIntegerStorageAreRefusedBeforeWriting)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout layout = layoutOf(2u);
    const std::array<float, 2u> source{0.25f, 0.75f};
    std::array<float, 2u> destination{7.0f, 8.0f};

    EXPECT_EQ(an::rangeSelectImage(execution, an::SampleType::Float32, source.data(), layout, destination.data(),
                                   layout, an::RangeSelectionRequest{.lowerLimit = 0.8, .upperLimit = 0.2})
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_FLOAT_EQ(destination[0], 7.0f);
    EXPECT_FLOAT_EQ(destination[1], 8.0f);
    EXPECT_EQ(
        an::rangeSelectImage(execution, an::SampleType::UInt16, source.data(), layout, destination.data(), layout, {})
            .status()
            .code(),
        an::ErrorCode::InvalidArgument);
}

TEST(RangeSelectionTest, ValueValidityAndLayoutDoNotChangeRampBits)
{
    const auto engine = makeEngine();
    const auto execution = makeExecution(engine);
    const auto extent = an::ImageExtent::create(257u, 2u, 1u, an::defaultEngineLimits());
    ASSERT_TRUE(extent.has_value());
    const auto layout = an::ImageLayout::create({.extent = *extent,
                                                 .elementSizeBytes = sizeof(double),
                                                 .samplesPerPixel = 1u,
                                                 .planeCount = 1u,
                                                 .rowAlignmentBytes = 1u},
                                                an::defaultEngineLimits());
    ASSERT_TRUE(layout.has_value());
    const auto paddedLayout = an::ImageLayout::create({.extent = *extent,
                                                       .elementSizeBytes = sizeof(double),
                                                       .samplesPerPixel = 1u,
                                                       .planeCount = 1u,
                                                       .rowAlignmentBytes = 4096u},
                                                      an::defaultEngineLimits());
    ASSERT_TRUE(paddedLayout.has_value());
    std::vector<double> padded(static_cast<std::size_t>(paddedLayout->totalBytes() / sizeof(double)), -7.0);
    std::vector<double> source(514u), flat(514u), selected(514u);
    for (std::size_t i = 0u; i < source.size(); ++i)
        source[i] = 0.2 + static_cast<double>(i) * 0.002;
    const auto finite = an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly});
    ASSERT_TRUE(finite.has_value());
    for (const bool screening : {false, true})
        for (const bool invert : {false, true})
        {
            const an::RangeSelectionRequest request{
                .lowerLimit = 0.17, .upperLimit = 0.87, .fuzziness = 0.73, .screening = screening, .invert = invert};
            ASSERT_TRUE(an::rangeSelectImage(execution, an::SampleType::Float64, source.data(), *layout, flat.data(),
                                             *layout, request)
                            .hasValue());
            ASSERT_TRUE(an::rangeSelectImage(execution, an::SampleType::Float64, source.data(), *layout,
                                             selected.data(), *layout, request, {.descriptor = *finite})
                            .hasValue());
            EXPECT_EQ(flat, selected);
            ASSERT_TRUE(an::rangeSelectImage(execution, an::SampleType::Float64, source.data(), *layout, padded.data(),
                                             *paddedLayout, request)
                            .hasValue());
            for (std::size_t i = 0u; i < flat.size(); ++i)
                EXPECT_EQ(flat[i], padded[(i / 257u) * 512u + i % 257u]) << i;
            EXPECT_EQ(padded[300u], -7.0);
        }
}
