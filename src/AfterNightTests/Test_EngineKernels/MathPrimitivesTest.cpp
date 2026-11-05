// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the transcendental providers: the policy that picks one, the exceptional
// answers all three share, the table that is exact, and the distance between the compiled
// polynomials and the scalar reference.

#include <AfterNightEngine/Algorithms/Kernel/MathKernel.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <stop_token>
#include <utility>
#include <vector>

namespace
{

[[nodiscard]] an::EngineContext mathEngine(std::uint32_t concurrency = 4u)
{
    an::EngineContextOptions options;
    options.maxConcurrency = concurrency;
    an::Result<an::EngineContext> made = an::EngineContext::create(options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::EngineContext{};
}

[[nodiscard]] an::ExecutionContext mathExecution(const an::EngineContext& engine, an::ComputeBackend backend,
                                                 std::uint32_t concurrency = 1u, std::stop_token stopToken = {})
{
    an::ExecutionOptions options;
    options.maxConcurrency = concurrency;
    options.backend = backend;
    options.stopToken = std::move(stopToken);
    // A conformance comparison is worthless if the engine quietly ran the same code twice, so the
    // requested backend is a requirement rather than a preference.
    options.backendFallback = an::BackendFallback::Forbidden;
    an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
}

template <typename SampleT>
[[nodiscard]] an::PlaneView<SampleT> mathPlane(std::span<SampleT> samples, std::uint32_t width, std::uint32_t height,
                                               std::uint32_t stride)
{
    const std::optional<an::PlaneView<SampleT>> made = an::PlaneView<SampleT>::create(
        samples, width, height, 1u, static_cast<std::uint64_t>(stride) * sizeof(std::remove_const_t<SampleT>));
    EXPECT_TRUE(made.has_value());
    return *made;
}

/// The bit pattern of one Float32, ordered so that a subtraction counts representable values.
///
/// The distance between two floats is a count of the values between them, which is what "units in
/// the last place" means. Reading the pattern as a signed integer and folding the negative half
/// gives a monotonic ordering, so one subtraction answers it for any two finite values.
///
/// @param value the sample.
/// @return its position in the ordering.
[[nodiscard]] std::int64_t orderedBits(float value)
{
    std::int32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    if (bits < 0)
    {
        return static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min()) - static_cast<std::int64_t>(bits);
    }
    return static_cast<std::int64_t>(bits);
}

/// How many representable Float32 values separate two results.
///
/// @param left one result.
/// @param right the other.
/// @return the count, or the largest representable count when either is not finite and they differ.
[[nodiscard]] double ulpDistance(float left, float right)
{
    if (left == right)
    {
        return 0.0;
    }
    if (!std::isfinite(left) || !std::isfinite(right))
    {
        return std::numeric_limits<double>::infinity();
    }
    const std::int64_t distance = orderedBits(left) - orderedBits(right);
    return static_cast<double>(distance < 0 ? -distance : distance);
}

/// Which of the classes an exceptional or subnormal result belongs to.
///
/// Where the reference result is not a finite normal number, the contract compares by class rather
/// than by distance: a subnormal is a handful of representable values away from zero, so a count of
/// them says nothing, and an infinity has no count at all.
enum class ResultClass
{
    NotANumber,
    PositiveInfinity,
    NegativeInfinity,
    ZeroOrSubnormal,
    Normal
};

[[nodiscard]] ResultClass classOf(float value)
{
    if (std::isnan(value))
    {
        return ResultClass::NotANumber;
    }
    if (std::isinf(value))
    {
        return value > 0.0f ? ResultClass::PositiveInfinity : ResultClass::NegativeInfinity;
    }
    if (std::abs(value) < std::numeric_limits<float>::min())
    {
        return ResultClass::ZeroOrSubnormal;
    }
    return ResultClass::Normal;
}

/// The samples every sweep covers: the exceptional inputs, the boundaries, and a wide spread.
///
/// Written out rather than generated so that a reader can see that the exceptional inputs are in
/// it. The spread crosses the whole exponent range in both directions, which is what makes the
/// distance measurement below a measurement rather than a spot check.
[[nodiscard]] std::vector<float> sweepSamples()
{
    std::vector<float> samples{std::numeric_limits<float>::quiet_NaN(),
                               std::numeric_limits<float>::infinity(),
                               -std::numeric_limits<float>::infinity(),
                               0.0f,
                               -0.0f,
                               1.0f,
                               -1.0f,
                               -2.5f,
                               std::numeric_limits<float>::min(),
                               -std::numeric_limits<float>::min(),
                               std::numeric_limits<float>::denorm_min(),
                               std::numeric_limits<float>::max(),
                               std::numeric_limits<float>::lowest(),
                               88.7228f,
                               88.7229f,
                               -87.0f,
                               -103.0f,
                               -104.5f};
    for (int exponent = -40; exponent <= 38; ++exponent)
    {
        const double scale = std::pow(10.0, exponent);
        samples.push_back(static_cast<float>(scale));
        samples.push_back(static_cast<float>(scale * 1.7320508075688772));
        samples.push_back(static_cast<float>(-scale * 1.4142135623730951));
    }
    for (int step = 0; step <= 512; ++step)
    {
        samples.push_back(static_cast<float>(step) / 512.0f);
    }
    return samples;
}

/// What one sweep of one function measured.
struct SweepOutcome
{
    /// The largest distance from the reference seen at any sample whose reference is normal.
    double worstUlp = 0.0;

