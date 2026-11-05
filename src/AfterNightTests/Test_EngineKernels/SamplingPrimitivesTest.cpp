// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for interpolation, borders, invalid samples, and deterministic scatter.

#include <AfterNightEngine/Algorithms/Kernel/GatherScatterKernel.h>
#include <AfterNightEngine/Algorithms/Kernel/ResampleKernel.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stop_token>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{

[[nodiscard]] an::EngineContext samplingEngine(std::uint32_t concurrency,
                                               std::uint64_t budget = an::defaultEngineLimits().memoryBudgetBytes)
{
    an::EngineContextOptions options;
    options.maxConcurrency = concurrency;
    options.limits.memoryBudgetBytes = budget;
    an::Result<an::EngineContext> made = an::EngineContext::create(options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::EngineContext{};
}

[[nodiscard]] an::ExecutionContext samplingExecution(const an::EngineContext& engine, std::uint32_t concurrency,
                                                     std::stop_token stopToken = {})
{
    an::ExecutionOptions options;
    options.maxConcurrency = concurrency;
    options.stopToken = std::move(stopToken);
    an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
}

template <typename SampleT>
[[nodiscard]] an::PlaneView<SampleT> samplingPlane(std::span<SampleT> samples, std::uint32_t width,
                                                   std::uint32_t height, std::uint32_t stride)
{
    const std::optional<an::PlaneView<SampleT>> made = an::PlaneView<SampleT>::create(
        samples, width, height, 1u, static_cast<std::uint64_t>(stride) * sizeof(std::remove_const_t<SampleT>));
    EXPECT_TRUE(made.has_value());
    return *made;
}

[[nodiscard]] an::ValidityDescriptor maskedValidity()
{
    an::ValidityDescriptorRequest request;
    request.rule = an::ValidityRule::ExplicitMask;
    const std::optional<an::ValidityDescriptor> made = an::ValidityDescriptor::create(request);
    EXPECT_TRUE(made.has_value());
    return *made;
}

} // namespace

