// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A colour image's chrominance combined with the lightness of a separate luminance image.
///
/// A luminance frame taken through a clear filter carries the detail and the signal; three colour frames carry the
/// hue. The combination decomposes the colour image into its CIE lightness, chroma and hue, replaces its lightness
/// with the luminance image - or blends the two by a weight - optionally stretching the luminance and the chroma by
/// a midtones transfer first, recomposes the colour, and scales each recomposed channel by a weight.
///
/// **The encoding and the scales.** The colour image's three channels are read under a stated encoding, sRGB-encoded
/// by default, and converted to CIE L*a*b* under its white. The lightness is read over a hundred, so the luminance
/// image, a fraction of full scale, stands in for it directly; the chroma transfer acts on the chroma `hypot(a*, b*)`
/// as a fraction of two hundred, and a* and b* are scaled by the stretched chroma over the chroma, which keeps their
/// hue without passing it through an angle. The recomposed colour is held to the unit interval before the channel
/// weights, and each weighted channel again after them.
///
/// **What is read and written.** Both images are read as fractions of full scale - an integer by the engine's
/// conversion, a floating sample as it is - so their numeric domain is a normalized one. Each pixel is computed in
/// double from its samples, and the result is written once in the representation the caller states, rounded to
/// nearest with ties to even and saturating; an integer or ruled source is read through Float32 working planes
/// first, Float64 when the result is Float64.
///
/// **Validity is per pixel.** A pixel any read sample of which either image's validity rejects is written as the
/// colour image's samples, a sample its own validity rejects as zero, and counted.

#pragma once

#include <AfterNightEngine/Algorithms/Color/ColorTransform.h>
#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <array>
#include <cstdint>

namespace an
{

/// One operation's policy and its permission to run. Defined in `Runtime/ExecutionContext.h`.
class ExecutionContext;

/// What an image is. Defined in `Image/ImageDescriptor.h`.
class ImageDescriptor;

/// Everything an LRGB combination does, stated once.
///
/// A plain aggregate whose defaults replace the colour image's lightness with the luminance image, stretch
/// nothing, weigh every channel one, and read the colour as sRGB-encoded.
struct LrgbCombinationRequest
{
    /// How much of the lightness the luminance image supplies: zero keeps the colour image's own, one replaces it.
    /// Within the unit interval.
    double lightnessWeight = 1.0;

    /// The midtones balance the luminance image is stretched by before it is used; one half leaves it as it is.
    /// Within the unit interval.
    double lightnessMidtones = 0.5;

    /// The midtones balance the colour image's chroma is stretched by; one half leaves it as it is. Within the unit
    /// interval.
    double chromaMidtones = 0.5;

    /// The weight each recomposed red, green and blue channel is multiplied by; finite and not negative.
    std::array<double, 3u> channelWeights{1.0, 1.0, 1.0};

    /// What the colour image's three channels are. Its schema must be `ChannelSchema::Rgb` and it must pass
    /// `checkColorEncoding`.
    ColorEncoding colorEncoding{
        .schema = ChannelSchema::Rgb, .primaries = RgbPrimaries::Srgb, .transfer = ColorTransferFunction::Srgb};

    /// The representation the result is written in.
    SampleType outputSampleType = SampleType::Float32;
};

/// One image an LRGB combination reads: the colour image or the luminance image.
///
/// Every member is borrowed for the call. The descriptor states the representation, the extent, the channels and
/// the numeric domain the samples are read in.
struct LrgbCombinationSource
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

/// What one combination did.
struct LrgbCombinationResult
{
    /// Pixels considered and pixels every read sample of which was valid, and samples written - every sample of the
    /// result, an invalid pixel as the colour image's.
    KernelRunResult run{};
};

/// A checked LRGB combination.
///
/// Built only by `planLrgbCombination`, so holding one means the request was accepted. Copyable; it holds the
/// request and nothing else.
class LrgbCombinationPlan
{
public:
    /// What was asked for.
    ///
    /// @return the checked request.
    [[nodiscard]] const LrgbCombinationRequest& request() const noexcept
    {
        return m_request;
    }

private:
    /// Records a checked request.
    ///
    /// @param request the request `planLrgbCombination` accepted.
    explicit LrgbCombinationPlan(const LrgbCombinationRequest& request) noexcept : m_request(request) {}

