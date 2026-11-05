// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A resolved expression turned into something that can be evaluated per
/// pixel without deciding anything per pixel.
///
/// Evaluating the tree directly would walk the same nodes for every pixel of every channel, following
/// pointers, switching on a node kind and calling through a function pointer stored on the node - for
/// a twenty-four-megapixel colour frame, seventy-two million times. What that costs is not only the
/// indirection - it is that every decision the expression already made is made again at every pixel.
///
/// So the tree is compiled once into a flat instruction stream over a stack of doubles. Everything
/// that can be decided while there is still one expression rather than seventy-two million pixels is
/// decided here: which image a sample comes from, which channel, which statistic, where a branch
/// jumps to, and how deep the stack has to be. What is left is a switch over a contiguous array, and
/// the working memory one evaluation needs is a fixed number of doubles the caller owns.
///
/// Nothing in a program is a pointer into an image. A program is planned once and executed against
/// whatever images are bound to it, exactly as a pointwise program is, which is what lets one
/// expression be compiled once and run over a sequence of frames.

#pragma once

#include <AfterNightEngine/Algorithms/PixelMath/PixelMathFunctions.h>
#include <AfterNightEngine/Algorithms/PixelMath/PixelMathResolution.h>
#include <AfterNightEngine/Algorithms/PixelMath/PixelMathSyntax.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>

#include <cstdint>
#include <span>
#include <vector>

namespace an
{

/// The deepest expression this engine will compile.
///
/// The compiler descends the tree once, so this is what bounds that descent: callers may raise the
/// parse bound to inspect generated input, but compilation refuses that deeper tree before
/// recursive emission can exhaust a platform's stack. It counts levels, as
/// `PixelMathSyntaxTree::maxDepth` does, and has the same value as the parser's default
/// `PixelMathParseLimits::maxDepth`, which counts steps; so an expression that takes every nesting
/// step the default parse admits reports one level more than this and is refused here.
inline constexpr std::uint32_t kMaxPixelMathCompileDepth = 64u;

/// The most values one expression will hold on its evaluation stack.
///
/// Computed exactly while compiling and refused past this, so the caller's scratch is a fixed size
/// rather than a guess and no evaluation can overrun it.
inline constexpr std::uint32_t kMaxPixelMathStackDepth = 256u;

/// One step of a compiled expression.
enum class PixelMathOpcode : std::uint8_t
{
    /// Pushes `constants()[operand]`.
    PushConstant = 0u,

    /// Pushes the sample named by `sampleReads()[operand]` at the pixel being evaluated.
    PushSample = 1u,

    /// Pushes `variables[operand]`.
    PushVariable = 2u,

    /// Pushes the whole-image statistic `statistics()[operand]` for the channel being written.
    PushStatistic = 3u,

    /// Pushes the column being written.
    PushColumn = 4u,

    /// Pushes the row being written.
    PushRow = 5u,

    /// Pushes the column divided by one less than the destination width, or zero for a
    /// one-pixel-wide destination.
    PushNormalizedColumn = 6u,

    /// Pushes the row divided by one less than the destination height, or zero for a
    /// one-pixel-high destination.
    PushNormalizedRow = 7u,

    /// Pushes the channel being written.
    PushChannel = 8u,

    /// Pushes the next value of the execution's random stream for this pixel.
    PushRandom = 9u,

    /// Pushes the colour component named by `colorReads()[operand]` of the pixel being evaluated.
    PushColorComponent = 10u,

    /// Pops `count` coordinates and pushes a sample of image `operand` read at them.
    ///
    /// The coordinates are the arguments of the sampling function after the image: a column and a
    /// row, and a channel when one was written.
    SampleImage = 11u,

    /// Replaces the value on top with `unary` applied to it.
    ApplyUnary = 12u,

    /// Pops two values and pushes `binary` applied to them, left below right.
    ApplyBinary = 13u,

    /// Pops `count` values and pushes what `function` makes of them.
    CallFunction = 14u,

    /// Stores the value on top into `variables[operand]`, leaving it on the stack.
    StoreVariable = 15u,

    /// Pops one value and discards it. What separates one statement from the next.
    Discard = 16u,

    /// Pops one value and continues at instruction `operand` when it is zero.
    JumpIfZero = 17u,

