// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - turning a resolved expression into instructions over a stack.
//
// A post-order walk of the resolved tree. A number, a constant, a sample, a variable, a statistic, a
// position, a random draw or a colour component is one push; an operator or a call emits its
// operands and then one instruction that pops them and pushes the answer. `iif` becomes a
// conditional jump over its first branch and a jump over its second, so only the chosen branch runs.
// An assignment stores the top of the stack and leaves it there, a compound assignment loads the
// variable first, and a sequence discards every statement's value but the last.
//
// Everything a program can know before its first pixel is decided here: the pool holds each
// constant value once, the language's constants and a bound image's width, height, channel count and
// colour flag are folded into it, sample and colour reads are numbered once each, and the stack
// depth is counted exactly. A program that reads a selected channel, samples with `pixel()` or reads
// a colour component is marked as unable to run in place.

#include <AfterNightEngine/Algorithms/PixelMath/PixelMathProgram.h>

#include <AfterNightEngine/Core/ErrorCode.h>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace an
{
namespace
{

constexpr const char* kOperation = "an::compilePixelMath";

/// The language's own constants, to twenty decimal places.
constexpr double kPi = 3.14159265358979323846;

/// Euler's number, the language's `e`.
constexpr double kEuler = 2.71828182845904523536;

/// Whether a call ranks a copy of its arguments, and therefore needs room for one above them.
[[nodiscard]] constexpr bool needsArgumentScratch(PixelMathFunctionId function) noexcept
{
    return function == PixelMathFunctionId::MedianAbsoluteDeviationOfArguments ||
           function == PixelMathFunctionId::BiweightMidvarianceOfArguments;
}

[[nodiscard]] Status compileFailure(ErrorCode code, const char* message)
{
    return Status::failure(
        code, StatusDetail{.message = message, .operation = kOperation, .domain = ErrorDomain::Algorithms});
}

} // namespace

/// Emits one compiled expression.
///
/// The walk is recursive over the tree, which is safe because the parse bounded the tree's depth and
/// this compiler refuses one deeper than it will descend. Every `emit` leaves exactly one value on
/// the stack, which is what makes the depth arithmetic below a running count rather than a
/// simulation of the jumps: the only correction is at a conditional's second branch, which starts
/// from the depth the first one started from.
class PixelMathCompiler
{
public:
    explicit PixelMathCompiler(const PixelMathResolution& resolution) noexcept : m_resolution(resolution) {}

    [[nodiscard]] Result<PixelMathProgram> run();

private:
    void push(PixelMathInstruction instruction, std::int32_t stackChange)
    {
        m_program.m_instructions.push_back(instruction);
        m_depth = static_cast<std::uint32_t>(static_cast<std::int64_t>(m_depth) + stackChange);
        if (m_depth > m_maxDepth)
        {
            m_maxDepth = m_depth;
        }
    }

    [[nodiscard]] std::uint32_t constantIndex(double value)
    {
        const auto found =
            std::find_if(m_program.m_constants.begin(), m_program.m_constants.end(), [value](double candidate) {
                return std::bit_cast<std::uint64_t>(candidate) == std::bit_cast<std::uint64_t>(value);
            });
        if (found != m_program.m_constants.end())
        {
            return static_cast<std::uint32_t>(std::distance(m_program.m_constants.begin(), found));
        }
        m_program.m_constants.push_back(value);
        return static_cast<std::uint32_t>(m_program.m_constants.size() - 1u);
    }

    [[nodiscard]] std::uint32_t sampleReadIndex(std::uint32_t image, std::uint32_t channel, bool selectsChannel)
    {
        for (std::size_t index = 0u; index < m_program.m_sampleReads.size(); ++index)
        {
            const PixelMathSampleRead& read = m_program.m_sampleReads[index];
            if (read.image == image && read.selectsChannel == selectsChannel &&
                (!selectsChannel || read.channel == channel))
            {
                return static_cast<std::uint32_t>(index);
            }
        }
        m_program.m_sampleReads.push_back(PixelMathSampleRead{image, channel, selectsChannel});
        if (selectsChannel)
        {
            m_program.m_supportsInPlace = false;
        }
        return static_cast<std::uint32_t>(m_program.m_sampleReads.size() - 1u);
    }

    [[nodiscard]] std::uint32_t colorReadIndex(std::uint32_t image, PixelMathFunctionId component)
    {
        for (std::size_t index = 0u; index < m_program.m_colorReads.size(); ++index)
        {
            const PixelMathColorRead& read = m_program.m_colorReads[index];
            if (read.image == image && read.component == component)
            {
                return static_cast<std::uint32_t>(index);
            }
        }
        m_program.m_colorReads.push_back(PixelMathColorRead{image, component});
        m_program.m_supportsInPlace = false;
        return static_cast<std::uint32_t>(m_program.m_colorReads.size() - 1u);
    }

    [[nodiscard]] bool emit(std::uint32_t nodeIndex, std::uint32_t depth);
    [[nodiscard]] bool emitCall(std::uint32_t nodeIndex, std::uint32_t depth);
    [[nodiscard]] bool emitBranch(std::uint32_t nodeIndex, std::uint32_t depth);

    const PixelMathResolution& m_resolution;
    PixelMathProgram m_program;
    std::uint32_t m_depth = 0u;
    std::uint32_t m_maxDepth = 0u;
    bool m_tooDeep = false;
    bool m_defect = false;
};

bool PixelMathCompiler::emitBranch(std::uint32_t nodeIndex, std::uint32_t depth)
{
    const std::span<const std::uint32_t> arguments = m_resolution.tree().children(nodeIndex);
    if (arguments.size() != 3u)
    {
        m_defect = true;
        return false;
    }
    if (!emit(arguments[0], depth + 1u))
    {
        return false;
    }
    const std::size_t jumpIfZero = m_program.m_instructions.size();
    push(PixelMathInstruction{.opcode = PixelMathOpcode::JumpIfZero}, -1);

    const std::uint32_t base = m_depth;
    if (!emit(arguments[1], depth + 1u))
    {
        return false;
    }
    const std::size_t jump = m_program.m_instructions.size();
    push(PixelMathInstruction{.opcode = PixelMathOpcode::Jump}, 0);

    m_program.m_instructions[jumpIfZero].operand = static_cast<std::uint32_t>(m_program.m_instructions.size());
    // The unchosen branch never ran, so the stack at the label is what it was before either branch.
    m_depth = base;
    if (!emit(arguments[2], depth + 1u))
    {
        return false;
    }
    m_program.m_instructions[jump].operand = static_cast<std::uint32_t>(m_program.m_instructions.size());
    return true;
}

bool PixelMathCompiler::emitCall(std::uint32_t nodeIndex, std::uint32_t depth)
{
    const PixelMathResolvedNode& resolved = m_resolution.nodes()[nodeIndex];
    const std::span<const std::uint32_t> arguments = m_resolution.tree().children(nodeIndex);
    const std::uint32_t argumentCount = static_cast<std::uint32_t>(arguments.size());

    switch (resolved.reference)
    {
    case PixelMathReference::ImageStatistic:
        push(PixelMathInstruction{.opcode = PixelMathOpcode::PushStatistic, .operand = resolved.slot}, 1);
        return true;
    case PixelMathReference::Function:
        break;
    default:
        m_defect = true;
        return false;
    }

    switch (resolved.function)
    {
    case PixelMathFunctionId::Pi:
        push(PixelMathInstruction{.opcode = PixelMathOpcode::PushConstant, .operand = constantIndex(kPi)}, 1);
        return true;
    case PixelMathFunctionId::EulerNumber:
        push(PixelMathInstruction{.opcode = PixelMathOpcode::PushConstant, .operand = constantIndex(kEuler)}, 1);
        return true;
    case PixelMathFunctionId::CurrentChannel:
        push(PixelMathInstruction{.opcode = PixelMathOpcode::PushChannel}, 1);
        return true;
    case PixelMathFunctionId::CurrentColumn:
        push(PixelMathInstruction{.opcode = PixelMathOpcode::PushColumn}, 1);
        return true;
    case PixelMathFunctionId::CurrentRow:
        push(PixelMathInstruction{.opcode = PixelMathOpcode::PushRow}, 1);
        return true;
    case PixelMathFunctionId::NormalizedColumn:
        push(PixelMathInstruction{.opcode = PixelMathOpcode::PushNormalizedColumn}, 1);
        return true;
    case PixelMathFunctionId::NormalizedRow:
        push(PixelMathInstruction{.opcode = PixelMathOpcode::PushNormalizedRow}, 1);
        return true;
    case PixelMathFunctionId::UniformRandom:
        m_program.m_usesRandom = true;
        push(PixelMathInstruction{.opcode = PixelMathOpcode::PushRandom}, 1);
        return true;
    case PixelMathFunctionId::ImageWidth:
    case PixelMathFunctionId::ImageHeight:
    case PixelMathFunctionId::ImageChannelCount:
    case PixelMathFunctionId::ImageIsColor: {
        // A bound image's geometry is known while there is still one expression rather than
        // seventy-two million pixels, so it is a constant rather than an instruction.
        const ImageExtent extent = m_resolution.images()[resolved.slot].extent;
        double value = 0.0;
        switch (resolved.function)
        {
        case PixelMathFunctionId::ImageWidth:
            value = static_cast<double>(extent.width());
            break;
        case PixelMathFunctionId::ImageHeight:
            value = static_cast<double>(extent.height());
            break;
        case PixelMathFunctionId::ImageChannelCount:
            value = static_cast<double>(extent.channelCount());
            break;
        default:
            value = extent.channelCount() >= 3u ? 1.0 : 0.0;
            break;
        }
        push(PixelMathInstruction{.opcode = PixelMathOpcode::PushConstant, .operand = constantIndex(value)}, 1);
        return true;
    }
    case PixelMathFunctionId::CieLightness:
    case PixelMathFunctionId::CieGreenRed:
    case PixelMathFunctionId::CieBlueYellow:
    case PixelMathFunctionId::CieChroma:
    case PixelMathFunctionId::CieHue:
    case PixelMathFunctionId::CieX:
    case PixelMathFunctionId::CieY:
    case PixelMathFunctionId::CieZ:
    case PixelMathFunctionId::HslHue:
    case PixelMathFunctionId::HslSaturation:
    case PixelMathFunctionId::HslLightness:
    case PixelMathFunctionId::HsvHue:
    case PixelMathFunctionId::HsvSaturation:
    case PixelMathFunctionId::HsvValue:
    case PixelMathFunctionId::LumaY:
    case PixelMathFunctionId::ChromaBlue:
    case PixelMathFunctionId::ChromaRed:
        push(PixelMathInstruction{.opcode = PixelMathOpcode::PushColorComponent,
                                  .operand = colorReadIndex(resolved.slot, resolved.function)},
             1);
        return true;
    case PixelMathFunctionId::SampleAt: {
        m_program.m_supportsInPlace = false;
        for (std::uint32_t index = 1u; index < argumentCount; ++index)
        {
            if (!emit(arguments[index], depth + 1u))
            {
                return false;
            }
        }
        const std::uint32_t popped = argumentCount - 1u;
        push(PixelMathInstruction{.opcode = PixelMathOpcode::SampleImage, .operand = resolved.slot, .count = popped},
             -static_cast<std::int32_t>(popped) + 1);
        return true;
    }
    case PixelMathFunctionId::Conditional:
        return emitBranch(nodeIndex, depth);
    case PixelMathFunctionId::InRectangle:
    case PixelMathFunctionId::InEllipse: {
        // A region test is a function of its four stated numbers and of where the pixel is, so the
        // column and the row being written follow the arguments onto the stack and the call takes six.
        for (std::uint32_t index = 0u; index < argumentCount; ++index)
        {
            if (!emit(arguments[index], depth + 1u))
            {
                return false;
            }
        }
        push(PixelMathInstruction{.opcode = PixelMathOpcode::PushColumn}, 1);
        push(PixelMathInstruction{.opcode = PixelMathOpcode::PushRow}, 1);
        const std::uint32_t popped = argumentCount + 2u;
        push(PixelMathInstruction{.opcode = PixelMathOpcode::CallFunction,
                                  .function = resolved.function,
                                  .count = popped},
             -static_cast<std::int32_t>(popped) + 1);
        return true;
    }
    default:
        break;
    }

    for (std::uint32_t index = 0u; index < argumentCount; ++index)
    {
        if (!emit(arguments[index], depth + 1u))
        {
            return false;
        }
    }
    if (needsArgumentScratch(resolved.function))
    {
        // A deviation estimator ranks a copy of its arguments rather than the arguments themselves,
        // and the copy lives above them on the same stack. Reserving it here is what keeps the
        // evaluator allocation-free without the caller having to know which functions need it.
        const std::uint32_t needed = m_depth + argumentCount;
        if (needed > m_maxDepth)
        {
            m_maxDepth = needed;
        }
    }
    push(PixelMathInstruction{.opcode = PixelMathOpcode::CallFunction,
                              .function = resolved.function,
                              .count = argumentCount},
         -static_cast<std::int32_t>(argumentCount) + 1);
    return true;
}

bool PixelMathCompiler::emit(std::uint32_t nodeIndex, std::uint32_t depth)
{
    if (depth > kMaxPixelMathCompileDepth)
    {
        m_tooDeep = true;
        return false;
    }
    const PixelMathSyntaxTree& tree = m_resolution.tree();
    const PixelMathNode& node = tree.nodes()[nodeIndex];
    const PixelMathResolvedNode& resolved = m_resolution.nodes()[nodeIndex];

    switch (node.kind)
    {
    case PixelMathSyntaxKind::Number:
        push(PixelMathInstruction{.opcode = PixelMathOpcode::PushConstant, .operand = constantIndex(node.number)}, 1);
        return true;
    case PixelMathSyntaxKind::Name:
    case PixelMathSyntaxKind::MetaSymbol:
        switch (resolved.reference)
        {
        case PixelMathReference::Image:
            push(PixelMathInstruction{.opcode = PixelMathOpcode::PushSample,
                                      .operand = sampleReadIndex(resolved.slot, resolved.channel, resolved.hasChannel)},
                 1);
            return true;
        case PixelMathReference::Constant:
            push(PixelMathInstruction{.opcode = PixelMathOpcode::PushConstant,
                                      .operand = constantIndex(m_resolution.constants()[resolved.slot])},
                 1);
            return true;
        case PixelMathReference::Variable:
            push(PixelMathInstruction{.opcode = PixelMathOpcode::PushVariable, .operand = resolved.slot}, 1);
            return true;
        default:
            m_defect = true;
            return false;
        }
    case PixelMathSyntaxKind::Unary: {
        const std::span<const std::uint32_t> children = tree.children(nodeIndex);
        if (children.size() != 1u || !emit(children[0], depth + 1u))
        {
            m_defect = m_defect || children.size() != 1u;
            return false;
        }
        push(PixelMathInstruction{.opcode = PixelMathOpcode::ApplyUnary, .unary = node.unary}, 0);
        return true;
    }
    case PixelMathSyntaxKind::Binary: {
        const std::span<const std::uint32_t> children = tree.children(nodeIndex);
        if (children.size() != 2u)
        {
            m_defect = true;
            return false;
        }
        if (!emit(children[0], depth + 1u) || !emit(children[1], depth + 1u))
        {
            return false;
        }
        push(PixelMathInstruction{.opcode = PixelMathOpcode::ApplyBinary, .binary = node.binary}, -1);
        return true;
    }
    case PixelMathSyntaxKind::Call:
        return emitCall(nodeIndex, depth);
    case PixelMathSyntaxKind::Assignment: {
        const std::span<const std::uint32_t> children = tree.children(nodeIndex);
        if (children.size() != 2u)
        {
            m_defect = true;
            return false;
        }
        const std::uint32_t slot = m_resolution.nodes()[children[0]].slot;
        if (node.assignment == PixelMathAssignmentOperator::Assign)
        {
            if (!emit(children[1], depth + 1u))
            {
                return false;
            }
        }
        else
        {
            push(PixelMathInstruction{.opcode = PixelMathOpcode::PushVariable, .operand = slot}, 1);
            if (!emit(children[1], depth + 1u))
            {
                return false;
            }
            push(PixelMathInstruction{.opcode = PixelMathOpcode::ApplyBinary,
                                      .binary = pixelMathAssignmentOperation(node.assignment)},
                 -1);
        }
        push(PixelMathInstruction{.opcode = PixelMathOpcode::StoreVariable, .operand = slot}, 0);
        return true;
    }
    case PixelMathSyntaxKind::Sequence: {
        const std::span<const std::uint32_t> children = tree.children(nodeIndex);
        if (children.empty())
        {
            m_defect = true;
            return false;
        }
        for (std::size_t index = 0u; index + 1u < children.size(); ++index)
        {
            if (!emit(children[index], depth + 1u))
            {
                return false;
            }
            push(PixelMathInstruction{.opcode = PixelMathOpcode::Discard}, -1);
        }
        return emit(children.back(), depth + 1u);
    }
    }
    m_defect = true;
    return false;
}

Result<PixelMathProgram> PixelMathCompiler::run()
{
    if (!m_resolution.succeeded())
    {
        return compileFailure(ErrorCode::InvalidArgument, "a refused resolution was given to the expression compiler");
    }
    if (m_resolution.tree().maxDepth() > kMaxPixelMathCompileDepth)
    {
        return Status::failure(
            ErrorCode::LimitExceeded,
            StatusDetail{
                .message = "the expression nests deeper than this engine will compile",
                .operation = kOperation,
                .domain = ErrorDomain::Algorithms,
                .context = {StatusContextField::ofCount(ContextKey::Limit, kMaxPixelMathCompileDepth),
                            StatusContextField::ofCount(ContextKey::Requested, m_resolution.tree().maxDepth())}});
    }

    m_program.m_images.assign(m_resolution.images().begin(), m_resolution.images().end());
    m_program.m_statistics.assign(m_resolution.statistics().begin(), m_resolution.statistics().end());
    m_program.m_variableInitialValues.assign(m_resolution.variableInitialValues().begin(),
                                             m_resolution.variableInitialValues().end());
    m_program.m_targetExtent = m_resolution.targetExtent();
    m_program.m_numericDomain = m_resolution.numericDomain();

    if (!emit(m_resolution.tree().rootIndex(), 1u))
    {
        if (m_tooDeep)
        {
            return compileFailure(ErrorCode::LimitExceeded,
                                  "the expression nests deeper than this engine will compile");
        }
        return compileFailure(ErrorCode::Internal,
                              "the resolved expression holds something this compiler cannot lower");
    }
    if (m_defect)
    {
        return compileFailure(ErrorCode::Internal,
                              "the resolved expression holds something this compiler cannot lower");
    }
    if (m_maxDepth > kMaxPixelMathStackDepth)
    {
        return Status::failure(
            ErrorCode::LimitExceeded,
            StatusDetail{.message = "the expression needs a deeper evaluation stack than this engine will allocate",
                         .operation = kOperation,
                         .domain = ErrorDomain::Algorithms,
                         .context = {StatusContextField::ofCount(ContextKey::Limit, kMaxPixelMathStackDepth),
                                     StatusContextField::ofCount(ContextKey::Requested, m_maxDepth)}});
    }
    m_program.m_stackDepth = m_maxDepth;
    return std::move(m_program);
}

Result<PixelMathProgram> compilePixelMath(const PixelMathResolution& resolution)
{
    PixelMathCompiler compiler(resolution);
    return compiler.run();
}

} // namespace an
