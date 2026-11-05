// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the power, gamma, spline, curve and LUT planners.

#include <AfterNightEngine/Algorithms/Pointwise/PointwiseEvaluator.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseExecution.h>
#include <AfterNightEngine/Algorithms/Pointwise/Arithmetic.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseCanonicalization.h>
#include <AfterNightEngine/Algorithms/Pointwise/ToneCurve.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
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
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace
{

[[nodiscard]] an::Result<double> evaluate(const an::PointwiseProgram& program, double input)
{
    const std::array<double, 1u> sources{input};
    std::array<double, 1u> output{};
    const an::Status status = an::evaluatePointwisePixel(program, sources, output);
    if (status.failed())
    {
        return status;
    }
    return output[0];
}

[[nodiscard]] an::TonePlanShape floatingTone()
{
    return an::TonePlanShape{};
}

/// An execution of a context of four threads on one backend and instruction set, two of them admitted.
[[nodiscard]] std::pair<an::EngineContext, an::ExecutionContext>
curveExecution(an::ComputeBackend backend, an::InstructionSet instructionSet = an::InstructionSet::Automatic)
{
    an::EngineContextOptions engineOptions;
    engineOptions.maxConcurrency = 4u;
    an::Result<an::EngineContext> engine = an::EngineContext::create(engineOptions);
    EXPECT_TRUE(engine.hasValue()) << engine.status().message();
    an::ExecutionOptions options;
    options.maxConcurrency = 2u;
    options.backend = backend;
    options.instructionSet = instructionSet;
    options.backendFallback = an::BackendFallback::Forbidden;
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, options);
    EXPECT_TRUE(execution.hasValue()) << execution.status().message();
    return {std::move(*engine), std::move(*execution)};
}

/// The compiled vector targets this host runs.
[[nodiscard]] std::vector<an::InstructionSet> runnableCurveTargets()
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

/// Whether two plans are the same bits: every table entry, the distance reported and the entry count.
void expectSamePlan(const an::Result<an::CurveProgramPlan>& expected, const an::Result<an::CurveProgramPlan>& actual,
                    const std::string& what)
{
    ASSERT_EQ(expected.hasValue(), actual.hasValue()) << what;
    if (!expected.hasValue())
    {
        EXPECT_EQ(expected.status().code(), actual.status().code()) << what;
        return;
    }
    EXPECT_EQ(std::bit_cast<std::uint64_t>(expected->maximumAbsoluteError()),
              std::bit_cast<std::uint64_t>(actual->maximumAbsoluteError()))
        << what << ": " << expected->maximumAbsoluteError() << " against " << actual->maximumAbsoluteError();
    EXPECT_EQ(expected->tableEntries(), actual->tableEntries()) << what;
    ASSERT_EQ(expected->program().tableCount(), actual->program().tableCount()) << what;
    for (std::uint32_t table = 0u; table < expected->program().tableCount(); ++table)
    {
        const std::span<const double> left = expected->program().table(table);
        const std::span<const double> right = actual->program().table(table);
        ASSERT_EQ(left.size(), right.size()) << what;
        for (std::size_t index = 0u; index < left.size(); ++index)
        {
            ASSERT_EQ(std::bit_cast<std::uint64_t>(left[index]), std::bit_cast<std::uint64_t>(right[index]))
                << what << " entry " << index << ": " << left[index] << " against " << right[index];
        }
    }
}

constexpr std::array<an::CurveControlPoint, 3u> kCurve{
    an::CurveControlPoint{.input = 0.0, .output = 0.0},
    an::CurveControlPoint{.input = 0.5, .output = 0.25},
    an::CurveControlPoint{.input = 1.0, .output = 1.0},
};

} // namespace

TEST(ToneCurveTest, GammaDirectionChecksDomainsAndNamesItsMathProvider)
{
    an::TonePlanShape encode = floatingTone();
    const an::Result<an::PowerProgramPlan> encoded = an::buildGammaProgram(encode, 2.0, an::GammaDirection::Encode);
    ASSERT_TRUE(encoded.hasValue()) << encoded.status().message();
    EXPECT_EQ(encoded->provider(), an::MathProvider::VectorApproximation);
    EXPECT_EQ(encoded->program().inputDomain(), an::NumericDomain::NormalizedLinear);
    EXPECT_EQ(encoded->program().outputDomain(), an::NumericDomain::EncodedNonlinear);
    ASSERT_TRUE(evaluate(encoded->program(), 0.25).hasValue());
    EXPECT_NEAR(*evaluate(encoded->program(), 0.25), 0.5, 1.0e-6);

    an::TonePlanShape decode = floatingTone();
    decode.inputDomain = an::NumericDomain::EncodedNonlinear;
    decode.outputDomain = an::NumericDomain::NormalizedLinear;
    const an::Result<an::PowerProgramPlan> decoded = an::buildGammaProgram(decode, 2.0, an::GammaDirection::Decode);
    ASSERT_TRUE(decoded.hasValue());
    EXPECT_NEAR(*evaluate(decoded->program(), 0.5), 0.25, 1.0e-6);

    const an::Result<an::PowerProgramPlan> mismatch = an::buildGammaProgram(encode, 2.0, an::GammaDirection::Decode);
    ASSERT_FALSE(mismatch.hasValue());
    EXPECT_EQ(mismatch.status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(an::buildGammaProgram(encode, 0.0, an::GammaDirection::Encode).status().code(),
              an::ErrorCode::InvalidArgument);
}

TEST(ToneCurveTest, IntegerPowerUsesOneExactEntryPerResidentCode)
{
    an::TonePlanShape shape;
    shape.sampleType = an::SampleType::UInt8;
    const an::Result<an::PowerProgramPlan> plan = an::buildPowerProgram(shape, 2.0);
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();
    EXPECT_EQ(plan->provider(), an::MathProvider::ExactTable);
    EXPECT_EQ(plan->program().table(0u).size(), 256u);
    EXPECT_DOUBLE_EQ(*evaluate(plan->program(), 0.0), 0.0);
    EXPECT_DOUBLE_EQ(*evaluate(plan->program(), 255.0), 255.0);
    EXPECT_NEAR(*evaluate(plan->program(), 128.0), 64.25098, 1.0e-4);

    const an::Result<an::PowerProgramPlan> refused = an::buildPowerProgram(shape, -1.0);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::Unsupported);
}

