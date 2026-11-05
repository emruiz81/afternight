// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract, compatibility equations and metadata tests for direct star reduction.

#include <AfterNightEngine/Algorithms/Stars/StarReduction.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/ImageSemantics.h>

#include "RegistrationTestSupport.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <type_traits>
#include <vector>

namespace
{

[[nodiscard]] double mtf(double midtones, double value)
{
    if (value <= 0.0)
    {
        return 0.0;
    }
    if (value >= 1.0)
    {
        return 1.0;
    }
    return ((midtones - 1.0) * value) / (((2.0 * midtones - 1.0) * value) - midtones);
}

[[nodiscard]] an::StarReductionPlan makePlan(const an::ImageDescriptor& descriptor,
                                             an::StarReductionRequest request = {})
{
    an::Result<an::StarReductionPlan> planned = an::planStarReduction(descriptor, descriptor, request);
    EXPECT_TRUE(planned.hasValue()) << planned.status().message();
    return planned.hasValue() ? *planned : an::StarReductionPlan{};
}

[[nodiscard]] double reduceOne(an::StarReductionRequest request, double target, double starless)
{
    an::EngineContext engine = registration_test::makeEngine();
    an::ExecutionContext execution = registration_test::makeExecution(engine);
    const an::ImageDescriptor descriptor = registration_test::descriptorOf(1u, 1u, 1u, an::SampleType::Float64);
    const an::ImageLayout layout = registration_test::layoutOf(descriptor);
    const an::StarReductionPlan plan = makePlan(descriptor, request);
    double destination = -1.0;
    an::Result<an::StarReductionResult> reduced =
        an::reduceStars(execution, &target, layout, &starless, layout, &destination, layout, plan);
    EXPECT_TRUE(reduced.hasValue()) << reduced.status().message();
    EXPECT_EQ(reduced->samplesWritten, 1u);
    return destination;
}

} // namespace

TEST(StarReductionContractTest, NamesEveryChoiceAndPublishesCheckedEffects)
{
    EXPECT_STREQ(an::starReductionMethodName(an::StarReductionMethod::Transfer), "Transfer");
    EXPECT_STREQ(an::starReductionMethodName(an::StarReductionMethod::Halo), "Halo");
    EXPECT_STREQ(an::starReductionMethodName(an::StarReductionMethod::Star), "Star");
    EXPECT_STREQ(an::starReductionMethodName(an::StarReductionMethod::Lightness), "Lightness");
    EXPECT_STREQ(an::starReductionMethodName(static_cast<an::StarReductionMethod>(99)), "UnknownStarReductionMethod");
    EXPECT_FALSE(an::isStarReductionMethod(static_cast<an::StarReductionMethod>(99)));

    EXPECT_STREQ(an::starReductionStrengthName(an::StarReductionStrength::Strong), "Strong");
    EXPECT_STREQ(an::starReductionStrengthName(an::StarReductionStrength::Moderate), "Moderate");
    EXPECT_STREQ(an::starReductionStrengthName(an::StarReductionStrength::Soft), "Soft");
    EXPECT_STREQ(an::starReductionStrengthName(static_cast<an::StarReductionStrength>(99)),
                 "UnknownStarReductionStrength");

    EXPECT_EQ(an::checkOperationEffect(an::kStarReductionEffect), an::OperationEffectError::None);
    EXPECT_EQ(an::checkOperationEffect(an::kReducedStarExtractionEffect), an::OperationEffectError::None);
    EXPECT_EQ(an::checkOperationEffect(an::kSmallStarProtectionMaskEffect), an::OperationEffectError::None);
    EXPECT_EQ(an::kStarReductionEffect.validity.policy, an::ValidityPolicy::Reject);
}