    /// How many samples were compared.
    std::uint64_t compared = 0u;
};

/// Applies one function to one run of samples on one backend.
///
/// @param engine the engine context.
/// @param backend which implementation to force.
/// @param parameters the function and its exponent.
/// @param input the samples.
/// @param concurrency how many threads the operation may use.
/// @return the results, one per sample.
[[nodiscard]] std::vector<float> applyRun(const an::EngineContext& engine, an::ComputeBackend backend,
                                          const an::MathKernelParameters& parameters, std::span<const float> input,
                                          std::uint32_t concurrency = 1u)
{
    an::ExecutionContext execution = mathExecution(engine, backend, concurrency);
    std::vector<float> output(input.size(), -7.5f);
    const std::uint32_t width = static_cast<std::uint32_t>(input.size());
    const an::PlaneView<const float> source = mathPlane<const float>(input, width, 1u, width);
    const an::PlaneView<float> destination = mathPlane<float>(output, width, 1u, width);
    const an::Result<an::MathExecutionResult> applied =
        an::applyMathFunctionPlane(execution, source, destination, parameters, {});
    EXPECT_TRUE(applied.hasValue()) << applied.status().message();
    if (applied.hasValue())
    {
        EXPECT_EQ(applied->run.outputsWritten, input.size());
        // What ran is read from the dispatch rather than assumed from the request: a host without
        // the compiled instruction set, and a build without it, both evaluate through the library.
        const bool compiled = an::instructionSetOfDispatchedCode(execution) != an::InstructionSet::Scalar &&
                              backend != an::ComputeBackend::Reference;
        EXPECT_EQ(applied->provider,
                  compiled ? an::MathProvider::VectorApproximation : an::MathProvider::ScalarLibrary);
    }
    return output;
}

/// Compares one function on the compiled backend against the scalar reference over the sweep.
///
/// @param engine the engine context.
/// @param parameters the function and its exponent.
/// @return the worst distance and how many samples were compared.
[[nodiscard]] SweepOutcome sweepAgainstReference(const an::EngineContext& engine,
                                                 const an::MathKernelParameters& parameters)
{
    const std::vector<float> samples = sweepSamples();
    const std::vector<float> compiled = applyRun(engine, an::ComputeBackend::Cpu, parameters, samples);
    SweepOutcome outcome;
    for (std::size_t index = 0u; index < samples.size(); ++index)
    {
        const float sample = samples[index];
        const float reference = an::referenceMathValue(parameters.function, sample, parameters.exponent);
        const float measured = compiled[index];
        if (classOf(reference) != ResultClass::Normal)
        {
            EXPECT_EQ(classOf(measured), classOf(reference))
                << an::mathFunctionName(parameters.function) << " at " << sample << " produced " << measured
                << " where the reference produced " << reference;
            continue;
        }
        const double distance = ulpDistance(measured, reference);
        const double allowed = an::mathUlpBound(parameters.function, sample, parameters.exponent);
        EXPECT_LE(distance, allowed) << an::mathFunctionName(parameters.function) << " at " << sample << " produced "
                                     << measured << " where the reference produced " << reference;
        outcome.worstUlp = distance > outcome.worstUlp ? distance : outcome.worstUlp;
        ++outcome.compared;
    }
    return outcome;
}

} // namespace