TEST(ToneCurveTest, RealtimePreviewGammaUsesACoarseLookupWithoutChangingProduction)
{
    const an::TonePlanShape shape = floatingTone();
    const an::Result<an::PowerProgramPlan> production = an::buildGammaProgram(shape, 2.0, an::GammaDirection::Encode);
    const an::Result<an::RealtimePreviewGammaProgramPlan> preview =
        an::buildRealtimePreviewGammaProgram(shape, 2.0, an::GammaDirection::Encode);
    ASSERT_TRUE(production.hasValue()) << production.status().message();
    ASSERT_TRUE(preview.hasValue()) << preview.status().message();

    EXPECT_EQ(production->provider(), an::MathProvider::VectorApproximation);
    EXPECT_EQ(preview->tableEntries(), an::kRealtimePreviewGammaTableEntries);
    EXPECT_GT(preview->maximumAbsoluteError(), 0.0);
    EXPECT_LT(preview->maximumAbsoluteError(), 0.04);

    constexpr double input = 0.2509;
    const double maximum = static_cast<double>(an::kRealtimePreviewGammaTableEntries - 1u);
    const std::uint32_t index = static_cast<std::uint32_t>(input * maximum);
    const float looked = static_cast<float>(std::floor(std::pow(static_cast<double>(index) / maximum, 0.5) * maximum)) /
                         static_cast<float>(maximum);
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(1u, 1u, 1u, an::defaultEngineLimits());
    ASSERT_TRUE(extent.has_value());
    const std::optional<an::ImageLayout> layout =
        an::ImageLayout::create(an::ImageLayoutRequest{.extent = *extent,
                                                       .elementSizeBytes = sizeof(float),
                                                       .samplesPerPixel = 1u,
                                                       .planeCount = 1u,
                                                       .rowAlignmentBytes = 1u},
                                an::defaultEngineLimits());
    ASSERT_TRUE(layout.has_value());
    an::EngineContextOptions engineOptions;
    engineOptions.maxConcurrency = 1u;
    an::Result<an::EngineContext> engine = an::EngineContext::create(engineOptions);
    ASSERT_TRUE(engine.hasValue()) << engine.status().message();
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, an::ExecutionOptions{});
    ASSERT_TRUE(execution.hasValue()) << execution.status().message();
    const std::array<float, 1u> source{static_cast<float>(input)};
    std::array<float, 1u> destination{};
    const an::Result<an::PointwiseExecutionResult> ran = an::executeRealtimePreviewGamma(
        *execution, *preview, an::PointwiseImageView{.samples = source.data(), .layout = &*layout},
        an::PointwiseMutableImageView{.samples = destination.data(), .layout = &*layout}, an::KernelValidityView{});
    ASSERT_TRUE(ran.hasValue()) << ran.status().message();
    EXPECT_FLOAT_EQ(destination.front(), looked);
    EXPECT_NE(destination.front(), static_cast<float>(*evaluate(production->program(), input)));

    an::TonePlanShape integer = shape;
    integer.sampleType = an::SampleType::UInt16;
    EXPECT_EQ(an::buildRealtimePreviewGammaProgram(integer, 2.0, an::GammaDirection::Encode).status().code(),
              an::ErrorCode::IncompatibleArguments);
}

TEST(ToneCurveTest, RealtimePreviewGammaReportsItsLargestDistanceFromTheCurveExactly)
{
    // The report's definition, written out: every interval of the lower-neighbour table probed at
    // eighths against the analytic transfer. The plan has to report exactly what that finds.
    constexpr std::uint32_t entries = an::kRealtimePreviewGammaTableEntries;
    const double maximumCode = static_cast<double>(entries - 1u);
    for (const an::SampleType sampleType : {an::SampleType::Float32, an::SampleType::Float64})
    {
        for (const an::GammaDirection direction : {an::GammaDirection::Encode, an::GammaDirection::Decode})
        {
            for (const double gamma : {1.0e-3, 0.25, 0.5, 1.0, 1.8, 2.2, 3.0, 10.0, 1.0e3})
            {
                an::TonePlanShape shape = floatingTone();
                shape.sampleType = sampleType;
                if (direction == an::GammaDirection::Decode)
                {
                    shape.inputDomain = an::NumericDomain::EncodedNonlinear;
                    shape.outputDomain = an::NumericDomain::NormalizedLinear;
                }
                const an::Result<an::RealtimePreviewGammaProgramPlan> plan =
                    an::buildRealtimePreviewGammaProgram(shape, gamma, direction);
                ASSERT_TRUE(plan.hasValue()) << plan.status().message();
                const double exponent = direction == an::GammaDirection::Encode ? 1.0 / gamma : gamma;
                double expected = 0.0;
                for (std::uint32_t interval = 0u; interval + 1u < entries; ++interval)
                {
                    const double entry =
                        std::floor(std::pow(static_cast<double>(interval) / maximumCode, exponent) * maximumCode) /
                        maximumCode;
                    for (std::uint32_t probe = 0u; probe <= 8u; ++probe)
                    {
                        const double input =
                            (static_cast<double>(interval) + static_cast<double>(probe) / 8.0) / maximumCode;
                        expected = (std::max)(expected, std::abs(entry - std::pow(input, exponent)));
                    }
                }
                EXPECT_EQ(plan->maximumAbsoluteError(), expected)
                    << "gamma " << gamma << (direction == an::GammaDirection::Encode ? " encode " : " decode ")
                    << (sampleType == an::SampleType::Float32 ? "Float32" : "Float64");
            }
        }
    }
}

