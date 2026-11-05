// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the pointwise program: what it refuses, what it computes, and which rewrites
// it is allowed to perform.
//
// The validation cases matter more here than in most value types, because a pointwise program
// exists so that an executor can be left with arithmetic and no decisions. Every check that does
// not happen when a chain is built has to happen per pixel, inside a fused loop, where there is
// nowhere to report it.
//
// The canonicalization cases are written from both sides on purpose. A rule that fires is tested
// for exactness over a sweep rather than on one value; a rule that a reader would expect to exist
// and does not - dropping a zero bias, merging two scales - is tested for *not* firing, with the
// value that shows why it would be wrong. A rewrite rule that is quietly added later will fail
// those cases, which is the point of them.

#include <AfterNightEngine/Algorithms/Pointwise/PointwiseCanonicalization.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseEvaluator.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseOp.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgram.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgramBuilder.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgramError.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/ComputePrecision.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace
{

/// A builder over one unsigned sixteen-bit source, which is the shape a lookup collapse needs.
[[nodiscard]] an::PointwiseProgramBuilder indexableBuilder()
{
    an::PointwiseProgramBuilder builder;
    builder.readingSamples(an::SampleType::UInt16);
    return builder;
}

/// The one value a chain produces from one single-channel input, or a failure.
[[nodiscard]] double evaluateOne(const an::PointwiseProgram& program, double input)
{
    std::array<double, 1u> output{};
    const std::array<double, 1u> sources{input};
    const an::Status status = an::evaluatePointwisePixel(program, sources, output);
    EXPECT_TRUE(status.ok()) << status.message();
    return output[0];
}

/// Whether two doubles are the same bits, so that a signed zero and a payload difference count.
[[nodiscard]] bool sameBits(double left, double right) noexcept
{
    return std::bit_cast<std::uint64_t>(left) == std::bit_cast<std::uint64_t>(right);
}

} // namespace

// ---------------------------------------------------------------------------------------------
// What a program refuses
// ---------------------------------------------------------------------------------------------

TEST(PointwiseProgramTest, AnEmptyChainIsRefusedRatherThanTreatedAsTheIdentity)
{
    const an::PointwiseProgramBuilder builder;

    EXPECT_EQ(builder.check(), an::PointwiseProgramError::EmptyProgram);
    EXPECT_FALSE(builder.build());
}

TEST(PointwiseProgramTest, AStepWhoseKindThisBuildCannotExecuteIsRefusedBeforeItsCoefficients)
{
    an::PointwiseProgramBuilder builder;
    an::PointwiseOp broken{};
    broken.kind = static_cast<an::PointwiseOpKind>(200u);
    broken.factor = std::numeric_limits<double>::quiet_NaN();
    builder.append(broken);

    EXPECT_EQ(builder.check(), an::PointwiseProgramError::UnknownOpKind);
}

TEST(PointwiseProgramTest, ACoefficientThatIsNotFiniteIsRefused)
{
    for (const double bad : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
                             -std::numeric_limits<double>::infinity()})
    {
        an::PointwiseProgramBuilder scale;
        scale.append(an::pointwiseScale(bad));
        EXPECT_EQ(scale.check(), an::PointwiseProgramError::NonFiniteCoefficient);

        an::PointwiseProgramBuilder bias;
        bias.append(an::pointwiseBias(bad));
        EXPECT_EQ(bias.check(), an::PointwiseProgramError::NonFiniteCoefficient);

        an::PointwiseProgramBuilder clamp;
        clamp.append(an::pointwiseClamp(bad, 1.0));
        EXPECT_EQ(clamp.check(), an::PointwiseProgramError::NonFiniteCoefficient);
    }
}

TEST(PointwiseProgramTest, AStepThatSetAMemberItsKindDoesNotUseIsRefused)
{
    an::PointwiseOp scale = an::pointwiseScale(2.0);
    scale.offset = 1.0;

    an::PointwiseProgramBuilder builder;
    builder.append(scale);

    EXPECT_EQ(builder.check(), an::PointwiseProgramError::UnusedFieldSet);
}

TEST(PointwiseProgramTest, TwoStepsOfOneKindAreEqualExactlyWhenTheyDoTheSameThing)
{
    EXPECT_TRUE(an::pointwiseScale(2.0) == an::pointwiseScale(2.0));
    EXPECT_FALSE(an::pointwiseScale(2.0) == an::pointwiseScale(2.5));
    EXPECT_FALSE(an::pointwiseScale(1.0) == an::pointwiseGamma(1.0));
    EXPECT_FALSE(an::pointwiseBlend(0u, 0.5) == an::pointwiseBlend(1u, 0.5));
}

TEST(PointwiseProgramTest, AClampWhoseBoundsAdmitNoValueIsRefused)
{
    an::PointwiseProgramBuilder builder;
    builder.append(an::pointwiseClamp(1.0, 0.0));

    EXPECT_EQ(builder.check(), an::PointwiseProgramError::InvertedClampBounds);
}

