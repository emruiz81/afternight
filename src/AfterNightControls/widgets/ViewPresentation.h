// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What a view paints, computed by the engine: an image's presentation, one channel or colour component of it, the
/// overlay of a mask, the counts the lens shows and the values it reads out.
///
/// Each is one call over an asset's read lease under an admitted execution, into display bytes a `PresentedImage`
/// owns, and none of them touches Qt beyond the bytes it fills: the view decides on the GUI thread, a worker computes,
/// and the GUI thread publishes.
///
/// **What a view reads its image as.** A display shows a fraction of full scale. An integer sample is one whatever its
/// numeric domain; a floating sample in a physical or sensor-code domain has no full scale, so the view restates it as
/// a normalized linear fraction and says so in `ViewPresentationOutcome::domainRestated`. The image itself is never
/// changed.
///
/// **What a view shows of a colour image.** `ViewChannel` names one of the engine's channel scopes, read from the
/// source's red, green and blue as sRGB-encoded samples in unit interval units, or the chrominance view, which
/// composes CIE a* as red and b* as green over a black blue.
///
/// **A view presents what it paints.** Zoomed out it presents the image's display proxy (`session/DisplayProxy.h`),
/// reduced by the largest factor that leaves it at least the size painted; at any zoom it presents the region it shows
/// with a margin, when that is less than the whole. What it measures is always the whole image: a stretch's statistics
/// are taken from every sample of the image it shows, never from a region of it or from its proxy, whose averaging
/// would narrow them.
#pragma once

#include "AfterNightControlsExport.h"
#include "widgets/PresentationBridge.h"

#include <AfterNightConsumerRuntime/ConsumerMemory.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Algorithms/Color/ChannelScope.h>
#include <AfterNightEngine/Algorithms/Masks/MaskOverlay.h>
#include <AfterNightEngine/Algorithms/Presentation/DisplayPresentation.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace an
{
class EngineContext;
class ExecutionContext;
} // namespace an

namespace an::ui
{

class DisplayProxy;

/// What a view shows of an image: the whole of it, one channel or colour component, or the chrominance view.
struct ViewChannel
{
    /// The channel scope shown; `Whole` for the image itself.
    an::ChannelScope scope = an::ChannelScope::Whole;

    /// Whether the view shows CIE a* as red and b* as green instead; `scope` is then ignored.
    bool chrominance = false;

    /// Field-wise equality.
    [[nodiscard]] friend bool operator==(const ViewChannel& left, const ViewChannel& right) noexcept = default;
};

/// The channel a view can show of an image of this description: what is asked, or the whole image when the image is
/// not colour.
///
/// @param descriptor the image's description.
/// @param asked what the view asks for.
/// @return what it shows.
[[nodiscard]] AFTERNIGHTCONTROLS_API ViewChannel effectiveViewChannel(const an::ImageDescriptor& descriptor,
                                                                      ViewChannel asked) noexcept;

/// The canonical text of a channel choice, for a derived key: the scope's stable engine name, or `chrominance`.
///
/// @param channel the choice.
/// @return the text.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::string viewChannelKey(ViewChannel channel);

/// A rectangle of an image, in its pixels.
struct ViewRegion
{
    /// The first column.
    std::uint32_t x = 0u;

    /// The first row.
    std::uint32_t y = 0u;

    /// The width; at least one.
    std::uint32_t width = 1u;

    /// The height; at least one.
    std::uint32_t height = 1u;

    /// Field-wise equality.
    [[nodiscard]] friend bool operator==(const ViewRegion& left, const ViewRegion& right) noexcept = default;
};

/// Everything one presentation for a view decides, snapshotted on the GUI thread before a worker runs it.
struct ViewPresentationSettings
{
    /// The engine's presentation request. A view paints bytes, so an output other than interleaved bytes, red, green,
    /// blue and alpha bytes, or premultiplied blue, green, red and alpha bytes is presented as interleaved bytes.
    an::DisplayPresentationRequest request{};

