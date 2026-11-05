// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the arithmetic, affine and rescale planners.

#include <AfterNightEngine/Algorithms/Pointwise/Arithmetic.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseEvaluator.h>
#include <AfterNightEngine/Core/ErrorCode.h>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>
#include <span>

namespace
{

[[nodiscard]] double evaluate(const an::PointwiseProgram& program, double primary, double operand = 0.0)
{
    const std::array<double, 2u> inputs{primary, operand};
    std::array<double, 1u> output{};
    const an::Status status =
        an::evaluatePointwisePixel(program, std::span<const double>(inputs).first(program.sourceCount()), output);
    EXPECT_TRUE(status.ok()) << status.message();
    return output[0];
}

[[nodiscard]] an::PointwisePlanShape rawShape()
{
    an::PointwisePlanShape shape;
    shape.numericDomain = an::NumericDomain::RawSensorCode;
    return shape;
}

} // namespace

TEST(ArithmeticTest, BinaryOperationsHaveExplicitUnclampedRawDomainMeaning)
{
    struct Example
    {
        an::BinaryArithmeticOperation operation;
        double rightScale;
        double expected;
    };
    constexpr std::array examples{
        Example{an::BinaryArithmeticOperation::Add, 2.0, 16.0},
        Example{an::BinaryArithmeticOperation::Subtract, 2.0, 4.0},
        Example{an::BinaryArithmeticOperation::Multiply, 2.0, 60.0},
        Example{an::BinaryArithmeticOperation::Divide, 2.0, static_cast<double>(static_cast<float>(20.0 / 3.0))},
        Example{an::BinaryArithmeticOperation::Minimum, 1.0, 3.0},
        Example{an::BinaryArithmeticOperation::Maximum, 1.0, 10.0},
    };

    for (const Example& example : examples)
    {
        const an::Result<an::PointwiseProgram> made =
            an::buildBinaryArithmeticProgram(rawShape(), example.operation, example.rightScale);
        ASSERT_TRUE(made.hasValue()) << made.status().message();
        EXPECT_EQ(made->inputDomain(), an::NumericDomain::RawSensorCode);
        EXPECT_EQ(made->outputDomain(), an::NumericDomain::RawSensorCode);
        EXPECT_DOUBLE_EQ(evaluate(*made, 10.0, 3.0), example.expected);
    }

    // 0.8 + 0.7 is 1.5. Representation does not imply normalization or a one-sided clamp.
    an::PointwisePlanShape normalized;
    const an::Result<an::PointwiseProgram> add =
        an::buildBinaryArithmeticProgram(normalized, an::BinaryArithmeticOperation::Add);
    ASSERT_TRUE(add.hasValue());
    EXPECT_DOUBLE_EQ(evaluate(*add, 0.8, 0.7), 1.5);
}

TEST(ArithmeticTest, DivisionAndMinMaxStateTheirIec60559ExtremeBehavior)
{
    const an::Result<an::PointwiseProgram> divide =
        an::buildBinaryArithmeticProgram(rawShape(), an::BinaryArithmeticOperation::Divide);
    const an::Result<an::PointwiseProgram> minimum =
        an::buildBinaryArithmeticProgram(rawShape(), an::BinaryArithmeticOperation::Minimum);
    const an::Result<an::PointwiseProgram> maximum =
        an::buildBinaryArithmeticProgram(rawShape(), an::BinaryArithmeticOperation::Maximum);
    ASSERT_TRUE(divide.hasValue());
    ASSERT_TRUE(minimum.hasValue());
    ASSERT_TRUE(maximum.hasValue());

    EXPECT_TRUE(std::isinf(evaluate(*divide, 1.0, 0.0)));
    EXPECT_TRUE(std::isnan(evaluate(*divide, 0.0, 0.0)));
    const double nan = std::numeric_limits<double>::quiet_NaN();
    EXPECT_DOUBLE_EQ(evaluate(*minimum, nan, 4.0), 4.0);
    EXPECT_DOUBLE_EQ(evaluate(*maximum, nan, 4.0), 4.0);
}

