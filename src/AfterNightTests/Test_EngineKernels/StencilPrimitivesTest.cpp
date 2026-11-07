// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for border-, halo- and tile-aware separable and dense convolution primitives.

#include <AfterNightEngine/Algorithms/Kernel/StencilKernel.h>
#include <AfterNightEngine/Algorithms/Kernel/DeviceBackend.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/CpuCapabilities.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace
{

[[nodiscard]] an::EngineContext stencilEngine(std::uint32_t concurrency)
{
    an::EngineContextOptions options;
    options.maxConcurrency = concurrency;
    an::Result<an::EngineContext> made = an::EngineContext::create(options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::EngineContext{};
}

[[nodiscard]] an::ExecutionContext stencilExecution(const an::EngineContext& engine, std::uint32_t concurrency)
{
    an::ExecutionOptions options;
    options.maxConcurrency = concurrency;
    an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
}

[[nodiscard]] an::ExecutionContext forcedStencilExecution(const an::EngineContext& engine, an::ComputeBackend backend,
                                                          an::InstructionSet instructionSet)
{
    an::ExecutionOptions options;
    options.maxConcurrency = 2u;
    options.backend = backend;
    options.instructionSet = instructionSet;
    options.backendFallback = an::BackendFallback::Forbidden;
    an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
}

[[nodiscard]] std::vector<an::InstructionSet> compiledVectorTargets()
{
    std::vector<an::InstructionSet> targets;
    const an::CpuCapabilities capabilities = an::cpuCapabilities();
    for (std::size_t index = 0u; index < an::kInstructionSetCount; ++index)
    {
        const an::InstructionSet candidate = static_cast<an::InstructionSet>(index);
        if (candidate != an::InstructionSet::Automatic && candidate != an::InstructionSet::Scalar &&
            an::instructionSetIsCompiledIn(candidate) && an::supportsInstructionSet(capabilities, candidate))
        {
            targets.push_back(candidate);
        }
    }
    return targets;
}

[[nodiscard]] an::PlaneView<const float> constPlane(std::span<const float> samples, std::uint32_t width,
                                                    std::uint32_t height, std::uint32_t strideSamples)
{
    const std::optional<an::PlaneView<const float>> view = an::PlaneView<const float>::create(
        samples, width, height, 1u, static_cast<std::uint64_t>(strideSamples) * sizeof(float));
    EXPECT_TRUE(view.has_value());
    return *view;
}

[[nodiscard]] an::PlaneView<float> writablePlane(std::span<float> samples, std::uint32_t width, std::uint32_t height,
                                                 std::uint32_t strideSamples)
{
    const std::optional<an::PlaneView<float>> view = an::PlaneView<float>::create(
        samples, width, height, 1u, static_cast<std::uint64_t>(strideSamples) * sizeof(float));
    EXPECT_TRUE(view.has_value());
    return *view;
}

[[nodiscard]] std::vector<float> stencilSamples(std::size_t count)
{
    std::vector<float> samples(count, 0.0f);
    std::uint64_t state = 0xda3e39cb94b95bdbull;
    for (std::size_t index = 0u; index < count; ++index)
    {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        samples[index] = static_cast<float>(static_cast<std::uint32_t>(state >> 40u) % 2048u) / 1024.0f - 1.0f;
    }
    return samples;
}

/// Normalized Gaussian taps, which is the shape every separable case in this suite uses.
[[nodiscard]] std::vector<double> gaussianTaps(std::uint32_t radius, double sigma)
{
    std::vector<double> taps(2u * static_cast<std::size_t>(radius) + 1u, 0.0);
    double total = 0.0;
    for (std::size_t index = 0u; index < taps.size(); ++index)
    {
        const double offset = static_cast<double>(index) - static_cast<double>(radius);
        taps[index] = std::exp(-0.5 * offset * offset / (sigma * sigma));
        total += taps[index];
    }
    for (double& tap : taps)
    {
        tap /= total;
    }
    return taps;
}

/// The coordinate one border rule resolves an out-of-range index to, or `size` for "not resolvable".
[[nodiscard]] std::int64_t oracleBorderIndex(std::int64_t index, std::int64_t size, an::KernelBorderMode border)
{
    if (index >= 0 && index < size)
    {
        return index;
    }
    switch (border)
    {
    case an::KernelBorderMode::Clamp:
        return index < 0 ? 0 : size - 1;
    case an::KernelBorderMode::Reflect:
        while (index < 0 || index >= size)
        {
            index = index < 0 ? -index - 1 : 2 * size - index - 1;
        }
        return index;
    case an::KernelBorderMode::ReflectExcludingEdge:
        while (index < 0 || index >= size)
        {
            index = index < 0 ? -index : 2 * size - index - 2;
        }
        return index;
    case an::KernelBorderMode::Wrap:
        index %= size;
        return index < 0 ? index + size : index;
    case an::KernelBorderMode::Constant:
    case an::KernelBorderMode::NotApplicable:
        break;
    }
    return size;
}

/// An independent scalar separable convolution written the way the contract reads, with the taps
/// `dilation` samples apart.
[[nodiscard]] std::vector<double> oracleSeparable(std::span<const float> samples, std::uint32_t width,
                                                  std::uint32_t height, std::uint32_t stride,
                                                  std::span<const double> horizontal, std::span<const double> vertical,
                                                  const an::StencilBorderPolicy& borders, std::uint32_t dilation = 1u)
{
    const std::int64_t spacing = static_cast<std::int64_t>(dilation);
    const std::int64_t radiusX = static_cast<std::int64_t>(horizontal.size() / 2u);
    const std::int64_t radiusY = static_cast<std::int64_t>(vertical.size() / 2u);
    double horizontalSum = 0.0;
    for (const double tap : horizontal)
    {
        horizontalSum += tap;
    }

    const auto intermediate = [&](std::int64_t y, std::int64_t x) {
        const std::int64_t sourceY = oracleBorderIndex(y, height, borders.border);
        if (sourceY == static_cast<std::int64_t>(height))
        {
            return static_cast<double>(borders.constantBorder) * horizontalSum;
        }
        double sum = 0.0;
        for (std::size_t tap = 0u; tap < horizontal.size(); ++tap)
        {
            const std::int64_t candidate = x + (static_cast<std::int64_t>(tap) - radiusX) * spacing;
            const std::int64_t sourceX = oracleBorderIndex(candidate, width, borders.border);
            const double value =
                sourceX == static_cast<std::int64_t>(width)
                    ? static_cast<double>(borders.constantBorder)
                    : static_cast<double>(
                          samples[static_cast<std::size_t>(sourceY) * stride + static_cast<std::size_t>(sourceX)]);
            sum += horizontal[tap] * value;
        }
        return sum;
    };

    std::vector<double> result(static_cast<std::size_t>(width) * height, 0.0);
    for (std::uint32_t y = 0u; y < height; ++y)
    {
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            double sum = 0.0;
            for (std::size_t tap = 0u; tap < vertical.size(); ++tap)
            {
                sum += vertical[tap] *
                       intermediate(static_cast<std::int64_t>(y) + (static_cast<std::int64_t>(tap) - radiusY) * spacing,
                                    x);
            }
            result[static_cast<std::size_t>(y) * width + x] = sum;
        }
    }
    return result;
}

/// `oracleSeparable` for an undilated pass over a tight plane, its horizontal pass taken once per source row and read
/// back by every output that needs it: the same sums in the same order, cheap enough for a wide kernel over a wide
/// plane.
[[nodiscard]] std::vector<double> oracleSeparableByRows(std::span<const float> samples, std::uint32_t width,
                                                        std::uint32_t height, std::span<const double> horizontal,
                                                        std::span<const double> vertical,
                                                        const an::StencilBorderPolicy& borders)
{
    const std::int64_t columns = static_cast<std::int64_t>(width);
    const std::int64_t rows = static_cast<std::int64_t>(height);
    const std::int64_t radiusX = static_cast<std::int64_t>(horizontal.size() / 2u);
    const std::int64_t radiusY = static_cast<std::int64_t>(vertical.size() / 2u);
    double horizontalSum = 0.0;
    for (const double tap : horizontal)
    {
        horizontalSum += tap;
    }
    std::vector<double> filteredRows(static_cast<std::size_t>(width) * height, 0.0);
    for (std::int64_t y = 0; y < rows; ++y)
    {
        for (std::int64_t x = 0; x < columns; ++x)
        {
            double sum = 0.0;
            for (std::size_t tap = 0u; tap < horizontal.size(); ++tap)
            {
                const std::int64_t sourceX =
                    oracleBorderIndex(x + static_cast<std::int64_t>(tap) - radiusX, columns, borders.border);
                const double value =
                    sourceX == columns ? static_cast<double>(borders.constantBorder)
                                       : static_cast<double>(samples[static_cast<std::size_t>(y * columns + sourceX)]);
                sum += horizontal[tap] * value;
            }
            filteredRows[static_cast<std::size_t>(y * columns + x)] = sum;
        }
    }
    std::vector<double> result(static_cast<std::size_t>(width) * height, 0.0);
    for (std::int64_t y = 0; y < rows; ++y)
    {
        for (std::int64_t x = 0; x < columns; ++x)
        {
            double sum = 0.0;
            for (std::size_t tap = 0u; tap < vertical.size(); ++tap)
            {
                const std::int64_t sourceY =
                    oracleBorderIndex(y + static_cast<std::int64_t>(tap) - radiusY, rows, borders.border);
                const double value = sourceY == rows ? static_cast<double>(borders.constantBorder) * horizontalSum
                                                     : filteredRows[static_cast<std::size_t>(sourceY * columns + x)];
                sum += vertical[tap] * value;
            }
            result[static_cast<std::size_t>(y * columns + x)] = sum;
        }
    }
    return result;
}

/// Holds every output of a tight plane to the oracle's sum narrowed to Float32, bit for bit: the passes accumulate tap
/// by tap in the taps' own order, as the oracle does, and narrow once.
void expectNarrowsTheOracle(std::span<const float> actual, const std::vector<double>& expected, std::uint32_t width,
                            std::uint32_t height, const char* what)
{
    std::uint64_t differ = 0u;
    std::size_t first = 0u;
    const std::size_t count = static_cast<std::size_t>(width) * height;
    for (std::size_t index = 0u; index < count; ++index)
    {
        if (actual[index] != static_cast<float>(expected[index]) && differ++ == 0u)
        {
            first = index;
        }
    }
    EXPECT_EQ(differ, 0u) << what << ": " << differ << " outputs differ from the oracle narrowed, the first at "
                          << first % width << "," << first / width << ": " << actual[first] << " against "
                          << static_cast<float>(expected[first]);
}

void expectMatchesOracle(std::span<const float> actual, const std::vector<double>& expected, std::uint32_t width,
                         std::uint32_t height, std::uint32_t stride, const char* what)
{
    for (std::uint32_t y = 0u; y < height; ++y)
    {
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            const double reference = expected[static_cast<std::size_t>(y) * width + x];
            const double tolerance = 1e-6 * (std::max)(1.0, std::abs(reference));
            EXPECT_NEAR(static_cast<double>(actual[static_cast<std::size_t>(y) * stride + x]), reference, tolerance)
                << what << " at " << x << "," << y;
        }
    }
}

} // namespace

