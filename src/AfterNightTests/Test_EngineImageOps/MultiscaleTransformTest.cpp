// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the multiscale transforms, their layers, and the operations between them.
//
// The suite is organised around the one property everything else depends on: the layers of a
// decomposition sum to the image it was made from. A weight means something only because of that, a
// shrinkage is only a denoise because of that, and a transform that quietly lost energy at an edge
// would still pass every test that only looked at one layer.

#include <AfterNightEngine/Algorithms/Kernel/StencilKernel.h>
#include <AfterNightEngine/Algorithms/Multiscale/MultiscaleTransform.h>
#include <AfterNightEngine/Algorithms/Spatial/Convolution.h>
#include <AfterNightEngine/Algorithms/Spatial/Gaussian.h>
#include <AfterNightEngine/Algorithms/Statistics/RobustStatistics.h>

#include "SpatialFilterFixture.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <random>
#include <span>
#include <utility>
#include <vector>

namespace
{

using spatial_test::interleavedLayout;
using spatial_test::makeEngine;
using spatial_test::makeExecution;
using spatial_test::planarLayout;
using spatial_test::randomPlane;

/// A validated plan, so a case that is not about planning does not spell the request twice.
///
/// @param algorithm which transform.
/// @param layerCount how many layers, the residual included.
/// @return the plan.
[[nodiscard]] an::MultiscaleTransformPlan plan(an::MultiscaleAlgorithm algorithm, std::uint32_t layerCount)
{
    an::MultiscaleTransformRequest request{};
    request.algorithm = algorithm;
    request.layerCount = layerCount;
    const an::Result<an::MultiscaleTransformPlan> planned = an::planMultiscaleTransform(request);
    EXPECT_TRUE(planned.hasValue()) << planned.status().message();
    return planned.hasValue() ? *planned : an::MultiscaleTransformPlan{};
}

/// What a transform's cascade produces from one plane, one separable pass at a time.
struct Cascade
{
    /// The plane itself, then every smoothing in the order the transform makes them.
    std::vector<std::vector<float>> smoothings;

    /// The most band memory any one of the passes held.
    std::uint64_t largestScratch = 0u;
};

/// Runs a transform's smoothings through `convolveSeparablePlane` with the plan's own filter at the
/// plan's own spacing: the starlet smooths the smoothing before, the multiscale-linear transform the
/// plane itself every time.
///
/// @param execution the operation policy.
/// @param transform the plan.
/// @param plane one channel, in packed rows.
/// @param width how many samples a row holds.
/// @param height how many rows there are.
/// @return the cascade.
[[nodiscard]] Cascade runCascade(const an::ExecutionContext& execution, const an::MultiscaleTransformPlan& transform,
                                 const std::vector<float>& plane, std::uint32_t width, std::uint32_t height)
{
    const bool starlet = transform.request().algorithm == an::MultiscaleAlgorithm::Starlet;
    const std::uint64_t stride = static_cast<std::uint64_t>(width) * sizeof(float);
    Cascade cascade;
    cascade.smoothings.push_back(plane);
    for (std::uint32_t detail = 0u; detail < transform.detailLayerCount(); ++detail)
    {
        std::array<double, an::kMaxStencilTaps> taps{};
        const std::span<double> filter{taps.data(), transform.layerTapCount(detail)};
        const an::Result<std::uint32_t> written =
            starlet ? an::writeMultiscaleScalingTaps(transform.request().scaling, filter)
                    : an::writeGaussianTaps(transform.layerSigma(detail), transform.layerTapCount(detail) / 2u, filter);
        EXPECT_TRUE(written.hasValue()) << written.status().message();
        std::vector<float> next(plane.size(), -1.0f);
        const std::optional<an::PlaneView<const float>> from = an::PlaneView<const float>::create(
            std::span<const float>{starlet ? cascade.smoothings.back() : plane}, width, height, 1u, stride);
        const std::optional<an::PlaneView<float>> into =
            an::PlaneView<float>::create(std::span<float>{next}, width, height, 1u, stride);
        EXPECT_TRUE(from.has_value() && into.has_value());
        const an::SeparableStencilRequest request{.horizontalTaps = filter,
                                                  .verticalTaps = filter,
                                                  .borders = transform.request().borders,
                                                  .dilation = transform.layerDilation(detail)};
        const an::Result<an::StencilExecutionResult> ran =
            an::convolveSeparablePlane(execution, *from, *into, request, an::KernelValidityView{});
        EXPECT_TRUE(ran.hasValue()) << ran.status().message();
        cascade.largestScratch = (std::max)(cascade.largestScratch, ran.hasValue() ? ran->scratchBytes : 0u);
        cascade.smoothings.push_back(std::move(next));
    }
    return cascade;
}

/// One channel of a planar sample buffer, as packed rows of its own.
///
/// @param samples every channel, one plane after another.
/// @param channel which one.
/// @param width how many samples a row holds.
/// @param height how many rows a plane has.
/// @return the channel's plane.
[[nodiscard]] std::vector<float> channelPlane(const std::vector<float>& samples, std::uint32_t channel,
                                              std::uint32_t width, std::uint32_t height)
{
    const std::size_t count = static_cast<std::size_t>(width) * height;
    const auto first = samples.begin() + static_cast<std::ptrdiff_t>(count * channel);
    return std::vector<float>(first, first + static_cast<std::ptrdiff_t>(count));
}

/// The same planes with their channels interleaved sample by sample.
///
/// @param samples every channel, one plane after another.
/// @param channels how many channels.
/// @param width how many samples a row holds.
/// @param height how many rows a plane has.
/// @return the interleaved samples.
[[nodiscard]] std::vector<float> interleave(const std::vector<float>& samples, std::uint32_t channels,
                                            std::uint32_t width, std::uint32_t height)
{
    std::vector<float> packed(samples.size(), 0.0f);
    const std::size_t count = static_cast<std::size_t>(width) * height;
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        for (std::size_t index = 0u; index < count; ++index)
        {
            packed[index * channels + channel] = samples[count * channel + index];
        }
    }
    return packed;
}

TEST(MultiscaleTransformTest, EveryScalingFunctionSumsToOneAndHasTheTapCountItReports)
{
    for (std::uint8_t index = 0u; index <= static_cast<std::uint8_t>(an::MultiscaleScalingFunction::Gaussian11);
         ++index)
    {
        const auto scaling = static_cast<an::MultiscaleScalingFunction>(index);
        ASSERT_TRUE(an::isMultiscaleScalingFunction(scaling));
        const std::uint32_t tapCount = an::multiscaleScalingTapCount(scaling);
        ASSERT_GT(tapCount, 0u);
        EXPECT_EQ(tapCount % 2u, 1u) << an::multiscaleScalingFunctionName(scaling);

        std::array<double, an::kMaxMultiscaleScalingTaps> taps{};
        const an::Result<std::uint32_t> written = an::writeMultiscaleScalingTaps(scaling, taps);
        ASSERT_TRUE(written.hasValue()) << written.status().message();
        EXPECT_EQ(*written, tapCount);

        double sum = 0.0;
        for (std::uint32_t tap = 0u; tap < tapCount; ++tap)
        {
            EXPECT_GT(taps[tap], 0.0) << an::multiscaleScalingFunctionName(scaling);
            sum += taps[tap];
        }
        EXPECT_NEAR(sum, 1.0, 1.0e-15) << an::multiscaleScalingFunctionName(scaling);
        // Symmetric about the centre, which is what makes a smoothing move nothing sideways.
        for (std::uint32_t tap = 0u; tap < tapCount / 2u; ++tap)
        {
            EXPECT_DOUBLE_EQ(taps[tap], taps[tapCount - 1u - tap]);
        }
    }

    // The two named kernels are exactly their published coefficients.
    std::array<double, 5u> linear{};
    ASSERT_TRUE(an::writeMultiscaleScalingTaps(an::MultiscaleScalingFunction::LinearInterpolation3, linear).hasValue());
    EXPECT_DOUBLE_EQ(linear[0], 0.25);
    EXPECT_DOUBLE_EQ(linear[1], 0.5);
    EXPECT_DOUBLE_EQ(linear[2], 0.25);
    std::array<double, 5u> spline{};
    ASSERT_TRUE(an::writeMultiscaleScalingTaps(an::MultiscaleScalingFunction::BSpline5, spline).hasValue());
    EXPECT_DOUBLE_EQ(spline[0], 1.0 / 16.0);
    EXPECT_DOUBLE_EQ(spline[2], 6.0 / 16.0);
    std::array<double, 3u> small{};
    ASSERT_TRUE(an::writeMultiscaleScalingTaps(an::MultiscaleScalingFunction::SmallScale8, small).hasValue());
    EXPECT_DOUBLE_EQ(small[1], 8.0 / 10.0);

    std::array<double, 2u> tooShort{};
    const an::Result<std::uint32_t> refused =
        an::writeMultiscaleScalingTaps(an::MultiscaleScalingFunction::BSpline5, tooShort);
    EXPECT_EQ(refused.status().code(), an::ErrorCode::OutOfRange);
    const an::Result<std::uint32_t> unknown =
        an::writeMultiscaleScalingTaps(static_cast<an::MultiscaleScalingFunction>(200u), spline);
    EXPECT_EQ(unknown.status().code(), an::ErrorCode::InvalidArgument);
}