TEST(StarReductionContractTest, PlannerRequiresOneExplicitRepresentationAndAUsableFractionalDomain)
{
    const an::ImageDescriptor f32 = registration_test::descriptorOf(4u, 3u);
    const an::ImageDescriptor f64 = registration_test::descriptorOf(4u, 3u, 1u, an::SampleType::Float64);
    an::Result<an::StarReductionPlan> mixed = an::planStarReduction(f32, f64, {});
    ASSERT_FALSE(mixed.hasValue());
    EXPECT_EQ(mixed.status().code(), an::ErrorCode::IncompatibleArguments);

    const an::ImageDescriptor raw =
        registration_test::descriptorOf(4u, 3u, 1u, an::SampleType::Float32, an::NumericDomain::RawSensorCode);
    an::Result<an::StarReductionPlan> rawPlan = an::planStarReduction(raw, raw, {});
    ASSERT_FALSE(rawPlan.hasValue());
    EXPECT_EQ(rawPlan.status().code(), an::ErrorCode::Unsupported);

    an::StarReductionRequest invalid;
    invalid.iterations = 0u;
    an::Result<an::StarReductionPlan> zeroIterations = an::planStarReduction(f32, f32, invalid);
    ASSERT_FALSE(zeroIterations.hasValue());
    EXPECT_EQ(zeroIterations.status().code(), an::ErrorCode::OutOfRange);

    invalid = {};
    invalid.stretchFactor = 1.0;
    an::Result<an::StarReductionPlan> endpoint = an::planStarReduction(f32, f32, invalid);
    ASSERT_FALSE(endpoint.hasValue());
    EXPECT_EQ(endpoint.status().code(), an::ErrorCode::OutOfRange);
}

TEST(StarReductionEquationTest, StarStrongModerateAndSoftMatchThePublishedEquations)
{
    an::StarReductionRequest request;
    request.method = an::StarReductionMethod::Star;
    request.iterations = 3u;
    request.strength = an::StarReductionStrength::Strong;

    double expected = 0.82 * 0.82 + (1.0 - 0.82) * 0.43;
    expected = expected * expected + (1.0 - expected) * 0.43;
    expected = expected * expected + (1.0 - expected) * 0.43;
    EXPECT_NEAR(reduceOne(request, 0.82, 0.43), expected, 1.0e-13);

    request.iterations = 1u;
    request.strength = an::StarReductionStrength::Soft;
    const double base = 0.74 * 0.74 + (1.0 - 0.74) * 0.31;
    const double moderate = (std::max)(base, 0.74 * base + base * (1.0 - base));
    const double soft = 0.5 * ((1.0 - 0.74) * 0.74 + (1.0 + 0.74) * moderate);
    EXPECT_NEAR(reduceOne(request, 0.74, 0.31), soft, 1.0e-13);
}

TEST(StarReductionEquationTest, TransferAndHaloMatchTheCompatibilityExpressions)
{
    an::StarReductionRequest request;
    request.method = an::StarReductionMethod::Transfer;
    request.stretchFactor = 0.2;
    const double midtones = 1.0 - request.stretchFactor;
    const double transfer = 1.0 - ((1.0 - mtf(midtones, 0.68)) / (1.0 - mtf(midtones, 0.29))) * (1.0 - 0.29);
    EXPECT_NEAR(reduceOne(request, 0.68, 0.29), transfer, 1.0e-13);

    request.method = an::StarReductionMethod::Halo;
    const double first = 1.0 - (1.0 - 0.68) / (1.0 - 0.29);
    const double second = 1.0 - (1.0 - mtf(midtones, 0.68)) / (1.0 - mtf(midtones, 0.29));
    const double halo = 0.68 - 0.5 * (1.0 + 0.68) * (first - second) * (1.0 - 0.5 * 0.29);
    EXPECT_NEAR(reduceOne(request, 0.68, 0.29), halo, 1.0e-13);
}

TEST(StarReductionEquationTest, LightnessUsesTheStatedRgbEncoding)
{
    an::EngineContext engine = registration_test::makeEngine();
    an::ExecutionContext execution = registration_test::makeExecution(engine);
    const an::ImageDescriptor descriptor = registration_test::descriptorOf(1u, 1u, 3u, an::SampleType::Float64);
    const an::ImageLayout layout = registration_test::layoutOf(descriptor);
    an::StarReductionRequest request;
    request.method = an::StarReductionMethod::Lightness;
    request.lightnessEncoding.transfer = an::ColorTransferFunction::Srgb;
    const an::StarReductionPlan plan = makePlan(descriptor, request);
    const std::array<double, 3> target{0.75, 0.28, 0.18};
    const std::array<double, 3> starless{0.35, 0.12, 0.08};
    std::array<double, 3> destination{};

    an::Result<an::StarReductionResult> reduced =
        an::reduceStars(execution, target.data(), layout, starless.data(), layout, destination.data(), layout, plan);
    ASSERT_TRUE(reduced.hasValue()) << reduced.status().message();
    EXPECT_NEAR(destination[0], 0.5371940323, 2.0e-8);
    EXPECT_NEAR(destination[1], 0.1948776129, 2.0e-8);
    EXPECT_NEAR(destination[2], 0.1267985081, 2.0e-8);
}