TEST(StencilPrimitivesTest, SeparableConvolutionMatchesTheScalarOracleOnTinyOddAndLargeKernels)
{
    struct Case
    {
        std::uint32_t width;
        std::uint32_t height;
        std::uint32_t stride;
        std::uint32_t radiusX;
        std::uint32_t radiusY;
    };
    const std::array<Case, 5u> cases{Case{1u, 1u, 1u, 0u, 0u}, Case{3u, 2u, 4u, 5u, 5u}, Case{17u, 13u, 20u, 1u, 3u},
                                     Case{64u, 9u, 64u, 7u, 2u}, Case{40u, 24u, 41u, 32u, 4u}};
    an::EngineContext engine = stencilEngine(4u);
    an::ExecutionContext execution = stencilExecution(engine, 4u);

    for (const Case& shape : cases)
    {
        const std::vector<float> samples = stencilSamples(static_cast<std::size_t>(shape.stride) * shape.height);
        std::vector<float> destination(samples.size(), 0.0f);
        const std::vector<double> horizontal = gaussianTaps(shape.radiusX, 1.7);
        const std::vector<double> vertical = gaussianTaps(shape.radiusY, 0.9);
        const an::StencilBorderPolicy borders{};
        const an::Result<an::StencilExecutionResult> ran = an::convolveSeparablePlane(
            execution, constPlane(samples, shape.width, shape.height, shape.stride),
            writablePlane(destination, shape.width, shape.height, shape.stride),
            an::SeparableStencilRequest{.horizontalTaps = horizontal, .verticalTaps = vertical, .borders = borders},
            an::KernelValidityView{});
        ASSERT_TRUE(ran.hasValue()) << ran.status().message();
        EXPECT_EQ(ran->horizontalRadius, shape.radiusX);
        EXPECT_EQ(ran->verticalRadius, shape.radiusY);
        EXPECT_EQ(ran->run.outputsWritten, static_cast<std::uint64_t>(shape.width) * shape.height);
        EXPECT_GE(ran->tileCount, 1u);
        EXPECT_LE(ran->scratchBytes, an::kMaxStencilScratchBytes);
        expectMatchesOracle(
            destination,
            oracleSeparable(samples, shape.width, shape.height, shape.stride, horizontal, vertical, borders),
            shape.width, shape.height, shape.stride, "separable");
    }
}

