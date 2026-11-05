// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for convolving an image, as one dense pass or as two separable ones.

#include <AfterNightEngine/Algorithms/Spatial/Convolution.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>

#include "SpatialFilterFixture.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace
{

using spatial_test::interleavedLayout;
using spatial_test::makeEngine;
using spatial_test::makeExecution;
using spatial_test::planarLayout;
using spatial_test::randomPlane;

/// The five-tap binomial blur, which is separable and sums to one.
constexpr std::array<double, 5> kBinomial5{1.0 / 16.0, 4.0 / 16.0, 6.0 / 16.0, 4.0 / 16.0, 1.0 / 16.0};

/// The three-tap binomial blur.
constexpr std::array<double, 3> kBinomial3{0.25, 0.5, 0.25};

/// The outer product of two tap arrays, row-major.
///
/// @param vertical the taps applied down a column.
/// @param horizontal the taps applied along a row.
/// @return the dense kernel the two passes are equivalent to.
[[nodiscard]] std::vector<double> outerProduct(std::span<const double> vertical, std::span<const double> horizontal)
{
    std::vector<double> dense(vertical.size() * horizontal.size(), 0.0);
    for (std::size_t row = 0u; row < vertical.size(); ++row)
    {
        for (std::size_t column = 0u; column < horizontal.size(); ++column)
        {
            dense[row * horizontal.size() + column] = vertical[row] * horizontal[column];
        }
    }
    return dense;
}

TEST(ConvolutionTest, ATapArraySumsInItsOwnOrder)
{
    EXPECT_DOUBLE_EQ(an::convolutionTapSum(kBinomial5), 1.0);
    EXPECT_DOUBLE_EQ(an::convolutionTapSum(std::span<const double>{}), 0.0);

    constexpr std::array<double, 3> gradient{-1.0, 0.0, 1.0};
    EXPECT_DOUBLE_EQ(an::convolutionTapSum(gradient), 0.0);
}

TEST(ConvolutionTest, AnOuterProductFactorsAndAHigherRankKernelDoesNot)
{
    const std::vector<double> dense = outerProduct(kBinomial3, kBinomial5);
    std::array<double, 5> horizontal{};
    std::array<double, 3> vertical{};
    const an::Result<an::ConvolutionSeparation> separated =
        an::separateConvolutionKernel(dense, 5u, 3u, 1e-12, horizontal, vertical);
    ASSERT_TRUE(separated.hasValue()) << separated.status().message();
    EXPECT_TRUE(separated.value().separable);
    EXPECT_EQ(separated.value().horizontalTapCount, 5u);
    EXPECT_EQ(separated.value().verticalTapCount, 3u);
    EXPECT_LT(separated.value().residual, 1e-15);
    EXPECT_NEAR(separated.value().coefficientSum, 1.0, 1e-12);

    // The recovered passes are the originals up to the scale the pivot fixes, so their product is
    // what matters rather than either array on its own.
    for (std::size_t row = 0u; row < 3u; ++row)
    {
        for (std::size_t column = 0u; column < 5u; ++column)
        {
            EXPECT_NEAR(vertical[row] * horizontal[column], dense[row * 5u + column], 1e-15);
        }
    }

    std::vector<double> corner = dense;
    corner[0] += 0.5;
    const an::Result<an::ConvolutionSeparation> refused =
        an::separateConvolutionKernel(corner, 5u, 3u, 1e-9, horizontal, vertical);
    ASSERT_TRUE(refused.hasValue()) << refused.status().message();
    EXPECT_FALSE(refused.value().separable);
    EXPECT_GT(refused.value().residual, 1e-9);
}

TEST(ConvolutionTest, AZeroKernelSeparatesRatherThanDividingByItsPivot)
{
    const std::vector<double> zeros(9u, 0.0);
    std::array<double, 3> horizontal{1.0, 1.0, 1.0};
    std::array<double, 3> vertical{1.0, 1.0, 1.0};
    const an::Result<an::ConvolutionSeparation> separated =
        an::separateConvolutionKernel(zeros, 3u, 3u, 0.0, horizontal, vertical);
    ASSERT_TRUE(separated.hasValue()) << separated.status().message();
    EXPECT_TRUE(separated.value().separable);
    EXPECT_DOUBLE_EQ(separated.value().residual, 0.0);
    EXPECT_DOUBLE_EQ(horizontal[1], 0.0);
    EXPECT_DOUBLE_EQ(vertical[1], 0.0);
}

TEST(ConvolutionTest, AKernelThatIsNotAKernelIsRefusedByName)
{
    std::array<double, 4> even{};
    std::array<double, 3> odd{};
    const std::vector<double> coefficients(12u, 0.0);
    EXPECT_EQ(an::separateConvolutionKernel(coefficients, 4u, 3u, 0.0, even, odd).status().code(),
              an::ErrorCode::InvalidArgument);

    std::array<double, 3> horizontal{};
    std::array<double, 3> vertical{};
    const std::vector<double> tooFew(8u, 0.0);
    EXPECT_EQ(an::separateConvolutionKernel(tooFew, 3u, 3u, 0.0, horizontal, vertical).status().code(),
              an::ErrorCode::IncompatibleArguments);

    std::vector<double> nine(9u, 0.0);
    nine[4] = std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(an::separateConvolutionKernel(nine, 3u, 3u, 0.0, horizontal, vertical).status().code(),
              an::ErrorCode::InvalidArgument);

    const std::vector<double> finite(9u, 1.0);
    EXPECT_EQ(an::separateConvolutionKernel(finite, 3u, 3u, -1.0, horizontal, vertical).status().code(),
              an::ErrorCode::InvalidArgument);

    std::array<double, 2> shortArray{};
    EXPECT_EQ(an::separateConvolutionKernel(finite, 3u, 3u, 0.0, shortArray, vertical).status().code(),
              an::ErrorCode::IncompatibleArguments);
}

TEST(ConvolutionTest, AnIdentityKernelReproducesEverySample)
{
    constexpr std::uint32_t kWidth = 13u;
    constexpr std::uint32_t kHeight = 7u;
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<float> source = randomPlane(kWidth, kHeight, 11u);
    std::vector<float> destination(source.size(), -1.0f);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);

    constexpr std::array<double, 1> identity{1.0};
    const an::SeparableConvolutionRequest request{.horizontalTaps = identity, .verticalTaps = identity};
    const an::Result<an::SpatialFilterResult> filtered = an::convolveImageSeparable(
        execution, an::SampleType::Float32, source.data(), layout, destination.data(), layout, request);
    ASSERT_TRUE(filtered.hasValue()) << filtered.status().message();
    EXPECT_EQ(filtered.value().channelCount, 1u);
    EXPECT_EQ(filtered.value().horizontalRadius, 0u);
    EXPECT_EQ(filtered.value().imagePasses, 2u);
    EXPECT_TRUE(filtered.value().readInPlace);
    EXPECT_TRUE(filtered.value().wroteInPlace);
    // Nothing was gathered, but the separable path still holds one intermediate band per tile.
    EXPECT_GT(filtered.value().workingBytes, 0u);
    EXPECT_EQ(filtered.value().run.outputsWritten, source.size());
    for (std::size_t index = 0u; index < source.size(); ++index)
    {
        EXPECT_FLOAT_EQ(destination[index], source[index]) << "at " << index;
    }
}