TEST(MultiscaleTransformTest, ADilatedSeparablePassIsTheSameFilterWithHolesInIt)
{
    // The kernel change the starlet is built on, checked where a caller reaches it: three taps two
    // samples apart must produce exactly what a five-by-five dense kernel with zeroes between the
    // coefficients produces.
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 37u;
    constexpr std::uint32_t kHeight = 29u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    const std::vector<float> source = randomPlane(kWidth, kHeight, 90210u);
    std::vector<float> dilated(source.size(), -1.0f);
    std::vector<float> dense(source.size(), -1.0f);

    const std::array<double, 3u> taps{0.25, 0.5, 0.25};
    an::SeparableConvolutionRequest separable{};
    separable.horizontalTaps = taps;
    separable.verticalTaps = taps;
    separable.dilation = 2u;
    separable.borders.border = an::KernelBorderMode::ReflectExcludingEdge;
    const an::Result<an::SpatialFilterResult> ran = an::convolveImageSeparable(
        execution, an::SampleType::Float32, source.data(), layout, dilated.data(), layout, separable);
    ASSERT_TRUE(ran.hasValue()) << ran.status().message();
    EXPECT_EQ(ran->horizontalRadius, 2u);
    EXPECT_EQ(ran->verticalRadius, 2u);

    std::array<double, 25u> holes{};
    for (std::uint32_t ky = 0u; ky < 3u; ++ky)
    {
        for (std::uint32_t kx = 0u; kx < 3u; ++kx)
        {
            holes[static_cast<std::size_t>(ky * 2u) * 5u + kx * 2u] = taps[ky] * taps[kx];
        }
    }
    an::DenseConvolutionRequest denseRequest{};
    denseRequest.coefficients = holes;
    denseRequest.width = 5u;
    denseRequest.height = 5u;
    denseRequest.borders.border = an::KernelBorderMode::ReflectExcludingEdge;
    const an::Result<an::SpatialFilterResult> reference = an::convolveImageDense(
        execution, an::SampleType::Float32, source.data(), layout, dense.data(), layout, denseRequest);
    ASSERT_TRUE(reference.hasValue()) << reference.status().message();

    for (std::size_t index = 0u; index < source.size(); ++index)
    {
        ASSERT_NEAR(dilated[index], dense[index], 1.0e-6f) << "at " << index;
    }

    // And the spacing is checked rather than assumed.
    separable.dilation = 0u;
    EXPECT_EQ(an::convolveImageSeparable(execution, an::SampleType::Float32, source.data(), layout, dilated.data(),
                                         layout, separable)
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
    separable.dilation = an::kMaxStencilDilation + 1u;
    EXPECT_EQ(an::convolveImageSeparable(execution, an::SampleType::Float32, source.data(), layout, dilated.data(),
                                         layout, separable)
                  .status()
                  .code(),
              an::ErrorCode::OutOfRange);
}

TEST(MultiscaleTransformTest, ADilatedPassCostsWhatAnUndilatedOneCostsInBandMemory)
{
    // The reason the tile is walked one residue class at a time: without it a band would hold the
    // whole `2 * radius * dilation` footprint and the deepest layers would leave the scratch
    // ceiling. The two runs are the same picture and the same taps, so the difference reported is
    // the spacing alone.
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 256u;
    constexpr std::uint32_t kHeight = 192u;
    const std::vector<float> source = randomPlane(kWidth, kHeight, 4242u);
    std::vector<float> destination(source.size(), 0.0f);
    const std::array<double, 5u> taps{1.0 / 16.0, 4.0 / 16.0, 6.0 / 16.0, 4.0 / 16.0, 1.0 / 16.0};

    const std::optional<an::PlaneView<const float>> input = an::PlaneView<const float>::create(
        std::span<const float>{source}, kWidth, kHeight, 1u, static_cast<std::uint64_t>(kWidth) * sizeof(float));
    const std::optional<an::PlaneView<float>> output = an::PlaneView<float>::create(
        std::span<float>{destination}, kWidth, kHeight, 1u, static_cast<std::uint64_t>(kWidth) * sizeof(float));
    ASSERT_TRUE(input.has_value());
    ASSERT_TRUE(output.has_value());

    an::SeparableStencilRequest request{};
    request.horizontalTaps = taps;
    request.verticalTaps = taps;
    request.borders.border = an::KernelBorderMode::ReflectExcludingEdge;
    const an::Result<an::StencilExecutionResult> undilated =
        an::convolveSeparablePlane(execution, *input, *output, request, an::KernelValidityView{});
    ASSERT_TRUE(undilated.hasValue()) << undilated.status().message();

    request.dilation = 64u;
    const an::Result<an::StencilExecutionResult> spread =
        an::convolveSeparablePlane(execution, *input, *output, request, an::KernelValidityView{});
    ASSERT_TRUE(spread.hasValue()) << spread.status().message();
    EXPECT_EQ(spread->dilation, 64u);
    EXPECT_EQ(spread->haloRows, 128u);
    EXPECT_EQ(spread->verticalRadius, 2u);
    // The whole point: the working set stays below what holding the spanned footprint would cost.
    EXPECT_LT(spread->scratchBytes, spread->haloRows * 2u * static_cast<std::uint64_t>(kWidth) * sizeof(double));
    EXPECT_LE(spread->scratchBytes, an::kMaxStencilScratchBytes);
    EXPECT_EQ(spread->run.outputsWritten, static_cast<std::uint64_t>(kWidth) * kHeight);
}

TEST(MultiscaleTransformTest, TheLayersOfAStarletDecompositionSumToTheImage)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 61u;
    constexpr std::uint32_t kHeight = 47u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    const std::vector<float> source = randomPlane(kWidth, kHeight, 13579u);
    std::vector<float> rebuilt(source.size(), -1.0f);

    const an::MultiscaleTransformPlan starlet = plan(an::MultiscaleAlgorithm::Starlet, 5u);
    EXPECT_EQ(starlet.layerCount(), 5u);
    EXPECT_EQ(starlet.detailLayerCount(), 4u);
    EXPECT_EQ(starlet.residualLayer(), 4u);
    EXPECT_EQ(starlet.imagePasses(), 13u);
    for (std::uint32_t detail = 0u; detail < starlet.detailLayerCount(); ++detail)
    {
        EXPECT_EQ(starlet.layerScale(detail), std::uint32_t{1} << detail);
        EXPECT_EQ(starlet.layerDilation(detail), std::uint32_t{1} << detail);
        EXPECT_EQ(starlet.layerTapCount(detail), 5u);
        EXPECT_EQ(starlet.layerRadius(detail), 2u * (std::uint32_t{1} << detail));
        EXPECT_DOUBLE_EQ(starlet.layerSigma(detail), 0.0);
    }

    const an::Result<an::MultiscaleDecomposition> decomposed =
        an::decomposeImageMultiscale(engine, execution, an::SampleType::Float32, source.data(), layout, starlet);
    ASSERT_TRUE(decomposed.hasValue()) << decomposed.status().message();
    EXPECT_EQ(decomposed->layerCount(), 5u);
    EXPECT_EQ(decomposed->channelCount(), 1u);
    EXPECT_EQ(decomposed->width(), kWidth);
    EXPECT_EQ(decomposed->height(), kHeight);
    EXPECT_EQ(decomposed->result().substitutedSamples, 0u);
    EXPECT_EQ(decomposed->result().imagePasses, 13u);
    EXPECT_EQ(decomposed->result().run.outputsWritten, static_cast<std::uint64_t>(kWidth) * kHeight * 5u);
    // The allocation carries aligned rows, so it holds the coefficients the plan counts and the
    // padding the plan has no way to know about.
    EXPECT_GE(decomposed->layerBytes(), starlet.layerBytes(kWidth, kHeight, 1u));
    EXPECT_EQ(starlet.layerBytes(kWidth, kHeight, 1u),
              static_cast<std::uint64_t>(kWidth) * kHeight * 5u * sizeof(float));

    const an::Result<an::SpatialFilterResult> summed =
        an::reconstructImageMultiscale(execution, *decomposed, an::SampleType::Float32, rebuilt.data(), layout);
    ASSERT_TRUE(summed.hasValue()) << summed.status().message();
    EXPECT_TRUE(summed->wroteInPlace);
    EXPECT_EQ(summed->imagePasses, 1u);
    EXPECT_EQ(summed->run.outputsWritten, static_cast<std::uint64_t>(kWidth) * kHeight);

    for (std::size_t index = 0u; index < source.size(); ++index)
    {
        ASSERT_NEAR(rebuilt[index], source[index], 1.0e-6f) << "at " << index;
    }
}