TEST(SamplingPrimitivesTest, LinearSamplingDefinesEveryBorderAndInvalidNeighborPolicy)
{
    const std::array<float, 4u> sourceSamples{1.0f, 2.0f, 3.0f, 5.0f};
    const an::PlaneView<const float> source = samplingPlane<const float>(sourceSamples, 2u, 2u, 2u);
    an::EngineContext engine = samplingEngine(2u);
    an::ExecutionContext execution = samplingExecution(engine, 2u);

    const auto gather = [&](an::KernelCoordinate coordinate, an::KernelBorderMode border) {
        float output = -99.0f;
        const an::SamplingKernelParameters parameters{
            .interpolation = an::KernelInterpolation::Linear, .border = border, .constantBorder = 9.0f};
        const an::Result<an::KernelRunResult> result = an::referenceGatherInterpolatedKernel(
            execution, source, std::span<const an::KernelCoordinate>{&coordinate, 1u}, std::span<float>{&output, 1u},
            parameters, {});
        EXPECT_TRUE(result.hasValue()) << result.status().message();
        return output;
    };

    EXPECT_FLOAT_EQ(gather({0.5, 0.5}, an::KernelBorderMode::Clamp), 2.75f);
    EXPECT_FLOAT_EQ(gather({-0.5, -0.5}, an::KernelBorderMode::Clamp), 1.0f);
    EXPECT_FLOAT_EQ(gather({-0.5, -0.5}, an::KernelBorderMode::Constant), 7.0f);
    EXPECT_FLOAT_EQ(gather({-0.5, -0.5}, an::KernelBorderMode::Reflect), 1.0f);
    EXPECT_FLOAT_EQ(gather({-0.5, -0.5}, an::KernelBorderMode::ReflectExcludingEdge), 2.75f);
    EXPECT_FLOAT_EQ(gather({-0.5, -0.5}, an::KernelBorderMode::Wrap), 2.75f);

    std::array<std::uint8_t, 2u> maskBytes{};
    const an::ImageExtent extent = *an::ImageExtent::create(2u, 2u, 1u, an::defaultEngineLimits());
    const std::optional<an::ValidityMaskView<std::uint8_t>> writable =
        an::ValidityMaskView<std::uint8_t>::createTight(maskBytes, extent);
    ASSERT_TRUE(writable.has_value());
    writable->fill(true);
    ASSERT_TRUE(writable->setValid(0u, 1u, 1u, false));
    const an::ValidityMaskView<const std::uint8_t> mask = writable->constView();
    const an::KernelValidityView validity{.descriptor = maskedValidity(), .mask = &mask};
    const an::KernelCoordinate center{0.5, 0.5};

    float output = -99.0f;
    an::SamplingKernelParameters parameters{.interpolation = an::KernelInterpolation::Linear,
                                            .invalid = an::SamplingInvalidPolicy::RequireAll};
    const an::Result<an::KernelRunResult> required =
        an::referenceGatherInterpolatedKernel(execution, source, std::span<const an::KernelCoordinate>{&center, 1u},
                                              std::span<float>{&output, 1u}, parameters, validity);
    ASSERT_TRUE(required.hasValue());
    EXPECT_EQ(output, -99.0f);
    EXPECT_EQ(required->selection.valid, 0u);

    parameters.invalid = an::SamplingInvalidPolicy::Renormalize;
    ASSERT_TRUE(an::referenceGatherInterpolatedKernel(execution, source,
                                                      std::span<const an::KernelCoordinate>{&center, 1u},
                                                      std::span<float>{&output, 1u}, parameters, validity)
                    .hasValue());
    EXPECT_FLOAT_EQ(output, 2.0f);

    parameters.invalid = an::SamplingInvalidPolicy::Substitute;
    parameters.invalidSample = 9.0f;
    ASSERT_TRUE(an::referenceGatherInterpolatedKernel(execution, source,
                                                      std::span<const an::KernelCoordinate>{&center, 1u},
                                                      std::span<float>{&output, 1u}, parameters, validity)
                    .hasValue());
    EXPECT_FLOAT_EQ(output, 3.75f);

    parameters.invalidSample = std::numeric_limits<float>::infinity();
    output = -99.0f;
    EXPECT_EQ(an::referenceGatherInterpolatedKernel(execution, source,
                                                    std::span<const an::KernelCoordinate>{&center, 1u},
                                                    std::span<float>{&output, 1u}, parameters, validity)
                  .code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(output, -99.0f);
}

TEST(SamplingPrimitivesTest, EveryInterpolationModeHasAReferenceSamplingContract)
{
    const std::array<float, 36u> sourceSamples{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                               0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                               0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    const an::PlaneView<const float> source = samplingPlane<const float>(sourceSamples, 6u, 6u, 6u);
    an::EngineContext engine = samplingEngine(2u);
    an::ExecutionContext execution = samplingExecution(engine, 2u);
    const an::KernelCoordinate coordinate{2.25, 2.25};

    const auto gather = [&](an::KernelInterpolation interpolation) {
        EXPECT_TRUE(an::isKernelInterpolation(interpolation));
        float output = -99.0f;
        const an::Result<an::KernelRunResult> result = an::referenceGatherInterpolatedKernel(
            execution, source, std::span<const an::KernelCoordinate>{&coordinate, 1u}, std::span<float>{&output, 1u},
            {.interpolation = interpolation, .border = an::KernelBorderMode::Clamp}, {});
        EXPECT_TRUE(result.hasValue()) << result.status().message();
        EXPECT_EQ(result->outputsWritten, 1u);
        return output;
    };

    const float nearest = gather(an::KernelInterpolation::Nearest);
    const float linear = gather(an::KernelInterpolation::Linear);
    const float cubic = gather(an::KernelInterpolation::Cubic);
    const float area = gather(an::KernelInterpolation::Area);
    const float lanczos = gather(an::KernelInterpolation::Lanczos4);
    const float linearExact = gather(an::KernelInterpolation::LinearExact);
    const float nearestExact = gather(an::KernelInterpolation::NearestExact);

    EXPECT_FLOAT_EQ(nearestExact, nearest);
    EXPECT_FLOAT_EQ(linearExact, linear);
    EXPECT_FLOAT_EQ(area, linear);
    EXPECT_FLOAT_EQ(nearest, 1.0f);
    EXPECT_FLOAT_EQ(linear, 0.5625f);
    EXPECT_NEAR(cubic, 0.7724761962890625f, 1.0e-7f);
    EXPECT_NEAR(lanczos, 0.7981431748640644f, 1.0e-7f);
}

TEST(SamplingPrimitivesTest, ParallelGatherMatchesTheScalarOracleAtEveryThreadCount)
{
    constexpr std::uint32_t kWidth = 17u;
    constexpr std::uint32_t kHeight = 9u;
    std::vector<float> sourceSamples(kWidth * kHeight);
    for (std::size_t index = 0u; index < sourceSamples.size(); ++index)
    {
        sourceSamples[index] = static_cast<float>((index % 23u) + 1u) / 7.0f;
    }
    std::array<std::uint8_t, 27u> maskBytes{};
    const an::ImageExtent extent = *an::ImageExtent::create(kWidth, kHeight, 1u, an::defaultEngineLimits());
    const std::optional<an::ValidityMaskView<std::uint8_t>> writable =
        an::ValidityMaskView<std::uint8_t>::createTight(maskBytes, extent);
    ASSERT_TRUE(writable.has_value());
    writable->fill(true);
    ASSERT_TRUE(writable->setValid(0u, 3u, 4u, false));
    const an::ValidityMaskView<const std::uint8_t> mask = writable->constView();
    const an::KernelValidityView validity{.descriptor = maskedValidity(), .mask = &mask};

    std::vector<an::KernelCoordinate> coordinates(4097u);
    for (std::size_t index = 0u; index < coordinates.size(); ++index)
    {
        coordinates[index] =
            an::KernelCoordinate{static_cast<double>(index % 29u) - 5.25, static_cast<double>(index % 13u) - 2.75};
    }
    const an::SamplingKernelParameters parameters{.interpolation = an::KernelInterpolation::Linear,
                                                  .border = an::KernelBorderMode::ReflectExcludingEdge,
                                                  .invalid = an::SamplingInvalidPolicy::Renormalize};
    std::vector<float> oracle(coordinates.size(), -77.0f);
    an::EngineContext oracleEngine = samplingEngine(1u);
    an::ExecutionContext oracleExecution = samplingExecution(oracleEngine, 1u);
    const an::Result<an::KernelRunResult> expected = an::referenceGatherInterpolatedKernel(
        oracleExecution, samplingPlane<const float>(sourceSamples, kWidth, kHeight, kWidth), coordinates, oracle,
        parameters, validity);
    ASSERT_TRUE(expected.hasValue()) << expected.status().message();

    for (const std::uint32_t threads : {1u, 2u, 4u})
    {
        an::EngineContext engine = samplingEngine(threads);
        an::ExecutionContext execution = samplingExecution(engine, threads);
        std::vector<float> actual(coordinates.size(), -77.0f);
        const an::Result<an::KernelRunResult> gathered =
            an::gatherSamples(execution, samplingPlane<const float>(sourceSamples, kWidth, kHeight, kWidth),
                              coordinates, actual, parameters, validity);
        ASSERT_TRUE(gathered.hasValue()) << gathered.status().message();
        EXPECT_EQ(*gathered, *expected);
        EXPECT_EQ(actual, oracle);
    }
}

TEST(SamplingPrimitivesTest, ParallelResampleMatchesReferenceOnOddPaddedOutputAndPreflightsCoordinates)
{
    constexpr std::uint32_t kWidth = 13u;
    constexpr std::uint32_t kHeight = 7u;
    constexpr std::uint32_t kStride = 16u;
    std::vector<float> sourceSamples(kStride * kHeight, -1000.0f);
    std::vector<an::KernelCoordinate> coordinates(kWidth * kHeight);
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            sourceSamples[static_cast<std::size_t>(y) * kStride + x] = static_cast<float>(3u * y + x);
            coordinates[static_cast<std::size_t>(y) * kWidth + x] =
                an::KernelCoordinate{static_cast<double>(x) - 0.25, static_cast<double>(y) + 0.125};
        }
    }
    std::vector<float> reference(kStride * kHeight, -77.0f);
    std::vector<float> parallel(kStride * kHeight, -77.0f);
    an::EngineContext engine = samplingEngine(4u);
    an::ExecutionContext execution = samplingExecution(engine, 4u);
    const an::SamplingKernelParameters parameters{.interpolation = an::KernelInterpolation::Linear,
                                                  .border = an::KernelBorderMode::ReflectExcludingEdge};
    const auto source = samplingPlane<const float>(sourceSamples, kWidth, kHeight, kStride);
    const an::Result<an::KernelRunResult> oracle = an::referenceResampleKernel(
        execution, source, samplingPlane<float>(reference, kWidth, kHeight, kStride), coordinates, parameters, {});
    ASSERT_TRUE(oracle.hasValue()) << oracle.status().message();
    const an::Result<an::SamplingExecutionResult> actual = an::resamplePlane(
        execution, source, samplingPlane<float>(parallel, kWidth, kHeight, kStride), coordinates, parameters, {});
    ASSERT_TRUE(actual.hasValue()) << actual.status().message();
    EXPECT_EQ(actual->run, *oracle);
    EXPECT_GT(actual->tileCount, 0u);
    EXPECT_GE(actual->plannedWorkers, 1u);
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        EXPECT_TRUE(std::ranges::equal(std::span<const float>{reference}.subspan(y * kStride, kWidth),
                                       std::span<const float>{parallel}.subspan(y * kStride, kWidth)));
        EXPECT_EQ(parallel[static_cast<std::size_t>(y) * kStride + kWidth], -77.0f);
    }

    coordinates.back().x = std::numeric_limits<double>::infinity();
    std::ranges::fill(parallel, -55.0f);
    EXPECT_EQ(an::resamplePlane(execution, source, samplingPlane<float>(parallel, kWidth, kHeight, kStride),
                                coordinates, parameters, {})
                  .code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_TRUE(std::ranges::all_of(parallel, [](float value) { return value == -55.0f; }));
}

