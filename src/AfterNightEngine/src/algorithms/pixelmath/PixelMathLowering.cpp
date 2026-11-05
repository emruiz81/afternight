// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - recognising the expressions that are already a fusable chain.
//
// A chain starts with a sample of an image at the pixel being written and then applies one step at
// a time to that running value. The steps recognised are the prefix `+`, `-` and `~`; adding,
// subtracting, multiplying or raising to a constant, and dividing by a constant whose reciprocal is
// exact; `clamp` or `range` between two ordered constants; and adding, subtracting, multiplying,
// dividing, comparing with, or taking the two-argument `min` or `max` of a sample of another image.
// The first instruction that fits none of them names the blocker, and so does anything the whole
// program uses that no chain can carry - a position, a draw, a variable, a branch, a `pixel()`
// sample, a colour component, a statistic or a second statement - which is scanned for first. Every
// image must also have the target's representation and extent, since a fused program states one
// representation for every source it reads.

#include <AfterNightEngine/Algorithms/PixelMath/PixelMathLowering.h>

#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgramBuilder.h>
#include <AfterNightEngine/Core/ErrorCode.h>

#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace an
{
namespace
{

constexpr const char* kOperation = "an::lowerPixelMathToPointwise";

/// Whether one over this number is exact.
///
/// Only a power of two has an exact reciprocal, and only then is multiplying by it the same value as
/// dividing by it. Everything else would make the fused chain and the interpreted expression compute
/// different numbers for the same input, which is worse than not fusing at all.
[[nodiscard]] bool hasExactReciprocal(double value) noexcept
{
    if (!std::isfinite(value) || value == 0.0)
    {
        return false;
    }
    int exponent = 0;
    const double mantissa = std::frexp(value, &exponent);
    if (mantissa != 0.5 && mantissa != -0.5)
    {
        return false;
    }
    const double reciprocal = 1.0 / value;
    return std::isfinite(reciprocal) && std::isnormal(reciprocal);
}

/// The fused comparison an infix comparison operator becomes, or nothing for any other operator.
[[nodiscard]] std::optional<PointwiseComparison> comparisonOf(PixelMathBinaryOperator op) noexcept
{
    switch (op)
    {
    case PixelMathBinaryOperator::Equal:
        return PointwiseComparison::Equal;
    case PixelMathBinaryOperator::NotEqual:
        return PointwiseComparison::NotEqual;
    case PixelMathBinaryOperator::Less:
        return PointwiseComparison::Less;
    case PixelMathBinaryOperator::LessOrEqual:
        return PointwiseComparison::LessOrEqual;
    case PixelMathBinaryOperator::Greater:
        return PointwiseComparison::Greater;
    case PixelMathBinaryOperator::GreaterOrEqual:
        return PointwiseComparison::GreaterOrEqual;
    default:
        break;
    }
    return std::nullopt;
}

} // namespace

/// Matches one compiled expression against the fusable chain shape.
///
/// The shape is recognised on the instruction stream rather than on the tree, because a chain has a
/// signature there that is impossible to mistake: one sample pushed, then a sequence of operands and
/// operators that each consume the running value. A tree walk would have to re-derive that the
/// left-hand side of every operator is the accumulator.
class PixelMathLowerer
{
public:
    PixelMathLowerer(const PixelMathProgram& program, const PixelMathLoweringOptions& options) noexcept
        : m_program(program), m_options(options)
    {
    }

    [[nodiscard]] Result<PixelMathLowering> run();

private:
    [[nodiscard]] PixelMathLowering blocked(PixelMathLoweringBlocker blocker) const
    {
        PixelMathLowering lowering;
        lowering.m_blocker = blocker;
        return lowering;
    }

    /// Which fused source plane one of the expression's images is, adding it when it is new.
    [[nodiscard]] std::optional<std::uint32_t> sourceOf(std::uint32_t image);

    [[nodiscard]] PixelMathLoweringBlocker scanForUnfusableInstructions() const;
    [[nodiscard]] PixelMathLoweringBlocker matchChain();

    const PixelMathProgram& m_program;
    PixelMathLoweringOptions m_options;
    std::vector<std::uint32_t> m_sources;
    PointwiseProgramBuilder m_builder;
    std::uint32_t m_stepCount = 0u;
};

std::optional<std::uint32_t> PixelMathLowerer::sourceOf(std::uint32_t image)
{
    for (std::size_t index = 0u; index < m_sources.size(); ++index)
    {
        if (m_sources[index] == image)
        {
            return static_cast<std::uint32_t>(index);
        }
    }
    if (m_sources.size() >= kMaxPointwiseSources)
    {
        return std::nullopt;
    }
    m_sources.push_back(image);
    return static_cast<std::uint32_t>(m_sources.size() - 1u);
}

PixelMathLoweringBlocker PixelMathLowerer::scanForUnfusableInstructions() const
{
    for (const PixelMathInstruction& instruction : m_program.instructions())
    {
        switch (instruction.opcode)
        {
        case PixelMathOpcode::PushColumn:
        case PixelMathOpcode::PushRow:
        case PixelMathOpcode::PushNormalizedColumn:
        case PixelMathOpcode::PushNormalizedRow:
        case PixelMathOpcode::PushChannel:
            return PixelMathLoweringBlocker::UsesPixelPosition;
        case PixelMathOpcode::PushRandom:
            return PixelMathLoweringBlocker::UsesRandom;
        case PixelMathOpcode::PushVariable:
        case PixelMathOpcode::StoreVariable:
            return PixelMathLoweringBlocker::UsesVariable;
        case PixelMathOpcode::JumpIfZero:
        case PixelMathOpcode::Jump:
            return PixelMathLoweringBlocker::UsesBranch;
        case PixelMathOpcode::SampleImage:
            return PixelMathLoweringBlocker::UsesImageSample;
        case PixelMathOpcode::PushColorComponent:
            return PixelMathLoweringBlocker::UsesColorComponent;
        case PixelMathOpcode::PushStatistic:
            return PixelMathLoweringBlocker::UsesImageStatistic;
        case PixelMathOpcode::Discard:
            return PixelMathLoweringBlocker::UsesMultipleStatements;
        default:
            break;
        }
    }
    return PixelMathLoweringBlocker::None;
}

PixelMathLoweringBlocker PixelMathLowerer::matchChain()
{
    const std::span<const PixelMathInstruction> instructions = m_program.instructions();
    const std::span<const PixelMathSampleRead> reads = m_program.sampleReads();
    const std::span<const double> constants = m_program.constants();

    if (instructions.empty() || instructions[0].opcode != PixelMathOpcode::PushSample)
    {
        return PixelMathLoweringBlocker::NotAChain;
    }
    if (reads[instructions[0].operand].selectsChannel)
    {
        return PixelMathLoweringBlocker::ChannelSelectionNotFusable;
    }
    if (!sourceOf(reads[instructions[0].operand].image).has_value())
    {
        return PixelMathLoweringBlocker::TooManyOperands;
    }

    const auto append = [&](const PointwiseOp& op) {
        m_builder.append(op);
        ++m_stepCount;
    };

    std::size_t at = 1u;
    while (at < instructions.size())
    {
        const PixelMathInstruction& instruction = instructions[at];
        if (instruction.opcode == PixelMathOpcode::ApplyUnary)
        {
            switch (instruction.unary)
            {
            case PixelMathUnaryOperator::Plus:
                break;
            case PixelMathUnaryOperator::Negate:
                append(pointwiseScale(-1.0));
                break;
            case PixelMathUnaryOperator::Complement:
                append(pointwiseScale(-1.0));
                append(pointwiseBias(1.0));
                break;
            case PixelMathUnaryOperator::Not:
                return PixelMathLoweringBlocker::OperationNotFusable;
            }
            ++at;
            continue;
        }

        if (instruction.opcode == PixelMathOpcode::PushConstant)
        {
            const double value = constants[instruction.operand];
            if (at + 1u < instructions.size() && instructions[at + 1u].opcode == PixelMathOpcode::ApplyBinary)
            {
                if (!std::isfinite(value))
                {
                    return PixelMathLoweringBlocker::CoefficientNotFinite;
                }
                switch (instructions[at + 1u].binary)
                {
                case PixelMathBinaryOperator::Add:
                    append(pointwiseBias(value));
                    break;
                case PixelMathBinaryOperator::Subtract:
                    append(pointwiseBias(-value));
                    break;
                case PixelMathBinaryOperator::Multiply:
                    append(pointwiseScale(value));
                    break;
                case PixelMathBinaryOperator::Divide:
                    if (!hasExactReciprocal(value))
                    {
                        return PixelMathLoweringBlocker::OperationNotFusable;
                    }
                    append(pointwiseScale(1.0 / value));
                    break;
                case PixelMathBinaryOperator::Power:
                    if (m_program.numericDomain() != NumericDomain::NormalizedLinear &&
                        m_program.numericDomain() != NumericDomain::EncodedNonlinear)
                        return PixelMathLoweringBlocker::OperationNotFusable;
                    append(pointwiseGamma(value));
                    break;
                default:
                    return PixelMathLoweringBlocker::OperationNotFusable;
                }
                at += 2u;
                continue;
            }
            if (at + 2u < instructions.size() && instructions[at + 1u].opcode == PixelMathOpcode::PushConstant &&
                instructions[at + 2u].opcode == PixelMathOpcode::CallFunction &&
                instructions[at + 2u].function == PixelMathFunctionId::Clamp && instructions[at + 2u].count == 3u)
            {
                const double high = constants[instructions[at + 1u].operand];
                if (!std::isfinite(value) || !std::isfinite(high))
                {
                    return PixelMathLoweringBlocker::CoefficientNotFinite;
                }
                if (value > high)
                {
                    return PixelMathLoweringBlocker::OperationNotFusable;
                }
                append(pointwiseClamp(value, high));
                at += 3u;
                continue;
            }
            return PixelMathLoweringBlocker::NotAChain;
        }

        if (instruction.opcode == PixelMathOpcode::PushSample)
        {
            const PixelMathSampleRead& read = reads[instruction.operand];
            if (read.selectsChannel)
            {
                return PixelMathLoweringBlocker::ChannelSelectionNotFusable;
            }
            const std::optional<std::uint32_t> source = sourceOf(read.image);
            if (!source.has_value())
            {
                return PixelMathLoweringBlocker::TooManyOperands;
            }
            if (at + 1u >= instructions.size())
            {
                return PixelMathLoweringBlocker::NotAChain;
            }
            const PixelMathInstruction& next = instructions[at + 1u];
            if (next.opcode == PixelMathOpcode::ApplyBinary)
            {
                switch (next.binary)
                {
                case PixelMathBinaryOperator::Add:
                    append(pointwiseAdd(*source));
                    break;
                case PixelMathBinaryOperator::Subtract:
                    append(pointwiseSubtract(*source));
                    break;
                case PixelMathBinaryOperator::Multiply:
                    append(pointwiseMultiply(*source));
                    break;
                case PixelMathBinaryOperator::Divide:
                    append(pointwiseDivide(*source));
                    break;
                default: {
                    const std::optional<PointwiseComparison> comparison = comparisonOf(next.binary);
                    if (!comparison.has_value())
                    {
                        return PixelMathLoweringBlocker::OperationNotFusable;
                    }
                    append(pointwiseCompare(*source, *comparison));
                    break;
                }
                }
                at += 2u;
                continue;
            }
            if (next.opcode == PixelMathOpcode::CallFunction && next.count == 2u)
            {
                if (next.function == PixelMathFunctionId::MinimumOfArguments)
                {
                    append(pointwiseMinimum(*source));
                }
                else if (next.function == PixelMathFunctionId::MaximumOfArguments)
                {
                    append(pointwiseMaximum(*source));
                }
                else
                {
                    return PixelMathLoweringBlocker::OperationNotFusable;
                }
                at += 2u;
                continue;
            }
            return PixelMathLoweringBlocker::NotAChain;
        }

        return PixelMathLoweringBlocker::NotAChain;
    }

    return PixelMathLoweringBlocker::None;
}

Result<PixelMathLowering> PixelMathLowerer::run()
{
    if (m_program.instructions().empty() || !m_program.targetExtent().has_value())
    {
        return Status::failure(ErrorCode::InvalidArgument,
                               StatusDetail{.message = "an empty expression program was given to the lowering",
                                            .operation = kOperation,
                                            .domain = ErrorDomain::Algorithms});
    }

    const PixelMathLoweringBlocker unfusable = scanForUnfusableInstructions();
    if (unfusable != PixelMathLoweringBlocker::None)
    {
        return blocked(unfusable);
    }

    const ImageExtent target = *m_program.targetExtent();
    if (target.channelCount() > kMaxPointwiseChannels)
    {
        return blocked(PixelMathLoweringBlocker::TooManyOperands);
    }
    const SampleType sampleType = m_program.images()[0].sampleType;
    for (const PixelMathImageDeclaration& declaration : m_program.images())
    {
        if (declaration.sampleType != sampleType || !(declaration.extent == target))
        {
            return blocked(PixelMathLoweringBlocker::OperandsDoNotAgree);
        }
    }

    const PixelMathLoweringBlocker matched = matchChain();
    if (matched != PixelMathLoweringBlocker::None)
    {
        return blocked(matched);
    }
    if (m_stepCount == 0u)
    {
        // The expression is one image and nothing else. An empty pointwise program is not the
        // identity, so the identity is written down: a caller that asked to copy an image asked for
        // it deliberately, and a chain that says so is a chain the executor can run.
        m_builder.append(pointwiseScale(1.0));
    }

    m_builder.channels(target.channelCount())
        .sources(static_cast<std::uint32_t>(m_sources.size()))
        .readingSamples(sampleType)
        .readingDomain(m_program.numericDomain())
        .writingDomain(m_program.numericDomain())
        .promising(m_options.outputContract)
        .computingIn(m_options.precision, m_options.precisionJustification);
    if (m_options.allowContraction)
    {
        m_builder.allowingContraction();
    }

    Result<PointwiseProgram> built = m_builder.build();
    if (!built.hasValue())
    {
        return built.status();
    }

    PixelMathLowering lowering;
    lowering.m_program.emplace(std::move(*built));
    lowering.m_sourceImages = std::move(m_sources);
    lowering.m_blocker = PixelMathLoweringBlocker::None;
    return lowering;
}

const char* pixelMathLoweringBlockerName(PixelMathLoweringBlocker blocker) noexcept
{
    switch (blocker)
    {
    case PixelMathLoweringBlocker::None:
        return "none";
    case PixelMathLoweringBlocker::NotAChain:
        return "not-a-chain";
    case PixelMathLoweringBlocker::UsesPixelPosition:
        return "uses-pixel-position";
    case PixelMathLoweringBlocker::UsesRandom:
        return "uses-random";
    case PixelMathLoweringBlocker::UsesVariable:
        return "uses-variable";
    case PixelMathLoweringBlocker::UsesBranch:
        return "uses-branch";
    case PixelMathLoweringBlocker::UsesImageSample:
        return "uses-image-sample";
    case PixelMathLoweringBlocker::UsesColorComponent:
        return "uses-color-component";
    case PixelMathLoweringBlocker::UsesImageStatistic:
        return "uses-image-statistic";
    case PixelMathLoweringBlocker::UsesMultipleStatements:
        return "uses-multiple-statements";
    case PixelMathLoweringBlocker::OperationNotFusable:
        return "operation-not-fusable";
    case PixelMathLoweringBlocker::ChannelSelectionNotFusable:
        return "channel-selection-not-fusable";
    case PixelMathLoweringBlocker::TooManyOperands:
        return "too-many-operands";
    case PixelMathLoweringBlocker::OperandsDoNotAgree:
        return "operands-do-not-agree";
    case PixelMathLoweringBlocker::CoefficientNotFinite:
        return "coefficient-not-finite";
    }
    return "unknown";
}

Result<PixelMathLowering> lowerPixelMathToPointwise(const PixelMathProgram& program,
                                                    const PixelMathLoweringOptions& options)
{
    PixelMathLowerer lowerer(program, options);
    return lowerer.run();
}

} // namespace an