TEST(MultiscaleTransformTest, ImagePassesCountAlgorithmsRatherThanChannelsOrBufferTransfers)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 7u;
    constexpr std::uint32_t kHeight = 5u;
    constexpr std::uint32_t kChannels = 3u;
    const an::ImageLayout layout = interleavedLayout(an::SampleType::Float32, kWidth, kHeight, kChannels);
    const std::vector<float> source(static_cast<std::size_t>(kWidth) * kHeight * kChannels, 0.25f);
    std::vector<float> destination(source.size(), 0.0f);
    const an::MultiscaleTransformPlan transform = plan(an::MultiscaleAlgorithm::Starlet, 4u);

    const an::Result<an::MultiscaleDecomposition> decomposed =
        an::decomposeImageMultiscale(engine, execution, an::SampleType::Float32, source.data(), layout, transform);
    ASSERT_TRUE(decomposed.hasValue()) << decomposed.status().message();
    EXPECT_EQ(decomposed->result().imagePasses, 10u);

    const an::Result<an::SpatialFilterResult> reconstructed =
        an::reconstructImageMultiscale(execution, *decomposed, an::SampleType::Float32, destination.data(), layout);
    ASSERT_TRUE(reconstructed.hasValue()) << reconstructed.status().message();
    EXPECT_EQ(reconstructed->imagePasses, 1u);
}

TEST(MultiscaleTransformTest, ReconstructionAccumulatesEveryLayerInFloat64BeforeStoring)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, 2u, 2u);
    const std::vector<float> source(4u, 0.0f);
    an::Result<an::MultiscaleDecomposition> decomposed = an::decomposeImageMultiscale(
        engine, execution, an::SampleType::Float32, source.data(), layout, plan(an::MultiscaleAlgorithm::Starlet, 3u));
    ASSERT_TRUE(decomposed.hasValue()) << decomposed.status().message();

    constexpr std::array<float, 3u> kCoefficients{100000000.0f, 1.0f, -100000000.0f};
    for (std::uint32_t layer = 0u; layer < kCoefficients.size(); ++layer)
    {
        const std::optional<an::PlaneView<float>> coefficients = decomposed->layer(0u, layer);
        ASSERT_TRUE(coefficients.has_value());
        for (std::uint32_t y = 0u; y < coefficients->height(); ++y)
        {
            std::fill(coefficients->row(y)->begin(), coefficients->row(y)->end(), kCoefficients[layer]);
        }
    }

    std::vector<float> destination(source.size(), 0.0f);
    const an::Result<an::SpatialFilterResult> reconstructed =
        an::reconstructImageMultiscale(execution, *decomposed, an::SampleType::Float32, destination.data(), layout);
    ASSERT_TRUE(reconstructed.hasValue()) << reconstructed.status().message();
    for (const float value : destination)
    {
        EXPECT_FLOAT_EQ(value, 1.0f);
    }
}

TEST(MultiscaleTransformTest, TheLayersOfAMultiscaleLinearDecompositionSumToTheImage)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 53u;
    constexpr std::uint32_t kHeight = 41u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    const std::vector<float> source = randomPlane(kWidth, kHeight, 24680u);
    std::vector<float> rebuilt(source.size(), -1.0f);

    const an::MultiscaleTransformPlan linear = plan(an::MultiscaleAlgorithm::MultiscaleLinear, 4u);
    for (std::uint32_t detail = 0u; detail < linear.detailLayerCount(); ++detail)
    {
        EXPECT_EQ(linear.layerDilation(detail), 1u);
        EXPECT_NEAR(linear.layerSigma(detail), static_cast<double>(std::uint32_t{1} << detail) / 3.0, 1.0e-12);
        EXPECT_GT(linear.layerTapCount(detail), 2u);
        EXPECT_EQ(linear.layerRadius(detail), linear.layerTapCount(detail) / 2u);
    }

    const an::Result<an::MultiscaleDecomposition> decomposed =
        an::decomposeImageMultiscale(engine, execution, an::SampleType::Float32, source.data(), layout, linear);
    ASSERT_TRUE(decomposed.hasValue()) << decomposed.status().message();
    const an::Result<an::SpatialFilterResult> summed =
        an::reconstructImageMultiscale(execution, *decomposed, an::SampleType::Float32, rebuilt.data(), layout);
    ASSERT_TRUE(summed.hasValue()) << summed.status().message();
    for (std::size_t index = 0u; index < source.size(); ++index)
    {
        ASSERT_NEAR(rebuilt[index], source[index], 1.0e-6f) << "at " << index;
    }
}

TEST(MultiscaleTransformTest, AFlatSurfaceHasNoDetailAndAllOfItsEnergyInTheResidual)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 24u;
    constexpr std::uint32_t kHeight = 18u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    const std::vector<float> source(static_cast<std::size_t>(kWidth) * kHeight, 0.375f);

    for (const an::MultiscaleAlgorithm algorithm :
         {an::MultiscaleAlgorithm::Starlet, an::MultiscaleAlgorithm::MultiscaleLinear})
    {
        const an::Result<an::MultiscaleDecomposition> decomposed = an::decomposeImageMultiscale(
            engine, execution, an::SampleType::Float32, source.data(), layout, plan(algorithm, 4u));
        ASSERT_TRUE(decomposed.hasValue()) << decomposed.status().message();
        for (std::uint32_t detail = 0u; detail < decomposed->detailLayerCount(); ++detail)
        {
            const std::optional<an::PlaneView<const float>> layer = decomposed->layer(0u, detail);
            ASSERT_TRUE(layer.has_value());
            for (std::uint32_t y = 0u; y < kHeight; ++y)
            {
                const std::span<const float> row = *layer->row(y);
                for (const float coefficient : row)
                {
                    ASSERT_NEAR(coefficient, 0.0f, 1.0e-6f) << an::multiscaleAlgorithmName(algorithm);
                }
            }
        }
        const std::optional<an::PlaneView<const float>> residual =
            decomposed->layer(0u, decomposed->detailLayerCount());
        ASSERT_TRUE(residual.has_value());
        for (std::uint32_t y = 0u; y < kHeight; ++y)
        {
            const std::span<const float> row = *residual->row(y);
            for (const float coefficient : row)
            {
                ASSERT_NEAR(coefficient, 0.375f, 1.0e-6f) << an::multiscaleAlgorithmName(algorithm);
            }
        }
    }
}