TEST(MathPrimitivesTest, ProviderFollowsTheRepresentationAndTheArithmeticWidth)
{
    an::MathProviderRequest request;
    request.function = an::MathFunction::Pow;
    request.inputSample = an::SampleType::UInt16;
    request.sampleCount = 1024u * 1024u;
    EXPECT_EQ(an::selectMathProvider(request), an::MathProvider::ExactTable);

    // A frame with fewer samples than the representation has code values costs more to tabulate
    // than to evaluate, so the table is not chosen for one.
    request.sampleCount = 1000u;
    EXPECT_EQ(an::selectMathProvider(request), an::MathProvider::VectorApproximation);

    // An unstated count is a table that has already been built, and is therefore free.
    request.sampleCount = 0u;
    EXPECT_EQ(an::selectMathProvider(request), an::MathProvider::ExactTable);

    request.sampleCount = 1024u * 1024u;
    request.inputSample = an::SampleType::Float32;
    EXPECT_EQ(an::selectMathProvider(request), an::MathProvider::VectorApproximation);

    request.precision = an::ComputePrecision::Float64;
    EXPECT_EQ(an::selectMathProvider(request), an::MathProvider::ScalarLibrary);

    request.precision = an::ComputePrecision::Float32;
    request.inputSample = an::SampleType::Float64;
    EXPECT_EQ(an::selectMathProvider(request), an::MathProvider::ScalarLibrary);

    request.inputSample = an::SampleType::UInt32;
    EXPECT_EQ(an::selectMathProvider(request), an::MathProvider::VectorApproximation);

    request.function = an::MathFunction::Unknown;
    EXPECT_EQ(an::selectMathProvider(request), an::MathProvider::Unknown);
}

TEST(MathPrimitivesTest, ReferenceAnswersEveryExceptionalInputAsTheContractStates)
{
    const float notANumber = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();

    EXPECT_TRUE(std::isnan(an::referenceMathValue(an::MathFunction::Exp, notANumber, 0.0)));
    EXPECT_EQ(an::referenceMathValue(an::MathFunction::Exp, infinity, 0.0), infinity);
    EXPECT_EQ(an::referenceMathValue(an::MathFunction::Exp, -infinity, 0.0), 0.0f);
    EXPECT_EQ(an::referenceMathValue(an::MathFunction::Exp, 0.0f, 0.0), 1.0f);
    EXPECT_EQ(an::referenceMathValue(an::MathFunction::Exp, -0.0f, 0.0), 1.0f);
    EXPECT_EQ(an::referenceMathValue(an::MathFunction::Exp, 200.0f, 0.0), infinity);

    EXPECT_TRUE(std::isnan(an::referenceMathValue(an::MathFunction::Log, notANumber, 0.0)));
    EXPECT_TRUE(std::isnan(an::referenceMathValue(an::MathFunction::Log, -1.0f, 0.0)));
    EXPECT_EQ(an::referenceMathValue(an::MathFunction::Log, 0.0f, 0.0), -infinity);
    EXPECT_EQ(an::referenceMathValue(an::MathFunction::Log, -0.0f, 0.0), -infinity);
    EXPECT_EQ(an::referenceMathValue(an::MathFunction::Log, infinity, 0.0), infinity);
    EXPECT_EQ(an::referenceMathValue(an::MathFunction::Log, 1.0f, 0.0), 0.0f);
    EXPECT_TRUE(std::isnan(an::referenceMathValue(an::MathFunction::Log, -infinity, 0.0)));

    // The narrowing this engine states: a negative base is outside the domain rather than answered
    // as std::pow answers it for an integral exponent.
    EXPECT_TRUE(std::isnan(an::referenceMathValue(an::MathFunction::Pow, -2.0f, 2.0)));
    EXPECT_TRUE(std::isnan(an::referenceMathValue(an::MathFunction::Pow, notANumber, 0.0)));
    EXPECT_TRUE(
        std::isnan(an::referenceMathValue(an::MathFunction::Pow, 2.0f, std::numeric_limits<double>::quiet_NaN())));
    EXPECT_EQ(an::referenceMathValue(an::MathFunction::Pow, 0.0f, 2.0), 0.0f);
    EXPECT_EQ(an::referenceMathValue(an::MathFunction::Pow, 0.0f, -2.0), infinity);
    EXPECT_EQ(an::referenceMathValue(an::MathFunction::Pow, 0.0f, 0.0), 1.0f);
    EXPECT_EQ(an::referenceMathValue(an::MathFunction::Pow, infinity, 2.0), infinity);
    EXPECT_EQ(an::referenceMathValue(an::MathFunction::Pow, infinity, -2.0), 0.0f);
    EXPECT_EQ(an::referenceMathValue(an::MathFunction::Pow, infinity, 0.0), 1.0f);
    EXPECT_EQ(an::referenceMathValue(an::MathFunction::Pow, 1.0f, 12345.0), 1.0f);
    EXPECT_EQ(an::referenceMathValue(an::MathFunction::Pow, 4.0f, 0.5), 2.0f);

    EXPECT_TRUE(std::isnan(an::referenceMathValue(an::MathFunction::Unknown, 1.0f, 1.0)));
}

