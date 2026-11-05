// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Which width an operation computes in, and why it is allowed to.
///
/// Storage type and arithmetic type are separate decisions, and this header is the second one. An
/// algorithm that picks its arithmetic from the C++ element type it happens to be handed sends a
/// sixteen-bit frame and a float frame down different code paths for reasons nobody chose, and a path
/// that needs more precision than it gets has no way to say so.
///
/// The rule is short and it is not a preference.
///
/// **Float32 is the default.** The eight- and sixteen-bit representations and Float32 itself compute
/// in Float32, which is what the measurements behind the fused-kernel shape assume: the arithmetic
/// happens in registers over stored integers, and a double lane processes half as many of them.
///
/// **Float64 is named, never selected.** An operation that computes in double says which of the
/// approved reasons applies to it. Two of those reasons are structural - the samples are already
/// Float64, or the result has to distinguish adjacent thirty-two-bit code values - and the engine
/// checks them against the operation's own storage and contract, so a justification that does not
/// apply is refused rather than believed. The rest name an algorithm and a numeric argument that was
/// made once and written down: the calibration divide, accumulation in stacking, a floating
/// reduction accumulator, and astrometric fitting. Adding another means making that argument, not
/// passing another enumerator.
///
/// **The thirty-two-bit integers are the interesting case**, and it is why a *contract* appears here
/// at all. Float32 cannot distinguish every adjacent UInt32 or Int32 value, so whether Float32 is
/// admissible over that storage depends on what the operation promises about its output: a result
/// that must round-trip stored values exactly needs Float64, and one whose declared tolerance makes
/// code-level precision irrelevant may use Float32. That choice belongs to the operation and its
/// conformance scenarios; it is never an implicit conversion and never a mode a caller selects for
/// the whole engine.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <cstddef>
#include <cstdint>
#include <optional>

namespace an
{

/// The width an operation does its arithmetic in.
///
/// Two values, and there will not be a third: a mode matrix is what this engine is replacing, and
/// every additional width multiplies the golden outputs a defect report has to be reproduced
/// against.
enum class ComputePrecision : std::uint8_t
{
    /// Binary32. The default for the eight- and sixteen-bit representations and for Float32.
    Float32,

    /// Binary64. Reached only with a stated justification.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Float64
};

/// What an operation promises about the values it writes.
///
/// Only two answers matter to a precision rule, and they are the two that decide whether the gap
/// between adjacent thirty-two-bit integers is observable in the result.
enum class OutputPrecisionContract : std::uint8_t
{
    /// The result must distinguish adjacent stored code values, or round-trip them exactly. What a
    /// lossless transform, an exact copy or an integer-preserving operation promises.
    ExactStoredValues,

    /// The result is normalized or lossy within a declared tolerance, and the gap between adjacent
    /// code values is inside it. What a display transform, a resampling or a statistic promises.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    NormalizedOrLossy
};

/// Why an operation computes in Float64.
///
/// A closed set. Two of these are structural and the engine can check them; the other four name an
/// algorithm whose numeric argument was made once and recorded, and adding a fifth means making
/// that argument rather than adding an enumerator.
enum class Float64Justification : std::uint8_t
{
    /// The samples are Float64 already, so computing in Float32 would discard precision the input
    /// carries. Applies only to Float64 storage.
    StoredInFloat64,

    /// The result must distinguish adjacent thirty-two-bit code values, which Float32 cannot.
    /// Applies only to UInt32 and Int32 storage under an exact contract.
    ExactIntegerCodeValues,

    /// The calibration divide, which computes in double because at frame scale the promotion costs a
    /// few per cent, inside the noise of the input and output around it.
    CalibrationDivide,

    /// Accumulation in stacking, where many frames sum and the partial sums outgrow the precision
    /// of the samples going into them.
    StackingAccumulation,

    /// The accumulator of a floating reduction, where the error of a long summation is what the
    /// wider type is buying.
    ReductionAccumulator,

    /// Astrometric and world-coordinate fitting, where the conditioning of the system rather than
    /// its throughput decides the width.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    AstrometricFitting
};

/// Why a stated precision was refused.
///
/// A vocabulary rather than a type with behaviour, in the shape `Image/DescriptorError.h` uses: the
/// `check` entry point below returns one of these, and the caller that only wants the answer uses
/// the plain query beside it.
enum class PrecisionError : std::uint8_t
{
    /// The stated precision is one this operation may compute in.
    None,

    /// The sample type is not one of the eight this engine stores.
    UnknownSampleType,

    /// The precision is not one of the two this engine computes in.
    UnknownComputePrecision,

    /// The output contract is not one this engine knows.
    UnknownOutputContract,