TEST(MultiscaleTransformTest, ALayerWeightDecidesWhatSurvivesTheReconstruction)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 33u;
    constexpr std::uint32_t kHeight = 27u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    const std::vector<float> source = randomPlane(kWidth, kHeight, 777u);
    std::vector<float> residualOnly(source.size(), -1.0f);
    std::vector<float> doubled(source.size(), -1.0f);

    const an::MultiscaleTransformPlan starlet = plan(an::MultiscaleAlgorithm::Starlet, 4u);
    const an::Result<an::MultiscaleDecomposition> decomposed =
        an::decomposeImageMultiscale(engine, execution, an::SampleType::Float32, source.data(), layout, starlet);
    ASSERT_TRUE(decomposed.hasValue()) << decomposed.status().message();

    std::array<an::MultiscaleLayerWeight, 4u> weights{};
    for (std::uint32_t layer = 0u; layer < 3u; ++layer)
    {
        weights[layer] = {.enabled = false, .gain = 1.0};
    }
    weights[3u] = {.enabled = true, .gain = 1.0};
    an::MultiscaleReconstructionRequest request{};
    request.layers = weights;
    ASSERT_TRUE(an::reconstructImageMultiscale(execution, *decomposed, an::SampleType::Float32, residualOnly.data(),
                                               layout, request)
                    .hasValue());

    const std::optional<an::PlaneView<const float>> residual = decomposed->layer(0u, 3u);
    ASSERT_TRUE(residual.has_value());
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        const std::span<const float> row = *residual->row(y);
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            ASSERT_NEAR(residualOnly[static_cast<std::size_t>(y) * kWidth + x], row[x], 1.0e-6f);
        }
    }

    // Doubling the finest detail adds exactly that layer to the image.
    for (an::MultiscaleLayerWeight& weight : weights)
    {
        weight = {.enabled = true, .gain = 1.0};
    }
    weights[0u].gain = 2.0;
    request.layers = weights;
    ASSERT_TRUE(
        an::reconstructImageMultiscale(execution, *decomposed, an::SampleType::Float32, doubled.data(), layout, request)
            .hasValue());
    const std::optional<an::PlaneView<const float>> finest = decomposed->layer(0u, 0u);
    ASSERT_TRUE(finest.has_value());
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        const std::span<const float> row = *finest->row(y);
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            const std::size_t index = static_cast<std::size_t>(y) * kWidth + x;
            ASSERT_NEAR(doubled[index], source[index] + row[x], 1.0e-5f);
        }
    }
}

TEST(MultiscaleTransformTest, AReconstructionHoldsItsValuesInsideAStatedInterval)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 16u;
    constexpr std::uint32_t kHeight = 12u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    const std::vector<float> source = randomPlane(kWidth, kHeight, 31337u);
    std::vector<float> clamped(source.size(), -1.0f);

    const an::Result<an::MultiscaleDecomposition> decomposed = an::decomposeImageMultiscale(
        engine, execution, an::SampleType::Float32, source.data(), layout, plan(an::MultiscaleAlgorithm::Starlet, 3u));
    ASSERT_TRUE(decomposed.hasValue()) << decomposed.status().message();

    std::array<an::MultiscaleLayerWeight, 3u> weights{};
    for (an::MultiscaleLayerWeight& weight : weights)
    {
        weight = {.enabled = true, .gain = 4.0};
    }
    an::MultiscaleReconstructionRequest request{};
    request.layers = weights;
    request.clamp = an::KernelValueWindow{.lowest = 0.25, .highest = 0.75};
    ASSERT_TRUE(
        an::reconstructImageMultiscale(execution, *decomposed, an::SampleType::Float32, clamped.data(), layout, request)
            .hasValue());
    for (const float value : clamped)
    {
        ASSERT_GE(value, 0.25f);
        ASSERT_LE(value, 0.75f);
    }

    request.clamp = an::KernelValueWindow{.lowest = 0.75, .highest = 0.25};
    EXPECT_EQ(
        an::reconstructImageMultiscale(execution, *decomposed, an::SampleType::Float32, clamped.data(), layout, request)
            .status()
            .code(),
        an::ErrorCode::InvalidArgument);
}

TEST(MultiscaleTransformTest, AnInterleavedImageDecomposesIntoTheSameLayersAsAPlanarOne)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 21u;
    constexpr std::uint32_t kHeight = 17u;
    constexpr std::uint32_t kChannels = 3u;
    const an::ImageLayout planar = planarLayout(an::SampleType::Float32, kWidth, kHeight, kChannels);
    const an::ImageLayout packed = interleavedLayout(an::SampleType::Float32, kWidth, kHeight, kChannels);
    const std::vector<float> planarSamples = randomPlane(kWidth, kHeight * kChannels, 8642u);
    std::vector<float> packedSamples(planarSamples.size(), 0.0f);
    for (std::uint32_t channel = 0u; channel < kChannels; ++channel)
    {
        for (std::uint32_t y = 0u; y < kHeight; ++y)
        {
            for (std::uint32_t x = 0u; x < kWidth; ++x)
            {
                const std::size_t from = (static_cast<std::size_t>(channel) * kHeight + y) * kWidth + x;
                const std::size_t to = (static_cast<std::size_t>(y) * kWidth + x) * kChannels + channel;
                packedSamples[to] = planarSamples[from];
            }
        }
    }

    const an::MultiscaleTransformPlan starlet = plan(an::MultiscaleAlgorithm::Starlet, 3u);
    const an::Result<an::MultiscaleDecomposition> fromPlanar =
        an::decomposeImageMultiscale(engine, execution, an::SampleType::Float32, planarSamples.data(), planar, starlet);
    const an::Result<an::MultiscaleDecomposition> fromPacked =
        an::decomposeImageMultiscale(engine, execution, an::SampleType::Float32, packedSamples.data(), packed, starlet);
    ASSERT_TRUE(fromPlanar.hasValue()) << fromPlanar.status().message();
    ASSERT_TRUE(fromPacked.hasValue()) << fromPacked.status().message();
    EXPECT_EQ(fromPlanar->channelCount(), kChannels);

    for (std::uint32_t channel = 0u; channel < kChannels; ++channel)
    {
        for (std::uint32_t layer = 0u; layer < 3u; ++layer)
        {
            const std::optional<an::PlaneView<const float>> left = fromPlanar->layer(channel, layer);
            const std::optional<an::PlaneView<const float>> right = fromPacked->layer(channel, layer);
            ASSERT_TRUE(left.has_value());
            ASSERT_TRUE(right.has_value());
            for (std::uint32_t y = 0u; y < kHeight; ++y)
            {
                const std::span<const float> first = *left->row(y);
                const std::span<const float> second = *right->row(y);
                for (std::uint32_t x = 0u; x < kWidth; ++x)
                {
                    ASSERT_FLOAT_EQ(first[x], second[x]) << channel << "/" << layer << " at " << x << "," << y;
                }
            }
        }
    }
}

TEST(MultiscaleTransformTest, AnIntegerImageRoundTripsThroughItsOwnCodeValues)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 19u;
    constexpr std::uint32_t kHeight = 15u;
    const an::ImageLayout layout = planarLayout(an::SampleType::UInt16, kWidth, kHeight);
    std::vector<std::uint16_t> source(static_cast<std::size_t>(kWidth) * kHeight, 0u);
    for (std::size_t index = 0u; index < source.size(); ++index)
    {
        source[index] = static_cast<std::uint16_t>(1000u + (index * 137u) % 40000u);
    }
    std::vector<std::uint16_t> rebuilt(source.size(), 0u);

    const an::Result<an::MultiscaleDecomposition> decomposed = an::decomposeImageMultiscale(
        engine, execution, an::SampleType::UInt16, source.data(), layout, plan(an::MultiscaleAlgorithm::Starlet, 4u));
    ASSERT_TRUE(decomposed.hasValue()) << decomposed.status().message();
    const an::Result<an::SpatialFilterResult> summed =
        an::reconstructImageMultiscale(execution, *decomposed, an::SampleType::UInt16, rebuilt.data(), layout);
    ASSERT_TRUE(summed.hasValue()) << summed.status().message();
    EXPECT_FALSE(summed->wroteInPlace);

    // The layers hold the image's own code values rather than a normalized copy of them, so the sum
    // lands back on the stored numbers rather than near them.
    for (std::size_t index = 0u; index < source.size(); ++index)
    {
        ASSERT_EQ(rebuilt[index], source[index]) << "at " << index;
    }
}

