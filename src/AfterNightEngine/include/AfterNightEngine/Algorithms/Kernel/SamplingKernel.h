// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The policy vocabulary of coordinate sampling: interpolation, border and invalid neighbours.
///
/// Resampling, gathering and the geometry operations built on them all read a plane at a
/// coordinate, and these three choices are everything that decides the answer there besides the
/// samples: which taps an interpolation reads and how it weighs them, what stands in for a tap
/// outside the plane, and what happens when a tap the validity rule rejects carries weight.
/// `SamplingKernelParameters` is the complete set; `NearestKernelParameters` is the narrower
/// set of the nearest-neighbour entry points, which accept only `Clamp` and `Constant`.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/KernelContract.h>

namespace an
{

/// How a coordinate combines nearby pixel centers.
enum class KernelInterpolation : std::uint8_t
{
    /// Read the center reached by rounding each coordinate component to the nearest integer.
    Nearest = 0u,

    /// Combine the four surrounding centers with bilinear weights accumulated in `double`.
    Linear = 1u,

    /// Combine a four-by-four neighbourhood with the OpenCV-compatible bicubic kernel (`a=-0.75`).
    Cubic = 2u,

    /// Integrate source pixel areas for resize; for a point-sampled warp, use linear interpolation.
    Area = 3u,

    /// Combine an eight-by-eight neighbourhood with a radius-four Lanczos window.
    Lanczos4 = 4u,

    /// A fixed-point linear resize for UInt8, UInt16 and Int16 samples, by OpenCV's `INTER_LINEAR_EXACT`
    /// rule. Other representations and point warps use the backend-independent linear evaluation order.
    LinearExact = 5u,

    /// A nearest resize in a 16-bit fixed-point coordinate, by OpenCV's `INTER_NEAREST_EXACT` rule, that
    /// copies exact stored values. Point warps use the backend-independent nearest-center tie rule.
    NearestExact = 6u
};

/// Whether a value names a supported interpolation rule.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param interpolation the value to test.
/// @return true for every declared interpolation rule.
[[nodiscard]] constexpr bool isKernelInterpolation(KernelInterpolation interpolation) noexcept
{
    return static_cast<std::uint8_t>(interpolation) <= static_cast<std::uint8_t>(KernelInterpolation::NearestExact);
}

/// What interpolation does when a nonzero-weight source sample is invalid.
enum class SamplingInvalidPolicy : std::uint8_t
{
    /// Leave the destination untouched unless every contributing source sample is valid.
    RequireAll,

    /// Omit invalid contributors and normalize the remaining weights; write nothing when none remain.
    Renormalize,

    /// Use `SamplingKernelParameters::invalidSample` in place of every invalid contributor.
    Substitute
};

/// Whether a value names a supported invalid-neighbor rule.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param policy the value to test.
/// @return true for each of the three declared policies.
[[nodiscard]] constexpr bool isSamplingInvalidPolicy(SamplingInvalidPolicy policy) noexcept
{
    return static_cast<std::uint8_t>(policy) <= static_cast<std::uint8_t>(SamplingInvalidPolicy::Substitute);
}

/// Complete numeric and footprint policy for a coordinate-sampling invocation.
struct SamplingKernelParameters
{
    /// How nearby pixel centers contribute.
    KernelInterpolation interpolation = KernelInterpolation::Nearest;

    /// How each outside tap is resolved. Every `KernelBorderMode` except `NotApplicable` is valid.
    KernelBorderMode border = KernelBorderMode::Clamp;

    /// Finite value of a tap outside the source under `KernelBorderMode::Constant`.
    float constantBorder = 0.0f;

    /// How nonzero-weight invalid source taps affect an output.
    SamplingInvalidPolicy invalid = SamplingInvalidPolicy::RequireAll;

    /// Finite replacement for an invalid source tap under `SamplingInvalidPolicy::Substitute`.
    float invalidSample = 0.0f;
};

/// Border behavior shared by nearest resample and gather invocations.
struct NearestKernelParameters
{
    /// `Clamp` reads the nearest edge sample; `Constant` produces `constantBorder`.
    KernelBorderMode border = KernelBorderMode::Clamp;

    /// Output produced outside the source footprint under `KernelBorderMode::Constant`.
    float constantBorder = 0.0f;
};

} // namespace an
