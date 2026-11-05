// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the noise the engine removes itself.
//
// One theme runs through the suite and it is the defect the domain was written around: a threshold
// is a number in the image's own units, so the same request over the same picture means the same
// thing whatever the picture is stored as. A bilateral filter that converts every input to floating
// point through a condition that is true of every value silently changes its colour sigma's meaning
// with the storage; the first case here guards against it.

#include <AfterNightEngine/Algorithms/NoiseReduction/NoiseReduction.h>
#include <AfterNightEngine/Algorithms/Spatial/Gaussian.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>

#include "SpatialFilterFixture.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>
#include <vector>

namespace
{

using spatial_test::interleavedLayout;
using spatial_test::makeEngine;
using spatial_test::makeExecution;
using spatial_test::planarLayout;
using spatial_test::randomPlane;

/// A three-channel planar image whose pixels all hold one triple.
///
/// @param width how many pixels one row holds.
/// @param height how many rows.
/// @param red the first channel's value.
/// @param green the second channel's value.
/// @param blue the third channel's value.
/// @return the samples, channel-major.
[[nodiscard]] std::vector<float> constantRgb(std::uint32_t width, std::uint32_t height, float red, float green,
                                             float blue)
{
    const std::size_t plane = static_cast<std::size_t>(width) * height;
    std::vector<float> samples(plane * 3u, 0.0f);
    std::fill(samples.begin(), samples.begin() + plane, red);
    std::fill(samples.begin() + plane, samples.begin() + 2u * plane, green);
    std::fill(samples.begin() + 2u * plane, samples.end(), blue);
    return samples;
}

TEST(NoiseReductionTest, ABilateralRangeSigmaMeansTheSameThingWhateverTheStorage)
{
    // A test of `type != float || type != uint8`, which no value can fail, would convert every image
    // to floating point and then apply the caller's colour sigma to samples that have been rescaled.
    // Here the two representations hold the same code values and the same range sigma is stated in
    // those code values, so the two pictures must come out equal.
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 24u;
    constexpr std::uint32_t kHeight = 20u;
    const std::size_t count = static_cast<std::size_t>(kWidth) * kHeight;

    std::vector<float> wide(count, 0.0f);
    std::vector<std::uint16_t> narrow(count, 0u);
    for (std::size_t index = 0u; index < count; ++index)
    {
        const auto code = static_cast<std::uint16_t>(1000u + (index * 971u) % 30000u);
        narrow[index] = code;
        wide[index] = static_cast<float>(code);
    }

    an::BilateralRequest request{};
    request.spatialSigma = 2.0;
    request.rangeSigma = 2000.0;
    const an::Result<an::BilateralPlan> planned = an::planBilateralFilter(request);
    ASSERT_TRUE(planned.hasValue()) << planned.status().message();
    EXPECT_EQ(planned->radius(), 3u);
    EXPECT_EQ(planned->imagePasses(), 1u);
    EXPECT_GT(planned->windowSamples(), 0u);
    EXPECT_LT(planned->windowSamples(), 49u);

    const an::ImageLayout wideLayout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    const an::ImageLayout narrowLayout = planarLayout(an::SampleType::UInt16, kWidth, kHeight);
    std::vector<float> wideOut(count, 0.0f);
    std::vector<std::uint16_t> narrowOut(count, 0u);
    ASSERT_TRUE(an::filterImageBilateral(execution, an::SampleType::Float32, wide.data(), wideLayout, wideOut.data(),
                                         wideLayout, *planned)
                    .hasValue());
    ASSERT_TRUE(an::filterImageBilateral(execution, an::SampleType::UInt16, narrow.data(), narrowLayout,
                                         narrowOut.data(), narrowLayout, *planned)
                    .hasValue());

    for (std::size_t index = 0u; index < count; ++index)
    {
        const auto expected = static_cast<std::uint16_t>(std::lround(wideOut[index]));
        ASSERT_NEAR(static_cast<double>(narrowOut[index]), static_cast<double>(expected), 1.0) << "at " << index;
    }
}

TEST(NoiseReductionTest, ABilateralFilterSmoothsBesideAnEdgeAndNotAcrossIt)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 32u;
    constexpr std::uint32_t kHeight = 16u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    std::vector<float> source(static_cast<std::size_t>(kWidth) * kHeight, 0.0f);
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            source[static_cast<std::size_t>(y) * kWidth + x] = x < kWidth / 2u ? 0.2f : 0.8f;
        }
    }
    std::vector<float> bilateral(source.size(), 0.0f);
    std::vector<float> gaussian(source.size(), 0.0f);

    const an::Result<an::BilateralPlan> planned = an::planBilateralFilter({.spatialSigma = 2.0, .rangeSigma = 0.05});
    ASSERT_TRUE(planned.hasValue()) << planned.status().message();
    ASSERT_TRUE(an::filterImageBilateral(execution, an::SampleType::Float32, source.data(), layout, bilateral.data(),
                                         layout, *planned)
                    .hasValue());
    const an::Result<an::GaussianPlan> blur = an::planGaussianBlur({.sigmaX = 2.0, .sigmaY = 2.0});
    ASSERT_TRUE(blur.hasValue()) << blur.status().message();
    ASSERT_TRUE(
        an::blurImageGaussian(execution, an::SampleType::Float32, source.data(), layout, gaussian.data(), layout, *blur)
            .hasValue());

    // Right against the edge the ordinary blur has moved the sample and the bilateral filter has
    // not, which is the whole difference between the two.
    const std::size_t justLeft = static_cast<std::size_t>(kHeight / 2u) * kWidth + kWidth / 2u - 1u;
    EXPECT_NEAR(bilateral[justLeft], 0.2f, 1.0e-4f);
    EXPECT_GT(gaussian[justLeft], 0.25f);
    const std::size_t justRight = justLeft + 1u;
    EXPECT_NEAR(bilateral[justRight], 0.8f, 1.0e-4f);
    EXPECT_LT(gaussian[justRight], 0.75f);
}

TEST(NoiseReductionTest, ABilateralFilterLeavesAFlatSurfaceExactlyWhereItWas)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, 12u, 10u);
    const std::vector<float> source(120u, 0.625f);
    std::vector<float> destination(120u, -1.0f);

    const an::Result<an::BilateralPlan> planned = an::planBilateralFilter({.spatialSigma = 3.0, .rangeSigma = 0.1});
    ASSERT_TRUE(planned.hasValue()) << planned.status().message();
    const an::Result<an::NoiseReductionResult> ran = an::filterImageBilateral(
        execution, an::SampleType::Float32, source.data(), layout, destination.data(), layout, *planned);
    ASSERT_TRUE(ran.hasValue()) << ran.status().message();
    EXPECT_EQ(ran->filter.imagePasses, 1u);
    EXPECT_EQ(ran->filter.run.outputsWritten, 120u);
    EXPECT_EQ(ran->samplesChanged, 0u);
    for (const float value : destination)
    {
        ASSERT_NEAR(value, 0.625f, 1.0e-6f);
    }
}

