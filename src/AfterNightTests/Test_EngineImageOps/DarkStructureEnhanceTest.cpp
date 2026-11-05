// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the dark-structure domain.
//
// The mask is the operation, so most of the suite is about the mask: a frame with a dark region in
// it has to produce a mask that is high inside that region and low outside it, and a frame with no
// dark structure at all has to say so rather than amplifying its own rounding into one. The
// enhancement is then checked for the two things that follow - it moves the samples the mask
// selects, and it leaves the rest exactly where they were.

#include <AfterNightEngine/Algorithms/Detail/DarkStructureEnhance.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include "SpatialFilterFixture.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stop_token>
#include <type_traits>
#include <vector>

namespace
{

using spatial_test::makeEngine;
using spatial_test::makeExecution;
using spatial_test::planarLayout;
using spatial_test::randomPlane;

static_assert(
    !std::is_constructible_v<an::DarkStructurePlan, const an::DarkStructureRequest&, const an::MultiscaleTransformPlan&,
                             const an::MultiscaleTransformPlan&, const an::MultiscaleTransformPlan&>);

/// A frame holding a bright sky with one large dark patch in it.
///
/// The patch is what the operation exists to find: it is far larger than the noise, far smaller than
/// the frame, and darker than everything around it.
///
/// @param width how many pixels one row holds.
/// @param height how many rows.
/// @param centreX the patch's centre column.
/// @param centreY its centre row.
/// @param radius its radius, in pixels.
/// @param depth how far below the sky its centre sits.
/// @return the samples, row-major.
[[nodiscard]] std::vector<float> skyWithDarkPatch(std::uint32_t width, std::uint32_t height, std::uint32_t centreX,
                                                  std::uint32_t centreY, double radius, double depth)
{
    std::vector<float> samples(static_cast<std::size_t>(width) * height, 0.45f);
    for (std::uint32_t y = 0u; y < height; ++y)
    {
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            const double dx = static_cast<double>(x) - static_cast<double>(centreX);
            const double dy = static_cast<double>(y) - static_cast<double>(centreY);
            const double distance = std::sqrt(dx * dx + dy * dy);
            if (distance > radius)
            {
                continue;
            }
            // A soft-edged well, so the mask has a gradient to follow rather than a step.
            const double fall = 0.5 * (1.0 + std::cos(distance / radius * 3.14159265358979323846));
            samples[static_cast<std::size_t>(y) * width + x] -= static_cast<float>(depth * fall);
        }
    }
    return samples;
}

/// A validated plan, so a case that is not about planning does not spell the request twice.
///
/// @param request what to ask for.
/// @return the plan.
[[nodiscard]] an::DarkStructurePlan planOf(const an::DarkStructureRequest& request)
{
    const an::Result<an::DarkStructurePlan> planned = an::planDarkStructure(request);
    EXPECT_TRUE(planned.hasValue()) << planned.status().message();
    return planned.hasValue() ? *planned : an::DarkStructurePlan{};
}