TEST(PointwiseProgramTest, NewArithmeticAndCurveFieldsAreValidatedBeforeExecution)
{
    an::PointwiseProgramBuilder blend;
    blend.sources(2u).append(an::pointwiseBlend(1u, 1.01));
    EXPECT_EQ(blend.check(), an::PointwiseProgramError::BlendWeightOutOfRange);

    an::PointwiseOp comparison = an::pointwiseCompare(1u, an::PointwiseComparison::Equal);
    comparison.comparison = static_cast<an::PointwiseComparison>(200u);
    an::PointwiseProgramBuilder compare;
    compare.sources(2u).append(comparison);
    EXPECT_EQ(compare.check(), an::PointwiseProgramError::UnknownComparison);

    const std::array<double, 1u> tooSmall{0.0};
    an::PointwiseProgramBuilder curve;
    const std::uint32_t table = curve.addTable(tooSmall);
    curve.append(an::pointwiseSampledCurve(table, 0.0, 1.0, an::PointwiseCurveSampling::Linear,
                                           an::PointwiseCurveDomainPolicy::Clamp));
    EXPECT_EQ(curve.check(), an::PointwiseProgramError::TableSizeMismatch);

    an::PointwiseProgramBuilder domain;
    domain.readingDomain(static_cast<an::NumericDomain>(200u)).append(an::pointwiseScale(1.0));
    EXPECT_EQ(domain.check(), an::PointwiseProgramError::NumericDomainRefused);
}

TEST(PointwiseProgramTest, CanonicalizationPreservesTheDeclaredInputAndOutputDomains)
{
    an::PointwiseProgramBuilder builder;
    builder.readingDomain(an::NumericDomain::NormalizedLinear)
        .writingDomain(an::NumericDomain::EncodedNonlinear)
        .append(an::pointwiseGamma(0.5));
    const an::Result<an::PointwiseProgram> built = builder.build();
    ASSERT_TRUE(built.hasValue()) << built.status().message();
    const an::Result<an::PointwiseProgram> canonical = an::canonicalizePointwiseProgram(*built);
    ASSERT_TRUE(canonical.hasValue()) << canonical.status().message();
    EXPECT_EQ(canonical->inputDomain(), an::NumericDomain::NormalizedLinear);
    EXPECT_EQ(canonical->outputDomain(), an::NumericDomain::EncodedNonlinear);
}

TEST(PointwiseProgramTest, AStepNamingASourcePlaneTheProgramDoesNotHaveIsRefused)
{
    an::PointwiseProgramBuilder builder;
    builder.sources(2u).append(an::pointwiseMask(2u));

    EXPECT_EQ(builder.check(), an::PointwiseProgramError::OperandOutOfRange);
}

TEST(PointwiseProgramTest, AStepNamingATableTheProgramDoesNotCarryIsRefused)
{
    an::PointwiseProgramBuilder builder;
    builder.append(an::pointwiseChannelMatrix(0u));

    EXPECT_EQ(builder.check(), an::PointwiseProgramError::TableOutOfRange);
}

TEST(PointwiseProgramTest, AChannelMatrixNeedsTheSquareOfTheChannelCount)
{
    const std::array<double, 4u> tooSmall{1.0, 0.0, 0.0, 1.0};

    an::PointwiseProgramBuilder builder;
    builder.channels(3u);
    const std::uint32_t table = builder.addTable(tooSmall);
    builder.append(an::pointwiseChannelMatrix(table));

    EXPECT_EQ(builder.check(), an::PointwiseProgramError::TableSizeMismatch);
}

TEST(PointwiseProgramTest, ATableEntryThatIsNotFiniteIsRefused)
{
    const std::array<double, 1u> broken{std::numeric_limits<double>::quiet_NaN()};

    an::PointwiseProgramBuilder builder;
    const std::uint32_t table = builder.addTable(broken);
    builder.append(an::pointwiseChannelMatrix(table));

    EXPECT_EQ(builder.check(), an::PointwiseProgramError::NonFiniteTableEntry);
}

TEST(PointwiseProgramTest, ALookupThatIsNotTheFirstStepHasNoIndexAndIsRefused)
{
    an::PointwiseProgramBuilder builder = indexableBuilder();
    const std::vector<double> entries(65536u, 0.0);
    const std::uint32_t table = builder.addTable(entries);
    builder.append(an::pointwiseScale(2.0)).append(an::pointwiseLut(table));

    EXPECT_EQ(builder.check(), an::PointwiseProgramError::LutNotFirstStep);
}

