// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Where a stack request's frame weights come from, and the local weight surface it applies.
///
/// `StackWeightingRequest` names one `StackWeighting` source per stack with the parameters the
/// derived sources read - the star-count scaling, the photometric model's terms, the floor - and
/// `LocalWeightingRequest` states the per-pixel surface applied on top of the frame weights. These
/// are the request's values only: `computeStackFrameWeights` in
/// `Algorithms/Stacking/StackFrameWeighting.h` derives the weights, and
/// `Algorithms/Stacking/LocalWeights.h` measures the surface. The header includes nothing of the
/// model, so these values can be read without the rest of the stacking surface.
#pragma once

#include <AfterNightEngine/Algorithms/Stacking/PhotometricWeighting.h>
#include <AfterNightEngine/Core/Api.h>
#include <cstdint>

namespace an
{

/// The default steepness of the exponential star-count scaling.
inline constexpr double kDefaultStarCountExponentialAlpha = 2.0;

/// The default floor a derived frame weight is raised to, so no frame is excluded outright.
inline constexpr double kDefaultMinimumFrameWeight = 0.01;

/// The default side, in pixels, of the window a local signal-to-noise weight is measured over.
inline constexpr std::uint32_t kDefaultLocalWeightWindow = 5u;

/// The smallest window a local weight may be measured over. A window narrower than three pixels
/// holds no neighbourhood.
inline constexpr std::uint32_t kMinimumLocalWeightWindow = 3u;

/// The default spacing, in pixels, of the grid local weights are sampled on.
inline constexpr std::uint32_t kDefaultLocalWeightGridSpacing = 64u;

/// The smallest grid spacing a local weight is sampled on. Below four pixels the grid is denser
/// than the window it is measured with and the interpolation between nodes has nothing to do.
inline constexpr std::uint32_t kMinimumLocalWeightGridSpacing = 4u;

/// The default exponent applied to a local signal-to-noise ratio to form its weight.
inline constexpr double kDefaultLocalWeightSnrExponent = 2.0;

/// Where a frame's weight comes from.
///
/// One source per stack. The derived sources are computed from measurements by the weighting
/// operation before the stack runs; `Explicit` takes the weights a caller already holds, which is
/// how photometric weights computed through `computePhotometricStackWeights` and any other
/// caller-owned set reach a stack.
enum class StackWeighting : std::uint8_t
{
    /// Every frame weighs one.
    Uniform,

    /// The caller supplies a `StackFrameWeights`, per frame or per frame and channel.
    Explicit,

    /// One over the square of each frame's background noise, the noise first multiplied by the
    /// reference frame's dispersion over the frame's own where both are measured.
    Noise,

    /// The number of detected stars in each frame, through a `StarCountScaling`.
    StarCount,

    /// The inverse square of each frame's full width at half maximum after a star-count penalty,
    /// rescaled between the narrowest and the widest; per channel when
    /// `StackWeightingRequest::perChannelFwhm` asks for it and the measurements carry channels.
    Fwhm,

    /// The number of frames already integrated into each input, for stacking previous stacks.
    ImageCount,

    /// The photometric model: zero-point transparency, its uncertainty and the alignment residual,
    /// under `StackWeightingRequest::photometric`.
    Photometric,

    /// Each frame's exposure time, over the mean of the frames': a frame exposed twice as long weighs
    /// twice as much.
    Exposure,

    /// Each frame's measured signal-to-noise ratio, over the mean of the frames'.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    SignalToNoise
};

/// Whether a value names a weighting source this build defines.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param weighting the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isStackWeighting(StackWeighting weighting) noexcept
{
    return static_cast<std::uint8_t>(weighting) <= static_cast<std::uint8_t>(StackWeighting::SignalToNoise);
}

/// Whether a weighting source is raised to `StackWeightingRequest::minimumWeight`.
///
/// The star-count and width sources produce a weight that can reach zero for the worst frame, and
/// the floor is what keeps that frame in the stack. Noise, image-count, exposure and
/// signal-to-noise weights are ratios that are positive by construction, the photometric source
/// carries its own floor, and the uniform and explicit sources are not derived at all.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param weighting the source to test.
/// @return true for `StarCount` and `Fwhm`.
[[nodiscard]] constexpr bool stackWeightingUsesMinimumWeight(StackWeighting weighting) noexcept
{
    return weighting == StackWeighting::StarCount || weighting == StackWeighting::Fwhm;
}

/// The source's own name.
///
/// Thread-safe and allocation-free.
///
/// @param weighting the source to name. A value outside the enumeration is named
///        `"UnknownStackWeighting"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* stackWeightingName(StackWeighting weighting) noexcept;

/// How a frame's star count becomes a weight, once the counts are normalized to the unit interval
/// between the fewest and the most stars in the set.
enum class StarCountScaling : std::uint8_t
{
    /// The normalized count itself.
    Linear,

