// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Generalized, histogram, asinh and screen-transfer stretches.
///
/// Nine one-dimensional curves under one parameter record, and three ways of applying them:
/// `evaluateGeneralizedStretch` for one sample, `stretchImage` for a working image, and
/// `remapStretchHistogram` for a histogram's counts. Every curve is defined on the unit interval:
/// its input is clamped into `[0, 1]` before it is evaluated, and it is computed in double.
///
/// The nonlinear families are piecewise. Below the low protection point and above the high one each
/// is a straight line; between them it is the family's curve on either side of the symmetry point,
/// with coefficients that make the pieces meet with matching slopes. Histogram inversion solves
/// the normalized rational primitive directly, using positive denominator terms at the protection
/// boundaries. The asinh curve is planned once for both directions, with tangent slopes that square
/// nothing, and inverted through the hyperbolic sine of the normalized primitive. The
/// generalized-hyperbolic curve is evaluated through one exponent per bias regime in both
/// directions, and inverted directly - where it saturates, from what it has still to rise.
///
/// On a colour image a curve may be applied per channel, to a luminance that then scales the RGB
/// channels together, or to a Lab or HSV component; `StretchColorMode` names each. The automatic
/// screen-transfer parameters are derived from caller-supplied channel statistics by
/// `deriveLinkedAutoStretch` and `deriveUnlinkedAutoStretch`, which read no pixels.

#pragma once

#include <AfterNightEngine/Algorithms/Color/ColorTransform.h>
#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Algorithms/Pointwise/MidtonesTransfer.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <array>
#include <cstdint>
#include <span>

namespace an
{

/// Every one-dimensional stretch family.
enum class StretchFunction : std::uint8_t
{
    /// The generalized hyperbolic family described by Payne, with bias selecting its branch: a
    /// logarithm at minus one, an exponential at zero, and a power law elsewhere - one that
    /// saturates between minus one and the largest bias, and one that grows without bound below
    /// minus one. A bias whose power law is the exponential to within a Float64 rounding - one
    /// whose magnitude is below `2^-80`, or whose product with the amount is below `2^-60` - is
    /// evaluated as the exponential, and an amount below `2^-60` as the identity it equals to
    /// within rounding.
    GeneralizedHyperbolic = 0u,

    /// The inverse of `GeneralizedHyperbolic` under the same parameters.
    InverseGeneralizedHyperbolic = 1u,

    /// The piecewise inverse-hyperbolic-sine family. An amount below `2^-60` is evaluated as the
    /// identity it equals to within rounding.
    Asinh = 2u,

    /// The inverse of `Asinh` under the same parameters.
    InverseAsinh = 3u,

    /// A black-point and white-point linear rescale, clamped to the unit interval. A white point at
    /// or below the black point makes it a step at the black point.
    Linear = 4u,

    /// The rational histogram-stretch family.
    Histogram = 5u,

    /// The inverse of `Histogram` under the same parameters.
    InverseHistogram = 6u,

    /// Unit-interval inversion, `1 - x`.
    Invert = 7u,

    /// A screen-transfer function with per-channel midtone, shadow and highlight parameters.
    ScreenTransfer = 8u
};

/// Whether a value names a stretch function this build implements.
///
/// @param function the value to inspect.
/// @return true for every implemented function.
[[nodiscard]] constexpr bool isStretchFunction(StretchFunction function) noexcept
{
    return static_cast<std::uint8_t>(function) <= static_cast<std::uint8_t>(StretchFunction::ScreenTransfer);
}

/// How a scalar stretch is applied to an RGB pixel.
enum class StretchColorMode : std::uint8_t
{
    /// Stretch each selected channel independently.
    Independent = 0u,

    /// Stretch a luminance and scale the selected RGB channels together: Rec.709 weights when all
    /// three are selected, equal weights over the selected ones otherwise.
    ///
    /// Where the scaled channels would pass one, the result is blended toward the independently
    /// stretched channels just far enough to keep the largest at one.
    HumanLuminance = 1u,

    /// Stretch the equal-weight mean of the selected RGB channels and scale them together, with the
    /// same blend toward the independent stretch as `HumanLuminance`.
    EqualLuminance = 2u,

    /// Stretch HSV saturation, then restore the original CIE Lab lightness.
    Saturation = 3u,

