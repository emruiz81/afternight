// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for fused pointwise execution over real image layouts.

#include <AfterNightEngine/Algorithms/Pointwise/PointwiseExecution.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseEvaluator.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseOp.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgramBuilder.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/CpuCapabilities.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{

[[nodiscard]] an::EngineContext makeEngine()
{
    an::EngineContextOptions options;
    options.maxConcurrency = 4u;
    an::Result<an::EngineContext> made = an::EngineContext::create(options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::EngineContext{};
}

[[nodiscard]] an::ExecutionContext makeExecution(const an::EngineContext& engine, an::ComputeBackend backend)
{
    an::ExecutionOptions options;
    options.maxConcurrency = 2u;
    options.backend = backend;
    options.backendFallback = an::BackendFallback::Forbidden;
    an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
}

[[nodiscard]] std::vector<an::InstructionSet> runnableVectorTargets()
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

[[nodiscard]] an::ExecutionContext makeVectorExecution(const an::EngineContext& engine,
                                                       an::InstructionSet instructionSet)
{
    an::ExecutionOptions options;
    options.maxConcurrency = 2u;
    options.backend = an::ComputeBackend::Cpu;
    options.instructionSet = instructionSet;
    options.backendFallback = an::BackendFallback::Forbidden;
    an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
}

[[nodiscard]] an::ImageLayout planarLayout(an::SampleType type, std::uint32_t width, std::uint32_t height,
                                           std::uint32_t channels, std::uint32_t rowPaddingSamples = 0u)
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, channels, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const std::uint32_t elementBytes = an::sampleTypeSizeBytes(type);
    const an::ImageLayoutRequest request{.extent = *extent,
                                         .elementSizeBytes = elementBytes,
                                         .samplesPerPixel = 1u,
                                         .planeCount = channels,
                                         .rowAlignmentBytes = 1u};
    const std::uint64_t rowStride = static_cast<std::uint64_t>(width + rowPaddingSamples) * elementBytes;
    const std::optional<an::ImageLayout> layout =
        an::ImageLayout::createWithStrides(request, rowStride, rowStride * height, an::defaultEngineLimits());
    EXPECT_TRUE(layout.has_value());
    return *layout;
}

[[nodiscard]] an::PointwiseProgram fusedProgram()
{
    an::PointwiseProgramBuilder builder;
    builder.channels(2u)
        .sources(2u)
        .append(an::pointwiseScale(1.25))
        .append(an::pointwiseBias(-0.5))
        .append(an::pointwiseClamp(-3.0, 8.0))
        .append(an::pointwiseBlend(1u, 0.25));
    an::Result<an::PointwiseProgram> program = builder.build();
    EXPECT_TRUE(program.hasValue()) << program.status().message();
    return std::move(*program);
}

[[nodiscard]] std::uint32_t nextGeneratedValue(std::uint32_t& state) noexcept
{
    state = state * 1664525u + 1013904223u;
    return state;
}

[[nodiscard]] an::PointwiseProgram generatedProgram(std::uint32_t expression, std::uint32_t& state)
{
    an::PointwiseProgramBuilder builder;
    builder.channels(2u).sources(3u);
    builder.append(an::pointwiseScale(static_cast<double>(nextGeneratedValue(state) % 25u + 1u) / 16.0));
    builder.append(
        an::pointwiseBias(static_cast<double>(static_cast<int>(nextGeneratedValue(state) % 33u) - 16) / 8.0));
    if ((expression & 1u) != 0u)
    {
        builder.allowingContraction();
    }

    const std::uint32_t extraSteps = 3u + nextGeneratedValue(state) % 13u;
    for (std::uint32_t step = 0u; step < extraSteps; ++step)
    {
        switch (nextGeneratedValue(state) % 5u)
        {
        case 0u:
            builder.append(an::pointwiseScale(static_cast<double>(nextGeneratedValue(state) % 25u + 1u) / 16.0));
            break;
        case 1u:
            builder.append(
                an::pointwiseBias(static_cast<double>(static_cast<int>(nextGeneratedValue(state) % 33u) - 16) / 8.0));
            break;
        case 2u: {
            const double low = static_cast<double>(static_cast<int>(nextGeneratedValue(state) % 17u) - 8) / 4.0;
            const double span = static_cast<double>(nextGeneratedValue(state) % 17u + 1u) / 4.0;
            builder.append(an::pointwiseClamp(low, low + span));
            break;
        }
        case 3u:
            builder.append(an::pointwiseMask(1u + nextGeneratedValue(state) % 2u));
            break;
        default:
            builder.append(an::pointwiseBlend(1u + nextGeneratedValue(state) % 2u,
                                              static_cast<double>(nextGeneratedValue(state) % 17u) / 16.0));
            break;
        }
    }
    an::Result<an::PointwiseProgram> program = builder.build();
    EXPECT_TRUE(program.hasValue()) << program.status().message();
    return std::move(*program);
}

/// The bits of a sample, so that two not-a-numbers compare by their patterns.
template <typename SampleT> [[nodiscard]] auto bitsOf(SampleT value) noexcept
{
    if constexpr (std::is_same_v<SampleT, float>)
    {
        return std::bit_cast<std::uint32_t>(value);
    }
    else
    {
        return std::bit_cast<std::uint64_t>(value);
    }
}

/// A negative not-a-number carrying a payload, which the evaluator answers with the quiet not-a-number.
template <typename SampleT> [[nodiscard]] SampleT negativeNanWithPayload() noexcept
{
    if constexpr (std::is_same_v<SampleT, float>)
    {
        return std::bit_cast<float>(std::uint32_t{0xFFC01234u});
    }
    else
    {
        return std::bit_cast<double>(std::uint64_t{0xFFF8000000001234ull});
    }
}

/// Inputs that reach every branch of a sampled curve over `[low, high]`: the infinities and not-a-numbers, both
/// zeros, the ends and just past them, every table node and the middle between two, and a dense walk.
template <typename SampleT>
[[nodiscard]] std::vector<SampleT> sampledCurveInputs(double low, double high, std::uint32_t entries)
{
    std::vector<SampleT> inputs{-std::numeric_limits<SampleT>::infinity(),
                                std::numeric_limits<SampleT>::infinity(),
                                std::numeric_limits<SampleT>::quiet_NaN(),
                                negativeNanWithPayload<SampleT>(),
                                SampleT{0},
                                -SampleT{0},
                                static_cast<SampleT>(low),
                                static_cast<SampleT>(high),
                                std::nextafter(static_cast<SampleT>(low), -std::numeric_limits<SampleT>::infinity()),
                                std::nextafter(static_cast<SampleT>(high), std::numeric_limits<SampleT>::infinity()),
                                static_cast<SampleT>(low - 3.0),
                                static_cast<SampleT>(high + 7.5),
                                std::numeric_limits<SampleT>::max(),
                                std::numeric_limits<SampleT>::lowest(),
                                std::numeric_limits<SampleT>::denorm_min()};
    const std::uint32_t nodes = (std::min)(entries, 300u);
    for (std::uint32_t node = 0u; node < nodes; ++node)
    {
        const std::uint32_t index =
            static_cast<std::uint32_t>(static_cast<std::uint64_t>(node) * (entries - 1u) / (std::max)(nodes - 1u, 1u));
        const double at = low + (high - low) * static_cast<double>(index) / static_cast<double>(entries - 1u);
        inputs.push_back(static_cast<SampleT>(at));
        inputs.push_back(static_cast<SampleT>(at + (high - low) * 0.5 / static_cast<double>(entries - 1u)));
    }
    std::uint32_t state = 12345u;
    while (inputs.size() < 997u)
    {
        state = state * 1664525u + 1013904223u;
        inputs.push_back(static_cast<SampleT>(low - 0.25 * (high - low) +
                                              1.5 * (high - low) * static_cast<double>(state >> 8) / 16777216.0));
    }
    return inputs;
}

/// A table whose neighbours are sometimes equal - including two zeros of either sign - and otherwise rise and fall.
[[nodiscard]] std::vector<double> sampledCurveTable(std::uint32_t entries)
{
    std::vector<double> table(entries, 0.0);
    for (std::uint32_t index = 0u; index < entries; ++index)
    {
        table[index] = 0.5 + 0.45 * std::sin(static_cast<double>(index) * 0.37) * (index % 5u == 3u ? 0.0 : 1.0);
    }
    if (entries >= 6u)
    {
        table[1] = -0.0;
        table[2] = 0.0;
        table[4] = table[5];
    }
    return table;
}