    /// The square of the normalized count, which penalizes a poor frame more steeply.
    Quadratic,

    /// `exp(-alpha * (1 - normalized))`, a smooth fall-off whose steepness is
    /// `StarCountWeightingParameters::exponentialAlpha`.
    Exponential,

    /// Each frame's count multiplied by its stars' median roundness where that is measured, then
    /// the quadratic rule.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    RoundnessWeighted
};

/// Whether a value names a scaling this build defines.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param scaling the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isStarCountScaling(StarCountScaling scaling) noexcept
{
    return static_cast<std::uint8_t>(scaling) <= static_cast<std::uint8_t>(StarCountScaling::RoundnessWeighted);
}

/// The scaling's own name.
///
/// Thread-safe and allocation-free.
///
/// @param scaling the scaling to name. A value outside the enumeration is named
///        `"UnknownStarCountScaling"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* starCountScalingName(StarCountScaling scaling) noexcept;

/// How star counts become weights.
struct StarCountWeightingParameters
{
    /// The rule applied to the normalized count.
    StarCountScaling scaling = StarCountScaling::Quadratic;

    /// The steepness of `StarCountScaling::Exponential`. Positive and finite; read by that scaling
    /// alone.
    double exponentialAlpha = kDefaultStarCountExponentialAlpha;

    /// Whether two records state the same rule.
    ///
    /// @param left one record.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend constexpr bool operator==(const StarCountWeightingParameters& left,
                                                   const StarCountWeightingParameters& right) noexcept = default;
};

/// Where frame weights come from and how the derived ones are shaped.
struct StackWeightingRequest
{
    /// The source.
    StackWeighting method = StackWeighting::Uniform;

    /// Read by `StackWeighting::StarCount`.
    StarCountWeightingParameters starCount{};

    /// Read by `StackWeighting::Photometric`, with its own floor and penalties.
    PhotometricWeightingRequest photometric{};

    /// The floor a `StarCount` or `Fwhm` weight is raised to, in the closed unit interval, so the
    /// worst frame is down-weighted rather than dropped.
    double minimumWeight = kDefaultMinimumFrameWeight;

    /// Whether `StackWeighting::Fwhm` weights each channel by its own width where the measurements
    /// carry one per channel. Refused with any other source, because it would be a decision
    /// recorded beside a source it does not affect.
    bool perChannelFwhm = false;

    /// Whether two requests state the same weighting.
    ///
    /// @param left one request.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend constexpr bool operator==(const StackWeightingRequest& left,
                                                   const StackWeightingRequest& right) noexcept = default;
};

/// A per-pixel weight surface from each frame's local signal-to-noise ratio, applied on top of
/// the frame weight.
struct LocalWeightingRequest
{
    /// Whether a local weight is applied at all. When false the other members are inert.
    bool enabled = false;

    /// The side, in pixels, of the window the ratio is measured over. Odd and at least
    /// `kMinimumLocalWeightWindow`.
    std::uint32_t windowSize = kDefaultLocalWeightWindow;

    /// The spacing, in pixels, of the grid the ratio is sampled on. At least
    /// `kMinimumLocalWeightGridSpacing`.
    std::uint32_t gridSpacing = kDefaultLocalWeightGridSpacing;

    /// The exponent the ratio is raised to. Positive and finite.
    double snrExponent = kDefaultLocalWeightSnrExponent;

    /// Whether two requests state the same local weighting.
    ///
    /// @param left one request.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend constexpr bool operator==(const LocalWeightingRequest& left,
                                                   const LocalWeightingRequest& right) noexcept = default;
};

} // namespace an