    /// What is shown of the image.
    ViewChannel channel{};

    /// One presents every sample through the production plan; more presents every this-many-th sample of every
    /// this-many-th row through the real-time preview plan, which a view does not ask for.
    std::uint32_t subsampleFactor = 1u;

    /// One presents the image's own samples; more presents its display proxy reduced by this factor, which the caller
    /// hands `presentForView`.
    std::uint32_t proxyFactor = 1u;

    /// A region of the image, in its own pixels, to present instead of the whole of it; clipped to the image. Over a
    /// proxy it is presented from the proxy's pixels that cover it.
    std::optional<ViewRegion> region{};
};

/// The canonical text of every presentation decision, for a derived key's presentation policy.
///
/// @param settings the decisions.
/// @return the text: equal decisions give equal text.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::string viewPresentationKey(const ViewPresentationSettings& settings);

/// What one presentation for a view produced.
struct ViewPresentationOutcome
{
    /// The bytes the view paints.
    std::shared_ptr<const PresentedImage> image;

    /// The balance and statistics it presented through, which a later presentation of the same revision reuses.
    an::DisplayPresentationAnalysis analysis{};

    /// Whether `analysis` was the caller's rather than measured by this presentation.
    bool analysisReused = false;

    /// Whether this presentation measured the whole image beside the part of it, or the proxy, that it presented.
    bool measuredWhole = false;

    /// How long measuring the whole image took, when this presentation did; zero otherwise.
    std::chrono::nanoseconds wholeMeasureTime{0};

    /// Whether a floating image in a physical or sensor-code domain was read as a normalized fraction.
    bool domainRestated = false;

    /// What was shown: the channel effective for the image.
    ViewChannel channel{};

    /// The width of the image presented from, whatever part of it or reduction of it was presented.
    std::uint32_t sourceWidth = 0u;

    /// The height of the image presented from.
    std::uint32_t sourceHeight = 0u;

    /// How many of the image's pixels each way one presented pixel stands for: one, the proxy's factor or the subsample
    /// factor.
    std::uint32_t factor = 1u;

    /// The part of the image, in its own pixels, the presented bytes cover: the presented extent times the factor,
    /// from where the presented part begins.
    ViewRegion covered{};
};

/// Whether a presentation of a part of an image may measure the whole image when the analysis handed to it is refused.
enum class WholeMeasurement : std::uint8_t
{
    /// The whole image is measured, on the calling thread, and the part presented through it.
    Measured,

    /// Nothing is measured: the refusal is returned, and the caller shows what it has. For a call on the GUI thread.
    Refused
};

/// Presents an image for a view: the channel or component asked for, of the image or of its display proxy, whole or a
/// region of it, through the plan the settings select, into display bytes.
///
/// A presentation of the whole image measures what it presents. One of a region or of a proxy that stretches or
/// balances is presented through the whole image's statistics, measured here when no analysis is handed or the one
/// handed is refused, because a region's statistics are its own and a proxy's are narrowed by its averaging.
///
/// @param engine the engine context the execution runs in, for the working planes a channel or a region needs.
/// @param execution the presentation's admitted execution.
/// @param source the image, under a read lease held for the call.
/// @param settings what to present.
/// @param reuse a balance and statistics to present through instead of measuring, or null. An analysis the plan
///        refuses - another promotion, balance or channel count, or a preview's for the production plan - is set
///        aside and the presentation measures again.
/// @param memory the session's consumer memory, which the display bytes are counted against, or null.
/// @param proxy the image's display proxy at `settings.proxyFactor` when that is more than one, or null.
/// @param measurement whether a part whose handed analysis is refused may measure the whole image here; refused, the
///        refusal is returned and nothing is measured.
/// @return what was presented; `ErrorCode::EmptySelection` for a region outside the image;
///         `ErrorCode::InvalidArgument` for a proxy missing, of another factor or of another revision of the image; or
///         what the engine refused.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<ViewPresentationOutcome>
presentForView(const an::EngineContext& engine, const an::ExecutionContext& execution,
               const an::consumer::AssetReadLease& source, const ViewPresentationSettings& settings,
               const an::DisplayPresentationAnalysis* reuse, an::consumer::ConsumerMemory* memory,
               const DisplayProxy* proxy = nullptr, WholeMeasurement measurement = WholeMeasurement::Measured);

/// Measures what a presentation of a whole image would present through - its balance and statistics - without
/// presenting it: what a process dialog stretches its preview by, so the preview is stretched as the image it is
/// applied to is. The request is read as `presentForView` reads it.
///
/// @param engine the engine context the execution runs in.
/// @param execution the measurement's admitted execution.
/// @param source the image, under a read lease held for the call.
/// @param settings the decisions; the channel is read as `presentForView` reads it - a presentation of one channel
///        measures that channel - and the region is not: the whole image is measured.
/// @return the analysis, or what the engine refused.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<an::DisplayPresentationAnalysis>
analyzeForView(const an::EngineContext& engine, const an::ExecutionContext& execution,
               const an::consumer::AssetReadLease& source, const ViewPresentationSettings& settings);

/// How a view shows its mask over the image it presents.
struct ViewMaskOverlaySettings
{
    /// The tint, the mask itself, or the shown image multiplied by the mask.
    an::MaskOverlayMode mode = an::MaskOverlayMode::Tint;