/// Runs a sampled-curve program over every input on the reference backend and on each runnable vector target, and
/// holds every output and the count of non-finite ones to the reference bit for bit.
template <typename SampleT>
void expectCompiledCurveMatchesTheEvaluator(const an::PointwiseProgram& program, double low, double high,
                                            std::uint32_t entries, const std::vector<an::InstructionSet>& targets)
{
    const std::vector<SampleT> inputs = sampledCurveInputs<SampleT>(low, high, entries);
    const std::uint32_t width = static_cast<std::uint32_t>(inputs.size());
    constexpr an::SampleType kType = std::is_same_v<SampleT, float> ? an::SampleType::Float32 : an::SampleType::Float64;
    const an::ImageLayout layout = planarLayout(kType, width, 1u, 1u);
    const std::array<an::PointwiseImageView, 1u> sources{
        an::PointwiseImageView{.samples = inputs.data(), .layout = &layout}};
    an::EngineContext engine = makeEngine();
    std::vector<SampleT> oracle(inputs.size(), SampleT{-99});
    an::ExecutionContext reference = makeExecution(engine, an::ComputeBackend::Reference);
    const an::Result<an::PointwiseExecutionResult> expected = an::executePointwiseProgram(
        reference, program, sources, an::PointwiseMutableImageView{.samples = oracle.data(), .layout = &layout},
        an::KernelValidityView{});
    ASSERT_TRUE(expected.hasValue()) << expected.status().message();
    for (const an::InstructionSet target : targets)
    {
        std::vector<SampleT> actual(inputs.size(), SampleT{-77});
        an::ExecutionContext vectorized = makeVectorExecution(engine, target);
        const an::Result<an::PointwiseExecutionResult> ran = an::executePointwiseProgram(
            vectorized, program, sources, an::PointwiseMutableImageView{.samples = actual.data(), .layout = &layout},
            an::KernelValidityView{});
        ASSERT_TRUE(ran.hasValue()) << an::instructionSetName(target) << ": " << ran.status().message();
        for (std::size_t index = 0u; index < inputs.size(); ++index)
        {
            ASSERT_EQ(bitsOf(actual[index]), bitsOf(oracle[index]))
                << an::instructionSetName(target) << " input " << inputs[index] << " at " << index << ": "
                << actual[index] << " against " << oracle[index];
        }
        EXPECT_EQ(ran->conversion.nonFiniteInputs, expected->conversion.nonFiniteInputs)
            << an::instructionSetName(target);
        EXPECT_EQ(ran->run.outputsWritten, expected->run.outputsWritten) << an::instructionSetName(target);
    }
}

/// The same comparison over three padded rows of two channels: a non-contiguous image, read a row at a time, its
/// padding left as it was.
template <typename SampleT>
void expectPaddedCurveMatchesTheEvaluator(const an::PointwiseProgram& program, double low, double high,
                                          std::uint32_t entries, const std::vector<an::InstructionSet>& targets)
{
    const std::vector<SampleT> inputs = sampledCurveInputs<SampleT>(low, high, entries);
    constexpr std::uint32_t kRows = 3u;
    constexpr std::uint32_t kPadding = 5u;
    const std::uint32_t width = static_cast<std::uint32_t>(inputs.size()) / kRows;
    const std::uint32_t channels = program.channelCount();
    constexpr an::SampleType kType = std::is_same_v<SampleT, float> ? an::SampleType::Float32 : an::SampleType::Float64;
    const an::ImageLayout layout = planarLayout(kType, width, kRows, channels, kPadding);
    const std::size_t stride = static_cast<std::size_t>(width) + kPadding;
    std::vector<SampleT> source(stride * kRows * channels, SampleT{-55});
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        for (std::uint32_t y = 0u; y < kRows; ++y)
        {
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                source[(channel * kRows + y) * stride + x] = inputs[(y * width + x + channel * 7u) % inputs.size()];
            }
        }
    }
    const std::array<an::PointwiseImageView, 1u> sources{
        an::PointwiseImageView{.samples = source.data(), .layout = &layout}};
    an::EngineContext engine = makeEngine();
    std::vector<SampleT> oracle(source.size(), SampleT{-99});
    an::ExecutionContext reference = makeExecution(engine, an::ComputeBackend::Reference);
    ASSERT_TRUE(an::executePointwiseProgram(reference, program, sources,
                                            an::PointwiseMutableImageView{.samples = oracle.data(), .layout = &layout},
                                            an::KernelValidityView{})
                    .hasValue());
    for (const an::InstructionSet target : targets)
    {
        std::vector<SampleT> actual(source.size(), SampleT{-99});
        an::ExecutionContext vectorized = makeVectorExecution(engine, target);
        ASSERT_TRUE(
            an::executePointwiseProgram(vectorized, program, sources,
                                        an::PointwiseMutableImageView{.samples = actual.data(), .layout = &layout},
                                        an::KernelValidityView{})
                .hasValue());
        for (std::size_t index = 0u; index < source.size(); ++index)
        {
            ASSERT_EQ(bitsOf(actual[index]), bitsOf(oracle[index]))
                << an::instructionSetName(target) << " padded sample " << index;
        }
    }
}
} // namespace

TEST(PointwiseExecutionTest, AGeneratedChainIsFusedAcrossPaddedRowsAndMatchesTheScalarOracle)
{
    constexpr std::uint32_t kWidth = 17u;
    constexpr std::uint32_t kHeight = 3u;
    constexpr std::uint32_t kChannels = 2u;
    constexpr std::uint32_t kStride = 20u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight, kChannels, 3u);
    std::array<float, kStride * kHeight * kChannels> primary{};
    std::array<float, kStride * kHeight * kChannels> operand{};
    std::array<float, kStride * kHeight * kChannels> destination{};
    destination.fill(-999.0f);
    for (std::uint32_t channel = 0u; channel < kChannels; ++channel)
    {
        for (std::uint32_t y = 0u; y < kHeight; ++y)
        {
            for (std::uint32_t x = 0u; x < kWidth; ++x)
            {
                const std::size_t index = channel * kStride * kHeight + y * kStride + x;
                primary[index] = static_cast<float>(static_cast<int>(x) - 8 + static_cast<int>(channel));
                operand[index] = static_cast<float>(x + channel * 2u) * 0.125f;
            }
        }
    }

    const an::PointwiseProgram program = fusedProgram();
    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine, an::ComputeBackend::Reference);
    const std::array<an::PointwiseImageView, 2u> sources{
        an::PointwiseImageView{.samples = primary.data(), .layout = &layout},
        an::PointwiseImageView{.samples = operand.data(), .layout = &layout}};
    const an::Result<an::PointwiseExecutionResult> ran = an::executePointwiseProgram(
        execution, program, sources, an::PointwiseMutableImageView{.samples = destination.data(), .layout = &layout},
        an::KernelValidityView{});
    ASSERT_TRUE(ran.hasValue()) << ran.status().message();
    EXPECT_EQ(ran->run.selection, (an::ValidityCount{.valid = 51u, .total = 51u}));
    EXPECT_EQ(ran->run.outputsWritten, 102u);
    EXPECT_EQ(ran->conversion.samplesConverted, 102u);

    for (std::uint32_t channel = 0u; channel < kChannels; ++channel)
    {
        for (std::uint32_t y = 0u; y < kHeight; ++y)
        {
            for (std::uint32_t x = 0u; x < kWidth; ++x)
            {
                const std::size_t index = channel * kStride * kHeight + y * kStride + x;
                const std::array<double, 4u> pixel{
                    primary[y * kStride + x], primary[kStride * kHeight + y * kStride + x], operand[y * kStride + x],
                    operand[kStride * kHeight + y * kStride + x]};
                std::array<double, 2u> expected{};
                ASSERT_TRUE(an::evaluatePointwisePixel(program, pixel, expected).ok());
                EXPECT_EQ(std::bit_cast<std::uint32_t>(destination[index]),
                          std::bit_cast<std::uint32_t>(static_cast<float>(expected[channel])));
            }
            EXPECT_EQ(destination[channel * kStride * kHeight + y * kStride + kWidth], -999.0f);
        }
    }
}

TEST(PointwiseExecutionTest, ArithmeticAndComparisonTablesMatchReferenceOnEveryRunnableVectorTarget)
{
    constexpr std::uint32_t kWidth = 37u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, 1u, 1u, 3u);
    std::array<float, 40u> left{};
    std::array<float, 40u> right{};
    for (std::uint32_t index = 0u; index < kWidth; ++index)
    {
        left[index] = static_cast<float>(static_cast<int>(index) - 18) * 0.125f;
        right[index] = static_cast<float>(index % 11u + 1u) * 0.0625f;
    }
    const std::array<an::PointwiseImageView, 2u> sources{
        an::PointwiseImageView{.samples = left.data(), .layout = &layout},
        an::PointwiseImageView{.samples = right.data(), .layout = &layout}};
    const std::array operations{
        an::pointwiseAdd(1u, 0.75),
        an::pointwiseSubtract(1u, 0.75),
        an::pointwiseMultiply(1u, 0.75),
        an::pointwiseDivide(1u, 0.75),
        an::pointwiseMinimum(1u),
        an::pointwiseMaximum(1u),
        an::pointwiseCompare(1u, an::PointwiseComparison::Less),
        an::pointwiseCompare(1u, an::PointwiseComparison::LessOrEqual),
        an::pointwiseCompare(1u, an::PointwiseComparison::Equal),
        an::pointwiseCompare(1u, an::PointwiseComparison::NotEqual),
        an::pointwiseCompare(1u, an::PointwiseComparison::GreaterOrEqual),
        an::pointwiseCompare(1u, an::PointwiseComparison::Greater),
        an::pointwiseBlend(1u, 0.375),
    };

    an::EngineContext engine = makeEngine();
    an::ExecutionContext reference = makeExecution(engine, an::ComputeBackend::Reference);
    for (const an::PointwiseOp& operation : operations)
    {
        an::PointwiseProgramBuilder builder;
        builder.sources(2u).append(operation);
        const an::Result<an::PointwiseProgram> program = builder.build();
        ASSERT_TRUE(program.hasValue()) << program.status().message();

        std::array<float, 40u> expected{};
        ASSERT_TRUE(
            an::executePointwiseProgram(reference, *program, sources,
                                        an::PointwiseMutableImageView{.samples = expected.data(), .layout = &layout},
                                        an::KernelValidityView{})
                .hasValue());
        for (const an::InstructionSet target : runnableVectorTargets())
        {
            std::array<float, 40u> actual{};
            an::ExecutionContext vector = makeVectorExecution(engine, target);
            const an::Result<an::PointwiseExecutionResult> ran = an::executePointwiseProgram(
                vector, *program, sources, an::PointwiseMutableImageView{.samples = actual.data(), .layout = &layout},
                an::KernelValidityView{});
            ASSERT_TRUE(ran.hasValue()) << ran.status().message();
            for (std::uint32_t index = 0u; index < kWidth; ++index)
            {
                EXPECT_EQ(std::bit_cast<std::uint32_t>(actual[index]), std::bit_cast<std::uint32_t>(expected[index]))
                    << an::pointwiseOpKindName(operation.kind) << " at sample " << index;
            }
        }
    }
}