TEST(StencilPrimitivesTest, SeparableConvolutionHonorsEveryBorderMode)
{
    constexpr std::uint32_t kWidth = 11u;
    constexpr std::uint32_t kHeight = 7u;
    const std::vector<float> samples = stencilSamples(static_cast<std::size_t>(kWidth) * kHeight);
    std::vector<float> destination(samples.size(), 0.0f);
    const std::vector<double> taps = gaussianTaps(4u, 1.3);
    an::EngineContext engine = stencilEngine(2u);
    an::ExecutionContext execution = stencilExecution(engine, 2u);

    for (const an::KernelBorderMode border :
         {an::KernelBorderMode::Clamp, an::KernelBorderMode::Constant, an::KernelBorderMode::Reflect,
          an::KernelBorderMode::ReflectExcludingEdge, an::KernelBorderMode::Wrap})
    {
        const an::StencilBorderPolicy borders{.border = border, .constantBorder = 0.375f};
        std::fill(destination.begin(), destination.end(), 0.0f);
        const an::Result<an::StencilExecutionResult> ran = an::convolveSeparablePlane(
            execution, constPlane(samples, kWidth, kHeight, kWidth),
            writablePlane(destination, kWidth, kHeight, kWidth),
            an::SeparableStencilRequest{.horizontalTaps = taps, .verticalTaps = taps, .borders = borders},
            an::KernelValidityView{});
        ASSERT_TRUE(ran.hasValue()) << ran.status().message();
        expectMatchesOracle(destination, oracleSeparable(samples, kWidth, kHeight, kWidth, taps, taps, borders), kWidth,
                            kHeight, kWidth, "border");
    }
}

TEST(StencilPrimitivesTest, SeparableConvolutionAgreesWithTheReferenceOnEveryCompiledTarget)
{
    constexpr std::uint32_t kWidth = 133u;
    constexpr std::uint32_t kHeight = 21u;
    const std::vector<float> samples = stencilSamples(static_cast<std::size_t>(kWidth) * kHeight);
    const std::vector<double> horizontal = gaussianTaps(6u, 2.1);
    const std::vector<double> vertical = gaussianTaps(3u, 1.1);
    const std::vector<an::InstructionSet> targets = compiledVectorTargets();
    if (targets.empty())
    {
        GTEST_SKIP() << "this host can run no compiled vector target";
    }
    // Wide enough that the reference execution and one forced target hold leases at the same time.
    an::EngineContext engine = stencilEngine(8u);

    std::vector<float> reference(samples.size(), 0.0f);
    an::ExecutionContext scalar =
        forcedStencilExecution(engine, an::ComputeBackend::Reference, an::InstructionSet::Scalar);
    const an::Result<an::StencilExecutionResult> scalarRun = an::convolveSeparablePlane(
        scalar, constPlane(samples, kWidth, kHeight, kWidth), writablePlane(reference, kWidth, kHeight, kWidth),
        an::SeparableStencilRequest{.horizontalTaps = horizontal, .verticalTaps = vertical}, an::KernelValidityView{});
    ASSERT_TRUE(scalarRun.hasValue()) << scalarRun.status().message();

    for (const an::InstructionSet target : targets)
    {
        std::vector<float> optimized(samples.size(), 0.0f);
        an::ExecutionContext execution = forcedStencilExecution(engine, an::ComputeBackend::Cpu, target);
        const an::Result<an::StencilExecutionResult> ran = an::convolveSeparablePlane(
            execution, constPlane(samples, kWidth, kHeight, kWidth), writablePlane(optimized, kWidth, kHeight, kWidth),
            an::SeparableStencilRequest{.horizontalTaps = horizontal, .verticalTaps = vertical},
            an::KernelValidityView{});
        ASSERT_TRUE(ran.hasValue()) << an::instructionSetName(target) << ": " << ran.status().message();
        for (std::size_t index = 0u; index < optimized.size(); ++index)
        {
            const double expected = static_cast<double>(reference[index]);
            EXPECT_NEAR(static_cast<double>(optimized[index]), expected, 1e-12 * (std::max)(1.0, std::abs(expected)))
                << an::instructionSetName(target) << " at " << index;
        }
    }
}

TEST(StencilPrimitivesTest, SeparableConvolutionIsIdenticalAtEveryAdmittedConcurrency)
{
    constexpr std::uint32_t kWidth = 97u;
    constexpr std::uint32_t kHeight = 53u;
    const std::vector<float> samples = stencilSamples(static_cast<std::size_t>(kWidth) * kHeight);
    const std::vector<double> taps = gaussianTaps(5u, 1.9);
    an::EngineContext engine = stencilEngine(4u);

    std::vector<float> expected(samples.size(), 0.0f);
    for (const std::uint32_t concurrency : {1u, 2u, 4u})
    {
        std::vector<float> destination(samples.size(), 0.0f);
        an::ExecutionContext execution = stencilExecution(engine, concurrency);
        const an::Result<an::StencilExecutionResult> ran = an::convolveSeparablePlane(
            execution, constPlane(samples, kWidth, kHeight, kWidth),
            writablePlane(destination, kWidth, kHeight, kWidth),
            an::SeparableStencilRequest{.horizontalTaps = taps, .verticalTaps = taps}, an::KernelValidityView{});
        ASSERT_TRUE(ran.hasValue()) << ran.status().message();
        if (concurrency == 1u)
        {
            expected = destination;
        }
        EXPECT_EQ(destination, expected) << "concurrency " << concurrency;
    }
}