TEST(NoiseReductionTest, EveryBilateralPlanningRefusalIsNamed)
{
    EXPECT_EQ(an::planBilateralFilter({.spatialSigma = 0.0}).status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::planBilateralFilter({.rangeSigma = 0.0}).status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::planBilateralFilter({.spatialSigma = std::numeric_limits<double>::quiet_NaN()}).status().code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::planBilateralFilter({.radius = an::kMaxBilateralRadius + 1u}).status().code(),
              an::ErrorCode::OutOfRange);
    EXPECT_EQ(an::planBilateralFilter({.spatialSigma = 1000.0}).status().code(), an::ErrorCode::OutOfRange);
    EXPECT_EQ(an::planBilateralFilter({.spatialSigma = (std::numeric_limits<double>::max)()}).status().code(),
              an::ErrorCode::OutOfRange);
    EXPECT_EQ(an::planBilateralFilter({.borders = {.border = an::KernelBorderMode::NotApplicable}}).status().code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(
        an::planBilateralFilter({.output = {.writeBack = {.domain = an::DomainMapping::FullScale}}}).status().code(),
        an::ErrorCode::InvalidArgument);

    // A stated radius is honoured rather than re-derived.
    const an::Result<an::BilateralPlan> stated = an::planBilateralFilter({.spatialSigma = 2.0, .radius = 5u});
    ASSERT_TRUE(stated.hasValue()) << stated.status().message();
    EXPECT_EQ(stated->radius(), 5u);
}

TEST(NoiseReductionTest, ImagePassesCountAlgorithmsRatherThanChannels)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, 5u, 5u, 2u);
    const std::vector<float> source(50u, 0.2f);
    std::vector<float> destination(source.size(), 0.0f);
    const an::Result<an::BilateralPlan> bilateralPlan = an::planBilateralFilter({});
    ASSERT_TRUE(bilateralPlan.hasValue()) << bilateralPlan.status().message();

    const an::Result<an::NoiseReductionResult> bilateral = an::filterImageBilateral(
        execution, an::SampleType::Float32, source.data(), layout, destination.data(), layout, *bilateralPlan);
    ASSERT_TRUE(bilateral.hasValue()) << bilateral.status().message();
    EXPECT_EQ(bilateral->filter.imagePasses, 1u);

    const an::Result<an::NoiseReductionResult> banding = an::suppressImageBanding(
        execution, an::SampleType::Float32, source.data(), layout, destination.data(), layout, {});
    ASSERT_TRUE(banding.hasValue()) << banding.status().message();
    EXPECT_EQ(banding->filter.imagePasses, 2u);

    const an::Result<an::NoiseReductionResult> impulse = an::rejectImageImpulseNoise(
        execution, an::SampleType::Float32, source.data(), layout, destination.data(), layout, {});
    ASSERT_TRUE(impulse.hasValue()) << impulse.status().message();
    EXPECT_EQ(impulse->filter.imagePasses, 2u);
}

TEST(NoiseReductionTest, BilateralCountsOnlyChangesThatSurviveIntegerWriteBack)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 7u;
    constexpr std::uint32_t kHeight = 7u;
    const an::ImageLayout layout = planarLayout(an::SampleType::UInt16, kWidth, kHeight);
    std::vector<std::uint16_t> source(static_cast<std::size_t>(kWidth) * kHeight, 100u);
    source[source.size() / 2u] = 101u;
    std::vector<std::uint16_t> destination(source.size(), 0u);

    const an::Result<an::BilateralPlan> planned =
        an::planBilateralFilter({.spatialSigma = 1.0, .rangeSigma = 100.0, .radius = 1u});
    ASSERT_TRUE(planned.hasValue()) << planned.status().message();
    const an::Result<an::NoiseReductionResult> ran = an::filterImageBilateral(
        execution, an::SampleType::UInt16, source.data(), layout, destination.data(), layout, *planned);
    ASSERT_TRUE(ran.hasValue()) << ran.status().message();

    std::uint64_t changed = 0u;
    for (std::size_t index = 0u; index < source.size(); ++index)
    {
        changed += destination[index] != source[index] ? 1u : 0u;
    }
    EXPECT_EQ(ran->samplesChanged, changed);
    EXPECT_EQ(changed, 1u);
}

TEST(NoiseReductionTest, AnExplicitMaskMustCoverTheWholeSpatialImage)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 4u;
    constexpr std::uint32_t kHeight = 3u;
    constexpr std::uint32_t kChannels = 3u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight, kChannels);
    const std::vector<float> source(static_cast<std::size_t>(kWidth) * kHeight * kChannels, 0.5f);
    std::vector<float> destination(source.size(), -1.0f);
    const an::Result<an::BilateralPlan> planned = an::planBilateralFilter({});
    ASSERT_TRUE(planned.hasValue()) << planned.status().message();

    const auto runWithMask = [&](const an::ImageExtent& maskExtent) {
        const std::uint64_t byteCount =
            an::packedMaskRowBytes(maskExtent.width()) * maskExtent.height() * maskExtent.channelCount();
        std::vector<std::uint8_t> bits(static_cast<std::size_t>(byteCount), 0xFFu);
        const std::optional<an::ValidityMaskView<std::uint8_t>> writable =
            an::ValidityMaskView<std::uint8_t>::createTight(bits, maskExtent);
        EXPECT_TRUE(writable.has_value());
        const an::ValidityMaskView<const std::uint8_t> mask = writable->constView();
        an::SpatialSelection selection{};
        selection.validity = *an::ValidityDescriptor::create({.rule = an::ValidityRule::ExplicitMask});
        selection.mask = &mask;
        return an::filterImageBilateral(execution, an::SampleType::Float32, source.data(), layout, destination.data(),
                                        layout, *planned, selection)
            .status()
            .code();
    };

    const an::ImageExtent wrongWidth = *an::ImageExtent::create(kWidth - 1u, kHeight, 1u, engine.limits());
    EXPECT_EQ(runWithMask(wrongWidth), an::ErrorCode::IncompatibleArguments);
    const an::ImageExtent wrongPlaneCount = *an::ImageExtent::create(kWidth, kHeight, 2u, engine.limits());
    EXPECT_EQ(runWithMask(wrongPlaneCount), an::ErrorCode::IncompatibleArguments);
}