TEST(PointwiseExecutionTest, GammaUsesTheVectorApproximationWithinItsPublishedTolerance)
{
    constexpr std::uint32_t kWidth = 257u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, 1u, 1u);
    std::array<float, kWidth> source{};
    std::array<float, kWidth> expected{};
    for (std::uint32_t index = 0u; index < kWidth; ++index)
    {
        source[index] = static_cast<float>(index) / static_cast<float>(kWidth - 1u);
    }
    const std::array<an::PointwiseImageView, 1u> sources{
        an::PointwiseImageView{.samples = source.data(), .layout = &layout}};
    an::PointwiseProgramBuilder builder;
    builder.append(an::pointwiseGamma(1.0 / 2.2));
    const an::Result<an::PointwiseProgram> program = builder.build();
    ASSERT_TRUE(program.hasValue());

    an::EngineContext engine = makeEngine();
    an::ExecutionContext reference = makeExecution(engine, an::ComputeBackend::Reference);
    ASSERT_TRUE(
        an::executePointwiseProgram(reference, *program, sources,
                                    an::PointwiseMutableImageView{.samples = expected.data(), .layout = &layout},
                                    an::KernelValidityView{})
            .hasValue());
    for (const an::InstructionSet target : runnableVectorTargets())
    {
        std::array<float, kWidth> actual{};
        an::ExecutionContext vector = makeVectorExecution(engine, target);
        const an::Result<an::PointwiseExecutionResult> ran = an::executePointwiseProgram(
            vector, *program, sources, an::PointwiseMutableImageView{.samples = actual.data(), .layout = &layout},
            an::KernelValidityView{});
        ASSERT_TRUE(ran.hasValue()) << ran.status().message();
        for (std::uint32_t index = 0u; index < kWidth; ++index)
        {
            EXPECT_NEAR(actual[index], expected[index], 2.0e-5f) << "sample " << index;
        }
    }
}

// A Float32 power answers a sample with the same bits wherever the sample sits in its row - inside the row's vectors
// or in its tail - and on every vector target, so a part of an image, a row split across workers or a narrower image
// is raised exactly as the whole image is.
TEST(PointwiseExecutionTest, AFloat32PowerAnswersASampleAlikeWhereverItSitsInItsRowAndOnEveryVectorTarget)
{
    constexpr std::uint32_t kSamples = 1024u;
    std::vector<float> values(kSamples);
    std::uint32_t state = 20261026u;
    for (float& value : values)
    {
        value = static_cast<float>(nextGeneratedValue(state) >> 8u) / 16777216.0f;
    }
    an::PointwiseProgramBuilder builder;
    builder.append(an::pointwiseGamma(1.0 / 2.2));
    const an::Result<an::PointwiseProgram> program = builder.build();
    ASSERT_TRUE(program.hasValue());
    const std::vector<an::InstructionSet> targets = runnableVectorTargets();
    ASSERT_FALSE(targets.empty());

    an::EngineContext engine = makeEngine();
    const auto raised = [&](an::InstructionSet target, std::uint32_t width, std::uint32_t height) {
        const an::ImageLayout layout = planarLayout(an::SampleType::Float32, width, height, 1u);
        std::vector<float> powers(static_cast<std::size_t>(width) * height, 0.0f);
        const std::array<an::PointwiseImageView, 1u> sources{
            an::PointwiseImageView{.samples = values.data(), .layout = &layout}};
        an::ExecutionContext vector = makeVectorExecution(engine, target);
        const an::Result<an::PointwiseExecutionResult> ran = an::executePointwiseProgram(
            vector, *program, sources, an::PointwiseMutableImageView{.samples = powers.data(), .layout = &layout},
            an::KernelValidityView{});
        EXPECT_TRUE(ran.hasValue()) << ran.status().message();
        return powers;
    };
    // One row of a whole number of every target's vectors, raised by the first target, is what every other run is held
    // to.
    const std::vector<float> whole = raised(targets.front(), kSamples, 1u);
    for (const an::InstructionSet target : targets)
    {
        for (const std::uint32_t width : {kSamples, 7u, 31u, 33u, 100u})
        {
            const std::uint32_t height = kSamples / width;
            const std::vector<float> powers = raised(target, width, height);
            std::uint32_t differing = 0u;
            for (std::size_t index = 0u; index < powers.size(); ++index)
            {
                differing +=
                    std::bit_cast<std::uint32_t>(powers[index]) != std::bit_cast<std::uint32_t>(whole[index]) ? 1u : 0u;
            }
            EXPECT_EQ(differing, 0u) << "target " << static_cast<int>(target) << ", rows of " << width
                                     << " samples: powers other than the whole row's";
        }
    }
}

TEST(PointwiseExecutionTest, ExactInPlaceIsAllowedAndPartialOverlapIsRefusedBeforeAnyWrite)
{
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, 8u, 1u, 1u);
    const an::ImageLayout shorter = planarLayout(an::SampleType::Float32, 7u, 1u, 1u);
    std::array<float, 9u> samples{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, -1.0f};
    an::PointwiseProgramBuilder builder;
    builder.append(an::pointwiseScale(2.0));
    const an::PointwiseProgram program = *builder.build();
    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine, an::ComputeBackend::Reference);

    const std::array<an::PointwiseImageView, 1u> inPlaceSource{
        an::PointwiseImageView{.samples = samples.data(), .layout = &layout}};
    ASSERT_TRUE(an::executePointwiseProgram(execution, program, inPlaceSource,
                                            an::PointwiseMutableImageView{.samples = samples.data(), .layout = &layout},
                                            an::KernelValidityView{})
                    .hasValue());
    EXPECT_EQ(samples[0], 2.0f);
    EXPECT_EQ(samples[7], 16.0f);

    const std::array<float, 9u> before = samples;
    const std::array<an::PointwiseImageView, 1u> overlapping{
        an::PointwiseImageView{.samples = samples.data(), .layout = &shorter}};
    const an::Result<an::PointwiseExecutionResult> refused = an::executePointwiseProgram(
        execution, program, overlapping,
        an::PointwiseMutableImageView{.samples = samples.data() + 1u, .layout = &shorter}, an::KernelValidityView{});
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(samples, before);
}

TEST(PointwiseExecutionTest, EveryCompiledVectorTargetMatchesTheOracleAcrossTailsAndReportsItsOwnCode)
{
    const std::vector<an::InstructionSet> targets = runnableVectorTargets();
    if (targets.empty())
    {
        GTEST_SKIP() << "this host can run no compiled vector target";
    }
    const an::PointwiseProgram program = fusedProgram();
    for (const std::uint32_t width : {1u, 7u, 8u, 9u, 17u, 31u, 33u, 65u})
    {
        const std::uint32_t stride = width + 3u;
        const an::ImageLayout layout = planarLayout(an::SampleType::Float32, width, 2u, 2u, 3u);
        std::vector<float> primary(static_cast<std::size_t>(stride) * 4u, -77.0f);
        std::vector<float> operand(static_cast<std::size_t>(stride) * 4u, -88.0f);
        std::vector<float> oracle(primary.size(), -99.0f);
        for (std::uint32_t channel = 0u; channel < 2u; ++channel)
        {
            for (std::uint32_t y = 0u; y < 2u; ++y)
            {
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    const std::size_t at = channel * stride * 2u + y * stride + x;
                    primary[at] = static_cast<float>(static_cast<int>(x % 19u) - 9) * 0.375f;
                    operand[at] = static_cast<float>(x + 3u * channel) * 0.0625f;
                }
            }
        }
        const std::array<an::PointwiseImageView, 2u> referenceSources{
            an::PointwiseImageView{.samples = primary.data(), .layout = &layout},
            an::PointwiseImageView{.samples = operand.data(), .layout = &layout}};
        an::EngineContext engine = makeEngine();
        an::ExecutionContext reference = makeExecution(engine, an::ComputeBackend::Reference);
        ASSERT_TRUE(
            an::executePointwiseProgram(reference, program, referenceSources,
                                        an::PointwiseMutableImageView{.samples = oracle.data(), .layout = &layout},
                                        an::KernelValidityView{})
                .hasValue());

        for (const an::InstructionSet target : targets)
        {
            std::vector<float> actual(oracle.size(), -99.0f);
            an::ExecutionContext vectorized = makeVectorExecution(engine, target);
            ASSERT_EQ(an::instructionSetOfDispatchedCode(vectorized), target);
            const an::Result<an::PointwiseExecutionResult> ran = an::executePointwiseProgram(
                vectorized, program, referenceSources,
                an::PointwiseMutableImageView{.samples = actual.data(), .layout = &layout}, an::KernelValidityView{});
            ASSERT_TRUE(ran.hasValue()) << an::instructionSetName(target) << ": " << ran.status().message();
            EXPECT_EQ(actual, oracle) << an::instructionSetName(target) << " width " << width;
        }
    }
}