TEST(StencilPrimitivesTest, ADilatedPassGivesEveryAdmittedWorkerATile)
{
    // A tap spacing that spans most of the plane leaves a single tile when a tile is a run of whole
    // rows, because every row of the run is read by the others. Taken one residue class at a time, the
    // tiles are independent at any spacing, so every worker the operation was admitted has one.
    constexpr std::uint32_t kWidth = 256u;
    constexpr std::uint32_t kHeight = 512u;
    constexpr std::uint32_t kWorkers = 4u;
    const std::vector<float> samples = stencilSamples(static_cast<std::size_t>(kWidth) * kHeight);
    std::vector<float> destination(samples.size(), 0.0f);
    const std::vector<double> taps = gaussianTaps(2u, 1.0);
    an::EngineContext engine = stencilEngine(kWorkers);
    an::ExecutionContext execution = stencilExecution(engine, kWorkers);
    ASSERT_EQ(execution.admittedConcurrency(), kWorkers);

    for (const std::uint32_t dilation : {16u, 64u, 128u})
    {
        const an::Result<an::StencilExecutionResult> ran = an::convolveSeparablePlane(
            execution, constPlane(samples, kWidth, kHeight, kWidth),
            writablePlane(destination, kWidth, kHeight, kWidth),
            an::SeparableStencilRequest{.horizontalTaps = taps, .verticalTaps = taps, .dilation = dilation},
            an::KernelValidityView{});
        ASSERT_TRUE(ran.hasValue()) << ran.status().message();
        EXPECT_GE(ran->tileCount, kWorkers) << "dilation " << dilation;
        EXPECT_EQ(ran->concurrentTiles, kWorkers) << "dilation " << dilation;
        EXPECT_EQ(ran->plannedWorkers, kWorkers) << "dilation " << dilation;
        EXPECT_EQ(ran->haloRows, 2u * dilation) << "dilation " << dilation;
        EXPECT_LE(ran->scratchBytes, an::kMaxStencilScratchBytes) << "dilation " << dilation;
        EXPECT_EQ(ran->run.outputsWritten, static_cast<std::uint64_t>(kWidth) * kHeight) << "dilation " << dilation;
    }
}

TEST(StencilPrimitivesTest, ADilatedPassMatchesTheOracleAndIsIdenticalAtEveryAdmittedConcurrency)
{
    // Spacings that divide the height, that do not, and that exceed it, so a residue class may hold
    // several rows, one row or none, under every border rule.
    constexpr std::uint32_t kWidth = 131u;
    constexpr std::uint32_t kHeight = 97u;
    const std::vector<float> samples = stencilSamples(static_cast<std::size_t>(kWidth) * kHeight);
    const std::vector<double> horizontal = gaussianTaps(2u, 1.2);
    const std::vector<double> vertical = gaussianTaps(3u, 1.5);
    an::EngineContext engine = stencilEngine(8u);

    for (const an::KernelBorderMode border :
         {an::KernelBorderMode::Clamp, an::KernelBorderMode::Constant, an::KernelBorderMode::Reflect,
          an::KernelBorderMode::ReflectExcludingEdge, an::KernelBorderMode::Wrap})
    {
        const an::StencilBorderPolicy borders{.border = border, .constantBorder = 0.25f};
        for (const std::uint32_t dilation : {2u, 3u, 7u, 16u, 40u, 97u, 130u})
        {
            const std::vector<double> oracle =
                oracleSeparable(samples, kWidth, kHeight, kWidth, horizontal, vertical, borders, dilation);
            std::vector<float> expected;
            for (const std::uint32_t concurrency : {1u, 3u, 8u})
            {
                std::vector<float> destination(samples.size(), -7.0f);
                an::ExecutionContext execution = stencilExecution(engine, concurrency);
                const an::Result<an::StencilExecutionResult> ran =
                    an::convolveSeparablePlane(execution, constPlane(samples, kWidth, kHeight, kWidth),
                                               writablePlane(destination, kWidth, kHeight, kWidth),
                                               an::SeparableStencilRequest{.horizontalTaps = horizontal,
                                                                           .verticalTaps = vertical,
                                                                           .borders = borders,
                                                                           .dilation = dilation},
                                               an::KernelValidityView{});
                ASSERT_TRUE(ran.hasValue()) << ran.status().message();
                EXPECT_EQ(ran->run.outputsWritten, static_cast<std::uint64_t>(kWidth) * kHeight);
                if (dilation >= kHeight)
                {
                    // Every class past the last row is empty and holds no tile, so each row is one.
                    EXPECT_EQ(ran->tileRows, 1u) << "dilation " << dilation;
                    EXPECT_EQ(ran->tileCount, kHeight) << "dilation " << dilation;
                }
                if (expected.empty())
                {
                    expected = destination;
                    expectMatchesOracle(destination, oracle, kWidth, kHeight, kWidth, "dilated");
                }
                EXPECT_EQ(destination, expected) << "dilation " << dilation << ", concurrency " << concurrency;
            }
        }
    }
}

TEST(StencilPrimitivesTest, ADilatedPassOverAPlaneNarrowerThanItsHaloMatchesTheOracle)
{
    // Every tap but the centre lands outside a plane this narrow, so the border rule supplies nearly
    // the whole padded row, on both sides of the one or two samples a residue class holds.
    constexpr std::uint32_t kWidth = 5u;
    constexpr std::uint32_t kHeight = 4u;
    const std::vector<float> samples = stencilSamples(static_cast<std::size_t>(kWidth) * kHeight);
    const std::vector<double> taps = gaussianTaps(3u, 1.4);
    an::EngineContext engine = stencilEngine(3u);

    for (const an::KernelBorderMode border :
         {an::KernelBorderMode::Clamp, an::KernelBorderMode::Constant, an::KernelBorderMode::Reflect,
          an::KernelBorderMode::ReflectExcludingEdge, an::KernelBorderMode::Wrap})
    {
        const an::StencilBorderPolicy borders{.border = border, .constantBorder = -0.5f};
        for (const std::uint32_t dilation : {1u, 2u, 3u, 7u})
        {
            for (const std::uint32_t concurrency : {1u, 3u})
            {
                std::vector<float> destination(samples.size(), 0.0f);
                an::ExecutionContext execution = stencilExecution(engine, concurrency);
                const an::Result<an::StencilExecutionResult> ran = an::convolveSeparablePlane(
                    execution, constPlane(samples, kWidth, kHeight, kWidth),
                    writablePlane(destination, kWidth, kHeight, kWidth),
                    an::SeparableStencilRequest{
                        .horizontalTaps = taps, .verticalTaps = taps, .borders = borders, .dilation = dilation},
                    an::KernelValidityView{});
                ASSERT_TRUE(ran.hasValue()) << ran.status().message();
                expectMatchesOracle(destination,
                                    oracleSeparable(samples, kWidth, kHeight, kWidth, taps, taps, borders, dilation),
                                    kWidth, kHeight, kWidth, "narrow");
            }
        }
    }
}

