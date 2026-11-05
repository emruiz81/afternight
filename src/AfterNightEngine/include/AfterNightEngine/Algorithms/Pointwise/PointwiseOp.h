// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The element-wise steps a fusable pointwise program is built from.
///
/// This vocabulary is deliberately small and closed. A pointwise program exists so that a chain of
/// per-pixel operations can be executed in one pass instead of one pass each, and the property that
/// makes that legal is that every step reads exactly one pixel and writes exactly one pixel. An
/// operation that reads a neighbour, a row, or the whole image is not expressible here and must not
/// become expressible: the moment one step needs a second pass, fusing the chain stops being sound
/// and the IR would be describing something it cannot deliver.
///
/// The steps carry their coefficients as `double` whatever the samples are, because a coefficient is
/// a statement about the operation rather than about the storage. What width the chain is evaluated
/// at is the program's declared decision, not something inferred from where a number came from.
///
/// One step is one meaning, and the members that meaning does not use are held at their neutral
/// values. That is what makes two steps comparable: a canonicalizer that has to ask which fields
/// matter before it can say whether two steps are the same step is a canonicalizer that will
/// eventually get it wrong.

#pragma once

#include <AfterNightEngine/Core/Api.h>

#include <cstddef>
#include <cstdint>

namespace an
{

/// Which element-wise operation one step performs.
///
/// The ordering carries no meaning and is not part of the ABI contract for anything but this
/// build; steps are compared by kind, never by ordinal.
enum class PointwiseOpKind : std::uint8_t
{
    /// No operation. The default, and never valid in a built program.
    Unknown = 0u,

    /// Multiplies the channel value by `factor`.
    Scale = 1u,

    /// Adds `offset` to the channel value.
    Bias = 2u,

    /// Restricts the channel value to `[low, high]`, both bounds inclusive.
    ///
    /// A value that is not a number is left alone rather than mapped to a bound, because the two
    /// comparisons a clamp performs are both false for it and inventing an answer here would hide
    /// a not-a-number that the caller's validity rule is entitled to see.
    Clamp = 3u,

    /// Raises the channel value to the power `factor`.
    ///
    /// Meant for a non-negative base: a program may hold one only when its input domain is
    /// `NormalizedLinear` or `EncodedNonlinear`, and building it is otherwise refused with
    /// `PointwiseProgramError::NumericDomainRefused`. Nothing proves that no negative value reaches
    /// the step, and every path answers a negative base a not-a-number at every exponent: the
    /// scalar evaluator answers it before `std::pow`, which would give a real power at an integral
    /// exponent, and the compiled Float32 kernel computes `exp(factor * log(value))`. The two
    /// differ for a not-a-number raised to zero, which the kernel answers a not-a-number and
    /// `std::pow` one. A zero base retains its sign at odd integral exponents. Inside the domain the
    /// compiled result is within the vector transcendental tier of the evaluator's, not identical to
    /// it; `pointwiseProgramAccuracy` states that tier.
    Gamma = 4u,

    /// Replaces the channel value with `table[value]`, indexed by the source sample's own integer
    /// value.
    ///
    /// Legal only as the first step of a program whose source samples are an unsigned eight- or
    /// sixteen-bit integer representation, because that is the only case in which the index is the
    /// sample and no rounding decision has to be invented to produce one. It is both the step a
    /// caller writes for a curve and the step a whole eligible chain collapses into.
    Lut = 5u,

    /// Replaces every channel of the pixel with a linear combination of all of them.
    ///
    /// The coefficients are a row-major square matrix of the program's channel count, held in the
    /// program's table at `table`.
    ChannelMatrix = 6u,

    /// Multiplies the channel value by the matching channel of source plane `operand`.
    ///
    /// This is the multiplicative reading of a mask, which is the one that composes: a mask that
    /// selects between the chain's result and some other value is a `Blend` against that value,
    /// and the unmodified input is available as source plane zero.
    Mask = 7u,

    /// Moves the channel value toward source plane `operand` by the constant `factor`.
    ///
    /// Computed as `current * (1 - factor) + operand * factor`, so `factor` of zero is the identity
    /// and one replaces the value outright.
    Blend = 8u,

    /// Adds the matching channel of source `operand`, multiplied by `factor`.
    Add = 9u,

    /// Subtracts the matching channel of source `operand`, multiplied by `factor`.
    Subtract = 10u,

    /// Multiplies by the matching channel of source `operand`, then by `factor`.
    Multiply = 11u,

    /// Divides by the matching channel of source `operand`, then multiplies by `factor`.
    ///
    /// Division follows IEC 60559 arithmetic: a non-zero finite value divided by zero produces a
    /// signed infinity and zero divided by zero produces a not-a-number. Narrowing to integer
    /// storage applies the program executor's declared conversion policy afterwards.
    Divide = 12u,

