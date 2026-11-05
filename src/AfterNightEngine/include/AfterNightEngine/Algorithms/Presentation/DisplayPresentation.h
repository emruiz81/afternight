// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What a viewer paints: one stated pipeline from an image to display samples.
///
/// Every place that shows an image - a viewer, a real-time preview, a thumbnail, an exported frame, a
/// command-line stretch - needs the same few steps between the samples an image holds and the bytes a
/// screen or a file receives, and separate copies of them would disagree: on how a sample is promoted
/// before it is stretched, on which limits a colour balance measures, on whether colour channels are
/// stretched together, and on how a sample becomes a byte. This header is the one pipeline, and every
/// step of it is a decision the request states.
///
/// **The steps, in the order they run.**
///
/// 1. *Promotion.* Every sample becomes a Float32 working sample as the engine converts one at full
///    scale - an integer divided by its representation's largest value, a floating sample carried as
///    it is - so a signed integer below zero is below the black point rather than shifted to mid-grey -
///    and is then held to the unit interval, which is all a display shows, so the statistics measure what
///    is presented. `PresentationPromotion::Quantized16` additionally holds every sample to one of 65,536 levels.
///    A sample the source's validity rule or mask rejects, and every non-finite sample, becomes a
///    quiet NaN here and stays one: no later step measures it, and the output says how it is shown.
/// 2. *Colour balance,* on three-channel colour only, each step when asked for. Neutralization
///    measures each channel's background as the median of the samples inside a stated closed range
///    and moves every channel's background onto their mean. Calibration measures each channel's white
///    as the mean inside a stated range and its background as the median inside another, and scales
///    each channel about its background so that every white stands as far above it as the strongest
///    channel's does. Both clamp to the unit interval.
/// 3. *Statistics,* for `PresentationStretch::Automatic`: each colour channel's exact median and median
///    absolute deviation over its valid samples, after the balance.
/// 4. *Stretch.* The automatic stretch is the midtones transfer `deriveLinkedAutoStretch` or
///    `deriveUnlinkedAutoStretch` derives from those statistics, applied to each colour channel; a
///    linked stretch gives every channel one transfer. `PresentationStretch::Linear` stretches
///    nothing. An alpha channel is never stretched.
/// 5. *Saturation,* on three-channel colour when its coefficient is not zero: the HSL saturation
///    enhancement of `Algorithms/Color/ColorAdjustment.h`, above a lightness threshold derived from
///    the stretched green channel by the named estimator.
/// 6. *Brightness and contrast:* an offset added and the result clamped, then a factor about one half
///    and the result clamped again.
/// 7. *Output,* in one of four forms: interleaved bytes with the source's channels, four interleaved
///    bytes per pixel in red, green, blue and alpha order, four in blue, green, red and alpha order with
///    the colour premultiplied by alpha - what a toolkit paints without converting - or the planar
///    Float32 working image a histogram reads. A byte is the working sample clamped to the unit interval
///    and scaled by 255, rounded to nearest with ties to even - the engine's own conversion to UInt8.
///
/// **Two plans, and only one of them may approximate.** `planDisplayPresentation` is the production
/// plan: every sample of the source is presented and measured, so what it shows is what an Apply that
/// writes the result back would write. `planRealtimePreviewDisplayPresentation` is the preview plan a
/// responsive control uses: it may present every n-th sample of every n-th row, and when it measures,
/// it measures those samples alone. Its analysis says so, and the production executor refuses it, so
/// a preview's statistics cannot reach a production result. Neither plan writes a log or reports
/// progress: the pipeline is a fixed chain of passes over one frame, and a caller presenting several
/// frames counts them itself.
///
/// **Statistics are reused, not recomputed per paint.** Each executor returns the analysis it used -
/// the balance it applied and the statistics it measured - and accepts one back. A caller keys it on
/// the identity and generations of the image it measured, and may present a different image of the
/// same channels through it, which is how a preview of part of an image is stretched as the whole
/// image is.

#pragma once

#include <AfterNightEngine/Algorithms/Color/ColorAdjustment.h>
#include <AfterNightEngine/Algorithms/Color/ColorChannels.h>
#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Algorithms/Pointwise/MidtonesTransfer.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>

#include <array>
#include <cstdint>