TEST(PointwiseProgramTest, ALookupOverASourceThatCannotIndexOneIsRefused)
{
    for (const an::SampleType source : {an::SampleType::Float32, an::SampleType::Int16, an::SampleType::UInt32})
    {
        an::PointwiseProgramBuilder builder;
        builder.readingSamples(source);
        const std::vector<double> entries(65536u, 0.0);
        const std::uint32_t table = builder.addTable(entries);
        builder.append(an::pointwiseLut(table));

        EXPECT_EQ(builder.check(), an::PointwiseProgramError::LutSourceNotIndexable) << an::sampleTypeName(source);
    }
}

TEST(PointwiseProgramTest, ALookupTableMustHaveOneEntryForEveryValueTheSourceCanHold)
{
    an::PointwiseProgramBuilder builder = indexableBuilder();
    const std::vector<double> entries(256u, 0.0);
    const std::uint32_t table = builder.addTable(entries);
    builder.append(an::pointwiseLut(table));

    EXPECT_EQ(builder.check(), an::PointwiseProgramError::TableSizeMismatch);
    EXPECT_EQ(an::pointwiseLutSizeFor(an::SampleType::UInt16), 65536u);
    EXPECT_EQ(an::pointwiseLutSizeFor(an::SampleType::UInt8), 256u);
    EXPECT_EQ(an::pointwiseLutSizeFor(an::SampleType::Int8), 0u);
    EXPECT_EQ(an::pointwiseLutSizeFor(an::SampleType::Float32), 0u);
}

TEST(PointwiseProgramTest, TheShapeDeclarationsAreBounded)
{
    an::PointwiseProgramBuilder noChannels;
    noChannels.channels(0u).append(an::pointwiseScale(2.0));
    EXPECT_EQ(noChannels.check(), an::PointwiseProgramError::ChannelCountUnsupported);

    an::PointwiseProgramBuilder tooManyChannels;
    tooManyChannels.channels(an::kMaxPointwiseChannels + 1u).append(an::pointwiseScale(2.0));
    EXPECT_EQ(tooManyChannels.check(), an::PointwiseProgramError::ChannelCountUnsupported);

    an::PointwiseProgramBuilder noSources;
    noSources.sources(0u).append(an::pointwiseScale(2.0));
    EXPECT_EQ(noSources.check(), an::PointwiseProgramError::SourceCountUnsupported);

    an::PointwiseProgramBuilder tooManySources;
    tooManySources.sources(an::kMaxPointwiseSources + 1u).append(an::pointwiseScale(2.0));
    EXPECT_EQ(tooManySources.check(), an::PointwiseProgramError::SourceCountUnsupported);

    an::PointwiseProgramBuilder tooManySteps;
    for (std::uint32_t index = 0u; index <= an::kMaxPointwiseSteps; ++index)
    {
        tooManySteps.append(an::pointwiseScale(2.0));
    }
    EXPECT_EQ(tooManySteps.check(), an::PointwiseProgramError::TooManySteps);
}

TEST(PointwiseProgramTest, TheChainReachesFloat64ThroughTheSameRuleEverythingElseDoes)
{
    an::PointwiseProgramBuilder unjustified;
    unjustified.append(an::pointwiseScale(2.0)).computingIn(an::ComputePrecision::Float64);
    EXPECT_EQ(unjustified.check(), an::PointwiseProgramError::ComputePrecisionRefused);

    an::PointwiseProgramBuilder justified;
    justified.append(an::pointwiseScale(2.0))
        .readingSamples(an::SampleType::Float64)
        .computingIn(an::ComputePrecision::Float64, an::Float64Justification::StoredInFloat64);
    EXPECT_EQ(justified.check(), an::PointwiseProgramError::None);
}

TEST(PointwiseProgramTest, AnAcceptedChainReportsWhatItWasTold)
{
    an::PointwiseProgramBuilder builder;
    builder.channels(3u)
        .sources(2u)
        .readingSamples(an::SampleType::UInt16)
        .promising(an::OutputPrecisionContract::NormalizedOrLossy)
        .allowingContraction()
        .append(an::pointwiseScale(2.0))
        .append(an::pointwiseMask(1u));

    const an::Result<an::PointwiseProgram> built = builder.build();
    ASSERT_TRUE(built) << built.status().message();
    EXPECT_EQ(built->channelCount(), 3u);
    EXPECT_EQ(built->sourceCount(), 2u);
    EXPECT_EQ(built->sourceSampleType(), an::SampleType::UInt16);
    EXPECT_EQ(built->precision(), an::ComputePrecision::Float32);
    EXPECT_FALSE(built->precisionJustification().has_value());
    EXPECT_TRUE(built->contractionAllowed());
    EXPECT_EQ(built->steps().size(), 2u);
}