TEST(PointwiseExecutionTest, Float64ProgramsUseEveryCompiledVectorTargetAcrossAPaddedTail)
{
    const std::vector<an::InstructionSet> targets = runnableVectorTargets();
    if (targets.empty())
    {
        GTEST_SKIP() << "this host can run no compiled vector target";
    }
    constexpr std::uint32_t kWidth = 11u;
    constexpr std::uint32_t kHeight = 2u;
    constexpr std::uint32_t kChannels = 2u;
    constexpr std::uint32_t kStride = 13u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float64, kWidth, kHeight, kChannels, 2u);
    std::array<double, kStride * kHeight * kChannels> primary{};
    std::array<double, kStride * kHeight * kChannels> operand{};
    std::array<double, kStride * kHeight * kChannels> oracle{};
    oracle.fill(-999.0);
    for (std::uint32_t channel = 0u; channel < kChannels; ++channel)
    {
        for (std::uint32_t y = 0u; y < kHeight; ++y)
        {
            for (std::uint32_t x = 0u; x < kWidth; ++x)
            {
                const std::size_t at = channel * kStride * kHeight + y * kStride + x;
                primary[at] = static_cast<double>(static_cast<int>(x) - 5 + static_cast<int>(channel)) * 0.25;
                operand[at] = static_cast<double>(x + y + channel) * 0.125;
            }
        }
    }

    an::PointwiseProgramBuilder builder;
    builder.channels(kChannels)
        .sources(2u)
        .readingSamples(an::SampleType::Float64)
        .computingIn(an::ComputePrecision::Float64, an::Float64Justification::StoredInFloat64)
        .append(an::pointwiseScale(1.125))
        .append(an::pointwiseBias(-0.375))
        .append(an::pointwiseClamp(-2.0, 4.0))
        .append(an::pointwiseBlend(1u, 0.125));
    const an::Result<an::PointwiseProgram> built = builder.build();
    ASSERT_TRUE(built.hasValue()) << built.status().message();
    const std::array<an::PointwiseImageView, 2u> sources{
        an::PointwiseImageView{.samples = primary.data(), .layout = &layout},
        an::PointwiseImageView{.samples = operand.data(), .layout = &layout}};
    an::EngineContext engine = makeEngine();
    an::ExecutionContext reference = makeExecution(engine, an::ComputeBackend::Reference);
    ASSERT_TRUE(an::executePointwiseProgram(reference, *built, sources,
                                            an::PointwiseMutableImageView{.samples = oracle.data(), .layout = &layout},
                                            an::KernelValidityView{})
                    .hasValue());

    for (const an::InstructionSet target : targets)
    {
        std::array<double, kStride * kHeight * kChannels> actual{};
        actual.fill(-999.0);
        an::ExecutionContext vectorized = makeVectorExecution(engine, target);
        const an::Result<an::PointwiseExecutionResult> ran = an::executePointwiseProgram(
            vectorized, *built, sources, an::PointwiseMutableImageView{.samples = actual.data(), .layout = &layout},
            an::KernelValidityView{});
        ASSERT_TRUE(ran.hasValue()) << an::instructionSetName(target) << ": " << ran.status().message();
        EXPECT_EQ(actual, oracle) << an::instructionSetName(target);
        EXPECT_EQ(ran->run.outputsWritten, kWidth * kHeight * kChannels);
    }
}

TEST(PointwiseExecutionTest, GeneratedExpressionsDifferentiallyMatchEveryRunnableVectorTarget)
{
    const std::vector<an::InstructionSet> targets = runnableVectorTargets();
    if (targets.empty())
    {
        GTEST_SKIP() << "this host can run no compiled vector target";
    }
    constexpr std::uint32_t kWidth = 37u;
    constexpr std::uint32_t kStride = 41u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, 2u, 2u, 4u);
    std::array<std::vector<float>, 3u> sourceStorage;
    for (std::uint32_t source = 0u; source < sourceStorage.size(); ++source)
    {
        sourceStorage[source].assign(static_cast<std::size_t>(kStride) * 4u, -73.0f);
        for (std::uint32_t channel = 0u; channel < 2u; ++channel)
        {
            for (std::uint32_t y = 0u; y < 2u; ++y)
            {
                for (std::uint32_t x = 0u; x < kWidth; ++x)
                {
                    const std::size_t at = channel * kStride * 2u + y * kStride + x;
                    const int numerator = static_cast<int>((x * 11u + y * 7u + channel * 5u + source * 13u) % 61u) - 30;
                    sourceStorage[source][at] = static_cast<float>(numerator) / 16.0f;
                }
            }
        }
    }
    const std::array<an::PointwiseImageView, 3u> sources{
        an::PointwiseImageView{.samples = sourceStorage[0].data(), .layout = &layout},
        an::PointwiseImageView{.samples = sourceStorage[1].data(), .layout = &layout},
        an::PointwiseImageView{.samples = sourceStorage[2].data(), .layout = &layout}};
    an::EngineContext engine = makeEngine();
    std::uint32_t state = 0xC001D00Du;
    for (std::uint32_t expression = 0u; expression < 32u; ++expression)
    {
        const an::PointwiseProgram program = generatedProgram(expression, state);
        std::vector<float> oracle(sourceStorage[0].size(), -101.0f);
        an::ExecutionContext reference = makeExecution(engine, an::ComputeBackend::Reference);
        ASSERT_TRUE(
            an::executePointwiseProgram(reference, program, sources,
                                        an::PointwiseMutableImageView{.samples = oracle.data(), .layout = &layout},
                                        an::KernelValidityView{})
                .hasValue());
        for (const an::InstructionSet target : targets)
        {
            std::vector<float> actual(oracle.size(), -101.0f);
            an::ExecutionContext vectorized = makeVectorExecution(engine, target);
            const an::Result<an::PointwiseExecutionResult> ran = an::executePointwiseProgram(
                vectorized, program, sources,
                an::PointwiseMutableImageView{.samples = actual.data(), .layout = &layout}, an::KernelValidityView{});
            ASSERT_TRUE(ran.hasValue()) << "expression " << expression << ", " << an::instructionSetName(target) << ": "
                                        << ran.status().message();
            EXPECT_EQ(actual, oracle) << "expression " << expression << ", " << an::instructionSetName(target);
        }
    }
}

TEST(PointwiseExecutionTest, GammaAndAChannelMatrixRemainCorrectForExactInPlaceCpuExecution)
{
    constexpr std::uint32_t kWidth = 5u;
    constexpr std::uint32_t kChannels = 3u;
    constexpr std::uint32_t kStride = 7u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, 1u, kChannels, 2u);
    std::array<float, kStride * kChannels> samples{};
    samples.fill(-101.0f);
    for (std::uint32_t channel = 0u; channel < kChannels; ++channel)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            samples[channel * kStride + x] = static_cast<float>(x + channel + 1u) * 0.25f;
        }
    }
    const std::array<float, kStride * kChannels> original = samples;

    const std::array<double, 9u> rotateChannels{0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 1.0, 0.0, 0.0};
    an::PointwiseProgramBuilder builder;
    builder.channels(kChannels).append(an::pointwiseGamma(2.0));
    const std::uint32_t matrix = builder.addTable(rotateChannels);
    builder.append(an::pointwiseChannelMatrix(matrix));
    const an::Result<an::PointwiseProgram> built = builder.build();
    ASSERT_TRUE(built.hasValue()) << built.status().message();

    std::array<float, kStride * kChannels> expected = original;
    for (std::uint32_t x = 0u; x < kWidth; ++x)
    {
        const std::array<double, kChannels> pixel{original[x], original[kStride + x], original[2u * kStride + x]};
        std::array<double, kChannels> output{};
        ASSERT_TRUE(an::evaluatePointwisePixel(*built, pixel, output).ok());
        for (std::uint32_t channel = 0u; channel < kChannels; ++channel)
        {
            expected[channel * kStride + x] = static_cast<float>(output[channel]);
        }
    }

    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine, an::ComputeBackend::Cpu);
    const std::array<an::PointwiseImageView, 1u> sources{
        an::PointwiseImageView{.samples = samples.data(), .layout = &layout}};
    const an::Result<an::PointwiseExecutionResult> ran = an::executePointwiseProgram(
        execution, *built, sources, an::PointwiseMutableImageView{.samples = samples.data(), .layout = &layout},
        an::KernelValidityView{});
    ASSERT_TRUE(ran.hasValue()) << ran.status().message();
    EXPECT_EQ(samples, expected);
    EXPECT_EQ(ran->run.outputsWritten, kWidth * kChannels);
}