TEST(StencilPrimitivesTest, ADilatedPassSkipsInvalidCentersIdenticallyAtEveryAdmittedConcurrency)
{
    constexpr std::uint32_t kWidth = 67u;
    constexpr std::uint32_t kHeight = 45u;
    std::vector<float> samples = stencilSamples(static_cast<std::size_t>(kWidth) * kHeight);
    for (std::size_t index = 5u; index < samples.size(); index += 11u)
    {
        samples[index] = std::numeric_limits<float>::quiet_NaN();
    }
    const std::optional<an::ValidityDescriptor> finite =
        an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::FiniteOnly});
    ASSERT_TRUE(finite.has_value());
    const std::vector<double> taps = gaussianTaps(2u, 1.0);
    an::EngineContext engine = stencilEngine(8u);
    const std::uint64_t invalid = (samples.size() - 5u + 10u) / 11u;

    for (const std::uint32_t dilation : {1u, 4u, 30u})
    {
        std::vector<float> expected;
        for (const std::uint32_t concurrency : {1u, 3u, 8u})
        {
            std::vector<float> destination(samples.size(), -7.0f);
            an::ExecutionContext execution = stencilExecution(engine, concurrency);
            const an::Result<an::StencilExecutionResult> ran = an::convolveSeparablePlane(
                execution, constPlane(samples, kWidth, kHeight, kWidth),
                writablePlane(destination, kWidth, kHeight, kWidth),
                an::SeparableStencilRequest{.horizontalTaps = taps,
                                            .verticalTaps = taps,
                                            .borders = an::StencilBorderPolicy{.invalidNeighbor = 0.5f},
                                            .dilation = dilation},
                an::KernelValidityView{.descriptor = *finite});
            ASSERT_TRUE(ran.hasValue()) << ran.status().message();
            EXPECT_EQ(ran->run.outputsWritten, samples.size() - invalid);
            for (std::size_t index = 5u; index < samples.size(); index += 11u)
            {
                ASSERT_EQ(destination[index], -7.0f) << "an invalid center was written at " << index;
            }
            if (expected.empty())
            {
                expected = destination;
            }
            EXPECT_EQ(std::memcmp(destination.data(), expected.data(), destination.size() * sizeof(float)), 0)
                << "dilation " << dilation << ", concurrency " << concurrency;
        }
    }
}

TEST(StencilPrimitivesTest, WhenTheCeilingHoldsFewerBandsThanWorkersADilatedPassRunsItsTilesInTurn)
{
    // Rows this wide leave room under the scratch ceiling for only a few bands of this halo, and a dilated pass holds
    // its bands across the whole width, so the eight admitted workers take the tiles a few at a time and the answer is
    // still one worker's.
    constexpr std::uint32_t kWidth = 20000u;
    constexpr std::uint32_t kHeight = 40u;
    constexpr std::uint32_t kDilation = 3u;
    const std::vector<float> samples = stencilSamples(static_cast<std::size_t>(kWidth) * kHeight);
    const std::vector<double> taps = gaussianTaps(8u, 3.0);
    an::EngineContext engine = stencilEngine(8u);

    std::vector<float> expected;
    for (const std::uint32_t concurrency : {1u, 8u})
    {
        std::vector<float> destination(samples.size(), 0.0f);
        an::ExecutionContext execution = stencilExecution(engine, concurrency);
        const an::Result<an::StencilExecutionResult> ran = an::convolveSeparablePlane(
            execution, constPlane(samples, kWidth, kHeight, kWidth),
            writablePlane(destination, kWidth, kHeight, kWidth),
            an::SeparableStencilRequest{.horizontalTaps = taps, .verticalTaps = taps, .dilation = kDilation},
            an::KernelValidityView{});
        ASSERT_TRUE(ran.hasValue()) << ran.status().message();
        EXPECT_LE(ran->scratchBytes, an::kMaxStencilScratchBytes);
        if (concurrency == 8u)
        {
            EXPECT_LT(ran->concurrentTiles, 8u);
            EXPECT_GE(ran->concurrentTiles, 2u);
            EXPECT_EQ(ran->plannedWorkers, ran->concurrentTiles);
        }
        if (expected.empty())
        {
            expected = destination;
        }
        EXPECT_EQ(destination, expected) << "concurrency " << concurrency;
    }
    const an::StencilBorderPolicy borders{};
    expectMatchesOracle(expected, oracleSeparable(samples, kWidth, kHeight, kWidth, taps, taps, borders, kDilation),
                        kWidth, kHeight, kWidth, "wide");
}

TEST(StencilPrimitivesTest, AnUndilatedPassOverAWidePlaneGivesEveryAdmittedWorkerABandOfItsColumns)
{
    // The same rows hold too few bands across their whole width for eight workers, so an undilated pass divides them
    // into strips of columns: every admitted worker holds a band of one, and every output is the oracle's sum narrowed,
    // whichever strip it fell in - a strip decides which samples a band holds, never what an output adds up.
    constexpr std::uint32_t kWidth = 20000u;
    constexpr std::uint32_t kHeight = 40u;
    const std::vector<float> samples = stencilSamples(static_cast<std::size_t>(kWidth) * kHeight);
    const std::vector<double> taps = gaussianTaps(8u, 3.0);
    const an::StencilBorderPolicy borders{};
    const std::vector<double> oracle = oracleSeparableByRows(samples, kWidth, kHeight, taps, taps, borders);
    an::EngineContext engine = stencilEngine(8u);

    for (const std::uint32_t concurrency : {1u, 8u})
    {
        std::vector<float> destination(samples.size(), 0.0f);
        an::ExecutionContext execution = stencilExecution(engine, concurrency);
        const an::Result<an::StencilExecutionResult> ran = an::convolveSeparablePlane(
            execution, constPlane(samples, kWidth, kHeight, kWidth),
            writablePlane(destination, kWidth, kHeight, kWidth),
            an::SeparableStencilRequest{.horizontalTaps = taps, .verticalTaps = taps, .borders = borders},
            an::KernelValidityView{});
        ASSERT_TRUE(ran.hasValue()) << ran.status().message();
        EXPECT_LE(ran->scratchBytes, an::kMaxStencilScratchBytes);
        EXPECT_EQ(ran->concurrentTiles, concurrency);
        EXPECT_EQ(ran->plannedWorkers, concurrency);
        EXPECT_EQ(ran->run.outputsWritten, static_cast<std::uint64_t>(kWidth) * kHeight);
        expectNarrowsTheOracle(destination, oracle, kWidth, kHeight,
                               concurrency == 1u ? "one worker" : "eight workers");
    }
}

