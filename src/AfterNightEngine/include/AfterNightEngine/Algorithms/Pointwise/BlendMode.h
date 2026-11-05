// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Named two-image compositing modes.
///
/// Each mode combines a base image with a blend image by the fixed formula its name selects, so a
/// composition a person built against those names keeps its look. The formulas are defined on
/// normalized samples: every mode is evaluated in double on values assumed to lie in `[0, 1]`, and
/// its result is clamped to that interval. Neither entry point checks the numeric domain, so an
/// image of raw codes or physical values has to be normalized first.
///
/// Three kinds of mode live here. Most are separable: `evaluateBlendSample` defines one sample pair,
/// and `blendImages` applies it to every channel, alpha included. `DarkerColor` and `LighterColor`
/// are separable on a one-channel image but on RGB choose a whole pixel by its Lab lightness. `Hue`,
/// `Saturation`, `Color` and `Lightness` exchange components between the two pixels in HSI or Lab
/// and need an RGB image with a stated encoding.
///
/// This is not the constant-weight `PointwiseOpKind::Blend` step of a pointwise program. That step
/// is fusable arithmetic; these modes run in their own image pass, because a colour-space round trip
/// per pixel is not something a pointwise program expresses.

#pragma once

#include <AfterNightEngine/Algorithms/Color/ColorTransform.h>
#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <cstdint>

namespace an
{

/// Every named blend mode.
enum class BlendMode : std::uint8_t
{
    /// Darkens the base by dividing its inverse by the blend; zero where the blend is zero.
    ColorBurn = 0u,

    /// Adds both samples and subtracts full scale.
    LinearBurn = 1u,

    /// Selects the darker sample, or the complete RGB pixel with lower Lab lightness; the base on a
    /// tie.
    DarkerColor = 2u,

    /// Selects the lighter sample, or the complete RGB pixel with higher Lab lightness; the blend
    /// on a tie.
    LighterColor = 3u,

    /// Multiplies the complements and complements the result.
    Screen = 4u,

    /// Reverses a screen composition to extract a star layer: `(base - blend) / (1 - blend)`, and
    /// zero where the blend is one or more.
    UnscreenStars = 5u,

    /// Brightens the base by dividing it by the blend's inverse; zero where the blend is exactly
    /// one.
    ColorDodge = 6u,

    /// Multiplies below half base intensity and screens above it.
    Overlay = 7u,

    /// Applies the quadratic soft-light curve `(1 - 2 blend) base^2 + 2 base blend`.
    SoftLight = 8u,

    /// Multiplies below half blend intensity and screens above it.
    HardLight = 9u,

    /// Applies color burn below half blend intensity and color dodge from half upward; zero where the
    /// blend is zero.
    VividLight = 10u,

    /// Adds twice the blend and subtracts full scale; zero where the blend is zero.
    LinearLight = 11u,

    /// Selects between a darken and lighten threshold around half blend intensity; zero where the
    /// blend is zero.
    PinLight = 12u,

    /// Absolute distance between the two samples.
    Difference = 13u,

    /// A lower-contrast difference blend.
    Exclusion = 14u,

    /// Raises the base to the blend power.
    Power = 15u,

    /// The quadrant-aware arc tangent of base over blend, clamped to full scale.
    ArcTangent = 16u,

    /// HSI hue from the blend with saturation and intensity from the base.
    Hue = 17u,

    /// HSI saturation from the blend with hue and intensity from the base.
    Saturation = 18u,

    /// Lab opponent channels from the blend with lightness from the base.
    Color = 19u,

    /// Lab lightness from the blend with opponent channels from the base.
    Lightness = 20u
};

/// Whether a value names one of the blend modes.
///
/// @param mode the value to inspect.
/// @return true for every supported mode.
[[nodiscard]] constexpr bool isBlendMode(BlendMode mode) noexcept
{
    return static_cast<std::uint8_t>(mode) <= static_cast<std::uint8_t>(BlendMode::Lightness);
}

/// Whether a mode applies independently to one sample pair.
///
/// Darker and lighter color are separable for a one-channel image but select a complete pixel for
/// RGB. The four component modes are never separable.
///
/// @param mode the mode to inspect.
/// @return true when the scalar evaluator below accepts it.
[[nodiscard]] constexpr bool isScalarBlendMode(BlendMode mode) noexcept
{
    return isBlendMode(mode) && mode != BlendMode::Hue && mode != BlendMode::Saturation && mode != BlendMode::Color &&
           mode != BlendMode::Lightness;
}

/// Evaluates one normalized sample pair under a separable blend mode.
///
/// The first value is the base and the second the blend. The result is clamped to `[0, 1]`.
///
/// The endpoint answers are deliberate: `ColorDodge` answers zero where the blend is exactly one, and
/// `ColorBurn`, `VividLight`, `LinearLight` and `PinLight` answer zero where it is exactly zero.
/// Changing any of them changes observable output.
///
/// Thread-safe, reentrant and allocation-free apart from a failure it describes.
///
/// @param mode a separable mode.
/// @param base finite base value, expected in the normalized domain; not range-checked.
/// @param blend finite blend value, expected in the normalized domain; not range-checked.
/// @return the blended value; `ErrorCode::InvalidArgument` for an unknown or component mode, or
///         `ErrorCode::InvalidData` for a non-finite input.
[[nodiscard]] AFTERNIGHTENGINE_API Result<double> evaluateBlendSample(BlendMode mode, double base, double blend);

/// The mode and color meaning of one whole-image blend.
struct BlendModeRequest
{
    /// Which named computation to perform.
    BlendMode mode = BlendMode::Screen;