    /// The tint's colour; its alpha is not read.
    an::MaskOverlayColor tint{.red = 255u, .green = 0u, .blue = 0u, .alpha = 255u};

    /// Whether the mask is shown inverted.
    bool invert = false;

    /// What the view shows of its image: a three-channel mask under a view of its red, green or blue channel shows
    /// that channel's mask alone.
    ViewChannel shownChannel{};

    /// One renders from the mask's own samples; more from its display proxy reduced by this factor, the reduction the
    /// image it lies over was presented at.
    std::uint32_t proxyFactor = 1u;

    /// The region of the mask, in its own pixels, the layer covers, as the image's presentation covered it; the whole
    /// mask when empty.
    std::optional<ViewRegion> region{};
};

/// Renders the layer a view paints over its image to show a mask.
///
/// A three-channel mask under a view of one stored channel contributes the matching channel. A multiply multiplies what
/// the view shows - `shown`, its presented bytes - so it follows the stretch the view applies; when the view shows one
/// channel and the mask has three, the multiply reads the mask's HSI intensity, the mean of its channels. Rendered from
/// the mask's proxy at the image's factor and over the image's region, the layer has the presented image's size.
///
/// @param engine the engine context, for the working planes a channel or a region of the mask needs.
/// @param execution the overlay's admitted execution.
/// @param mask the mask's image, under a read lease held for the call.
/// @param settings how the mask is shown.
/// @param shown what the view shows, for a multiply; null for the other modes.
/// @param memory the session's consumer memory, which the layer is counted against, or null.
/// @param maskProxy the mask's display proxy at `settings.proxyFactor` when that is more than one, or null.
/// @return the layer, straight red, green, blue and alpha bytes of the size of the part of the mask rendered; or what
///         the engine refused, such as a mask of another size than the shown image for a multiply or a mask that is not
///         one or three channels.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<std::shared_ptr<const PresentedImage>>
renderViewMaskOverlay(const an::EngineContext& engine, const an::ExecutionContext& execution,
                      const an::consumer::AssetReadLease& mask, const ViewMaskOverlaySettings& settings,
                      const PresentedImage* shown, an::consumer::ConsumerMemory* memory,
                      const DisplayProxy* maskProxy = nullptr);

/// What a view presents at a zoom: the display proxy's factor and the region of the image to present.
struct ViewDisplayGeometry
{
    /// One presents the image's own samples; more its display proxy reduced by this factor.
    std::uint32_t proxyFactor = 1u;

