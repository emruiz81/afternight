// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the total-variation denoiser.
//
// The denoiser is the engine's own rather than a call into a Python interpreter, so the first thing
// worth proving is the one a library call cannot promise: two runs of the same request give the same
// frame, on any number of threads, and the run says what it spent. The rest is what the two methods
// are: both minimize the same functional, so at convergence they have to agree with each other, and
// each has to leave a flat frame alone and an edge standing.

#include <AfterNightEngine/Algorithms/NoiseReduction/TotalVariationDenoise.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>

#include "SpatialFilterFixture.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace
{

using spatial_test::makeEngine;
using spatial_test::makeExecution;
using spatial_test::planarLayout;
using spatial_test::randomPlane;

constexpr std::uint32_t kWidth = 37u;
constexpr std::uint32_t kHeight = 29u;

/// A frame with a hard vertical edge and reproducible noise on both sides of it.
///
/// @param width how many pixels one row holds.
/// @param height how many rows.
/// @param amplitude how far a sample may be moved from its side's level.
/// @return the samples.
[[nodiscard]] std::vector<float> noisyEdge(const std::uint32_t width, const std::uint32_t height, const float amplitude)
{
    std::vector<float> samples(static_cast<std::size_t>(width) * height, 0.0f);
    const std::vector<float> noise = randomPlane(width, height, 20260920u);
    for (std::uint32_t y = 0u; y < height; ++y)
    {
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            const std::size_t index = static_cast<std::size_t>(y) * width + x;
            const float level = x < width / 2u ? 0.25f : 0.75f;
            samples[index] = level + amplitude * (noise[index] - 0.5f);
        }
    }
    return samples;
}

/// Runs one denoise over a single-channel Float32 plane.
///
/// @param execution the operation to run under.
/// @param request what to run.
/// @param source the plane to read.
/// @param destination where the answer goes, resized to the source.
/// @return what the operation reported.
[[nodiscard]] an::Result<an::TotalVariationResult> run(const an::ExecutionContext& execution,
                                                       const an::TotalVariationRequest& request,
                                                       const std::vector<float>& source,
                                                       std::vector<float>& destination)
{
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight, 1u);
    destination.assign(source.size(), 0.0f);
    const an::Result<an::TotalVariationPlan> plan = an::planTotalVariationDenoise(request);
    EXPECT_TRUE(plan.hasValue()) << plan.status().message();
    if (!plan.hasValue())
    {
        return plan.status();
    }
    return an::denoiseImageTotalVariation(execution, an::SampleType::Float32, source.data(), layout, destination.data(),
                                          layout, *plan);
}

/// The total variation of a plane, as the sum of its forward gradient magnitudes.
///
/// @param samples the plane.
/// @return the total variation.
[[nodiscard]] double totalVariation(const std::vector<float>& samples)
{
    double sum = 0.0;
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            const std::size_t index = static_cast<std::size_t>(y) * kWidth + x;
            const double down = y + 1u == kHeight ? 0.0 : samples[index + kWidth] - samples[index];
            const double right = x + 1u == kWidth ? 0.0 : samples[index + 1u] - samples[index];
            sum += std::sqrt(down * down + right * right);
        }
    }
    return sum;
}

/// The Rudin-Osher-Fatemi functional both methods minimize.
///
/// @param answer the denoised plane.
/// @param source the plane it was denoised from.
/// @param fidelity the weight of the data term, in the split's convention.
/// @return the total variation plus the weighted squared distance from the source.
[[nodiscard]] double rofEnergy(const std::vector<float>& answer, const std::vector<float>& source,
                               const double fidelity)
{
    double distance = 0.0;
    for (std::size_t index = 0u; index < answer.size(); ++index)
    {
        const double difference = static_cast<double>(answer[index]) - source[index];
        distance += difference * difference;
    }
    return totalVariation(answer) + 0.5 * fidelity * distance;
}

TEST(TotalVariationDenoiseTest, EitherMethodLowersTheTotalVariationAndKeepsTheEdge)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<float> source = noisyEdge(kWidth, kHeight, 0.2f);
    const double before = totalVariation(source);

    for (const an::TotalVariationMethod method :
         {an::TotalVariationMethod::Chambolle, an::TotalVariationMethod::SplitBregman})
    {
        an::TotalVariationRequest request;
        request.method = method;
        request.weight = method == an::TotalVariationMethod::Chambolle ? 0.1 : 8.0;
        request.maximumIterations = 200u;
        request.tolerance = 0.0;
        std::vector<float> destination;
        const an::Result<an::TotalVariationResult> result = run(execution, request, source, destination);
        ASSERT_TRUE(result.hasValue()) << an::totalVariationMethodName(method) << ": " << result.status().message();
        const std::string where = an::totalVariationMethodName(method);

        EXPECT_LT(totalVariation(destination), before) << where;
        // The edge is between the two halves, and the levels on either side of it survive.
        const double left = destination[static_cast<std::size_t>(kHeight / 2u) * kWidth + 4u];
        const double right = destination[static_cast<std::size_t>(kHeight / 2u) * kWidth + kWidth - 5u];
        EXPECT_NEAR(left, 0.25, 0.03) << where;
        EXPECT_NEAR(right, 0.75, 0.03) << where;
        EXPECT_EQ(result->iterationsRun, 200u) << where;
        EXPECT_FALSE(result->converged) << where;
    }
}

