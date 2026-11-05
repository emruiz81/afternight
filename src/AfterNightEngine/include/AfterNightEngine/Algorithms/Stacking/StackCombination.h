// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// How a stack combines the samples of one output pixel, and what each combination admits.
///
/// `StackCombination` is the combination a stack request names, `stackCombinationCapabilities`
/// the closed table of what each one admits - a rejection, frame or local weights, an input in any
/// numeric domain - which `StackModel::check` applies, and `kMaxStackFrames` the frame ceiling every
/// stacking operation and the photometric weight tables share. The header includes nothing of the
/// model, so a planner, a user interface or another stacking header can read these values without
/// the rest of the stacking surface.
#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <cstdint>

namespace an
{

/// The maximum frame count accepted by stacking and its photometric weight tables.
inline constexpr std::uint32_t kMaxStackFrames = 100000u;

/// The factor the auto-adaptive weighted average scales normalized samples by before iterating.
///
/// The algorithm was written for integer-like sample ranges and stops early when a dispersion is
/// smaller than five units, so samples in the unit interval have to be rescaled for it to behave.
/// This is a named property of the combination and not of any storage: the column kernel of
/// `combineStackSampleHistory` scales by it and scales the result back, and no sample is ever
/// stored at this scale. Every column is scaled, whatever its largest magnitude.
inline constexpr double kAutoAdaptiveSampleScale = 65535.0;

/// How the surviving samples of one output pixel become its value.
///
/// Separate from which samples survive, because the two are separate decisions, and because a
/// combination that cannot take a rejection is a fact about the combination.
enum class StackCombination : std::uint8_t
{
    /// The arithmetic mean of the surviving samples, weighted when weights are supplied.
    Average,

    /// The sum of the surviving samples, weighted when weights are supplied. No rejection: a sum
    /// with samples removed is a sum of something else.
    Sum,

    /// The exact median of the samples. Neither rejection nor weights: a median already ignores
    /// its extremes, and a weighted median is a different statistic this engine does not define.
    Median,

    /// The smallest sample.
    Minimum,

    /// The largest sample.
    Maximum,

    /// The iterative robust weighted mean with no tunable parameter, computed over samples scaled
    /// by `kAutoAdaptiveSampleScale`. It defines its own down-weighting, so it admits neither a
    /// rejection nor supplied weights, and it needs a normalized linear input because its scale
    /// assumes the unit interval.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    AutoAdaptiveWeightedAverage
};

/// Whether a value names a combination this build defines.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param combination the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isStackCombination(StackCombination combination) noexcept
{
    return static_cast<std::uint8_t>(combination) <=
           static_cast<std::uint8_t>(StackCombination::AutoAdaptiveWeightedAverage);
}

/// The combination's own name.
///
/// Thread-safe and allocation-free.
///
/// @param combination the combination to name. A value outside the enumeration is named
///        `"UnknownStackCombination"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* stackCombinationName(StackCombination combination) noexcept;

/// What a combination can be asked to do with its samples.
///
/// The closed compatibility table `StackModel::check` applies. It is a value rather than three
/// predicates so that a planner or a user interface can read a combination's whole row at once.
struct StackCombinationCapabilities
{
    /// Whether a rejection other than `StackRejection::None` may be stated.
    bool admitsRejection = false;

    /// Whether a weighting other than `StackWeighting::Uniform`, or a local weighting, may be
    /// stated.
    bool admitsWeighting = false;

    /// Whether the input must be in `NumericDomain::NormalizedLinear`.
    bool requiresNormalizedLinearDomain = false;

    /// Whether two rows state the same capabilities.
    ///
    /// @param left one row.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend constexpr bool operator==(const StackCombinationCapabilities& left,
                                                   const StackCombinationCapabilities& right) noexcept = default;
};

/// The capability row of a combination.
///
/// The average admits everything; the sum admits weights and refuses a rejection; the median, the
/// extrema and the auto-adaptive mean admit neither, and the last of those additionally needs a
/// normalized linear input because `kAutoAdaptiveSampleScale` assumes one.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param combination the combination to describe. A value outside the enumeration describes a
///        row that admits nothing, which is what `StackModel::check` refuses by name first.
/// @return the combination's row.
[[nodiscard]] constexpr StackCombinationCapabilities stackCombinationCapabilities(StackCombination combination) noexcept
{
    switch (combination)
    {
    case StackCombination::Average:
        return StackCombinationCapabilities{
            .admitsRejection = true, .admitsWeighting = true, .requiresNormalizedLinearDomain = false};
    case StackCombination::Sum:
        return StackCombinationCapabilities{
            .admitsRejection = false, .admitsWeighting = true, .requiresNormalizedLinearDomain = false};
    case StackCombination::Median:
    case StackCombination::Minimum:
    case StackCombination::Maximum:
        return StackCombinationCapabilities{
            .admitsRejection = false, .admitsWeighting = false, .requiresNormalizedLinearDomain = false};
    case StackCombination::AutoAdaptiveWeightedAverage:
        return StackCombinationCapabilities{
            .admitsRejection = false, .admitsWeighting = false, .requiresNormalizedLinearDomain = true};
    }
    return StackCombinationCapabilities{};
}

} // namespace an