namespace an
{

/// One operation's policy and its permission to run. Defined in `Runtime/ExecutionContext.h`.
class ExecutionContext;

/// What an image is. Defined in `Image/ImageDescriptor.h`.
class ImageDescriptor;

/// The shadows clipping a display's automatic stretch uses unless asked for another: the black point
/// sits this many normalized median absolute deviations from the median.
inline constexpr double kDisplayShadowsClipping = -2.8;

/// The background level a display's automatic stretch moves the median to unless asked for another.
inline constexpr double kDisplayTargetBackground = 0.25;

/// The largest factor a real-time preview may subsample by.
inline constexpr std::uint32_t kMaxRealtimePreviewSubsampleFactor = 4096u;

/// Whether a presentation stretches.
enum class PresentationStretch : std::uint8_t
{
    /// The working samples are shown as they are, after any colour balance.
    Linear = 0u,

    /// The midtones transfer the statistics derive moves each channel's background to the target.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Automatic = 1u
};

/// How a source sample becomes a working sample before anything else.
enum class PresentationPromotion : std::uint8_t
{
    /// The engine's full-scale conversion to Float32 and nothing more, so a stretch can reveal every
    /// level the source holds.
    Gradient = 0u,

    /// The same conversion, then every sample held to the nearest of 65,536 evenly spaced levels of
    /// the unit interval, with a sample outside it clamped first. A strong stretch shows the levels as
    /// bands; an unsigned source of sixteen bits or fewer, every code of which is already one of those
    /// levels, is unchanged by it, and a signed one moves by less than half a level.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Quantized16 = 1u
};

/// What a presentation writes.
enum class PresentationOutput : std::uint8_t
{
    /// UInt8, one byte per sample, interleaved, with the source's channels: grey, RGB or RGBA.
    Interleaved8 = 0u,

    /// UInt8, four bytes per pixel interleaved in red, green, blue and alpha order. Grey is written
    /// to all three colour bytes, and alpha is opaque unless the source carries an alpha channel.
    Rgba8 = 1u,

    /// Float32, planar, with the source's channels: the working image after the last step, within the
    /// unit interval, for a caller that measures what it shows, such as a histogram. An invalid sample is
    /// a quiet NaN.
    WorkingFloat32 = 2u,

    /// UInt8, four bytes per pixel interleaved in blue, green, red and alpha order, each colour
    /// premultiplied by the pixel's alpha before it becomes a byte. Grey is written to all three colour
    /// bytes, and alpha is opaque unless the source carries an alpha channel, so an opaque pixel's
    /// colour bytes are `Rgba8`'s in the other order. On a little-endian host the four bytes are one
    /// 32-bit premultiplied ARGB word, which a toolkit paints without converting it.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Bgra8 = 3u
};

/// How a presentation shows a sample that means nothing.
enum class PresentationInvalidPixels : std::uint8_t
{
    /// An invalid sample is written as zero, and an alpha byte stays what the pixel's alpha is.
    Black = 0u,

    /// As `Black`, and in an output with alpha a pixel with any invalid colour sample is written
    /// entirely as zero, so it is transparent. An output without alpha shows it black.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Transparent = 1u
};

/// Whether a value is one of the stretches this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param stretch the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isPresentationStretch(PresentationStretch stretch) noexcept
{
    return static_cast<std::uint8_t>(stretch) <= static_cast<std::uint8_t>(PresentationStretch::Automatic);
}

/// Whether a value is one of the promotions this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param promotion the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isPresentationPromotion(PresentationPromotion promotion) noexcept
{
    return static_cast<std::uint8_t>(promotion) <= static_cast<std::uint8_t>(PresentationPromotion::Quantized16);
}

/// Whether a value is one of the outputs this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param output the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isPresentationOutput(PresentationOutput output) noexcept
{
    return static_cast<std::uint8_t>(output) <= static_cast<std::uint8_t>(PresentationOutput::Bgra8);
}

/// Whether a value is one of the invalid-pixel rules this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param invalidPixels the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isPresentationInvalidPixels(PresentationInvalidPixels invalidPixels) noexcept
{
    return static_cast<std::uint8_t>(invalidPixels) <=
           static_cast<std::uint8_t>(PresentationInvalidPixels::Transparent);
}

/// A closed interval of working samples a colour balance measures.
struct PresentationInterval
{
    /// The lowest sample measured. Finite and within `[0, 1]`.
    double low = 0.0;