TEST(ToneCurveTest, RealtimePreviewGammaHoldsEveryEntryOfTheExactPower)
{
    // Every entry is the curve at its interval's start, from the exact power, rounded down to a multiple of one over
    // the largest code, and the reported distance is the exact power's - whatever evaluated the curve. Twenty-four
    // exponents an octave from 1/64 to 64 in both directions, each table read back at the middle of every interval.
    constexpr std::uint32_t kEntries = an::kRealtimePreviewGammaTableEntries;
    const double maximumCode = static_cast<double>(kEntries - 1u);
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(kEntries, 1u, 1u, an::defaultEngineLimits());
    ASSERT_TRUE(extent.has_value());
    const std::optional<an::ImageLayout> layout =
        an::ImageLayout::create(an::ImageLayoutRequest{.extent = *extent,
                                                       .elementSizeBytes = sizeof(double),
                                                       .samplesPerPixel = 1u,
                                                       .planeCount = 1u,
                                                       .rowAlignmentBytes = 1u},
                                an::defaultEngineLimits());
    ASSERT_TRUE(layout.has_value());
    an::EngineContextOptions engineOptions;
    engineOptions.maxConcurrency = 1u;
    an::Result<an::EngineContext> engine = an::EngineContext::create(engineOptions);
    ASSERT_TRUE(engine.hasValue()) << engine.status().message();
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, an::ExecutionOptions{});
    ASSERT_TRUE(execution.hasValue()) << execution.status().message();
    std::vector<double> source(kEntries, 0.0);
    for (std::uint32_t index = 0u; index < kEntries; ++index)
    {
        source[index] = (static_cast<double>(index) + 0.5) / maximumCode;
    }
    std::vector<double> destination(kEntries, -1.0);
    for (std::uint32_t step = 0u; step <= 12u * 24u; ++step)
    {
        const double gamma = std::pow(2.0, -6.0 + static_cast<double>(step) / 24.0);
        for (const an::GammaDirection direction : {an::GammaDirection::Encode, an::GammaDirection::Decode})
        {
            an::TonePlanShape shape = floatingTone();
            shape.sampleType = an::SampleType::Float64;
            if (direction == an::GammaDirection::Decode)
            {
                shape.inputDomain = an::NumericDomain::EncodedNonlinear;
                shape.outputDomain = an::NumericDomain::NormalizedLinear;
            }
            const an::Result<an::RealtimePreviewGammaProgramPlan> plan =
                an::buildRealtimePreviewGammaProgram(shape, gamma, direction);
            ASSERT_TRUE(plan.hasValue()) << plan.status().message();
            const an::Result<an::PointwiseExecutionResult> ran = an::executeRealtimePreviewGamma(
                *execution, *plan, an::PointwiseImageView{.samples = source.data(), .layout = &*layout},
                an::PointwiseMutableImageView{.samples = destination.data(), .layout = &*layout},
                an::KernelValidityView{});
            ASSERT_TRUE(ran.hasValue()) << ran.status().message();
            const double exponent = direction == an::GammaDirection::Encode ? 1.0 / gamma : gamma;
            double previous = 0.0;
            double largest = 0.0;
            for (std::uint32_t index = 0u; index < kEntries; ++index)
            {
                const double curve = std::pow(static_cast<double>(index) / maximumCode, exponent);
                const double entry = std::floor(curve * maximumCode) / maximumCode;
                // The preview applies its table in Float32; an entry rounded from another floor is a whole code away.
                ASSERT_EQ(std::bit_cast<std::uint32_t>(static_cast<float>(destination[index])),
                          std::bit_cast<std::uint32_t>(static_cast<float>(entry)))
                    << "gamma " << gamma << (direction == an::GammaDirection::Encode ? " encode" : " decode")
                    << ", entry " << index;
                if (index != 0u)
                {
                    largest = (std::max)(largest, std::abs(previous - curve));
                }
                previous = entry;
            }
            EXPECT_EQ(std::bit_cast<std::uint64_t>(plan->maximumAbsoluteError()), std::bit_cast<std::uint64_t>(largest))
                << "gamma " << gamma << (direction == an::GammaDirection::Encode ? " encode" : " decode");
        }
    }
}

TEST(ToneCurveTest, GeneralTonePlansRefuseThePreviewOnlyLowerSamplingRule)
{
    constexpr std::array<double, 3u> entries{0.0, 0.5, 1.0};
    an::LookupProgramRequest request;
    request.entries = entries;
    request.sampling = an::PointwiseCurveSampling::Lower;
    EXPECT_EQ(an::buildLookupProgram(floatingTone(), request).status().code(), an::ErrorCode::IncompatibleArguments);
}