    /// The planner is the only constructor's caller.
    friend AFTERNIGHTENGINE_API Result<LrgbCombinationPlan> planLrgbCombination(const LrgbCombinationRequest& request);

    /// The checked request.
    LrgbCombinationRequest m_request;
};

/// Checks a request and makes the plan.
///
/// Thread-safe and allocation-free apart from a failure it describes.
///
/// @param request the weights, the midtones balances, the colour encoding and the result's representation.
/// @return the plan; or `ErrorCode::InvalidArgument` for a weight or balance outside its interval or not finite, an
///         encoding that is invalid or not RGB, or a representation outside its enumeration.
[[nodiscard]] AFTERNIGHTENGINE_API Result<LrgbCombinationPlan>
planLrgbCombination(const LrgbCombinationRequest& request);

/// What a combination writes, as a layout request.
///
/// Three planes of the request's representation, the colour image's width and height, each row starting on an
/// element boundary. The executor accepts any layout of that shape, padded or interleaved.
///
/// Thread-safe. Allocates only the failure it describes.
///
/// @param plan the combination.
/// @param color the colour image's description.
/// @return the request to build the destination's layout from; or `ErrorCode::IncompatibleArguments` for a colour
///         image that is not three channels.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ImageLayoutRequest> lrgbCombinationLayout(const LrgbCombinationPlan& plan,
                                                                                    const ImageDescriptor& color);

/// Combines a colour image with a luminance image.
///
/// The colour is decomposed into packed L*C*h*, the luminance and the chroma stretched when their balances are not
/// one half, the lightness replaced or blended, the colour recomposed and held to the unit interval, each channel
/// weighed and held again, and every sample written once.
///
/// Thread-safe with respect to both sources, which are only read. Runs on the operation's admitted threads, and the
/// answer does not depend on how many. Takes working memory from the execution's engine context. A stop may leave
/// the destination partly written.
///
/// @param execution the operation policy and engine-context lease.
/// @param plan the combination.
/// @param color the colour image: three channels in a normalized domain.
/// @param luminance the luminance image: one channel of the colour image's width and height, in a normalized
///        domain.
/// @param destination the first byte written.
/// @param destinationLayout where the result goes: three channels of the request's representation and the colour
///        image's width and height.
/// @return what was done; `ErrorCode::NotInitialized` for an empty execution; `ErrorCode::InvalidArgument` for a
///         null member of a source, a layout or pointer that does not describe its descriptor, a null destination,
///         a destination layout that does not describe the result's representation, or a validity mask missing,
///         unwanted or overlapping the destination; `ErrorCode::IncompatibleArguments` for a colour image that is
///         not three channels, a luminance image that is not one channel of its width and height, a source not in
///         a normalized domain, a destination of another extent or overlapping a source, or a validity mask that
///         does not cover its source; `ErrorCode::InvalidData` for a sample that is not finite in a pixel the
///         validity admits; the working memory's refusal; the colour transform's or the stretch's failure; or the
///         execution's cancellation or backend-selection failure.
[[nodiscard]] AFTERNIGHTENGINE_API Result<LrgbCombinationResult>
combineLrgb(const ExecutionContext& execution, const LrgbCombinationPlan& plan, const LrgbCombinationSource& color,
            const LrgbCombinationSource& luminance, void* destination, const ImageLayout& destinationLayout);

/// What `combineLrgb` does to the image it writes.
///
/// A new image whose representation is the request's and whose samples are neither source's, so the description
/// and the meaning are restated; the pixel grid is the colour image's, so its astrometric solution still describes
/// the result. The validity combines both sources' claims.
inline constexpr OperationEffect kLrgbCombinationEffect{.description = DescriptionEffect::Restates,
                                                        .semantics = SemanticEffect::Restates,
                                                        .validity =
                                                            ValidityTransition{.policy = ValidityPolicy::Combine}};

} // namespace an