    /// The highest sample measured. Finite, within `[0, 1]` and not below `low`.
    double high = 1.0;
};

/// Everything a presentation does, stated once.
///
/// A plain aggregate: every field has the default a viewer starts from, the planners check them all,
/// and the plan keeps a copy. The constants of the automatic stretch are this header's, so a caller
/// that wants the default stretch states nothing.
struct DisplayPresentationRequest
{
    /// How a source sample becomes a working sample.
    PresentationPromotion promotion = PresentationPromotion::Gradient;

    /// Whether three-channel colour is neutralized: every channel's background moved onto their mean.
    bool neutralize = false;

    /// The samples neutralization measures each channel's background over.
    PresentationInterval neutralizationBackground{0.0, 0.1};

    /// Whether three-channel colour is calibrated against its own white and background.
    bool calibrate = false;

    /// The samples calibration measures each channel's background over, by their median.
    PresentationInterval calibrationBackground{0.0, 0.1};

    /// The samples calibration measures each channel's white over, by their mean.
    PresentationInterval calibrationWhite{0.0, 0.9};

    /// Whether the presentation stretches.
    PresentationStretch stretch = PresentationStretch::Automatic;

    /// Whether every colour channel takes the one transfer derived from all of them. Ignored for a
    /// single channel, where the two are the same.
    bool linked = false;

    /// The automatic stretch's shadows clipping, in normalized median absolute deviations. Finite.
    double shadowsClipping = kDisplayShadowsClipping;

    /// The level the automatic stretch moves the background to. Finite and within `[0, 1]`.
    double targetBackground = kDisplayTargetBackground;

    /// What is added to the saturation multiplier: zero leaves saturation alone and skips the step,
    /// one doubles it. Finite. Applies to three-channel colour only.
    double saturation = 0.0;

    /// The multiplier on the location plus the deviation of the stretched green channel that makes
    /// the saturation step's lightness threshold. Finite; a threshold above one is held at one.
    double saturationBackgroundFactor = 1.0;

    /// Which location of the stretched green channel the saturation threshold starts from.
    SaturationBackgroundLocation saturationBackgroundLocation = SaturationBackgroundLocation::Median;

    /// The offset added to every colour sample after the saturation step. Finite and within
    /// `[-1, 1]`; zero leaves the samples alone.
    double brightness = 0.0;

    /// The factor every colour sample is scaled by about one half after the brightness. Finite and
    /// positive; one leaves the samples alone.
    double contrast = 1.0;

    /// What the presentation writes.
    PresentationOutput output = PresentationOutput::Interleaved8;

    /// How a sample that means nothing is shown.
    PresentationInvalidPixels invalidPixels = PresentationInvalidPixels::Black;
};

/// What a presentation measured, which a caller may hand back to present without measuring again.
///
/// A value: it borrows nothing and holds nothing of the image it came from. A caller keys a copy on
/// the identity and the pixel and validity generations of the image it was taken from. An executor
/// accepts one only when it was taken with the plan's own promotion and balance and has what the
/// plan's stretch needs; the production executor also refuses one a preview took.
struct DisplayPresentationAnalysis
{
    /// The colour channels measured: one for grey, three for colour. An alpha channel is not one.
    std::uint32_t colorChannels = 0u;

    /// The promotion the samples were measured under.
    PresentationPromotion promotion = PresentationPromotion::Gradient;

    /// Whether a real-time preview took it, from subsampled samples.
    bool realtimePreview = false;

    /// Whether neutralization was applied, and so whether `neutralizationOffsets` mean anything.
    bool neutralized = false;

    /// What neutralization added to each colour channel; zero when it did not run.
    ColorChannelOffsets neutralizationOffsets{0.0, 0.0, 0.0};

    /// Whether calibration was applied, after any neutralization.
    bool calibrated = false;

    /// What calibration multiplied each colour channel by; one when it did not run.
    ColorChannelGains calibrationGains{1.0, 1.0, 1.0};

    /// What calibration added to each colour channel after the gain; zero when it did not run.
    ColorChannelOffsets calibrationOffsets{0.0, 0.0, 0.0};

    /// Whether the statistics below were measured, which an automatic stretch needs.
    bool measured = false;

    /// Each colour channel's median after the balance; zero for a channel not measured.
    std::array<double, 3u> medians{0.0, 0.0, 0.0};

    /// Each colour channel's median absolute deviation, not scaled to a sigma.
    std::array<double, 3u> medianAbsoluteDeviations{0.0, 0.0, 0.0};