TEST(ToneCurveTest, RealtimePreviewGammaSafelyPreservesNanAndClampsInfinities)
{
    constexpr std::uint32_t kWidth = 65u;
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(kWidth, 1u, 1u, an::defaultEngineLimits());
    ASSERT_TRUE(extent.has_value());
    const std::optional<an::ImageLayout> layout =
        an::ImageLayout::create(an::ImageLayoutRequest{.extent = *extent,
                                                       .elementSizeBytes = sizeof(float),
                                                       .samplesPerPixel = 1u,
                                                       .planeCount = 1u,
                                                       .rowAlignmentBytes = 1u},
                                an::defaultEngineLimits());
    ASSERT_TRUE(layout.has_value());

    an::EngineContextOptions engineOptions;
    engineOptions.maxConcurrency = 1u;
    an::Result<an::EngineContext> engine = an::EngineContext::create(engineOptions);
    ASSERT_TRUE(engine.hasValue()) << engine.status().message();
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, an::ExecutionOptions{});
    ASSERT_TRUE(execution.hasValue()) << execution.status().message();
    const an::Result<an::RealtimePreviewGammaProgramPlan> plan =
        an::buildRealtimePreviewGammaProgram(floatingTone(), 2.0, an::GammaDirection::Encode);
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();

    std::array<float, kWidth> source{};
    source.fill(0.5f);
    source[0] = std::numeric_limits<float>::quiet_NaN();
    source[1] = -std::numeric_limits<float>::infinity();
    source[2] = std::numeric_limits<float>::infinity();
    source[3] = -1.0f;
    source[4] = 2.0f;
    std::array<float, kWidth> destination{};
    const an::Result<an::PointwiseExecutionResult> ran = an::executeRealtimePreviewGamma(
        *execution, *plan, an::PointwiseImageView{.samples = source.data(), .layout = &*layout},
        an::PointwiseMutableImageView{.samples = destination.data(), .layout = &*layout}, an::KernelValidityView{});
    ASSERT_TRUE(ran.hasValue()) << ran.status().message();
    EXPECT_TRUE(std::isnan(destination[0]));
    EXPECT_FLOAT_EQ(destination[1], 0.0f);
    EXPECT_FLOAT_EQ(destination[2], 1.0f);
    EXPECT_FLOAT_EQ(destination[3], 0.0f);
    EXPECT_FLOAT_EQ(destination[4], 1.0f);
    EXPECT_EQ(ran->conversion.nonFiniteInputs, 3u);
}

TEST(ToneCurveTest, RealtimePreviewGammaWritesEverySampleOfAFrameTooLargeToStayCached)
{
    // Above 32 MiB the lookup writes with streaming stores. An odd sample count split among four
    // workers starts most runs off a vector boundary, so each run's head, body and tail are reached,
    // apart and in place, and every sample is held to the table the plan is defined by.
    constexpr std::uint32_t kSide = 3001u;
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(kSide, kSide, 1u, an::defaultEngineLimits());
    ASSERT_TRUE(extent.has_value());
    const std::optional<an::ImageLayout> layout =
        an::ImageLayout::create(an::ImageLayoutRequest{.extent = *extent,
                                                       .elementSizeBytes = sizeof(float),
                                                       .samplesPerPixel = 1u,
                                                       .planeCount = 1u,
                                                       .rowAlignmentBytes = 1u},
                                an::defaultEngineLimits());
    ASSERT_TRUE(layout.has_value());
    ASSERT_GE(layout->totalBytes(), std::uint64_t{32u} << 20u);
    an::EngineContextOptions engineOptions;
    engineOptions.maxConcurrency = 4u;
    an::Result<an::EngineContext> engine = an::EngineContext::create(engineOptions);
    ASSERT_TRUE(engine.hasValue()) << engine.status().message();
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, an::ExecutionOptions{});
    ASSERT_TRUE(execution.hasValue()) << execution.status().message();
    const an::Result<an::RealtimePreviewGammaProgramPlan> plan =
        an::buildRealtimePreviewGammaProgram(floatingTone(), 2.2, an::GammaDirection::Encode);
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();

    const double maximumCode = static_cast<double>(an::kRealtimePreviewGammaTableEntries - 1u);
    std::vector<float> table(an::kRealtimePreviewGammaTableEntries);
    for (std::size_t index = 0u; index < table.size(); ++index)
    {
        table[index] = static_cast<float>(
            std::floor(std::pow(static_cast<double>(index) / maximumCode, 1.0 / 2.2) * maximumCode) / maximumCode);
    }
    const std::size_t count = static_cast<std::size_t>(kSide) * kSide;
    std::vector<float> source(count);
    std::uint64_t nonFinite = 0u;
    constexpr std::array<float, 6u> kSpecial{std::numeric_limits<float>::quiet_NaN(),
                                             -std::numeric_limits<float>::infinity(),
                                             std::numeric_limits<float>::infinity(),
                                             -0.5f,
                                             1.5f,
                                             -0.0f};
    for (std::size_t index = 0u; index < count; ++index)
    {
        source[index] =
            index % 13u == 0u ? kSpecial[(index / 13u) % kSpecial.size()] : static_cast<float>(index % 4093u) / 4092.0f;
        nonFinite += std::isfinite(source[index]) ? 0u : 1u;
    }
    const auto expected = [&](float value) {
        if (std::isnan(value))
        {
            return std::numeric_limits<float>::quiet_NaN();
        }
        if (!(value >= 0.0f && value <= 1.0f))
        {
            return value < 0.0f ? table.front() : table.back();
        }
        return table[static_cast<std::size_t>(value * static_cast<float>(maximumCode))];
    };
    const auto mismatches = [&](const std::vector<float>& produced, std::string& first) {
        std::size_t wrong = 0u;
        for (std::size_t index = 0u; index < count; ++index)
        {
            const float want = expected(source[index]);
            const bool same = std::isnan(want) ? std::isnan(produced[index]) : produced[index] == want;
            if (!same && wrong++ == 0u)
            {
                first = "sample " + std::to_string(index) + " of " + std::to_string(source[index]) + ": " +
                        std::to_string(produced[index]) + " where the table gives " + std::to_string(want);
            }
        }
        return wrong;
    };

    std::vector<float> destination(count, -1.0f);
    const an::Result<an::PointwiseExecutionResult> apart = an::executeRealtimePreviewGamma(
        *execution, *plan, an::PointwiseImageView{.samples = source.data(), .layout = &*layout},
        an::PointwiseMutableImageView{.samples = destination.data(), .layout = &*layout}, an::KernelValidityView{});
    ASSERT_TRUE(apart.hasValue()) << apart.status().message();
    EXPECT_EQ(apart->conversion.nonFiniteInputs, nonFinite);
    std::string first;
    EXPECT_EQ(mismatches(destination, first), 0u) << first;

    std::vector<float> inPlace = source;
    const an::Result<an::PointwiseExecutionResult> overwritten = an::executeRealtimePreviewGamma(
        *execution, *plan, an::PointwiseImageView{.samples = inPlace.data(), .layout = &*layout},
        an::PointwiseMutableImageView{.samples = inPlace.data(), .layout = &*layout}, an::KernelValidityView{});
    ASSERT_TRUE(overwritten.hasValue()) << overwritten.status().message();
    EXPECT_EQ(overwritten->conversion.nonFiniteInputs, nonFinite);
    EXPECT_EQ(mismatches(inPlace, first), 0u) << first;
}