TEST(DarkStructureEnhanceTest, APlanDerivesThreeTransformsAndRefusesWhatCannotMeanAnything)
{
    an::DarkStructureRequest request{};
    request.removedLayers = 9u;
    const an::DarkStructurePlan plan = planOf(request);
    EXPECT_EQ(plan.finePlan().layerCount(), an::kDarkStructureFineLayers + 1u);
    EXPECT_EQ(plan.coarsePlan().layerCount(), 10u);
    EXPECT_EQ(plan.maskPlan().layerCount(), an::kDarkStructureMaskLayers);
    EXPECT_EQ(plan.maskPlan().request().scaling, an::MultiscaleScalingFunction::LinearInterpolation3);
    EXPECT_EQ(plan.finePlan().request().scaling, an::MultiscaleScalingFunction::BSpline5);
    EXPECT_GT(plan.imagePasses(), 0u);
    EXPECT_EQ(plan.maskLayerBytes(100u, 80u), static_cast<std::uint64_t>(100u) * 80u * 3u * sizeof(float));

    an::DarkStructureRequest broken = request;
    broken.removedLayers = an::kMinDarkStructureRemovedLayers - 1u;
    const auto belowMinimum = an::planDarkStructure(broken);
    EXPECT_EQ(belowMinimum.code(), an::ErrorCode::OutOfRange);
    ASSERT_NE(belowMinimum.status().findContext(an::ContextKey::Limit), nullptr);
    EXPECT_EQ(belowMinimum.status().findContext(an::ContextKey::Limit)->count, an::kMinDarkStructureRemovedLayers);
    broken = request;
    broken.removedLayers = an::kMaxDarkStructureRemovedLayers + 1u;
    EXPECT_EQ(an::planDarkStructure(broken).status().code(), an::ErrorCode::OutOfRange);
    broken = request;
    broken.iterations = 0u;
    EXPECT_EQ(an::planDarkStructure(broken).status().code(), an::ErrorCode::OutOfRange);
    broken = request;
    broken.iterations = an::kMaxDarkStructureIterations + 1u;
    EXPECT_EQ(an::planDarkStructure(broken).status().code(), an::ErrorCode::OutOfRange);
    broken = request;
    broken.whiteLevel = 0.0;
    EXPECT_EQ(an::planDarkStructure(broken).status().code(), an::ErrorCode::InvalidArgument);
    broken = request;
    broken.transfer.midtones = 1.5;
    EXPECT_EQ(an::planDarkStructure(broken).status().code(), an::ErrorCode::InvalidArgument);
    broken = request;
    broken.transfer.shadows = 0.9;
    broken.transfer.highlights = 0.1;
    EXPECT_EQ(an::planDarkStructure(broken).status().code(), an::ErrorCode::InvalidArgument);
    broken = request;
    broken.scaling = static_cast<an::MultiscaleScalingFunction>(99u);
    EXPECT_EQ(an::planDarkStructure(broken).status().code(), an::ErrorCode::InvalidArgument);
    broken = request;
    broken.output.writeBack.domain = an::DomainMapping::FullScale;
    EXPECT_EQ(an::planDarkStructure(broken).status().code(), an::ErrorCode::InvalidArgument);
}

TEST(DarkStructureEnhanceTest, AMaskIsHighInsideTheDarkStructureAndLowOutsideIt)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 192u;
    constexpr std::uint32_t kHeight = 160u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    const std::vector<float> samples = skyWithDarkPatch(kWidth, kHeight, kWidth / 2u, kHeight / 2u, 30.0, 0.25);

    an::DarkStructureRequest request{};
    request.removedLayers = 8u;
    const an::DarkStructurePlan plan = planOf(request);

    std::vector<float> mask(samples.size(), -1.0f);
    const std::optional<an::PlaneView<float>> maskPlane =
        an::PlaneView<float>::create(mask, kWidth, kHeight, 1u, static_cast<std::uint64_t>(kWidth) * sizeof(float));
    ASSERT_TRUE(maskPlane.has_value());
    const an::Result<an::DarkStructureResult> built = an::buildImageDarkStructureMask(
        engine, execution, an::SampleType::Float32, samples.data(), layout, plan, *maskPlane);
    ASSERT_TRUE(built.hasValue()) << built.status().message();
    EXPECT_EQ(built->channelCount, 1u);
    EXPECT_EQ(built->iterationsRun, 0u);
    EXPECT_FALSE(built->maskDegenerate);
    EXPECT_EQ(built->run.outputsWritten, static_cast<std::uint64_t>(kWidth) * kHeight);
    EXPECT_GT(built->measurementHighest, built->measurementLowest);
    EXPECT_GT(built->workingBytes, 0u);
    ASSERT_NE(engine.memoryResource(), nullptr);
    EXPECT_EQ(built->workingBytes, engine.memoryResource()->counters().peakBytes);
    EXPECT_GT(built->maskCoefficientsShrunk, 0u);
    EXPECT_EQ(built->precision, an::ComputePrecision::Float32);

    for (const float weight : mask)
    {
        ASSERT_GE(weight, 0.0f);
        ASSERT_LE(weight, 1.0f);
    }
    const float centre = mask[static_cast<std::size_t>(kHeight / 2u) * kWidth + kWidth / 2u];
    const float corner = mask[static_cast<std::size_t>(6u) * kWidth + 6u];
    EXPECT_GT(centre, 0.7f);
    EXPECT_LT(corner, 0.3f);
    EXPECT_GT(centre, corner + 0.5f);
}