    /// How many valid samples each colour channel's statistics came from. A channel with none takes
    /// no part in deriving a transfer, and is presented through the identity.
    std::array<std::uint64_t, 3u> measuredSamples{0u, 0u, 0u};
};

/// What one presentation did.
struct DisplayPresentationResult
{
    /// The balance and statistics it used: the caller's, when one was accepted, or its own.
    DisplayPresentationAnalysis analysis{};

    /// Whether `analysis` is the caller's rather than measured by this call.
    bool analysisReused = false;

    /// The transfer each colour channel took: the identity under `PresentationStretch::Linear`.
    std::array<MidtonesTransferParameters, 3u> transfer{};

    /// The lightness threshold the saturation step used, or zero when the step did not run.
    double saturationBackground = 0.0;

    /// The width of what was written, in pixels.
    std::uint32_t width = 0u;

    /// The height of what was written, in pixels.
    std::uint32_t height = 0u;

    /// Pixels with at least one invalid colour sample.
    std::uint64_t invalidPixels = 0u;
};

/// The image a presentation reads.
///
/// Every member is borrowed for the call. The descriptor states the representation, the extent, the
/// channels - `ChannelSchema::Mono`, `ChannelSchema::Rgb` or `ChannelSchema::Rgba`, whose fourth channel
/// is alpha - and the numeric domain. An integer source is presented as a fraction of its
/// representation's full scale whatever its domain; a floating one must state a normalized or encoded
/// domain, because a physical quantity or a sensor code in floating point has no full scale to present.
struct DisplayPresentationSource
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

/// A checked production presentation.
///
/// Built only by `planDisplayPresentation`, so holding one means the request was accepted. Copyable;
/// it holds the request and nothing else.
class DisplayPresentationPlan
{
public:
    /// What was asked for.
    ///
    /// @return the checked request.
    [[nodiscard]] const DisplayPresentationRequest& request() const noexcept
    {
        return m_request;
    }

private:
    /// Records a checked request.
    ///
    /// @param request the request `planDisplayPresentation` accepted.
    explicit DisplayPresentationPlan(const DisplayPresentationRequest& request) noexcept : m_request(request) {}

    /// The planner is the only constructor's caller.
    friend AFTERNIGHTENGINE_API Result<DisplayPresentationPlan>
    planDisplayPresentation(const DisplayPresentationRequest& request);

    /// The checked request.
    DisplayPresentationRequest m_request;
};

/// A checked real-time preview presentation.
///
/// Built only by `planRealtimePreviewDisplayPresentation`. A distinct type from the production plan,
/// so no production executor can be handed one.
class RealtimePreviewDisplayPresentationPlan
{
public:
    /// What was asked for.
    ///
    /// @return the checked request.
    [[nodiscard]] const DisplayPresentationRequest& request() const noexcept
    {
        return m_request;
    }

    /// Which samples the preview presents: every this-many-th sample of every this-many-th row,
    /// starting from the first. One presents every sample.
    ///
    /// @return the factor, at least one.
    [[nodiscard]] std::uint32_t subsampleFactor() const noexcept
    {
        return m_subsampleFactor;
    }

private:
    /// Records a checked request and factor.
    ///
    /// @param request the request the planner accepted.
    /// @param subsampleFactor the factor the planner accepted.
    RealtimePreviewDisplayPresentationPlan(const DisplayPresentationRequest& request,
                                           std::uint32_t subsampleFactor) noexcept
        : m_request(request), m_subsampleFactor(subsampleFactor)
    {
    }

    /// The planner is the only constructor's caller.
    friend AFTERNIGHTENGINE_API Result<RealtimePreviewDisplayPresentationPlan>
    planRealtimePreviewDisplayPresentation(const DisplayPresentationRequest& request, std::uint32_t subsampleFactor);

    /// The checked request.
    DisplayPresentationRequest m_request;