TEST(ToneCurveTest, CurvePoliciesCloseTheOutOfRangeLookupDefect)
{
    EXPECT_DOUBLE_EQ(*an::evaluateCurve(kCurve, an::CurveInterpolation::Linear, an::CurveDomainPolicy::Clamp, -10.0),
                     0.0);
    EXPECT_DOUBLE_EQ(*an::evaluateCurve(kCurve, an::CurveInterpolation::Linear, an::CurveDomainPolicy::Clamp, 10.0),
                     1.0);
    EXPECT_DOUBLE_EQ(
        *an::evaluateCurve(kCurve, an::CurveInterpolation::Linear, an::CurveDomainPolicy::LinearExtrapolation, -0.5),
        -0.25);
    EXPECT_DOUBLE_EQ(*an::evaluateCurve(kCurve, an::CurveInterpolation::MonotoneCubic,
                                        an::CurveDomainPolicy::LinearExtrapolation, -0.5),
                     -0.25);
    const an::Result<double> rejected =
        an::evaluateCurve(kCurve, an::CurveInterpolation::Linear, an::CurveDomainPolicy::Reject, 1.01);
    ASSERT_FALSE(rejected.hasValue());
    EXPECT_EQ(rejected.status().code(), an::ErrorCode::OutOfRange);

    const std::array<double, 3u> entries{0.0, 0.5, 1.0};
    an::LookupProgramRequest request;
    request.entries = entries;
    request.domainPolicy = an::CurveDomainPolicy::Clamp;
    const an::Result<an::CurveProgramPlan> clamped = an::buildLookupProgram(floatingTone(), request);
    ASSERT_TRUE(clamped.hasValue()) << clamped.status().message();
    EXPECT_DOUBLE_EQ(*evaluate(clamped->program(), -100.0), 0.0);
    EXPECT_DOUBLE_EQ(*evaluate(clamped->program(), 100.0), 1.0);
    EXPECT_DOUBLE_EQ(*evaluate(clamped->program(), -std::numeric_limits<double>::infinity()), 0.0);
    EXPECT_DOUBLE_EQ(*evaluate(clamped->program(), std::numeric_limits<double>::infinity()), 1.0);

    request.domainPolicy = an::CurveDomainPolicy::Reject;
    const an::Result<an::CurveProgramPlan> rejecting = an::buildLookupProgram(floatingTone(), request);
    ASSERT_TRUE(rejecting.hasValue());
    const an::Result<double> outside = evaluate(rejecting->program(), -0.01);
    ASSERT_FALSE(outside.hasValue());
    EXPECT_EQ(outside.status().code(), an::ErrorCode::OutOfRange);

    request.domainPolicy = an::CurveDomainPolicy::LinearExtrapolation;
    const an::Result<an::CurveProgramPlan> extrapolating = an::buildLookupProgram(floatingTone(), request);
    ASSERT_TRUE(extrapolating.hasValue());
    EXPECT_TRUE(std::isinf(*evaluate(extrapolating->program(), std::numeric_limits<double>::infinity())));
}

TEST(ToneCurveTest, MonotoneSplinePassesThroughPointsAndSampledPlanReportsMeasuredError)
{
    for (const an::CurveControlPoint& point : kCurve)
    {
        const an::Result<double> value =
            an::evaluateCurve(kCurve, an::CurveInterpolation::MonotoneCubic, an::CurveDomainPolicy::Clamp, point.input);
        ASSERT_TRUE(value.hasValue());
        EXPECT_DOUBLE_EQ(*value, point.output);
    }

    an::CurveProgramRequest request;
    request.points = kCurve;
    request.interpolation = an::CurveInterpolation::MonotoneCubic;
    request.approximation = an::CurveApproximation::SampledLinear;
    request.tableEntries = 17u;
    const an::Result<an::CurveProgramPlan> plan = an::buildCurveProgram(floatingTone(), request);
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();
    EXPECT_EQ(plan->approximation(), an::CurveApproximation::SampledLinear);
    EXPECT_EQ(plan->tableEntries(), 17u);
    EXPECT_GT(plan->maximumAbsoluteError(), 0.0);
    EXPECT_LT(plan->maximumAbsoluteError(), 0.003);
    for (const double input : {0.03, 0.18, 0.63, 0.91})
    {
        const an::Result<double> expected =
            an::evaluateCurve(kCurve, an::CurveInterpolation::MonotoneCubic, an::CurveDomainPolicy::Clamp, input);
        ASSERT_TRUE(expected.hasValue());
        ASSERT_TRUE(evaluate(plan->program(), input).hasValue());
        EXPECT_LE(std::abs(*evaluate(plan->program(), input) - *expected), plan->maximumAbsoluteError() + 1.0e-12);
    }
}

