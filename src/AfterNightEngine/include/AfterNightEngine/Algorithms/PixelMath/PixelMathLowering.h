// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The expressions that are already a fusable chain, and why the rest are not.
///
/// Most of what people write in this language is a chain: take an image, scale it, subtract another,
/// clamp the result. That is exactly what `Algorithms/Pointwise/PointwiseProgram.h` describes and
/// what its executor runs in one fused pass with compiled kernels. Sending such an expression through
/// a per-pixel interpreter instead would be paying an interpreter for arithmetic the engine already
/// has a vectorized kernel for.
///
/// So this stage recognizes the chains and hands them over. What it will not do is guess: a lowering
/// either produces a chain of the expression's own operations in the expression's own order, or it
/// says which property of the expression stopped it. No operation is replaced by an algebraic
/// substitute that computes a different number - a division by a constant becomes a multiplication
/// only when the reciprocal is exact, which is only for a power of two - and there is no partial
/// lowering, because an expression that computes one thing on the fused path and another on the
/// interpreted path is worse than one that is always slow.
///
/// What can still differ is rounding and exceptional values, and a caller comparing the two paths
/// should know where. The chain rounds after each step at `PixelMathLoweringOptions::precision`,
/// Float32 unless the caller justifies Float64, where the interpreter rounds after each step in
/// double. A power step of a Float32 chain may run on the compiled vector approximation, within the
/// tier `pointwiseProgramAccuracy` states. And a minimum or maximum follows `fmin` and `fmax`, so a
/// number wins over a not-a-number, where the interpreter returns its first argument.
///
/// The recognizable shape is a chain over one primary image: a sample of one image, then a sequence
/// of steps each of which combines the running value with a constant or with a whole other image. An
/// expression that branches per pixel, reads a stated coordinate, draws a random number, assigns to a
/// variable, converts a colour, needs a whole-image statistic, or is simply not left-leaning is not
/// that shape, and each of those is a named answer rather than a silent refusal.

#pragma once

