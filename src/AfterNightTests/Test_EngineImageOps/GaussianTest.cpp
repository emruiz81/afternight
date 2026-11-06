// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the Gaussian blur, exact and approximated.

#include <AfterNightEngine/Algorithms/Spatial/Gaussian.h>

#include <AfterNightEngine/Algorithms/Spatial/Convolution.h>
#include <AfterNightEngine/Core/ErrorCode.h>

#include "SpatialFilterFixture.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace
{

using spatial_test::makeEngine;
using spatial_test::makeExecution;
using spatial_test::planarLayout;
using spatial_test::randomPlane;

TEST(GaussianTest, TapsAreTheDensityAtTheSampleCentreAndAreRenormalized)
{
    constexpr double kSigma = 1.5;
    constexpr std::uint32_t kRadius = 6u;
    std::array<double, 2u * kRadius + 1u> taps{};
    const an::Result<std::uint32_t> written = an::writeGaussianTaps(kSigma, kRadius, taps);
    ASSERT_TRUE(written.hasValue()) << written.status().message();
    EXPECT_EQ(written.value(), taps.size());

    EXPECT_NEAR(an::convolutionTapSum(taps), 1.0, 1e-15);
    for (std::uint32_t index = 0u; index <= kRadius; ++index)
    {
        EXPECT_DOUBLE_EQ(taps[index], taps[taps.size() - 1u - index]) << "at " << index;
    }
    // Each tap is the unnormalized density divided by their sum, so ratios between taps are the
    // ratios of the density itself.
    const double expected = std::exp(-0.5 * (2.0 * 2.0) / (kSigma * kSigma));
    EXPECT_NEAR(taps[kRadius + 2u] / taps[kRadius], expected, 1e-12);
}

TEST(GaussianTest, TheTapWriterRefusesWhatItCannotProduce)
{
    std::array<double, 3> taps{};
    EXPECT_EQ(an::writeGaussianTaps(0.0, 1u, taps).status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::writeGaussianTaps(std::numeric_limits<double>::infinity(), 1u, taps).status().code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::writeGaussianTaps(1.0, an::kMaxStencilRadius + 1u, taps).status().code(), an::ErrorCode::OutOfRange);
    EXPECT_EQ(an::writeGaussianTaps(1.0, 2u, taps).status().code(), an::ErrorCode::IncompatibleArguments);
}

TEST(GaussianTest, APlanReportsTheRadiusItsTruncationImplies)
{
    const an::Result<an::GaussianPlan> plan = an::planGaussianBlur({.sigmaX = 2.0, .sigmaY = 3.0, .truncation = 4.0});
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();
    EXPECT_EQ(plan.value().method(), an::GaussianMethod::SeparableTaps);
    EXPECT_EQ(plan.value().horizontalRadius(), 8u);
    EXPECT_EQ(plan.value().verticalRadius(), 12u);
    EXPECT_EQ(plan.value().imagePasses(), 2u);
    EXPECT_DOUBLE_EQ(plan.value().realizedSigmaX(), 2.0);
    EXPECT_DOUBLE_EQ(plan.value().realizedSigmaY(), 3.0);
    for (const std::uint32_t radius : plan.value().horizontalBoxRadii())
    {
        EXPECT_EQ(radius, 0u);
    }

    const an::Result<an::GaussianPlan> tight = an::planGaussianBlur({.sigmaX = 2.0, .sigmaY = 2.0, .truncation = 2.0});
    ASSERT_TRUE(tight.hasValue()) << tight.status().message();
    EXPECT_EQ(tight.value().horizontalRadius(), 4u);
}

TEST(GaussianTest, ABoxPlanReportsTheSigmaItsWidthsRealize)
{
    const an::Result<an::GaussianPlan> plan = an::planRealtimePreviewGaussianBlur({.sigmaX = 3.0, .sigmaY = 3.0});
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();
    EXPECT_EQ(plan.value().imagePasses(), 6u);
    EXPECT_NEAR(plan.value().realizedSigmaX(), 3.0, 0.25);
    EXPECT_EQ(plan.value().horizontalBoxRadii().size(), an::kGaussianBoxPasses);

    std::uint32_t reach = 0u;
    for (const std::uint32_t radius : plan.value().horizontalBoxRadii())
    {
        reach += radius;
        EXPECT_GT(radius, 0u);
    }
    EXPECT_EQ(plan.value().horizontalRadius(), reach);
}

TEST(GaussianTest, ABoxPlanReachesPastTheWidestTapArray)
{
    // A box pass is a running sum, whose cost does not depend on its width, so a repeated-box plan is held to its own
    // reach rather than to the widest tap array an exact plan may have: a sigma of three hundred reaches some nine
    // hundred pixels on the boxes, and an exact plan of it is still refused.
    const an::Result<an::GaussianPlan> wide = an::planRealtimePreviewGaussianBlur({.sigmaX = 300.0, .sigmaY = 120.0});
    ASSERT_TRUE(wide.hasValue()) << wide.status().message();
    EXPECT_GT(wide->horizontalRadius(), an::kMaxStencilRadius);
    EXPECT_GT(wide->verticalRadius(), an::kMaxStencilRadius);
    EXPECT_LE(wide->horizontalRadius(), an::kMaxRealtimePreviewBoxReach);
    EXPECT_NEAR(wide->realizedSigmaX(), 300.0, 1.0);
    EXPECT_NEAR(wide->realizedSigmaY(), 120.0, 1.0);
    EXPECT_EQ(an::planGaussianBlur({.sigmaX = 300.0, .sigmaY = 300.0}).status().code(), an::ErrorCode::OutOfRange);

    // A reach past the boxes' own ceiling is refused by name, however far past it the sigma lies: its widths are never
    // computed into a number that wraps.
    for (const double sigma : {static_cast<double>(an::kMaxRealtimePreviewBoxReach), 1.0e9, 1.0e15, 1.0e300})
    {
        const an::Result<an::GaussianPlan> refused =
            an::planRealtimePreviewGaussianBlur({.sigmaX = 1.0, .sigmaY = sigma});
        ASSERT_FALSE(refused.hasValue()) << "sigma " << sigma;
        EXPECT_EQ(refused.status().code(), an::ErrorCode::OutOfRange) << "sigma " << sigma;
    }
}

TEST(GaussianTest, AWideBoxBlurIsTheSameOnItsFusedAndGeneralPathsAndLeavesAFlatSurfaceFlat)
{
    // Past the widest tap array the boxes still run their fused Float32 form over a plane wider than twice their
    // radius, and their general passes over any other: the two agree, and a box wider than the whole plane folds its
    // reach through the mirrored plane as a narrow one does at an edge.
    constexpr std::uint32_t kWidth = 900u;
    constexpr std::uint32_t kHeight = 700u;
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<float> source = randomPlane(kWidth, kHeight, 71u);
    const an::ImageLayout floatLayout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    const an::ImageLayout doubleLayout = planarLayout(an::SampleType::Float64, kWidth, kHeight);
    const std::vector<double> sourceDoubles(source.begin(), source.end());

    const an::Result<an::GaussianPlan> plan = an::planRealtimePreviewGaussianBlur({.sigmaX = 60.0, .sigmaY = 60.0});
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();
    ASSERT_GT(plan->horizontalRadius(), an::kMaxStencilRadius);
    std::vector<float> fused(source.size(), 0.0f);
    std::vector<double> general(source.size(), 0.0);
    ASSERT_TRUE(an::blurImageRealtimePreviewGaussian(execution, an::SampleType::Float32, source.data(), floatLayout,
                                                     fused.data(), floatLayout, *plan)
                    .hasValue());
    ASSERT_TRUE(an::blurImageRealtimePreviewGaussian(execution, an::SampleType::Float64, sourceDoubles.data(),
                                                     doubleLayout, general.data(), doubleLayout, *plan)
                    .hasValue());
    double worst = 0.0;
    for (std::size_t index = 0u; index < source.size(); ++index)
    {
        worst = (std::max)(worst, std::abs(static_cast<double>(fused[index]) - general[index]));
    }
    // The fused form keeps its running sums in Float32, the general one in Float64.
    EXPECT_LT(worst, 1.0e-4);

    const std::vector<float> flat(source.size(), 0.375f);
    const an::Result<an::GaussianPlan> wider = an::planRealtimePreviewGaussianBlur({.sigmaX = 800.0, .sigmaY = 800.0});
    ASSERT_TRUE(wider.hasValue()) << wider.status().message();
    ASSERT_GT(wider->horizontalRadius(), kWidth);
    std::vector<float> blurred(source.size(), 0.0f);
    ASSERT_TRUE(an::blurImageRealtimePreviewGaussian(execution, an::SampleType::Float32, flat.data(), floatLayout,
                                                     blurred.data(), floatLayout, *wider)
                    .hasValue());
    for (const float value : blurred)
    {
        ASSERT_NEAR(value, 0.375f, 1.0e-5f);
    }
}

TEST(GaussianTest, APlanRefusesWhatItCannotBlur)
{
    EXPECT_EQ(an::planGaussianBlur({.sigmaX = 0.0}).status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::planGaussianBlur({.sigmaY = std::numeric_limits<double>::quiet_NaN()}).status().code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::planGaussianBlur({.truncation = 0.5}).status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::planGaussianBlur({.method = static_cast<an::GaussianMethod>(9)}).status().code(),
              an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(an::planGaussianBlur({.method = an::GaussianMethod::RepeatedBox}).status().code(),
              an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(an::planGaussianBlur({.borders = {.border = an::KernelBorderMode::NotApplicable}}).status().code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::planGaussianBlur({.sigmaX = 64.0, .sigmaY = 64.0}).status().code(), an::ErrorCode::OutOfRange);

    an::GaussianRequest rescaling{};
    rescaling.output.writeBack.domain = an::DomainMapping::FullScale;
    EXPECT_EQ(an::planGaussianBlur(rescaling).status().code(), an::ErrorCode::InvalidArgument);
}

TEST(GaussianTest, TheMethodNamesAreThePublishedOnes)
{
    EXPECT_STREQ(an::gaussianMethodName(an::GaussianMethod::SeparableTaps), "SeparableTaps");
    EXPECT_STREQ(an::gaussianMethodName(an::GaussianMethod::RepeatedBox), "RepeatedBox");
    EXPECT_STREQ(an::gaussianMethodName(static_cast<an::GaussianMethod>(7)), "UnknownGaussianMethod");
    EXPECT_TRUE(an::isGaussianMethod(an::GaussianMethod::RepeatedBox));
    EXPECT_FALSE(an::isGaussianMethod(static_cast<an::GaussianMethod>(7)));
}

TEST(GaussianTest, TheExactBlurIsTheSeparableConvolutionOfItsOwnTaps)
{
    constexpr std::uint32_t kWidth = 33u;
    constexpr std::uint32_t kHeight = 27u;
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<float> source = randomPlane(kWidth, kHeight, 41u);
    std::vector<float> viaBlur(source.size(), 0.0f);
    std::vector<float> viaTaps(source.size(), 0.0f);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);

    const an::Result<an::GaussianPlan> plan = an::planGaussianBlur({.sigmaX = 1.7, .sigmaY = 1.7});
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();
    const an::Result<an::SpatialFilterResult> blurred = an::blurImageGaussian(
        execution, an::SampleType::Float32, source.data(), layout, viaBlur.data(), layout, plan.value());
    ASSERT_TRUE(blurred.hasValue()) << blurred.status().message();
    EXPECT_EQ(blurred.value().imagePasses, 2u);
    EXPECT_EQ(blurred.value().horizontalRadius, plan.value().horizontalRadius());

    const std::uint32_t tapCount = 2u * plan.value().horizontalRadius() + 1u;
    std::vector<double> taps(tapCount, 0.0);
    ASSERT_TRUE(an::writeGaussianTaps(1.7, plan.value().horizontalRadius(), taps).hasValue());
    const an::SeparableConvolutionRequest request{.horizontalTaps = taps,
                                                  .verticalTaps = taps,
                                                  .borders = {.border = an::KernelBorderMode::ReflectExcludingEdge}};
    ASSERT_TRUE(an::convolveImageSeparable(execution, an::SampleType::Float32, source.data(), layout, viaTaps.data(),
                                           layout, request)
                    .hasValue());

    for (std::size_t index = 0u; index < source.size(); ++index)
    {
        EXPECT_FLOAT_EQ(viaBlur[index], viaTaps[index]) << "at " << index;
    }
}

TEST(GaussianTest, BothMethodsLeaveAFlatSurfaceFlat)
{
    constexpr std::uint32_t kWidth = 24u;
    constexpr std::uint32_t kHeight = 18u;
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<float> source(static_cast<std::size_t>(kWidth) * kHeight, 0.375f);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);

    for (const an::GaussianMethod method : {an::GaussianMethod::SeparableTaps, an::GaussianMethod::RepeatedBox})
    {
        std::vector<float> destination(source.size(), 0.0f);
        const an::Result<an::GaussianPlan> plan =
            method == an::GaussianMethod::SeparableTaps
                ? an::planGaussianBlur({.sigmaX = 2.5, .sigmaY = 2.5})
                : an::planRealtimePreviewGaussianBlur({.sigmaX = 2.5, .sigmaY = 2.5});
        ASSERT_TRUE(plan.hasValue()) << plan.status().message();
        const an::Result<an::SpatialFilterResult> blurred =
            method == an::GaussianMethod::SeparableTaps
                ? an::blurImageGaussian(execution, an::SampleType::Float32, source.data(), layout, destination.data(),
                                        layout, plan.value())
                : an::blurImageRealtimePreviewGaussian(execution, an::SampleType::Float32, source.data(), layout,
                                                       destination.data(), layout, plan.value());
        ASSERT_TRUE(blurred.hasValue()) << blurred.status().message();
        EXPECT_EQ(blurred.value().run.outputsWritten, source.size()) << an::gaussianMethodName(method);
        for (const float sample : destination)
        {
            EXPECT_NEAR(sample, 0.375f, 1e-6f) << an::gaussianMethodName(method);
        }
    }
}

TEST(GaussianTest, AProductionWideGaussianMatchesThePreciseReferenceWithinTheFloat32Contract)
{
    constexpr std::uint32_t kWidth = 257u;
    constexpr std::uint32_t kHeight = 257u;
    constexpr double kSigma = 30.0;
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext automatic = makeExecution(engine, 3u, an::ComputeBackend::Automatic);
    const an::ExecutionContext reference = makeExecution(engine, 3u, an::ComputeBackend::Reference);
    const std::vector<float> source = randomPlane(kWidth, kHeight, 47u);
    std::vector<float> fast(source.size(), 0.0f);
    std::vector<float> precise(source.size(), 0.0f);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    const an::Result<an::GaussianPlan> plan = an::planGaussianBlur({.sigmaX = kSigma, .sigmaY = kSigma});
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();

    const an::Result<an::SpatialFilterResult> ranFast = an::blurImageGaussian(
        automatic, an::SampleType::Float32, source.data(), layout, fast.data(), layout, plan.value());
    const an::Result<an::SpatialFilterResult> ranPrecise = an::blurImageGaussian(
        reference, an::SampleType::Float32, source.data(), layout, precise.data(), layout, plan.value());
    ASSERT_TRUE(ranFast.hasValue()) << ranFast.status().message();
    ASSERT_TRUE(ranPrecise.hasValue()) << ranPrecise.status().message();
    EXPECT_EQ(ranFast->workingBytes, source.size() * sizeof(float));

    float worst = 0.0f;
    for (std::size_t index = 0u; index < source.size(); ++index)
    {
        worst = (std::max)(worst, std::abs(fast[index] - precise[index]));
    }
    EXPECT_LT(worst, 1e-5f);
}

TEST(GaussianTest, TheApproximationIsCloseToTheGaussianItApproximates)
{
    constexpr std::uint32_t kWidth = 64u;
    constexpr std::uint32_t kHeight = 48u;
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<float> source = randomPlane(kWidth, kHeight, 53u);
    std::vector<float> exact(source.size(), 0.0f);
    std::vector<float> approximate(source.size(), 0.0f);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);

    const an::Result<an::GaussianPlan> exactPlan = an::planGaussianBlur({.sigmaX = 4.0, .sigmaY = 4.0});
    const an::Result<an::GaussianPlan> boxPlan = an::planRealtimePreviewGaussianBlur({.sigmaX = 4.0, .sigmaY = 4.0});
    ASSERT_TRUE(exactPlan.hasValue());
    ASSERT_TRUE(boxPlan.hasValue());
    ASSERT_TRUE(an::blurImageGaussian(execution, an::SampleType::Float32, source.data(), layout, exact.data(), layout,
                                      exactPlan.value())
                    .hasValue());
    const an::Result<an::SpatialFilterResult> boxed = an::blurImageRealtimePreviewGaussian(
        execution, an::SampleType::Float32, source.data(), layout, approximate.data(), layout, boxPlan.value());
    ASSERT_TRUE(boxed.hasValue()) << boxed.status().message();
    EXPECT_EQ(boxed.value().imagePasses, 6u);

    float worst = 0.0f;
    for (std::size_t index = 0u; index < source.size(); ++index)
    {
        worst = (std::max)(worst, std::abs(exact[index] - approximate[index]));
    }
    // Three box passes are an approximation and not a route to the same picture. A few parts in a
    // thousand of the unit interval is what that costs, and pinning it is what stops the
    // approximation quietly becoming worse.
    EXPECT_LT(worst, 0.01f);
    EXPECT_GT(worst, 0.0f);
}