TEST(ToneCurveTest, ExactIntegerCurveAndCallerLookupNeverIndexPastTheirTables)
{
    an::TonePlanShape shape;
    shape.sampleType = an::SampleType::UInt8;
    an::CurveProgramRequest curve;
    curve.points = kCurve;
    curve.interpolation = an::CurveInterpolation::Linear;
    curve.approximation = an::CurveApproximation::ExactAtResidentValues;
    const an::Result<an::CurveProgramPlan> exact = an::buildCurveProgram(shape, curve);
    ASSERT_TRUE(exact.hasValue()) << exact.status().message();
    EXPECT_EQ(exact->tableEntries(), 256u);
    EXPECT_DOUBLE_EQ(exact->maximumAbsoluteError(), 0.0);
    EXPECT_DOUBLE_EQ(*evaluate(exact->program(), 255.0), 255.0);

    std::vector<double> lookup(256u);
    for (std::size_t index = 0u; index < lookup.size(); ++index)
    {
        lookup[index] = static_cast<double>(lookup.size() - index - 1u) / 255.0;
    }
    an::LookupProgramRequest request;
    request.entries = lookup;
    const an::Result<an::CurveProgramPlan> table = an::buildLookupProgram(shape, request);
    ASSERT_TRUE(table.hasValue());
    EXPECT_DOUBLE_EQ(*evaluate(table->program(), 0.0), 255.0);
    EXPECT_DOUBLE_EQ(*evaluate(table->program(), 255.0), 0.0);
}

TEST(ToneCurveTest, MalformedCurvesAndRawDomainsAreRefusedDuringPlanning)
{
    const std::array<an::CurveControlPoint, 2u> reversed{
        an::CurveControlPoint{.input = 1.0, .output = 0.0},
        an::CurveControlPoint{.input = 0.0, .output = 1.0},
    };
    an::CurveProgramRequest request;
    request.points = reversed;
    EXPECT_EQ(an::buildCurveProgram(floatingTone(), request).status().code(), an::ErrorCode::InvalidArgument);

    an::TonePlanShape raw = floatingTone();
    raw.inputDomain = an::NumericDomain::RawSensorCode;
    EXPECT_EQ(an::buildPowerProgram(raw, 2.0).status().code(), an::ErrorCode::IncompatibleArguments);
}

// A gamma is meant for a non-negative base, and a negative one that reaches it anyway is not a number on every path:
// the compiled run's vector part, its tail and the evaluator a validity rule sends a row through. At an exponent that
// is a whole number std::pow answers a real power, and a row once came out not-a-number in its vector part and finite
// in its tail.
TEST(ToneCurveTest, AGammaAnswersANegativeBaseNotANumberInEveryColumnOnEveryPath)
{
    constexpr std::uint32_t kWidth = 65u;
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(kWidth, 1u, 1u, an::defaultEngineLimits());
    ASSERT_TRUE(extent.has_value());
    const std::optional<an::ImageLayout> layout =
        an::ImageLayout::create(an::ImageLayoutRequest{.extent = *extent,
                                                       .elementSizeBytes = sizeof(float),
                                                       .samplesPerPixel = 1u,
                                                       .planeCount = 1u,
                                                       .rowAlignmentBytes = 1u},
                                an::defaultEngineLimits());
    ASSERT_TRUE(layout.has_value());
    an::EngineContextOptions engineOptions;
    engineOptions.maxConcurrency = 1u;
    an::Result<an::EngineContext> engine = an::EngineContext::create(engineOptions);
    ASSERT_TRUE(engine.hasValue()) << engine.status().message();
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, an::ExecutionOptions{});
    ASSERT_TRUE(execution.hasValue()) << execution.status().message();

    // Encoding with a gamma of one half raises to the power two.
    const an::Result<an::PowerProgramPlan> plan =
        an::buildGammaProgram(floatingTone(), 0.5, an::GammaDirection::Encode);
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();
    const an::Result<double> evaluated = evaluate(plan->program(), -0.5);
    ASSERT_TRUE(evaluated.hasValue());
    EXPECT_TRUE(std::isnan(*evaluated)) << *evaluated;

    std::array<float, kWidth> source{};
    source.fill(-0.5f);
    for (const an::ValidityRule rule : {an::ValidityRule::AllValid, an::ValidityRule::FiniteOnly})
    {
        const std::optional<an::ValidityDescriptor> validity = an::ValidityDescriptor::create({.rule = rule});
        ASSERT_TRUE(validity.has_value());
        std::array<float, kWidth> destination{};
        const std::array<an::PointwiseImageView, 1u> sources{
            an::PointwiseImageView{.samples = source.data(), .layout = &*layout}};
        const an::Result<an::PointwiseExecutionResult> ran = an::executePointwiseProgram(
            *execution, plan->program(), sources,
            an::PointwiseMutableImageView{.samples = destination.data(), .layout = &*layout},
            an::KernelValidityView{.descriptor = *validity});
        ASSERT_TRUE(ran.hasValue()) << ran.status().message();
        for (std::uint32_t column = 0u; column < kWidth; ++column)
        {
            EXPECT_TRUE(std::isnan(destination[column]))
                << "rule " << static_cast<int>(rule) << ", column " << column << ": " << destination[column];
        }
    }
}