    /// Continues at instruction `operand`.
    Jump = 18u
};

/// Whether a value names an instruction this build executes.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param opcode the value to inspect.
/// @return true for every supported opcode.
[[nodiscard]] constexpr bool isPixelMathOpcode(PixelMathOpcode opcode) noexcept
{
    return static_cast<std::uint8_t>(opcode) <= static_cast<std::uint8_t>(PixelMathOpcode::Jump);
}

/// One instruction and its operands.
///
/// Which members carry meaning is decided by `opcode`, and every other member is held at the neutral
/// value shown here.
struct PixelMathInstruction
{
    /// What this step does.
    PixelMathOpcode opcode = PixelMathOpcode::PushConstant;

    /// `ApplyUnary`'s operator. `Plus` elsewhere.
    PixelMathUnaryOperator unary = PixelMathUnaryOperator::Plus;

    /// `ApplyBinary`'s operator. `Add` elsewhere.
    PixelMathBinaryOperator binary = PixelMathBinaryOperator::Add;

    /// `CallFunction`'s function. `AbsoluteValue` elsewhere.
    PixelMathFunctionId function = PixelMathFunctionId::AbsoluteValue;

    /// A table index, a variable index, an image index, or a jump target, as `opcode` says. Zero
    /// elsewhere.
    std::uint32_t operand = 0u;

    /// How many values `CallFunction` and `SampleImage` pop. Zero elsewhere.
    std::uint32_t count = 0u;
};

/// One sample of one bound image, read at the pixel being evaluated.
struct PixelMathSampleRead
{
    /// Which of the program's images.
    std::uint32_t image = 0u;

    /// Which channel, when the expression named one.
    std::uint32_t channel = 0u;

    /// Whether the expression named a channel. When it did not, the channel being written is read,
    /// except from a single-channel image, whose one channel is broadcast.
    bool selectsChannel = false;
};

/// One colour component of one bound image, computed at the pixel being evaluated.
struct PixelMathColorRead
{
    /// Which of the program's images.
    std::uint32_t image = 0u;

    /// Which component. Always one of the colour-component functions.
    PixelMathFunctionId component = PixelMathFunctionId::CieLightness;
};

/// A compiled expression: instructions, tables, and everything an execution must bind.
///
/// Immutable once built, and produced only by `compilePixelMath`. Copyable and movable, and safe to
/// read from any number of threads at once.
class PixelMathProgram
{
public:
    /// A program that computes nothing.
    ///
    /// The state a default-constructed value holds. Executing one is refused.
    PixelMathProgram() = default;

    /// Copies a program, including its instructions and tables.
    PixelMathProgram(const PixelMathProgram&) = default;

    /// Takes over a program's instructions and tables.
    PixelMathProgram(PixelMathProgram&&) noexcept = default;

    /// Copies a program over this one.
    ///
    /// @return this program.
    PixelMathProgram& operator=(const PixelMathProgram&) = default;

    /// Takes over a program's instructions and tables, leaving the other empty.
    ///
    /// @return this program.
    PixelMathProgram& operator=(PixelMathProgram&&) noexcept = default;

    /// Releases the instructions and tables.
    ~PixelMathProgram() = default;

    /// The instructions, in execution order.
    ///
    /// Borrowed for as long as this program lives.
    ///
    /// @return the instructions, empty for a program that computes nothing.
    [[nodiscard]] std::span<const PixelMathInstruction> instructions() const noexcept
    {
        return std::span<const PixelMathInstruction>(m_instructions);
    }

    /// Every constant the instructions read.
    ///
    /// Includes the language's own constants and every fact about a bound image's geometry, both of
    /// which are known while compiling and are therefore not computed per pixel.
    ///
    /// Borrowed for as long as this program lives.
    ///
    /// @return the constants.
    [[nodiscard]] std::span<const double> constants() const noexcept
    {
        return std::span<const double>(m_constants);
    }

    /// Every sample read the instructions perform at the pixel being evaluated.
    ///
    /// Borrowed for as long as this program lives.
    ///
    /// @return the sample reads.
    [[nodiscard]] std::span<const PixelMathSampleRead> sampleReads() const noexcept
    {
        return std::span<const PixelMathSampleRead>(m_sampleReads);
    }

    /// Every colour component the instructions compute at the pixel being evaluated.
    ///
    /// Borrowed for as long as this program lives.
    ///
    /// @return the colour reads.
    [[nodiscard]] std::span<const PixelMathColorRead> colorReads() const noexcept
    {
        return std::span<const PixelMathColorRead>(m_colorReads);
    }

