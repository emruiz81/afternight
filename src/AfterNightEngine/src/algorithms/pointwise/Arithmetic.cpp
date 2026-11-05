// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - planned arithmetic, blend, affine and rescale operations.

#include <AfterNightEngine/Algorithms/Pointwise/Arithmetic.h>

#include <AfterNightEngine/Algorithms/Pointwise/PointwiseOp.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgramBuilder.h>
#include <AfterNightEngine/Core/ErrorCode.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace an
{
namespace
{

constexpr const char* kOperation = "an::buildPointwiseArithmeticProgram";

[[nodiscard]] Status arithmeticFailure(ErrorCode code, const char* message)
{
    return Status::failure(
        code, StatusDetail{.message = message, .operation = kOperation, .domain = ErrorDomain::Algorithms});
}

[[nodiscard]] Status validateShape(const PointwisePlanShape& shape)
{
    if (shape.sampleType != SampleType::Float32 && shape.sampleType != SampleType::Float64)
    {
        return arithmeticFailure(
            ErrorCode::IncompatibleArguments,
            "pointwise arithmetic operates on a Float32 or named Float64 working image; integer storage must be "
            "converted under its stated numeric domain first");
    }
    if (!isNumericDomain(shape.numericDomain))
    {
        return arithmeticFailure(ErrorCode::InvalidArgument, "the pointwise arithmetic domain is not known");
    }
    return Status::success();
}

[[nodiscard]] PointwiseProgramBuilder configuredBuilder(const PointwisePlanShape& shape, std::uint32_t sources)
{
    PointwiseProgramBuilder builder;
    builder.channels(shape.channelCount)
        .sources(sources)
        .readingSamples(shape.sampleType)
        .readingDomain(shape.numericDomain)
        .writingDomain(shape.numericDomain)
        .promising(shape.outputContract)
        .computingIn(shape.precision, shape.precisionJustification);
    if (shape.allowContraction)
    {
        builder.allowingContraction();
    }
    return builder;
}

[[nodiscard]] Result<PointwiseProgram> finish(const PointwisePlanShape& shape, PointwiseProgramBuilder builder)
{
    const Status valid = validateShape(shape);
    if (valid.failed())
    {
        return valid;
    }
    return builder.build();
}

[[nodiscard]] bool validClamp(const ClampInterval& interval) noexcept
{
    return std::isfinite(interval.low) && std::isfinite(interval.high) && interval.low <= interval.high;
}

} // namespace

RescalePlan::RescalePlan(PointwiseProgram program, RescaleDisposition disposition,
                         std::optional<double> constantOutput) noexcept
    : m_program(std::move(program)), m_disposition(disposition), m_constantOutput(constantOutput)
{
}

const PointwiseProgram& RescalePlan::program() const noexcept
{
    return m_program;
}

RescaleDisposition RescalePlan::disposition() const noexcept
{
    return m_disposition;
}

std::optional<double> RescalePlan::constantOutput() const noexcept
{
    return m_constantOutput;
}

Result<PointwiseProgram> buildBinaryArithmeticProgram(const PointwisePlanShape& shape,
                                                      BinaryArithmeticOperation operation, double rightScale)
{
    if (!isBinaryArithmeticOperation(operation) || !std::isfinite(rightScale))
    {
        return arithmeticFailure(ErrorCode::InvalidArgument,
                                 "a binary arithmetic operation and its secondary scale must be known and finite");
    }
    if ((operation == BinaryArithmeticOperation::Minimum || operation == BinaryArithmeticOperation::Maximum) &&
        rightScale != 1.0)
    {
        return arithmeticFailure(ErrorCode::InvalidArgument,
                                 "minimum and maximum do not have a secondary scale; it must remain one");
    }

    PointwiseProgramBuilder builder = configuredBuilder(shape, 2u);
    switch (operation)
    {
    case BinaryArithmeticOperation::Add:
        builder.append(pointwiseAdd(1u, rightScale));
        break;
    case BinaryArithmeticOperation::Subtract:
        builder.append(pointwiseSubtract(1u, rightScale));
        break;
    case BinaryArithmeticOperation::Multiply:
        builder.append(pointwiseMultiply(1u, rightScale));
        break;
    case BinaryArithmeticOperation::Divide:
        builder.append(pointwiseDivide(1u, rightScale));
        break;
    case BinaryArithmeticOperation::Minimum:
        builder.append(pointwiseMinimum(1u));
        break;
    case BinaryArithmeticOperation::Maximum:
        builder.append(pointwiseMaximum(1u));
        break;
    }
    return finish(shape, std::move(builder));
}

Result<PointwiseProgram> buildBlendProgram(const PointwisePlanShape& shape, double weight)
{
    if (!std::isfinite(weight) || weight < 0.0 || weight > 1.0)
    {
        return arithmeticFailure(ErrorCode::OutOfRange, "a blend weight must lie in the closed interval [0, 1]");
    }
    PointwiseProgramBuilder builder = configuredBuilder(shape, 2u);
    builder.append(pointwiseBlend(1u, weight));
    return finish(shape, std::move(builder));
}

Result<PointwiseProgram> buildComparisonProgram(const PointwisePlanShape& shape, PointwiseComparison comparison)
{
    if (!isPointwiseComparison(comparison))
    {
        return arithmeticFailure(ErrorCode::InvalidArgument, "the comparison relation is not one this build knows");
    }
    PointwiseProgramBuilder builder = configuredBuilder(shape, 2u);
    builder.append(pointwiseCompare(1u, comparison));
    return finish(shape, std::move(builder));
}

Result<PointwiseProgram> buildScaleBiasProgram(const PointwisePlanShape& shape, double scale, double bias,
                                               std::optional<ClampInterval> clamp)
{
    if (!std::isfinite(scale) || !std::isfinite(bias) || (clamp.has_value() && !validClamp(*clamp)))
    {
        return arithmeticFailure(ErrorCode::InvalidArgument,
                                 "scale, bias and any clamp bounds must be finite and the bounds ordered");
    }
    PointwiseProgramBuilder builder = configuredBuilder(shape, 1u);
    builder.append(pointwiseScale(scale)).append(pointwiseBias(bias));
    if (clamp.has_value())
    {
        builder.append(pointwiseClamp(clamp->low, clamp->high));
    }
    return finish(shape, std::move(builder));
}

Result<PointwiseProgram> buildExposureProgram(const PointwisePlanShape& shape, double stops)
{
    const double scale = std::exp2(stops);
    if (!std::isfinite(stops) || !std::isfinite(scale))
    {
        return arithmeticFailure(ErrorCode::OutOfRange, "the requested exposure produces no finite multiplier");
    }
    return buildScaleBiasProgram(shape, scale, 0.0);
}

Result<PointwiseProgram> buildBrightnessProgram(const PointwisePlanShape& shape, double amount)
{
    return buildScaleBiasProgram(shape, 1.0, amount);
}

Result<PointwiseProgram> buildContrastProgram(const PointwisePlanShape& shape, double factor, double pivot)
{
    if (!std::isfinite(factor) || !std::isfinite(pivot))
    {
        return arithmeticFailure(ErrorCode::InvalidArgument, "contrast factor and pivot must be finite");
    }
    const double bias = pivot - pivot * factor;
    if (!std::isfinite(bias))
    {
        return arithmeticFailure(ErrorCode::OutOfRange, "the requested contrast produces no finite affine transform");
    }
    return buildScaleBiasProgram(shape, factor, bias);
}

Result<PointwiseProgram> buildClampProgram(const PointwisePlanShape& shape, const ClampInterval& interval)
{
    if (!validClamp(interval))
    {
        return arithmeticFailure(ErrorCode::InvalidArgument, "clamp bounds must be finite and ordered");
    }
    PointwiseProgramBuilder builder = configuredBuilder(shape, 1u);
    builder.append(pointwiseClamp(interval.low, interval.high));
    return finish(shape, std::move(builder));
}

Result<RescalePlan> buildRescaleProgram(const PointwisePlanShape& shape, const RescaleRequest& request)
{
    if (!std::isfinite(request.inputLow) || !std::isfinite(request.inputHigh) || !std::isfinite(request.outputLow) ||
        !std::isfinite(request.outputHigh) || request.inputLow > request.inputHigh ||
        !isConstantRescalePolicy(request.constantPolicy))
    {
        return arithmeticFailure(ErrorCode::InvalidArgument,
                                 "rescale intervals and the constant-image policy must be finite, ordered and known");
    }

    if (request.inputLow == request.inputHigh)
    {
        if (request.constantPolicy == ConstantRescalePolicy::Reject)
        {
            return arithmeticFailure(ErrorCode::EmptySelection,
                                     "a constant input interval was refused by the rescale policy");
        }
        if (request.constantPolicy == ConstantRescalePolicy::Preserve)
        {
            Result<PointwiseProgram> identity = buildScaleBiasProgram(shape, 1.0, 0.0);
            if (!identity.hasValue())
            {
                return identity.status();
            }
            return RescalePlan(std::move(*identity), RescaleDisposition::ConstantPreserved, std::nullopt);
        }

        double value = request.outputLow;
        if (request.constantPolicy == ConstantRescalePolicy::MapToMidpoint)
        {
            value = request.outputLow + (request.outputHigh - request.outputLow) * 0.5;
        }
        else if (request.constantPolicy == ConstantRescalePolicy::MapToUpper)
        {
            value = request.outputHigh;
        }
        if (!std::isfinite(value))
        {
            return arithmeticFailure(ErrorCode::OutOfRange,
                                     "the constant-image rescale policy produced no finite output");
        }
        Result<PointwiseProgram> constant = buildScaleBiasProgram(shape, 0.0, value);
        if (!constant.hasValue())
        {
            return constant.status();
        }
        return RescalePlan(std::move(*constant), RescaleDisposition::ConstantMapped, value);
    }

    const double scale = (request.outputHigh - request.outputLow) / (request.inputHigh - request.inputLow);
    const double bias = request.outputLow - request.inputLow * scale;
    if (!std::isfinite(scale) || !std::isfinite(bias))
    {
        return arithmeticFailure(ErrorCode::OutOfRange, "the requested rescale produces no finite affine transform");
    }
    std::optional<ClampInterval> clamp;
    if (request.clampOutput)
    {
        clamp = ClampInterval{.low = (std::min)(request.outputLow, request.outputHigh),
                              .high = (std::max)(request.outputLow, request.outputHigh)};
    }
    Result<PointwiseProgram> program = buildScaleBiasProgram(shape, scale, bias, clamp);
    if (!program.hasValue())
    {
        return program.status();
    }
    return RescalePlan(std::move(*program), RescaleDisposition::Affine, std::nullopt);
}

} // namespace an