TEST(ConvolutionTest, TheSeparableAndDensePathsProduceTheSamePicture)
{
    constexpr std::uint32_t kWidth = 21u;
    constexpr std::uint32_t kHeight = 17u;
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<float> source = randomPlane(kWidth, kHeight, 23u);
    std::vector<float> viaPasses(source.size(), 0.0f);
    std::vector<float> viaDense(source.size(), 0.0f);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);

    const an::StencilBorderPolicy borders{.border = an::KernelBorderMode::ReflectExcludingEdge};
    const an::SeparableConvolutionRequest separable{
        .horizontalTaps = kBinomial5, .verticalTaps = kBinomial3, .borders = borders};
    ASSERT_TRUE(an::convolveImageSeparable(execution, an::SampleType::Float32, source.data(), layout, viaPasses.data(),
                                           layout, separable)
                    .hasValue());

    const std::vector<double> dense = outerProduct(kBinomial3, kBinomial5);
    const an::DenseConvolutionRequest denseRequest{
        .coefficients = dense, .width = 5u, .height = 3u, .borders = borders};
    const an::Result<an::SpatialFilterResult> ran = an::convolveImageDense(
        execution, an::SampleType::Float32, source.data(), layout, viaDense.data(), layout, denseRequest);
    ASSERT_TRUE(ran.hasValue()) << ran.status().message();
    EXPECT_EQ(ran.value().imagePasses, 1u);
    // The dense path needs no intermediate at all, and both sides were reached where they lie.
    EXPECT_TRUE(ran.value().readInPlace);
    EXPECT_TRUE(ran.value().wroteInPlace);
    EXPECT_EQ(ran.value().workingBytes, 0u);

    for (std::size_t index = 0u; index < source.size(); ++index)
    {
        EXPECT_NEAR(viaPasses[index], viaDense[index], 1e-6f) << "at " << index;
    }
}