TEST(MultiscaleTransformTest, ARejectedSampleIsSubstitutedOnceAndCounted)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 12u;
    constexpr std::uint32_t kHeight = 9u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    std::vector<float> source(static_cast<std::size_t>(kWidth) * kHeight, 0.5f);
    source[40u] = std::numeric_limits<float>::quiet_NaN();
    source[41u] = std::numeric_limits<float>::infinity();
    std::vector<float> rebuilt(source.size(), -1.0f);

    an::SpatialSelection selection{};
    selection.validity = an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly}).value();

    const an::Result<an::MultiscaleDecomposition> decomposed =
        an::decomposeImageMultiscale(engine, execution, an::SampleType::Float32, source.data(), layout,
                                     plan(an::MultiscaleAlgorithm::Starlet, 3u), selection);
    ASSERT_TRUE(decomposed.hasValue()) << decomposed.status().message();
    EXPECT_EQ(decomposed->result().substitutedSamples, 2u);
    EXPECT_EQ(decomposed->result().run.selection.valid, static_cast<std::uint64_t>(kWidth) * kHeight - 2u);

    // Nothing meaningless survives into a layer, which is what makes the sum a number.
    for (std::uint32_t layer = 0u; layer < decomposed->layerCount(); ++layer)
    {
        const std::optional<an::PlaneView<const float>> plane = decomposed->layer(0u, layer);
        ASSERT_TRUE(plane.has_value());
        for (std::uint32_t y = 0u; y < kHeight; ++y)
        {
            const std::span<const float> row = *plane->row(y);
            for (const float coefficient : row)
            {
                ASSERT_TRUE(std::isfinite(coefficient));
            }
        }
    }

    ASSERT_TRUE(an::reconstructImageMultiscale(execution, *decomposed, an::SampleType::Float32, rebuilt.data(), layout)
                    .hasValue());
    // The two rejected samples come back as the substitute rather than as what they were, which is
    // the decomposition saying what it did rather than pretending the image was intact.
    EXPECT_NEAR(rebuilt[40u], 0.0f, 1.0e-6f);
    EXPECT_NEAR(rebuilt[41u], 0.0f, 1.0e-6f);
    EXPECT_NEAR(rebuilt[0u], 0.5f, 1.0e-6f);
}

TEST(MultiscaleTransformTest, ANoiseEstimateIsTheDeviationOfTheLayerAroundItsOwnMedian)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 8u;
    constexpr std::uint32_t kHeight = 8u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    const std::vector<float> source(static_cast<std::size_t>(kWidth) * kHeight, 0.5f);

    an::Result<an::MultiscaleDecomposition> decomposed = an::decomposeImageMultiscale(
        engine, execution, an::SampleType::Float32, source.data(), layout, plan(an::MultiscaleAlgorithm::Starlet, 3u));
    ASSERT_TRUE(decomposed.hasValue()) << decomposed.status().message();

    // A flat image has a residual of the constant and no deviation at all.
    const an::Result<an::MultiscaleLayerNoise> residual =
        an::estimateMultiscaleLayerNoise(execution, *decomposed, 0u, 2u);
    ASSERT_TRUE(residual.hasValue()) << residual.status().message();
    EXPECT_NEAR(residual->median, 0.5, 1.0e-6);
    EXPECT_NEAR(residual->medianAbsoluteDeviation, 0.0, 1.0e-6);
    EXPECT_NEAR(residual->sigma, 0.0, 1.0e-6);
    EXPECT_EQ(residual->sampleCount, static_cast<std::uint64_t>(kWidth) * kHeight);

    // A layer whose coefficients are known answers with the deviation they have.
    const std::optional<an::PlaneView<float>> layer = decomposed->layer(0u, 0u);
    ASSERT_TRUE(layer.has_value());
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        const std::span<float> row = *layer->row(y);
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            row[x] = ((x + y) % 2u == 0u) ? -2.0f : 2.0f;
        }
    }
    const an::Result<an::MultiscaleLayerNoise> measured =
        an::estimateMultiscaleLayerNoise(execution, *decomposed, 0u, 0u);
    ASSERT_TRUE(measured.hasValue()) << measured.status().message();
    EXPECT_NEAR(measured->median, 0.0, 1.0e-6);
    EXPECT_NEAR(measured->medianAbsoluteDeviation, 2.0, 1.0e-6);
    EXPECT_NEAR(measured->sigma, 2.0 * an::kMedianAbsoluteDeviationToSigma, 1.0e-6);

    EXPECT_EQ(an::estimateMultiscaleLayerNoise(execution, *decomposed, 0u, 9u).status().code(),
              an::ErrorCode::OutOfRange);
}

TEST(MultiscaleTransformTest, ShrinkageRemovesTheCoefficientsAThresholdCallsNoise)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 4u;
    constexpr std::uint32_t kHeight = 4u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    const std::vector<float> source(static_cast<std::size_t>(kWidth) * kHeight, 0.25f);

    for (const an::MultiscaleShrinkage rule : {an::MultiscaleShrinkage::Soft, an::MultiscaleShrinkage::Hard})
    {
        an::Result<an::MultiscaleDecomposition> decomposed =
            an::decomposeImageMultiscale(engine, execution, an::SampleType::Float32, source.data(), layout,
                                         plan(an::MultiscaleAlgorithm::Starlet, 3u));
        ASSERT_TRUE(decomposed.hasValue()) << decomposed.status().message();
        const std::optional<an::PlaneView<float>> layer = decomposed->layer(0u, 0u);
        ASSERT_TRUE(layer.has_value());
        const std::array<float, 4u> pattern{0.05f, -0.05f, 4.0f, -4.0f};
        for (std::uint32_t y = 0u; y < kHeight; ++y)
        {
            const std::span<float> row = *layer->row(y);
            for (std::uint32_t x = 0u; x < kWidth; ++x)
            {
                row[x] = pattern[x];
            }
        }

        const an::Result<an::MultiscaleShrinkResult> shrunk = an::shrinkMultiscaleLayer(
            execution, *decomposed, 0u, 0u, {.shrinkage = rule, .threshold = 1.0, .amount = 1.0});
        ASSERT_TRUE(shrunk.hasValue()) << shrunk.status().message();
        EXPECT_EQ(shrunk->samplesConsidered, static_cast<std::uint64_t>(kWidth) * kHeight);
        EXPECT_EQ(shrunk->samplesChanged, rule == an::MultiscaleShrinkage::Soft ? 16u : 8u);

        const std::span<const float> row = *decomposed->layer(0u, 0u)->row(0u);
        EXPECT_FLOAT_EQ(row[0u], 0.0f);
        EXPECT_FLOAT_EQ(row[1u], 0.0f);
        EXPECT_FLOAT_EQ(row[2u], rule == an::MultiscaleShrinkage::Soft ? 3.0f : 4.0f);
        EXPECT_FLOAT_EQ(row[3u], rule == an::MultiscaleShrinkage::Soft ? -3.0f : -4.0f);
    }
}

TEST(MultiscaleTransformTest, AHalfAppliedShrinkageMovesACoefficientHalfWay)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, 4u, 4u);
    const std::vector<float> source(16u, 0.25f);
    an::Result<an::MultiscaleDecomposition> decomposed = an::decomposeImageMultiscale(
        engine, execution, an::SampleType::Float32, source.data(), layout, plan(an::MultiscaleAlgorithm::Starlet, 3u));
    ASSERT_TRUE(decomposed.hasValue()) << decomposed.status().message();
    const std::optional<an::PlaneView<float>> layer = decomposed->layer(0u, 0u);
    ASSERT_TRUE(layer.has_value());
    for (std::uint32_t y = 0u; y < 4u; ++y)
    {
        const std::span<float> row = *layer->row(y);
        for (float& coefficient : row)
        {
            coefficient = 0.4f;
        }
    }
    ASSERT_TRUE(an::shrinkMultiscaleLayer(execution, *decomposed, 0u, 0u,
                                          {.shrinkage = an::MultiscaleShrinkage::Hard, .threshold = 1.0, .amount = 0.5})
                    .hasValue());
    EXPECT_FLOAT_EQ((*decomposed->layer(0u, 0u)->row(0u))[0u], 0.2f);

    EXPECT_EQ(an::shrinkMultiscaleLayer(execution, *decomposed, 0u, 0u, {.threshold = -1.0}).status().code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(
        an::shrinkMultiscaleLayer(execution, *decomposed, 0u, 0u, {.threshold = 1.0, .amount = 2.0}).status().code(),
        an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::shrinkMultiscaleLayer(execution, *decomposed, 0u, 5u, {.threshold = 1.0}).status().code(),
              an::ErrorCode::OutOfRange);
}