    /// Keeps the lesser of the current value and the matching channel of source `operand`, as
    /// `fmin` does: when exactly one of them is not a number, the other wins.
    Minimum = 13u,

    /// Keeps the greater of the current value and the matching channel of source `operand`, as
    /// `fmax` does: when exactly one of them is not a number, the other wins.
    Maximum = 14u,

    /// Replaces the current value with one when `current comparison operand` holds and zero
    /// otherwise; every relation but `NotEqual` is false for a not-a-number.
    Compare = 15u,

    /// Evaluates a sampled curve over `[low, high]` through program table `table`.
    ///
    /// Unlike `Lut`, this step accepts floating input, need not be first, and states how values
    /// between and outside its samples are handled. It is the fusable representation of a curve;
    /// `Lut` remains the exact code-value path for enumerable integer input.
    SampledCurve = 16u
};

/// How many step kinds this build knows, including `Unknown`.
///
/// `SampledCurve` is last, which is what this depends on. It exists so a table with one entry per
/// kind can be sized without repeating the list.
inline constexpr std::size_t kPointwiseOpKindCount = static_cast<std::size_t>(PointwiseOpKind::SampledCurve) + 1u;

/// Whether a value names a step this build can execute.
///
/// `Unknown` is not one: it is the absence of a step rather than a step. Thread-safe,
/// allocation-free, and usable in a constant expression.
///
/// @param kind the value to check.
/// @return true when the value names an executable step.
[[nodiscard]] constexpr bool isPointwiseOpKind(PointwiseOpKind kind) noexcept
{
    return kind >= PointwiseOpKind::Scale && kind <= PointwiseOpKind::SampledCurve;
}

/// The relation a comparison step tests.
enum class PointwiseComparison : std::uint8_t
{
    /// Strictly less than.
    Less = 0u,

    /// Less than or equal to.
    LessOrEqual = 1u,

    /// Exactly equal under IEC 60559 arithmetic.
    Equal = 2u,

    /// Not equal under IEC 60559 arithmetic. A not-a-number compares not equal.
    NotEqual = 3u,

    /// Greater than or equal to.
    GreaterOrEqual = 4u,

    /// Strictly greater than.
    Greater = 5u
};

/// Whether a value names a comparison relation this build implements.
///
/// @param comparison the value to inspect.
/// @return true for every supported relation.
[[nodiscard]] constexpr bool isPointwiseComparison(PointwiseComparison comparison) noexcept
{
    return static_cast<std::uint8_t>(comparison) <= static_cast<std::uint8_t>(PointwiseComparison::Greater);
}

/// How a sampled curve resolves a value between two entries.
enum class PointwiseCurveSampling : std::uint8_t
{
    /// Uses the nearest entry; an exact half chooses the entry on the greater-input side.
    Nearest = 0u,

    /// Interpolates linearly between the two bracketing entries.
    Linear = 1u,

    /// Uses the entry on the lower-input side.
    ///
    /// This is the lookup rule of the real-time preview tables: multiplying a normalized sample by
    /// `entryCount - 1` and truncating the result. It is in the IR so the opaque preview gamma plan
    /// can reach an optimized kernel. The production pointwise executor and general tone-table
    /// builder refuse it. Valid only with `Clamp`, because a step function has no segment to
    /// extrapolate.
    Lower = 2u
};

/// Whether a value names a sampled-curve interpolation this build implements.
///
/// @param sampling the value to inspect.
/// @return true for every supported mode.
[[nodiscard]] constexpr bool isPointwiseCurveSampling(PointwiseCurveSampling sampling) noexcept
{
    return sampling == PointwiseCurveSampling::Nearest || sampling == PointwiseCurveSampling::Linear ||
           sampling == PointwiseCurveSampling::Lower;
}

/// What a sampled curve does outside the closed interval its table covers.
enum class PointwiseCurveDomainPolicy : std::uint8_t
{
    /// Uses the nearest endpoint. This is the safe display-transform default: a value outside the unit
    /// interval never forms an index outside the table.
    Clamp = 0u,

    /// Refuses at the first outside sample. Image execution may already have written a prefix.
    Reject = 1u,