TEST(ConvolutionTest, TheReferenceBackendAndTheCompiledOneAgree)
{
    constexpr std::uint32_t kWidth = 37u;
    constexpr std::uint32_t kHeight = 29u;
    const an::EngineContext engine = makeEngine();
    const std::vector<float> source = randomPlane(kWidth, kHeight, 29u);
    std::vector<float> compiled(source.size(), 0.0f);
    std::vector<float> scalar(source.size(), 0.0f);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    const an::SeparableConvolutionRequest request{.horizontalTaps = kBinomial5, .verticalTaps = kBinomial5};

    const an::ExecutionContext automatic = makeExecution(engine, 3u, an::ComputeBackend::Automatic);
    ASSERT_TRUE(an::convolveImageSeparable(automatic, an::SampleType::Float32, source.data(), layout, compiled.data(),
                                           layout, request)
                    .hasValue());
    const an::ExecutionContext reference = makeExecution(engine, 1u, an::ComputeBackend::Reference);
    ASSERT_TRUE(an::convolveImageSeparable(reference, an::SampleType::Float32, source.data(), layout, scalar.data(),
                                           layout, request)
                    .hasValue());

    for (std::size_t index = 0u; index < source.size(); ++index)
    {
        EXPECT_FLOAT_EQ(compiled[index], scalar[index]) << "at " << index;
    }
}

TEST(ConvolutionTest, AnInterleavedImageIsFilteredLikeAPlanarOne)
{
    constexpr std::uint32_t kWidth = 11u;
    constexpr std::uint32_t kHeight = 9u;
    constexpr std::uint32_t kChannels = 3u;
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<float> planar = randomPlane(kWidth, kHeight * kChannels, 31u);

    std::vector<float> interleaved(planar.size(), 0.0f);
    for (std::uint32_t channel = 0u; channel < kChannels; ++channel)
    {
        for (std::uint32_t index = 0u; index < kWidth * kHeight; ++index)
        {
            interleaved[static_cast<std::size_t>(index) * kChannels + channel] =
                planar[static_cast<std::size_t>(channel) * kWidth * kHeight + index];
        }
    }

    const an::ImageLayout planarShape = planarLayout(an::SampleType::Float32, kWidth, kHeight, kChannels);
    const an::ImageLayout interleavedShape = interleavedLayout(an::SampleType::Float32, kWidth, kHeight, kChannels);
    std::vector<float> planarOut(planar.size(), 0.0f);
    std::vector<float> interleavedOut(planar.size(), 0.0f);
    const an::SeparableConvolutionRequest request{.horizontalTaps = kBinomial3, .verticalTaps = kBinomial3};

    ASSERT_TRUE(an::convolveImageSeparable(execution, an::SampleType::Float32, planar.data(), planarShape,
                                           planarOut.data(), planarShape, request)
                    .hasValue());
    const an::Result<an::SpatialFilterResult> gathered =
        an::convolveImageSeparable(execution, an::SampleType::Float32, interleaved.data(), interleavedShape,
                                   interleavedOut.data(), interleavedShape, request);
    ASSERT_TRUE(gathered.hasValue()) << gathered.status().message();
    EXPECT_FALSE(gathered.value().readInPlace);
    EXPECT_FALSE(gathered.value().wroteInPlace);
    EXPECT_GT(gathered.value().workingBytes, 0u);

    for (std::uint32_t channel = 0u; channel < kChannels; ++channel)
    {
        for (std::uint32_t index = 0u; index < kWidth * kHeight; ++index)
        {
            EXPECT_FLOAT_EQ(interleavedOut[static_cast<std::size_t>(index) * kChannels + channel],
                            planarOut[static_cast<std::size_t>(channel) * kWidth * kHeight + index])
                << "channel " << channel << " index " << index;
        }
    }
}

