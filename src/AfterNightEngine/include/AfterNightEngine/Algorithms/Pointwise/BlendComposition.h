// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// One image blended over another by one of twenty-seven named modes, with the blend image's pre-filters, the
/// target's preserved lightness and an opacity.
///
/// The image blend a person composes is more than its mode: each image may first be stretched by a midtones
/// transfer, the blend image may be blurred, high-passed or colourized, a grey image meets a colour one as three
/// equal channels, the target's CIE lightness may be kept through the blend, and the result is laid over the
/// target at an opacity. This header is that whole composition, in that order, over the engine's own operations:
/// `stretchImage` for the transfers, the exact Gaussian for the blur and the high pass, `colorizeImage` for the
/// colourizing filters, the colour transform for the lightness, and the named formulas of
/// `Algorithms/Pointwise/BlendMode.h` for the twenty modes it holds.
///
/// **The twenty-seven modes** are the product's, in its menu's order: the twenty `BlendMode` names, and Replace,
/// Darken, Lighten, Multiply, Linear Dodge, Subtract and Divide, which are image arithmetic - the darker and
/// lighter sample, the product, the sum held at one, the difference held at zero, and the quotient held to the
/// unit interval with a zero divisor answering zero. Lightness over a grey blend image takes that image's samples
/// as the lightness itself rather than as three equal channels.
///
/// **What is read and written.** Both images are read as fractions of full scale - an integer by the engine's
/// conversion, a floating sample as it is - so their numeric domain is a normalized one. One channel or three
/// each; the result has three when either image has three or the mode or filter makes colour, and one otherwise.
/// Every step works on Float32 planes, Float64 when the result is Float64, and the result is written once in the
/// representation the caller states, rounded to nearest with ties to even and saturating.
///
/// **The opacity** lays the composite over the target as the target was before its transfer: the target times one
/// less the opacity plus the composite times the opacity. It is part of the composition so the whole answer is one
/// call.
///
/// **Validity is per pixel.** A pixel any read sample of which either image's validity rejects is written as the
/// target's samples, a sample its own validity rejects as zero, and counted.

#pragma once

#include <AfterNightEngine/Algorithms/Color/ColorTransform.h>
#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Algorithms/Pointwise/MidtonesTransfer.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <cstdint>

namespace an
{

/// One operation's policy and its permission to run. Defined in `Runtime/ExecutionContext.h`.
class ExecutionContext;

/// What an image is. Defined in `Image/ImageDescriptor.h`.
class ImageDescriptor;

/// The twenty-seven blend modes the product offers, in its menu's order.
///
/// The target is the base and the blend image the blend in every formula.
enum class BlendCompositionMode : std::uint8_t
{
    /// The blend image.
    Replace = 0u,

    /// The darker of the two samples, the target on a tie.
    Darken = 1u,

    /// The product of the two samples.
    Multiply = 2u,

    /// `BlendMode::ColorBurn`.
    ColorBurn = 3u,

    /// `BlendMode::LinearBurn`.
    LinearBurn = 4u,

    /// `BlendMode::DarkerColor`.
    DarkerColor = 5u,

    /// The lighter of the two samples, the target on a tie.
    Lighten = 6u,

    /// `BlendMode::Screen`.
    Screen = 7u,

    /// `BlendMode::ColorDodge`.
    ColorDodge = 8u,

    /// The sum of the two samples, held at one.
    LinearDodge = 9u,

    /// `BlendMode::LighterColor`.
    LighterColor = 10u,

    /// `BlendMode::Overlay`.
    Overlay = 11u,

    /// `BlendMode::SoftLight`.
    SoftLight = 12u,

    /// `BlendMode::HardLight`.
    HardLight = 13u,

    /// `BlendMode::VividLight`.
    VividLight = 14u,

    /// `BlendMode::LinearLight`.
    LinearLight = 15u,

    /// `BlendMode::PinLight`.
    PinLight = 16u,

    /// `BlendMode::Difference`.
    Difference = 17u,

    /// `BlendMode::Exclusion`.
    Exclusion = 18u,

    /// The target less the blend image, held at zero.
    Subtract = 19u,

    /// The target over the blend image, held to the unit interval; zero where the blend image is zero.
    Divide = 20u,

    /// `BlendMode::Power`.
    Power = 21u,

    /// `BlendMode::ArcTangent`.
    ArcTangent = 22u,

    /// `BlendMode::Hue`.
    Hue = 23u,

    /// `BlendMode::Saturation`.
    Saturation = 24u,

    /// `BlendMode::Color`.
    Color = 25u,

