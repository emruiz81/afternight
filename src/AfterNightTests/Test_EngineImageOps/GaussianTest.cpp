// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the Gaussian blur, exact and approximated.

#include <AfterNightEngine/Algorithms/Spatial/Gaussian.h>

#include <AfterNightEngine/Algorithms/Spatial/Convolution.h>
#include <AfterNightEngine/Core/ErrorCode.h>

#include "SpatialFilterFixture.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
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
    EXPECT_EQ(an::writeGaussianTaps(1.0, an::kMaxGaussianRadius + 1u, taps).status().code(), an::ErrorCode::OutOfRange);
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
    // hundred pixels on the boxes, and an exact plan of a sigma whose kernel no tap array holds is refused.
    const an::Result<an::GaussianPlan> wide = an::planRealtimePreviewGaussianBlur({.sigmaX = 300.0, .sigmaY = 120.0});
    ASSERT_TRUE(wide.hasValue()) << wide.status().message();
    EXPECT_GT(wide->horizontalRadius(), an::kMaxStencilRadius);
    EXPECT_GT(wide->verticalRadius(), an::kMaxStencilRadius);
    EXPECT_LE(wide->horizontalRadius(), an::kMaxRealtimePreviewBoxReach);
    EXPECT_NEAR(wide->realizedSigmaX(), 300.0, 1.0);
    EXPECT_NEAR(wide->realizedSigmaY(), 120.0, 1.0);
    EXPECT_EQ(an::planGaussianBlur({.sigmaX = 2000.0, .sigmaY = 2000.0}).status().code(), an::ErrorCode::OutOfRange);

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
    EXPECT_EQ(an::planGaussianBlur({.sigmaX = 1100.0, .sigmaY = 1100.0}).status().code(), an::ErrorCode::OutOfRange);

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

// A plan wider than the stencil family's widest kernel is one exact kernel on the compiled symmetric path: its taps,
// written for the plan's own radius, sum to one, and the blur is the Gaussian computed here in double precision, a
// neighbour beyond the edge its mirror image, to within the Float32 sums the path keeps.
TEST(GaussianTest, AWideExactPlanIsOneKernelOnTheSymmetricPath)
{
    constexpr std::uint32_t kWidth = 400u;
    constexpr std::uint32_t kHeight = 340u;
    constexpr double kSigma = 40.0;
    const an::Result<an::GaussianPlan> plan = an::planGaussianBlur({.sigmaX = kSigma, .sigmaY = kSigma});
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();
    const std::uint32_t radius = plan->horizontalRadius();
    ASSERT_EQ(radius, 160u);
    ASSERT_GT(radius, an::kMaxStencilRadius);

    std::vector<double> taps(2u * radius + 1u, 0.0);
    ASSERT_TRUE(an::writeGaussianTaps(kSigma, radius, taps).hasValue());
    double total = 0.0;
    for (const double tap : taps)
    {
        total += tap;
    }
    EXPECT_NEAR(total, 1.0, 1e-12);

    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<float> source = randomPlane(kWidth, kHeight, 59u);
    std::vector<float> blurred(source.size(), 0.0f);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    const an::Result<an::SpatialFilterResult> ran =
        an::blurImageGaussian(execution, an::SampleType::Float32, source.data(), layout, blurred.data(), layout, *plan);
    ASSERT_TRUE(ran.hasValue()) << ran.status().message();
    EXPECT_EQ(ran->horizontalRadius, radius);
    EXPECT_EQ(ran->imagePasses, 2u);

    const auto mirrored = [](std::int64_t index, std::int64_t length) {
        const std::int64_t period = 2 * (length - 1);
        std::int64_t folded = index % period;
        if (folded < 0)
        {
            folded += period;
        }
        return folded < length ? folded : period - folded;
    };
    const std::int64_t width = kWidth;
    const std::int64_t height = kHeight;
    const std::int64_t reach = radius;
    std::vector<double> across(source.size(), 0.0);
    for (std::int64_t y = 0; y < height; ++y)
    {
        for (std::int64_t x = 0; x < width; ++x)
        {
            double sum = 0.0;
            for (std::int64_t k = -reach; k <= reach; ++k)
            {
                sum += taps[static_cast<std::size_t>(k + reach)] *
                       static_cast<double>(source[static_cast<std::size_t>(y * width + mirrored(x + k, width))]);
            }
            across[static_cast<std::size_t>(y * width + x)] = sum;
        }
    }
    double worst = 0.0;
    for (std::int64_t y = 0; y < height; ++y)
    {
        for (std::int64_t x = 0; x < width; ++x)
        {
            double sum = 0.0;
            for (std::int64_t k = -reach; k <= reach; ++k)
            {
                sum += taps[static_cast<std::size_t>(k + reach)] *
                       across[static_cast<std::size_t>(mirrored(y + k, height) * width + x)];
            }
            worst = (std::max)(worst,
                               std::abs(static_cast<double>(blurred[static_cast<std::size_t>(y * width + x)]) - sum));
        }
    }
    EXPECT_LT(worst, 1e-5);
}