TEST(ConvolutionTest, TheWorkingNumbersAreTheStoredNumbers)
{
    // A sixteen-bit image filtered by a kernel that sums to one comes back at the same code values,
    // not at a rescaled unit interval. A flat surface is the sharpest form of that question.
    constexpr std::uint32_t kWidth = 9u;
    constexpr std::uint32_t kHeight = 5u;
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<std::uint16_t> source(static_cast<std::size_t>(kWidth) * kHeight, 4321u);
    std::vector<std::uint16_t> destination(source.size(), 0u);
    const an::ImageLayout layout = planarLayout(an::SampleType::UInt16, kWidth, kHeight);
    const an::SeparableConvolutionRequest request{.horizontalTaps = kBinomial5,
                                                  .verticalTaps = kBinomial5,
                                                  .borders = {.border = an::KernelBorderMode::ReflectExcludingEdge}};

    const an::Result<an::SpatialFilterResult> filtered = an::convolveImageSeparable(
        execution, an::SampleType::UInt16, source.data(), layout, destination.data(), layout, request);
    ASSERT_TRUE(filtered.hasValue()) << filtered.status().message();
    for (const std::uint16_t sample : destination)
    {
        EXPECT_EQ(sample, 4321u);
    }
}

TEST(ConvolutionTest, AConstantBorderContributesWhatItSays)
{
    // One row of ones with a constant border of zero: the leftmost output is the taps that fall
    // inside, and nothing else.
    constexpr std::uint32_t kWidth = 5u;
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<float> source(kWidth, 1.0f);
    std::vector<float> destination(kWidth, 0.0f);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, 1u);

    constexpr std::array<double, 1> identity{1.0};
    const an::SeparableConvolutionRequest request{
        .horizontalTaps = kBinomial3,
        .verticalTaps = identity,
        .borders = {.border = an::KernelBorderMode::Constant, .constantBorder = 0.0f}};
    ASSERT_TRUE(an::convolveImageSeparable(execution, an::SampleType::Float32, source.data(), layout,
                                           destination.data(), layout, request)
                    .hasValue());
    EXPECT_FLOAT_EQ(destination[0], 0.75f);
    EXPECT_FLOAT_EQ(destination[2], 1.0f);
    EXPECT_FLOAT_EQ(destination[4], 0.75f);
}

TEST(ConvolutionTest, AnInvalidCentreLeavesItsDestinationSampleUntouched)
{
    constexpr std::uint32_t kWidth = 5u;
    constexpr std::uint32_t kHeight = 3u;
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    std::vector<float> source(static_cast<std::size_t>(kWidth) * kHeight, 1.0f);
    source[7] = std::numeric_limits<float>::quiet_NaN();
    std::vector<float> destination(source.size(), -7.0f);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);

    const an::SeparableConvolutionRequest request{
        .horizontalTaps = kBinomial3,
        .verticalTaps = kBinomial3,
        .borders = {.border = an::KernelBorderMode::ReflectExcludingEdge, .invalidNeighbor = 1.0f}};
    an::SpatialSelection selection{};
    const std::optional<an::ValidityDescriptor> finiteOnly =
        an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly});
    ASSERT_TRUE(finiteOnly.has_value());
    selection.validity = *finiteOnly;

    const an::Result<an::SpatialFilterResult> filtered = an::convolveImageSeparable(
        execution, an::SampleType::Float32, source.data(), layout, destination.data(), layout, request, selection);
    ASSERT_TRUE(filtered.hasValue()) << filtered.status().message();
    EXPECT_EQ(filtered.value().run.outputsWritten, source.size() - 1u);
    EXPECT_FLOAT_EQ(destination[7], -7.0f);
    // Its neighbours were filtered with the substitute in its place, so they are still exactly one.
    EXPECT_FLOAT_EQ(destination[6], 1.0f);
    EXPECT_FLOAT_EQ(destination[8], 1.0f);
}