TEST(SamplingPrimitivesTest, ScatterConflictPoliciesAreBitwiseStableAcrossThreadCounts)
{
    constexpr std::uint32_t kCount = 257u;
    std::vector<float> sourceSamples(kCount);
    std::vector<an::KernelCoordinate> coordinates(kCount);
    for (std::uint32_t index = 0u; index < kCount; ++index)
    {
        sourceSamples[index] = static_cast<float>((index % 11u) + 1u) / 8.0f;
        coordinates[index] = an::KernelCoordinate{static_cast<double>(index % 17u), 0.0};
    }

    for (const an::ScatterConflictPolicy conflict :
         {an::ScatterConflictPolicy::First, an::ScatterConflictPolicy::Last, an::ScatterConflictPolicy::Add})
    {
        coordinates.back() = an::KernelCoordinate{99.0, 0.0};
        std::array<float, 17u> oracle{};
        oracle.fill(1.0f);
        an::EngineContext oracleEngine = samplingEngine(1u);
        an::ExecutionContext oracleExecution = samplingExecution(oracleEngine, 1u);
        const an::Result<an::ScatterKernelResult> expected = an::referenceScatterKernel(
            oracleExecution, samplingPlane<const float>(sourceSamples, kCount, 1u, kCount), coordinates,
            samplingPlane<float>(oracle, 17u, 1u, 17u),
            an::ScatterKernelParameters{.conflicts = conflict, .bounds = an::ScatterBoundsPolicy::Discard}, {});
        ASSERT_TRUE(expected.hasValue()) << expected.status().message();
        std::array<float, 17u> baseline{};
        an::ScatterExecutionResult baselineResult{};
        for (const std::uint32_t threads : {1u, 2u, 4u})
        {
            an::EngineContext engine = samplingEngine(threads);
            an::ExecutionContext execution = samplingExecution(engine, threads);
            std::array<float, 17u> output{};
            output.fill(1.0f);
            const an::Result<an::ScatterExecutionResult> scattered = an::scatterPlane(
                execution, samplingPlane<const float>(sourceSamples, kCount, 1u, kCount), coordinates,
                samplingPlane<float>(output, 17u, 1u, 17u),
                an::ScatterKernelParameters{.conflicts = conflict, .bounds = an::ScatterBoundsPolicy::Discard}, {});
            ASSERT_TRUE(scattered.hasValue()) << scattered.status().message();
            EXPECT_EQ(scattered->run, *expected);
            EXPECT_EQ(output, oracle);
            EXPECT_EQ(scattered->run.conflicts, kCount - 1u - 17u);
            EXPECT_EQ(scattered->run.discarded, 1u);
            EXPECT_EQ(scattered->destinationGroups, 17u);
            EXPECT_GT(scattered->scratchBytes, 0u);
            if (threads == 1u)
            {
                baseline = output;
                baselineResult = *scattered;
            }
            else
            {
                EXPECT_EQ(output, baseline);
                EXPECT_EQ(scattered->run, baselineResult.run);
            }
        }
    }
}