TEST(TotalVariationDenoiseTest, TheTwoMethodsAgreeAtConvergenceBecauseTheyMinimizeTheSameFunctional)
{
    // The one property that is not either method's own: Chambolle's weight is the denoising strength
    // and the split's is the fidelity, and the functional they minimize is the same one when the two
    // are reciprocal. Run both far past their own stopping rules and the answers have to meet.
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<float> source = noisyEdge(kWidth, kHeight, 0.15f);

    an::TotalVariationRequest chambolle;
    chambolle.method = an::TotalVariationMethod::Chambolle;
    chambolle.weight = 0.05;
    chambolle.maximumIterations = 8000u;
    chambolle.tolerance = 0.0;
    std::vector<float> dual;
    const an::Result<an::TotalVariationResult> first = run(execution, chambolle, source, dual);
    ASSERT_TRUE(first.hasValue()) << first.status().message();

    an::TotalVariationRequest bregman;
    bregman.method = an::TotalVariationMethod::SplitBregman;
    bregman.weight = 1.0 / chambolle.weight;
    bregman.maximumIterations = 8000u;
    bregman.tolerance = 0.0;
    std::vector<float> split;
    const an::Result<an::TotalVariationResult> second = run(execution, bregman, source, split);
    ASSERT_TRUE(second.hasValue()) << second.status().message();

    double largest = 0.0;
    for (std::size_t index = 0u; index < dual.size(); ++index)
    {
        largest = (std::max)(largest, std::abs(static_cast<double>(dual[index]) - split[index]));
    }
    // The functional itself is the principled comparison: the minimizer is unique, so two methods
    // that reach it have to report the same energy. The per-sample bound below is looser on purpose
    // - it is the distance each iteration still is from that minimum after a finite number of
    // steps, which at two thousand sweeps was 0.022 and falls with the sweep count.
    const double dualEnergy = rofEnergy(dual, source, 1.0 / chambolle.weight);
    const double splitEnergy = rofEnergy(split, source, bregman.weight);
    EXPECT_NEAR(dualEnergy, splitEnergy, 0.01 * (std::max)(dualEnergy, splitEnergy))
        << "dual " << dualEnergy << ", split " << splitEnergy;
    EXPECT_LT(largest, 0.02) << "largest disagreement " << largest;
}

TEST(TotalVariationDenoiseTest, TheSameRequestGivesTheSameFrameAtEveryThreadCount)
{
    const an::EngineContext engine = makeEngine(8u);
    const std::vector<float> source = noisyEdge(kWidth, kHeight, 0.2f);
    for (const an::TotalVariationMethod method :
         {an::TotalVariationMethod::Chambolle, an::TotalVariationMethod::SplitBregman})
    {
        an::TotalVariationRequest request;
        request.method = method;
        request.weight = method == an::TotalVariationMethod::Chambolle ? 0.1 : 8.0;
        request.maximumIterations = 40u;
        request.tolerance = 0.0;
        std::vector<float> one;
        std::vector<float> many;
        const an::ExecutionContext serial = makeExecution(engine, 1u);
        const an::ExecutionContext parallel = makeExecution(engine, 8u);
        ASSERT_TRUE(run(serial, request, source, one).hasValue());
        ASSERT_TRUE(run(parallel, request, source, many).hasValue());
        EXPECT_EQ(one, many) << an::totalVariationMethodName(method);
    }
}

TEST(TotalVariationDenoiseTest, AToleranceStopsEarlyAndSaysSo)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<float> source = noisyEdge(kWidth, kHeight, 0.05f);

    an::TotalVariationRequest request;
    request.method = an::TotalVariationMethod::Chambolle;
    request.weight = 0.05;
    request.maximumIterations = 500u;
    request.tolerance = 1.0e-2;
    std::vector<float> destination;
    const an::Result<an::TotalVariationResult> result = run(execution, request, source, destination);
    ASSERT_TRUE(result.hasValue()) << result.status().message();
    EXPECT_TRUE(result->converged);
    EXPECT_LT(result->iterationsRun, request.maximumIterations);
    EXPECT_GT(result->iterationsRun, 1u);
}