    /// What the first three channels mean for the darker-color, lighter-color and component modes.
    ///
    /// Ignored by the separable modes and by a one-channel image. Otherwise it must pass
    /// `checkColorEncoding` and its schema must be `Rgb` for a three-channel image and `Rgba` for a
    /// four-channel image. Alpha is never part of a color-space conversion.
    ColorEncoding colorEncoding{};
};

/// Counts from one named blend execution.
struct BlendModeResult
{
    /// Pixels considered and admitted, and channel samples written.
    KernelRunResult run{};
};

/// Blends two equally shaped floating working images into a destination.
///
/// All three layouts may be planar, interleaved or mixed and may use different strides. Float32 and
/// Float64 are accepted; integer storage converts once through `Image/ImageConversion.h` because
/// these formulas are defined in a normalized processing domain. Exact aliasing with either input
/// is supported and every partial overlap is refused. Every channel of both inputs is loaded before
/// a destination channel is written.
///
/// A pixel participates only when both packed-mask views admit it and every sample read from both
/// inputs satisfies that input's value-validity rule. An invalid pixel leaves the destination
/// untouched. Separable modes operate on every channel, including alpha. Darker/lighter select a
/// complete RGB(A) pixel by Lab lightness, while Hue, Saturation, Color and Lightness preserve the
/// base alpha unchanged.
///
/// Thread-safe with respect to the execution and inputs, which are only read. Runs on the operation's
/// admitted threads. A cancellation or selected non-finite value may leave earlier rows written.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType Float32 or Float64, shared by all three images.
/// @param baseSamples first byte of the base image.
/// @param baseLayout the base image's geometry and strides.
/// @param blendSamples first byte of the blend image.
/// @param blendLayout the blend image's geometry and strides.
/// @param destinationSamples first writable byte of the destination.
/// @param destinationLayout the destination's geometry and strides.
/// @param request mode and explicit RGB encoding where required.
/// @param baseValidity the base image's validity and optional packed mask.
/// @param blendValidity the blend image's validity and optional packed mask.
/// @return counts on success; `ErrorCode::NotInitialized` for an empty execution;
///         `ErrorCode::InvalidArgument` for an unknown mode, a null or misaligned pointer, a layout
///         whose element width is not the representation's, a packed mask that is missing, not
///         needed or overlapping the destination, or a color encoding that is invalid or whose
///         schema disagrees with the channel count; `ErrorCode::IncompatibleArguments` for unequal
///         extents, a representation other than Float32 or Float64, more than four channels, a
///         whole-color or component mode on a channel count it does not support, a mask that does
///         not cover its image, or a partial overlap; `ErrorCode::InvalidData` for a selected
///         sample that is not finite; the color transform's failure; or the execution's
///         cancellation or backend-selection failure.
[[nodiscard]] AFTERNIGHTENGINE_API Result<BlendModeResult>
blendImages(const ExecutionContext& execution, SampleType sampleType, const void* baseSamples,
            const ImageLayout& baseLayout, const void* blendSamples, const ImageLayout& blendLayout,
            void* destinationSamples, const ImageLayout& destinationLayout, const BlendModeRequest& request,
            const KernelValidityView& baseValidity = {}, const KernelValidityView& blendValidity = {});

/// What `blendImages` does to the image it writes.
///
/// The samples are written and their meaning is restated, because a composite is not the quantity
/// either input measured; the validity combines both inputs' claims, since a pixel is written only
/// where both admit it.
inline constexpr OperationEffect kBlendModeEffect{.samples = SampleEffect::Writes,
                                                  .semantics = SemanticEffect::Restates,
                                                  .validity = ValidityTransition{.policy = ValidityPolicy::Combine}};

} // namespace an
