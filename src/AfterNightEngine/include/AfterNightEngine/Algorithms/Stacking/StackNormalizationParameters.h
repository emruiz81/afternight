// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Which normalization a stack request states, and to which reference frame.
///
/// `StackNormalizationScope` says whether frames are brought to the reference globally, locally,
/// or locally and then globally, and `StackNormalizationRequest` carries the scope with the global
/// method, the estimator it measures with, and the reference frame. These are the request's
/// values only: whether coefficients are prepared by the caller or measured by the stack is the
/// source contract's to say, and applying them is the source path's work. The header depends on
/// the normalization vocabulary and nothing else of the stacking surface.
#pragma once

#include <AfterNightEngine/Algorithms/Normalization/NormalizationMethod.h>
#include <AfterNightEngine/Core/Api.h>
#include <cstdint>
#include <optional>

namespace an
{

/// Which normalization a stack applies to its frames before combining them.
///
/// Global normalization brings every frame to the reference frame's level and spread with one
/// affine per channel; local normalization does so with a smooth surface. The stack states which,
/// and the source path applies the stated one to each frame as it is read.
enum class StackNormalizationScope : std::uint8_t
{
    /// Frames are combined as they are.
    None,

    /// Each frame is matched to the reference by `StackNormalizationRequest::globalMethod`.
    Global,

    /// Each frame is matched to the reference by a local normalization surface.
    Local,

    /// Local first, then the global method over the locally normalized frames.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    LocalThenGlobal
};

/// Whether a value names a scope this build defines.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param scope the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isStackNormalizationScope(StackNormalizationScope scope) noexcept
{
    return static_cast<std::uint8_t>(scope) <= static_cast<std::uint8_t>(StackNormalizationScope::LocalThenGlobal);
}

/// Whether a scope applies a global method, and therefore needs one named.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param scope the scope to test.
/// @return true for `Global` and `LocalThenGlobal`.
[[nodiscard]] constexpr bool stackNormalizationScopeUsesGlobalMethod(StackNormalizationScope scope) noexcept
{
    return scope == StackNormalizationScope::Global || scope == StackNormalizationScope::LocalThenGlobal;
}

/// The scope's own name.
///
/// Thread-safe and allocation-free.
///
/// @param scope the scope to name. A value outside the enumeration is named
///        `"UnknownStackNormalizationScope"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* stackNormalizationScopeName(StackNormalizationScope scope) noexcept;

/// Which normalization the stack applies, and to what reference.
struct StackNormalizationRequest
{
    /// Which normalization, if any.
    StackNormalizationScope scope = StackNormalizationScope::None;

    /// The method of the global normalization. Required when `scope` uses a global method and
    /// refused when it does not, so a method is never recorded beside a scope that ignores it.
    std::optional<NormalizationMethod> globalMethod{};

    /// The location and dispersion estimator the global normalization measures frames with. Read
    /// only under a global method that matches dispersions; the value must name an estimator
    /// whatever the scope.
    ScaleEstimator scaleEstimator = ScaleEstimator::IterativeKSigma;

    /// The frame every other frame is matched to, as an index into the stack's frames. Must be
    /// below `StackInputDescription::frameCount`. The noise and width weighting sources read the
    /// same frame as their reference, whatever the scope.
    std::uint32_t referenceFrame = 0u;

    /// Whether two requests state the same normalization.
    ///
    /// Not `noexcept`, because an optional's comparison is not.
    ///
    /// @param left one request.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend constexpr bool operator==(const StackNormalizationRequest& left,
                                                   const StackNormalizationRequest& right) = default;
};

} // namespace an