TEST(GaussianTest, TheApproximationLeavesAnInvalidCentreUntouched)
{
    constexpr std::uint32_t kWidth = 16u;
    constexpr std::uint32_t kHeight = 12u;
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    std::vector<float> source = randomPlane(kWidth, kHeight, 59u);
    source[40] = std::numeric_limits<float>::quiet_NaN();
    std::vector<float> destination(source.size(), -3.5f);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);

    const std::optional<an::ValidityDescriptor> finiteOnly =
        an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly});
    ASSERT_TRUE(finiteOnly.has_value());
    an::SpatialSelection selection{};
    selection.validity = *finiteOnly;

    const an::Result<an::GaussianPlan> plan = an::planRealtimePreviewGaussianBlur({.sigmaX = 2.0, .sigmaY = 2.0});
    ASSERT_TRUE(plan.hasValue());
    const an::Result<an::SpatialFilterResult> blurred = an::blurImageRealtimePreviewGaussian(
        execution, an::SampleType::Float32, source.data(), layout, destination.data(), layout, plan.value(), selection);
    ASSERT_TRUE(blurred.hasValue()) << blurred.status().message();
    EXPECT_FLOAT_EQ(destination[40], -3.5f);
    EXPECT_EQ(blurred.value().run.outputsWritten, source.size() - 1u);
    for (std::size_t index = 0u; index < source.size(); ++index)
    {
        if (index != 40u)
        {
            EXPECT_TRUE(std::isfinite(destination[index])) << "at " << index;
        }
    }
}