    /// Continues the first or last table segment linearly beyond the covered interval.
    LinearExtrapolation = 2u
};

/// Whether a value names a sampled-curve domain policy this build implements.
///
/// @param policy the value to inspect.
/// @return true for every supported policy.
[[nodiscard]] constexpr bool isPointwiseCurveDomainPolicy(PointwiseCurveDomainPolicy policy) noexcept
{
    return static_cast<std::uint8_t>(policy) <=
           static_cast<std::uint8_t>(PointwiseCurveDomainPolicy::LinearExtrapolation);
}

/// The step's stable name for diagnostics, failure messages, and tests.
///
/// Thread-safe, reentrant, and allocation-free. The returned pointer is a string literal that
/// outlives any caller.
///
/// @param kind the step kind, including `Unknown` and values this build does not know.
/// @return the name, or "unknown" for anything this build cannot execute.
[[nodiscard]] AFTERNIGHTENGINE_API const char* pointwiseOpKindName(PointwiseOpKind kind) noexcept;

/// One element-wise step and its coefficients.
///
/// Which members carry meaning is decided by `kind`, and every other member is held at the neutral
/// value shown here. A program that is built rejects a step whose unused members were set, so two
/// steps of the same kind are equal exactly when they do the same thing.
struct PointwiseOp
{
    /// What this step does.
    PointwiseOpKind kind = PointwiseOpKind::Unknown;

    /// `Scale`'s multiplier, `Gamma`'s exponent, `Blend`'s constant weight, or the secondary
    /// operand multiplier of the four arithmetic steps. One elsewhere.
    double factor = 1.0;

    /// `Bias`'s addend. Zero elsewhere.
    double offset = 0.0;

    /// `Clamp`'s inclusive lower bound or `SampledCurve`'s domain start. Zero elsewhere.
    double low = 0.0;

    /// `Clamp`'s inclusive upper bound or `SampledCurve`'s domain end. Zero elsewhere.
    double high = 0.0;

    /// Which source plane the mask, blend, arithmetic, minimum, maximum and comparison steps read.
    /// Zero elsewhere, and zero is also the program's primary source, whose original value - not the
    /// chain's running value - is what such a step reads there.
    std::uint32_t operand = 0u;

    /// Which of the program's tables `Lut`, `ChannelMatrix` and `SampledCurve` read. Zero elsewhere.
    std::uint32_t table = 0u;

    /// The relation `Compare` applies. `Equal` elsewhere.
    PointwiseComparison comparison = PointwiseComparison::Equal;

    /// How `SampledCurve` reads between entries. `Linear` elsewhere.
    PointwiseCurveSampling sampling = PointwiseCurveSampling::Linear;