TEST(SamplingPrimitivesTest, RejectingScatterAndScratchRefusalAreAtomic)
{
    const std::array<float, 4u> sourceSamples{1.0f, 2.0f, 3.0f, 4.0f};
    const std::array<an::KernelCoordinate, 4u> conflictCoordinates{{{0.0, 0.0}, {0.0, 0.0}, {1.0, 0.0}, {2.0, 0.0}}};
    an::EngineContext engine = samplingEngine(2u);
    an::ExecutionContext execution = samplingExecution(engine, 2u);
    std::array<float, 4u> output{9.0f, 9.0f, 9.0f, 9.0f};
    EXPECT_EQ(an::scatterPlane(execution, samplingPlane<const float>(sourceSamples, 4u, 1u, 4u), conflictCoordinates,
                               samplingPlane<float>(output, 4u, 1u, 4u),
                               an::ScatterKernelParameters{.conflicts = an::ScatterConflictPolicy::Reject}, {})
                  .code(),
              an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(output, (std::array<float, 4u>{9.0f, 9.0f, 9.0f, 9.0f}));

    constexpr std::uint32_t kLargeCount = 512u;
    an::EngineContext limited = samplingEngine(2u, 4096u);
    an::ExecutionContext limitedExecution = samplingExecution(limited, 2u);
    std::vector<float> largeSource(kLargeCount, 1.0f);
    std::vector<an::KernelCoordinate> largeCoordinates(kLargeCount);
    for (std::uint32_t index = 0u; index < kLargeCount; ++index)
    {
        largeCoordinates[index] = an::KernelCoordinate{static_cast<double>(index), 0.0};
    }
    std::vector<float> largeOutput(kLargeCount, 9.0f);
    EXPECT_EQ(an::scatterPlane(limitedExecution, samplingPlane<const float>(largeSource, kLargeCount, 1u, kLargeCount),
                               largeCoordinates, samplingPlane<float>(largeOutput, kLargeCount, 1u, kLargeCount),
                               an::ScatterKernelParameters{.conflicts = an::ScatterConflictPolicy::First}, {})
                  .code(),
              an::ErrorCode::MemoryBudgetExceeded);
    EXPECT_TRUE(std::ranges::all_of(largeOutput, [](float value) { return value == 9.0f; }));
}

TEST(SamplingPrimitivesTest, ResamplingIsBitwiseIdenticalAcrossEveryBorderInvalidAndValidityCombination)
{
    // The parallel resampler resolves what does not change from one coordinate to the next once per
    // operation instead of once per tap. Everything that resolution captures - the interpolation,
    // the border, the invalid-neighbor policy, whether the validity rule can reject anything, and
    // where the source rows are - is swept here against the scalar oracle, because a plan that
    // captured any of them wrongly would still produce plausible pixels.
    constexpr std::uint32_t kWidth = 11u;
    constexpr std::uint32_t kHeight = 9u;
    constexpr std::uint32_t kStride = 16u;
    std::vector<float> sourceSamples(kStride * kHeight, 0.0f);
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            sourceSamples[static_cast<std::size_t>(y) * kStride + x] = static_cast<float>(5u * y + x) * 0.25f;
        }
    }
    // Samples a value rule can reject, and one a weight of exactly zero must never multiply.
    sourceSamples[static_cast<std::size_t>(2u) * kStride + 3u] = std::numeric_limits<float>::quiet_NaN();
    sourceSamples[static_cast<std::size_t>(4u) * kStride + 5u] = std::numeric_limits<float>::infinity();
    sourceSamples[static_cast<std::size_t>(6u) * kStride + 7u] = 0.0f;

    // Exact integers give a tap a weight of exactly zero, which the sampler skips rather than
    // multiplies; the negative and far-outside coordinates drive every border transform.
    const std::array<double, 6u> offsets{0.0, -0.5, 0.25, 1.0, -3.75, 12.5};
    std::vector<an::KernelCoordinate> coordinates(static_cast<std::size_t>(kWidth) * kHeight);
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            const std::size_t index = static_cast<std::size_t>(y) * kWidth + x;
            coordinates[index] = an::KernelCoordinate{static_cast<double>(x) + offsets[index % offsets.size()],
                                                      static_cast<double>(y) + offsets[(index / 2u) % offsets.size()]};
        }
    }

    std::array<std::uint8_t, 2u * kHeight> maskBytes{};
    const an::ImageExtent extent = *an::ImageExtent::create(kWidth, kHeight, 1u, an::defaultEngineLimits());
    const std::optional<an::ValidityMaskView<std::uint8_t>> writable =
        an::ValidityMaskView<std::uint8_t>::createTight(maskBytes, extent);
    ASSERT_TRUE(writable.has_value());
    writable->fill(true);
    ASSERT_TRUE(writable->setValid(0u, 3u, 4u, false));
    ASSERT_TRUE(writable->setValid(0u, 0u, 0u, false));
    const an::ValidityMaskView<const std::uint8_t> mask = writable->constView();

    const std::array<an::KernelInterpolation, 7u> interpolations{
        an::KernelInterpolation::Nearest,     an::KernelInterpolation::Linear,   an::KernelInterpolation::Cubic,
        an::KernelInterpolation::Area,        an::KernelInterpolation::Lanczos4, an::KernelInterpolation::LinearExact,
        an::KernelInterpolation::NearestExact};
    const std::array<an::KernelBorderMode, 5u> borders{
        an::KernelBorderMode::Clamp, an::KernelBorderMode::Constant, an::KernelBorderMode::Reflect,
        an::KernelBorderMode::ReflectExcludingEdge, an::KernelBorderMode::Wrap};
    const std::array<an::SamplingInvalidPolicy, 3u> policies{an::SamplingInvalidPolicy::RequireAll,
                                                             an::SamplingInvalidPolicy::Renormalize,
                                                             an::SamplingInvalidPolicy::Substitute};
    const std::array<an::ValidityRule, 4u> rules{an::ValidityRule::AllValid, an::ValidityRule::FiniteOnly,
                                                 an::ValidityRule::ZeroIsNoData, an::ValidityRule::ExplicitMask};

    an::EngineContext engine = samplingEngine(4u);
    const auto source = samplingPlane<const float>(sourceSamples, kWidth, kHeight, kStride);
    std::size_t compared = 0u;
    for (const an::KernelInterpolation interpolation : interpolations)
    {
        for (const an::KernelBorderMode border : borders)
        {
            for (const an::SamplingInvalidPolicy policy : policies)
            {
                for (const an::ValidityRule rule : rules)
                {
                    an::ValidityDescriptorRequest request;
                    request.rule = rule;
                    const std::optional<an::ValidityDescriptor> descriptor = an::ValidityDescriptor::create(request);
                    ASSERT_TRUE(descriptor.has_value());
                    const an::KernelValidityView validity{
                        .descriptor = *descriptor, .mask = rule == an::ValidityRule::ExplicitMask ? &mask : nullptr};
                    const an::SamplingKernelParameters parameters{.interpolation = interpolation,
                                                                  .border = border,
                                                                  .constantBorder = 0.5f,
                                                                  .invalid = policy,
                                                                  .invalidSample = -2.25f};

                    std::vector<float> reference(kStride * kHeight, -77.0f);
                    an::ExecutionContext oracleExecution = samplingExecution(engine, 1u);
                    const an::Result<an::KernelRunResult> oracle = an::referenceResampleKernel(
                        oracleExecution, source, samplingPlane<float>(reference, kWidth, kHeight, kStride), coordinates,
                        parameters, validity);
                    ASSERT_TRUE(oracle.hasValue()) << oracle.status().message();

                    for (const std::uint32_t workers : {1u, 2u, 4u})
                    {
                        std::vector<float> parallel(kStride * kHeight, -77.0f);
                        an::ExecutionContext execution = samplingExecution(engine, workers);
                        const an::Result<an::SamplingExecutionResult> actual = an::resamplePlane(
                            execution, source, samplingPlane<float>(parallel, kWidth, kHeight, kStride), coordinates,
                            parameters, validity);
                        ASSERT_TRUE(actual.hasValue()) << actual.status().message();
                        EXPECT_EQ(actual->run, *oracle);
                        // Bitwise rather than near: a NaN must arrive as a NaN and a skipped tap
                        // must not become a multiplication by zero.
                        EXPECT_TRUE(std::ranges::equal(reference, parallel,
                                                       [](float left, float right) {
                                                           return std::bit_cast<std::uint32_t>(left) ==
                                                                  std::bit_cast<std::uint32_t>(right);
                                                       }))
                            << "interpolation " << static_cast<int>(interpolation) << " border "
                            << static_cast<int>(border) << " invalid " << static_cast<int>(policy) << " validity "
                            << static_cast<int>(rule) << " on " << workers << " workers";
                        ++compared;
                    }
                }
            }
        }
    }
    EXPECT_EQ(compared, interpolations.size() * borders.size() * policies.size() * rules.size() * 3u);
}