// Under the all-valid rule the operation resolves its border once, into a copy of the plane that
// carries the border rule already applied, and every output then reads a window that is inside
// that copy. Whatever the copy holds at the border must be what the rule says, sample for sample,
// under each of the five rules. An explicit mask that accepts every sample takes the general path,
// which resolves each neighbour by name per tap, and under the reference backend both paths sum the
// same taps in the same order through the same exponential, so the two answers are the same bits.
// The copy is working memory, and the operation reports it.
TEST(NoiseReductionTest, TheAllValidBorderIsTheBorderRuleBitForBit)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine, 3u, an::ComputeBackend::Reference);
    constexpr std::uint32_t kWidth = 13u;
    constexpr std::uint32_t kHeight = 9u;
    constexpr std::uint32_t kRadius = 3u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight, 1u);
    const std::vector<float> source = randomPlane(kWidth, kHeight, 0x5EEDu);

    const std::uint64_t maskBytes = an::packedMaskRowBytes(kWidth) * kHeight;
    std::vector<std::uint8_t> bits(static_cast<std::size_t>(maskBytes), 0xFFu);
    const an::ImageExtent extent = *an::ImageExtent::create(kWidth, kHeight, 1u, engine.limits());
    const std::optional<an::ValidityMaskView<std::uint8_t>> writable =
        an::ValidityMaskView<std::uint8_t>::createTight(bits, extent);
    ASSERT_TRUE(writable.has_value());
    const an::ValidityMaskView<const std::uint8_t> mask = writable->constView();
    an::SpatialSelection everyMaskBitSet{};
    everyMaskBitSet.validity = *an::ValidityDescriptor::create({.rule = an::ValidityRule::ExplicitMask});
    everyMaskBitSet.mask = &mask;

    constexpr std::array<an::KernelBorderMode, 5u> kBorders{
        an::KernelBorderMode::Clamp, an::KernelBorderMode::Constant, an::KernelBorderMode::Reflect,
        an::KernelBorderMode::ReflectExcludingEdge, an::KernelBorderMode::Wrap};
    for (const an::KernelBorderMode border : kBorders)
    {
        const an::Result<an::BilateralPlan> planned = an::planBilateralFilter(
            {.spatialSigma = 1.5,
             .rangeSigma = 0.2,
             .radius = kRadius,
             .borders = {.border = border, .constantBorder = 0.125f, .invalidNeighbor = -0.5f}});
        ASSERT_TRUE(planned.hasValue()) << planned.status().message();

        std::vector<float> allValid(source.size(), -1.0f);
        const an::Result<an::NoiseReductionResult> bordered = an::filterImageBilateral(
            execution, an::SampleType::Float32, source.data(), layout, allValid.data(), layout, *planned);
        ASSERT_TRUE(bordered.hasValue()) << bordered.status().message();

        std::vector<float> masked(source.size(), -1.0f);
        const an::Result<an::NoiseReductionResult> general =
            an::filterImageBilateral(execution, an::SampleType::Float32, source.data(), layout, masked.data(), layout,
                                     *planned, everyMaskBitSet);
        ASSERT_TRUE(general.hasValue()) << general.status().message();

        EXPECT_EQ(std::memcmp(allValid.data(), masked.data(), allValid.size() * sizeof(float)), 0)
            << "border mode " << static_cast<int>(border);
        const std::uint64_t borderedBytes =
            static_cast<std::uint64_t>(kWidth + 2u * kRadius) * (kHeight + 2u * kRadius) * sizeof(float);
        EXPECT_GE(bordered->filter.workingBytes, borderedBytes) << "border mode " << static_cast<int>(border);
    }
}

TEST(NoiseReductionTest, EachChromaticNoiseMethodHoldsGreenToItsOwnCeiling)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 4u;
    constexpr std::uint32_t kHeight = 4u;
    constexpr std::size_t kPlane = 16u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight, 3u);
    const std::vector<float> source = constantRgb(kWidth, kHeight, 0.2f, 0.9f, 0.4f);

    const std::array<std::pair<an::ChromaticNoiseMethod, float>, 4u> expected{
        {{an::ChromaticNoiseMethod::AverageNeutral, 0.3f},
         {an::ChromaticNoiseMethod::MaximumNeutral, 0.4f},
         {an::ChromaticNoiseMethod::MaximumMask, 0.63f},
         {an::ChromaticNoiseMethod::AdditiveMask, 0.72f}}};
    for (const auto& [method, green] : expected)
    {
        std::vector<float> destination(source.size(), -1.0f);
        const an::Result<an::ChromaticNoisePlan> planned =
            an::planChromaticNoiseReduction({.method = method, .amount = 0.5});
        ASSERT_TRUE(planned.hasValue()) << planned.status().message();
        const an::Result<an::NoiseReductionResult> ran = an::reduceImageChromaticNoise(
            execution, an::SampleType::Float32, source.data(), layout, destination.data(), layout, *planned);
        ASSERT_TRUE(ran.hasValue()) << ran.status().message();
        EXPECT_EQ(ran->filter.channelCount, 3u);
        EXPECT_EQ(ran->samplesChanged, kPlane) << an::chromaticNoiseMethodName(method);
        for (std::size_t index = 0u; index < kPlane; ++index)
        {
            ASSERT_NEAR(destination[index], 0.2f, 1.0e-6f) << an::chromaticNoiseMethodName(method);
            ASSERT_NEAR(destination[kPlane + index], green, 1.0e-6f) << an::chromaticNoiseMethodName(method);
            ASSERT_NEAR(destination[2u * kPlane + index], 0.4f, 1.0e-6f) << an::chromaticNoiseMethodName(method);
        }
    }
}

TEST(NoiseReductionTest, AChromaticNoiseWhiteLevelIsStatedRatherThanInferredFromTheStorage)
{
    // The same picture in code values, with the white level stated to match, produces the same
    // relative answer - which is what a per-type normalize-and-denormalize helper cannot promise.
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 3u;
    constexpr std::uint32_t kHeight = 3u;
    constexpr std::size_t kPlane = 9u;
    const an::ImageLayout layout = planarLayout(an::SampleType::UInt16, kWidth, kHeight, 3u);
    std::vector<std::uint16_t> source(kPlane * 3u, 0u);
    std::fill(source.begin(), source.begin() + kPlane, static_cast<std::uint16_t>(200u));
    std::fill(source.begin() + kPlane, source.begin() + 2u * kPlane, static_cast<std::uint16_t>(900u));
    std::fill(source.begin() + 2u * kPlane, source.end(), static_cast<std::uint16_t>(400u));
    std::vector<std::uint16_t> destination(source.size(), 0u);

    const an::Result<an::ChromaticNoisePlan> planned =
        an::planChromaticNoiseReduction({.method = an::ChromaticNoiseMethod::AverageNeutral, .whiteLevel = 1000.0});
    ASSERT_TRUE(planned.hasValue()) << planned.status().message();
    ASSERT_TRUE(an::reduceImageChromaticNoise(execution, an::SampleType::UInt16, source.data(), layout,
                                              destination.data(), layout, *planned)
                    .hasValue());
    for (std::size_t index = 0u; index < kPlane; ++index)
    {
        ASSERT_EQ(destination[index], 200u);
        ASSERT_EQ(destination[kPlane + index], 300u);
        ASSERT_EQ(destination[2u * kPlane + index], 400u);
    }
}