TEST(MathPrimitivesTest, CompiledPolynomialsAgreeWithTheReferenceWithinTheStatedBound)
{
    an::EngineContext engine = mathEngine();
    if (!an::instructionSetIsCompiledIn(an::InstructionSet::Avx2))
    {
        GTEST_SKIP() << "this build contains no compiled vector kernels to compare";
    }

    const SweepOutcome exponential =
        sweepAgainstReference(engine, an::MathKernelParameters{.function = an::MathFunction::Exp});
    const SweepOutcome logarithm =
        sweepAgainstReference(engine, an::MathKernelParameters{.function = an::MathFunction::Log});
    const SweepOutcome gamma = sweepAgainstReference(
        engine, an::MathKernelParameters{.function = an::MathFunction::Pow, .exponent = 1.0 / 2.2});
    const SweepOutcome square =
        sweepAgainstReference(engine, an::MathKernelParameters{.function = an::MathFunction::Pow, .exponent = 2.0});

    EXPECT_GT(exponential.compared, 100u);
    EXPECT_GT(logarithm.compared, 100u);
    EXPECT_GT(gamma.compared, 100u);
    EXPECT_GT(square.compared, 100u);

    // The comparison is between two implementations rather than between one implementation and
    // itself: if the compiled path were the scalar one, every distance would be zero and this
    // suite would be proving nothing. At least one of the four is expected to differ somewhere.
    EXPECT_GT(exponential.worstUlp + logarithm.worstUlp + gamma.worstUlp + square.worstUlp, 0.0);

    // Recorded rather than only bounded, so a regression inside the contract is still visible in
    // the run output a measurement record quotes.
    std::cout << "worst distance in units in the last place: exp " << exponential.worstUlp << ", log "
              << logarithm.worstUlp << ", pow(1/2.2) " << gamma.worstUlp << ", pow(2) " << square.worstUlp << "\n";
}

TEST(MathPrimitivesTest, TheGammaDomainStaysFarInsideTheAcceptedTier)
{
    an::EngineContext engine = mathEngine();
    if (!an::instructionSetIsCompiledIn(an::InstructionSet::Avx2))
    {
        GTEST_SKIP() << "this build contains no compiled vector kernels to compare";
    }

    // The domain a tone curve actually applies: normalized intensities, down to the smallest
    // sixteen-bit code value and below it. The empirical ceiling here is far tighter than the
    // derived bound and is what a regression would trip first.
    std::vector<float> samples;
    for (int step = 1; step <= 4096; ++step)
    {
        samples.push_back(static_cast<float>(step) / 4096.0f);
        samples.push_back(static_cast<float>(step) / 65535.0f);
    }
    const an::MathKernelParameters parameters{.function = an::MathFunction::Pow, .exponent = 1.0 / 2.2};
    const std::vector<float> compiled = applyRun(engine, an::ComputeBackend::Cpu, parameters, samples);
    double worst = 0.0;
    for (std::size_t index = 0u; index < samples.size(); ++index)
    {
        const float reference = an::referenceMathValue(parameters.function, samples[index], parameters.exponent);
        const double distance = ulpDistance(compiled[index], reference);
        worst = distance > worst ? distance : worst;
    }
    EXPECT_LE(worst, 16.0) << "the gamma transfer drifted from the scalar reference by " << worst
                           << " units in the last place";
    std::cout << "worst distance over the gamma domain: " << worst << " units in the last place\n";
}

