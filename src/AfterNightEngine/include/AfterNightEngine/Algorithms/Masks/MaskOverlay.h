// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What a viewer paints over an image to show its mask: one layer of red, green, blue and alpha bytes.
///
/// A mask says, sample by sample, how much of a process reaches the image: one lets all of it through,
/// zero protects the pixel. A viewer shows it as a layer painted over the presented image, and this header
/// is the operation that writes that layer - interleaved bytes in red, green, blue and alpha order, with
/// straight rather than premultiplied alpha, which is what a toolkit image of that format paints.
///
/// **Three modes.** `MaskOverlayMode::Tint` lays a stated colour over the image where the mask protects
/// it: every pixel is the tint, and its opacity is one minus the mask, so a fully selected pixel is clear
/// and a protected one is the tint. `MaskOverlayMode::Replace` shows the mask itself, opaque, in grey for a
/// one-channel mask and in colour for a three-channel one. `MaskOverlayMode::Multiply` shows an image the
/// caller passes - the presented image, or anything else of the mask's size - multiplied by the mask,
/// opaque. Inverting the mask is a field of the request, so what is shown is the mask a process would use.
///
/// **A three-channel mask** tints a pixel as three one-channel layers of the same colour stacked over one
/// another would: its opacity is one minus the product of the three channels. A caller that shows one of
/// the channels passes a one-channel view of it. Replace shows the three channels as a colour, and
/// multiply multiplies each of a colour image's channels by its own.
///
/// **What a sample is read as.** A mask or an image is read as a fraction of full scale, so its numeric
/// domain is a normalized one, linear or encoded; an integer sample is its code over its representation's
/// largest, by the engine's own conversion, and a floating one is as it is. A mask sample is then held to
/// the unit interval, with a sample that is not a number read as zero. A byte is a value in the unit
/// interval times 255, rounded to nearest with ties to even, so a value below zero or not a number is zero
/// and one above one is 255.
///
/// **Validity is per pixel.** A pixel any of whose read samples the source's validity rejects - the
/// mask's, and for a multiply the image's - is written as the request's invalid-pixel colour, by default
/// fully transparent, and counted.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/OperationEffect.h>

#include <cstdint>

namespace an
{

/// One operation's policy and its permission to run. Defined in `Runtime/ExecutionContext.h`.
class ExecutionContext;

/// What an image is. Defined in `Image/ImageDescriptor.h`.
class ImageDescriptor;

/// What a mask overlay shows.
enum class MaskOverlayMode : std::uint8_t
{
    /// The tint, with an opacity of one minus the mask.
    Tint = 0u,

    /// The mask itself, opaque.
    Replace = 1u,

    /// An image the caller passes, multiplied by the mask, opaque.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Multiply = 2u
};

/// Whether a value is one of the modes this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param mode the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isMaskOverlayMode(MaskOverlayMode mode) noexcept
{
    return static_cast<std::uint8_t>(mode) <= static_cast<std::uint8_t>(MaskOverlayMode::Multiply);
}

/// One pixel of an overlay: red, green, blue and a straight alpha, each a byte.
struct MaskOverlayColor
{
    /// The red byte.
    std::uint8_t red = 0u;

    /// The green byte.
    std::uint8_t green = 0u;

    /// The blue byte.
    std::uint8_t blue = 0u;

    /// The alpha byte: zero is clear, 255 opaque.
    std::uint8_t alpha = 0u;
};

/// Everything a mask overlay does, stated once.
///
/// A plain aggregate whose defaults are a red tint of the mask as it is, with invalid pixels clear.
struct MaskOverlayRequest
{
    /// What is shown.
    MaskOverlayMode mode = MaskOverlayMode::Tint;

    /// The colour a tint lays down; its alpha is not read, because the mask decides the opacity. Read by
    /// `MaskOverlayMode::Tint` alone.
    MaskOverlayColor tint{.red = 255u, .green = 0u, .blue = 0u, .alpha = 255u};

    /// Whether the mask is shown inverted: each of its samples, once held to the unit interval, read as
    /// one minus itself.
    bool invertMask = false;

    /// What a pixel is written as when a sample it reads is invalid.
    MaskOverlayColor invalidPixel{};
};

/// One image an overlay reads: the mask, or the image a multiply shows.
///
/// Every member is borrowed for the call. The descriptor states the representation, the extent, the
/// channels and the numeric domain the samples are read in.
struct MaskOverlaySource
{
    /// What the samples are. Must not be null.
    const ImageDescriptor* descriptor = nullptr;

    /// The first byte of the samples, aligned for their representation. Must not be null.
    const void* samples = nullptr;

    /// Where the samples sit; it must describe the descriptor. Must not be null.
    const ImageLayout* layout = nullptr;