TEST(NoiseReductionTest, PreservingLightnessKeepsThePixelAsBrightAsItWas)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 2u;
    constexpr std::uint32_t kHeight = 2u;
    constexpr std::size_t kPlane = 4u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight, 3u);
    const std::vector<float> source = constantRgb(kWidth, kHeight, 0.2f, 0.9f, 0.4f);
    std::vector<float> plain(source.size(), 0.0f);
    std::vector<float> preserved(source.size(), 0.0f);

    an::ChromaticNoiseRequest request{};
    request.method = an::ChromaticNoiseMethod::AverageNeutral;
    const an::Result<an::ChromaticNoisePlan> plainPlan = an::planChromaticNoiseReduction(request);
    ASSERT_TRUE(plainPlan.hasValue()) << plainPlan.status().message();
    request.preserveLightness = true;
    const an::Result<an::ChromaticNoisePlan> preservedPlan = an::planChromaticNoiseReduction(request);
    ASSERT_TRUE(preservedPlan.hasValue()) << preservedPlan.status().message();
    EXPECT_FALSE(plainPlan->preservesLightness());
    EXPECT_TRUE(preservedPlan->preservesLightness());

    ASSERT_TRUE(an::reduceImageChromaticNoise(execution, an::SampleType::Float32, source.data(), layout, plain.data(),
                                              layout, *plainPlan)
                    .hasValue());
    ASSERT_TRUE(an::reduceImageChromaticNoise(execution, an::SampleType::Float32, source.data(), layout,
                                              preserved.data(), layout, *preservedPlan)
                    .hasValue());

    const auto lightness = [&](const std::vector<float>& samples) {
        const std::array<double, 3u> rgb{samples[0], samples[kPlane], samples[2u * kPlane]};
        std::array<double, 3u> lab{};
        EXPECT_TRUE(an::convertColorPixel(preservedPlan->toLab(), rgb, lab).ok());
        return lab[0];
    };
    EXPECT_NEAR(lightness(preserved), lightness(source), 1.0e-3);
    // And the plain path does darken the pixel, so the case is measuring something.
    EXPECT_LT(lightness(plain), lightness(source) - 1.0);
}

TEST(NoiseReductionTest, PreservingLightnessUsesTheStatedWhiteLevelAsTheRgbUnit)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 2u;
    constexpr std::uint32_t kHeight = 2u;
    constexpr std::size_t kPlane = 4u;
    const an::ImageLayout normalizedLayout = planarLayout(an::SampleType::Float32, kWidth, kHeight, 3u);
    const an::ImageLayout codeLayout = planarLayout(an::SampleType::UInt16, kWidth, kHeight, 3u);
    const std::vector<float> normalizedSource = constantRgb(kWidth, kHeight, 0.2f, 0.9f, 0.4f);
    std::vector<std::uint16_t> codeSource(kPlane * 3u, 0u);
    std::fill(codeSource.begin(), codeSource.begin() + kPlane, static_cast<std::uint16_t>(200u));
    std::fill(codeSource.begin() + kPlane, codeSource.begin() + 2u * kPlane, static_cast<std::uint16_t>(900u));
    std::fill(codeSource.begin() + 2u * kPlane, codeSource.end(), static_cast<std::uint16_t>(400u));
    std::vector<float> normalizedDestination(normalizedSource.size(), 0.0f);
    std::vector<std::uint16_t> codeDestination(codeSource.size(), 0u);

    const an::Result<an::ChromaticNoisePlan> normalizedPlan = an::planChromaticNoiseReduction(
        {.method = an::ChromaticNoiseMethod::AverageNeutral, .preserveLightness = true});
    const an::Result<an::ChromaticNoisePlan> codePlan = an::planChromaticNoiseReduction(
        {.method = an::ChromaticNoiseMethod::AverageNeutral, .whiteLevel = 1000.0, .preserveLightness = true});
    ASSERT_TRUE(normalizedPlan.hasValue()) << normalizedPlan.status().message();
    ASSERT_TRUE(codePlan.hasValue()) << codePlan.status().message();
    ASSERT_TRUE(an::reduceImageChromaticNoise(execution, an::SampleType::Float32, normalizedSource.data(),
                                              normalizedLayout, normalizedDestination.data(), normalizedLayout,
                                              *normalizedPlan)
                    .hasValue());
    const an::Result<an::NoiseReductionResult> codeResult =
        an::reduceImageChromaticNoise(execution, an::SampleType::UInt16, codeSource.data(), codeLayout,
                                      codeDestination.data(), codeLayout, *codePlan);
    ASSERT_TRUE(codeResult.hasValue()) << codeResult.status().message();

    std::uint64_t changed = 0u;
    for (std::size_t index = 0u; index < codeSource.size(); ++index)
    {
        EXPECT_NEAR(static_cast<double>(codeDestination[index]) / 1000.0, normalizedDestination[index], 0.002)
            << "at " << index;
        changed += codeDestination[index] != codeSource[index] ? 1u : 0u;
    }
    EXPECT_EQ(codeResult->samplesChanged, changed);
}