    /// Stretch CIE Lab lightness while retaining the opponent channels.
    Lightness = 4u,

    /// Stretch the luminance `StretchImageRequest::luminanceCoefficients` states and scale all
    /// three channels by the same ratio, with `StretchImageRequest::overflow` deciding what happens
    /// above one. A pixel whose luminance is at most 1e-12 becomes a neutral grey at the stretched
    /// value when every channel is that dark too, and black otherwise.
    Color = 5u
};

/// Whether a value names a supported color application mode.
///
/// @param mode the value to inspect.
/// @return true for every implemented mode.
[[nodiscard]] constexpr bool isStretchColorMode(StretchColorMode mode) noexcept
{
    return static_cast<std::uint8_t>(mode) <= static_cast<std::uint8_t>(StretchColorMode::Color);
}

/// How the color-luminance mode handles a scaled RGB value above full scale.
enum class StretchOverflowPolicy : std::uint8_t
{
    /// Clamp each scaled channel independently to the unit interval.
    Clip = 0u,

    /// Divide every scaled channel by one common factor, preserving their ratios.
    Rescale = 1u
};

/// Whether a value names a supported overflow policy.
///
/// @param policy the value to inspect.
/// @return true for both supported policies.
[[nodiscard]] constexpr bool isStretchOverflowPolicy(StretchOverflowPolicy policy) noexcept
{
    return policy == StretchOverflowPolicy::Clip || policy == StretchOverflowPolicy::Rescale;
}

/// A robust location and scale pair used to derive an automatic screen transfer.
struct StretchChannelStatistics
{
    /// Channel median in the normalized processing domain.
    double median = 0.0;

    /// Median absolute deviation in the same domain, before normal-consistency scaling.
    double medianAbsoluteDeviation = 0.0;

    /// Statistics compare by value.
    friend constexpr bool operator==(const StretchChannelStatistics& left,
                                     const StretchChannelStatistics& right) noexcept = default;
};

/// The smallest generalized-hyperbolic bias a request may state.
inline constexpr double kMinimumGeneralizedHyperbolicBias = -5.0;

/// The largest generalized-hyperbolic bias a request may state.
inline constexpr double kMaximumGeneralizedHyperbolicBias = 15.0;

/// All scalar parameters shared by the generalized stretch families.
///
/// Every member except `screenTransfer` is validated whichever function is named, so a member the
/// function ignores must still hold a value in its stated range.
struct GeneralizedStretchParameters
{
    /// Which curve to evaluate.
    StretchFunction function = StretchFunction::GeneralizedHyperbolic;

    /// Generalized-hyperbolic bias, accepted from `kMinimumGeneralizedHyperbolicBias` through
    /// `kMaximumGeneralizedHyperbolicBias` and ignored by other families.
    double bias = 0.0;

    /// Nonnegative stretch amount; zero makes every nonlinear family the identity.
    double amount = 0.0;

    /// Low protection point in the unit interval.
    double lowProtection = 0.0;

    /// Symmetry point, not below `lowProtection`.
    double symmetryPoint = 0.0;

    /// High protection point, not below `symmetryPoint` and at most one.
    double highProtection = 1.0;

    /// Linear black point in the unit interval, read only by `Linear`.
    double blackPoint = 0.0;

    /// Linear white point in the unit interval, read only by `Linear`.
    double whitePoint = 1.0;