TEST(PointwiseExecutionTest, AnIntegerLookupExecutesOverPaddedImageStorage)
{
    constexpr std::uint32_t kWidth = 9u;
    constexpr std::uint32_t kStride = 12u;
    const an::ImageLayout layout = planarLayout(an::SampleType::UInt8, kWidth, 1u, 1u, 3u);
    std::array<std::uint8_t, kStride> source{0u, 1u, 2u, 17u, 31u, 63u, 127u, 200u, 255u};
    std::array<std::uint8_t, kStride> destination{};
    destination.fill(73u);
    std::array<double, 256u> entries{};
    for (std::size_t index = 0u; index < entries.size(); ++index)
    {
        entries[index] = static_cast<double>(255u - index);
    }
    an::PointwiseProgramBuilder builder;
    builder.readingSamples(an::SampleType::UInt8);
    const std::uint32_t table = builder.addTable(entries);
    builder.append(an::pointwiseLut(table));
    const an::Result<an::PointwiseProgram> built = builder.build();
    ASSERT_TRUE(built.hasValue()) << built.status().message();

    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine, an::ComputeBackend::Cpu);
    const std::array<an::PointwiseImageView, 1u> sources{
        an::PointwiseImageView{.samples = source.data(), .layout = &layout}};
    const an::Result<an::PointwiseExecutionResult> ran = an::executePointwiseProgram(
        execution, *built, sources, an::PointwiseMutableImageView{.samples = destination.data(), .layout = &layout},
        an::KernelValidityView{});
    ASSERT_TRUE(ran.hasValue()) << ran.status().message();
    for (std::uint32_t x = 0u; x < kWidth; ++x)
    {
        EXPECT_EQ(destination[x], static_cast<std::uint8_t>(255u - source[x]));
    }
    EXPECT_EQ(destination[kWidth], 73u);
    EXPECT_EQ(destination[kStride - 1u], 73u);
}

TEST(PointwiseExecutionTest, ProductionExecutionRefusesARepresentablePreviewOnlyLowerCurve)
{
    constexpr std::uint32_t kWidth = 6u;
    constexpr std::uint32_t kHeight = 2u;
    constexpr std::uint32_t kStride = 8u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight, 1u, 2u);
    const std::array<double, 4u> entries{0.0, 0.25, 0.5, 1.0};
    std::array<float, kStride * kHeight> source{-std::numeric_limits<float>::infinity(),
                                                -1.0f,
                                                0.0f,
                                                0.2f,
                                                0.5f,
                                                0.999f,
                                                71.0f,
                                                72.0f,
                                                1.0f,
                                                std::numeric_limits<float>::infinity(),
                                                std::numeric_limits<float>::quiet_NaN(),
                                                0.34f,
                                                0.67f,
                                                2.0f,
                                                73.0f,
                                                74.0f};
    std::array<float, kStride * kHeight> destination{};
    destination.fill(-19.0f);

    an::PointwiseProgramBuilder builder;
    const std::uint32_t table = builder.addTable(entries);
    builder.append(an::pointwiseSampledCurve(table, 0.0, 1.0, an::PointwiseCurveSampling::Lower,
                                             an::PointwiseCurveDomainPolicy::Clamp));
    const an::Result<an::PointwiseProgram> built = builder.build();
    ASSERT_TRUE(built.hasValue()) << built.status().message();

    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine, an::ComputeBackend::Cpu);
    const std::array<an::PointwiseImageView, 1u> sources{
        an::PointwiseImageView{.samples = source.data(), .layout = &layout}};
    const an::Result<an::PointwiseExecutionResult> ran = an::executePointwiseProgram(
        execution, *built, sources, an::PointwiseMutableImageView{.samples = destination.data(), .layout = &layout},
        an::KernelValidityView{});
    ASSERT_FALSE(ran.hasValue());
    EXPECT_EQ(ran.status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_TRUE(std::ranges::all_of(destination, [](float value) { return value == -19.0f; }));
}

TEST(PointwiseExecutionTest, ExplicitValidityMaskSkipsPixelsAndPreservesPadding)
{
    constexpr std::uint32_t kWidth = 17u;
    constexpr std::uint32_t kStride = 20u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, 1u, 1u, 3u);
    std::array<float, kStride> source{};
    std::array<float, kStride> destination{};
    destination.fill(-19.0f);
    for (std::uint32_t x = 0u; x < kWidth; ++x)
    {
        source[x] = static_cast<float>(x + 1u);
    }
    std::array<std::uint8_t, 3u> maskBytes{};
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(kWidth, 1u, 1u, an::defaultEngineLimits());
    ASSERT_TRUE(extent.has_value());
    const std::optional<an::ValidityMaskView<std::uint8_t>> writableMask =
        an::ValidityMaskView<std::uint8_t>::createTight(maskBytes, *extent);
    ASSERT_TRUE(writableMask.has_value());
    for (std::uint32_t x = 0u; x < kWidth; ++x)
    {
        EXPECT_TRUE(writableMask->setValid(0u, x, 0u, x % 3u != 0u));
    }
    const an::ValidityMaskView<const std::uint8_t> mask = writableMask->constView();
    an::ValidityDescriptorRequest request;
    request.rule = an::ValidityRule::ExplicitMask;
    const std::optional<an::ValidityDescriptor> descriptor = an::ValidityDescriptor::create(request);
    ASSERT_TRUE(descriptor.has_value());

    an::PointwiseProgramBuilder builder;
    builder.append(an::pointwiseScale(2.0));
    const an::PointwiseProgram program = *builder.build();
    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine, an::ComputeBackend::Reference);
    const std::array<an::PointwiseImageView, 1u> sources{
        an::PointwiseImageView{.samples = source.data(), .layout = &layout}};
    const an::Result<an::PointwiseExecutionResult> ran = an::executePointwiseProgram(
        execution, program, sources, an::PointwiseMutableImageView{.samples = destination.data(), .layout = &layout},
        an::KernelValidityView{.descriptor = *descriptor, .mask = &mask});
    ASSERT_TRUE(ran.hasValue()) << ran.status().message();
    EXPECT_EQ(ran->run.selection, (an::ValidityCount{.valid = 11u, .total = 17u}));
    EXPECT_EQ(ran->run.outputsWritten, 11u);
    for (std::uint32_t x = 0u; x < kWidth; ++x)
    {
        EXPECT_EQ(destination[x], x % 3u == 0u ? -19.0f : source[x] * 2.0f);
    }
    EXPECT_EQ(destination[kWidth], -19.0f);
    EXPECT_EQ(destination[kStride - 1u], -19.0f);
}

TEST(PointwiseExecutionTest, AValidityMaskOverlappingTheDestinationIsRefusedBeforeAnyWrite)
{
    constexpr std::uint32_t kWidth = 8u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, 1u, 1u);
    const std::array<float, kWidth> source{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    std::array<float, kWidth> destination{};
    destination.fill(-17.0f);
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(kWidth, 1u, 1u, an::defaultEngineLimits());
    ASSERT_TRUE(extent.has_value());
    const std::span<std::uint8_t> destinationBytes{reinterpret_cast<std::uint8_t*>(destination.data()),
                                                   sizeof(destination)};
    const std::optional<an::ValidityMaskView<std::uint8_t>> writableMask =
        an::ValidityMaskView<std::uint8_t>::createTight(destinationBytes, *extent);
    ASSERT_TRUE(writableMask.has_value());
    writableMask->fill(true);
    const an::ValidityMaskView<const std::uint8_t> mask = writableMask->constView();
    const std::array<float, kWidth> before = destination;

    an::ValidityDescriptorRequest validityRequest;
    validityRequest.rule = an::ValidityRule::ExplicitMask;
    const std::optional<an::ValidityDescriptor> validity = an::ValidityDescriptor::create(validityRequest);
    ASSERT_TRUE(validity.has_value());
    an::PointwiseProgramBuilder builder;
    builder.append(an::pointwiseScale(2.0));
    const an::PointwiseProgram program = *builder.build();
    const std::array<an::PointwiseImageView, 1u> sources{
        an::PointwiseImageView{.samples = source.data(), .layout = &layout}};
    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine, an::ComputeBackend::Reference);

    const an::Result<an::PointwiseExecutionResult> refused = an::executePointwiseProgram(
        execution, program, sources, an::PointwiseMutableImageView{.samples = destination.data(), .layout = &layout},
        an::KernelValidityView{.descriptor = *validity, .mask = &mask});
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(destination, before);
}

