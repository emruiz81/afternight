// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the noise this engine puts into an image on purpose.
//
// The property worth more than the distributions is that the frame is reproducible: a seed and a
// coordinate decide a sample, so the same request gives the same frame at any thread count. That is
// what a library call through an interpreter cannot promise, and it is what a person comparing two
// denoisers on the same synthetic frame depends on.

#include <AfterNightEngine/Algorithms/Synthesis/NoiseGeneration.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>

#include "SpatialFilterFixture.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace
{

using spatial_test::makeEngine;
using spatial_test::makeExecution;
using spatial_test::planarLayout;

constexpr std::uint32_t kWidth = 128u;
constexpr std::uint32_t kHeight = 96u;

/// A flat single-channel plane.
///
/// @param value what every sample holds.
/// @return the samples.
[[nodiscard]] std::vector<float> flatPlane(const float value)
{
    return std::vector<float>(static_cast<std::size_t>(kWidth) * kHeight, value);
}

/// Runs one generation over a single-channel Float32 plane.
///
/// @param execution the operation to run under.
/// @param request what to run.
/// @param source the plane to read.
/// @param destination where the answer goes, resized to the source.
/// @return what the operation reported.
[[nodiscard]] an::Result<an::NoiseGenerationResult> run(const an::ExecutionContext& execution,
                                                        const an::NoiseGenerationRequest& request,
                                                        const std::vector<float>& source,
                                                        std::vector<float>& destination)
{
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight, 1u);
    destination.assign(source.size(), 0.0f);
    const an::Result<an::NoiseGenerationPlan> plan = an::planNoiseGeneration(request);
    EXPECT_TRUE(plan.hasValue()) << plan.status().message();
    if (!plan.hasValue())
    {
        return plan.status();
    }
    return an::generateImageNoise(execution, an::SampleType::Float32, source.data(), layout, destination.data(), layout,
                                  *plan);
}

/// The mean and the variance of a plane.
///
/// @param samples the plane.
/// @return the mean and the variance, in that order.
[[nodiscard]] std::pair<double, double> moments(const std::vector<float>& samples)
{
    double sum = 0.0;
    for (const float sample : samples)
    {
        sum += sample;
    }
    const double mean = sum / static_cast<double>(samples.size());
    double squared = 0.0;
    for (const float sample : samples)
    {
        const double centred = static_cast<double>(sample) - mean;
        squared += centred * centred;
    }
    return {mean, squared / static_cast<double>(samples.size())};
}

TEST(NoiseGenerationTest, TheSameSeedGivesTheSameFrameAtEveryThreadCount)
{
    const an::EngineContext engine = makeEngine(8u);
    const std::vector<float> source = flatPlane(0.5f);
    for (const an::NoiseKind kind : {an::NoiseKind::Gaussian, an::NoiseKind::Poisson, an::NoiseKind::Salt,
                                     an::NoiseKind::Pepper, an::NoiseKind::SaltAndPepper, an::NoiseKind::Speckle})
    {
        an::NoiseGenerationRequest request;
        request.kind = kind;
        request.seed = 0x5EEDu;
        std::vector<float> one;
        std::vector<float> many;
        ASSERT_TRUE(run(makeExecution(engine, 1u), request, source, one).hasValue()) << an::noiseKindName(kind);
        ASSERT_TRUE(run(makeExecution(engine, 8u), request, source, many).hasValue()) << an::noiseKindName(kind);
        EXPECT_EQ(one, many) << an::noiseKindName(kind);
    }
}

TEST(NoiseGenerationTest, TwoSeedsDrawUnrelatedNoise)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<float> source = flatPlane(0.5f);
    an::NoiseGenerationRequest request;
    request.kind = an::NoiseKind::Gaussian;
    request.seed = 1u;
    std::vector<float> first;
    ASSERT_TRUE(run(execution, request, source, first).hasValue());
    request.seed = 2u;
    std::vector<float> second;
    ASSERT_TRUE(run(execution, request, source, second).hasValue());
    EXPECT_NE(first, second);
}