TEST(NoiseReductionTest, AChromaticPixelIsRejectedWhenAnyOfItsThreeSamplesIsInvalid)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 2u;
    constexpr std::uint32_t kHeight = 1u;
    constexpr std::size_t kPlane = 2u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight, 3u);
    std::vector<float> source = constantRgb(kWidth, kHeight, 0.2f, 0.9f, 0.4f);
    source[2u * kPlane] = std::numeric_limits<float>::quiet_NaN();
    std::vector<float> destination(source.size(), -7.0f);
    an::SpatialSelection selection{};
    selection.validity = *an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly});
    const an::Result<an::ChromaticNoisePlan> planned = an::planChromaticNoiseReduction({});
    ASSERT_TRUE(planned.hasValue()) << planned.status().message();

    const an::Result<an::NoiseReductionResult> ran = an::reduceImageChromaticNoise(
        execution, an::SampleType::Float32, source.data(), layout, destination.data(), layout, *planned, selection);
    ASSERT_TRUE(ran.hasValue()) << ran.status().message();
    EXPECT_FLOAT_EQ(destination[0u], -7.0f);
    EXPECT_FLOAT_EQ(destination[kPlane], -7.0f);
    EXPECT_FLOAT_EQ(destination[2u * kPlane], -7.0f);
    EXPECT_FLOAT_EQ(destination[1u], 0.2f);
    EXPECT_FLOAT_EQ(destination[kPlane + 1u], 0.3f);
    EXPECT_FLOAT_EQ(destination[2u * kPlane + 1u], 0.4f);
    EXPECT_EQ(ran->filter.run.selection, (an::ValidityCount{.valid = 3u, .total = 6u}));
    EXPECT_EQ(ran->filter.run.outputsWritten, 3u);
    EXPECT_EQ(ran->samplesChanged, 1u);
}

TEST(NoiseReductionTest, APerChannelMaskRejectsTheWholeChromaticPixelBeforeIntegerScatter)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 2u;
    constexpr std::uint32_t kHeight = 2u;
    constexpr std::uint32_t kChannels = 3u;
    const an::ImageLayout layout = interleavedLayout(an::SampleType::UInt16, kWidth, kHeight, kChannels);
    std::vector<std::uint16_t> source(static_cast<std::size_t>(kWidth) * kHeight * kChannels, 0u);
    for (std::size_t pixel = 0u; pixel < source.size() / kChannels; ++pixel)
    {
        source[pixel * kChannels] = 200u;
        source[pixel * kChannels + 1u] = 900u;
        source[pixel * kChannels + 2u] = 400u;
    }
    std::vector<std::uint16_t> destination(source.size(), 7777u);

    const an::ImageExtent maskExtent = *an::ImageExtent::create(kWidth, kHeight, kChannels, engine.limits());
    const std::uint64_t maskBytes = an::packedMaskRowBytes(kWidth) * kHeight * kChannels;
    std::vector<std::uint8_t> bits(static_cast<std::size_t>(maskBytes), 0u);
    const std::optional<an::ValidityMaskView<std::uint8_t>> writable =
        an::ValidityMaskView<std::uint8_t>::createTight(bits, maskExtent);
    ASSERT_TRUE(writable.has_value());
    for (std::uint32_t plane = 0u; plane < kChannels; ++plane)
    {
        for (std::uint32_t y = 0u; y < kHeight; ++y)
        {
            for (std::uint32_t x = 0u; x < kWidth; ++x)
            {
                ASSERT_TRUE(writable->setValid(plane, x, y, true));
            }
        }
    }
    ASSERT_TRUE(writable->setValid(2u, 0u, 0u, false));
    const an::ValidityMaskView<const std::uint8_t> mask = writable->constView();
    an::SpatialSelection selection{};
    selection.validity = *an::ValidityDescriptor::create({.rule = an::ValidityRule::ExplicitMask});
    selection.mask = &mask;
    const an::Result<an::ChromaticNoisePlan> planned = an::planChromaticNoiseReduction({.whiteLevel = 1000.0});
    ASSERT_TRUE(planned.hasValue()) << planned.status().message();

    const an::Result<an::NoiseReductionResult> ran = an::reduceImageChromaticNoise(
        execution, an::SampleType::UInt16, source.data(), layout, destination.data(), layout, *planned, selection);
    ASSERT_TRUE(ran.hasValue()) << ran.status().message();
    EXPECT_EQ(destination[0u], 7777u);
    EXPECT_EQ(destination[1u], 7777u);
    EXPECT_EQ(destination[2u], 7777u);
    for (std::size_t pixel = 1u; pixel < source.size() / kChannels; ++pixel)
    {
        EXPECT_EQ(destination[pixel * kChannels], 200u);
        EXPECT_EQ(destination[pixel * kChannels + 1u], 300u);
        EXPECT_EQ(destination[pixel * kChannels + 2u], 400u);
    }
    EXPECT_EQ(ran->filter.run.selection, (an::ValidityCount{.valid = 9u, .total = 12u}));
    EXPECT_EQ(ran->filter.run.outputsWritten, 9u);
    EXPECT_EQ(ran->samplesChanged, 3u);
}

TEST(NoiseReductionTest, EveryChromaticNoiseRefusalIsNamed)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    EXPECT_EQ(an::planChromaticNoiseReduction({.method = static_cast<an::ChromaticNoiseMethod>(9u)}).status().code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::planChromaticNoiseReduction({.amount = 1.5}).status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::planChromaticNoiseReduction({.whiteLevel = 0.0}).status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_FALSE((std::is_constructible_v<an::ChromaticNoisePlan, const an::ChromaticNoiseRequest&,
                                          const an::ColorConversionPlan&, const an::ColorConversionPlan&>));
    EXPECT_EQ(an::planChromaticNoiseReduction({.output = {.writeBack = {.domain = an::DomainMapping::FullScale}}})
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
    an::ChromaticNoiseRequest wrongSchema{};
    wrongSchema.preserveLightness = true;
    wrongSchema.rgbEncoding.schema = an::ChannelSchema::Mono;
    EXPECT_EQ(an::planChromaticNoiseReduction(wrongSchema).status().code(), an::ErrorCode::IncompatibleArguments);

    const an::Result<an::ChromaticNoisePlan> planned = an::planChromaticNoiseReduction({});
    ASSERT_TRUE(planned.hasValue()) << planned.status().message();
    const an::ImageLayout mono = planarLayout(an::SampleType::Float32, 4u, 4u);
    const std::vector<float> source(16u, 0.5f);
    std::vector<float> destination(16u, 0.0f);
    EXPECT_EQ(an::reduceImageChromaticNoise(execution, an::SampleType::Float32, source.data(), mono, destination.data(),
                                            mono, *planned)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
}

TEST(NoiseReductionTest, BandingSuppressionLevelsTheRowsOfAPerfectlyBandedImage)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 16u;
    constexpr std::uint32_t kHeight = 9u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    const std::array<float, kHeight> offsets{0.00f, 0.03f, -0.02f, 0.01f, 0.05f, -0.04f, 0.02f, 0.00f, -0.01f};
    std::vector<float> source(static_cast<std::size_t>(kWidth) * kHeight, 0.0f);
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            source[static_cast<std::size_t>(y) * kWidth + x] = 0.5f + offsets[y];
        }
    }
    std::vector<float> destination(source.size(), -1.0f);

    const an::Result<an::NoiseReductionResult> ran = an::suppressImageBanding(
        execution, an::SampleType::Float32, source.data(), layout, destination.data(), layout, {});
    ASSERT_TRUE(ran.hasValue()) << ran.status().message();
    EXPECT_EQ(ran->filter.imagePasses, 2u);
    EXPECT_EQ(ran->filter.run.outputsWritten, static_cast<std::uint64_t>(kWidth) * kHeight);

    // Every row is corrected onto the brightest row's level, which is what shifting the correction
    // so that its smallest value is zero means.
    const float expected = 0.5f + *std::max_element(offsets.begin(), offsets.end());
    for (const float value : destination)
    {
        ASSERT_NEAR(value, expected, 1.0e-5f);
    }

    // An amount of zero writes the image back unchanged and reports that nothing moved.
    std::vector<float> untouched(source.size(), -1.0f);
    const an::Result<an::NoiseReductionResult> none = an::suppressImageBanding(
        execution, an::SampleType::Float32, source.data(), layout, untouched.data(), layout, {.amount = 0.0});
    ASSERT_TRUE(none.hasValue()) << none.status().message();
    EXPECT_EQ(none->samplesChanged, 0u);
    for (std::size_t index = 0u; index < source.size(); ++index)
    {
        ASSERT_FLOAT_EQ(untouched[index], source[index]);
    }
}