TEST(PointwiseExecutionTest, IntegerOutputNarrowsOnceWithTheApprovedCounters)
{
    const an::ImageLayout layout = planarLayout(an::SampleType::UInt16, 5u, 1u, 1u);
    const std::array<std::uint16_t, 5u> source{0u, 1u, 2u, 40000u, 65535u};
    std::array<std::uint16_t, 5u> destination{};
    an::PointwiseProgramBuilder builder;
    builder.readingSamples(an::SampleType::UInt16).append(an::pointwiseScale(2.0)).append(an::pointwiseBias(0.5));
    const an::Result<an::PointwiseProgram> built = builder.build();
    ASSERT_TRUE(built.hasValue()) << built.status().message();
    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine, an::ComputeBackend::Reference);
    const std::array<an::PointwiseImageView, 1u> sources{
        an::PointwiseImageView{.samples = source.data(), .layout = &layout}};
    const an::Result<an::PointwiseExecutionResult> ran = an::executePointwiseProgram(
        execution, *built, sources, an::PointwiseMutableImageView{.samples = destination.data(), .layout = &layout},
        an::KernelValidityView{});
    ASSERT_TRUE(ran.hasValue()) << ran.status().message();
    EXPECT_EQ(destination, (std::array<std::uint16_t, 5u>{0u, 2u, 4u, 65535u, 65535u}));
    EXPECT_EQ(ran->conversion.samplesConverted, 5u);
    EXPECT_EQ(ran->conversion.saturatedSamples, 2u);
    EXPECT_EQ(ran->conversion.nonFiniteInputs, 0u);
}

TEST(PointwiseExecutionTest, ContractionIsUsedOnlyWhenTheProgramPermitsIt)
{
    an::PointwiseProgramBuilder separateBuilder;
    separateBuilder.append(an::pointwiseScale(1.00000011920928955078125))
        .append(an::pointwiseBias(-1.0000002384185791015625));
    an::PointwiseProgramBuilder contractedBuilder;
    contractedBuilder.append(an::pointwiseScale(1.00000011920928955078125))
        .append(an::pointwiseBias(-1.0000002384185791015625))
        .allowingContraction();
    const an::PointwiseProgram separate = *separateBuilder.build();
    const an::PointwiseProgram contracted = *contractedBuilder.build();
    const std::array<double, 1u> source{1.00000011920928955078125};
    std::array<double, 1u> separateOutput{};
    std::array<double, 1u> contractedOutput{};
    ASSERT_TRUE(an::evaluatePointwisePixel(separate, source, separateOutput).ok());
    ASSERT_TRUE(an::evaluatePointwisePixel(contracted, source, contractedOutput).ok());
    const float input = static_cast<float>(source[0]);
    const float factor = static_cast<float>(1.00000011920928955078125);
    const float bias = static_cast<float>(-1.0000002384185791015625);
    EXPECT_EQ(static_cast<float>(contractedOutput[0]), std::fma(input, factor, bias));
    EXPECT_EQ(static_cast<float>(separateOutput[0]), input * factor + bias);
}

TEST(PointwiseExecutionTest, VectorRunsCountEveryNonFiniteOutputWithoutASecondSemanticPath)
{
    const std::vector<an::InstructionSet> targets = runnableVectorTargets();
    if (targets.empty())
    {
        GTEST_SKIP() << "this host can run no compiled vector target";
    }
    constexpr std::uint32_t kWidth = 17u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, 1u, 1u);
    std::array<float, kWidth> source{};
    for (std::uint32_t index = 0u; index < kWidth; ++index)
    {
        source[index] = static_cast<float>(index + 1u);
    }
    source[1] = std::numeric_limits<float>::quiet_NaN();
    source[8] = std::numeric_limits<float>::infinity();
    source[16] = -std::numeric_limits<float>::infinity();

    an::PointwiseProgramBuilder builder;
    builder.append(an::pointwiseScale(2.0));
    const an::PointwiseProgram program = *builder.build();
    const std::array<an::PointwiseImageView, 1u> sources{
        an::PointwiseImageView{.samples = source.data(), .layout = &layout}};
    an::EngineContext engine = makeEngine();
    for (const an::InstructionSet target : targets)
    {
        std::array<float, kWidth> destination{};
        an::ExecutionContext execution = makeVectorExecution(engine, target);
        const an::Result<an::PointwiseExecutionResult> ran = an::executePointwiseProgram(
            execution, program, sources,
            an::PointwiseMutableImageView{.samples = destination.data(), .layout = &layout}, an::KernelValidityView{});
        ASSERT_TRUE(ran.hasValue()) << an::instructionSetName(target) << ": " << ran.status().message();
        EXPECT_EQ(ran->conversion.nonFiniteInputs, 3u) << an::instructionSetName(target);
        EXPECT_TRUE(std::isnan(destination[1])) << an::instructionSetName(target);
        EXPECT_EQ(destination[8], std::numeric_limits<float>::infinity()) << an::instructionSetName(target);
        EXPECT_EQ(destination[16], -std::numeric_limits<float>::infinity()) << an::instructionSetName(target);
        EXPECT_EQ(destination[7], source[7] * 2.0f) << an::instructionSetName(target);
    }
}

// A sampled curve - the curves process's whole program, or one step of a chain - runs through the compiled body on
// every vector target and writes the evaluator's bits: interpolated or nearest, clamped or extrapolated, over a table
// of two entries, one with equal and signed-zero neighbours, and the largest the engine samples a curve into, in
// both widths, at every branch the evaluator takes.
TEST(PointwiseExecutionTest, SampledCurvesRunCompiledAndWriteTheEvaluatorsBits)
{
    const std::vector<an::InstructionSet> targets = runnableVectorTargets();
    if (targets.empty())
    {
        GTEST_SKIP() << "this host can run no compiled vector target";
    }
    for (const std::uint32_t entries : {2u, 9u, 65536u})
    {
        const std::vector<double> table = sampledCurveTable(entries);
        for (const an::PointwiseCurveSampling sampling :
             {an::PointwiseCurveSampling::Linear, an::PointwiseCurveSampling::Nearest})
        {
            for (const an::PointwiseCurveDomainPolicy policy :
                 {an::PointwiseCurveDomainPolicy::Clamp, an::PointwiseCurveDomainPolicy::LinearExtrapolation})
            {
                for (const auto& [low, high] : {std::pair{0.0, 1.0}, std::pair{-0.75, 2.5}})
                {
                    SCOPED_TRACE(::testing::Message()
                                 << entries << " entries, sampling " << static_cast<int>(sampling) << ", policy "
                                 << static_cast<int>(policy) << ", [" << low << ", " << high << "]");
                    an::PointwiseProgramBuilder single;
                    single.append(an::pointwiseSampledCurve(single.addTable(table), low, high, sampling, policy));
                    const an::Result<an::PointwiseProgram> curve = single.build();
                    ASSERT_TRUE(curve.hasValue()) << curve.status().message();
                    expectCompiledCurveMatchesTheEvaluator<float>(*curve, low, high, entries, targets);
                    expectPaddedCurveMatchesTheEvaluator<float>(*curve, low, high, entries, targets);

                    an::PointwiseProgramBuilder chained;
                    chained.append(an::pointwiseScale(1.0625))
                        .append(an::pointwiseSampledCurve(chained.addTable(table), low, high, sampling, policy))
                        .append(an::pointwiseBias(-0.125));
                    const an::Result<an::PointwiseProgram> chain = chained.build();
                    ASSERT_TRUE(chain.hasValue()) << chain.status().message();
                    expectCompiledCurveMatchesTheEvaluator<float>(*chain, low, high, entries, targets);

                    an::PointwiseProgramBuilder twoChannels;
                    twoChannels.channels(2u).append(
                        an::pointwiseSampledCurve(twoChannels.addTable(table), low, high, sampling, policy));
                    const an::Result<an::PointwiseProgram> pair = twoChannels.build();
                    ASSERT_TRUE(pair.hasValue()) << pair.status().message();
                    expectPaddedCurveMatchesTheEvaluator<float>(*pair, low, high, entries, targets);

                    an::PointwiseProgramBuilder wide;
                    wide.readingSamples(an::SampleType::Float64)
                        .computingIn(an::ComputePrecision::Float64, an::Float64Justification::StoredInFloat64)
                        .append(an::pointwiseSampledCurve(wide.addTable(table), low, high, sampling, policy));
                    const an::Result<an::PointwiseProgram> wideCurve = wide.build();
                    ASSERT_TRUE(wideCurve.hasValue()) << wideCurve.status().message();
                    expectCompiledCurveMatchesTheEvaluator<double>(*wideCurve, low, high, entries, targets);
                }
            }
        }
    }
}