TEST(StarReductionExecutionTest, PlanarAndInterleavedLayoutsProduceTheSamePixels)
{
    an::EngineContext engine = registration_test::makeEngine();
    an::ExecutionContext execution = registration_test::makeExecution(engine);
    const an::ImageDescriptor descriptor = registration_test::descriptorOf(2u, 1u, 3u);
    const an::ImageLayout planar = registration_test::layoutOf(descriptor);
    const an::ImageLayout interleaved = registration_test::layoutOf(descriptor, an::StorageArrangement::Interleaved);
    const an::StarReductionPlan plan = makePlan(descriptor);
    const std::array<float, 6> planarTarget{0.8f, 0.6f, 0.7f, 0.5f, 0.4f, 0.3f};
    const std::array<float, 6> planarStarless{0.3f, 0.2f, 0.25f, 0.15f, 0.1f, 0.05f};
    const std::array<float, 6> interleavedTarget{0.8f, 0.7f, 0.4f, 0.6f, 0.5f, 0.3f};
    const std::array<float, 6> interleavedStarless{0.3f, 0.25f, 0.1f, 0.2f, 0.15f, 0.05f};
    std::array<float, 6> planarOut{};
    std::array<float, 6> interleavedOut{};

    ASSERT_TRUE(an::reduceStars(execution, planarTarget.data(), planar, planarStarless.data(), planar, planarOut.data(),
                                planar, plan)
                    .hasValue());
    ASSERT_TRUE(an::reduceStars(execution, interleavedTarget.data(), interleaved, interleavedStarless.data(),
                                interleaved, interleavedOut.data(), interleaved, plan)
                    .hasValue());
    EXPECT_FLOAT_EQ(planarOut[0], interleavedOut[0]);
    EXPECT_FLOAT_EQ(planarOut[2], interleavedOut[1]);
    EXPECT_FLOAT_EQ(planarOut[4], interleavedOut[2]);
    EXPECT_FLOAT_EQ(planarOut[1], interleavedOut[3]);
    EXPECT_FLOAT_EQ(planarOut[3], interleavedOut[4]);
    EXPECT_FLOAT_EQ(planarOut[5], interleavedOut[5]);
}