TEST(NoiseReductionTest, BandingSuppressionWorksDownColumnsWhenAskedTo)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 9u;
    constexpr std::uint32_t kHeight = 16u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    const std::array<float, kWidth> offsets{0.00f, 0.03f, -0.02f, 0.01f, 0.05f, -0.04f, 0.02f, 0.00f, -0.01f};
    std::vector<float> source(static_cast<std::size_t>(kWidth) * kHeight, 0.0f);
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            source[static_cast<std::size_t>(y) * kWidth + x] = 0.5f + offsets[x];
        }
    }
    std::vector<float> destination(source.size(), -1.0f);
    ASSERT_TRUE(an::suppressImageBanding(execution, an::SampleType::Float32, source.data(), layout, destination.data(),
                                         layout, {.verticalBanding = true})
                    .hasValue());
    const float expected = 0.5f + *std::max_element(offsets.begin(), offsets.end());
    for (const float value : destination)
    {
        ASSERT_NEAR(value, expected, 1.0e-5f);
    }
}

TEST(NoiseReductionTest, BandingCorrectionIsMeasuredPerChannelRatherThanAcrossTheImage)
{
    // One running minimum across every channel would let a badly banded red channel shift the
    // correction applied to blue. Here each channel is levelled against its own rows.
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 8u;
    constexpr std::uint32_t kHeight = 4u;
    constexpr std::size_t kPlane = 32u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight, 2u);
    const std::array<float, kHeight> strong{0.00f, 0.40f, 0.10f, 0.20f};
    const std::array<float, kHeight> weak{0.00f, 0.01f, 0.02f, 0.03f};
    std::vector<float> source(kPlane * 2u, 0.0f);
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            const std::size_t offset = static_cast<std::size_t>(y) * kWidth + x;
            source[offset] = 0.3f + strong[y];
            source[kPlane + offset] = 0.3f + weak[y];
        }
    }
    std::vector<float> destination(source.size(), -1.0f);
    ASSERT_TRUE(an::suppressImageBanding(execution, an::SampleType::Float32, source.data(), layout, destination.data(),
                                         layout, {})
                    .hasValue());
    for (std::size_t index = 0u; index < kPlane; ++index)
    {
        ASSERT_NEAR(destination[index], 0.3f + 0.40f, 1.0e-5f);
        ASSERT_NEAR(destination[kPlane + index], 0.3f + 0.03f, 1.0e-5f);
    }
}

TEST(NoiseReductionTest, AHighlightRejectedLineIsNeitherMeasuredNorWritten)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 4u;
    constexpr std::uint32_t kHeight = 4u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    std::vector<float> source(static_cast<std::size_t>(kWidth) * kHeight, 0.0f);
    std::fill(source.begin(), source.begin() + 2u * kWidth, 100.0f);
    std::fill(source.begin() + 3u * kWidth, source.end(), 2.0f);
    std::vector<float> destination(source.size(), -7.0f);

    const an::Result<an::NoiseReductionResult> ran =
        an::suppressImageBanding(execution, an::SampleType::Float32, source.data(), layout, destination.data(), layout,
                                 {.protectHighlights = true, .highlightSigma = 2.0});
    ASSERT_TRUE(ran.hasValue()) << ran.status().message();
    for (std::size_t index = 0u; index < 2u * kWidth; ++index)
    {
        EXPECT_FLOAT_EQ(destination[index], -7.0f) << "at " << index;
    }
    for (std::size_t index = 2u * kWidth; index < source.size(); ++index)
    {
        EXPECT_NEAR(destination[index], 2.0f, 1.0e-6f) << "at " << index;
    }
    EXPECT_EQ(ran->filter.run.selection, (an::ValidityCount{.valid = 8u, .total = 16u}));
    EXPECT_EQ(ran->filter.run.outputsWritten, 8u);
    EXPECT_EQ(ran->samplesChanged, 4u);
}

TEST(NoiseReductionTest, BandingCountsOnlyChangesThatSurviveIntegerWriteBack)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 8u;
    constexpr std::uint32_t kHeight = 2u;
    const an::ImageLayout layout = planarLayout(an::SampleType::UInt16, kWidth, kHeight);
    std::vector<std::uint16_t> source(static_cast<std::size_t>(kWidth) * kHeight, 100u);
    std::fill(source.begin() + kWidth, source.end(), static_cast<std::uint16_t>(101u));
    std::vector<std::uint16_t> destination(source.size(), 0u);

    const an::Result<an::NoiseReductionResult> ran = an::suppressImageBanding(
        execution, an::SampleType::UInt16, source.data(), layout, destination.data(), layout, {.amount = 0.1});
    ASSERT_TRUE(ran.hasValue()) << ran.status().message();
    EXPECT_EQ(destination, source);
    EXPECT_EQ(ran->samplesChanged, 0u);
}

TEST(NoiseReductionTest, EveryBandingRefusalIsNamed)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, 4u, 4u);
    std::vector<float> source(16u, 0.5f);
    std::vector<float> destination(16u, 0.0f);
    EXPECT_EQ(an::suppressImageBanding(execution, an::SampleType::Float32, source.data(), layout, destination.data(),
                                       layout, {.amount = -1.0})
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::suppressImageBanding(execution, an::SampleType::Float32, source.data(), layout, destination.data(),
                                       layout, {.protectHighlights = true, .highlightSigma = 0.0})
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(
        an::suppressImageBanding(execution, an::SampleType::Float32, source.data(), layout, source.data(), layout, {})
            .status()
            .code(),
        an::ErrorCode::IncompatibleArguments);
}