    /// The region of the image, in its own pixels, to present - what the view shows with a margin each way, aligned to
    /// the factor - or empty for the whole image.
    std::optional<ViewRegion> region{};
};

/// What a view presents of an image of a size at a zoom, showing a part of it.
///
/// The factor is the largest that leaves the proxy at least the size painted - `floor(1 / (scale * devicePixelRatio))`
/// below one, one at or above - so the residual scaling at paint is a reduction by less than two or a magnification
/// of native pixels. The region is the part shown with half its own size again on each side, so a short pan paints
/// from what is already presented; it is left empty when it would cover the image whole or nothing is known of what is
/// shown.
///
/// @param width the image's width.
/// @param height the image's height.
/// @param scale the view's zoom: painted pixels, in logical units, per image pixel.
/// @param devicePixelRatio the device pixels per logical pixel of the paint.
/// @param shown the part of the image shown, in its own pixels, or empty when it is not known.
/// @return the geometry.
[[nodiscard]] AFTERNIGHTCONTROLS_API ViewDisplayGeometry viewDisplayGeometry(std::uint32_t width, std::uint32_t height,
                                                                             double scale, double devicePixelRatio,
                                                                             const std::optional<ViewRegion>& shown);

/// The values of one pixel, as the lens reads them out.
struct PixelReadout
{
    /// How many values were read: one for grey, three for colour; an alpha channel is not read.
    std::uint32_t channels = 0u;

    /// Each channel's sample as a fraction of full scale - an integer over its representation's largest value, a
    /// floating sample as it is - or as the presented byte over 255 for a presented image.
    std::array<double, 3u> values{0.0, 0.0, 0.0};
};

/// Reads one pixel of an image.
///
/// @param image the image, under a read lease.
/// @param x the column; clamped into the image.
/// @param y the row; clamped into the image.
/// @return the readout, or nothing for a lease on nothing.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::optional<PixelReadout> readPixel(const an::consumer::AssetReadLease& image,
                                                                           std::int64_t x, std::int64_t y) noexcept;

/// Reads one pixel of presented bytes.
///
/// @param image the presented image.
/// @param x the column; clamped into the image.
/// @param y the row; clamped into the image.
/// @return the readout; an alpha byte is not read.
[[nodiscard]] AFTERNIGHTCONTROLS_API PixelReadout readPresentedPixel(const PresentedImage& image, std::int64_t x,
                                                                     std::int64_t y) noexcept;

/// How many bins the lens's histogram has: six bits of a byte.
inline constexpr std::uint32_t kLensHistogramBins = 64u;

/// The lens's histogram of a region of presented bytes: each colour channel's bytes counted into 64 bins of four codes.
struct LensHistogram
{
    /// How many channels were counted: one for grey, three for colour.
    std::uint32_t channels = 0u;

    /// Each channel's counts.
    std::array<std::array<std::uint64_t, kLensHistogramBins>, 3u> counts{};
};

/// Counts a region of presented bytes for the lens.
///
/// @param execution the count's admitted execution.
/// @param image the presented image.
/// @param originX the region's first column.
/// @param originY the region's first row.
/// @param width the region's width; a region is clipped to the image.
/// @param height the region's height.
/// @return the counts, or what the engine refused.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<LensHistogram>
countPresentedRegion(const an::ExecutionContext& execution, const PresentedImage& image, std::uint32_t originX,
                     std::uint32_t originY, std::uint32_t width, std::uint32_t height);

/// A brightness level of the viewer's eight-bit editing scale as the presentation's offset: the level over 255.
///
/// @param level the level.
/// @return the offset.
[[nodiscard]] AFTERNIGHTCONTROLS_API double presentationBrightnessOffset(int level) noexcept;

/// A contrast level of the viewer's eight-bit editing scale as the presentation's factor about one half.
///
/// @param level the level, below 259.
/// @return the factor.
[[nodiscard]] AFTERNIGHTCONTROLS_API double presentationContrastFactor(int level) noexcept;

} // namespace an::ui