    /// Per-channel screen-transfer parameters; a channel index above two reuses entry two. Checked
    /// only when `function` is `ScreenTransfer`.
    std::array<MidtonesTransferParameters, 3u> screenTransfer{};
};

/// Derives one linked automatic screen transfer from channel medians and deviations.
///
/// A normal-consistency factor, a breakdown guard and an inverted-image branch: each deviation is
/// scaled to a normal sigma, a sigma of zero is taken as 0.001, and when every median is above one
/// half the image is treated as inverted and the highlight point is derived instead of the shadow
/// point. The channels' medians and clipping points are averaged into one transfer. It reads no
/// pixels and allocates nothing.
///
/// @param statistics one or more channel statistics.
/// @param shadowsClipping signed sigma multiplier, conventionally `-2.8`.
/// @param targetBackground desired normalized background, conventionally `0.25`.
/// @return the linked parameters; `ErrorCode::InvalidArgument` for an empty span, a median outside
///         the unit interval, a deviation that is negative or not finite, a `shadowsClipping` that
///         is not finite, or a `targetBackground` outside the unit interval.
[[nodiscard]] AFTERNIGHTENGINE_API Result<MidtonesTransferParameters>
deriveLinkedAutoStretch(std::span<const StretchChannelStatistics> statistics, double shadowsClipping,
                        double targetBackground);

/// Derives one automatic screen transfer per channel.
///
/// All output slots are written only after every input is validated, so a refusal leaves the
/// caller's span unchanged. The rules are `deriveLinkedAutoStretch`'s applied to each channel
/// alone, except that whether the image is treated as inverted is still decided once, from every
/// channel's median.
///
/// @param statistics one or more channel statistics.
/// @param shadowsClipping signed sigma multiplier, conventionally `-2.8`.
/// @param targetBackground desired normalized background, conventionally `0.25`.
/// @param destination exactly as many output slots as statistics.
/// @return success; `ErrorCode::InvalidArgument` for any value `deriveLinkedAutoStretch` refuses or
///         spans of unequal length.
[[nodiscard]] AFTERNIGHTENGINE_API Status
deriveUnlinkedAutoStretch(std::span<const StretchChannelStatistics> statistics, double shadowsClipping,
                          double targetBackground, std::span<MidtonesTransferParameters> destination);

/// Evaluates one generalized-stretch request for one normalized sample.
///
/// Nonlinear inverse families follow their corresponding monotone forward definition. Histogram
/// inversion uses a normalized piecewise rational solve and asinh inversion the hyperbolic sine of
/// the normalized primitive; the asinh curve's tangent lines are planned without squaring the
/// amount in either direction. The generalized-hyperbolic family solves its curve's exponent
/// directly, and where the curve saturates solves what it has still to rise. An inverse maps zero
/// and below to positive zero and one and above to one, and so do the generalized-hyperbolic and
/// asinh forward curves. Nonlinear families ignore the linear black and white points, in a preview
/// and in an image alike.
///
/// Thread-safe, reentrant and allocation-free apart from a failure it describes. It plans the
/// coefficients on every call, so an image loop should use `stretchImage`.
///
/// @param value finite sample; every family clamps it to the unit interval first.
/// @param parameters the curve and all parameters it can read.
/// @param channel channel selecting a screen-transfer triplet; values above two select entry two.
/// @return the transformed value; `ErrorCode::InvalidArgument` for a parameter outside its range
///         or parameters whose coefficients are singular or not finite; or
///         `ErrorCode::InvalidData` for a non-finite input or result.
[[nodiscard]] AFTERNIGHTENGINE_API Result<double>
evaluateGeneralizedStretch(double value, const GeneralizedStretchParameters& parameters, std::uint32_t channel = 0u);

/// Parameters for applying one scalar stretch to a whole working image.
struct StretchImageRequest
{
    /// Scalar curve parameters.
    GeneralizedStretchParameters stretch{};

    /// How RGB channels participate.
    StretchColorMode colorMode = StretchColorMode::Independent;

    /// Overflow handling used only by `StretchColorMode::Color`.
    StretchOverflowPolicy overflow = StretchOverflowPolicy::Clip;

    /// Meaning of the first three channels for color modes.
    ///
    /// Ignored when the function is `Linear`, the mode is `Independent`, or the image has one
    /// channel. Otherwise it must pass `checkColorEncoding` and its schema must be `Rgb` for three
    /// channels and `Rgba` for four - for the two luminance modes too, although they do not convert
    /// through it.
    ColorEncoding colorEncoding{};

    /// Luminance weights read only by `StretchColorMode::Color`. Each must be finite; a negative
    /// one counts as zero, the rest are divided by their sum, and equal thirds are used when they
    /// sum to zero.
    std::array<double, 3u> luminanceCoefficients{1.0, 1.0, 1.0};