TEST(NoiseGenerationTest, AGaussianDrawHasTheMeanAndVarianceItWasAskedFor)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<float> source = flatPlane(0.5f);
    an::NoiseGenerationRequest request;
    request.kind = an::NoiseKind::Gaussian;
    request.mean = 0.0;
    request.variance = 0.0025;
    request.clip = false;
    request.seed = 20260920u;
    std::vector<float> destination;
    const an::Result<an::NoiseGenerationResult> result = run(execution, request, source, destination);
    ASSERT_TRUE(result.hasValue()) << result.status().message();

    const auto [mean, variance] = moments(destination);
    // Twelve thousand samples: the standard error of the mean is one part in a hundred of the
    // standard deviation, and of the variance about one part in eighty.
    EXPECT_NEAR(mean, 0.5, 0.005);
    EXPECT_NEAR(variance, request.variance, request.variance * 0.1);
    EXPECT_EQ(result->samplesClipped, 0u);
}

TEST(NoiseGenerationTest, APoissonDrawHasTheVarianceItsOwnRateImplies)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    an::NoiseGenerationRequest request;
    request.kind = an::NoiseKind::Poisson;
    request.clip = false;
    request.seed = 4242u;
    // Both sides of the method's own threshold: a rate of four takes the multiplication method and
    // one of five hundred takes the transformed rejection, and each has to have the variance of its
    // own rate, which for a Poisson count is the rate itself.
    for (const double rate : {4.0, 500.0})
    {
        request.poissonScale = rate;
        const std::vector<float> source = flatPlane(1.0f);
        std::vector<float> destination;
        ASSERT_TRUE(run(execution, request, source, destination).hasValue()) << rate;
        const auto [mean, variance] = moments(destination);
        EXPECT_NEAR(mean, 1.0, 0.05) << rate;
        EXPECT_NEAR(variance, 1.0 / rate, (1.0 / rate) * 0.15) << rate;
    }
}

TEST(NoiseGenerationTest, TheReplacementKindsReplaceTheFractionTheyWereAskedFor)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<float> source = flatPlane(0.5f);
    const auto counted = [&](const an::NoiseKind kind, const double amount, const double saltFraction) {
        an::NoiseGenerationRequest request;
        request.kind = kind;
        request.amount = amount;
        request.saltFraction = saltFraction;
        request.seed = 77u;
        std::vector<float> destination;
        EXPECT_TRUE(run(execution, request, source, destination).hasValue());
        std::uint64_t salted = 0u;
        std::uint64_t peppered = 0u;
        for (const float sample : destination)
        {
            salted += sample == 1.0f ? 1u : 0u;
            peppered += sample == 0.0f ? 1u : 0u;
        }
        return std::pair<std::uint64_t, std::uint64_t>{salted, peppered};
    };

    const auto total = static_cast<double>(kWidth) * kHeight;
    const auto [salted, noPepper] = counted(an::NoiseKind::Salt, 0.1, 0.5);
    EXPECT_NEAR(static_cast<double>(salted) / total, 0.1, 0.01);
    EXPECT_EQ(noPepper, 0u);

    const auto [noSalt, peppered] = counted(an::NoiseKind::Pepper, 0.1, 0.5);
    EXPECT_EQ(noSalt, 0u);
    EXPECT_NEAR(static_cast<double>(peppered) / total, 0.1, 0.01);

    const auto [bothSalt, bothPepper] = counted(an::NoiseKind::SaltAndPepper, 0.2, 0.25);
    EXPECT_NEAR(static_cast<double>(bothSalt + bothPepper) / total, 0.2, 0.02);
    EXPECT_NEAR(static_cast<double>(bothSalt) / static_cast<double>(bothSalt + bothPepper), 0.25, 0.05);
}

TEST(NoiseGenerationTest, AClipHoldsTheResultAndCountsWhatItHeld)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<float> source = flatPlane(0.98f);
    an::NoiseGenerationRequest request;
    request.kind = an::NoiseKind::Gaussian;
    request.variance = 0.04;
    request.clip = true;
    request.seed = 11u;
    std::vector<float> destination;
    const an::Result<an::NoiseGenerationResult> result = run(execution, request, source, destination);
    ASSERT_TRUE(result.hasValue()) << result.status().message();
    EXPECT_GT(result->samplesClipped, 0u);
    EXPECT_TRUE(std::all_of(destination.begin(), destination.end(),
                            [](const float sample) { return sample >= 0.0f && sample <= 1.0f; }));
}