TEST(DarkStructureEnhanceTest, AFrameWithNoDarkStructureProducesAMaskOfZerosAndSaysSo)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 64u;
    constexpr std::uint32_t kHeight = 48u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    const std::vector<float> samples(static_cast<std::size_t>(kWidth) * kHeight, 0.4f);

    const an::DarkStructurePlan plan = planOf(an::DarkStructureRequest{});
    std::vector<float> mask(samples.size(), -1.0f);
    const std::optional<an::PlaneView<float>> maskPlane =
        an::PlaneView<float>::create(mask, kWidth, kHeight, 1u, static_cast<std::uint64_t>(kWidth) * sizeof(float));
    ASSERT_TRUE(maskPlane.has_value());
    const an::Result<an::DarkStructureResult> built = an::buildImageDarkStructureMask(
        engine, execution, an::SampleType::Float32, samples.data(), layout, plan, *maskPlane);
    ASSERT_TRUE(built.hasValue()) << built.status().message();
    EXPECT_TRUE(built->maskDegenerate);
    for (const float weight : mask)
    {
        ASSERT_EQ(weight, 0.0f);
    }

    // A frame the mask cannot describe is a frame the enhancement leaves exactly as it was.
    std::vector<float> enhanced(samples.size(), -1.0f);
    const an::Result<an::DarkStructureResult> applied = an::enhanceImageDarkStructures(
        engine, execution, an::SampleType::Float32, samples.data(), layout, enhanced.data(), layout, plan);
    ASSERT_TRUE(applied.hasValue()) << applied.status().message();
    EXPECT_TRUE(applied->maskDegenerate);
    EXPECT_EQ(applied->samplesChanged, 0u);
    for (const float sample : enhanced)
    {
        ASSERT_NEAR(sample, 0.4f, 1.0e-6f);
    }
}

TEST(DarkStructureEnhanceTest, AnEnhancementDarkensWhereTheMaskSelectsAndLeavesTheRestAlone)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 192u;
    constexpr std::uint32_t kHeight = 160u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    const std::vector<float> samples = skyWithDarkPatch(kWidth, kHeight, kWidth / 2u, kHeight / 2u, 30.0, 0.25);

    an::DarkStructureRequest request{};
    request.removedLayers = 8u;
    request.transfer.midtones = 0.8;
    const an::DarkStructurePlan plan = planOf(request);

    std::vector<float> mask(samples.size(), -1.0f);
    std::vector<float> enhanced(samples.size(), -1.0f);
    const std::optional<an::PlaneView<float>> maskPlane =
        an::PlaneView<float>::create(mask, kWidth, kHeight, 1u, static_cast<std::uint64_t>(kWidth) * sizeof(float));
    ASSERT_TRUE(maskPlane.has_value());
    const an::Result<an::DarkStructureResult> applied = an::enhanceImageDarkStructures(
        engine, execution, an::SampleType::Float32, samples.data(), layout, enhanced.data(), layout, plan, &*maskPlane);
    ASSERT_TRUE(applied.hasValue()) << applied.status().message();
    EXPECT_EQ(applied->iterationsRun, 1u);
    EXPECT_GT(applied->samplesChanged, 0u);
    EXPECT_FALSE(applied->maskDegenerate);

    const std::size_t centre = static_cast<std::size_t>(kHeight / 2u) * kWidth + kWidth / 2u;
    const std::size_t corner = static_cast<std::size_t>(6u) * kWidth + 6u;
    // A midtones balance above one half darkens, and the mask decides where.
    EXPECT_LT(enhanced[centre], samples[centre]);
    EXPECT_NEAR(enhanced[corner], samples[corner], 0.02f);
    EXPECT_GT(mask[centre], mask[corner]);

    // The mask the enhancement used is the mask the builder produces from the same frame.
    std::vector<float> separate(samples.size(), -1.0f);
    const std::optional<an::PlaneView<float>> separatePlane =
        an::PlaneView<float>::create(separate, kWidth, kHeight, 1u, static_cast<std::uint64_t>(kWidth) * sizeof(float));
    ASSERT_TRUE(separatePlane.has_value());
    ASSERT_TRUE(an::buildImageDarkStructureMask(engine, execution, an::SampleType::Float32, samples.data(), layout,
                                                plan, *separatePlane)
                    .hasValue());
    for (std::size_t index = 0u; index < mask.size(); ++index)
    {
        ASSERT_EQ(mask[index], separate[index]) << "at " << index;
    }
}