    /// The justification is not one of the approved reasons.
    UnknownJustification,

    /// Float64 was asked for with no reason given. The default is Float32, and departing from it is
    /// a decision that carries its argument.
    Float64NeedsJustification,

    /// A justification was given for an operation that computes in Float32. It would be a reason
    /// recorded beside a choice it did not cause.
    JustificationWithoutFloat64,

    /// The justification does not apply to this storage or this contract - `StoredInFloat64` over
    /// samples that are not Float64, or `ExactIntegerCodeValues` over anything but the two
    /// thirty-two-bit integers or where nothing promises exactness.
    JustificationNotApplicable,

    /// Float32 was asked for where the storage and contract require Float64: an exact promise over
    /// samples that are already Float64, or over thirty-two-bit integers whose result must
    /// distinguish adjacent code values.
    ///
    /// Deliberately the last enumerator; `kPrecisionErrorCount` is derived from it.
    PrecisionBelowContract
};

/// How many reasons `PrecisionError` holds, counting `None`.
///
/// Derived from the last enumerator rather than written out, so it cannot drift from the
/// enumeration while `PrecisionBelowContract` stays last.
inline constexpr std::size_t kPrecisionErrorCount =
    static_cast<std::size_t>(PrecisionError::PrecisionBelowContract) + 1u;

/// Whether a value is one of the precisions this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param precision the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isComputePrecision(ComputePrecision precision) noexcept
{
    return static_cast<std::uint8_t>(precision) <= static_cast<std::uint8_t>(ComputePrecision::Float64);
}

/// Whether a value is one of the contracts this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param contract the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isOutputPrecisionContract(OutputPrecisionContract contract) noexcept
{
    return static_cast<std::uint8_t>(contract) <= static_cast<std::uint8_t>(OutputPrecisionContract::NormalizedOrLossy);
}

/// Whether a value is one of the approved reasons.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param justification the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isFloat64Justification(Float64Justification justification) noexcept
{
    return static_cast<std::uint8_t>(justification) <=
           static_cast<std::uint8_t>(Float64Justification::AstrometricFitting);
}

/// The C++ type one of these precisions computes in.
///
/// The closed two-way table between the enumeration and the arithmetic type, so that a kernel
/// written against `ComputeSampleOf<PrecisionV>` and a plan written against `ComputePrecision`
/// cannot disagree.
template <ComputePrecision PrecisionV> struct ComputeSampleTraits;

/// Float32 computes in `float`.
template <> struct ComputeSampleTraits<ComputePrecision::Float32>
{
    /// The arithmetic type.
    using Sample = float;
};

/// Float64 computes in `double`.
template <> struct ComputeSampleTraits<ComputePrecision::Float64>
{
    /// The arithmetic type.
    using Sample = double;
};

/// The arithmetic type of a precision, spelled where a template needs it.
///
/// @tparam PrecisionV the precision.
template <ComputePrecision PrecisionV> using ComputeSampleOf = typename ComputeSampleTraits<PrecisionV>::Sample;

/// The resident representation an arithmetic type is stored as, when it is stored.
///
/// A working image is an ordinary image whose samples are the arithmetic type, so this is what
/// turns a precision into the `SampleType` such an image is described with.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param precision the arithmetic width.
/// @return `SampleType::Float32` or `SampleType::Float64`. A value outside the enumeration yields
///         `SampleType::Float32`, the default; an entry point that checks membership first refuses
///         such a value before asking this.
[[nodiscard]] constexpr SampleType workingSampleTypeOf(ComputePrecision precision) noexcept
{
    return precision == ComputePrecision::Float64 ? SampleType::Float64 : SampleType::Float32;
}

/// The width an operation computes in when it states no preference.
///
/// Float32 everywhere except where the samples already carry more or the promise needs more:
/// Float64 storage, whose input precision computing in Float32 would discard, and the thirty-two-bit
/// integers under an exact contract, whose adjacent code values Float32 cannot distinguish.
///
/// This is the width an operation gets by saying nothing. It is not the same question as whether
/// Float32 is *admissible* - `requiresFloat64` answers that - because a declared tolerance can
/// permit a downcast that is nonetheless not what an operation should get by default.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param storage what the samples are stored as.
/// @param contract what the operation promises about its output.
/// @return the default width. A sample type or contract outside its enumeration yields
///         `ComputePrecision::Float32`; `checkComputePrecision` is what refuses such a value by
///         name.
[[nodiscard]] constexpr ComputePrecision defaultComputePrecision(SampleType storage,
                                                                 OutputPrecisionContract contract) noexcept
{
    if (!isSampleType(storage) || !isOutputPrecisionContract(contract))
    {
        return ComputePrecision::Float32;
    }
    if (storage == SampleType::Float64)
    {
        return ComputePrecision::Float64;
    }
    if ((storage == SampleType::UInt32 || storage == SampleType::Int32) &&
        contract == OutputPrecisionContract::ExactStoredValues)
    {
        return ComputePrecision::Float64;
    }
    return ComputePrecision::Float32;
}

/// Whether computing in Float32 is refused outright for this storage and promise.
///
/// The narrower question, and the one the two thirty-two-bit integers and Float64 storage answer
/// differently from `defaultComputePrecision`. An exact promise over samples Float32 cannot
/// represent is a contradiction and is refused; the same storage under a declared tolerance is a
/// downcast the operation is allowed to make, and it makes it by asking rather than by default.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param storage what the samples are stored as.
/// @param contract what the operation promises about its output.
/// @return true when only Float64 can satisfy the promise.
[[nodiscard]] constexpr bool requiresFloat64(SampleType storage, OutputPrecisionContract contract) noexcept
{
    if (!isSampleType(storage) || contract != OutputPrecisionContract::ExactStoredValues)
    {
        return false;
    }
    return storage == SampleType::Float64 || storage == SampleType::UInt32 || storage == SampleType::Int32;
}

/// Whether an operation may compute at this width, and if not, why not.
///
/// The order is a contract. Every value is checked for membership of its own enumeration first, so
/// a rule is never applied to a value it cannot interpret; the rules that follow are the two
/// directions a precision can be wrong - reaching for Float64 without a reason, and settling for
/// Float32 where the storage and the promise do not allow it.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param storage what the samples are stored as.
/// @param contract what the operation promises about its output.
/// @param requested the width the operation intends to compute at.
/// @param justification why, when the width is Float64. Left empty for Float32.
/// @return `PrecisionError::None` when the operation may compute at that width; otherwise the first
///         rule that refused.
[[nodiscard]] constexpr PrecisionError checkComputePrecision(SampleType storage, OutputPrecisionContract contract,
                                                             ComputePrecision requested,
                                                             std::optional<Float64Justification> justification) noexcept
{
    if (!isSampleType(storage))
    {
        return PrecisionError::UnknownSampleType;
    }
    if (!isComputePrecision(requested))
    {
        return PrecisionError::UnknownComputePrecision;
    }
    if (!isOutputPrecisionContract(contract))
    {
        return PrecisionError::UnknownOutputContract;
    }
    if (justification.has_value() && !isFloat64Justification(*justification))
    {
        return PrecisionError::UnknownJustification;
    }

    if (requested == ComputePrecision::Float64)
    {
        if (!justification.has_value())
        {
            return PrecisionError::Float64NeedsJustification;
        }
        if (*justification == Float64Justification::StoredInFloat64 && storage != SampleType::Float64)
        {
            return PrecisionError::JustificationNotApplicable;
        }
        if (*justification == Float64Justification::ExactIntegerCodeValues &&
            ((storage != SampleType::UInt32 && storage != SampleType::Int32) ||
             contract != OutputPrecisionContract::ExactStoredValues))
        {
            return PrecisionError::JustificationNotApplicable;
        }
        return PrecisionError::None;
    }

    if (justification.has_value())
    {
        return PrecisionError::JustificationWithoutFloat64;
    }
    if (requiresFloat64(storage, contract))
    {
        return PrecisionError::PrecisionBelowContract;
    }
    return PrecisionError::None;
}

/// The precision's own name, for a log line, a diagnostic field or a failure message.
///
/// Thread-safe and allocation-free.
///
/// @param precision the width to name. A value outside the enumeration is named
///        `"UnknownComputePrecision"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* computePrecisionName(ComputePrecision precision) noexcept;

/// The contract's own name, for a log line, a diagnostic field or a failure message.
///
/// Thread-safe and allocation-free.
///
/// @param contract the contract to name. A value outside the enumeration is named
///        `"UnknownOutputPrecisionContract"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* outputPrecisionContractName(OutputPrecisionContract contract) noexcept;

/// The justification's own name, for a log line, a diagnostic field or a failure message.
///
/// Thread-safe and allocation-free.
///
/// @param justification the reason to name. A value outside the enumeration is named
///        `"UnknownFloat64Justification"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* float64JustificationName(Float64Justification justification) noexcept;

/// The refusal's own name, for a log line or a test failure message.
///
/// Thread-safe and allocation-free.
///
/// @param error the reason to name. A value outside the enumeration is named
///        `"UnknownPrecisionError"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* precisionErrorName(PrecisionError error) noexcept;

} // namespace an