TEST(NoiseGenerationTest, ASampleTheValidityRuleRejectsKeepsTheValueItHad)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    std::vector<float> source = flatPlane(0.5f);
    source[17] = 0.0f;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight, 1u);
    std::vector<float> destination(source.size(), -1.0f);

    an::NoiseGenerationRequest request;
    request.kind = an::NoiseKind::Salt;
    request.amount = 1.0;
    request.seed = 5u;
    const an::Result<an::NoiseGenerationPlan> plan = an::planNoiseGeneration(request);
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();
    an::SpatialSelection selection;
    selection.validity = *an::ValidityDescriptor::create({.rule = an::ValidityRule::ZeroIsNoData});
    const an::Result<an::NoiseGenerationResult> result = an::generateImageNoise(
        execution, an::SampleType::Float32, source.data(), layout, destination.data(), layout, *plan, selection);
    ASSERT_TRUE(result.hasValue()) << result.status().message();
    EXPECT_FLOAT_EQ(destination[17], 0.0f);
    EXPECT_FLOAT_EQ(destination[18], 1.0f);
}

TEST(NoiseGenerationTest, EveryMalformedRequestIsRefusedByName)
{
    const auto refused = [](const an::NoiseGenerationRequest& request) {
        const an::Result<an::NoiseGenerationPlan> plan = an::planNoiseGeneration(request);
        EXPECT_FALSE(plan.hasValue());
        return plan.hasValue() ? an::ErrorCode::Ok : plan.status().code();
    };

    an::NoiseGenerationRequest request;
    request.kind = static_cast<an::NoiseKind>(9u);
    EXPECT_EQ(refused(request), an::ErrorCode::InvalidArgument);

    request = {};
    request.variance = -1.0;
    EXPECT_EQ(refused(request), an::ErrorCode::InvalidArgument);

    request = {};
    request.amount = 1.5;
    EXPECT_EQ(refused(request), an::ErrorCode::InvalidArgument);

    request = {};
    request.saltFraction = -0.5;
    EXPECT_EQ(refused(request), an::ErrorCode::InvalidArgument);

    request = {};
    request.lowValue = 1.0;
    request.highValue = 1.0;
    EXPECT_EQ(refused(request), an::ErrorCode::InvalidArgument);

    request = {};
    request.poissonScale = 0.0;
    EXPECT_EQ(refused(request), an::ErrorCode::InvalidArgument);

    request = {};
    request.kind = an::NoiseKind::Poisson;
    request.poissonScale = an::kMaxPoissonRate * 2.0;
    EXPECT_EQ(refused(request), an::ErrorCode::OutOfRange);

    request = {};
    request.output.writeBack.domain = an::DomainMapping::FullScale;
    EXPECT_EQ(refused(request), an::ErrorCode::InvalidArgument);
}

TEST(NoiseGenerationTest, APlanReportsTheDrawsEachKindTakes)
{
    const auto draws = [](const an::NoiseKind kind) {
        an::NoiseGenerationRequest request;
        request.kind = kind;
        const an::Result<an::NoiseGenerationPlan> plan = an::planNoiseGeneration(request);
        EXPECT_TRUE(plan.hasValue()) << plan.status().message();
        return plan.hasValue() ? plan->drawsPerSample() : 0u;
    };
    EXPECT_EQ(draws(an::NoiseKind::Gaussian), 2u);
    EXPECT_EQ(draws(an::NoiseKind::Speckle), 2u);
    EXPECT_EQ(draws(an::NoiseKind::SaltAndPepper), 2u);
    EXPECT_EQ(draws(an::NoiseKind::Poisson), 1u);
    EXPECT_EQ(draws(an::NoiseKind::Salt), 1u);
    EXPECT_EQ(draws(an::NoiseKind::Pepper), 1u);

    EXPECT_STREQ(an::noiseKindName(an::NoiseKind::SaltAndPepper), "salt-and-pepper");
    EXPECT_STREQ(an::noiseKindName(static_cast<an::NoiseKind>(9u)), "unknown");
    EXPECT_TRUE(an::isNoiseKind(an::NoiseKind::Speckle));
    EXPECT_FALSE(an::isNoiseKind(static_cast<an::NoiseKind>(6u)));
}

} // namespace
