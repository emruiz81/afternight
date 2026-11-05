// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A comet stacked twice and put back together: the stars sharp from one stack, the comet sharp from
/// another, blended where the comet is.
///
/// Frames stacked on their stars blur a comet, which moved between them; frames stacked on the comet
/// trail the stars. The composite keeps the sharp half of each. Every frame is split at the comet's
/// aperture into a star layer - the frame with the aperture filled by the local sky - and a comet
/// layer - that sky everywhere but inside the aperture, where the frame shows through. The star
/// layers are stacked on the stars' registration and the comet layers on the comet's, and the comet
/// stack is blended into the star stack through a feathered mask over every frame's projected
/// aperture.
///
/// Three operations make it, and the stack between them is the ordinary one:
///
/// - `splitCometAperture` writes one frame's two layers. The sky is the median of the frame's valid
///   samples whose pixel centres lie in an annulus around the aperture, from the larger of one and a
///   half radii and the radius plus one pixel, out to the larger of that plus two pixels and two and a
///   half radii; an annulus holding fewer than `kMinimumCometBackgroundSamples` takes the median of
///   the whole channel. A pixel's aperture weight is one within the radius, falls linearly to zero
///   across the feather beyond it, and is zero outside. Each sample's star layer is
///   `frame * (1 - weight) + sky * weight` and its comet layer `sky + (frame - star)`, in double.
/// - The layers are warped onto their planes and combined by `executeStack`, the star layers under the
///   stars' registration and the comet layers under the comet's - `executeCometRegistration`'s
///   transforms - as any other frames are.
/// - `renderCometMask` writes the mask: each frame's aperture, projected onto the star stack's plane by
///   `projectCometAperture` and made a circle by `cometApertureCircleOf`, with the same falling edge;
///   where apertures overlap, the larger weight.
/// - `blendCometComposite` writes the composite: where the mask is above zero, a position of the star
///   stack is mapped back through the reference frame's star registration and forward through its
///   comet registration, the comet stack is sampled there bilinearly, and the two are blended by the
///   mask.
///
/// Positions are engine pixel coordinates - the first pixel's centre is (0.5, 0.5) - and a stack
/// drizzled by a scale has its plane enlarged by it, so a position `p` of the drizzled plane is
/// `p / scale` on the registration plane.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Algorithms/Registration/RegistrationTransform.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace an
{

/// One operation's policy and its permission to run. Defined in `Runtime/ExecutionContext.h`.
class ExecutionContext;

/// What an image is. Defined in `Image/ImageDescriptor.h`.
class ImageDescriptor;

/// The two polygons a projected comet aperture produces. Defined in
/// `Algorithms/Registration/CometTracking.h`.
///
/// Named here rather than included: a caller turning a projection into a circle holds one, and has
/// therefore already included that header.
struct CometApertureProjection;

/// The most channels a comet composite reads.
inline constexpr std::uint32_t kMaxCometCompositeChannels = 4u;

/// The fewest valid samples an aperture's annulus needs before its median is the sky; below it the
/// whole channel's median is.
inline constexpr std::uint32_t kMinimumCometBackgroundSamples = 8u;

/// What a comet's aperture measurements say about its radius, each present or absent.
struct CometApertureMeasurement
{
    /// The aperture radius a caller or a tracker chose, in pixels.
    std::optional<double> apertureRadius{};

    /// The comet's measured diameter, in pixels.
    std::optional<double> measuredDiameter{};

    /// The comet's measured full width at half maximum, in pixels.
    std::optional<double> measuredFullWidth{};
};

/// The radius a comet's aperture is split and masked at, from what was measured of it.
///
/// The product's order: a chosen aperture radius, then half the measured diameter, then one and a
/// half full widths at half maximum but never less than two pixels. A figure that is absent, not
/// finite or not positive is skipped.
///
/// Thread-safe, reentrant and allocation-free.
///
/// @param measurement what was measured.
/// @return the radius, or nothing when no figure gives one.
[[nodiscard]] AFTERNIGHTENGINE_API std::optional<double>
resolveCometApertureRadius(const CometApertureMeasurement& measurement) noexcept;

/// Resolves every frame's aperture radius, giving a frame that has none the run's.
///
/// Each frame's radius is `resolveCometApertureRadius`'s; a frame for which that gives nothing takes
/// the run's radius, the upper median of the radii that were resolved - the product's.
///
/// Thread-safe and reentrant. Allocates nothing but a failure it describes; the ranking is done in
/// the destination span.
///
/// @param measurements one record per frame.
/// @param radii receives one radius per frame; exactly as long as `measurements`.
/// @return how many frames took the run's radius; `ErrorCode::IncompatibleArguments` when `radii` is
///         not as long as `measurements`; or `ErrorCode::EmptySelection` when no frame resolves a
///         radius.
[[nodiscard]] AFTERNIGHTENGINE_API Result<std::uint32_t>
resolveCometApertureRadii(std::span<const CometApertureMeasurement> measurements, std::span<double> radii);

/// A circle on an image's plane: where a comet's aperture lies and how large it is there.
struct CometApertureCircle
{
    /// The centre, in engine pixel coordinates.
    PixelCoordinate center{};

    /// The radius, in pixels.
    double radius = 0.0;
};

/// The circle a projected aperture describes on its output plane, enlarged by a drizzle's scale.
///
/// The centre is the projection's unclipped output centre and the radius the mean distance from it
/// to every output polygon point at a finite, positive distance - the source radius when there is no
/// such point - both multiplied by the scale.
///
/// Thread-safe, reentrant and allocation-free.
///
/// @param projection the projected aperture, from `projectCometAperture`.
/// @param sourceRadius the aperture's radius on its source frame, finite and positive.
/// @param scale the enlargement of the plane the circle is drawn on, finite and in [1, 4]; one for a
///        stack that was not drizzled.
/// @return the circle; or `ErrorCode::InvalidArgument` for a source radius or scale outside its range
///         or a projected centre that is not finite.
[[nodiscard]] AFTERNIGHTENGINE_API Result<CometApertureCircle>
cometApertureCircleOf(const CometApertureProjection& projection, double sourceRadius, double scale = 1.0);

/// One image a composite operation reads: a frame, a stacked layer or a mask.
///
/// Every member is borrowed for the call. The descriptor states the representation, the extent, the
/// channels and the numeric domain the samples are read in.
struct CometCompositeSource
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

/// Where one frame is split.
struct CometApertureSplitRequest
{
    /// The aperture on the frame's plane. Its centre is finite and its radius finite and positive.
    CometApertureCircle aperture{};

    /// How far beyond the radius the weight falls to zero, in pixels; empty for the product's larger
    /// of one pixel and thirty-five hundredths of the radius. Finite and not negative when stated;
    /// zero is a hard edge.
    std::optional<double> feather{};

    /// The representation both layers are written in: `SampleType::Float32` or
    /// `SampleType::Float64`.
    SampleType outputSampleType = SampleType::Float32;
};

/// A checked split.
///
/// Built only by `planCometApertureSplit`, so holding one means the request was accepted. Copyable; it
/// holds the request and the feather it resolved.
class CometApertureSplitPlan
{
public:
    /// What was asked for.
    ///
    /// @return the checked request.
    [[nodiscard]] const CometApertureSplitRequest& request() const noexcept
    {
        return m_request;
    }

    /// The feather the split uses: the request's, or the product's rule over the radius.
    ///
    /// @return the width in pixels, finite and not negative.
    [[nodiscard]] double feather() const noexcept
    {
        return m_feather;
    }

private:
    /// Records a checked request.
    ///
    /// @param request the request `planCometApertureSplit` accepted.
    /// @param feather the feather it resolved.
    CometApertureSplitPlan(const CometApertureSplitRequest& request, double feather) noexcept
        : m_request(request), m_feather(feather)
    {
    }

    /// The planner is the only constructor's caller.
    friend AFTERNIGHTENGINE_API Result<CometApertureSplitPlan>
    planCometApertureSplit(const CometApertureSplitRequest& request);

    /// The checked request.
    CometApertureSplitRequest m_request;

    /// The resolved feather.
    double m_feather;
};

/// Checks a split request and makes the plan.
///
/// Thread-safe and allocation-free apart from a failure it describes.
///
/// @param request the aperture, the feather and the layers' representation.
/// @return the plan; or `ErrorCode::InvalidArgument` for a centre that is not finite, a radius that is
///         not finite and positive, a feather that is negative or not finite, or a representation that
///         is not Float32 or Float64.
[[nodiscard]] AFTERNIGHTENGINE_API Result<CometApertureSplitPlan>
planCometApertureSplit(const CometApertureSplitRequest& request);

/// What a split writes, as a layout request for each layer.
///
/// One plane per channel of the request's representation, the frame's extent, each row starting on an
/// element boundary. The executor accepts any layout of that shape, padded or interleaved.
///
/// Thread-safe. Allocates only the failure it describes.
///
/// @param plan the split.
/// @param frame the frame's description.
/// @return the request to build each layer's layout from; or `ErrorCode::IncompatibleArguments` for a
///         frame of more than `kMaxCometCompositeChannels` channels.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ImageLayoutRequest>
cometApertureSplitLayout(const CometApertureSplitPlan& plan, const ImageDescriptor& frame);

/// What one split measured and wrote.
struct CometApertureSplitResult
{
    /// Each channel's sky, as a fraction of full scale for an integer frame; zero beyond the frame's
    /// channels.
    std::array<double, kMaxCometCompositeChannels> background{};

    /// How many valid samples each channel's annulus held.
    std::array<std::uint64_t, kMaxCometCompositeChannels> annulusSamples{};

    /// Whether each channel's sky is the whole channel's median, because its annulus held fewer than
    /// `kMinimumCometBackgroundSamples`.
    std::array<bool, kMaxCometCompositeChannels> wholeChannelBackground{};

    /// Pixels considered and pixels every sample of which was valid, and samples written to each layer.
    KernelRunResult run{};

    /// The engine-context bytes held during the call.
    std::uint64_t workingBytes = 0u;
};

/// Splits one frame at a comet's aperture into a star layer and a comet layer.
///
/// The frame is read as its stated numeric domain reads it - an integer sample as its fraction of
/// full scale - into the request's representation. The sky, the weight and both layers follow the
/// rules this header states; every sample is computed in double and written once. Either layer may be
/// left unwritten by passing a null pointer for it, which is how the star layer of one frame and the
/// comet layer of another are made without a layer nobody reads.
///
/// **Validity.** The layers state the frame's validity: a sample its rule rejects takes no part in the
/// sky and is written to both layers as a quiet NaN under a finite-only rule and as zero under a mask,
/// which the layers keep. A zero-as-no-data or range rule is refused, because a layer's arithmetic can
/// land on what such a rule rejects.
///
/// Thread-safe with respect to the frame, which is only read. Runs on the operation's admitted
/// threads, and the answer does not depend on how many. Takes working memory from the execution's
/// engine context. A stop may leave the layers partly written.
///
/// @param execution the operation policy and engine-context lease.
/// @param plan the split.
/// @param frame the frame: one to `kMaxCometCompositeChannels` channels.
/// @param starLayer the star layer's first byte, or null for none.
/// @param starLayout where the star layer goes: the request's representation and the frame's extent;
///        read only when `starLayer` is not null.
/// @param cometLayer the comet layer's first byte, or null for none.
/// @param cometLayout where the comet layer goes, likewise.
/// @return what was measured and written; `ErrorCode::NotInitialized` for an empty execution;
///         `ErrorCode::InvalidArgument` for a null member of the frame, a layout or pointer that does
///         not describe its descriptor, both layers null, a layer layout that does not describe the
///         request's representation, or a validity mask missing or unwanted;
///         `ErrorCode::IncompatibleArguments` for a frame of too many channels, a layer of another
///         extent, a layer overlapping the frame or the other layer, or a validity mask that does not
///         cover the frame; `ErrorCode::Unsupported` for a zero-as-no-data or range rule;
///         `ErrorCode::EmptySelection` for a channel with no valid sample to take the sky from;
///         `ErrorCode::InvalidData` for a sample that is not finite in a pixel the validity admits;
///         the working memory's refusal; or the execution's cancellation.
[[nodiscard]] AFTERNIGHTENGINE_API Result<CometApertureSplitResult>
splitCometAperture(const ExecutionContext& execution, const CometApertureSplitPlan& plan,
                   const CometCompositeSource& frame, void* starLayer, const ImageLayout* starLayout, void* cometLayer,
                   const ImageLayout* cometLayout);

/// What `splitCometAperture` does to the layers it writes.
///
/// Two new images whose representation is the request's and whose samples are neither the frame's nor
/// each other's, so the description and the meaning are restated; the pixel grid is the frame's, so
/// its astrometric solution still describes both. The validity is the frame's, kept.
inline constexpr OperationEffect kCometApertureSplitEffect{.description = DescriptionEffect::Restates,
                                                           .semantics = SemanticEffect::Restates,
                                                           .validity =
                                                               ValidityTransition{.policy = ValidityPolicy::Preserve}};

/// How a comet mask is drawn.
struct CometMaskRequest
{
    /// How far beyond each aperture's radius the weight falls to zero, in pixels; empty for the
    /// product's larger of two pixels and forty-five hundredths of the radius. Finite and not negative
    /// when stated.
    std::optional<double> feather{};
};

/// What one mask drew.
struct CometMaskResult
{
    /// How many apertures were drawn.
    std::uint32_t apertures = 0u;

    /// How many of the mask's pixels some aperture weighs above zero.
    std::uint64_t coveredPixels = 0u;
};

/// Draws a comet mask: every aperture's weight, the largest where they overlap, zero elsewhere.
///
/// Each aperture's weight follows the rule this header states, with the request's feather. The mask is
/// one Float32 channel, every pixel written.
///
/// Thread-safe with respect to the apertures, which are only read. Runs on the operation's admitted
/// threads, and the answer does not depend on how many. A stop may leave the mask partly written.
///
/// @param execution the operation policy and engine-context lease.
/// @param apertures the circles, on the mask's plane; none draws a mask of zeros.
/// @param request the feather.
/// @param destination the mask's first byte.
/// @param destinationLayout where the mask goes: one channel of four-byte elements.
/// @return what was drawn; `ErrorCode::NotInitialized` for an empty execution;
///         `ErrorCode::InvalidArgument` for a null destination, a layout that is not one channel of
///         four-byte elements or a misaligned destination, an aperture whose centre is not finite or
///         whose radius is not finite and positive, or a feather that is negative or not finite; or the
///         execution's cancellation.
[[nodiscard]] AFTERNIGHTENGINE_API Result<CometMaskResult>
renderCometMask(const ExecutionContext& execution, std::span<const CometApertureCircle> apertures,
                const CometMaskRequest& request, void* destination, const ImageLayout& destinationLayout);

/// How a comet stack is blended into a star stack.
///
/// The two registrations are the reference frame's: where its source positions land on the star
/// stack's plane and on the comet stack's, each before a drizzle's scale and each with the post
/// translation its plane's canvas adds.
struct CometCompositeBlendRequest
{
    /// The reference frame's star registration.
    RegistrationTransform starTransform{};

    /// The offset the star stack's canvas adds after the star registration.
    PixelCoordinate starPostTranslation{};

    /// The reference frame's comet registration.
    RegistrationTransform cometTransform{};

    /// The offset the comet stack's canvas adds after the comet registration.
    PixelCoordinate cometPostTranslation{};

    /// The enlargement both stacks were drizzled by, finite and in [1, 4]; one when neither was.
    double scale = 1.0;
};

/// A checked blend.
///
/// Built only by `planCometCompositeBlend`, so holding one means the request was accepted. Copyable; it
/// holds the request and nothing else.
class CometCompositeBlendPlan
{
public:
    /// What was asked for.
    ///
    /// @return the checked request.
    [[nodiscard]] const CometCompositeBlendRequest& request() const noexcept
    {
        return m_request;
    }

private:
    /// Records a checked request.
    ///
    /// @param request the request `planCometCompositeBlend` accepted.
    explicit CometCompositeBlendPlan(const CometCompositeBlendRequest& request) noexcept : m_request(request) {}

    /// The planner is the only constructor's caller.
    friend AFTERNIGHTENGINE_API Result<CometCompositeBlendPlan>
    planCometCompositeBlend(const CometCompositeBlendRequest& request);

    /// The checked request.
    CometCompositeBlendRequest m_request;
};

/// Checks a blend request and makes the plan.
///
/// Thread-safe and allocation-free apart from a failure it describes.
///
/// @param request the reference frame's two registrations, their post translations and the scale.
/// @return the plan; or `ErrorCode::InvalidArgument` for a post translation that is not finite or a
///         scale outside its range.
[[nodiscard]] AFTERNIGHTENGINE_API Result<CometCompositeBlendPlan>
planCometCompositeBlend(const CometCompositeBlendRequest& request);

/// What one blend did.
struct CometCompositeBlendResult
{
    /// Pixels considered, pixels the mask weighed above zero and every read sample of which was valid,
    /// and samples written - every sample of the composite.
    KernelRunResult run{};

    /// How many weighed pixels were blended.
    std::uint64_t blendedPixels = 0u;

    /// How many weighed pixels kept the star stack's samples because a comet sample their bilinear
    /// read needed was invalid.
    std::uint64_t keptPixels = 0u;

    /// How many weighed pixels sampled the comet stack at their own position, because the registrations
    /// did not map them - the product's fallback.
    std::uint64_t unmappedPixels = 0u;
};

/// Blends a comet stack into a star stack through a mask.
///
/// Where the mask weighs a pixel above zero - its value, as a fraction of full scale, held to the unit
/// interval - the pixel's centre `p` on the star stack's plane is taken to `p / scale`, back through the
/// star registration and its post translation to the reference frame's source plane, forward through
/// the comet registration and its post translation, and times the scale onto the comet stack's plane.
/// The comet stack is read there bilinearly between the four nearest pixel centres - zero outside the
/// rectangle those centres span - and the composite is `star * (1 - weight) + comet * weight`, in
/// double. Elsewhere it is the star stack's sample.
///
/// **Validity is per pixel.** The composite states the star stack's validity: a weighed pixel any star
/// sample of which it rejects is copied whole. A pixel whose bilinear read needs a comet sample the
/// comet stack's validity rejects keeps the star stack's samples, and a mask sample its validity
/// rejects weighs nothing.
///
/// Thread-safe with respect to every source, which is only read. Runs on the operation's admitted
/// threads, and the answer does not depend on how many. A stop may leave the destination partly
/// written.
///
/// @param execution the operation policy and engine-context lease.
/// @param plan the blend.
/// @param star the star stack: Float32 or Float64, one to `kMaxCometCompositeChannels` channels.
/// @param comet the comet stack: Float32 or Float64, the star stack's channels, any width and height.
/// @param mask the mask: Float32 or Float64, one channel, the star stack's width and height.
/// @param destination the first byte written.
/// @param destinationLayout where the composite goes: the star stack's representation and extent.
/// @return what was done; `ErrorCode::NotInitialized` for an empty execution;
///         `ErrorCode::InvalidArgument` for a null member of a source, a layout or pointer that does not
///         describe its descriptor, a null destination, a destination layout that does not describe the
///         star stack's representation, or a validity mask missing, unwanted or overlapping the
///         destination; `ErrorCode::IncompatibleArguments` for a stack or mask that is not floating,
///         too many channels, a comet stack of other channels, a mask of another width or height or of
///         more than one channel, a destination of another extent or overlapping a source, or a
///         validity mask that does not cover its source; `ErrorCode::InvalidData` for a sample the
///         blend computes with that is not finite in a pixel the validity admits; or the execution's
///         cancellation.
[[nodiscard]] AFTERNIGHTENGINE_API Result<CometCompositeBlendResult>
blendCometComposite(const ExecutionContext& execution, const CometCompositeBlendPlan& plan,
                    const CometCompositeSource& star, const CometCompositeSource& comet,
                    const CometCompositeSource& mask, void* destination, const ImageLayout& destinationLayout);

/// What `blendCometComposite` does to the image it writes.
///
/// The star stack's representation, extent and pixel grid, so the description and the astrometric
/// solution stand; the samples are a mixture of two stacks, so the meaning is restated. The validity is
/// the star stack's, kept.
inline constexpr OperationEffect kCometCompositeBlendEffect{
    .semantics = SemanticEffect::Restates, .validity = ValidityTransition{.policy = ValidityPolicy::Preserve}};

} // namespace an