TEST(SamplingPrimitivesTest, TheCoordinatePreflightStaysAtomicWhereverTheBadCoordinateIs)
{
    // The preflight reads the whole coordinate run before the first tile writes anything. It is
    // checked on more than one worker, so a preflight that reported per range instead of for the
    // whole run would leave the tiles that had already passed committed.
    constexpr std::uint32_t kWidth = 64u;
    constexpr std::uint32_t kHeight = 48u;
    const std::size_t count = static_cast<std::size_t>(kWidth) * kHeight;
    std::vector<float> sourceSamples(count, 1.5f);
    std::vector<an::KernelCoordinate> coordinates(count);
    for (std::size_t index = 0u; index < count; ++index)
    {
        coordinates[index] =
            an::KernelCoordinate{static_cast<double>(index % kWidth), static_cast<double>(index / kWidth)};
    }
    an::EngineContext engine = samplingEngine(4u);
    const auto source = samplingPlane<const float>(sourceSamples, kWidth, kHeight, kWidth);
    const an::SamplingKernelParameters parameters{.interpolation = an::KernelInterpolation::Linear,
                                                  .border = an::KernelBorderMode::Clamp};
    // The first, an interior one, and the last: a preflight that stopped at its own range boundary
    // would miss at least one of them.
    const std::array<std::size_t, 4u> spoiled{0u, count / 3u, count - 1u, count / 2u + 7u};
    const std::array<double, 2u> bad{std::numeric_limits<double>::quiet_NaN(), 1e300};
    for (const std::size_t index : spoiled)
    {
        for (const double value : bad)
        {
            for (const std::uint32_t workers : {1u, 2u, 4u})
            {
                const an::KernelCoordinate kept = coordinates[index];
                coordinates[index] = an::KernelCoordinate{value, kept.y};
                std::vector<float> destination(count, -55.0f);
                an::ExecutionContext execution = samplingExecution(engine, workers);
                EXPECT_EQ(an::resamplePlane(execution, source,
                                            samplingPlane<float>(destination, kWidth, kHeight, kWidth), coordinates,
                                            parameters, {})
                              .code(),
                          an::ErrorCode::InvalidArgument);
                EXPECT_TRUE(std::ranges::all_of(destination, [](float sample) { return sample == -55.0f; }))
                    << "a coordinate refused at " << index << " on " << workers << " workers still wrote output";
                coordinates[index] = kept;
            }
        }
    }
}

TEST(SamplingPrimitivesTest, GatherOverlapIsAnArgumentConflict)
{
    const auto engine = samplingEngine(1u);
    const auto execution = samplingExecution(engine, 1u);
    std::array<float, 4u> samples{1.0f, 2.0f, 3.0f, 4.0f};
    const std::array<an::KernelCoordinate, 1u> positions{{{0.0, 0.0}}};
    const auto gathered = an::gatherSamples(execution, samplingPlane<const float>(samples, 2u, 2u, 2u), positions,
                                            std::span(samples).first(1u), {}, {});
    EXPECT_EQ(gathered.code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_FLOAT_EQ(samples[0], 1.0f);
}