namespace
{

// Unit-domain samples that begin with the values a reordered or skipped step would round
// differently, followed by a deterministic spread across the whole domain.
template <typename SampleT> [[nodiscard]] std::vector<SampleT> hostileUnitSamples(std::size_t count, std::uint32_t seed)
{
    std::vector<SampleT> samples;
    if constexpr (std::is_floating_point_v<SampleT>)
    {
        samples = {SampleT{0},
                   -SampleT{0},
                   SampleT{1},
                   std::numeric_limits<SampleT>::denorm_min(),
                   (std::numeric_limits<SampleT>::min)(),
                   std::nextafter(SampleT{1}, SampleT{0}),
                   SampleT{0.5},
                   std::nextafter(SampleT{0.5}, SampleT{1}),
                   static_cast<SampleT>(1.0e-7)};
    }
    else
    {
        constexpr SampleT kMaximum = (std::numeric_limits<SampleT>::max)();
        samples = {SampleT{0}, SampleT{1}, kMaximum, static_cast<SampleT>(kMaximum - 1u),
                   static_cast<SampleT>(kMaximum / 2u)};
    }
    std::uint32_t state = seed;
    while (samples.size() < count)
    {
        state = state * 1664525u + 1013904223u;
        const double unit = static_cast<double>(state >> 8u) / static_cast<double>(1u << 24u);
        if constexpr (std::is_floating_point_v<SampleT>)
        {
            samples.push_back(static_cast<SampleT>(unit));
        }
        else
        {
            samples.push_back(static_cast<SampleT>(unit * (std::numeric_limits<SampleT>::max)()));
        }
    }
    return samples;
}

// Reduces one image through a contiguous layout, which takes the blocked run, and through a layout
// with padded rows, which takes the sample-by-sample path, and requires the same bits and counts.
template <typename SampleT> void expectContiguousRunMatchesStridedPath(an::SampleType sampleType)
{
    constexpr std::uint32_t kWidth = 37u;
    constexpr std::uint32_t kHeight = 23u;
    constexpr std::size_t kSamples = std::size_t{kWidth} * kHeight;
    an::EngineContext engine = registration_test::makeEngine();
    an::ExecutionContext execution = registration_test::makeExecution(engine);
    const an::ImageDescriptor descriptor = registration_test::descriptorOf(kWidth, kHeight, 1u, sampleType);
    const an::ImageLayout contiguous = registration_test::layoutOf(descriptor);
    const std::optional<an::ImageLayoutRequest> paddedRequest =
        an::layoutRequestFor(descriptor, an::StorageArrangement::Planar, 64u);
    ASSERT_TRUE(paddedRequest.has_value());
    const std::optional<an::ImageLayout> padded = an::ImageLayout::create(*paddedRequest, an::defaultEngineLimits());
    ASSERT_TRUE(padded.has_value());
    ASSERT_TRUE(contiguous.isContiguous());
    ASSERT_FALSE(padded->isContiguous());

    const std::vector<SampleT> target = hostileUnitSamples<SampleT>(kSamples, 17u);
    std::vector<SampleT> starless = hostileUnitSamples<SampleT>(kSamples, 29u);
    std::reverse(starless.begin(), starless.end());
    const std::size_t paddedRow = static_cast<std::size_t>(padded->rowStrideBytes() / sizeof(SampleT));
    std::vector<SampleT> paddedTarget(static_cast<std::size_t>(padded->totalBytes() / sizeof(SampleT)));
    std::vector<SampleT> paddedStarless(paddedTarget.size());
    for (std::size_t y = 0u; y < kHeight; ++y)
    {
        std::copy_n(target.begin() + static_cast<std::ptrdiff_t>(y * kWidth), kWidth,
                    paddedTarget.begin() + static_cast<std::ptrdiff_t>(y * paddedRow));
        std::copy_n(starless.begin() + static_cast<std::ptrdiff_t>(y * kWidth), kWidth,
                    paddedStarless.begin() + static_cast<std::ptrdiff_t>(y * paddedRow));
    }

    for (const an::StarReductionMethod method : {an::StarReductionMethod::Transfer, an::StarReductionMethod::Halo,
                                                 an::StarReductionMethod::Star, an::StarReductionMethod::Lightness})
    {
        for (const an::StarReductionStrength strength :
             {an::StarReductionStrength::Strong, an::StarReductionStrength::Moderate, an::StarReductionStrength::Soft})
        {
            for (std::uint32_t iterations = 1u; iterations <= 3u; ++iterations)
            {
                an::StarReductionRequest request;
                request.method = method;
                request.strength = strength;
                request.iterations = iterations;
                request.stretchFactor = 0.35;
                const an::StarReductionPlan plan = makePlan(descriptor, request);
                std::vector<SampleT> flat(kSamples);
                std::vector<SampleT> strided(paddedTarget.size());
                const an::Result<an::StarReductionResult> flatResult = an::reduceStars(
                    execution, target.data(), contiguous, starless.data(), contiguous, flat.data(), contiguous, plan);
                const an::Result<an::StarReductionResult> stridedResult =
                    an::reduceStars(execution, paddedTarget.data(), *padded, paddedStarless.data(), *padded,
                                    strided.data(), *padded, plan);
                ASSERT_TRUE(flatResult.hasValue()) << flatResult.status().message();
                ASSERT_TRUE(stridedResult.hasValue()) << stridedResult.status().message();
                EXPECT_EQ(flatResult->samplesWritten, kSamples);
                EXPECT_EQ(flatResult->clampedOutputs, stridedResult->clampedOutputs)
                    << an::starReductionMethodName(method) << ' ' << an::starReductionStrengthName(strength) << ' '
                    << iterations;
                std::size_t differing = 0u;
                std::size_t firstDifference = kSamples;
                for (std::size_t y = 0u; y < kHeight; ++y)
                {
                    for (std::size_t x = 0u; x < kWidth; ++x)
                    {
                        const SampleT expected = strided[y * paddedRow + x];
                        const SampleT actual = flat[y * kWidth + x];
                        if (std::memcmp(&expected, &actual, sizeof(SampleT)) != 0)
                        {
                            firstDifference = (std::min)(firstDifference, y * kWidth + x);
                            ++differing;
                        }
                    }
                }
                EXPECT_EQ(differing, 0u) << an::starReductionMethodName(method) << ' '
                                         << an::starReductionStrengthName(strength) << ' ' << iterations
                                         << ": first at sample " << firstDifference;
            }
        }
    }
}

} // namespace