    /// Whether each channel is changed, read by `Independent` and the two luminance modes; the
    /// other colour modes change all three RGB channels. A one-channel image is always stretched,
    /// whatever entry zero says. Alpha is retained by default, and always by the colour modes.
    std::array<bool, 4u> channelEnabled{true, true, true, false};
};

/// Counts from one whole-image stretch.
struct StretchImageResult
{
    /// Pixels considered and admitted, and selected channel samples written.
    KernelRunResult run{};
};

/// Applies a generalized stretch to a normalized floating working image.
///
/// Float32 and Float64 are accepted, with at most four channels. Source and destination layouts may
/// differ and may be planar or interleaved. Exact in-place use is supported; partial overlap is
/// refused. Invalid pixels leave the destination untouched. Every sample of a valid pixel is first
/// clamped to `[0, 1]`, so unselected channels and alpha are copied as that clamped value.
///
/// Lightness and saturation modes convert through the request's stated encoding and CIE Lab;
/// luminance modes scale RGB together; independent mode applies the curve per selected channel.
/// Linear stretches always use independent mode.
///
/// Thread-safe with respect to the execution and source. Runs on the operation's admitted threads.
/// Cancellation or selected invalid data may leave earlier rows written.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType Float32 or Float64, shared by source and destination.
/// @param sourceSamples first byte of the source image.
/// @param sourceLayout the source geometry and strides.
/// @param destinationSamples first writable byte of the destination.
/// @param destinationLayout the destination geometry and strides.
/// @param request the curve, channel and color policy.
/// @param validity the source validity and optional packed mask.
/// @return counts on success; `ErrorCode::NotInitialized` for an empty execution;
///         `ErrorCode::InvalidArgument` for a stretch parameter outside its range or with singular
///         coefficients, an unknown color mode or overflow policy, a luminance coefficient that is
///         not finite, a null or misaligned pointer, a layout whose element width is not the
///         representation's, a packed mask that is missing, not needed or overlapping the
///         destination, or a color encoding that is invalid or whose schema disagrees with the
///         channel count; `ErrorCode::IncompatibleArguments` for a representation other than
///         Float32 or Float64, unequal extents, more than four channels, a color mode on a
///         two-channel image, a mask that does not cover the source, or a partial overlap;
///         `ErrorCode::InvalidData` for a selected sample or a result that is not finite; the color
///         transform's failure; or the execution's cancellation or backend-selection failure.
[[nodiscard]] AFTERNIGHTENGINE_API Result<StretchImageResult>
stretchImage(const ExecutionContext& execution, SampleType sampleType, const void* sourceSamples,
             const ImageLayout& sourceLayout, void* destinationSamples, const ImageLayout& destinationLayout,
             const StretchImageRequest& request, const KernelValidityView& validity = {});

/// Remaps one histogram through a generalized stretch.
///
/// Bin centers cover `[0, 1]`, bin `i` of `n` standing for `i / (n - 1)`. Each complete input count
/// is placed in the nearest output bin, so count conservation is exact. The input and destination
/// must have equal nonzero lengths and must not overlap. Validation is failure-atomic; cancellation,
/// checked every 4,096 bins on the calling thread, may leave a partial destination.
///
/// @param execution the operation policy and engine-context lease.
/// @param source caller-owned input counts.
/// @param destination caller-owned output counts, cleared after validation.
/// @param parameters the scalar stretch parameters; screen transfer uses channel zero.
/// @return success; `ErrorCode::NotInitialized` for an empty execution;
///         `ErrorCode::InvalidArgument` for empty, unequal or overlapping spans or parameters
///         `evaluateGeneralizedStretch` refuses; `ErrorCode::InvalidData` when the total count
///         overflows a 64-bit counter; or the execution's cancellation or backend-selection
///         failure.
[[nodiscard]] AFTERNIGHTENGINE_API Status remapStretchHistogram(const ExecutionContext& execution,
                                                                std::span<const std::uint64_t> source,
                                                                std::span<std::uint64_t> destination,
                                                                const GeneralizedStretchParameters& parameters);

/// What `stretchImage` does to the image it writes.
///
/// The samples are written and their meaning is restated, because a stretched sample no longer
/// stands for the quantity it measured; the validity is preserved, because invalid pixels are left
/// untouched.
inline constexpr OperationEffect kStretchImageEffect{.samples = SampleEffect::Writes,
                                                     .semantics = SemanticEffect::Restates,
                                                     .validity =
                                                         ValidityTransition{.policy = ValidityPolicy::Preserve}};

} // namespace an