    /// Every whole-image statistic the program needs, in the order an execution supplies them.
    ///
    /// Borrowed for as long as this program lives.
    ///
    /// @return the statistic requests.
    [[nodiscard]] std::span<const PixelMathStatisticRequest> statistics() const noexcept
    {
        return std::span<const PixelMathStatisticRequest>(m_statistics);
    }

    /// What each variable holds at the start of every pixel.
    ///
    /// Borrowed for as long as this program lives.
    ///
    /// @return the initial values, one per variable.
    [[nodiscard]] std::span<const double> variableInitialValues() const noexcept
    {
        return std::span<const double>(m_variableInitialValues);
    }

    /// What each of the program's images is, in the order an execution binds them.
    ///
    /// Index zero is the target. Borrowed for as long as this program lives.
    ///
    /// @return the declarations.
    [[nodiscard]] std::span<const PixelMathImageDeclaration> images() const noexcept
    {
        return std::span<const PixelMathImageDeclaration>(m_images);
    }

    /// How many values one evaluation may hold at once.
    ///
    /// Computed exactly while compiling, so a caller sizes its scratch from this rather than from a
    /// limit.
    ///
    /// @return the stack depth, at most `kMaxPixelMathStackDepth`.
    [[nodiscard]] std::uint32_t stackDepth() const noexcept
    {
        return m_stackDepth;
    }

    /// The extent of the image being written.
    ///
    /// @return the target extent this program was compiled for, or no value for a program that
    ///         computes nothing.
    [[nodiscard]] std::optional<ImageExtent> targetExtent() const noexcept
    {
        return m_targetExtent;
    }

    /// What the samples this program reads and writes mean.
    ///
    /// @return the numeric domain every bound image shares.
    [[nodiscard]] NumericDomain numericDomain() const noexcept
    {
        return m_numericDomain;
    }

    /// Whether the program draws from the execution's random stream.
    ///
    /// @return true when any instruction pushes a random value.
    [[nodiscard]] bool usesRandom() const noexcept
    {
        return m_usesRandom;
    }

    /// Whether a destination that is exactly one of the bound images is safe.
    ///
    /// True when every read the program makes at the pixel being evaluated is of the channel being
    /// written, which is the only case in which writing a sample cannot change what a later pixel or
    /// channel reads. An explicit channel selector, the sampling function and a colour component all
    /// make it false.
    ///
    /// @return true when the program may be executed in place.
    [[nodiscard]] bool supportsInPlace() const noexcept
    {
        return m_supportsInPlace;
    }

private:
    friend class PixelMathCompiler;

    std::vector<PixelMathInstruction> m_instructions;
    std::vector<double> m_constants;
    std::vector<PixelMathSampleRead> m_sampleReads;
    std::vector<PixelMathColorRead> m_colorReads;
    std::vector<PixelMathStatisticRequest> m_statistics;
    std::vector<double> m_variableInitialValues;
    std::vector<PixelMathImageDeclaration> m_images;
    std::uint32_t m_stackDepth = 0u;
    std::optional<ImageExtent> m_targetExtent{};
    NumericDomain m_numericDomain = NumericDomain::NormalizedLinear;
    bool m_usesRandom = false;
    bool m_supportsInPlace = true;
};

/// The one thing that compiles a resolution. Defined in the engine's implementation.
///
/// Named here rather than published for the reason the parser and the resolver are: it exists so
/// that the type above has exactly one producer.
class PixelMathCompiler;

/// Turns a resolved expression into instructions.
///
/// The resolution is borrowed for the call; the program copies what it needs, so it outlives the
/// resolution and the tree behind it.
///
/// Everything a pixel does not have to decide is decided here: a bound image's width, height and
/// channel count become constants, the language's own constants are folded, a branch becomes a jump,
/// and the stack depth is computed exactly.
///
/// Thread-safe and reentrant. Allocates the program.
///
/// @param resolution a resolution that succeeded.
/// @return the program; `ErrorCode::InvalidArgument` for a resolution that was refused;
///         `ErrorCode::LimitExceeded` when the expression is deeper than
///         `kMaxPixelMathCompileDepth` or needs a stack above `kMaxPixelMathStackDepth`; or
///         `ErrorCode::Internal` when the resolution does not describe something this compiler can
///         lower, which is a defect rather than a caller's mistake.
[[nodiscard]] AFTERNIGHTENGINE_API Result<PixelMathProgram> compilePixelMath(const PixelMathResolution& resolution);

} // namespace an
