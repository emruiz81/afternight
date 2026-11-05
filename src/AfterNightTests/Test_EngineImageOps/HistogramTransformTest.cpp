// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the approximate histogram transform.

#include <AfterNightEngine/Algorithms/Statistics/HistogramTransform.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <numeric>
#include <stop_token>
#include <utility>

namespace
{

constexpr std::array<an::CurveControlPoint, 2u> kIdentity{
    an::CurveControlPoint{.input = 0.0, .output = 0.0},
    an::CurveControlPoint{.input = 1.0, .output = 1.0},
};

[[nodiscard]] an::ImageHistogram histogram(std::span<const std::uint64_t> bins, double low = 0.0, double high = 1.0)
{
    an::ImageHistogram result;
    result.counted = std::accumulate(bins.begin(), bins.end(), std::uint64_t{0});
    result.lowest = low;
    result.highest = high;
    result.binCount = bins.size();
    return result;
}

[[nodiscard]] an::EngineContext histogramTransformEngine()
{
    an::EngineContextOptions options;
    options.maxConcurrency = 2u;
    an::Result<an::EngineContext> made = an::EngineContext::create(options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::EngineContext{};
}

[[nodiscard]] an::ExecutionContext histogramTransformExecution(const an::EngineContext& engine,
                                                               std::stop_token stopToken = {})
{
    an::ExecutionOptions options;
    options.maxConcurrency = 1u;
    options.stopToken = stopToken;
    an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
}

} // namespace

TEST(HistogramTransformTest, IdentityNearestMappingConservesEveryCount)
{
    an::EngineContext engine = histogramTransformEngine();
    an::ExecutionContext execution = histogramTransformExecution(engine);
    const std::array<std::uint64_t, 4u> source{2u, 3u, 5u, 7u};
    std::array<std::uint64_t, 4u> destination{};
    an::HistogramTransformRequest request;
    request.points = kIdentity;
    request.interpolation = an::CurveInterpolation::Linear;
    request.approximation = an::HistogramTransformApproximation::NearestBin;

    const an::Result<an::HistogramTransformResult> transformed =
        an::transformImageHistogram(execution, histogram(source), source, request, destination);
    ASSERT_TRUE(transformed.hasValue()) << transformed.status().message();
    EXPECT_EQ(destination, source);
    EXPECT_EQ(transformed->count, 17u);
    EXPECT_EQ(transformed->sourceBinsVisited, 4u);
    EXPECT_EQ(transformed->clampedLow, 0u);
    EXPECT_EQ(transformed->clampedHigh, 0u);
}

TEST(HistogramTransformTest, LinearPlacementSplitsIntegersDeterministically)
{
    an::EngineContext engine = histogramTransformEngine();
    an::ExecutionContext execution = histogramTransformExecution(engine);
    const std::array<std::uint64_t, 1u> source{9u};
    std::array<std::uint64_t, 4u> destination{};
    constexpr std::array<an::CurveControlPoint, 2u> curve{
        an::CurveControlPoint{.input = 0.0, .output = 0.0},
        an::CurveControlPoint{.input = 1.0, .output = 0.5},
    };
    an::HistogramTransformRequest request;
    request.points = curve;
    request.interpolation = an::CurveInterpolation::Linear;
    request.approximation = an::HistogramTransformApproximation::LinearSplit;

    const an::Result<an::HistogramTransformResult> transformed =
        an::transformImageHistogram(execution, histogram(source), source, request, destination);
    ASSERT_TRUE(transformed.hasValue()) << transformed.status().message();
    EXPECT_EQ(std::accumulate(destination.begin(), destination.end(), std::uint64_t{0}), 9u);
    EXPECT_EQ(destination[0], 5u);
    EXPECT_EQ(destination[1], 4u);
}

TEST(HistogramTransformTest, RangeClampIsCountedAndRangeRejectIsFailureAtomic)
{
    an::EngineContext engine = histogramTransformEngine();
    an::ExecutionContext execution = histogramTransformExecution(engine);
    const std::array<std::uint64_t, 2u> source{4u, 6u};
    constexpr std::array<an::CurveControlPoint, 2u> curve{
        an::CurveControlPoint{.input = 0.0, .output = 0.0},
        an::CurveControlPoint{.input = 1.0, .output = 1.0},
    };
    an::HistogramTransformRequest request;
    request.points = curve;
    request.outputLow = 0.4;
    request.outputHigh = 0.6;
    request.approximation = an::HistogramTransformApproximation::NearestBin;

    std::array<std::uint64_t, 2u> destination{};
    const an::Result<an::HistogramTransformResult> clamped =
        an::transformImageHistogram(execution, histogram(source), source, request, destination);
    ASSERT_TRUE(clamped.hasValue());
    EXPECT_EQ(clamped->clampedLow, 4u);
    EXPECT_EQ(clamped->clampedHigh, 6u);
    EXPECT_EQ(destination[0], 4u);
    EXPECT_EQ(destination[1], 6u);

    request.rangePolicy = an::HistogramTransformRangePolicy::Reject;
    destination = {91u, 92u};
    const an::Result<an::HistogramTransformResult> rejected =
        an::transformImageHistogram(execution, histogram(source), source, request, destination);
    ASSERT_FALSE(rejected.hasValue());
    EXPECT_EQ(rejected.status().code(), an::ErrorCode::OutOfRange);
    EXPECT_EQ(destination, (std::array<std::uint64_t, 2u>{91u, 92u}));
}

TEST(HistogramTransformTest, EmptyMismatchedAndOverlappingInputsAreRefused)
{
    an::EngineContext engine = histogramTransformEngine();
    an::ExecutionContext execution = histogramTransformExecution(engine);
    std::array<std::uint64_t, 2u> empty{};
    std::array<std::uint64_t, 2u> destination{7u, 8u};
    an::HistogramTransformRequest request;
    request.points = kIdentity;
    EXPECT_EQ(an::transformImageHistogram(execution, histogram(empty), empty, request, destination).status().code(),
              an::ErrorCode::EmptySelection);
    EXPECT_EQ(destination, (std::array<std::uint64_t, 2u>{7u, 8u}));

    const std::array<std::uint64_t, 2u> source{1u, 2u};
    an::ImageHistogram wrong = histogram(source);
    wrong.counted = 99u;
    EXPECT_EQ(an::transformImageHistogram(execution, wrong, source, request, destination).status().code(),
              an::ErrorCode::IncompatibleArguments);

    std::array<std::uint64_t, 3u> shared{1u, 2u, 3u};
    const std::span<const std::uint64_t> overlappingSource(shared.data(), 2u);
    const std::span<std::uint64_t> overlappingDestination(shared.data() + 1u, 2u);
    EXPECT_EQ(an::transformImageHistogram(execution, histogram(overlappingSource), overlappingSource, request,
                                          overlappingDestination)
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);

    std::stop_source stop;
    ASSERT_TRUE(stop.request_stop());
    an::ExecutionContext cancelledExecution = histogramTransformExecution(engine, stop.get_token());
    destination = {17u, 19u};
    EXPECT_EQ(an::transformImageHistogram(cancelledExecution, histogram(source), source, request, destination)
                  .status()
                  .code(),
              an::ErrorCode::Cancelled);
    EXPECT_EQ(destination, (std::array<std::uint64_t, 2u>{17u, 19u}));
}