TEST(MathPrimitivesTest, EveryRunLengthAgreesWithTheReference)
{
    an::EngineContext engine = mathEngine();
    std::vector<float> samples;
    for (int index = 0; index < 64; ++index)
    {
        samples.push_back(0.001f + static_cast<float>(index) * 0.0157f);
    }
    const an::MathKernelParameters parameters{.function = an::MathFunction::Pow, .exponent = 1.0 / 2.2};

    // A run whose length is not a whole number of vector widths is the case a tail handles, and the
    // tail must produce what the interior produces: what one sample evaluates to may not depend on
    // where in the run it sat.
    const std::vector<float> full = applyRun(engine, an::ComputeBackend::Cpu, parameters, samples);
    for (std::size_t length = 1u; length <= samples.size(); ++length)
    {
        const std::vector<float> partial =
            applyRun(engine, an::ComputeBackend::Cpu, parameters, std::span<const float>(samples).first(length));
        for (std::size_t index = 0u; index < length; ++index)
        {
            EXPECT_EQ(partial[index], full[index]) << "length " << length << " sample " << index;
        }
    }
}

TEST(MathPrimitivesTest, TableIsExactAtEveryCodeValueOfASixteenBitInput)
{
    an::EngineContext engine = mathEngine();
    an::ExecutionContext execution = mathExecution(engine, an::ComputeBackend::Automatic, 4u);
    std::vector<float> entries(an::kMaxMathTableEntries, -1.0f);
    const an::MathTableRequest request{
        .parameters = {.function = an::MathFunction::Pow, .exponent = 1.0 / 2.2}, .first = 0.0, .last = 1.0};
    const an::Result<an::MathExecutionResult> built = an::buildMathFunctionTable(execution, request, entries);
    ASSERT_TRUE(built.hasValue()) << built.status().message();
    EXPECT_EQ(built->provider, an::MathProvider::ExactTable);
    EXPECT_EQ(built->run.outputsWritten, an::kMaxMathTableEntries);
    EXPECT_EQ(built->nonFiniteOutputs, 0u);

    EXPECT_EQ(entries.front(), 0.0f);
    EXPECT_EQ(entries.back(), 1.0f);
    for (std::uint32_t code = 0u; code < an::kMaxMathTableEntries; ++code)
    {
        const float point = static_cast<float>(static_cast<double>(code) / 65535.0);
        const float expected = an::referenceMathValue(request.parameters.function, point, request.parameters.exponent);
        ASSERT_EQ(entries[code], expected) << "code value " << code;
    }
}

TEST(MathPrimitivesTest, TableRefusesAnImpossibleRequest)
{
    an::EngineContext engine = mathEngine();
    an::ExecutionContext execution = mathExecution(engine, an::ComputeBackend::Automatic);
    std::array<float, 4u> entries{};

    an::MathTableRequest request{.parameters = {.function = an::MathFunction::Unknown, .exponent = 1.0}};
    EXPECT_EQ(an::buildMathFunctionTable(execution, request, entries).status().code(), an::ErrorCode::InvalidArgument);

    request.parameters.function = an::MathFunction::Pow;
    request.parameters.exponent = std::numeric_limits<double>::infinity();
    EXPECT_EQ(an::buildMathFunctionTable(execution, request, entries).status().code(), an::ErrorCode::InvalidArgument);

    request.parameters.exponent = 1.0;
    request.last = std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(an::buildMathFunctionTable(execution, request, entries).status().code(), an::ErrorCode::InvalidArgument);

    request.last = 1.0;
    EXPECT_EQ(an::buildMathFunctionTable(execution, request, std::span<float>{}).status().code(),
              an::ErrorCode::OutOfRange);

    std::vector<float> tooMany(an::kMaxMathTableEntries + 1u, 0.0f);
    EXPECT_EQ(an::buildMathFunctionTable(execution, request, tooMany).status().code(), an::ErrorCode::OutOfRange);

    // One entry is a legal table and is evaluated at the start of its domain.
    std::array<float, 1u> single{};
    request.first = 4.0;
    request.last = 16.0;
    request.parameters.exponent = 0.5;
    ASSERT_TRUE(an::buildMathFunctionTable(execution, request, single).hasValue());
    EXPECT_EQ(single[0], 2.0f);
}