TEST(StencilPrimitivesTest, AnUndilatedPassWhoseWholeRowBandExceedsTheCeilingRunsInStripsOfColumns)
{
    // A band across the whole width at this halo holds more than the scratch ceiling even for one row of output - the
    // 129 rows a radius of 64 reads, of 8,200 Float64 samples each - which a wide frame under a wide Gaussian asks for.
    // The pass is planned over strips of columns instead, its edges mirrored as the whole row's are, the rows above and
    // below folded again and again through a plane lower than the reach, and every output is the oracle's sum narrowed.
    constexpr std::uint32_t kWidth = 8200u;
    constexpr std::uint32_t kHeight = 24u;
    constexpr std::uint32_t kRadius = 64u;
    static_assert((2u * kRadius + 1u) * static_cast<std::uint64_t>(kWidth) * sizeof(double) >
                  an::kMaxStencilScratchBytes);
    const std::vector<float> samples = stencilSamples(static_cast<std::size_t>(kWidth) * kHeight);
    const std::vector<double> taps = gaussianTaps(kRadius, 16.0);
    const an::StencilBorderPolicy borders{.border = an::KernelBorderMode::ReflectExcludingEdge};
    an::EngineContext engine = stencilEngine(4u);
    an::ExecutionContext execution = stencilExecution(engine, 4u);

    std::vector<float> destination(samples.size(), 0.0f);
    const an::Result<an::StencilExecutionResult> ran = an::convolveSeparablePlane(
        execution, constPlane(samples, kWidth, kHeight, kWidth), writablePlane(destination, kWidth, kHeight, kWidth),
        an::SeparableStencilRequest{.horizontalTaps = taps, .verticalTaps = taps, .borders = borders},
        an::KernelValidityView{});
    ASSERT_TRUE(ran.hasValue()) << ran.status().message();
    EXPECT_LE(ran->scratchBytes, an::kMaxStencilScratchBytes);
    EXPECT_EQ(ran->concurrentTiles, 4u);
    EXPECT_EQ(ran->haloRows, kRadius);
    expectNarrowsTheOracle(destination, oracleSeparableByRows(samples, kWidth, kHeight, taps, taps, borders), kWidth,
                           kHeight, "a band of the whole row above the ceiling");
}

TEST(StencilPrimitivesTest, SeparableConvolutionSkipsInvalidCentersAndSubstitutesInvalidNeighbors)
{
    constexpr std::uint32_t kWidth = 5u;
    const std::array<float, kWidth> samples{1.0f, 0.0f, 4.0f, 8.0f, 2.0f};
    std::array<float, kWidth> destination{-3.0f, -3.0f, -3.0f, -3.0f, -3.0f};
    const std::array<double, 3u> horizontal{0.25, 0.5, 0.25};
    const std::array<double, 1u> vertical{1.0};
    an::ValidityDescriptorRequest request;
    request.rule = an::ValidityRule::ZeroIsNoData;
    const an::ValidityDescriptor validity = *an::ValidityDescriptor::create(request);

    an::EngineContext engine = stencilEngine(2u);
    an::ExecutionContext execution = stencilExecution(engine, 2u);
    const an::Result<an::StencilExecutionResult> ran = an::convolveSeparablePlane(
        execution, constPlane(samples, kWidth, 1u, kWidth), writablePlane(destination, kWidth, 1u, kWidth),
        an::SeparableStencilRequest{.horizontalTaps = horizontal,
                                    .verticalTaps = vertical,
                                    .borders = an::StencilBorderPolicy{.invalidNeighbor = 0.5f}},
        an::KernelValidityView{.descriptor = validity});
    ASSERT_TRUE(ran.hasValue()) << ran.status().message();

    EXPECT_EQ(ran->run.selection.total, 5u);
    EXPECT_EQ(ran->run.selection.valid, 4u);
    EXPECT_EQ(ran->run.outputsWritten, 4u);
    EXPECT_FLOAT_EQ(destination[1], -3.0f);
    // The invalid neighbour at index one contributes the stated substitute rather than its own zero.
    EXPECT_FLOAT_EQ(destination[2], static_cast<float>(0.25 * 0.5 + 0.5 * 4.0 + 0.25 * 8.0));
}

TEST(StencilPrimitivesTest, SeparableConvolutionReportsABoundedTileAndHaloPlan)
{
    constexpr std::uint32_t kWidth = 512u;
    constexpr std::uint32_t kHeight = 384u;
    const std::vector<float> samples = stencilSamples(static_cast<std::size_t>(kWidth) * kHeight);
    std::vector<float> destination(samples.size(), 0.0f);
    const std::vector<double> taps = gaussianTaps(8u, 3.0);
    an::EngineContext engine = stencilEngine(4u);
    an::ExecutionContext execution = stencilExecution(engine, 4u);

    const an::Result<an::StencilExecutionResult> ran = an::convolveSeparablePlane(
        execution, constPlane(samples, kWidth, kHeight, kWidth), writablePlane(destination, kWidth, kHeight, kWidth),
        an::SeparableStencilRequest{.horizontalTaps = taps, .verticalTaps = taps}, an::KernelValidityView{});
    ASSERT_TRUE(ran.hasValue()) << ran.status().message();

    EXPECT_EQ(ran->haloRows, 8u);
    EXPECT_GE(ran->tileRows, 1u);
    EXPECT_EQ(ran->tileCount, (kHeight + ran->tileRows - 1u) / ran->tileRows);
    EXPECT_LE(ran->scratchBytes, an::kMaxStencilScratchBytes);
    // Tiling exists so the intermediate is a band per concurrent worker rather than a whole image.
    EXPECT_LT(ran->scratchBytes, static_cast<std::uint64_t>(kWidth) * kHeight * sizeof(double));
    EXPECT_LE(ran->concurrentTiles, ran->tileCount);
}

TEST(StencilPrimitivesTest, ADilatedPassRefusesAMinimumPlanAboveItsScratchCeiling)
{
    // A dilated pass holds its bands across the whole width, so a row this wide leaves no plan under the ceiling.
    constexpr std::uint32_t kWidth = 300000u;
    const std::vector<float> samples(kWidth, 1.0f);
    std::vector<float> destination(kWidth, -3.0f);
    const std::array<double, 1u> horizontal{1.0};
    const std::array<double, 3u> vertical{0.25, 0.5, 0.25};
    an::EngineContext engine = stencilEngine(2u);
    an::ExecutionContext execution = stencilExecution(engine, 2u);

    const an::Result<an::StencilExecutionResult> refused = an::convolveSeparablePlane(
        execution, constPlane(samples, kWidth, 1u, kWidth), writablePlane(destination, kWidth, 1u, kWidth),
        an::SeparableStencilRequest{.horizontalTaps = horizontal, .verticalTaps = vertical, .dilation = 2u},
        an::KernelValidityView{});
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::LimitExceeded);
    EXPECT_TRUE(std::all_of(destination.begin(), destination.end(), [](float value) { return value == -3.0f; }));
}