TEST(TotalVariationDenoiseTest, RejectedNonFiniteSamplesDoNotEnterEitherSolveOrChangeTheDestination)
{
    const auto engine = makeEngine();
    const auto execution = makeExecution(engine);
    for (const auto method : {an::TotalVariationMethod::Chambolle, an::TotalVariationMethod::SplitBregman})
    {
        an::TotalVariationRequest request;
        request.method = method;
        request.maximumIterations = 8u;
        request.tolerance = 0.0;
        const auto plan = an::planTotalVariationDenoise(request);
        ASSERT_TRUE(plan.hasValue());
        for (const bool interleaved : {false, true})
        {
            const auto layout = interleaved ? spatial_test::interleavedLayout(an::SampleType::Float32, 5u, 5u, 2u)
                                            : planarLayout(an::SampleType::Float32, 5u, 5u, 2u);
            for (const float rejected :
                 {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                  -std::numeric_limits<float>::infinity()})
            {
                std::vector<float> source(50u, 0.25f);
                source[12u] = rejected;
                std::vector<float> substitute = source;
                substitute[12u] = 0.0f;
                std::vector<float> expected(50u, -9.0f);
                std::vector<float> actual(50u, -9.0f);
                ASSERT_TRUE(an::denoiseImageTotalVariation(execution, an::SampleType::Float32, substitute.data(),
                                                           layout, expected.data(), layout, *plan)
                                .hasValue());
                an::SpatialSelection selection;
                selection.validity = *an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly});
                const auto result = an::denoiseImageTotalVariation(execution, an::SampleType::Float32, source.data(),
                                                                   layout, actual.data(), layout, *plan, selection);
                ASSERT_TRUE(result.hasValue()) << result.status().message();
                EXPECT_EQ(result->noise.filter.run.outputsWritten, 49u);
                EXPECT_EQ(result->noise.filter.run.selection.valid, 49u);
                EXPECT_FLOAT_EQ(actual[12u], -9.0f);
                for (std::size_t i = 0u; i < actual.size(); ++i)
                {
                    if (i == 12u)
                        continue;
                    EXPECT_TRUE(std::isfinite(actual[i]));
                    EXPECT_FLOAT_EQ(actual[i], expected[i]);
                }
            }
        }
    }
}

TEST(TotalVariationDenoiseTest, ASampleTheValidityRuleRejectsKeepsItsDestinationValue)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    std::vector<float> source = noisyEdge(kWidth, kHeight, 0.2f);
    const std::size_t hole = static_cast<std::size_t>(kHeight / 3u) * kWidth + 7u;
    source[hole] = 0.0f;

    an::TotalVariationRequest request;
    request.method = an::TotalVariationMethod::Chambolle;
    request.weight = 0.1;
    request.maximumIterations = 50u;
    request.tolerance = 0.0;

    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight, 1u);
    std::vector<float> destination(source.size(), -1.0f);
    const an::Result<an::TotalVariationPlan> plan = an::planTotalVariationDenoise(request);
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();
    an::SpatialSelection selection;
    selection.validity = *an::ValidityDescriptor::create({.rule = an::ValidityRule::ZeroIsNoData});
    const an::Result<an::TotalVariationResult> result = an::denoiseImageTotalVariation(
        execution, an::SampleType::Float32, source.data(), layout, destination.data(), layout, *plan, selection);
    ASSERT_TRUE(result.hasValue()) << result.status().message();
    EXPECT_FLOAT_EQ(destination[hole], -1.0f);
}

TEST(TotalVariationDenoiseTest, EveryMalformedRequestIsRefusedByName)
{
    const auto refused = [](const an::TotalVariationRequest& request) {
        const an::Result<an::TotalVariationPlan> plan = an::planTotalVariationDenoise(request);
        EXPECT_FALSE(plan.hasValue());
        return plan.hasValue() ? an::ErrorCode::Ok : plan.status().code();
    };

    an::TotalVariationRequest request;
    request.method = static_cast<an::TotalVariationMethod>(7u);
    EXPECT_EQ(refused(request), an::ErrorCode::InvalidArgument);

    request = {};
    request.weight = 0.0;
    EXPECT_EQ(refused(request), an::ErrorCode::InvalidArgument);

    request = {};
    request.tolerance = -1.0;
    EXPECT_EQ(refused(request), an::ErrorCode::InvalidArgument);

    request = {};
    request.maximumIterations = 0u;
    EXPECT_EQ(refused(request), an::ErrorCode::InvalidArgument);

    request = {};
    request.maximumIterations = an::kMaxTotalVariationIterations + 1u;
    EXPECT_EQ(refused(request), an::ErrorCode::OutOfRange);

    request = {};
    request.output.writeBack.domain = an::DomainMapping::FullScale;
    EXPECT_EQ(refused(request), an::ErrorCode::InvalidArgument);
}

TEST(TotalVariationDenoiseTest, APlanReportsWhatItWillSpend)
{
    an::TotalVariationRequest request;
    request.maximumIterations = 123u;
    const an::Result<an::TotalVariationPlan> plan = an::planTotalVariationDenoise(request);
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();
    EXPECT_EQ(plan->imagePasses(), 123u);
    EXPECT_EQ(plan->workingPlanesPerChannel(), 6u);
    EXPECT_EQ(plan->request().maximumIterations, 123u);
    EXPECT_STREQ(an::totalVariationMethodName(an::TotalVariationMethod::Chambolle), "chambolle");
    EXPECT_STREQ(an::totalVariationMethodName(an::TotalVariationMethod::SplitBregman), "split-bregman");
    EXPECT_STREQ(an::totalVariationMethodName(static_cast<an::TotalVariationMethod>(9u)), "unknown");
}

} // namespace