TEST(MathPrimitivesTest, RejectedSamplesKeepTheDestinationTheyHad)
{
    an::EngineContext engine = mathEngine();
    an::ExecutionContext execution = mathExecution(engine, an::ComputeBackend::Automatic, 2u);

    const std::array<float, 8u> input{
        1.0f, std::numeric_limits<float>::quiet_NaN(), 4.0f, 9.0f, std::numeric_limits<float>::infinity(), 16.0f, 25.0f,
        36.0f};
    std::array<float, 8u> output{};
    output.fill(-3.0f);
    const an::PlaneView<const float> source = mathPlane<const float>(input, 8u, 1u, 8u);
    const an::PlaneView<float> destination = mathPlane<float>(output, 8u, 1u, 8u);

    an::ValidityDescriptorRequest descriptorRequest;
    descriptorRequest.rule = an::ValidityRule::FiniteOnly;
    const std::optional<an::ValidityDescriptor> descriptor = an::ValidityDescriptor::create(descriptorRequest);
    ASSERT_TRUE(descriptor.has_value());
    const an::KernelValidityView validity{.descriptor = *descriptor};

    const an::Result<an::MathExecutionResult> applied = an::applyMathFunctionPlane(
        execution, source, destination, {.function = an::MathFunction::Pow, .exponent = 0.5}, validity);
    ASSERT_TRUE(applied.hasValue()) << applied.status().message();
    EXPECT_EQ(applied->run.selection.valid, 6u);
    EXPECT_EQ(applied->run.selection.total, 8u);
    EXPECT_EQ(applied->run.outputsWritten, 6u);
    EXPECT_EQ(applied->nonFiniteOutputs, 0u);

    EXPECT_FLOAT_EQ(output[0], 1.0f);
    EXPECT_EQ(output[1], -3.0f);
    EXPECT_FLOAT_EQ(output[2], 2.0f);
    EXPECT_FLOAT_EQ(output[3], 3.0f);
    EXPECT_EQ(output[4], -3.0f);
    EXPECT_FLOAT_EQ(output[5], 4.0f);
    EXPECT_FLOAT_EQ(output[6], 5.0f);
    EXPECT_FLOAT_EQ(output[7], 6.0f);
}

TEST(MathPrimitivesTest, NonFiniteResultsAreCountedRatherThanRefused)
{
    an::EngineContext engine = mathEngine();
    an::ExecutionContext execution = mathExecution(engine, an::ComputeBackend::Automatic);

    const std::array<float, 4u> input{0.0f, 1.0f, 0.0f, 2.0f};
    std::array<float, 4u> output{};
    const an::PlaneView<const float> source = mathPlane<const float>(input, 4u, 1u, 4u);
    const an::PlaneView<float> destination = mathPlane<float>(output, 4u, 1u, 4u);
    const an::Result<an::MathExecutionResult> applied =
        an::applyMathFunctionPlane(execution, source, destination, {.function = an::MathFunction::Log}, {});
    ASSERT_TRUE(applied.hasValue()) << applied.status().message();
    EXPECT_EQ(applied->nonFiniteOutputs, 2u);
    EXPECT_EQ(output[0], -std::numeric_limits<float>::infinity());
    EXPECT_EQ(output[1], 0.0f);
}

TEST(MathPrimitivesTest, InPlaceMatchesTheDisjointResultAndPartialOverlapIsRefused)
{
    an::EngineContext engine = mathEngine();
    an::ExecutionContext execution = mathExecution(engine, an::ComputeBackend::Automatic, 2u);

    std::vector<float> samples;
    for (int index = 0; index < 100; ++index)
    {
        samples.push_back(0.01f + static_cast<float>(index) * 0.013f);
    }
    const an::MathKernelParameters parameters{.function = an::MathFunction::Pow, .exponent = 1.0 / 2.2};
    const std::vector<float> disjoint = applyRun(engine, an::ComputeBackend::Automatic, parameters, samples);

    std::vector<float> inPlace = samples;
    const an::PlaneView<const float> source = mathPlane<const float>(std::span<const float>(inPlace), 20u, 5u, 20u);
    const an::PlaneView<float> destination = mathPlane<float>(std::span<float>(inPlace), 20u, 5u, 20u);
    ASSERT_TRUE(an::applyMathFunctionPlane(execution, source, destination, parameters, {}).hasValue());
    for (std::size_t index = 0u; index < samples.size(); ++index)
    {
        EXPECT_EQ(inPlace[index], disjoint[index]) << "sample " << index;
    }

    // A destination one sample into the source overlaps without being the source, which no member
    // of this family accepts.
    std::vector<float> buffer(200u, 1.0f);
    const an::PlaneView<const float> overlapSource =
        mathPlane<const float>(std::span<const float>(buffer).subspan(0u, 100u), 100u, 1u, 100u);
    const an::PlaneView<float> overlapDestination =
        mathPlane<float>(std::span<float>(buffer).subspan(1u, 100u), 100u, 1u, 100u);
    EXPECT_EQ(an::applyMathFunctionPlane(execution, overlapSource, overlapDestination, parameters, {}).status().code(),
              an::ErrorCode::InvalidArgument);
}