TEST(StencilPrimitivesTest, AnUndilatedRowWiderThanTheCeilingHoldsIsFilteredInStripsOfColumns)
{
    // The same row with its taps adjacent needs no band across its whole width: it is filtered a strip of columns at a
    // time, and every output is the oracle's sum narrowed.
    constexpr std::uint32_t kWidth = 300000u;
    const std::vector<float> samples = stencilSamples(kWidth);
    std::vector<float> destination(kWidth, -3.0f);
    const std::array<double, 3u> horizontal{0.2, 0.5, 0.3};
    const std::array<double, 3u> vertical{0.25, 0.5, 0.25};
    an::EngineContext engine = stencilEngine(2u);
    an::ExecutionContext execution = stencilExecution(engine, 2u);

    const an::Result<an::StencilExecutionResult> ran = an::convolveSeparablePlane(
        execution, constPlane(samples, kWidth, 1u, kWidth), writablePlane(destination, kWidth, 1u, kWidth),
        an::SeparableStencilRequest{.horizontalTaps = horizontal, .verticalTaps = vertical}, an::KernelValidityView{});
    ASSERT_TRUE(ran.hasValue()) << ran.status().message();
    EXPECT_LE(ran->scratchBytes, an::kMaxStencilScratchBytes);
    EXPECT_EQ(ran->run.outputsWritten, static_cast<std::uint64_t>(kWidth));
    expectNarrowsTheOracle(destination,
                           oracleSeparableByRows(samples, kWidth, 1u, horizontal, vertical, an::StencilBorderPolicy{}),
                           kWidth, 1u, "a row wider than the ceiling holds");
}