#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgram.h>
#include <AfterNightEngine/Algorithms/PixelMath/PixelMathProgram.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/ComputePrecision.h>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace an
{

/// The arithmetic a lowered chain is evaluated under.
///
/// Everything else the pointwise program needs - the representation, the numeric domain, the channel
/// count - is a fact about the expression that was compiled and is read from it rather than restated
/// here, so the two cannot disagree.
struct PixelMathLoweringOptions
{
    /// Width of the chain's arithmetic.
    ComputePrecision precision = ComputePrecision::Float32;

    /// Why Float64 is required. Empty for Float32 and required for Float64.
    std::optional<Float64Justification> precisionJustification{};

    /// What the chain promises about the values it writes.
    OutputPrecisionContract outputContract = OutputPrecisionContract::NormalizedOrLossy;

    /// Whether an adjacent scale and bias may contract into one fused multiply-add.
    bool allowContraction = false;
};

/// Why an expression is not a fusable chain.
///
/// One value per property that stopped the lowering, because a caller acts on them differently: an
/// expression that draws a random number will never be a chain, and one that is written right-leaning
/// becomes one if it is rewritten.
enum class PixelMathLoweringBlocker : std::uint8_t
{
    /// Nothing stopped it. The lowering produced a program.
    None = 0u,

    /// The instructions are not a chain over one primary image - a constant on the left of a
    /// subtraction, a nested expression as an operand, a minimum or maximum against a constant, a
    /// call other than a two-argument minimum or maximum against an image or a clamp between two
    /// constants, or an expression that reads no image at all.
    NotAChain = 1u,

    /// The expression's value depends on where in the image it is evaluated.
    UsesPixelPosition = 2u,

    /// The expression draws from the random stream.
    UsesRandom = 3u,

    /// The expression assigns to a variable.
    UsesVariable = 4u,

    /// The expression chooses between two values per pixel.
    UsesBranch = 5u,

    /// The expression reads an image at coordinates it states.
    UsesImageSample = 6u,

    /// The expression converts a pixel's colour.
    UsesColorComponent = 7u,

    /// The expression needs a statistic of a whole image. Its value is a number this stage does not
    /// have and which differs per channel, so folding it into a chain that runs over every channel
    /// would be wrong rather than merely unsupported.
    UsesImageStatistic = 8u,

    /// The expression holds more than one statement.
    UsesMultipleStatements = 9u,

    /// A step of the chain is an operation the fusable vocabulary has no step for, or one it cannot
    /// take exactly - a remainder, an absolute difference, a logical operator, a comparison against
    /// a constant, a division by a constant that is not a power of two, a clamp whose low bound is
    /// above its high bound, or a two-argument call against an image other than a minimum or maximum.
    OperationNotFusable = 10u,

    /// A step reads a stated channel of an image rather than the channel being written, which a
    /// fused source plane cannot express.
    ChannelSelectionNotFusable = 11u,

    /// The expression names more images than one fused program binds, or more channels than one
    /// fused pixel carries.
    TooManyOperands = 12u,

    /// Two of the expression's images do not share a representation or an extent, which a fused
    /// program requires of every source it binds.
    OperandsDoNotAgree = 13u,

    /// A coefficient the chain would carry is not finite, so there is no step that describes it.
    CoefficientNotFinite = 14u
};

/// Whether a value names a blocker this build reports.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param blocker the value to inspect.
/// @return true for every supported blocker, including `None`.
[[nodiscard]] constexpr bool isPixelMathLoweringBlocker(PixelMathLoweringBlocker blocker) noexcept
{
    return static_cast<std::uint8_t>(blocker) <=
           static_cast<std::uint8_t>(PixelMathLoweringBlocker::CoefficientNotFinite);
}

/// The blocker's stable name, for diagnostics and tests.
///
/// Thread-safe, reentrant, and allocation-free. The returned pointer is a string literal that
/// outlives any caller.
///
/// @param blocker the blocker, including `None` and values this build does not know.
/// @return the name, or "unknown" for a value outside the enumeration.
[[nodiscard]] AFTERNIGHTENGINE_API const char* pixelMathLoweringBlockerName(PixelMathLoweringBlocker blocker) noexcept;

/// What lowering one expression produced: a fusable chain, or the reason there is not one.
///
/// Copyable and movable, and safe to read from any number of threads at once.
class PixelMathLowering
{
public:
    /// A lowering that produced nothing and blames nothing.
    PixelMathLowering() = default;

    /// Copies a lowering, including its program.
    PixelMathLowering(const PixelMathLowering&) = default;

    /// Takes over a lowering's program.
    PixelMathLowering(PixelMathLowering&&) noexcept = default;

    /// Copies a lowering over this one.
    ///
    /// @return this lowering.
    PixelMathLowering& operator=(const PixelMathLowering&) = default;

    /// Takes over a lowering's program, leaving the other empty.
    ///
    /// @return this lowering.
    PixelMathLowering& operator=(PixelMathLowering&&) noexcept = default;

    /// Releases the program.
    ~PixelMathLowering() = default;

    /// Whether the expression became a fusable chain.
    ///
    /// @return true exactly when `blocker()` is `PixelMathLoweringBlocker::None`.
    [[nodiscard]] bool lowered() const noexcept
    {
        return m_blocker == PixelMathLoweringBlocker::None;
    }

    /// The fused chain.
    ///
    /// Borrowed for as long as this lowering lives, and present only when `lowered()` is true: an
    /// expression that is not a chain produced no program, and there is no empty pointwise program
    /// to stand in for one.
    ///
    /// @return the pointwise program, or no value when the expression did not lower.
    [[nodiscard]] const std::optional<PointwiseProgram>& program() const noexcept
    {
        return m_program;
    }

    /// Which of the expression's images each of the chain's source planes is.
    ///
    /// Source zero is the chain's primary input. An execution binds
    /// `sourceImages()[i]` of the compiled expression's images as pointwise source `i`.
    ///
    /// Borrowed for as long as this lowering lives.
    ///
    /// @return the image indices, empty when the expression did not lower.
    [[nodiscard]] std::span<const std::uint32_t> sourceImages() const noexcept
    {
        return std::span<const std::uint32_t>(m_sourceImages);
    }

    /// Why the expression is not a fusable chain.
    ///
    /// @return the blocker, or `PixelMathLoweringBlocker::None` when it is one.
    [[nodiscard]] PixelMathLoweringBlocker blocker() const noexcept
    {
        return m_blocker;
    }

private:
    friend class PixelMathLowerer;

    std::optional<PointwiseProgram> m_program{};
    std::vector<std::uint32_t> m_sourceImages;
    PixelMathLoweringBlocker m_blocker = PixelMathLoweringBlocker::NotAChain;
};

/// The one thing that lowers a compiled expression. Defined in the engine's implementation.
///
/// Named here rather than published for the reason the parser, the resolver and the compiler are: it
/// exists so that the type above has exactly one producer.
class PixelMathLowerer;

/// Turns a compiled expression into a fusable chain when it is one.
///
/// An expression that is not a chain is not a failure: the lowering comes back with `lowered()`
/// false and a blocker naming the property that stopped it, and the caller runs the specialized
/// evaluator instead. A failure is returned only when the chain was recognized and the pointwise
/// program refused it, such as options stating a precision the pointwise rules refuse. A power
/// over a domain other than normalized linear or encoded nonlinear returns `OperationNotFusable`
/// so the interpreter can compute it.
///
/// Thread-safe and reentrant.
///
/// @param program the compiled expression.
/// @param options the arithmetic the chain would be evaluated under.
/// @return the lowering; `ErrorCode::InvalidArgument` for an empty program; or the pointwise
///         builder's refusal, `ErrorCode::InvalidArgument`, when a recognized chain did not build.
[[nodiscard]] AFTERNIGHTENGINE_API Result<PixelMathLowering>
lowerPixelMathToPointwise(const PixelMathProgram& program, const PixelMathLoweringOptions& options = {});

} // namespace an