TEST(MathPrimitivesTest, ResultDoesNotDependOnHowManyThreadsRan)
{
    an::EngineContext engine = mathEngine(4u);
    std::vector<float> samples;
    for (int index = 0; index < 5000; ++index)
    {
        samples.push_back(1e-6f + static_cast<float>(index) * 3.1e-4f);
    }
    const an::MathKernelParameters parameters{.function = an::MathFunction::Pow, .exponent = 1.0 / 2.2};
    const std::vector<float> single = applyRun(engine, an::ComputeBackend::Automatic, parameters, samples, 1u);
    const std::vector<float> many = applyRun(engine, an::ComputeBackend::Automatic, parameters, samples, 4u);
    EXPECT_EQ(single, many);
}

TEST(MathPrimitivesTest, PlaneRefusesAnUnknownFunctionAndANonFiniteExponent)
{
    an::EngineContext engine = mathEngine();
    an::ExecutionContext execution = mathExecution(engine, an::ComputeBackend::Automatic);
    const std::array<float, 4u> input{1.0f, 2.0f, 3.0f, 4.0f};
    std::array<float, 4u> output{};
    const an::PlaneView<const float> source = mathPlane<const float>(input, 4u, 1u, 4u);
    const an::PlaneView<float> destination = mathPlane<float>(output, 4u, 1u, 4u);

    EXPECT_EQ(an::applyMathFunctionPlane(execution, source, destination, {.function = an::MathFunction::Unknown}, {})
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::applyMathFunctionPlane(
                  execution, source, destination,
                  {.function = an::MathFunction::Pow, .exponent = std::numeric_limits<double>::quiet_NaN()}, {})
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
}

TEST(MathPrimitivesTest, AnOperationThatWasAlreadyStoppedWritesNothing)
{
    an::EngineContext engine = mathEngine();
    std::stop_source stop;
    stop.request_stop();
    an::ExecutionContext execution = mathExecution(engine, an::ComputeBackend::Automatic, 2u, stop.get_token());

    std::vector<float> input(4096u, 0.5f);
    std::vector<float> output(4096u, -1.0f);
    const an::PlaneView<const float> source = mathPlane<const float>(input, 64u, 64u, 64u);
    const an::PlaneView<float> destination = mathPlane<float>(output, 64u, 64u, 64u);
    const an::Result<an::MathExecutionResult> applied =
        an::applyMathFunctionPlane(execution, source, destination, {.function = an::MathFunction::Log}, {});
    ASSERT_FALSE(applied.hasValue());
    EXPECT_EQ(applied.status().code(), an::ErrorCode::Cancelled);
    // A stop observed before any output leaves the destination as it was, which is what lets a
    // caller reuse the buffer rather than having to know how far the operation got.
    EXPECT_EQ(output.front(), -1.0f);
    EXPECT_EQ(output.back(), -1.0f);

    std::array<float, 8u> entries{};
    entries.fill(-1.0f);
    const an::MathTableRequest request{.parameters = {.function = an::MathFunction::Exp}};
    EXPECT_EQ(an::buildMathFunctionTable(execution, request, entries).status().code(), an::ErrorCode::Cancelled);
    EXPECT_EQ(entries.front(), -1.0f);
}

TEST(MathPrimitivesTest, NamesAndBoundsAreStableForDiagnostics)
{
    EXPECT_STREQ(an::mathFunctionName(an::MathFunction::Exp), "Exp");
    EXPECT_STREQ(an::mathFunctionName(an::MathFunction::Log), "Log");
    EXPECT_STREQ(an::mathFunctionName(an::MathFunction::Pow), "Pow");
    EXPECT_STREQ(an::mathFunctionName(an::MathFunction::Unknown), "UnknownMathFunction");
    EXPECT_STREQ(an::mathProviderName(an::MathProvider::ExactTable), "ExactTable");
    EXPECT_STREQ(an::mathProviderName(an::MathProvider::VectorApproximation), "VectorApproximation");
    EXPECT_STREQ(an::mathProviderName(an::MathProvider::ScalarLibrary), "ScalarLibrary");
    EXPECT_STREQ(an::mathProviderName(an::MathProvider::Unknown), "UnknownMathProvider");

    EXPECT_EQ(an::mathFunctionAccuracy(an::MathFunction::Exp).metric, an::KernelAccuracyMetric::Ulp);
    EXPECT_EQ(an::mathFunctionAccuracy(an::MathFunction::Exp).maximumError, an::kVectorMathUlpTier);
    EXPECT_EQ(an::mathFunctionAccuracy(an::MathFunction::Unknown).metric, an::KernelAccuracyMetric::Exact);

    // A compiled polynomial carries the tier; a composition carries the tier plus what its argument
    // costs, which is what makes the bound a derivation rather than a tolerance.
    EXPECT_EQ(an::mathUlpBound(an::MathFunction::Exp, 1.0f, 0.0), an::kVectorMathUlpTier);
    EXPECT_EQ(an::mathUlpBound(an::MathFunction::Pow, 1.0f, 2.0), an::kVectorMathUlpTier);
    EXPECT_GT(an::mathUlpBound(an::MathFunction::Pow, 1.0e-6f, 1.0 / 2.2), an::kVectorMathUlpTier);
    EXPECT_EQ(an::mathUlpBound(an::MathFunction::Log, 0.0f, 0.0), 0.0);
    EXPECT_EQ(an::mathUlpBound(an::MathFunction::Unknown, 1.0f, 1.0), 0.0);
}

TEST(MathPrimitivesTest, SignedZeroPowersAgreeAcrossProvidersAndVectorTails)
{
    const an::EngineContext engine = mathEngine();
    const std::vector<float> samples(37u, -0.0f);
    for (const double exponent : {1.0, 3.0, -1.0, -3.0, 2.0, -2.0, 0.5, -0.5, 0.0, 16777215.0, 16777216.0})
    {
        const an::MathKernelParameters parameters{.function = an::MathFunction::Pow, .exponent = exponent};
        const auto reference = applyRun(engine, an::ComputeBackend::Reference, parameters, samples);
        const auto compiled = applyRun(engine, an::ComputeBackend::Cpu, parameters, samples);
        for (std::size_t index = 0u; index < samples.size(); ++index)
        {
            EXPECT_EQ(compiled[index], reference[index]) << exponent << " at " << index;
            EXPECT_EQ(std::signbit(compiled[index]), std::signbit(reference[index])) << exponent << " at " << index;
        }
    }
}

TEST(MathPrimitivesTest, ExponentNarrowingCannotChangeSignedZeroClassification)
{
    const auto engine = mathEngine();
    const auto execution = mathExecution(engine, an::ComputeBackend::Cpu);
    for (const double exponent :
         {16777217.0, -16777217.0, 3.00000001, -3.00000001, 1.0e-50, -1.0e-50, 1.0e100, -1.0e100})
    {
        std::array<float, 3u> samples{-0.0f, 0.0f, 1.0f};
        const auto original = samples;
        const auto result = an::applyMathFunctionPlane(execution, mathPlane<const float>(samples, 3u, 1u, 3u),
                                                       mathPlane<float>(samples, 3u, 1u, 3u),
                                                       {.function = an::MathFunction::Pow, .exponent = exponent}, {});
        ASSERT_TRUE(result.hasValue()) << result.status().message();
        EXPECT_EQ(result->provider, an::MathProvider::ScalarLibrary);
        for (std::size_t i = 0u; i < samples.size(); ++i)
        {
            const float expected = static_cast<float>(std::pow(static_cast<double>(original[i]), exponent));
            EXPECT_EQ(samples[i], expected) << exponent;
            EXPECT_EQ(std::signbit(samples[i]), std::signbit(expected)) << exponent;
        }
    }
}