TEST(ConvolutionTest, TheRefusalsAFilterOwesItsCaller)
{
    constexpr std::uint32_t kWidth = 7u;
    constexpr std::uint32_t kHeight = 5u;
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    std::vector<float> samples(static_cast<std::size_t>(kWidth) * kHeight, 0.0f);
    std::vector<float> other(samples.size(), 0.0f);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    const an::SeparableConvolutionRequest plain{.horizontalTaps = kBinomial3, .verticalTaps = kBinomial3};

    EXPECT_EQ(an::convolveImageSeparable(execution, an::SampleType::Float32, samples.data(), layout, samples.data(),
                                         layout, plain)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);

    an::SeparableConvolutionRequest rescaling = plain;
    rescaling.output.writeBack.domain = an::DomainMapping::FullScale;
    EXPECT_EQ(an::convolveImageSeparable(execution, an::SampleType::Float32, samples.data(), layout, other.data(),
                                         layout, rescaling)
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);

    std::vector<std::uint32_t> wide(samples.size(), 0u);
    std::vector<std::uint32_t> wideOut(samples.size(), 0u);
    const an::ImageLayout wideLayout = planarLayout(an::SampleType::UInt32, kWidth, kHeight);
    an::SeparableConvolutionRequest exact = plain;
    exact.output.contract = an::OutputPrecisionContract::ExactStoredValues;
    EXPECT_EQ(an::convolveImageSeparable(execution, an::SampleType::UInt32, wide.data(), wideLayout, wideOut.data(),
                                         wideLayout, exact)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);

    constexpr std::array<double, 2> evenTaps{0.5, 0.5};
    an::SeparableConvolutionRequest even = plain;
    even.horizontalTaps = evenTaps;
    EXPECT_EQ(an::convolveImageSeparable(execution, an::SampleType::Float32, samples.data(), layout, other.data(),
                                         layout, even)
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);

    const an::ExecutionContext nothing{};
    EXPECT_EQ(an::convolveImageSeparable(nothing, an::SampleType::Float32, samples.data(), layout, other.data(), layout,
                                         plain)
                  .status()
                  .code(),
              an::ErrorCode::NotInitialized);
}

TEST(ConvolutionTest, TheDeclaredEffectIsWhatAFilterDoes)
{
    EXPECT_EQ(an::kSpatialFilterEffect.samples, an::SampleEffect::Writes);
    EXPECT_EQ(an::kSpatialFilterEffect.description, an::DescriptionEffect::Preserves);
    EXPECT_EQ(an::kSpatialFilterEffect.semantics, an::SemanticEffect::Preserves);
    EXPECT_EQ(an::kSpatialFilterEffect.solution, an::SolutionEffect::Preserves);
    EXPECT_EQ(an::kSpatialFilterEffect.validity.policy, an::ValidityPolicy::Preserve);
    EXPECT_EQ(an::checkOperationEffect(an::kSpatialFilterEffect), an::OperationEffectError::None);
}

TEST(ConvolutionTest, ThePrecisionRuleIsTheFamilyRule)
{
    EXPECT_EQ(an::spatialPrecisionError(an::SampleType::UInt16, an::OutputPrecisionContract::NormalizedOrLossy),
              an::PrecisionError::None);
    EXPECT_EQ(an::spatialPrecisionError(an::SampleType::Float64, an::OutputPrecisionContract::NormalizedOrLossy),
              an::PrecisionError::None);
    EXPECT_EQ(an::spatialPrecisionError(an::SampleType::Float64, an::OutputPrecisionContract::ExactStoredValues),
              an::PrecisionError::PrecisionBelowContract);
    EXPECT_EQ(an::spatialPrecisionError(an::SampleType::Int32, an::OutputPrecisionContract::ExactStoredValues),
              an::PrecisionError::PrecisionBelowContract);
    EXPECT_EQ(an::spatialPrecisionError(an::SampleType::UInt8, an::OutputPrecisionContract::ExactStoredValues),
              an::PrecisionError::None);
}

} // namespace