    /// The checked factor.
    std::uint32_t m_subsampleFactor = 1u;
};

/// Checks a request and makes the production plan.
///
/// Thread-safe and allocation-free.
///
/// @param request every decision of the presentation.
/// @return the plan; or `ErrorCode::InvalidArgument` for an enumerator outside its enumeration, an
///         interval that is not finite, ordered and within `[0, 1]`, a clipping that is not finite, a
///         target background outside `[0, 1]`, a saturation or saturation factor that is not finite,
///         a brightness outside `[-1, 1]` or a contrast that is not finite and positive.
[[nodiscard]] AFTERNIGHTENGINE_API Result<DisplayPresentationPlan>
planDisplayPresentation(const DisplayPresentationRequest& request);

/// Checks a request and a subsample factor and makes the real-time preview plan.
///
/// Thread-safe and allocation-free.
///
/// @param request every decision of the presentation.
/// @param subsampleFactor which samples to present: every this-many-th of every this-many-th row.
///        Within `[1, kMaxRealtimePreviewSubsampleFactor]`.
/// @return the plan; `ErrorCode::OutOfRange` for a factor outside its range; or whatever
///         `planDisplayPresentation` refuses the request for.
[[nodiscard]] AFTERNIGHTENGINE_API Result<RealtimePreviewDisplayPresentationPlan>
planRealtimePreviewDisplayPresentation(const DisplayPresentationRequest& request, std::uint32_t subsampleFactor);

/// What a production presentation of this source writes, as a layout request.
///
/// The source's extent; one byte per sample with the source's channels interleaved, four interleaved
/// bytes for `Rgba8` and `Bgra8`, or four-byte samples in one plane per channel, as the plan's output
/// asks; and rows aligned
/// to four bytes for the byte outputs, which a toolkit image built over the buffer expects, and to the
/// engine's buffer alignment for the working image. The executor accepts any layout with the same
/// extent and arrangement, padded or not.
///
/// Thread-safe and allocation-free.
///
/// @param plan the presentation.
/// @param source what it reads.
/// @return the request to build the destination's layout from; or `ErrorCode::IncompatibleArguments`
///         for a schema other than grey, RGB or RGBA, or a channel count the schema does not have.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ImageLayoutRequest>
displayPresentationLayout(const DisplayPresentationPlan& plan, const ImageDescriptor& source);

/// What a real-time preview presentation of this source writes, as a layout request.
///
/// As `displayPresentationLayout`, over an extent of one pixel for every subsampled one: the source's
/// width and height divided by the factor and rounded up.
///
/// Thread-safe and allocation-free.
///
/// @param plan the presentation.
/// @param source what it reads.
/// @return the request; or what `displayPresentationLayout` refuses the source for.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ImageLayoutRequest>
realtimePreviewDisplayPresentationLayout(const RealtimePreviewDisplayPresentationPlan& plan,
                                         const ImageDescriptor& source);

/// Promotes, balances and measures a source as the production plan would, and writes nothing.
///
/// For a caller that keys an analysis on an image before presenting it, or presents part of an image
/// through the whole image's analysis. Under `PresentationStretch::Linear` nothing is measured, and the
/// analysis holds the balance alone.
///
/// A Float32 source under the gradient promotion, with no balance and no mask, is measured where it lies:
/// the statistics read each stored sample held to the unit interval, which is what its promotion is,
/// so no working image is taken. An eight- or sixteen-bit source with no balance is measured from how
/// often each of its codes occurs among the samples its rule and mask admit, each code promoted once,
/// so none is taken either. Any other source is promoted into a Float32 working image of its size,
/// taken from the execution's memory resource and returned before this returns, and measured there; the
/// figures are the same every way. Thread-safe with respect to the source, which is only read. Runs on
/// the operation's admitted threads, and the answer does not depend on how many.
///
/// @param execution the operation policy and engine-context lease.
/// @param plan the presentation.
/// @param source what it reads.
/// @return the analysis; or a failure as `presentImage` describes it.
[[nodiscard]] AFTERNIGHTENGINE_API Result<DisplayPresentationAnalysis>
analyzeDisplayPresentation(const ExecutionContext& execution, const DisplayPresentationPlan& plan,
                           const DisplayPresentationSource& source);

/// Presents a source through the production plan.
///
/// Every sample is presented and, unless an analysis is supplied, measured: an eight- or sixteen-bit source
/// with no balance from its codes before it is promoted, as `analyzeDisplayPresentation` measures it, and
/// every other source in its working image. The destination is written whole on success; on a failure
/// after the checks, a stop included, it may be partly written. It may not overlap the source or its mask.
/// When the output is the working image and the destination is laid out as one, the steps run in it;
/// otherwise a Float32 working image of the source's size is taken from the execution's memory resource
/// for the call.
///
/// Thread-safe with respect to the source and the analysis, which are only read. Runs on the
/// operation's admitted threads, and the answer does not depend on how many.
///
/// @param execution the operation policy and engine-context lease.
/// @param plan the presentation.
/// @param source what it reads.
/// @param destination the first byte written, aligned for the output's representation.
/// @param destinationLayout where the output goes: the source's extent in the output's arrangement.
/// @param analysis a balance and statistics to present through instead of measuring, or null.
///        Borrowed for the call.
/// @return what was done; `ErrorCode::NotInitialized` for an empty execution;
///         `ErrorCode::InvalidArgument` for a null member of the source, a null or misaligned
///         destination, a layout that does not describe what it should, or a mask missing, unwanted
///         or overlapping the destination; `ErrorCode::IncompatibleArguments` for a schema other than
///         grey, RGB or RGBA, a floating source in a physical or sensor-code domain, a balance asked
///         of a single channel, a destination of another extent or arrangement, a destination
///         overlapping the source, a mask that does not cover the source, or an analysis taken under
///         another promotion, balance or channel count, by a preview, or without the statistics an
///         automatic stretch needs; `ErrorCode::EmptySelection` when a balance finds no sample inside
///         one of its intervals; `ErrorCode::InvalidData` when calibration finds a channel whose white
///         does not stand above its background; a memory-budget refusal; or the execution's
///         cancellation or backend-selection failure.
[[nodiscard]] AFTERNIGHTENGINE_API Result<DisplayPresentationResult>
presentImage(const ExecutionContext& execution, const DisplayPresentationPlan& plan,
             const DisplayPresentationSource& source, void* destination, const ImageLayout& destinationLayout,
             const DisplayPresentationAnalysis* analysis = nullptr);

/// Presents a source through the real-time preview plan.
///
/// As `presentImage`, over the subsampled samples alone: the destination has the extent
/// `realtimePreviewDisplayPresentationLayout` states, and an analysis this call measures comes from the
/// subsampled samples and says so. A production analysis is accepted. The result is display data: it
/// is not the production presentation of the same source, and it is not to be written back into an
/// image or a file.
///
/// @param execution the operation policy and engine-context lease.
/// @param plan the presentation.
/// @param source what it reads.
/// @param destination the first byte written, aligned for the output's representation.
/// @param destinationLayout where the output goes: the subsampled extent in the output's arrangement.
/// @param analysis a balance and statistics to present through instead of measuring, or null.
///        Borrowed for the call.
/// @return what was done; or a failure as `presentImage` describes it, except that a preview's
///         analysis is accepted.
[[nodiscard]] AFTERNIGHTENGINE_API Result<DisplayPresentationResult>
presentImageRealtimePreview(const ExecutionContext& execution, const RealtimePreviewDisplayPresentationPlan& plan,
                            const DisplayPresentationSource& source, void* destination,
                            const ImageLayout& destinationLayout,
                            const DisplayPresentationAnalysis* analysis = nullptr);

/// What `presentImage` does to the image it writes.
///
/// A new destination of another description - bytes or a working image - whose samples are display
/// data rather than what the source measured, so the meaning is restated, the source's header cards
/// and output decision no longer describe it, and its validity is regenerated: every byte is a value,
/// and the working image's invalid samples are not finite. The pixel grid is the source's, so an
/// astrometric solution still describes it.
inline constexpr OperationEffect kDisplayPresentationEffect{
    .description = DescriptionEffect::Restates,
    .semantics = SemanticEffect::Restates,
    .codecMetadata = CodecMetadataEffect::Drops,
    .outputIntent = OutputIntentEffect::Withdraws,
    .validity = {.policy = ValidityPolicy::Regenerate,
                 .produced = *ValidityDescriptor::create({.rule = ValidityRule::FiniteOnly})}};

/// What `presentImageRealtimePreview` does to the image it writes.
///
/// As `kDisplayPresentationEffect`, and a subsampled grid is not the source's, so an astrometric
/// solution no longer describes it.
inline constexpr OperationEffect kRealtimePreviewDisplayPresentationEffect{
    .description = DescriptionEffect::Restates,
    .semantics = SemanticEffect::Restates,
    .solution = SolutionEffect::Invalidates,
    .codecMetadata = CodecMetadataEffect::Drops,
    .outputIntent = OutputIntentEffect::Withdraws,
    .validity = {.policy = ValidityPolicy::Regenerate,
                 .produced = *ValidityDescriptor::create({.rule = ValidityRule::FiniteOnly})}};

} // namespace an