    /// `BlendMode::Lightness`; a grey blend image's samples are the lightness itself.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Lightness = 26u
};

/// Whether a value is one of the modes this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param mode the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isBlendCompositionMode(BlendCompositionMode mode) noexcept
{
    return static_cast<std::uint8_t>(mode) <= static_cast<std::uint8_t>(BlendCompositionMode::Lightness);
}

/// What is done to the blend image, after its transfer and before the blend.
enum class BlendPreFilter : std::uint8_t
{
    /// Nothing.
    None = 0u,

    /// The exact Gaussian of the request's standard deviation.
    GaussianBlur = 1u,

    /// The image less its exact Gaussian, plus one half, held to the unit interval.
    HighPass = 2u,

    /// The image's CIE lightness - the image itself when it is grey - colourized with the request's hue and
    /// saturation as the HSV value: three channels.
    ColorizeLightness = 3u,

    /// Each channel colourized with the request's hue and saturation as its own HSV value: three channels.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    ColorizeChannels = 4u
};

/// Whether a value is one of the pre-filters this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param filter the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isBlendPreFilter(BlendPreFilter filter) noexcept
{
    return static_cast<std::uint8_t>(filter) <= static_cast<std::uint8_t>(BlendPreFilter::ColorizeChannels);
}

/// Everything an image blend does, stated once.
///
/// A plain aggregate whose defaults replace the target with the blend image, with no transfer, no filter, no kept
/// lightness, full opacity, and colour read as sRGB-encoded.
struct BlendCompositionRequest
{
    /// The mode.
    BlendCompositionMode mode = BlendCompositionMode::Replace;

    /// The midtones transfer the target is stretched by first; the identity, the default, leaves it as it is.
    MidtonesTransferParameters targetTransfer{};

    /// The midtones transfer the blend image is stretched by first; the identity leaves it as it is.
    MidtonesTransferParameters blendTransfer{};

    /// What is done to the blend image after its transfer.
    BlendPreFilter filter = BlendPreFilter::None;

    /// The standard deviation of the blur and the high pass, in pixels - what the product's dialog calls the
    /// radius. Finite and above `kMinimumGaussianSigma`; read by those two filters alone. One above about 32 runs
    /// as several exact passes whose variances add up to it, so its cost grows with its square.
    double filterSigma = 10.0;

    /// The colourizing filters' hue, in degrees; finite, wrapped modulo 360.
    double filterHueDegrees = 0.0;

    /// The colourizing filters' saturation, within the unit interval.
    double filterSaturation = 1.0;

    /// Whether the composite keeps the transferred target's CIE lightness. Read only when the result has three
    /// channels.
    bool preserveLightness = false;

    /// How much of the composite is laid over the target, within the unit interval.
    double opacity = 1.0;

    /// What three channels are, for the colour modes, the lightness filter and the kept lightness. Its schema must
    /// be `ChannelSchema::Rgb` and it must pass `checkColorEncoding`.
    ColorEncoding colorEncoding{
        .schema = ChannelSchema::Rgb, .primaries = RgbPrimaries::Srgb, .transfer = ColorTransferFunction::Srgb};

    /// The representation the result is written in.
    SampleType outputSampleType = SampleType::Float32;
};

/// One image a blend reads: the target or the blend image.
///
/// Every member is borrowed for the call. The descriptor states the representation, the extent, the channels and
/// the numeric domain the samples are read in.
struct BlendCompositionSource
{
    /// What the samples are. Must not be null.
    const ImageDescriptor* descriptor = nullptr;

    /// The first byte of the samples, aligned for their representation. Must not be null.
    const void* samples = nullptr;

    /// Where the samples sit; it must describe the descriptor. Must not be null.
    const ImageLayout* layout = nullptr;

    /// Which samples mean something. A mask with one plane per channel gives each channel its own plane; any other
    /// mask gives every channel the view's `maskPlane`.
    KernelValidityView validity{};
};

/// What one blend did.
struct BlendCompositionResult
{
    /// Pixels considered and pixels every read sample of which was valid, and samples written - every sample of the
    /// result, an invalid pixel as the target's.
    KernelRunResult run{};

    /// How many exact Gaussian passes the blur or the high pass ran; zero for the other filters.
    std::uint32_t gaussianPasses = 0u;
};

/// A checked image blend.
///
/// Built only by `planBlendComposition`, so holding one means the request was accepted. Copyable; it holds the
/// request and the Gaussian's pass count and nothing else.
class BlendCompositionPlan
{
public:
    /// What was asked for.
    ///
    /// @return the checked request.
    [[nodiscard]] const BlendCompositionRequest& request() const noexcept
    {
        return m_request;
    }

    /// How many exact Gaussian passes a blur or a high pass runs, each of the standard deviation over the square
    /// root of their count; zero for the other filters.
    ///
    /// @return the pass count.
    [[nodiscard]] std::uint32_t gaussianPasses() const noexcept
    {
        return m_gaussianPasses;
    }

private:
    /// Records a checked request.
    ///
    /// @param request the request `planBlendComposition` accepted.
    /// @param gaussianPasses the passes its filter runs.
    BlendCompositionPlan(const BlendCompositionRequest& request, std::uint32_t gaussianPasses) noexcept
        : m_request(request), m_gaussianPasses(gaussianPasses)
    {
    }