TEST(StarReductionExecutionTest, AContiguousRunReducesEverySampleToTheBitsOfTheStridedPath)
{
    expectContiguousRunMatchesStridedPath<float>(an::SampleType::Float32);
    expectContiguousRunMatchesStridedPath<double>(an::SampleType::Float64);
    expectContiguousRunMatchesStridedPath<std::uint16_t>(an::SampleType::UInt16);
}

TEST(StarReductionExecutionTest, RejectsAliasingIncompleteValidityAndInvalidFloatingSamples)
{
    an::EngineContext engine = registration_test::makeEngine();
    an::ExecutionContext execution = registration_test::makeExecution(engine);
    const an::ImageDescriptor descriptor = registration_test::descriptorOf(2u, 1u);
    const an::ImageLayout layout = registration_test::layoutOf(descriptor);
    const an::StarReductionPlan plan = makePlan(descriptor);
    std::array<float, 2> target{0.8f, 0.6f};
    std::array<float, 2> starless{0.3f, 0.2f};

    an::Result<an::StarReductionResult> aliased =
        an::reduceStars(execution, target.data(), layout, starless.data(), layout, target.data(), layout, plan);
    ASSERT_FALSE(aliased.hasValue());
    EXPECT_EQ(aliased.status().code(), an::ErrorCode::Aliasing);

    const std::optional<an::ValidityDescriptor> finiteOnly =
        an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly});
    ASSERT_TRUE(finiteOnly.has_value());
    std::array<float, 2> output{};
    an::Result<an::StarReductionResult> incomplete =
        an::reduceStars(execution, target.data(), layout, starless.data(), layout, output.data(), layout, plan,
                        *finiteOnly, an::ValidityDescriptor::allValid());
    ASSERT_FALSE(incomplete.hasValue());
    EXPECT_EQ(incomplete.status().code(), an::ErrorCode::IncompatibleArguments);

    target[1] = std::numeric_limits<float>::quiet_NaN();
    an::Result<an::StarReductionResult> nonFinite =
        an::reduceStars(execution, target.data(), layout, starless.data(), layout, output.data(), layout, plan);
    ASSERT_FALSE(nonFinite.hasValue());
    EXPECT_EQ(nonFinite.status().code(), an::ErrorCode::InvalidData);
}

TEST(StarReductionExecutionTest, ExtractsResidualWithAndWithoutReverseStretch)
{
    an::EngineContext engine = registration_test::makeEngine();
    an::ExecutionContext execution = registration_test::makeExecution(engine);
    const an::ImageDescriptor descriptor = registration_test::descriptorOf(1u, 1u, 1u, an::SampleType::Float64);
    const an::ImageLayout layout = registration_test::layoutOf(descriptor);
    const an::StarReductionPlan plan = makePlan(descriptor);
    const double reduced = 0.62;
    const double starless = 0.25;
    double destination = 0.0;

    an::Result<an::StarReductionResult> normal =
        an::extractReducedStars(execution, &reduced, layout, &starless, layout, &destination, layout, plan, {});
    ASSERT_TRUE(normal.hasValue()) << normal.status().message();
    EXPECT_NEAR(destination, (0.62 - 0.25) / (1.0 - 0.25), 1.0e-13);

    an::ReducedStarExtractionRequest request;
    request.reverseStretch = true;
    request.stretchFactor = 0.2;
    ASSERT_TRUE(
        an::extractReducedStars(execution, &reduced, layout, &starless, layout, &destination, layout, plan, request)
            .hasValue());
    const double expected = mtf(0.8, (mtf(0.2, 0.62) - mtf(0.2, 0.25)) / (1.0 - mtf(0.2, 0.25)));
    EXPECT_NEAR(destination, expected, 1.0e-13);
}