    /// Which samples mean something. A mask with one plane per channel gives each channel its own
    /// plane; any other mask gives every channel the view's `maskPlane`.
    KernelValidityView validity{};
};

/// What one overlay did.
struct MaskOverlayResult
{
    /// Pixels considered and pixels every read sample of which was valid, and pixels written - every
    /// pixel of the layer, valid or not.
    KernelRunResult run{};
};

/// A checked mask overlay.
///
/// Built only by `planMaskOverlay`, so holding one means the request was accepted. Copyable; it holds
/// the request and nothing else.
class MaskOverlayPlan
{
public:
    /// What was asked for.
    ///
    /// @return the checked request.
    [[nodiscard]] const MaskOverlayRequest& request() const noexcept
    {
        return m_request;
    }

private:
    /// Records a checked request.
    ///
    /// @param request the request `planMaskOverlay` accepted.
    explicit MaskOverlayPlan(const MaskOverlayRequest& request) noexcept : m_request(request) {}

    /// The planner is the only constructor's caller.
    friend AFTERNIGHTENGINE_API Result<MaskOverlayPlan> planMaskOverlay(const MaskOverlayRequest& request);

    /// The checked request.
    MaskOverlayRequest m_request;
};

/// Checks a request and makes the plan.
///
/// Thread-safe and allocation-free apart from a failure it describes.
///
/// @param request the mode, the tint, the inversion and the invalid-pixel colour.
/// @return the plan; or `ErrorCode::InvalidArgument` for a mode outside its enumeration.
[[nodiscard]] AFTERNIGHTENGINE_API Result<MaskOverlayPlan> planMaskOverlay(const MaskOverlayRequest& request);

/// What an overlay of a mask writes, as a layout request.
///
/// One interleaved plane of four bytes per pixel, red, green, blue and alpha, the mask's width and
/// height, each row starting on a four-byte boundary. The executor accepts any such layout, padded or
/// not.
///
/// Thread-safe. Allocates only the failure it describes.
///
/// @param plan the overlay.
/// @param mask the mask's description, whose width and height the layer has.
/// @return the request to build the destination's layout from; or `ErrorCode::IncompatibleArguments`
///         for a mask that is not one or three channels.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ImageLayoutRequest> maskOverlayLayout(const MaskOverlayPlan& plan,
                                                                                const ImageDescriptor& mask);

/// Writes the overlay of a mask.
///
/// Every pixel of the layer is written once, from the mask's samples and, for a multiply, the image's,
/// each computed in double and rounded once to its byte.
///
/// Thread-safe with respect to both sources, which are only read. Runs on the operation's admitted
/// threads, and the answer does not depend on how many. Allocation-free apart from a failure it
/// describes. A stop may leave earlier rows written.
///
/// @param execution the operation policy and engine-context lease.
/// @param plan the overlay.
/// @param mask the mask: one or three channels, in a normalized domain.
/// @param image the image a multiply shows: one or three channels of the mask's width and height, in a
///        normalized domain, with three channels when the mask has three; null for the other modes.
/// @param destination the first byte written.
/// @param destinationLayout where the layer goes: one interleaved plane of four one-byte samples per
///        pixel, the mask's width and height.
/// @return what was done; `ErrorCode::NotInitialized` for an empty execution;
///         `ErrorCode::InvalidArgument` for a null member of a source, a layout or pointer that does not
///         describe its descriptor, a null destination, an image passed to a mode other than multiply or
///         missing from a multiply, or a validity mask missing, unwanted or overlapping the destination;
///         `ErrorCode::IncompatibleArguments` for a source that is not one or three channels or not in a
///         normalized domain, an image of another width or height than the mask or of one channel under
///         a three-channel mask, a destination of another shape or arrangement, a destination
///         overlapping a source, or a validity mask that does not cover its source; or the execution's
///         cancellation or backend-selection failure.
[[nodiscard]] AFTERNIGHTENGINE_API Result<MaskOverlayResult>
renderMaskOverlay(const ExecutionContext& execution, const MaskOverlayPlan& plan, const MaskOverlaySource& mask,
                  const MaskOverlaySource* image, void* destination, const ImageLayout& destinationLayout);

/// What `renderMaskOverlay` does to the layer it writes.
///
/// A new destination of another description - display bytes rather than what the mask measured - so
/// the meaning is restated, the source's header cards and output decision do not describe it, and every
/// byte is a value. The pixel grid is the mask's, so an astrometric solution still describes it.
inline constexpr OperationEffect kMaskOverlayEffect{
    .description = DescriptionEffect::Restates,
    .semantics = SemanticEffect::Restates,
    .codecMetadata = CodecMetadataEffect::Drops,
    .outputIntent = OutputIntentEffect::Withdraws,
    .validity = {.policy = ValidityPolicy::Regenerate, .produced = ValidityDescriptor{}}};

} // namespace an