    /// The planner is the only constructor's caller.
    friend AFTERNIGHTENGINE_API Result<BlendCompositionPlan>
    planBlendComposition(const BlendCompositionRequest& request);

    /// The checked request.
    BlendCompositionRequest m_request;

    /// The Gaussian's passes.
    std::uint32_t m_gaussianPasses = 0u;
};

/// Checks a request and makes the plan.
///
/// Thread-safe and allocation-free apart from a failure it describes.
///
/// @param request the mode, the transfers, the filter, the kept lightness, the opacity, the colour encoding and the
///        result's representation.
/// @return the plan; or `ErrorCode::InvalidArgument` for an enumerator outside its enumeration, a transfer whose
///         parameters are not finite, within the unit interval and ordered, a filter parameter or opacity outside
///         its interval or not finite, an encoding that is invalid or not RGB, or a representation outside its
///         enumeration.
[[nodiscard]] AFTERNIGHTENGINE_API Result<BlendCompositionPlan>
planBlendComposition(const BlendCompositionRequest& request);

/// How many channels a blend of two images writes: three when either image has three or the mode or filter makes
/// colour, one otherwise.
///
/// Thread-safe and allocation-free.
///
/// @param plan the blend.
/// @param target the target's description.
/// @param blend the blend image's description.
/// @return one or three.
[[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t blendCompositionChannels(const BlendCompositionPlan& plan,
                                                                          const ImageDescriptor& target,
                                                                          const ImageDescriptor& blend) noexcept;

/// What a blend writes, as a layout request.
///
/// `blendCompositionChannels` planes of the request's representation, the target's width and height, each row
/// starting on an element boundary. The executor accepts any layout of that shape, padded or interleaved.
///
/// Thread-safe. Allocates only the failure it describes.
///
/// @param plan the blend.
/// @param target the target's description.
/// @param blend the blend image's description.
/// @return the request to build the destination's layout from; or `ErrorCode::IncompatibleArguments` for an image
///         that is not one or three channels, or two of different widths or heights.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ImageLayoutRequest>
blendCompositionLayout(const BlendCompositionPlan& plan, const ImageDescriptor& target, const ImageDescriptor& blend);

/// Blends one image over another.
///
/// Both images are read and transferred, the blend image filtered, a grey image given three equal channels where
/// the other is colour, the target's lightness measured when it is kept, the mode applied, the lightness restored,
/// the composite laid over the target at the opacity, and every sample written once.
///
/// Thread-safe with respect to both sources, which are only read. Runs on the operation's admitted threads, and the
/// answer does not depend on how many. Takes working memory from the execution's engine context. A stop may leave
/// the destination partly written.
///
/// @param execution the operation policy and engine-context lease.
/// @param plan the blend.
/// @param target the target: one or three channels in a normalized domain.
/// @param blend the blend image: one or three channels of the target's width and height, in a normalized domain.
/// @param destination the first byte written.
/// @param destinationLayout where the result goes: `blendCompositionChannels` channels of the request's
///        representation and the target's width and height.
/// @return what was done; `ErrorCode::NotInitialized` for an empty execution; `ErrorCode::InvalidArgument` for a
///         null member of a source, a layout or pointer that does not describe its descriptor, a null destination,
///         a destination layout that does not describe the result's representation, or a validity mask missing,
///         unwanted or overlapping the destination; `ErrorCode::IncompatibleArguments` for an image that is not one
///         or three channels or not in a normalized domain, two of different widths or heights, a destination of
///         another extent or overlapping a source, or a validity mask that does not cover its source;
///         `ErrorCode::InvalidData` for a sample that is not finite in a pixel the validity admits; the working
///         memory's refusal; the failure of a composed operation; or the execution's cancellation or
///         backend-selection failure.
[[nodiscard]] AFTERNIGHTENGINE_API Result<BlendCompositionResult>
composeBlend(const ExecutionContext& execution, const BlendCompositionPlan& plan, const BlendCompositionSource& target,
             const BlendCompositionSource& blend, void* destination, const ImageLayout& destinationLayout);

/// What `composeBlend` does to the image it writes.
///
/// A new image whose representation is the request's and whose samples are neither source's, so the description
/// and the meaning are restated; the pixel grid is the target's, so its astrometric solution still describes the
/// result. The validity combines both sources' claims.
inline constexpr OperationEffect kBlendCompositionEffect{.description = DescriptionEffect::Restates,
                                                         .semantics = SemanticEffect::Restates,
                                                         .validity =
                                                             ValidityTransition{.policy = ValidityPolicy::Combine}};

} // namespace an