TEST(DarkStructureEnhanceTest, MoreIterationsMoveTheResultFurtherAndABrighteningTransferGoesTheOtherWay)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 160u;
    constexpr std::uint32_t kHeight = 128u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    const std::vector<float> samples = skyWithDarkPatch(kWidth, kHeight, kWidth / 2u, kHeight / 2u, 26.0, 0.25);
    const std::size_t centre = static_cast<std::size_t>(kHeight / 2u) * kWidth + kWidth / 2u;

    an::DarkStructureRequest request{};
    request.removedLayers = 8u;
    request.transfer.midtones = 0.8;
    request.iterations = 1u;
    std::vector<float> once(samples.size(), 0.0f);
    ASSERT_TRUE(an::enhanceImageDarkStructures(engine, execution, an::SampleType::Float32, samples.data(), layout,
                                               once.data(), layout, planOf(request))
                    .hasValue());

    request.iterations = 3u;
    std::vector<float> thrice(samples.size(), 0.0f);
    const an::Result<an::DarkStructureResult> repeated = an::enhanceImageDarkStructures(
        engine, execution, an::SampleType::Float32, samples.data(), layout, thrice.data(), layout, planOf(request));
    ASSERT_TRUE(repeated.hasValue()) << repeated.status().message();
    EXPECT_EQ(repeated->iterationsRun, 3u);
    EXPECT_LT(thrice[centre], once[centre]);

    // A balance below one half is allowed; the same mask with one brightens exactly the structures
    // it darkened.
    request.iterations = 1u;
    request.transfer.midtones = 0.2;
    std::vector<float> brightened(samples.size(), 0.0f);
    ASSERT_TRUE(an::enhanceImageDarkStructures(engine, execution, an::SampleType::Float32, samples.data(), layout,
                                               brightened.data(), layout, planOf(request))
                    .hasValue());
    EXPECT_GT(brightened[centre], samples[centre]);
}