    /// What `SampledCurve` does outside its interval. `Clamp` elsewhere.
    PointwiseCurveDomainPolicy domainPolicy = PointwiseCurveDomainPolicy::Clamp;
};

/// Whether two steps are the same step.
///
/// Defined member by member, which is exact because the members a kind does not use are held at
/// their neutral values. Thread-safe and allocation-free.
///
/// @param left one step.
/// @param right the other.
/// @return true when both name the same operation with the same coefficients.
[[nodiscard]] constexpr bool operator==(const PointwiseOp& left, const PointwiseOp& right) noexcept
{
    return left.kind == right.kind && left.factor == right.factor && left.offset == right.offset &&
           left.low == right.low && left.high == right.high && left.operand == right.operand &&
           left.table == right.table && left.comparison == right.comparison && left.sampling == right.sampling &&
           left.domainPolicy == right.domainPolicy;
}

/// A step that multiplies by `factor`.
///
/// Thread-safe, allocation-free, and usable in a constant expression. The coefficient is not
/// checked here - a program checks its steps when it is built, where every rule is applied in one
/// place and in step order.
///
/// @param factor the multiplier.
/// @return the step.
[[nodiscard]] constexpr PointwiseOp pointwiseScale(double factor) noexcept
{
    return PointwiseOp{.kind = PointwiseOpKind::Scale, .factor = factor};
}

/// A step that adds `offset`.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param offset the addend.
/// @return the step.
[[nodiscard]] constexpr PointwiseOp pointwiseBias(double offset) noexcept
{
    return PointwiseOp{.kind = PointwiseOpKind::Bias, .offset = offset};
}

/// A step that restricts values to `[low, high]`.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param low the inclusive lower bound.
/// @param high the inclusive upper bound.
/// @return the step.
[[nodiscard]] constexpr PointwiseOp pointwiseClamp(double low, double high) noexcept
{
    return PointwiseOp{.kind = PointwiseOpKind::Clamp, .low = low, .high = high};
}

/// A step that raises values to the power `exponent`.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param exponent the power.
/// @return the step.
[[nodiscard]] constexpr PointwiseOp pointwiseGamma(double exponent) noexcept
{
    return PointwiseOp{.kind = PointwiseOpKind::Gamma, .factor = exponent};
}

/// A step that replaces the sample with the program table `table` indexed by the sample.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param table which of the program's tables to read.
/// @return the step.
[[nodiscard]] constexpr PointwiseOp pointwiseLut(std::uint32_t table) noexcept
{
    return PointwiseOp{.kind = PointwiseOpKind::Lut, .table = table};
}

/// A step that mixes the pixel's channels through the square matrix in program table `table`.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param table which of the program's tables holds the row-major coefficients.
/// @return the step.
[[nodiscard]] constexpr PointwiseOp pointwiseChannelMatrix(std::uint32_t table) noexcept
{
    return PointwiseOp{.kind = PointwiseOpKind::ChannelMatrix, .table = table};
}

/// A step that multiplies by the matching channel of source plane `operand`.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param operand which source plane supplies the mask.
/// @return the step.
[[nodiscard]] constexpr PointwiseOp pointwiseMask(std::uint32_t operand) noexcept
{
    return PointwiseOp{.kind = PointwiseOpKind::Mask, .operand = operand};
}

/// A step that moves the value toward source plane `operand` by the constant `factor`.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param operand which source plane to blend toward.
/// @param factor the weight given to that plane.
/// @return the step.
[[nodiscard]] constexpr PointwiseOp pointwiseBlend(std::uint32_t operand, double factor) noexcept
{
    return PointwiseOp{.kind = PointwiseOpKind::Blend, .factor = factor, .operand = operand};
}

/// A step that adds a scaled matching channel of another source.
///
/// @param operand which source supplies the addend.
/// @param factor multiplier applied to the addend before addition.
/// @return the step.
[[nodiscard]] constexpr PointwiseOp pointwiseAdd(std::uint32_t operand, double factor = 1.0) noexcept
{
    return PointwiseOp{.kind = PointwiseOpKind::Add, .factor = factor, .operand = operand};
}

/// A step that subtracts a scaled matching channel of another source.
///
/// @param operand which source supplies the subtrahend.
/// @param factor multiplier applied to the subtrahend before subtraction.
/// @return the step.
[[nodiscard]] constexpr PointwiseOp pointwiseSubtract(std::uint32_t operand, double factor = 1.0) noexcept
{
    return PointwiseOp{.kind = PointwiseOpKind::Subtract, .factor = factor, .operand = operand};
}

/// A step that multiplies by a matching channel of another source and a scalar.
///
/// @param operand which source supplies the multiplier.
/// @param factor scalar multiplier applied after the source multiplication.
/// @return the step.
[[nodiscard]] constexpr PointwiseOp pointwiseMultiply(std::uint32_t operand, double factor = 1.0) noexcept
{
    return PointwiseOp{.kind = PointwiseOpKind::Multiply, .factor = factor, .operand = operand};
}

/// A step that divides by a matching channel of another source and applies a scalar.
///
/// @param operand which source supplies the divisor.
/// @param factor scalar multiplier applied after division.
/// @return the step.
[[nodiscard]] constexpr PointwiseOp pointwiseDivide(std::uint32_t operand, double factor = 1.0) noexcept
{
    return PointwiseOp{.kind = PointwiseOpKind::Divide, .factor = factor, .operand = operand};
}

/// A step that keeps the lesser of the current and matching source values.
///
/// @param operand which source supplies the other value.
/// @return the step.
[[nodiscard]] constexpr PointwiseOp pointwiseMinimum(std::uint32_t operand) noexcept
{
    return PointwiseOp{.kind = PointwiseOpKind::Minimum, .operand = operand};
}

/// A step that keeps the greater of the current and matching source values.
///
/// @param operand which source supplies the other value.
/// @return the step.
[[nodiscard]] constexpr PointwiseOp pointwiseMaximum(std::uint32_t operand) noexcept
{
    return PointwiseOp{.kind = PointwiseOpKind::Maximum, .operand = operand};
}

/// A step that replaces each value with zero or one after a comparison.
///
/// @param operand which source supplies the right-hand value.
/// @param comparison relation applied as `current comparison operand`.
/// @return the step.
[[nodiscard]] constexpr PointwiseOp pointwiseCompare(std::uint32_t operand, PointwiseComparison comparison) noexcept
{
    return PointwiseOp{.kind = PointwiseOpKind::Compare, .operand = operand, .comparison = comparison};
}

/// A step that evaluates a sampled curve.
///
/// The table needs at least two entries. Validation of its interval, entries, interpolation and
/// domain policy occurs when the program is built. A `Lower` step can be represented for the
/// internal preview plan, but the production pointwise executor refuses to run it.
///
/// @param table which program table holds the samples.
/// @param low input value represented by the first entry.
/// @param high input value represented by the last entry; greater than `low`.
/// @param sampling how values between entries are resolved.
/// @param policy what values outside `[low, high]` do.
/// @return the step.
[[nodiscard]] constexpr PointwiseOp
pointwiseSampledCurve(std::uint32_t table, double low, double high,
                      PointwiseCurveSampling sampling = PointwiseCurveSampling::Linear,
                      PointwiseCurveDomainPolicy policy = PointwiseCurveDomainPolicy::Clamp) noexcept
{
    return PointwiseOp{.kind = PointwiseOpKind::SampledCurve,
                       .low = low,
                       .high = high,
                       .table = table,
                       .sampling = sampling,
                       .domainPolicy = policy};
}

} // namespace an