TEST(PointwiseProgramTest, EveryRefusalHasItsOwnName)
{
    EXPECT_STREQ(an::pointwiseProgramErrorName(an::PointwiseProgramError::None), "none");
    EXPECT_STREQ(an::pointwiseProgramErrorName(static_cast<an::PointwiseProgramError>(200)), "unknown");
    EXPECT_STREQ(an::pointwiseOpKindName(an::PointwiseOpKind::Lut), "lut");
    EXPECT_STREQ(an::pointwiseOpKindName(static_cast<an::PointwiseOpKind>(200)), "unknown");
    EXPECT_FALSE(an::isPointwiseOpKind(an::PointwiseOpKind::Unknown));
    EXPECT_TRUE(an::isPointwiseOpKind(an::PointwiseOpKind::Blend));
}

// ---------------------------------------------------------------------------------------------
// What a program computes
// ---------------------------------------------------------------------------------------------

TEST(PointwiseEvaluatorTest, TheArithmeticStepsComputeWhatTheirNamesSay)
{
    an::PointwiseProgramBuilder builder;
    builder.append(an::pointwiseScale(3.0)).append(an::pointwiseBias(1.0)).append(an::pointwiseClamp(0.0, 5.0));
    const an::Result<an::PointwiseProgram> program = builder.build();
    ASSERT_TRUE(program) << program.status().message();

    EXPECT_DOUBLE_EQ(evaluateOne(*program, 1.0), 4.0);
    EXPECT_DOUBLE_EQ(evaluateOne(*program, 0.0), 1.0);
    EXPECT_DOUBLE_EQ(evaluateOne(*program, 10.0), 5.0);
    EXPECT_DOUBLE_EQ(evaluateOne(*program, -10.0), 0.0);
}

TEST(PointwiseEvaluatorTest, AClampLeavesANotANumberExactlyWhereItFoundIt)
{
    an::PointwiseProgramBuilder builder;
    builder.append(an::pointwiseClamp(0.0, 1.0));
    const an::Result<an::PointwiseProgram> program = builder.build();
    ASSERT_TRUE(program) << program.status().message();

    EXPECT_TRUE(std::isnan(evaluateOne(*program, std::numeric_limits<double>::quiet_NaN())));
    EXPECT_DOUBLE_EQ(evaluateOne(*program, std::numeric_limits<double>::infinity()), 1.0);
    EXPECT_DOUBLE_EQ(evaluateOne(*program, -std::numeric_limits<double>::infinity()), 0.0);
}

TEST(PointwiseEvaluatorTest, GammaRaisesTheValueToThePower)
{
    an::PointwiseProgramBuilder builder;
    builder.append(an::pointwiseGamma(0.5));
    const an::Result<an::PointwiseProgram> program = builder.build();
    ASSERT_TRUE(program) << program.status().message();

    EXPECT_FLOAT_EQ(static_cast<float>(evaluateOne(*program, 4.0)), 2.0f);
    EXPECT_FLOAT_EQ(static_cast<float>(evaluateOne(*program, 0.0)), 0.0f);
}

TEST(PointwiseEvaluatorTest, AMaskMultipliesByItsPlaneAndABlendMovesTowardIt)
{
    an::PointwiseProgramBuilder maskBuilder;
    maskBuilder.sources(2u).append(an::pointwiseMask(1u));
    const an::Result<an::PointwiseProgram> masked = maskBuilder.build();
    ASSERT_TRUE(masked) << masked.status().message();

    std::array<double, 1u> output{};
    const std::array<double, 2u> sources{8.0, 0.25};
    ASSERT_TRUE(an::evaluatePointwisePixel(*masked, sources, output).ok());
    EXPECT_DOUBLE_EQ(output[0], 2.0);

    an::PointwiseProgramBuilder blendBuilder;
    blendBuilder.sources(2u).append(an::pointwiseScale(0.0)).append(an::pointwiseBlend(1u, 0.25));
    const an::Result<an::PointwiseProgram> blended = blendBuilder.build();
    ASSERT_TRUE(blended) << blended.status().message();

    const std::array<double, 2u> blendSources{4.0, 8.0};
    ASSERT_TRUE(an::evaluatePointwisePixel(*blended, blendSources, output).ok());
    EXPECT_DOUBLE_EQ(output[0], 2.0);
}

TEST(PointwiseEvaluatorTest, AChannelMatrixMixesEveryChannelOfThePixel)
{
    const std::array<double, 9u> swapRedAndBlue{0.0, 0.0, 1.0, 0.0, 1.0, 0.0, 1.0, 0.0, 0.0};

    an::PointwiseProgramBuilder builder;
    builder.channels(3u);
    const std::uint32_t table = builder.addTable(swapRedAndBlue);
    builder.append(an::pointwiseChannelMatrix(table));
    const an::Result<an::PointwiseProgram> program = builder.build();
    ASSERT_TRUE(program) << program.status().message();

    std::array<double, 3u> output{};
    const std::array<double, 3u> sources{1.0, 2.0, 3.0};
    ASSERT_TRUE(an::evaluatePointwisePixel(*program, sources, output).ok());
    EXPECT_DOUBLE_EQ(output[0], 3.0);
    EXPECT_DOUBLE_EQ(output[1], 2.0);
    EXPECT_DOUBLE_EQ(output[2], 1.0);
}