TEST(ArithmeticTest, BlendComparisonAndAffineConveniencesShareThePointwiseContract)
{
    const an::PointwisePlanShape shape = rawShape();
    const an::Result<an::PointwiseProgram> blend = an::buildBlendProgram(shape, 0.25);
    ASSERT_TRUE(blend.hasValue());
    EXPECT_DOUBLE_EQ(evaluate(*blend, 8.0, 4.0), 7.0);
    EXPECT_EQ(an::buildBlendProgram(shape, 1.01).status().code(), an::ErrorCode::OutOfRange);

    for (const auto [comparison, expected] :
         {std::pair{an::PointwiseComparison::Less, 1.0}, std::pair{an::PointwiseComparison::LessOrEqual, 1.0},
          std::pair{an::PointwiseComparison::Equal, 0.0}, std::pair{an::PointwiseComparison::NotEqual, 1.0},
          std::pair{an::PointwiseComparison::GreaterOrEqual, 0.0}, std::pair{an::PointwiseComparison::Greater, 0.0}})
    {
        const an::Result<an::PointwiseProgram> program = an::buildComparisonProgram(shape, comparison);
        ASSERT_TRUE(program.hasValue());
        EXPECT_DOUBLE_EQ(evaluate(*program, 2.0, 3.0), expected);
    }

    const an::Result<an::PointwiseProgram> exposure = an::buildExposureProgram(shape, 2.0);
    const an::Result<an::PointwiseProgram> brightness = an::buildBrightnessProgram(shape, -3.0);
    const an::Result<an::PointwiseProgram> contrast = an::buildContrastProgram(shape, 2.0, 5.0);
    const an::Result<an::PointwiseProgram> clamped =
        an::buildScaleBiasProgram(shape, 2.0, -1.0, an::ClampInterval{.low = 0.0, .high = 10.0});
    ASSERT_TRUE(exposure.hasValue());
    ASSERT_TRUE(brightness.hasValue());
    ASSERT_TRUE(contrast.hasValue());
    ASSERT_TRUE(clamped.hasValue());
    EXPECT_DOUBLE_EQ(evaluate(*exposure, 3.0), 12.0);
    EXPECT_DOUBLE_EQ(evaluate(*brightness, 5.0), 2.0);
    EXPECT_DOUBLE_EQ(evaluate(*contrast, 8.0), 11.0);
    EXPECT_DOUBLE_EQ(evaluate(*clamped, 20.0), 10.0);
}

TEST(ArithmeticTest, ConstantRescaleIsNamedAndNeverDividesByZero)
{
    an::RescaleRequest request;
    request.inputLow = 7.0;
    request.inputHigh = 7.0;
    request.outputLow = -2.0;
    request.outputHigh = 6.0;

    struct Mapping
    {
        an::ConstantRescalePolicy policy;
        double expected;
    };
    constexpr std::array mappings{
        Mapping{an::ConstantRescalePolicy::MapToLower, -2.0},
        Mapping{an::ConstantRescalePolicy::MapToMidpoint, 2.0},
        Mapping{an::ConstantRescalePolicy::MapToUpper, 6.0},
    };
    for (const Mapping& mapping : mappings)
    {
        request.constantPolicy = mapping.policy;
        const an::Result<an::RescalePlan> plan = an::buildRescaleProgram(rawShape(), request);
        ASSERT_TRUE(plan.hasValue()) << plan.status().message();
        EXPECT_EQ(plan->disposition(), an::RescaleDisposition::ConstantMapped);
        ASSERT_TRUE(plan->constantOutput().has_value());
        EXPECT_DOUBLE_EQ(*plan->constantOutput(), mapping.expected);
        EXPECT_DOUBLE_EQ(evaluate(plan->program(), 7.0), mapping.expected);
        EXPECT_TRUE(std::isfinite(evaluate(plan->program(), 7.0)));
    }

    request.constantPolicy = an::ConstantRescalePolicy::Preserve;
    const an::Result<an::RescalePlan> preserved = an::buildRescaleProgram(rawShape(), request);
    ASSERT_TRUE(preserved.hasValue());
    EXPECT_EQ(preserved->disposition(), an::RescaleDisposition::ConstantPreserved);
    EXPECT_DOUBLE_EQ(evaluate(preserved->program(), 7.0), 7.0);

    request.constantPolicy = an::ConstantRescalePolicy::Reject;
    const an::Result<an::RescalePlan> rejected = an::buildRescaleProgram(rawShape(), request);
    ASSERT_FALSE(rejected.hasValue());
    EXPECT_EQ(rejected.status().code(), an::ErrorCode::EmptySelection);
}

TEST(ArithmeticTest, OrdinaryRescaleMapsEndpointsAndRejectsUnrepresentableCoefficients)
{
    an::RescaleRequest request;
    request.inputLow = 10.0;
    request.inputHigh = 20.0;
    request.outputLow = 1.0;
    request.outputHigh = -1.0;
    const an::Result<an::RescalePlan> plan = an::buildRescaleProgram(rawShape(), request);
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();
    EXPECT_EQ(plan->disposition(), an::RescaleDisposition::Affine);
    EXPECT_DOUBLE_EQ(evaluate(plan->program(), 10.0), 1.0);
    EXPECT_DOUBLE_EQ(evaluate(plan->program(), 20.0), -1.0);
    EXPECT_DOUBLE_EQ(evaluate(plan->program(), 100.0), -1.0);

    request.inputLow = -std::numeric_limits<double>::max();
    request.inputHigh = std::numeric_limits<double>::max();
    request.outputLow = -std::numeric_limits<double>::max();
    request.outputHigh = std::numeric_limits<double>::max();
    const an::Result<an::RescalePlan> overflow = an::buildRescaleProgram(rawShape(), request);
    ASSERT_FALSE(overflow.hasValue());
    EXPECT_EQ(overflow.status().code(), an::ErrorCode::OutOfRange);
}

TEST(ArithmeticTest, IntegerArithmeticIsRefusedInsteadOfGuessingItsDomain)
{
    an::PointwisePlanShape shape;
    shape.sampleType = an::SampleType::UInt16;
    const an::Result<an::PointwiseProgram> result =
        an::buildBinaryArithmeticProgram(shape, an::BinaryArithmeticOperation::Add);
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.status().code(), an::ErrorCode::IncompatibleArguments);
}