TEST(DarkStructureEnhanceTest, AWhiteLevelIsWhatAnIntegerFrameIsMeasuredAgainst)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 128u;
    constexpr std::uint32_t kHeight = 96u;
    const std::vector<float> floating = skyWithDarkPatch(kWidth, kHeight, kWidth / 2u, kHeight / 2u, 22.0, 0.25);
    const an::ImageLayout floatLayout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    const an::ImageLayout integerLayout = planarLayout(an::SampleType::UInt16, kWidth, kHeight);

    std::vector<std::uint16_t> stored(floating.size(), 0u);
    for (std::size_t index = 0u; index < floating.size(); ++index)
    {
        stored[index] = static_cast<std::uint16_t>(std::lround(static_cast<double>(floating[index]) * 65535.0));
    }

    an::DarkStructureRequest request{};
    request.removedLayers = 8u;
    request.transfer.midtones = 0.8;
    std::vector<float> floatingEnhanced(floating.size(), 0.0f);
    ASSERT_TRUE(an::enhanceImageDarkStructures(engine, execution, an::SampleType::Float32, floating.data(), floatLayout,
                                               floatingEnhanced.data(), floatLayout, planOf(request))
                    .hasValue());

    request.whiteLevel = 65535.0;
    std::vector<std::uint16_t> integerEnhanced(stored.size(), 0u);
    const an::Result<an::DarkStructureResult> applied =
        an::enhanceImageDarkStructures(engine, execution, an::SampleType::UInt16, stored.data(), integerLayout,
                                       integerEnhanced.data(), integerLayout, planOf(request));
    ASSERT_TRUE(applied.hasValue()) << applied.status().message();

    // The same picture in two representations is the same operation, because the white level says
    // what a fully exposed sample is in each of them.
    for (std::size_t index = 0u; index < stored.size(); index += 37u)
    {
        const double asFloat = static_cast<double>(integerEnhanced[index]) / 65535.0;
        ASSERT_NEAR(asFloat, static_cast<double>(floatingEnhanced[index]), 2.0e-3) << "at " << index;
    }
}

TEST(DarkStructureEnhanceTest, EveryExecutionRefusalIsNamed)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 48u;
    constexpr std::uint32_t kHeight = 40u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    std::vector<float> samples(static_cast<std::size_t>(kWidth) * kHeight, 0.4f);
    std::vector<float> destination(samples.size(), 0.0f);
    std::vector<float> mask(samples.size(), 0.0f);
    const std::optional<an::PlaneView<float>> maskPlane =
        an::PlaneView<float>::create(mask, kWidth, kHeight, 1u, static_cast<std::uint64_t>(kWidth) * sizeof(float));
    ASSERT_TRUE(maskPlane.has_value());
    const an::DarkStructurePlan plan = planOf(an::DarkStructureRequest{});

    EXPECT_EQ(
        an::buildImageDarkStructureMask(engine, execution, an::SampleType::Float32, nullptr, layout, plan, *maskPlane)
            .status()
            .code(),
        an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::buildImageDarkStructureMask(engine, execution, an::SampleType::Float32, samples.data(), layout,
                                              an::DarkStructurePlan{}, *maskPlane)
                  .status()
                  .code(),
              an::ErrorCode::NotInitialized);
    EXPECT_EQ(an::buildImageDarkStructureMask(an::EngineContext{}, execution, an::SampleType::Float32, samples.data(),
                                              layout, plan, *maskPlane)
                  .status()
                  .code(),
              an::ErrorCode::NotInitialized);

    std::vector<float> smallMask(16u, 0.0f);
    const std::optional<an::PlaneView<float>> smallPlane =
        an::PlaneView<float>::create(smallMask, 4u, 4u, 1u, 4u * sizeof(float));
    ASSERT_TRUE(smallPlane.has_value());
    EXPECT_EQ(an::buildImageDarkStructureMask(engine, execution, an::SampleType::Float32, samples.data(), layout, plan,
                                              *smallPlane)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);

    EXPECT_EQ(an::enhanceImageDarkStructures(engine, execution, an::SampleType::Float32, samples.data(), layout,
                                             samples.data(), layout, plan)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(an::enhanceImageDarkStructures(engine, execution, an::SampleType::Float32, samples.data(), layout,
                                             destination.data(), layout, plan, &*smallPlane)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);

    const an::EngineContext otherEngine = makeEngine();
    EXPECT_EQ(an::buildImageDarkStructureMask(otherEngine, execution, an::SampleType::Float32, samples.data(), layout,
                                              plan, *maskPlane)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);

    std::stop_source stopped;
    an::ExecutionOptions options;
    options.stopToken = stopped.get_token();
    an::Result<an::ExecutionContext> cancellable = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(cancellable.hasValue()) << cancellable.status().message();
    stopped.request_stop();
    EXPECT_EQ(an::enhanceImageDarkStructures(engine, *cancellable, an::SampleType::Float32, samples.data(), layout,
                                             destination.data(), layout, plan)
                  .status()
                  .code(),
              an::ErrorCode::Cancelled);
    for (const float sample : destination)
    {
        ASSERT_EQ(sample, 0.0f);
    }
}