TEST(MultiscaleTransformTest, EveryPlanningRefusalIsNamed)
{
    an::MultiscaleTransformRequest request{};
    request.algorithm = static_cast<an::MultiscaleAlgorithm>(9u);
    EXPECT_EQ(an::planMultiscaleTransform(request).status().code(), an::ErrorCode::InvalidArgument);

    request = {};
    request.scaling = static_cast<an::MultiscaleScalingFunction>(90u);
    EXPECT_EQ(an::planMultiscaleTransform(request).status().code(), an::ErrorCode::InvalidArgument);

    request = {};
    request.spacing = an::MultiscaleLayerSpacing::Linear;
    request.linearScale = 0u;
    EXPECT_EQ(an::planMultiscaleTransform(request).status().code(), an::ErrorCode::InvalidArgument);

    request = {};
    request.layerCount = 1u;
    EXPECT_EQ(an::planMultiscaleTransform(request).status().code(), an::ErrorCode::OutOfRange);
    request.layerCount = an::kMaxMultiscaleLayers + 1u;
    EXPECT_EQ(an::planMultiscaleTransform(request).status().code(), an::ErrorCode::OutOfRange);

    request = {};
    request.borders.border = an::KernelBorderMode::NotApplicable;
    EXPECT_EQ(an::planMultiscaleTransform(request).status().code(), an::ErrorCode::InvalidArgument);

    request = {};
    request.output.writeBack.domain = an::DomainMapping::FullScale;
    EXPECT_EQ(an::planMultiscaleTransform(request).status().code(), an::ErrorCode::InvalidArgument);

    // The deepest layer of each transform is checked against the kernel contract rather than
    // silently narrowed when the kernel stops fitting.
    request = {};
    request.spacing = an::MultiscaleLayerSpacing::Linear;
    request.linearScale = 200u;
    request.layerCount = 6u;
    const an::Result<an::MultiscaleTransformPlan> spread = an::planMultiscaleTransform(request);
    EXPECT_EQ(spread.status().code(), an::ErrorCode::OutOfRange);

    request = {};
    request.spacing = an::MultiscaleLayerSpacing::Linear;
    request.linearScale = (std::numeric_limits<std::uint32_t>::max)();
    request.layerCount = 3u;
    const an::Result<an::MultiscaleTransformPlan> overflow = an::planMultiscaleTransform(request);
    EXPECT_EQ(overflow.status().code(), an::ErrorCode::OutOfRange);
    const an::MultiscaleTransformPlan unchecked{request};
    EXPECT_EQ(unchecked.layerScale(1u), 0u);

    request = {};
    request.algorithm = an::MultiscaleAlgorithm::MultiscaleLinear;
    request.layerCount = an::kMaxMultiscaleLayers;
    const an::Result<an::MultiscaleTransformPlan> wide = an::planMultiscaleTransform(request);
    EXPECT_EQ(wide.status().code(), an::ErrorCode::OutOfRange);
}

TEST(MultiscaleTransformTest, EveryExecutionRefusalIsNamed)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, 8u, 8u);
    const std::vector<float> source(64u, 0.5f);
    std::vector<float> destination(64u, 0.0f);
    const an::MultiscaleTransformPlan starlet = plan(an::MultiscaleAlgorithm::Starlet, 3u);

    EXPECT_EQ(an::decomposeImageMultiscale(engine, an::ExecutionContext{}, an::SampleType::Float32, source.data(),
                                           layout, starlet)
                  .status()
                  .code(),
              an::ErrorCode::NotInitialized);
    EXPECT_EQ(an::decomposeImageMultiscale(engine, execution, an::SampleType::Float32, nullptr, layout, starlet)
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::decomposeImageMultiscale(engine, execution, an::SampleType::Float32, source.data(), layout,
                                           an::MultiscaleTransformPlan{})
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);

    // An exact promise this family cannot keep in Float32 is refused before a sample is read.
    an::MultiscaleTransformRequest exact{};
    exact.output.contract = an::OutputPrecisionContract::ExactStoredValues;
    const an::Result<an::MultiscaleTransformPlan> exactPlan = an::planMultiscaleTransform(exact);
    ASSERT_TRUE(exactPlan.hasValue()) << exactPlan.status().message();
    const an::ImageLayout wide = planarLayout(an::SampleType::UInt32, 8u, 8u);
    const std::vector<std::uint32_t> wideSamples(64u, 7u);
    EXPECT_EQ(
        an::decomposeImageMultiscale(engine, execution, an::SampleType::UInt32, wideSamples.data(), wide, *exactPlan)
            .status()
            .code(),
        an::ErrorCode::IncompatibleArguments);

    // The layers belong to one context, and an operation leased from another cannot own them.
    const an::EngineContext other = makeEngine();
    const an::ExecutionContext borrowed = makeExecution(other);
    EXPECT_EQ(an::decomposeImageMultiscale(engine, borrowed, an::SampleType::Float32, source.data(), layout, starlet)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);

    const an::Result<an::MultiscaleDecomposition> decomposed =
        an::decomposeImageMultiscale(engine, execution, an::SampleType::Float32, source.data(), layout, starlet);
    ASSERT_TRUE(decomposed.hasValue()) << decomposed.status().message();

    EXPECT_EQ(an::reconstructImageMultiscale(execution, an::MultiscaleDecomposition{}, an::SampleType::Float32,
                                             destination.data(), layout)
                  .status()
                  .code(),
              an::ErrorCode::NotInitialized);
    const an::ImageLayout otherShape = planarLayout(an::SampleType::Float32, 4u, 4u);
    std::vector<float> small(16u, 0.0f);
    EXPECT_EQ(an::reconstructImageMultiscale(execution, *decomposed, an::SampleType::Float32, small.data(), otherShape)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);

    std::array<an::MultiscaleLayerWeight, 2u> tooFew{};
    an::MultiscaleReconstructionRequest request{};
    request.layers = tooFew;
    EXPECT_EQ(an::reconstructImageMultiscale(execution, *decomposed, an::SampleType::Float32, destination.data(),
                                             layout, request)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);

    std::array<an::MultiscaleLayerWeight, 3u> broken{};
    broken[0] = {.enabled = true, .gain = std::numeric_limits<double>::quiet_NaN()};
    request.layers = broken;
    EXPECT_EQ(an::reconstructImageMultiscale(execution, *decomposed, an::SampleType::Float32, destination.data(),
                                             layout, request)
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
}