TEST(GaussianTest, ProductionAndRealtimePreviewPlansCannotCrossExecutionEntrypoints)
{
    constexpr std::uint32_t kWidth = 8u;
    constexpr std::uint32_t kHeight = 6u;
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<float> source(static_cast<std::size_t>(kWidth) * kHeight, 0.25f);
    std::vector<float> destination(source.size(), 0.0f);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    const an::Result<an::GaussianPlan> exact = an::planGaussianBlur({.sigmaX = 1.0, .sigmaY = 1.0});
    const an::Result<an::GaussianPlan> preview = an::planRealtimePreviewGaussianBlur({.sigmaX = 1.0, .sigmaY = 1.0});
    ASSERT_TRUE(exact.hasValue());
    ASSERT_TRUE(preview.hasValue());

    EXPECT_EQ(an::blurImageGaussian(execution, an::SampleType::Float32, source.data(), layout, destination.data(),
                                    layout, preview.value())
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(an::blurImageRealtimePreviewGaussian(execution, an::SampleType::Float32, source.data(), layout,
                                                   destination.data(), layout, exact.value())
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
}

TEST(GaussianTest, AThreeChannelSixteenBitImageIsBlurredChannelByChannel)
{
    constexpr std::uint32_t kWidth = 12u;
    constexpr std::uint32_t kHeight = 10u;
    constexpr std::uint32_t kChannels = 3u;
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    std::vector<std::uint16_t> source(static_cast<std::size_t>(kWidth) * kHeight * kChannels, 0u);
    for (std::uint32_t channel = 0u; channel < kChannels; ++channel)
    {
        for (std::uint32_t index = 0u; index < kWidth * kHeight; ++index)
        {
            source[static_cast<std::size_t>(channel) * kWidth * kHeight + index] =
                static_cast<std::uint16_t>(1000u * (channel + 1u));
        }
    }
    std::vector<std::uint16_t> destination(source.size(), 0u);
    const an::ImageLayout layout = planarLayout(an::SampleType::UInt16, kWidth, kHeight, kChannels);

    const an::Result<an::GaussianPlan> plan = an::planGaussianBlur({.sigmaX = 1.2, .sigmaY = 1.2});
    ASSERT_TRUE(plan.hasValue());
    const an::Result<an::SpatialFilterResult> blurred = an::blurImageGaussian(
        execution, an::SampleType::UInt16, source.data(), layout, destination.data(), layout, plan.value());
    ASSERT_TRUE(blurred.hasValue()) << blurred.status().message();
    EXPECT_EQ(blurred.value().channelCount, kChannels);
    EXPECT_FALSE(blurred.value().readInPlace);
    for (std::uint32_t channel = 0u; channel < kChannels; ++channel)
    {
        for (std::uint32_t index = 0u; index < kWidth * kHeight; ++index)
        {
            EXPECT_EQ(destination[static_cast<std::size_t>(channel) * kWidth * kHeight + index],
                      static_cast<std::uint16_t>(1000u * (channel + 1u)))
                << "channel " << channel;
        }
    }
}

} // namespace