TEST(PointwiseEvaluatorTest, ALookupIsIndexedByTheSamplesOwnCodeValue)
{
    an::PointwiseProgramBuilder builder = indexableBuilder();
    std::vector<double> entries(65536u, 0.0);
    entries[7u] = 42.0;
    entries[65535u] = 99.0;
    const std::uint32_t table = builder.addTable(entries);
    builder.append(an::pointwiseLut(table));
    const an::Result<an::PointwiseProgram> program = builder.build();
    ASSERT_TRUE(program) << program.status().message();

    EXPECT_DOUBLE_EQ(evaluateOne(*program, 7.0), 42.0);
    EXPECT_DOUBLE_EQ(evaluateOne(*program, 65535.0), 99.0);
}

TEST(PointwiseEvaluatorTest, ASpanOfTheWrongLengthIsRefusedRatherThanRead)
{
    an::PointwiseProgramBuilder builder;
    builder.channels(3u).append(an::pointwiseScale(2.0));
    const an::Result<an::PointwiseProgram> program = builder.build();
    ASSERT_TRUE(program) << program.status().message();

    std::array<double, 3u> output{};
    const std::array<double, 2u> tooFew{1.0, 2.0};
    EXPECT_EQ(an::evaluatePointwisePixel(*program, tooFew, output).code(), an::ErrorCode::InvalidArgument);

    std::array<double, 2u> shortOutput{};
    const std::array<double, 3u> sources{1.0, 2.0, 3.0};
    EXPECT_EQ(an::evaluatePointwisePixel(*program, sources, shortOutput).code(), an::ErrorCode::InvalidArgument);
}

TEST(PointwiseEvaluatorTest, SeparatelyAllocatedInputAndOutputSpansAreAccepted)
{
    an::PointwiseProgramBuilder builder;
    builder.append(an::pointwiseScale(2.0));
    const an::Result<an::PointwiseProgram> program = builder.build();
    ASSERT_TRUE(program) << program.status().message();

    const std::vector<double> sources{3.0};
    std::vector<double> output(1u, 0.0);
    ASSERT_TRUE(an::evaluatePointwisePixel(*program, sources, output).ok());
    EXPECT_DOUBLE_EQ(output[0], 6.0);
}

TEST(PointwiseEvaluatorTest, TheDeclaredWidthIsTheWidthTheArithmeticRunsAt)
{
    // A value whose Float32 rounding is observable: one third, scaled and then unscaled.
    an::PointwiseProgramBuilder narrow;
    narrow.append(an::pointwiseScale(1.0 / 3.0)).append(an::pointwiseScale(3.0));
    const an::Result<an::PointwiseProgram> asFloat32 = narrow.build();
    ASSERT_TRUE(asFloat32) << asFloat32.status().message();

    an::PointwiseProgramBuilder wide;
    wide.append(an::pointwiseScale(1.0 / 3.0))
        .append(an::pointwiseScale(3.0))
        .readingSamples(an::SampleType::Float64)
        .computingIn(an::ComputePrecision::Float64, an::Float64Justification::StoredInFloat64);
    const an::Result<an::PointwiseProgram> asFloat64 = wide.build();
    ASSERT_TRUE(asFloat64) << asFloat64.status().message();

    EXPECT_EQ(asFloat32->precision(), an::ComputePrecision::Float32);
    EXPECT_EQ(asFloat64->precision(), an::ComputePrecision::Float64);

    bool anyDisagreement = false;
    for (std::uint32_t code = 1u; code < 4096u && !anyDisagreement; ++code)
    {
        const double input = static_cast<double>(code);
        anyDisagreement = !sameBits(evaluateOne(*asFloat32, input), evaluateOne(*asFloat64, input));
    }
    EXPECT_TRUE(anyDisagreement) << "the two widths agreed everywhere, so this program does not test what it says";
}

// ---------------------------------------------------------------------------------------------
// What canonicalization may and may not rewrite
// ---------------------------------------------------------------------------------------------

TEST(PointwiseCanonicalizationTest, AScaleByExactlyOneIsDropped)
{
    an::PointwiseProgramBuilder builder;
    builder.append(an::pointwiseScale(1.0)).append(an::pointwiseBias(2.0)).append(an::pointwiseScale(1.0));
    const an::Result<an::PointwiseProgram> program = builder.build();
    ASSERT_TRUE(program) << program.status().message();

    const an::Result<an::PointwiseProgram> canonical = an::canonicalizePointwiseProgram(*program);
    ASSERT_TRUE(canonical) << canonical.status().message();
    ASSERT_EQ(canonical->steps().size(), 1u);
    EXPECT_TRUE(canonical->steps()[0] == an::pointwiseBias(2.0));
}