TEST(MultiscaleTransformTest, TheResidualIsTheLayerADecompositionWouldHaveHeld)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 53u;
    constexpr std::uint32_t kHeight = 41u;
    constexpr std::uint32_t kChannels = 3u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight, kChannels);
    std::vector<float> source(static_cast<std::size_t>(kWidth) * kHeight * kChannels, 0.0f);
    for (std::uint32_t channel = 0u; channel < kChannels; ++channel)
    {
        const std::vector<float> plane = randomPlane(kWidth, kHeight, 4242u + channel);
        std::copy(plane.begin(), plane.end(), source.begin() + static_cast<std::ptrdiff_t>(channel) * kWidth * kHeight);
    }
    std::vector<float> residual(source.size(), -1.0f);

    const an::MultiscaleTransformPlan starlet = plan(an::MultiscaleAlgorithm::Starlet, 5u);
    const an::Result<an::SpatialFilterResult> smoothed = an::smoothImageMultiscaleResidual(
        execution, an::SampleType::Float32, source.data(), layout, residual.data(), layout, starlet);
    ASSERT_TRUE(smoothed.hasValue()) << smoothed.status().message();
    EXPECT_EQ(smoothed->channelCount, kChannels);
    EXPECT_EQ(smoothed->imagePasses, 8u);
    EXPECT_TRUE(smoothed->wroteInPlace);
    EXPECT_FALSE(smoothed->readInPlace);
    EXPECT_EQ(smoothed->run.outputsWritten, static_cast<std::uint64_t>(kWidth) * kHeight * kChannels);
    // The cascade reaches as far as the sum of every layer it applies, not as far as its deepest one.
    EXPECT_EQ(smoothed->horizontalRadius, 2u + 4u + 8u + 16u);

    const an::Result<an::MultiscaleDecomposition> decomposed =
        an::decomposeImageMultiscale(engine, execution, an::SampleType::Float32, source.data(), layout, starlet);
    ASSERT_TRUE(decomposed.hasValue()) << decomposed.status().message();

    // Bit-identical rather than close: the two routes run the same smoothings in the same order, and
    // a caller comparing them is entitled to the same answer.
    for (std::uint32_t channel = 0u; channel < kChannels; ++channel)
    {
        const std::optional<an::PlaneView<const float>> layer = decomposed->layer(channel, starlet.residualLayer());
        ASSERT_TRUE(layer.has_value());
        for (std::uint32_t y = 0u; y < kHeight; ++y)
        {
            const std::span<const float> row = *layer->row(y);
            for (std::uint32_t x = 0u; x < kWidth; ++x)
            {
                const std::size_t index = (static_cast<std::size_t>(channel) * kHeight + y) * kWidth + x;
                ASSERT_EQ(residual[index], row[x]) << "at channel " << channel << " (" << x << ", " << y << ")";
            }
        }
    }
}

TEST(MultiscaleTransformTest, TheResidualOfAMultiscaleLinearTransformIsItsDeepestSmoothingAlone)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 37u;
    constexpr std::uint32_t kHeight = 29u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    const std::vector<float> source = randomPlane(kWidth, kHeight, 8642u);
    std::vector<float> residual(source.size(), -1.0f);

    const an::MultiscaleTransformPlan linear = plan(an::MultiscaleAlgorithm::MultiscaleLinear, 4u);
    const an::Result<an::SpatialFilterResult> smoothed = an::smoothImageMultiscaleResidual(
        execution, an::SampleType::Float32, source.data(), layout, residual.data(), layout, linear);
    ASSERT_TRUE(smoothed.hasValue()) << smoothed.status().message();
    // One smoothing rather than three: this transform smooths the source itself for every layer, so
    // its residual is the deepest one and the cascade collapses.
    EXPECT_EQ(smoothed->imagePasses, 2u);
    EXPECT_EQ(smoothed->horizontalRadius, linear.layerRadius(linear.detailLayerCount() - 1u));

    const an::Result<an::MultiscaleDecomposition> decomposed =
        an::decomposeImageMultiscale(engine, execution, an::SampleType::Float32, source.data(), layout, linear);
    ASSERT_TRUE(decomposed.hasValue()) << decomposed.status().message();
    const std::optional<an::PlaneView<const float>> layer = decomposed->layer(0u, linear.residualLayer());
    ASSERT_TRUE(layer.has_value());
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        const std::span<const float> row = *layer->row(y);
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            ASSERT_EQ(residual[static_cast<std::size_t>(y) * kWidth + x], row[x]) << "at (" << x << ", " << y << ")";
        }
    }
}

TEST(MultiscaleTransformTest, EveryLayerIsTheDifferenceOfTwoSuccessiveSmoothingsExactly)
{
    // Bit for bit rather than close: a decomposition runs the transform's own separable passes in the
    // transform's own order, so where it keeps the cascade between them changes nothing it computes.
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 67u;
    constexpr std::uint32_t kHeight = 43u;
    constexpr std::uint32_t kChannels = 2u;
    const std::vector<float> planar = randomPlane(kWidth, kHeight * kChannels, 97531u);
    const std::vector<float> packed = interleave(planar, kChannels, kWidth, kHeight);
    const std::uint64_t planeBytes = static_cast<std::uint64_t>(kWidth) * kHeight * sizeof(float);

    for (const an::MultiscaleTransformPlan& transform :
         {plan(an::MultiscaleAlgorithm::Starlet, 5u), plan(an::MultiscaleAlgorithm::MultiscaleLinear, 4u)})
    {
        const bool starlet = transform.request().algorithm == an::MultiscaleAlgorithm::Starlet;
        for (const bool interleaved : {false, true})
        {
            const an::Result<an::MultiscaleDecomposition> decomposed = an::decomposeImageMultiscale(
                engine, execution, an::SampleType::Float32, interleaved ? packed.data() : planar.data(),
                interleaved ? interleavedLayout(an::SampleType::Float32, kWidth, kHeight, kChannels)
                            : planarLayout(an::SampleType::Float32, kWidth, kHeight, kChannels),
                transform);
            ASSERT_TRUE(decomposed.hasValue()) << decomposed.status().message();

            std::uint64_t largestScratch = 0u;
            for (std::uint32_t channel = 0u; channel < kChannels; ++channel)
            {
                const Cascade cascade =
                    runCascade(execution, transform, channelPlane(planar, channel, kWidth, kHeight), kWidth, kHeight);
                largestScratch = (std::max)(largestScratch, cascade.largestScratch);
                for (std::uint32_t layer = 0u; layer < transform.layerCount(); ++layer)
                {
                    const bool residual = layer == transform.residualLayer();
                    const std::optional<an::PlaneView<const float>> held = decomposed->layer(channel, layer);
                    ASSERT_TRUE(held.has_value());
                    for (std::uint32_t y = 0u; y < kHeight; ++y)
                    {
                        const std::span<const float> row = *held->row(y);
                        for (std::uint32_t x = 0u; x < kWidth; ++x)
                        {
                            const std::size_t index = static_cast<std::size_t>(y) * kWidth + x;
                            const float expected =
                                residual ? cascade.smoothings[layer][index]
                                         : cascade.smoothings[layer][index] - cascade.smoothings[layer + 1u][index];
                            ASSERT_EQ(std::bit_cast<std::uint32_t>(row[x]), std::bit_cast<std::uint32_t>(expected))
                                << (starlet ? "starlet" : "linear") << (interleaved ? " interleaved" : " planar")
                                << ", channel " << channel << ", layer " << layer << " at (" << x << ", " << y << ")";
                        }
                    }
                }
            }

            // Two planes alternate between the smoothings. The starlet is gathered into its first
            // layer, and the multiscale-linear transform, which smooths the gathered plane for every
            // layer, holds that one as well.
            const std::uint64_t planes = starlet ? 2u : 3u;
            const std::uint64_t working = decomposed->result().workingBytes;
            EXPECT_GE(working, largestScratch + planes * planeBytes);
            EXPECT_LT(working, largestScratch + (planes + 1u) * planeBytes);
            EXPECT_EQ(decomposed->result().imagePasses, transform.imagePasses());
        }
    }
}

