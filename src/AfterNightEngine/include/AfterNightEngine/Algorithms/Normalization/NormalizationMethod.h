// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The two choices a frame normalization is named by, on their own.
///
/// `GlobalNormalization.h` reaches the kernel, statistics and dispatch vocabularies because its
/// entry points measure, match and apply over frames. Naming a normalization is not an operation: a
/// stacking request states one before any frame is read, and a persisted document records one. The
/// two enumerations live here so that those callers can name a normalization without inheriting
/// the closure of the operations that perform one. The operations header includes this one, so a
/// caller of either sees exactly one definition.

#pragma once

#include <AfterNightEngine/Core/Api.h>

#include <cstdint>

namespace an
{

/// How one frame is brought to the reference.
enum class NormalizationMethod : std::uint8_t
{
    /// Add one offset per channel so the locations agree.
    Additive,

    /// Multiply by one factor per channel so the locations agree.
    Multiplicative,

    /// Multiply so the dispersions agree, then add so the locations agree.
    AdditiveScaling,

    /// Multiply so the dispersions agree, then multiply again so the locations agree.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    MultiplicativeScaling
};

/// Whether a value names a method this build implements.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param method the value to inspect.
/// @return true for every implemented method.
[[nodiscard]] constexpr bool isNormalizationMethod(NormalizationMethod method) noexcept
{
    return static_cast<std::uint8_t>(method) <= static_cast<std::uint8_t>(NormalizationMethod::MultiplicativeScaling);
}

/// Whether a method never adds an offset.
///
/// The two multiplicative methods write `sample * scale` and nothing else, which keeps the sign of
/// a zero product; the two additive ones write `sample * scale + offset`.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param method the method to inspect.
/// @return true for `Multiplicative` and `MultiplicativeScaling`.
[[nodiscard]] constexpr bool normalizationMethodIsMultiplicative(NormalizationMethod method) noexcept
{
    return method == NormalizationMethod::Multiplicative || method == NormalizationMethod::MultiplicativeScaling;
}

/// Whether a method matches dispersions as well as locations.
///
/// The two scaling methods read a frame's dispersion through the selected `ScaleEstimator`; the
/// two plain methods read its location alone and never consult the estimator.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param method the method to inspect.
/// @return true for `AdditiveScaling` and `MultiplicativeScaling`.
[[nodiscard]] constexpr bool normalizationMethodMatchesDispersion(NormalizationMethod method) noexcept
{
    return method == NormalizationMethod::AdditiveScaling || method == NormalizationMethod::MultiplicativeScaling;
}

/// The method's own name.
///
/// Thread-safe and allocation-free.
///
/// @param method the method to name. A value outside the enumeration is named
///        `"UnknownNormalizationMethod"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* normalizationMethodName(NormalizationMethod method) noexcept;

/// Which robust location and dispersion describe a frame.
enum class ScaleEstimator : std::uint8_t
{
    /// The median and the median absolute deviation around it.
    MedianAbsoluteDeviation,

    /// The iterative k-sigma estimator: the median and the corrected
    /// biweight scale of the samples within `kIterativeKSigmaClipWidth` median absolute deviations
    /// of the median, both constants stated in `GlobalNormalization.h` beside the operation that
    /// applies them. Despite the name, the window is decided once.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    IterativeKSigma
};

/// Whether a value names an estimator this build implements.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param estimator the value to inspect.
/// @return true for every implemented estimator.
[[nodiscard]] constexpr bool isScaleEstimator(ScaleEstimator estimator) noexcept
{
    return static_cast<std::uint8_t>(estimator) <= static_cast<std::uint8_t>(ScaleEstimator::IterativeKSigma);
}

/// The estimator's own name.
///
/// Thread-safe and allocation-free.
///
/// @param estimator the estimator to name. A value outside the enumeration is named
///        `"UnknownScaleEstimator"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* scaleEstimatorName(ScaleEstimator estimator) noexcept;

} // namespace an