TEST(PointwiseCanonicalizationTest, Float64JustificationSurvivesEveryRewrite)
{
    an::PointwiseProgramBuilder exactInteger;
    exactInteger.readingSamples(an::SampleType::UInt32)
        .promising(an::OutputPrecisionContract::ExactStoredValues)
        .computingIn(an::ComputePrecision::Float64, an::Float64Justification::ExactIntegerCodeValues)
        .append(an::pointwiseScale(1.0))
        .append(an::pointwiseBias(2.0));
    const an::Result<an::PointwiseProgram> program = exactInteger.build();
    ASSERT_TRUE(program) << program.status().message();

    const an::Result<an::PointwiseProgram> canonical = an::canonicalizePointwiseProgram(*program);
    ASSERT_TRUE(canonical) << canonical.status().message();
    EXPECT_EQ(canonical->precisionJustification(), an::Float64Justification::ExactIntegerCodeValues);
    ASSERT_EQ(canonical->steps().size(), 1u);
    EXPECT_TRUE(canonical->steps()[0] == an::pointwiseBias(2.0));

    an::PointwiseProgramBuilder algorithmic;
    algorithmic.computingIn(an::ComputePrecision::Float64, an::Float64Justification::CalibrationDivide)
        .append(an::pointwiseScale(2.0));
    const an::Result<an::PointwiseProgram> algorithmicProgram = algorithmic.build();
    ASSERT_TRUE(algorithmicProgram) << algorithmicProgram.status().message();
    const an::Result<an::PointwiseProgram> algorithmicCanonical = an::canonicalizePointwiseProgram(*algorithmicProgram);
    ASSERT_TRUE(algorithmicCanonical) << algorithmicCanonical.status().message();
    EXPECT_EQ(algorithmicCanonical->precisionJustification(), an::Float64Justification::CalibrationDivide);
}

TEST(PointwiseCanonicalizationTest, ABiasOfZeroIsNotDroppedBecauseItErasesASignedZero)
{
    an::PointwiseProgramBuilder builder;
    builder.append(an::pointwiseBias(0.0));
    const an::Result<an::PointwiseProgram> program = builder.build();
    ASSERT_TRUE(program) << program.status().message();

    const an::Result<an::PointwiseProgram> canonical = an::canonicalizePointwiseProgram(*program);
    ASSERT_TRUE(canonical) << canonical.status().message();
    ASSERT_EQ(canonical->steps().size(), 1u);

    // Asserting the step rather than the step count, because dropping the bias would leave an
    // identity behind it and a count would not notice.
    EXPECT_TRUE(canonical->steps()[0] == an::pointwiseBias(0.0));

    // The reason, stated as a value: the step that looks like the identity is the one that erases
    // a signed zero, so a chain it was dropped from would answer this differently.
    EXPECT_FALSE(sameBits(-0.0, 0.0));
    EXPECT_TRUE(sameBits(evaluateOne(*program, -0.0), 0.0));
    EXPECT_TRUE(sameBits(evaluateOne(*canonical, -0.0), 0.0));
}

TEST(PointwiseCanonicalizationTest, TwoAdjacentScalesAreNotMergedBecauseMultiplicationDoesNotAssociate)
{
    an::PointwiseProgramBuilder builder;
    builder.append(an::pointwiseScale(1.0 / 49.0)).append(an::pointwiseScale(49.0));
    const an::Result<an::PointwiseProgram> program = builder.build();
    ASSERT_TRUE(program) << program.status().message();

    const an::Result<an::PointwiseProgram> canonical = an::canonicalizePointwiseProgram(*program);
    ASSERT_TRUE(canonical) << canonical.status().message();
    EXPECT_EQ(canonical->steps().size(), 2u);

    // The merged chain would be a scale by one, and this is a value it would disagree on.
    an::PointwiseProgramBuilder mergedBuilder;
    mergedBuilder.append(an::pointwiseScale((1.0 / 49.0) * 49.0));
    const an::Result<an::PointwiseProgram> merged = mergedBuilder.build();
    ASSERT_TRUE(merged) << merged.status().message();

    bool anyDisagreement = false;
    for (std::uint32_t code = 0u; code < 4096u && !anyDisagreement; ++code)
    {
        anyDisagreement = !sameBits(evaluateOne(*program, static_cast<double>(code)),
                                    evaluateOne(*merged, static_cast<double>(code)));
    }
    EXPECT_TRUE(anyDisagreement) << "the merge this rule refuses to make would have been harmless here";
}