TEST(MultiscaleTransformTest, TheResidualIsTheDeepestSmoothingWhateverTheCascadeLength)
{
    // The cascade alternates between the answer and one working plane, so which plane the source is
    // gathered into depends on whether it runs an odd or an even number of passes. Both, into a
    // caller's plane and into one scattered afterwards, have to land on the cascade's last smoothing.
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 29u;
    constexpr std::uint32_t kHeight = 23u;
    constexpr std::uint32_t kChannels = 2u;
    const std::vector<float> planar = randomPlane(kWidth, kHeight * kChannels, 24680u);
    const std::vector<float> packed = interleave(planar, kChannels, kWidth, kHeight);
    const std::uint64_t planeBytes = static_cast<std::uint64_t>(kWidth) * kHeight * sizeof(float);

    for (std::uint32_t layers = an::kMinMultiscaleLayers; layers <= 6u; ++layers)
    {
        const an::MultiscaleTransformPlan starlet = plan(an::MultiscaleAlgorithm::Starlet, layers);
        for (const bool interleaved : {false, true})
        {
            const an::ImageLayout layout = interleaved
                                               ? interleavedLayout(an::SampleType::Float32, kWidth, kHeight, kChannels)
                                               : planarLayout(an::SampleType::Float32, kWidth, kHeight, kChannels);
            std::vector<float> answer(planar.size(), -1.0f);
            const an::Result<an::SpatialFilterResult> smoothed = an::smoothImageMultiscaleResidual(
                execution, an::SampleType::Float32, interleaved ? packed.data() : planar.data(), layout, answer.data(),
                layout, starlet);
            ASSERT_TRUE(smoothed.hasValue()) << smoothed.status().message();
            EXPECT_EQ(smoothed->wroteInPlace, !interleaved);

            std::uint64_t largestScratch = 0u;
            for (std::uint32_t channel = 0u; channel < kChannels; ++channel)
            {
                const Cascade cascade =
                    runCascade(execution, starlet, channelPlane(planar, channel, kWidth, kHeight), kWidth, kHeight);
                largestScratch = (std::max)(largestScratch, cascade.largestScratch);
                for (std::size_t index = 0u; index < cascade.smoothings.back().size(); ++index)
                {
                    const std::size_t at = interleaved ? index * kChannels + channel
                                                       : static_cast<std::size_t>(kWidth) * kHeight * channel + index;
                    ASSERT_EQ(std::bit_cast<std::uint32_t>(answer[at]),
                              std::bit_cast<std::uint32_t>(cascade.smoothings.back()[index]))
                        << layers << " layers" << (interleaved ? ", interleaved" : ", planar") << ", channel "
                        << channel << " at " << index;
                }
            }

            // One working plane beside the answer: a plane of the caller's when the channel is one, and
            // a second one to scatter from when it is not.
            const std::uint64_t planes = interleaved ? 2u : 1u;
            EXPECT_GE(smoothed->workingBytes, largestScratch + planes * planeBytes);
            EXPECT_LT(smoothed->workingBytes, largestScratch + (planes + 1u) * planeBytes);
        }
    }
}

TEST(MultiscaleTransformTest, AReconstructionSumsEverySampleFromPositiveZeroInLayerOrder)
{
    // The oracle is the sum the contract states: Float64, from positive zero, over the enabled layers
    // in their order, then the clamp, then one narrowing. A sum started from its first term instead
    // would turn a sum of negative zeros negative, and one reordered would round differently.
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    // Wide enough to cross any chunk the arithmetic is carried out in, and not a multiple of one.
    constexpr std::uint32_t kWidth = 1031u;
    constexpr std::uint32_t kHeight = 5u;
    constexpr std::uint32_t kLayers = 4u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    const std::vector<float> source(static_cast<std::size_t>(kWidth) * kHeight, 0.0f);
    an::Result<an::MultiscaleDecomposition> decomposed =
        an::decomposeImageMultiscale(engine, execution, an::SampleType::Float32, source.data(), layout,
                                     plan(an::MultiscaleAlgorithm::Starlet, kLayers));
    ASSERT_TRUE(decomposed.hasValue()) << decomposed.status().message();

    // Coefficients spread over many binades so a reordered sum rounds differently, with the first row
    // of every layer zero and every fourth sample negative zero, and one infinite coefficient in a layer
    // that two of the weightings leave out, which a layer weighted zero must not turn into a NaN.
    std::mt19937_64 generator(0x5EEDu);
    std::uniform_real_distribution<double> mantissa(-1.0, 1.0);
    std::uniform_int_distribution<int> binade(-30, 30);
    for (std::uint32_t layer = 0u; layer < kLayers; ++layer)
    {
        const std::optional<an::PlaneView<float>> coefficients = decomposed->layer(0u, layer);
        ASSERT_TRUE(coefficients.has_value());
        for (std::uint32_t y = 0u; y < kHeight; ++y)
        {
            const std::span<float> row = *coefficients->row(y);
            for (std::uint32_t x = 0u; x < kWidth; ++x)
            {
                row[x] = y == 0u ? (x % 4u == 0u ? -0.0f : 0.0f)
                                 : static_cast<float>(std::ldexp(mantissa(generator), binade(generator)));
            }
            if (layer == 1u && y == 1u)
            {
                row[5u] = std::numeric_limits<float>::infinity();
            }
        }
    }

    const std::array<std::array<an::MultiscaleLayerWeight, kLayers>, 3u> weightings{{
        {{{.enabled = true, .gain = 1.0},
          {.enabled = true, .gain = 1.0},
          {.enabled = true, .gain = 1.0},
          {.enabled = true, .gain = 1.0}}},
        {{{.enabled = true, .gain = -0.75},
          {.enabled = false, .gain = 3.0},
          {.enabled = true, .gain = 1.5},
          {.enabled = true, .gain = -1.0}}},
        {{{.enabled = true, .gain = -1.0},
          {.enabled = true, .gain = 0.0},
          {.enabled = true, .gain = -2.0},
          {.enabled = false, .gain = 1.0}}},
    }};
    for (std::size_t weighting = 0u; weighting < weightings.size(); ++weighting)
    {
        for (const bool clamped : {false, true})
        {
            an::MultiscaleReconstructionRequest request{};
            request.layers = weightings[weighting];
            if (clamped)
            {
                request.clamp = an::KernelValueWindow{.lowest = -0.25, .highest = 0.5};
            }
            std::vector<float> destination(source.size(), 7.0f);
            ASSERT_TRUE(an::reconstructImageMultiscale(execution, *decomposed, an::SampleType::Float32,
                                                       destination.data(), layout, request)
                            .hasValue());
            for (std::uint32_t y = 0u; y < kHeight; ++y)
            {
                for (std::uint32_t x = 0u; x < kWidth; ++x)
                {
                    double sum = 0.0;
                    for (std::uint32_t layer = 0u; layer < kLayers; ++layer)
                    {
                        const an::MultiscaleLayerWeight& weight = weightings[weighting][layer];
                        if (weight.enabled && weight.gain != 0.0)
                        {
                            sum += weight.gain * static_cast<double>((*decomposed->layer(0u, layer)->row(y))[x]);
                        }
                    }
                    if (clamped)
                    {
                        sum = std::clamp(sum, -0.25, 0.5);
                    }
                    const float expected = static_cast<float>(sum);
                    const float produced = destination[static_cast<std::size_t>(y) * kWidth + x];
                    ASSERT_EQ(std::bit_cast<std::uint32_t>(produced), std::bit_cast<std::uint32_t>(expected))
                        << "weighting " << weighting << (clamped ? ", clamped" : "") << " at (" << x << ", " << y
                        << "): " << produced << " against " << expected;
                }
            }
        }
    }
}

TEST(MultiscaleTransformTest, AResidualRefusesTheArgumentsADecompositionRefuses)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 16u;
    constexpr std::uint32_t kHeight = 12u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    std::vector<float> samples(static_cast<std::size_t>(kWidth) * kHeight, 0.25f);
    std::vector<float> destination(samples.size(), 0.0f);
    const an::MultiscaleTransformPlan starlet = plan(an::MultiscaleAlgorithm::Starlet, 3u);

    EXPECT_EQ(an::smoothImageMultiscaleResidual(execution, an::SampleType::Float32, nullptr, layout, destination.data(),
                                                layout, starlet)
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::smoothImageMultiscaleResidual(execution, an::SampleType::Float32, samples.data(), layout,
                                                samples.data(), layout, starlet)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(an::smoothImageMultiscaleResidual(execution, an::SampleType::Float32, samples.data(), layout,
                                                destination.data(), layout, an::MultiscaleTransformPlan{})
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
}

TEST(MultiscaleTransformTest, TheDeclaredEffectsSayWhatEachHalfOfTheRoundTripDoes)
{
    EXPECT_EQ(an::kMultiscaleDecompositionEffect.samples, an::SampleEffect::Reads);
    EXPECT_EQ(an::checkOperationEffect(an::kMultiscaleDecompositionEffect), an::OperationEffectError::None);
    EXPECT_EQ(an::kMultiscaleReconstructionEffect.samples, an::SampleEffect::Writes);
    EXPECT_EQ(an::kMultiscaleReconstructionEffect.description, an::DescriptionEffect::Preserves);
    EXPECT_EQ(an::kMultiscaleReconstructionEffect.semantics, an::SemanticEffect::Preserves);
    EXPECT_EQ(an::checkOperationEffect(an::kMultiscaleReconstructionEffect), an::OperationEffectError::None);
}

} // namespace