TEST(NoiseReductionTest, ImpulseRejectionReplacesTheSampleThatStandsAloneAndNothingElse)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 7u;
    constexpr std::uint32_t kHeight = 7u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    std::vector<float> source(static_cast<std::size_t>(kWidth) * kHeight, 0.2f);
    const std::size_t centre = static_cast<std::size_t>(kHeight / 2u) * kWidth + kWidth / 2u;
    source[centre] = 0.9f;
    std::vector<float> destination(source.size(), -1.0f);

    const an::Result<an::NoiseReductionResult> ran =
        an::rejectImageImpulseNoise(execution, an::SampleType::Float32, source.data(), layout, destination.data(),
                                    layout, {.radius = 1u, .highThreshold = 0.5});
    ASSERT_TRUE(ran.hasValue()) << ran.status().message();
    EXPECT_EQ(ran->samplesChanged, 1u);
    EXPECT_NEAR(destination[centre], 0.2f, 1.0e-6f);
    for (std::size_t index = 0u; index < source.size(); ++index)
    {
        if (index != centre)
        {
            ASSERT_FLOAT_EQ(destination[index], source[index]) << "at " << index;
        }
    }

    // A threshold above the deviation leaves the spike where it was, and reports so.
    std::vector<float> kept(source.size(), -1.0f);
    const an::Result<an::NoiseReductionResult> quiet =
        an::rejectImageImpulseNoise(execution, an::SampleType::Float32, source.data(), layout, kept.data(), layout,
                                    {.radius = 1u, .highThreshold = 0.95});
    ASSERT_TRUE(quiet.hasValue()) << quiet.status().message();
    EXPECT_EQ(quiet->samplesChanged, 0u);
    EXPECT_FLOAT_EQ(kept[centre], 0.9f);
}

TEST(NoiseReductionTest, ImpulseRejectionAnswersLowSamplesOnlyWhenAskedTo)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 7u;
    constexpr std::uint32_t kHeight = 7u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    std::vector<float> source(static_cast<std::size_t>(kWidth) * kHeight, 0.2f);
    const std::size_t centre = static_cast<std::size_t>(kHeight / 2u) * kWidth + kWidth / 2u;
    source[centre] = 0.02f;
    std::vector<float> highOnly(source.size(), -1.0f);
    std::vector<float> lowToo(source.size(), -1.0f);

    const an::Result<an::NoiseReductionResult> high =
        an::rejectImageImpulseNoise(execution, an::SampleType::Float32, source.data(), layout, highOnly.data(), layout,
                                    {.radius = 1u, .highThreshold = 0.5});
    ASSERT_TRUE(high.hasValue()) << high.status().message();
    EXPECT_EQ(high->samplesChanged, 0u);
    EXPECT_FLOAT_EQ(highOnly[centre], 0.02f);

    const an::Result<an::NoiseReductionResult> low =
        an::rejectImageImpulseNoise(execution, an::SampleType::Float32, source.data(), layout, lowToo.data(), layout,
                                    {.radius = 1u, .rejectHigh = false, .rejectLow = true, .lowThreshold = 0.5});
    ASSERT_TRUE(low.hasValue()) << low.status().message();
    EXPECT_EQ(low->samplesChanged, 1u);
    EXPECT_NEAR(lowToo[centre], 0.2f, 1.0e-6f);
}

TEST(NoiseReductionTest, ImpulseRejectionCopiesAnIntegerImageThroughItsOwnCodeValues)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 5u;
    constexpr std::uint32_t kHeight = 5u;
    const an::ImageLayout layout = planarLayout(an::SampleType::UInt16, kWidth, kHeight);
    std::vector<std::uint16_t> source(static_cast<std::size_t>(kWidth) * kHeight, 0u);
    for (std::size_t index = 0u; index < source.size(); ++index)
    {
        source[index] = static_cast<std::uint16_t>(20000u + (index * 7u) % 11u);
    }
    source[12u] = 60000u;
    std::vector<std::uint16_t> destination(source.size(), 0u);

    const an::Result<an::NoiseReductionResult> ran =
        an::rejectImageImpulseNoise(execution, an::SampleType::UInt16, source.data(), layout, destination.data(),
                                    layout, {.radius = 1u, .highThreshold = 0.5});
    ASSERT_TRUE(ran.hasValue()) << ran.status().message();
    EXPECT_EQ(ran->samplesChanged, 1u);
    for (std::size_t index = 0u; index < source.size(); ++index)
    {
        if (index != 12u)
        {
            ASSERT_EQ(destination[index], source[index]) << "at " << index;
        }
    }
    EXPECT_LT(destination[12u], 21000u);
}

TEST(NoiseReductionTest, AZeroSampleHasNoRelativeDeviationAndIsLeftAlone)
{
    // Dividing by the sample to get a relative deviation would make a stored zero an infinity that
    // compares above every threshold and is replaced whatever its neighbourhood says.
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 5u;
    constexpr std::uint32_t kHeight = 5u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    std::vector<float> source(static_cast<std::size_t>(kWidth) * kHeight, 0.0f);
    std::vector<float> destination(source.size(), -1.0f);

    const an::Result<an::NoiseReductionResult> ran = an::rejectImageImpulseNoise(
        execution, an::SampleType::Float32, source.data(), layout, destination.data(), layout,
        {.radius = 1u, .rejectHigh = true, .highThreshold = 0.1, .rejectLow = true, .lowThreshold = 0.1});
    ASSERT_TRUE(ran.hasValue()) << ran.status().message();
    EXPECT_EQ(ran->samplesChanged, 0u);
    for (const float value : destination)
    {
        ASSERT_FLOAT_EQ(value, 0.0f);
    }
}

TEST(NoiseReductionTest, EveryImpulseRefusalIsNamed)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, 5u, 5u);
    const std::vector<float> source(25u, 0.5f);
    std::vector<float> destination(25u, 0.0f);
    EXPECT_EQ(an::rejectImageImpulseNoise(execution, an::SampleType::Float32, source.data(), layout, destination.data(),
                                          layout, {.radius = 0u})
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::rejectImageImpulseNoise(execution, an::SampleType::Float32, source.data(), layout, destination.data(),
                                          layout, {.highThreshold = 1.0})
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::rejectImageImpulseNoise(execution, an::SampleType::Float32, source.data(), layout, destination.data(),
                                          layout, {.rejectLow = true, .lowThreshold = -0.5})
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::rejectImageImpulseNoise(execution, an::SampleType::Float32, source.data(), layout, destination.data(),
                                          layout, {.borders = {.border = static_cast<an::KernelBorderMode>(99u)}})
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);

    // A truncated border window is accepted here, so a saved rejection keeps the population it was
    // tuned against.
    EXPECT_TRUE(an::rejectImageImpulseNoise(execution, an::SampleType::Float32, source.data(), layout,
                                            destination.data(), layout,
                                            {.borders = {.border = an::KernelBorderMode::NotApplicable}})
                    .hasValue());
}