TEST(PointwiseCanonicalizationTest, AdjacentClampsBecomeTheirIntersectionAndAgreeEverywhere)
{
    an::PointwiseProgramBuilder builder;
    builder.append(an::pointwiseClamp(0.0, 100.0)).append(an::pointwiseClamp(10.0, 50.0));
    const an::Result<an::PointwiseProgram> program = builder.build();
    ASSERT_TRUE(program) << program.status().message();

    const an::Result<an::PointwiseProgram> canonical = an::canonicalizePointwiseProgram(*program);
    ASSERT_TRUE(canonical) << canonical.status().message();
    ASSERT_EQ(canonical->steps().size(), 1u);
    EXPECT_TRUE(canonical->steps()[0] == an::pointwiseClamp(10.0, 50.0));

    for (std::int32_t value = -200; value <= 200; ++value)
    {
        const double input = static_cast<double>(value) * 0.75;
        EXPECT_TRUE(sameBits(evaluateOne(*program, input), evaluateOne(*canonical, input))) << input;
    }
}

TEST(PointwiseCanonicalizationTest, ClampsWhoseIntersectionAdmitsNothingAreLeftAlone)
{
    an::PointwiseProgramBuilder builder;
    builder.append(an::pointwiseClamp(0.0, 1.0)).append(an::pointwiseClamp(5.0, 6.0));
    const an::Result<an::PointwiseProgram> program = builder.build();
    ASSERT_TRUE(program) << program.status().message();

    const an::Result<an::PointwiseProgram> canonical = an::canonicalizePointwiseProgram(*program);
    ASSERT_TRUE(canonical) << canonical.status().message();
    EXPECT_EQ(canonical->steps().size(), 2u);
    EXPECT_DOUBLE_EQ(evaluateOne(*canonical, 0.5), 5.0);
}

TEST(PointwiseCanonicalizationTest, AnEligibleChainCollapsesIntoOneTableThatAgreesOnEveryInput)
{
    an::PointwiseProgramBuilder builder = indexableBuilder();
    builder.append(an::pointwiseScale(1.0 / 65535.0))
        .append(an::pointwiseGamma(1.0 / 2.2))
        .append(an::pointwiseScale(65535.0))
        .append(an::pointwiseClamp(0.0, 65535.0));
    const an::Result<an::PointwiseProgram> program = builder.build();
    ASSERT_TRUE(program) << program.status().message();
    EXPECT_TRUE(an::pointwiseProgramCollapsesToLut(*program));

    const an::Result<an::PointwiseProgram> canonical = an::canonicalizePointwiseProgram(*program);
    ASSERT_TRUE(canonical) << canonical.status().message();
    ASSERT_EQ(canonical->steps().size(), 1u);
    EXPECT_EQ(canonical->steps()[0].kind, an::PointwiseOpKind::Lut);
    EXPECT_EQ(canonical->tableCount(), 1u);
    EXPECT_EQ(canonical->table(0u).size(), 65536u);

    for (std::uint32_t code = 0u; code <= 65535u; ++code)
    {
        const double input = static_cast<double>(code);
        ASSERT_TRUE(sameBits(evaluateOne(*program, input), evaluateOne(*canonical, input))) << code;
    }
}

TEST(PointwiseCanonicalizationTest, AChainThatReadsSomethingItsValueDoesNotDetermineDoesNotCollapse)
{
    an::PointwiseProgramBuilder masked = indexableBuilder();
    masked.sources(2u).append(an::pointwiseScale(2.0)).append(an::pointwiseMask(1u));
    const an::Result<an::PointwiseProgram> withMask = masked.build();
    ASSERT_TRUE(withMask) << withMask.status().message();
    EXPECT_FALSE(an::pointwiseProgramCollapsesToLut(*withMask));

    an::PointwiseProgramBuilder floating;
    floating.append(an::pointwiseScale(2.0));
    const an::Result<an::PointwiseProgram> overFloat = floating.build();
    ASSERT_TRUE(overFloat) << overFloat.status().message();
    EXPECT_FALSE(an::pointwiseProgramCollapsesToLut(*overFloat));

    const an::Result<an::PointwiseProgram> canonical = an::canonicalizePointwiseProgram(*withMask);
    ASSERT_TRUE(canonical) << canonical.status().message();
    EXPECT_EQ(canonical->steps().size(), 2u);
}

TEST(PointwiseCanonicalizationTest, RewritingACanonicalProgramChangesNothing)
{
    an::PointwiseProgramBuilder builder = indexableBuilder();
    builder.append(an::pointwiseScale(1.0))
        .append(an::pointwiseScale(2.0))
        .append(an::pointwiseClamp(0.0, 100.0))
        .append(an::pointwiseClamp(10.0, 50.0));
    const an::Result<an::PointwiseProgram> program = builder.build();
    ASSERT_TRUE(program) << program.status().message();

    const an::Result<an::PointwiseProgram> once = an::canonicalizePointwiseProgram(*program);
    ASSERT_TRUE(once) << once.status().message();
    const an::Result<an::PointwiseProgram> twice = an::canonicalizePointwiseProgram(*once);
    ASSERT_TRUE(twice) << twice.status().message();

    EXPECT_TRUE(*once == *twice);
}