TEST(StencilPrimitivesTest, SeparableConvolutionRefusesRequestsOutsideItsStatedContract)
{
    constexpr std::uint32_t kWidth = 8u;
    std::vector<float> samples(kWidth, 1.0f);
    std::vector<float> destination(kWidth, 0.0f);
    an::EngineContext engine = stencilEngine(2u);
    an::ExecutionContext execution = stencilExecution(engine, 2u);
    const std::array<double, 3u> taps{0.25, 0.5, 0.25};
    const auto attempt = [&](const an::SeparableStencilRequest& request) {
        return an::convolveSeparablePlane(execution, constPlane(samples, kWidth, 1u, kWidth),
                                          writablePlane(destination, kWidth, 1u, kWidth), request,
                                          an::KernelValidityView{})
            .status()
            .code();
    };

    const std::array<double, 2u> even{0.5, 0.5};
    EXPECT_EQ(attempt(an::SeparableStencilRequest{.horizontalTaps = even, .verticalTaps = taps}),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(attempt(an::SeparableStencilRequest{.horizontalTaps = std::span<const double>{}, .verticalTaps = taps}),
              an::ErrorCode::InvalidArgument);

    const std::array<double, 3u> notFinite{0.25, std::numeric_limits<double>::quiet_NaN(), 0.25};
    EXPECT_EQ(attempt(an::SeparableStencilRequest{.horizontalTaps = notFinite, .verticalTaps = taps}),
              an::ErrorCode::InvalidArgument);

    const std::vector<double> tooWide(2u * static_cast<std::size_t>(an::kMaxStencilRadius) + 3u, 0.0);
    EXPECT_EQ(attempt(an::SeparableStencilRequest{.horizontalTaps = tooWide, .verticalTaps = taps}),
              an::ErrorCode::OutOfRange);

    EXPECT_EQ(an::convolveSeparablePlane(
                  execution, constPlane(samples, kWidth, 1u, kWidth), writablePlane(samples, kWidth, 1u, kWidth),
                  an::SeparableStencilRequest{.horizontalTaps = taps, .verticalTaps = taps}, an::KernelValidityView{})
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
}

TEST(StencilPrimitivesTest, DenseConvolutionReproducesTheSeparableResultForASeparableKernel)
{
    constexpr std::uint32_t kWidth = 29u;
    constexpr std::uint32_t kHeight = 19u;
    const std::vector<float> samples = stencilSamples(static_cast<std::size_t>(kWidth) * kHeight);
    const std::vector<double> horizontal = gaussianTaps(3u, 1.4);
    const std::vector<double> vertical = gaussianTaps(2u, 0.8);
    std::vector<double> dense(horizontal.size() * vertical.size(), 0.0);
    for (std::size_t row = 0u; row < vertical.size(); ++row)
    {
        for (std::size_t column = 0u; column < horizontal.size(); ++column)
        {
            dense[row * horizontal.size() + column] = vertical[row] * horizontal[column];
        }
    }

    an::EngineContext engine = stencilEngine(4u);
    an::ExecutionContext execution = stencilExecution(engine, 4u);
    std::vector<float> separableResult(samples.size(), 0.0f);
    const an::Result<an::StencilExecutionResult> separable = an::convolveSeparablePlane(
        execution, constPlane(samples, kWidth, kHeight, kWidth),
        writablePlane(separableResult, kWidth, kHeight, kWidth),
        an::SeparableStencilRequest{.horizontalTaps = horizontal, .verticalTaps = vertical}, an::KernelValidityView{});
    ASSERT_TRUE(separable.hasValue()) << separable.status().message();

    std::vector<float> denseResult(samples.size(), 0.0f);
    const an::Result<an::StencilExecutionResult> ran = an::convolveDensePlane(
        execution, constPlane(samples, kWidth, kHeight, kWidth), writablePlane(denseResult, kWidth, kHeight, kWidth),
        an::DenseStencilRequest{.coefficients = dense,
                                .width = static_cast<std::uint32_t>(horizontal.size()),
                                .height = static_cast<std::uint32_t>(vertical.size())},
        an::KernelValidityView{});
    ASSERT_TRUE(ran.hasValue()) << ran.status().message();
    EXPECT_EQ(ran->scratchBytes, 0u);

    for (std::size_t index = 0u; index < denseResult.size(); ++index)
    {
        EXPECT_NEAR(static_cast<double>(denseResult[index]), static_cast<double>(separableResult[index]), 1e-6)
            << "at " << index;
    }
}

TEST(StencilPrimitivesTest, DenseConvolutionRefusesDimensionsOutsideItsStatedContract)
{
    constexpr std::uint32_t kWidth = 8u;
    std::vector<float> samples(kWidth, 1.0f);
    std::vector<float> destination(kWidth, 0.0f);
    an::EngineContext engine = stencilEngine(2u);
    an::ExecutionContext execution = stencilExecution(engine, 2u);
    const std::array<double, 4u> coefficients{0.25, 0.25, 0.25, 0.25};
    const auto attempt = [&](const an::DenseStencilRequest& request) {
        return an::convolveDensePlane(execution, constPlane(samples, kWidth, 1u, kWidth),
                                      writablePlane(destination, kWidth, 1u, kWidth), request, an::KernelValidityView{})
            .status()
            .code();
    };

    EXPECT_EQ(attempt(an::DenseStencilRequest{.coefficients = coefficients, .width = 2u, .height = 2u}),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(attempt(an::DenseStencilRequest{.coefficients = coefficients, .width = 3u, .height = 1u}),
              an::ErrorCode::IncompatibleArguments);
    // Odd, so the extent check passes and the coefficient ceiling is what refuses it.
    const std::vector<double> huge(static_cast<std::size_t>(an::kMaxStencilKernelSamples) + 1u, 0.0);
    EXPECT_EQ(attempt(an::DenseStencilRequest{
                  .coefficients = huge, .width = static_cast<std::uint32_t>(huge.size()), .height = 1u}),
              an::ErrorCode::OutOfRange);
}

TEST(StencilDeviceKernelContractTest, PlanContractAndRequestStateTheBorderHaloAndBufferShape)
{
    an::Stencil3x3KernelParameters parameters;
    parameters.coefficients = {0.125, -0.25, 0.375, -0.5, 1.5, -0.5, 0.375, -0.25, 0.125};
    parameters.border = an::KernelBorderMode::Constant;
    parameters.constantBorder = -0.75f;

    const an::KernelPlanKey plan = an::stencil3x3KernelPlan(parameters);
    EXPECT_EQ(plan.id, an::kStencil3x3KernelId);
    EXPECT_EQ(plan.computeSample, an::SampleType::Float64);
    EXPECT_EQ(plan.border, an::KernelBorderMode::Constant);
    EXPECT_EQ(plan.stride, an::KernelStridePolicy::RowStrided);
    EXPECT_EQ(plan.validity, an::ValidityRule::AllValid);
    EXPECT_EQ(plan.haloRadius, an::kStencil3x3HaloRadius);
    EXPECT_EQ(plan.scratch, an::KernelScratchPolicy::DeviceAllocated);

    const an::KernelContract contract = an::stencil3x3KernelContract();
    EXPECT_TRUE(an::kernelContractSupports(contract, plan));
    EXPECT_EQ(contract.capabilities.borders, an::KernelBorderCapability::ClampAndConstant);
    EXPECT_EQ(contract.capabilities.haloRadius, an::kStencil3x3HaloRadius);
    EXPECT_EQ(contract.capabilities.computeSample, an::SampleType::Float64);

    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(13u, 7u, 1u, an::defaultEngineLimits());
    ASSERT_TRUE(extent.has_value());
    const std::optional<an::ImageLayout> layout =
        an::ImageLayout::create(an::ImageLayoutRequest{.extent = *extent,
                                                       .elementSizeBytes = sizeof(float),
                                                       .samplesPerPixel = 1u,
                                                       .planeCount = 1u,
                                                       .rowAlignmentBytes = 64u},
                                an::defaultEngineLimits());
    ASSERT_TRUE(layout.has_value());
    const std::array layouts{*layout, *layout};
    const an::DeviceKernelRequest request{
        .plan = plan, .bindingLayouts = layouts, .arguments = an::deviceKernelArguments(parameters)};
    EXPECT_TRUE(an::checkStencil3x3KernelRequest(request).ok());

    an::KernelPlanKey wrongBorder = plan;
    wrongBorder.border = an::KernelBorderMode::Clamp;
    EXPECT_EQ(
        an::checkStencil3x3KernelRequest(an::DeviceKernelRequest{.plan = wrongBorder,
                                                                 .bindingLayouts = layouts,
                                                                 .arguments = an::deviceKernelArguments(parameters)})
            .code(),
        an::ErrorCode::IncompatibleArguments);

    an::KernelPlanKey wrongHalo = plan;
    wrongHalo.haloRadius = 0u;
    EXPECT_EQ(an::checkStencil3x3KernelRequest(
                  an::DeviceKernelRequest{
                      .plan = wrongHalo, .bindingLayouts = layouts, .arguments = an::deviceKernelArguments(parameters)})
                  .code(),
              an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(an::checkStencil3x3KernelRequest(
                  an::DeviceKernelRequest{.plan = plan,
                                          .bindingLayouts = std::span<const an::ImageLayout>(layouts).first(1u),
                                          .arguments = an::deviceKernelArguments(parameters)})
                  .code(),
              an::ErrorCode::InvalidArgument);

    const an::EngineLimits enormousLimits{.maxImageWidth = (std::numeric_limits<std::uint32_t>::max)(),
                                          .maxImageHeight = (std::numeric_limits<std::uint32_t>::max)(),
                                          .maxChannelCount = 1u,
                                          .maxImageBytes = (std::numeric_limits<std::uint64_t>::max)(),
                                          .memoryBudgetBytes = (std::numeric_limits<std::uint64_t>::max)()};
    const std::optional<an::ImageExtent> enormousExtent =
        an::ImageExtent::create(2'000'000'000u, 1'000'000'000u, 1u, enormousLimits);
    ASSERT_TRUE(enormousExtent.has_value());
    const std::optional<an::ImageLayout> enormousLayout =
        an::ImageLayout::create(an::ImageLayoutRequest{.extent = *enormousExtent,
                                                       .elementSizeBytes = sizeof(float),
                                                       .samplesPerPixel = 1u,
                                                       .planeCount = 1u,
                                                       .rowAlignmentBytes = 1u},
                                enormousLimits);
    ASSERT_TRUE(enormousLayout.has_value());
    const std::array enormousLayouts{*enormousLayout, *enormousLayout};
    EXPECT_EQ(
        an::checkStencil3x3KernelRequest(an::DeviceKernelRequest{.plan = plan,
                                                                 .bindingLayouts = enormousLayouts,
                                                                 .arguments = an::deviceKernelArguments(parameters)})
            .code(),
        an::ErrorCode::OutOfRange);
}