// An eight- or sixteen-bit image's program that is one exact lookup - an integer curve - writes the evaluator's code
// for every input code and keeps its counters, through its answers made once: an entry between two codes rounds to
// nearest even, one outside the representation saturates, and one beyond Float32's range is infinite at that width
// and narrows as the evaluator narrows it, at either arithmetic width, over padded rows of two channels.
TEST(PointwiseExecutionTest, AnIntegerLookupWritesTheEvaluatorsCodesAndCounters)
{
    const auto run = [](auto sample, an::SampleType sampleType, bool wide) {
        using SampleT = decltype(sample);
        constexpr std::uint32_t kCodes = std::uint32_t{1u} << (8u * sizeof(SampleT));
        const double largest = static_cast<double>(kCodes - 1u);
        std::vector<double> table(kCodes, 0.0);
        for (std::uint32_t code = 0u; code < kCodes; ++code)
        {
            table[code] = largest - static_cast<double>(code) + 0.5 * static_cast<double>(code % 3u);
        }
        table[1] = -3.25;
        table[2] = largest + 7.0;
        table[3] = 1e300;
        table[4] = -1e300;
        table[5] = 2.5;
        table[6] = 3.5;
        table[7] = 1e-30;
        an::PointwiseProgramBuilder builder;
        builder.channels(2u).readingSamples(sampleType);
        if (wide)
        {
            // Exact code values justify Float64 only over 32-bit integers; the calibration divide's reason applies to
            // any storage, so a program over eight or sixteen bits reaches the wider arithmetic through it.
            builder.computingIn(an::ComputePrecision::Float64, an::Float64Justification::CalibrationDivide);
        }
        builder.append(an::pointwiseLut(builder.addTable(table)));
        const an::Result<an::PointwiseProgram> program = builder.build();
        ASSERT_TRUE(program.hasValue()) << program.status().message();

        constexpr std::uint32_t kWidth = 157u;
        constexpr std::uint32_t kRows = 3u;
        constexpr std::uint32_t kPadding = 3u;
        const an::ImageLayout layout = planarLayout(sampleType, kWidth, kRows, 2u, kPadding);
        const std::size_t stride = kWidth + kPadding;
        std::vector<SampleT> source(stride * kRows * 2u, static_cast<SampleT>(11u));
        std::uint32_t state = 77u;
        for (std::size_t index = 0u; index < source.size(); ++index)
        {
            state = state * 1664525u + 1013904223u;
            source[index] = static_cast<SampleT>(index < 16u ? index : (state >> 9) % kCodes);
        }
        const std::array<an::PointwiseImageView, 1u> sources{
            an::PointwiseImageView{.samples = source.data(), .layout = &layout}};
        an::EngineContext engine = makeEngine();
        std::vector<SampleT> oracle(source.size(), static_cast<SampleT>(9u));
        an::ExecutionContext reference = makeExecution(engine, an::ComputeBackend::Reference);
        const an::Result<an::PointwiseExecutionResult> expected = an::executePointwiseProgram(
            reference, *program, sources, an::PointwiseMutableImageView{.samples = oracle.data(), .layout = &layout},
            an::KernelValidityView{});
        ASSERT_TRUE(expected.hasValue()) << expected.status().message();
        std::vector<SampleT> actual(source.size(), static_cast<SampleT>(9u));
        an::ExecutionContext compiled = makeExecution(engine, an::ComputeBackend::Cpu);
        const an::Result<an::PointwiseExecutionResult> ran = an::executePointwiseProgram(
            compiled, *program, sources, an::PointwiseMutableImageView{.samples = actual.data(), .layout = &layout},
            an::KernelValidityView{});
        ASSERT_TRUE(ran.hasValue()) << ran.status().message();
        EXPECT_EQ(actual, oracle);
        EXPECT_GT(expected->conversion.saturatedSamples, 0u);
        EXPECT_EQ(expected->conversion.nonFiniteInputs > 0u, !wide);
        EXPECT_EQ(ran->conversion.saturatedSamples, expected->conversion.saturatedSamples);
        EXPECT_EQ(ran->conversion.nonFiniteInputs, expected->conversion.nonFiniteInputs);
        EXPECT_EQ(ran->conversion.samplesConverted, expected->conversion.samplesConverted);
        EXPECT_EQ(ran->run.outputsWritten, expected->run.outputsWritten);
        EXPECT_EQ(ran->run.selection, expected->run.selection);
    };
    for (const bool wide : {false, true})
    {
        SCOPED_TRACE(wide ? "Float64" : "Float32");
        run(std::uint8_t{}, an::SampleType::UInt8, wide);
        run(std::uint16_t{}, an::SampleType::UInt16, wide);
    }
}

/// The programs an integer image's blend and arithmetic are made of, at one width: the modulation's blend toward a
/// second image, the mask's blend as the process framework builds it - straight and inverted - and the arithmetic
/// steps, a comparison and a division by a third image that holds zeros, scaled so that results round half way and
/// saturate at both ends.
[[nodiscard]] std::vector<an::PointwiseProgram> integerPrograms(an::SampleType type, an::ComputePrecision precision,
                                                                double fullScale)
{
    const auto builder = [&](std::uint32_t sources) {
        an::PointwiseProgramBuilder made;
        made.channels(2u).sources(sources).readingSamples(type);
        if (precision == an::ComputePrecision::Float64)
        {
            const bool exactCodes = type == an::SampleType::UInt32 || type == an::SampleType::Int32;
            made.computingIn(an::ComputePrecision::Float64, exactCodes
                                                                ? an::Float64Justification::ExactIntegerCodeValues
                                                                : an::Float64Justification::CalibrationDivide);
            if (exactCodes)
            {
                made.promising(an::OutputPrecisionContract::ExactStoredValues);
            }
        }
        return made;
    };
    std::vector<an::PointwiseProgram> programs;
    const auto keep = [&](an::PointwiseProgramBuilder& made) {
        an::Result<an::PointwiseProgram> built = made.build();
        EXPECT_TRUE(built.hasValue()) << built.status().message();
        if (built.hasValue())
        {
            programs.push_back(std::move(*built));
        }
    };
    an::PointwiseProgramBuilder modulated = builder(2u);
    modulated.append(an::pointwiseBlend(1u, 1.0 - 0.37));
    keep(modulated);
    an::PointwiseProgramBuilder masked = builder(3u);
    masked.append(an::pointwiseSubtract(1u))
        .append(an::pointwiseScale(0.6))
        .append(an::pointwiseMask(2u))
        .append(an::pointwiseScale(1.0 / fullScale))
        .append(an::pointwiseAdd(1u));
    keep(masked);
    an::PointwiseProgramBuilder inverted = builder(3u);
    inverted.append(an::pointwiseSubtract(1u))
        .append(an::pointwiseMask(2u))
        .append(an::pointwiseScale(-1.0 / fullScale))
        .append(an::pointwiseAdd(1u))
        .append(an::pointwiseAdd(0u, 0.6))
        .append(an::pointwiseSubtract(1u, 0.6));
    keep(inverted);
    // The clamp comes before the division, so a quotient by zero reaches the narrowing infinite or not a number.
    an::PointwiseProgramBuilder arithmetic = builder(3u);
    arithmetic.append(an::pointwiseScale(2.5))
        .append(an::pointwiseBias(-0.25 * fullScale - 0.5))
        .append(an::pointwiseClamp(-2.0 * fullScale, 3.0 * fullScale))
        .append(an::pointwiseMultiply(1u, 1.0 / fullScale))
        .append(an::pointwiseDivide(2u, 0.5));
    keep(arithmetic);
    an::PointwiseProgramBuilder compared = builder(2u);
    compared.append(an::pointwiseCompare(1u, an::PointwiseComparison::LessOrEqual))
        .append(an::pointwiseScale(fullScale));
    keep(compared);
    return programs;
}

/// Random codes over a representation's whole range, with its ends, zero and the codes about zero among them.
template <typename SampleT> [[nodiscard]] std::vector<SampleT> integerCodes(std::size_t count, std::uint32_t seed)
{
    std::vector<SampleT> codes(count);
    std::uint32_t state = seed;
    const std::array<SampleT, 5u> special{(std::numeric_limits<SampleT>::lowest)(),
                                          (std::numeric_limits<SampleT>::max)(), SampleT{0}, SampleT{1},
                                          static_cast<SampleT>(SampleT{0} - SampleT{1})};
    for (std::size_t index = 0u; index < count; ++index)
    {
        const std::uint64_t wide =
            (static_cast<std::uint64_t>(nextGeneratedValue(state)) << 32u) | nextGeneratedValue(state);
        codes[index] = index % 7u == 3u ? special[(index / 7u) % special.size()] : static_cast<SampleT>(wide);
    }
    return codes;
}