TEST(NoiseReductionTest, TheDeclaredEffectSaysThatRemovingNoiseChangesSamplesAndNothingElse)
{
    EXPECT_EQ(an::kNoiseReductionEffect.samples, an::SampleEffect::Writes);
    EXPECT_EQ(an::kNoiseReductionEffect.description, an::DescriptionEffect::Preserves);
    EXPECT_EQ(an::kNoiseReductionEffect.semantics, an::SemanticEffect::Preserves);
    EXPECT_EQ(an::kNoiseReductionEffect.solution, an::SolutionEffect::Preserves);
    EXPECT_EQ(an::checkOperationEffect(an::kNoiseReductionEffect), an::OperationEffectError::None);
}

TEST(NoiseReductionTest, ABilateralFilterAgreesBetweenTheCompiledAndReferenceExponentials)
{
    // The range weight is an exponential, and the compiled per-instruction-set polynomial and the
    // platform library are two evaluations of it that the accuracy tier bounds. The filtered
    // pictures therefore agree to within that tier scaled by the window, whichever table ran.
    const an::EngineContext engine = makeEngine(4u);
    const an::ExecutionContext reference = makeExecution(engine, 3u, an::ComputeBackend::Reference);
    const an::ExecutionContext compiled = makeExecution(engine, 3u, an::ComputeBackend::Automatic);
    constexpr std::uint32_t kWidth = 61u;
    constexpr std::uint32_t kHeight = 37u;
    const std::vector<float> source = randomPlane(kWidth, kHeight, 0xB11A7u);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    const an::Result<an::BilateralPlan> planned = an::planBilateralFilter({.spatialSigma = 2.0, .rangeSigma = 0.08});
    ASSERT_TRUE(planned.hasValue()) << planned.status().message();

    std::vector<float> referenceOut(source.size(), -1.0f);
    std::vector<float> compiledOut(source.size(), -1.0f);
    const an::Result<an::NoiseReductionResult> first = an::filterImageBilateral(
        reference, an::SampleType::Float32, source.data(), layout, referenceOut.data(), layout, *planned);
    const an::Result<an::NoiseReductionResult> second = an::filterImageBilateral(
        compiled, an::SampleType::Float32, source.data(), layout, compiledOut.data(), layout, *planned);
    ASSERT_TRUE(first.hasValue()) << first.status().message();
    ASSERT_TRUE(second.hasValue()) << second.status().message();
    EXPECT_EQ(first->filter.run.outputsWritten, source.size());
    EXPECT_EQ(second->filter.run.outputsWritten, source.size());
    for (std::size_t index = 0u; index < source.size(); ++index)
    {
        ASSERT_TRUE(std::isfinite(referenceOut[index]) && std::isfinite(compiledOut[index])) << index;
        ASSERT_NEAR(compiledOut[index], referenceOut[index], 2.0e-6f) << "at " << index;
    }
}

TEST(NoiseReductionTest, ABilateralFilterProducesTheSameBytesAtEveryThreadCount)
{
    // The accumulation order of every output sample is a fixed function of where it sits, not of
    // which worker reached it, so one worker and four produce identical bytes - over a padded,
    // three-channel image whose height is not a multiple of any band, and over an image so small
    // that every pixel is a border pixel.
    const an::EngineContext engine = makeEngine(4u);
    const an::ExecutionContext one = makeExecution(engine, 1u);
    const an::ExecutionContext four = makeExecution(engine, 4u);
    const an::Result<an::BilateralPlan> planned = an::planBilateralFilter({.spatialSigma = 2.5, .rangeSigma = 0.05});
    ASSERT_TRUE(planned.hasValue()) << planned.status().message();
    EXPECT_EQ(planned->radius(), 4u);

    const auto compare = [&](std::uint32_t width, std::uint32_t height, std::uint32_t channels, std::uint32_t padding) {
        const an::ImageLayout layout = planarLayout(an::SampleType::Float32, width, height, channels, padding);
        // The buffers span every stride in full so the padding after the last row can be checked.
        const std::size_t count = static_cast<std::size_t>(layout.planeStrideBytes() * channels / sizeof(float));
        std::vector<float> source(count, 0.0f);
        spatial_test::Sequence sequence{0x9A5Bu + width};
        for (float& sample : source)
        {
            sample = sequence.next();
        }
        std::vector<float> narrowOut(count, -1.0f);
        std::vector<float> wideOut(count, -1.0f);
        const an::Result<an::NoiseReductionResult> narrow = an::filterImageBilateral(
            one, an::SampleType::Float32, source.data(), layout, narrowOut.data(), layout, *planned);
        const an::Result<an::NoiseReductionResult> wide = an::filterImageBilateral(
            four, an::SampleType::Float32, source.data(), layout, wideOut.data(), layout, *planned);
        ASSERT_TRUE(narrow.hasValue()) << narrow.status().message();
        ASSERT_TRUE(wide.hasValue()) << wide.status().message();
        EXPECT_EQ(narrow->filter.run.outputsWritten, static_cast<std::uint64_t>(width) * height * channels);
        EXPECT_EQ(wide->filter.run.outputsWritten, narrow->filter.run.outputsWritten);
        EXPECT_EQ(narrow->samplesChanged, wide->samplesChanged);
        EXPECT_EQ(std::memcmp(narrowOut.data(), wideOut.data(), count * sizeof(float)), 0)
            << width << "x" << height << "x" << channels;
        for (std::uint32_t channel = 0u; channel < channels; ++channel)
        {
            for (std::uint32_t y = 0u; y < height; ++y)
            {
                const float* const row = narrowOut.data() + *layout.rowByteOffset(channel, y) / sizeof(float);
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    ASSERT_TRUE(std::isfinite(row[x])) << channel << "," << x << "," << y;
                    ASSERT_GE(row[x], 0.0f);
                    ASSERT_LE(row[x], 1.0f);
                }
                for (std::uint32_t x = width; x < width + padding; ++x)
                {
                    ASSERT_EQ(row[x], -1.0f) << "padding " << channel << "," << x << "," << y;
                }
            }
        }
    };
    compare(70u, 45u, 3u, 5u);
    compare(9u, 6u, 1u, 0u);
    compare(3u, 2u, 2u, 1u);
}

} // namespace