// The compiled symmetric path is, sample for sample, the Float32 sum its rows state: the centre tap's product, then
// from the nearest offset outward the two samples at that offset added before their shared tap weights them, the
// border mirrored without its edge sample - along each row, then down each column of that intermediate. Both planes
// are wide enough for the vertical pass to walk them in several runs of columns, the last a partial one, at a radius a
// tap array holds and at a wider one, so a run that reads or writes another run's columns or rows differs.
TEST(GaussianTest, TheSymmetricPathIsItsFloat32SumInEveryRunOfColumns)
{
    struct Shape
    {
        std::uint32_t width;
        std::uint32_t height;
        double sigma;
    };
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    for (const Shape shape : {Shape{1700u, 90u, 10.0}, Shape{403u, 340u, 40.0}})
    {
        SCOPED_TRACE(shape.sigma);
        const an::Result<an::GaussianPlan> plan = an::planGaussianBlur({.sigmaX = shape.sigma, .sigmaY = shape.sigma});
        ASSERT_TRUE(plan.hasValue()) << plan.status().message();
        const std::uint32_t radius = plan->horizontalRadius();
        std::vector<double> exact(2u * radius + 1u, 0.0);
        ASSERT_TRUE(an::writeGaussianTaps(shape.sigma, radius, exact).hasValue());
        std::vector<float> taps(exact.size(), 0.0f);
        std::transform(exact.begin(), exact.end(), taps.begin(), [](double tap) { return static_cast<float>(tap); });

        const std::vector<float> source = randomPlane(shape.width, shape.height, 61u);
        std::vector<float> blurred(source.size(), 0.0f);
        const an::ImageLayout layout = planarLayout(an::SampleType::Float32, shape.width, shape.height);
        const an::Result<an::SpatialFilterResult> ran = an::blurImageGaussian(
            execution, an::SampleType::Float32, source.data(), layout, blurred.data(), layout, *plan);
        ASSERT_TRUE(ran.hasValue()) << ran.status().message();

        const auto mirrored = [](std::uint32_t index, std::int64_t offset, std::uint32_t length) {
            const std::int64_t at = static_cast<std::int64_t>(index) + offset;
            const std::int64_t last = static_cast<std::int64_t>(length) - 1;
            return static_cast<std::size_t>(at < 0 ? -at : (at > last ? 2 * last - at : at));
        };
        const std::size_t width = shape.width;
        std::vector<float> across(source.size(), 0.0f);
        for (std::uint32_t y = 0u; y < shape.height; ++y)
        {
            for (std::uint32_t x = 0u; x < shape.width; ++x)
            {
                const float* const row = source.data() + y * width;
                float sum = taps[radius] * row[x];
                for (std::uint32_t k = 1u; k <= radius; ++k)
                {
                    const auto offset = static_cast<std::int64_t>(k);
                    sum += taps[radius - k] *
                           (row[mirrored(x, -offset, shape.width)] + row[mirrored(x, offset, shape.width)]);
                }
                across[y * width + x] = sum;
            }
        }
        std::uint64_t differing = 0u;
        for (std::uint32_t y = 0u; y < shape.height; ++y)
        {
            for (std::uint32_t x = 0u; x < shape.width; ++x)
            {
                float sum = taps[radius] * across[y * width + x];
                for (std::uint32_t k = 1u; k <= radius; ++k)
                {
                    const auto offset = static_cast<std::int64_t>(k);
                    sum += taps[radius - k] * (across[mirrored(y, -offset, shape.height) * width + x] +
                                               across[mirrored(y, offset, shape.height) * width + x]);
                }
                differing +=
                    std::bit_cast<std::uint32_t>(sum) != std::bit_cast<std::uint32_t>(blurred[y * width + x]) ? 1u : 0u;
            }
        }
        EXPECT_EQ(differing, 0u) << "samples that are not the Float32 sum in its order";
    }
}

// The general path's tap arrays and halo bands stop at the stencil family's widest kernel, so a wider plan is refused
// by name wherever the symmetric path does not run it: a Float64 plane, a plane under a validity rule, a plane not
// twice the radius high, and an execution forced onto the reference backend, whose table has no symmetric rows.
TEST(GaussianTest, AWideExactPlanIsRefusedWhereTheSymmetricPathDoesNotApply)
{
    const an::Result<an::GaussianPlan> plan = an::planGaussianBlur({.sigmaX = 40.0, .sigmaY = 40.0});
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ExecutionContext reference = makeExecution(engine, 3u, an::ComputeBackend::Reference);

    const auto refusal = [&](const an::ExecutionContext& on, an::SampleType type, std::uint32_t width,
                             std::uint32_t height, const an::SpatialSelection& selection) {
        const an::ImageLayout layout = planarLayout(type, width, height);
        const std::size_t bytes = static_cast<std::size_t>(width) * height * an::sampleTypeSizeBytes(type);
        std::vector<std::uint8_t> source(bytes, 0u);
        std::vector<std::uint8_t> destination(bytes, 0u);
        const an::Result<an::SpatialFilterResult> ran =
            an::blurImageGaussian(on, type, source.data(), layout, destination.data(), layout, *plan, selection);
        return ran.hasValue() ? an::ErrorCode::Ok : ran.status().code();
    };
    EXPECT_EQ(refusal(execution, an::SampleType::Float64, 400u, 340u, {}), an::ErrorCode::OutOfRange);
    EXPECT_EQ(refusal(execution, an::SampleType::Float32, 400u, 300u, {}), an::ErrorCode::OutOfRange);
    EXPECT_EQ(refusal(reference, an::SampleType::Float32, 400u, 340u, {}), an::ErrorCode::OutOfRange);
    const std::optional<an::ValidityDescriptor> finite =
        an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly});
    ASSERT_TRUE(finite.has_value());
    EXPECT_EQ(refusal(execution, an::SampleType::Float32, 400u, 340u, an::SpatialSelection{.validity = *finite}),
              an::ErrorCode::OutOfRange);
    EXPECT_EQ(refusal(execution, an::SampleType::Float32, 400u, 340u, {}), an::ErrorCode::Ok);
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