/// Runs every integer program over one representation on the reference backend and on every runnable vector target,
/// into a separate destination and in place over the first source, and holds the codes and the counts equal.
template <typename SampleT>
void expectIntegerProgramsAgree(an::SampleType type, an::ComputePrecision precision,
                                const std::vector<an::InstructionSet>& targets)
{
    const double fullScale = static_cast<double>((std::numeric_limits<SampleT>::max)());
    an::EngineContext engine = makeEngine();
    for (const std::uint32_t padding : {0u, 3u})
    {
        constexpr std::uint32_t kWidth = 37u;
        constexpr std::uint32_t kRows = 9u;
        const an::ImageLayout layout = planarLayout(type, kWidth, kRows, 2u, padding);
        const std::size_t samples = layout.totalBytes() / sizeof(SampleT);
        std::vector<std::vector<SampleT>> inputs;
        for (std::uint32_t source = 0u; source < 3u; ++source)
        {
            inputs.push_back(integerCodes<SampleT>(samples, 0x9E3779B9u * (source + 1u) + padding));
        }
        // The divisor holds zeros, where a quotient is infinite or not a number.
        for (std::size_t index = 0u; index < samples; index += 5u)
        {
            inputs[2][index] = SampleT{0};
        }
        for (const an::PointwiseProgram& program : integerPrograms(type, precision, fullScale))
        {
            std::vector<an::PointwiseImageView> sources;
            for (std::uint32_t source = 0u; source < program.sourceCount(); ++source)
            {
                sources.push_back(an::PointwiseImageView{.samples = inputs[source].data(), .layout = &layout});
            }
            for (const bool inPlace : {false, true})
            {
                const auto runOn = [&](an::ExecutionContext& execution, std::vector<SampleT>& written) {
                    written = inPlace ? inputs[0] : std::vector<SampleT>(samples, static_cast<SampleT>(7));
                    std::vector<an::PointwiseImageView> bound = sources;
                    if (inPlace)
                    {
                        bound[0].samples = written.data();
                    }
                    return an::executePointwiseProgram(
                        execution, program, bound,
                        an::PointwiseMutableImageView{.samples = written.data(), .layout = &layout},
                        an::KernelValidityView{});
                };
                an::ExecutionContext reference = makeExecution(engine, an::ComputeBackend::Reference);
                std::vector<SampleT> oracle;
                const an::Result<an::PointwiseExecutionResult> expected = runOn(reference, oracle);
                ASSERT_TRUE(expected.hasValue()) << expected.status().message();
                for (const an::InstructionSet target : targets)
                {
                    SCOPED_TRACE(::testing::Message()
                                 << an::instructionSetName(target) << ", " << program.sourceCount()
                                 << " sources, padding " << padding << (inPlace ? ", in place" : ""));
                    an::ExecutionContext compiled = makeVectorExecution(engine, target);
                    std::vector<SampleT> actual;
                    const an::Result<an::PointwiseExecutionResult> ran = runOn(compiled, actual);
                    ASSERT_TRUE(ran.hasValue()) << ran.status().message();
                    std::size_t first = actual.size();
                    for (std::size_t index = 0u; index < actual.size(); ++index)
                    {
                        if (actual[index] != oracle[index])
                        {
                            first = index;
                            break;
                        }
                    }
                    EXPECT_EQ(first, actual.size())
                        << "the first sample written otherwise is at " << first << ": "
                        << static_cast<std::int64_t>(first < actual.size() ? actual[first] : SampleT{0})
                        << " where the evaluator wrote "
                        << static_cast<std::int64_t>(first < actual.size() ? oracle[first] : SampleT{0});
                    EXPECT_EQ(ran->conversion.saturatedSamples, expected->conversion.saturatedSamples);
                    EXPECT_EQ(ran->conversion.nonFiniteInputs, expected->conversion.nonFiniteInputs);
                    EXPECT_EQ(ran->conversion.samplesConverted, expected->conversion.samplesConverted);
                    EXPECT_EQ(ran->run.outputsWritten, expected->run.outputsWritten);
                    EXPECT_EQ(ran->run.selection, expected->run.selection);
                }
            }
        }
    }
}

// A program over integer samples - the blend a mask or a modulation applies to a camera frame - runs through the
// compiled run on every vector target, which widens every source to the program's width, evaluates the steps there and
// narrows the result as the evaluator does: every code and every count is the evaluator's, for every integer
// representation at each width it is compiled for, one to three sources, codes that round half way, saturate at either
// end or come from a division by zero, rows whose width leaves a partial vector, padded or not, in place or not.
TEST(PointwiseExecutionTest, AnIntegerImagesProgramWritesTheEvaluatorsCodesOnEveryVectorTarget)
{
    const std::vector<an::InstructionSet> targets = runnableVectorTargets();
    if (targets.empty())
    {
        GTEST_SKIP() << "this host can run no compiled vector target";
    }
    for (const an::ComputePrecision precision : {an::ComputePrecision::Float32, an::ComputePrecision::Float64})
    {
        SCOPED_TRACE(precision == an::ComputePrecision::Float64 ? "Float64" : "Float32");
        expectIntegerProgramsAgree<std::uint8_t>(an::SampleType::UInt8, precision, targets);
        expectIntegerProgramsAgree<std::int8_t>(an::SampleType::Int8, precision, targets);
        expectIntegerProgramsAgree<std::uint16_t>(an::SampleType::UInt16, precision, targets);
        expectIntegerProgramsAgree<std::int16_t>(an::SampleType::Int16, precision, targets);
    }
    expectIntegerProgramsAgree<std::uint32_t>(an::SampleType::UInt32, an::ComputePrecision::Float64, targets);
    expectIntegerProgramsAgree<std::int32_t>(an::SampleType::Int32, an::ComputePrecision::Float64, targets);
}

TEST(PointwiseExecutionTest, AnIntegerLookupOfATableInsideItsCodesNarrowsEveryCodeAsTheEvaluatorDoes)
{
    // A table whose every entry lies inside the representation is narrowed by the compiled conversion alone. Every
    // code's answer is the evaluator's at each width - an entry a hair above a half rounds up from Float64 and, once
    // narrowed to Float32, to even - and neither side counts a saturated or non-finite sample.
    const auto run = [](auto sample, an::SampleType sampleType, bool wide) {
        using SampleT = decltype(sample);
        constexpr std::uint32_t kCodes = std::uint32_t{1u} << (8u * sizeof(SampleT));
        const double largest = static_cast<double>(kCodes - 1u);
        constexpr std::array<double, 8u> kFractions{
            0.0, 0.5, 0.25, 0.75, 0.5 + 0x1.0p-30, 0.5 - 0x1.0p-30, 0.49999999999, 0.9999999};
        std::vector<double> table(kCodes, 0.0);
        for (std::uint32_t code = 0u; code < kCodes; ++code)
        {
            const double base = static_cast<double>((code * 40503u) % kCodes);
            table[code] = (std::min)(largest, base + kFractions[code % kFractions.size()]);
        }
        table[1] = largest;
        table[2] = largest - 0.5;
        table[3] = 0.5;
        table[4] = 1.5;
        an::PointwiseProgramBuilder builder;
        builder.channels(2u).readingSamples(sampleType);
        if (wide)
        {
            builder.computingIn(an::ComputePrecision::Float64, an::Float64Justification::CalibrationDivide);
        }
        builder.append(an::pointwiseLut(builder.addTable(table)));
        const an::Result<an::PointwiseProgram> program = builder.build();
        ASSERT_TRUE(program.hasValue()) << program.status().message();

        constexpr std::uint32_t kWidth = 256u;
        constexpr std::uint32_t kRows = kCodes / kWidth + 1u;
        constexpr std::uint32_t kPadding = 5u;
        const an::ImageLayout layout = planarLayout(sampleType, kWidth, kRows, 2u, kPadding);
        const std::size_t stride = kWidth + kPadding;
        std::vector<SampleT> source(stride * kRows * 2u, static_cast<SampleT>(13u));
        for (std::uint32_t plane = 0u; plane < 2u; ++plane)
        {
            for (std::uint32_t row = 0u; row < kRows; ++row)
            {
                for (std::uint32_t x = 0u; x < kWidth; ++x)
                {
                    source[(plane * kRows + row) * stride + x] =
                        static_cast<SampleT>((row * kWidth + x + plane * 12345u) % kCodes);
                }
            }
        }
        const std::array<an::PointwiseImageView, 1u> sources{
            an::PointwiseImageView{.samples = source.data(), .layout = &layout}};
        an::EngineContext engine = makeEngine();
        std::vector<SampleT> oracle(source.size(), static_cast<SampleT>(9u));
        an::ExecutionContext reference = makeExecution(engine, an::ComputeBackend::Reference);
        const an::Result<an::PointwiseExecutionResult> expected = an::executePointwiseProgram(
            reference, *program, sources, an::PointwiseMutableImageView{.samples = oracle.data(), .layout = &layout},
            an::KernelValidityView{});
        ASSERT_TRUE(expected.hasValue()) << expected.status().message();
        std::vector<SampleT> actual(source.size(), static_cast<SampleT>(9u));
        an::ExecutionContext compiled = makeExecution(engine, an::ComputeBackend::Cpu);
        const an::Result<an::PointwiseExecutionResult> ran = an::executePointwiseProgram(
            compiled, *program, sources, an::PointwiseMutableImageView{.samples = actual.data(), .layout = &layout},
            an::KernelValidityView{});
        ASSERT_TRUE(ran.hasValue()) << ran.status().message();
        EXPECT_EQ(actual, oracle);
        EXPECT_EQ(expected->conversion.saturatedSamples, 0u);
        EXPECT_EQ(expected->conversion.nonFiniteInputs, 0u);
        EXPECT_EQ(ran->conversion.saturatedSamples, 0u);
        EXPECT_EQ(ran->conversion.nonFiniteInputs, 0u);
        EXPECT_EQ(ran->conversion.samplesConverted, expected->conversion.samplesConverted);
        EXPECT_EQ(ran->run.outputsWritten, expected->run.outputsWritten);
    };
    for (const bool wide : {false, true})
    {
        SCOPED_TRACE(wide ? "Float64" : "Float32");
        run(std::uint8_t{}, an::SampleType::UInt8, wide);
        run(std::uint16_t{}, an::SampleType::UInt16, wide);
    }
}