TEST(ToneCurveTest, GammaCurveAndClampFuseIntoOneCheckedProgram)
{
    const an::Result<an::PowerProgramPlan> gamma =
        an::buildGammaProgram(floatingTone(), 2.0, an::GammaDirection::Encode);
    ASSERT_TRUE(gamma.hasValue());

    an::TonePlanShape curveShape = floatingTone();
    curveShape.inputDomain = an::NumericDomain::EncodedNonlinear;
    curveShape.outputDomain = an::NumericDomain::EncodedNonlinear;
    an::CurveProgramRequest curveRequest;
    curveRequest.points = kCurve;
    curveRequest.interpolation = an::CurveInterpolation::Linear;
    curveRequest.approximation = an::CurveApproximation::SampledLinear;
    curveRequest.tableEntries = 17u;
    const an::Result<an::CurveProgramPlan> curve = an::buildCurveProgram(curveShape, curveRequest);
    ASSERT_TRUE(curve.hasValue());

    an::PointwisePlanShape clampShape;
    clampShape.numericDomain = an::NumericDomain::EncodedNonlinear;
    const an::Result<an::PointwiseProgram> clamp =
        an::buildClampProgram(clampShape, an::ClampInterval{.low = 0.0, .high = 1.0});
    ASSERT_TRUE(clamp.hasValue());

    const std::array<an::PointwiseProgram, 3u> stages{gamma->program(), curve->program(), *clamp};
    const an::Result<an::PointwiseProgram> fused = an::fusePointwisePrograms(stages);
    ASSERT_TRUE(fused.hasValue()) << fused.status().message();
    EXPECT_EQ(fused->inputDomain(), an::NumericDomain::NormalizedLinear);
    EXPECT_EQ(fused->outputDomain(), an::NumericDomain::EncodedNonlinear);
    EXPECT_EQ(fused->steps().size(), 3u);
    EXPECT_DOUBLE_EQ(*evaluate(*fused, 0.25), 0.25);

    std::array<an::PointwiseProgram, 2u> reversed{curve->program(), gamma->program()};
    const an::Result<an::PointwiseProgram> incompatible = an::fusePointwisePrograms(reversed);
    ASSERT_FALSE(incompatible.hasValue());
    EXPECT_EQ(incompatible.status().code(), an::ErrorCode::IncompatibleArguments);
}

// A curve's table is filled and measured through the compiled runs - serially for a plan built with no execution,
// in chunks on the execution's workers for one built with an execution - and both write the scalar loops' bits,
// which the reference backend runs: every entry and the distance reported, for every rule, an exact table of every
// code whose points do not reach the ends, every outside policy, both samplings and every table size.
TEST(ToneCurveTest, ACurvesTableFromTheCompiledRunsIsTheScalarLoopsBitForBit)
{
    const std::vector<std::vector<an::CurveControlPoint>> pointSets{
        {{0.0, 0.0}, {0.25, 0.12}, {0.5, 0.5}, {0.75, 0.88}, {1.0, 1.0}},
        {{0.0, 0.05}, {0.1, 0.9}, {0.15, 0.1}, {0.6, 0.95}, {0.61, 0.2}, {1.0, 0.7}},
        {{0.2, 0.3}, {0.45, 0.6}, {0.55, 0.62}, {0.7, 0.2}, {0.8, 0.8}},
        {{0.0, 1.0}, {0.3, 1.0}, {0.31, 0.0}, {1.0, 0.0}},
    };
    const std::vector<an::CurveInterpolation> rules{
        an::CurveInterpolation::Linear, an::CurveInterpolation::MonotoneCubic, an::CurveInterpolation::NaturalCubic,
        an::CurveInterpolation::Akima, an::CurveInterpolation::Steffen};
    const std::vector<an::InstructionSet> targets = runnableCurveTargets();
    auto [referenceEngine, reference] = curveExecution(an::ComputeBackend::Reference);
    std::vector<std::pair<an::EngineContext, an::ExecutionContext>> compiled;
    for (const an::InstructionSet target : targets)
    {
        compiled.push_back(curveExecution(an::ComputeBackend::Cpu, target));
    }
    std::uint32_t built = 0u;
    for (std::size_t set = 0u; set < pointSets.size(); ++set)
    {
        for (const an::CurveInterpolation rule : rules)
        {
            if (pointSets[set].size() < an::minimumCurvePoints(rule))
            {
                continue;
            }
            for (const an::CurveDomainPolicy policy : {an::CurveDomainPolicy::Clamp, an::CurveDomainPolicy::Reject,
                                                       an::CurveDomainPolicy::LinearExtrapolation})
            {
                struct Case
                {
                    an::SampleType sampleType;
                    an::CurveApproximation approximation;
                    std::uint32_t entries;
                };
                const std::vector<Case> cases{
                    {an::SampleType::Float32, an::CurveApproximation::SampledLinear, 2u},
                    {an::SampleType::Float32, an::CurveApproximation::SampledLinear, 3u},
                    {an::SampleType::Float32, an::CurveApproximation::SampledLinear, 4097u},
                    {an::SampleType::Float32, an::CurveApproximation::SampledLinear, 65536u},
                    {an::SampleType::Float32, an::CurveApproximation::SampledNearest, 1023u},
                    {an::SampleType::Float64, an::CurveApproximation::SampledLinear, 65536u},
                    {an::SampleType::UInt8, an::CurveApproximation::ExactAtResidentValues, 0u},
                    {an::SampleType::UInt16, an::CurveApproximation::ExactAtResidentValues, 0u}};
                for (const Case& item : cases)
                {
                    an::TonePlanShape shape;
                    shape.sampleType = item.sampleType;
                    if (item.sampleType == an::SampleType::Float64)
                    {
                        shape.precision = an::ComputePrecision::Float64;
                        shape.precisionJustification = an::Float64Justification::StoredInFloat64;
                    }
                    an::CurveProgramRequest request;
                    request.points = pointSets[set];
                    request.interpolation = rule;
                    request.approximation = item.approximation;
                    request.tableEntries = item.entries;
                    request.domainPolicy = policy;
                    const std::string what =
                        "points " + std::to_string(set) + ", rule " + std::to_string(static_cast<int>(rule)) +
                        ", policy " + std::to_string(static_cast<int>(policy)) + ", type " +
                        std::to_string(static_cast<int>(item.sampleType)) + ", entries " + std::to_string(item.entries);
                    const an::Result<an::CurveProgramPlan> scalar = an::buildCurveProgram(reference, shape, request);
                    expectSamePlan(scalar, an::buildCurveProgram(shape, request), what + ", host serial");
                    for (std::size_t target = 0u; target < targets.size(); ++target)
                    {
                        expectSamePlan(scalar, an::buildCurveProgram(compiled[target].second, shape, request),
                                       what + ", " + an::instructionSetName(targets[target]));
                    }
                    built += scalar.hasValue() ? 1u : 0u;
                }
            }
        }
    }
    // Every case but an exact table under a refusing policy whose points stop short of an end builds.
    EXPECT_GT(built, 300u);
}