TEST(PointwiseCanonicalizationTest, ATableNoStepReadsAnyMoreIsRemoved)
{
    an::PointwiseProgramBuilder builder = indexableBuilder();
    std::vector<double> entries(65536u, 0.0);
    for (std::uint32_t code = 0u; code <= 65535u; ++code)
    {
        entries[code] = static_cast<double>(65535u - code);
    }
    const std::uint32_t table = builder.addTable(entries);
    builder.append(an::pointwiseLut(table)).append(an::pointwiseScale(2.0));
    const an::Result<an::PointwiseProgram> program = builder.build();
    ASSERT_TRUE(program) << program.status().message();
    EXPECT_EQ(program->tableCount(), 1u);

    const an::Result<an::PointwiseProgram> canonical = an::canonicalizePointwiseProgram(*program);
    ASSERT_TRUE(canonical) << canonical.status().message();
    ASSERT_EQ(canonical->steps().size(), 1u);
    EXPECT_EQ(canonical->steps()[0].kind, an::PointwiseOpKind::Lut);
    EXPECT_EQ(canonical->tableCount(), 1u);

    for (std::uint32_t code = 0u; code <= 65535u; ++code)
    {
        const double input = static_cast<double>(code);
        ASSERT_TRUE(sameBits(evaluateOne(*program, input), evaluateOne(*canonical, input))) << code;
    }
}

TEST(PointwiseCanonicalizationTest, FusionRemapsEveryKindOfTableInLaterStages)
{
    std::vector<double> reverse(65536u);
    for (std::size_t i = 0u; i < reverse.size(); ++i)
    {
        reverse[i] = static_cast<double>(65535u - i);
    }
    auto first = indexableBuilder();
    first.append(an::pointwiseLut(first.addTable(reverse)));
    const auto left = first.build();
    ASSERT_TRUE(left.hasValue());
    for (const auto kind : {an::PointwiseOpKind::ChannelMatrix, an::PointwiseOpKind::SampledCurve})
    {
        auto second = indexableBuilder();
        const std::array<double, 1u> matrix{0.5};
        const std::array<double, 2u> curve{0.0, 32767.5};
        if (kind == an::PointwiseOpKind::ChannelMatrix)
            second.append(an::pointwiseChannelMatrix(second.addTable(matrix)));
        else
            second.append(an::pointwiseSampledCurve(second.addTable(curve), 0.0, 65535.0));
        const auto right = second.build();
        ASSERT_TRUE(right.hasValue()) << right.status().message();
        const std::array<an::PointwiseProgram, 2u> stages{*left, *right};
        const auto fused = an::fusePointwisePrograms(stages);
        ASSERT_TRUE(fused.hasValue()) << fused.status().message();
        for (const double value : {0.0, 1.0, 100.0, 30000.0, 65535.0})
            EXPECT_TRUE(sameBits(evaluateOne(*fused, value), evaluateOne(*right, evaluateOne(*left, value))));
    }
}

TEST(PointwiseCanonicalizationTest, TwoProgramsAreEqualOnlyWhenEveryDeclarationAgrees)
{
    an::PointwiseProgramBuilder one;
    one.append(an::pointwiseScale(2.0));
    an::PointwiseProgramBuilder other;
    other.append(an::pointwiseScale(2.0)).channels(2u);

    const an::Result<an::PointwiseProgram> left = one.build();
    const an::Result<an::PointwiseProgram> right = other.build();
    ASSERT_TRUE(left);
    ASSERT_TRUE(right);

    EXPECT_TRUE(*left == *left);
    EXPECT_FALSE(*left == *right);

    an::PointwiseProgramBuilder calibrated;
    calibrated.computingIn(an::ComputePrecision::Float64, an::Float64Justification::CalibrationDivide)
        .append(an::pointwiseScale(2.0));
    an::PointwiseProgramBuilder stacked;
    stacked.computingIn(an::ComputePrecision::Float64, an::Float64Justification::StackingAccumulation)
        .append(an::pointwiseScale(2.0));
    const an::Result<an::PointwiseProgram> calibratedProgram = calibrated.build();
    const an::Result<an::PointwiseProgram> stackedProgram = stacked.build();
    ASSERT_TRUE(calibratedProgram);
    ASSERT_TRUE(stackedProgram);
    EXPECT_FALSE(*calibratedProgram == *stackedProgram);
}

TEST(PointwiseCanonicalizationTest, CollapsedLookupPreservesContractionAtEveryCode)
{
    auto builder = indexableBuilder();
    builder.allowingContraction().append(an::pointwiseScale(1.00000011920928955078125)).append(an::pointwiseBias(-3.0));
    const auto program = builder.build();
    ASSERT_TRUE(program.hasValue());
    const auto collapsed = an::canonicalizePointwiseProgram(*program);
    ASSERT_TRUE(collapsed.hasValue());
    for (std::uint32_t code = 0u; code < 65536u; ++code)
    {
        ASSERT_EQ(evaluateOne(*collapsed, code), evaluateOne(*program, code)) << code;
    }
}