TEST(DarkStructureEnhanceTest, ARejectedSampleIsSubstitutedOnceAndCounted)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 96u;
    constexpr std::uint32_t kHeight = 72u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    std::vector<float> samples = skyWithDarkPatch(kWidth, kHeight, kWidth / 2u, kHeight / 2u, 18.0, 0.25);
    samples[100u] = std::numeric_limits<float>::quiet_NaN();
    samples[101u] = std::numeric_limits<float>::infinity();

    an::SpatialSelection selection{};
    selection.validity = an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly}).value();

    std::vector<float> mask(samples.size(), -1.0f);
    const std::optional<an::PlaneView<float>> maskPlane =
        an::PlaneView<float>::create(mask, kWidth, kHeight, 1u, static_cast<std::uint64_t>(kWidth) * sizeof(float));
    ASSERT_TRUE(maskPlane.has_value());
    const an::DarkStructurePlan plan = planOf(an::DarkStructureRequest{});
    const an::Result<an::DarkStructureResult> built = an::buildImageDarkStructureMask(
        engine, execution, an::SampleType::Float32, samples.data(), layout, plan, *maskPlane, selection);
    ASSERT_TRUE(built.hasValue()) << built.status().message();
    EXPECT_EQ(built->substitutedSamples, 2u);
    EXPECT_EQ(built->run.selection.valid, static_cast<std::uint64_t>(kWidth) * kHeight - 2u);
    for (const float weight : mask)
    {
        ASSERT_TRUE(std::isfinite(weight));
    }

    std::vector<float> enhanced(samples.size(), -7.0f);
    const an::Result<an::DarkStructureResult> applied =
        an::enhanceImageDarkStructures(engine, execution, an::SampleType::Float32, samples.data(), layout,
                                       enhanced.data(), layout, plan, nullptr, selection);
    ASSERT_TRUE(applied.hasValue()) << applied.status().message();
    EXPECT_EQ(applied->substitutedSamples, 2u);
    EXPECT_EQ(applied->run.outputsWritten, static_cast<std::uint64_t>(kWidth) * kHeight - 2u);
    EXPECT_EQ(enhanced[100u], -7.0f);
    EXPECT_EQ(enhanced[101u], -7.0f);
}

TEST(DarkStructureEnhanceTest, TheDeclaredEffectsSayWhatEachHalfDoes)
{
    EXPECT_EQ(an::kDarkStructureMaskEffect.samples, an::SampleEffect::Reads);
    EXPECT_EQ(an::checkOperationEffect(an::kDarkStructureMaskEffect), an::OperationEffectError::None);
    EXPECT_EQ(an::kDarkStructureEnhanceEffect.samples, an::SampleEffect::Writes);
    EXPECT_EQ(an::kDarkStructureEnhanceEffect.description, an::DescriptionEffect::Preserves);
    EXPECT_EQ(an::kDarkStructureEnhanceEffect.semantics, an::SemanticEffect::Preserves);
    EXPECT_EQ(an::kDarkStructureEnhanceEffect.validity.policy, an::ValidityPolicy::Preserve);
    EXPECT_EQ(an::checkOperationEffect(an::kDarkStructureEnhanceEffect), an::OperationEffectError::None);
}

} // namespace