TEST(StarReductionExecutionTest, BuildsAContextBudgetedDeterministicProtectionMask)
{
    an::EngineContext engine = registration_test::makeEngine();
    an::ExecutionContext execution = registration_test::makeExecution(engine);
    const an::ImageDescriptor descriptor = registration_test::descriptorOf(9u, 9u);
    const an::ImageLayout layout = registration_test::layoutOf(descriptor);
    const an::StarReductionPlan plan = makePlan(descriptor);
    const an::ImageLayout maskLayout = registration_test::layoutOf(plan.maskDescriptor());
    std::array<float, 81> target{};
    std::array<float, 81> starless{};
    target.fill(0.2f);
    starless.fill(0.2f);
    target[40] = 0.95f;
    starless[40] = 0.35f;
    std::array<float, 81> first{};
    std::array<float, 81> second{};

    an::Result<an::StarReductionResult> made = an::buildSmallStarProtectionMask(
        execution, target.data(), layout, starless.data(), layout, first.data(), maskLayout, plan);
    ASSERT_TRUE(made.hasValue()) << made.status().message();
    ASSERT_TRUE(an::buildSmallStarProtectionMask(execution, target.data(), layout, starless.data(), layout,
                                                 second.data(), maskLayout, plan)
                    .hasValue());
    EXPECT_GT(made->workingBytes, 0u);
    EXPECT_EQ(first, second);
    for (float value : first)
    {
        EXPECT_TRUE(std::isfinite(value));
        EXPECT_GE(value, 0.0f);
        EXPECT_LE(value, 1.0f);
    }
}

TEST(StarReductionMetadataTest, PlansNonlinearOutputsAndBuildsExplicitTransitions)
{
    const an::ImageDescriptor linear =
        registration_test::descriptorOf(5u, 4u, 1u, an::SampleType::Float32, an::NumericDomain::NormalizedLinear);
    const an::StarReductionPlan plan = makePlan(linear);
    EXPECT_EQ(plan.outputDescriptor().numericDomain(), an::NumericDomain::EncodedNonlinear);
    EXPECT_EQ(plan.maskDescriptor().channelSchema(), an::ChannelSchema::Mono);
    EXPECT_EQ(plan.maskDescriptor().numericDomain(), an::NumericDomain::EncodedNonlinear);

    an::ImageSemanticsRequest semanticsRequest;
    semanticsRequest.transferFunction = an::TransferFunction::Linear;
    semanticsRequest.processingState.flatFielded = true;
    an::Result<an::ImageSemantics> semantics = an::ImageSemantics::create(semanticsRequest);
    ASSERT_TRUE(semantics.hasValue());
    an::Result<an::MetadataTransition> transition = an::starReductionTransition(plan, *semantics);
    ASSERT_TRUE(transition.hasValue()) << transition.status().message();
    ASSERT_TRUE(transition->semantics.has_value());
    EXPECT_EQ(transition->semantics->transferFunction(), an::TransferFunction::Unspecified);
    EXPECT_TRUE(transition->semantics->processingState().flatFielded);
    EXPECT_FALSE(transition->semantics->processingState().registered);
    ASSERT_TRUE(transition->record.has_value());

    an::Result<an::MetadataTransition> maskTransition = an::smallStarProtectionMaskTransition();
    ASSERT_TRUE(maskTransition.hasValue()) << maskTransition.status().message();
    ASSERT_TRUE(maskTransition->semantics.has_value());
    EXPECT_EQ(maskTransition->semantics->processingState(), an::ProcessingState{});
    EXPECT_TRUE(maskTransition->astrometry.has_value());
}