// The compiled build measures a sampled table's distance from its curve only on the intervals a curvature bound
// cannot exclude; over curves drawn at random - few points and many, close points and far, every rule - the distance
// it reports is the one the reference measures at every probe, bit for bit, as is every entry.
TEST(ToneCurveTest, ACurvesMeasuredDistanceLeavesOutOnlyIntervalsThatCannotHoldTheLargest)
{
    std::uint64_t state = 0x9E3779B97F4A7C15ull;
    const auto next = [&state]() {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<double>(state >> 11) / 9007199254740992.0;
    };
    const std::vector<an::InstructionSet> targets = runnableCurveTargets();
    auto [referenceEngine, reference] = curveExecution(an::ComputeBackend::Reference);
    std::vector<std::pair<an::EngineContext, an::ExecutionContext>> compiled;
    for (const an::InstructionSet target : targets)
    {
        compiled.push_back(curveExecution(an::ComputeBackend::Cpu, target));
    }
    for (int drawn = 0; drawn < 48; ++drawn)
    {
        const std::size_t count = 3u + static_cast<std::size_t>(next() * 10.0);
        const bool spansUnit = drawn % 3 != 2;
        const double first = spansUnit ? 0.0 : 0.05 + 0.2 * next();
        const double last = spansUnit ? 1.0 : 0.7 + 0.25 * next();
        std::vector<double> inputs{first, last};
        while (inputs.size() < count)
        {
            inputs.push_back(first + (last - first) * next());
        }
        std::sort(inputs.begin(), inputs.end());
        inputs.erase(std::unique(inputs.begin(), inputs.end()), inputs.end());
        std::vector<an::CurveControlPoint> points;
        for (const double input : inputs)
        {
            points.push_back(an::CurveControlPoint{.input = input, .output = drawn % 4 == 0 ? input : next()});
        }
        for (const an::CurveInterpolation rule :
             {an::CurveInterpolation::Linear, an::CurveInterpolation::MonotoneCubic,
              an::CurveInterpolation::NaturalCubic, an::CurveInterpolation::Akima, an::CurveInterpolation::Steffen})
        {
            if (points.size() < an::minimumCurvePoints(rule))
            {
                continue;
            }
            for (const std::uint32_t entries : {4097u, 65536u})
            {
                an::CurveProgramRequest request;
                request.points = points;
                request.interpolation = rule;
                request.approximation = an::CurveApproximation::SampledLinear;
                request.tableEntries = entries;
                const an::TonePlanShape shape;
                const std::string what = "curve " + std::to_string(drawn) + ", rule " +
                                         std::to_string(static_cast<int>(rule)) + ", entries " +
                                         std::to_string(entries);
                const an::Result<an::CurveProgramPlan> scalar = an::buildCurveProgram(reference, shape, request);
                ASSERT_TRUE(scalar.hasValue()) << what << ": " << scalar.status().message();
                expectSamePlan(scalar, an::buildCurveProgram(shape, request), what + ", host serial");
                for (std::size_t target = 0u; target < targets.size(); ++target)
                {
                    expectSamePlan(scalar, an::buildCurveProgram(compiled[target].second, shape, request),
                                   what + ", " + an::instructionSetName(targets[target]));
                }
            }
        }
    }
}

TEST(ToneCurveTest, ACurvesTableBuiltOnAnExecutionStopsWithItAndRefusesAnEmptyOne)
{
    const std::array<an::CurveControlPoint, 5u> points{
        an::CurveControlPoint{.input = 0.0, .output = 0.0}, an::CurveControlPoint{.input = 0.25, .output = 0.12},
        an::CurveControlPoint{.input = 0.5, .output = 0.5}, an::CurveControlPoint{.input = 0.75, .output = 0.88},
        an::CurveControlPoint{.input = 1.0, .output = 1.0}};
    an::CurveProgramRequest request;
    request.points = points;
    request.interpolation = an::CurveInterpolation::Akima;
    request.approximation = an::CurveApproximation::SampledLinear;
    request.tableEntries = an::kMaxMathTableEntries;
    const an::TonePlanShape shape;

    const an::Result<an::CurveProgramPlan> empty = an::buildCurveProgram(an::ExecutionContext{}, shape, request);
    ASSERT_FALSE(empty.hasValue());
    EXPECT_EQ(empty.status().code(), an::ErrorCode::NotInitialized);

    an::EngineContextOptions engineOptions;
    engineOptions.maxConcurrency = 2u;
    an::Result<an::EngineContext> engine = an::EngineContext::create(engineOptions);
    ASSERT_TRUE(engine.hasValue());
    std::stop_source stop;
    an::ExecutionOptions options;
    options.maxConcurrency = 2u;
    options.stopToken = stop.get_token();
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, options);
    ASSERT_TRUE(execution.hasValue());
    stop.request_stop();
    const an::Result<an::CurveProgramPlan> stopped = an::buildCurveProgram(*execution, shape, request);
    ASSERT_FALSE(stopped.hasValue());
    EXPECT_EQ(stopped.status().code(), an::ErrorCode::Cancelled);
}
