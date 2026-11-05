// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A starless image and its stars put back together, optionally through a mask.
///
/// Star removal leaves two images: the background without its stars, and the stars alone. After each has been
/// processed on its own, this operation recombines them: the star image, its edges optionally tightened, is
/// added to the starless one, and where a mask is given the sum is blended over the starless image by it, so a
/// mask of one shows the recombined pixel and a mask of zero the starless one.
///
/// **What is added.** Both images are read in their stated numeric domain, an integer as its fraction of full
/// scale and a floating sample as it is, and they must state the same one. In a normalized domain the sum is held
/// at full scale, one; in a physical domain it is not held at all, so recombining an image whose samples exceed
/// one loses nothing. A sensor-code image is refused, since adding two raw readings is not what a recombination
/// means.
///
/// **The result's representation.** The caller states it, or leaves it to the higher of the two images'
/// representations in a fixed order - a narrower before a wider, a signed before an unsigned of the same width, an
/// integer before a floating one. Every sample is computed in double and written once, rounded to nearest with ties
/// to even and saturating.
///
/// **The mask** is one channel, which weighs every channel, or one per channel of the images; it is read as a
/// fraction of full scale, held to the unit interval, and inverted on request.
///
/// **Validity is per pixel.** A pixel any read sample of which a source's validity rejects - the starless
/// image's, the stars' or the mask's - is written as the starless image's samples, a sample the starless image's own
/// validity rejects as zero, and counted.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <cstdint>
#include <optional>

namespace an
{

/// One operation's policy and its permission to run. Defined in `Runtime/ExecutionContext.h`.
class ExecutionContext;

/// What an image is. Defined in `Image/ImageDescriptor.h`.
class ImageDescriptor;

/// Everything a recombination does, stated once.
///
/// A plain aggregate whose defaults add the stars as they are, unmasked, in the higher of the two representations.
struct StarMaskRecombinationRequest
{
    /// How strongly the star image's edges are tightened before it is added: a weighted four-direction Sobel
    /// magnitude subtracted from each sample. Zero leaves the stars as they are; finite and not negative.
    double edgeTightening = 0.0;

    /// Whether the mask is read inverted: each value, once held to the unit interval, as one minus itself.
    bool invertMask = false;

    /// The representation the result is written in; empty for the higher of the two images' representations.
    std::optional<SampleType> outputSampleType{};
};

/// One image a recombination reads: the starless image, the stars or the mask.
///
/// Every member is borrowed for the call. The descriptor states the representation, the extent, the channels and
/// the numeric domain the samples are read in.
struct StarMaskRecombinationSource
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

/// What one recombination did.
struct StarMaskRecombinationResult
{
    /// Pixels considered and pixels every read sample of which was valid, and samples written - every sample of the
    /// result, an invalid pixel as the starless image's.
    KernelRunResult run{};
};

/// A checked recombination.
///
/// Built only by `planStarMaskRecombination`, so holding one means the request was accepted. Copyable; it holds
/// the request and nothing else.
class StarMaskRecombinationPlan
{
public:
    /// What was asked for.
    ///
    /// @return the checked request.
    [[nodiscard]] const StarMaskRecombinationRequest& request() const noexcept
    {
        return m_request;
    }

private:
    /// Records a checked request.
    ///
    /// @param request the request `planStarMaskRecombination` accepted.
    explicit StarMaskRecombinationPlan(const StarMaskRecombinationRequest& request) noexcept : m_request(request) {}

    /// The planner is the only constructor's caller.
    friend AFTERNIGHTENGINE_API Result<StarMaskRecombinationPlan>
    planStarMaskRecombination(const StarMaskRecombinationRequest& request);

    /// The checked request.
    StarMaskRecombinationRequest m_request;
};

/// Checks a request and makes the plan.
///
/// Thread-safe and allocation-free apart from a failure it describes.
///
/// @param request the edge tightening, the mask's inversion and the result's representation.
/// @return the plan; or `ErrorCode::InvalidArgument` for an edge tightening that is negative or not finite, or a
///         representation outside its enumeration.
[[nodiscard]] AFTERNIGHTENGINE_API Result<StarMaskRecombinationPlan>
planStarMaskRecombination(const StarMaskRecombinationRequest& request);

/// The representation a recombination of two images writes.
///
/// The request's, when it states one; otherwise the higher of the two in this order: 8-bit signed,
/// 8-bit unsigned, 16-bit signed, 16-bit unsigned, 32-bit signed, 32-bit unsigned, Float32, Float64.
///
/// Thread-safe and allocation-free.
///
/// @param plan the recombination.
/// @param starless the starless image's description.
/// @param stars the star image's description.
/// @return the representation.
[[nodiscard]] AFTERNIGHTENGINE_API SampleType starMaskRecombinationSampleType(const StarMaskRecombinationPlan& plan,
                                                                              const ImageDescriptor& starless,
                                                                              const ImageDescriptor& stars) noexcept;

/// What a recombination writes, as a layout request.
///
/// One plane per channel of `starMaskRecombinationSampleType`, the starless image's extent, each row starting on
/// an element boundary. The executor accepts any layout of that shape, padded or interleaved.
///
/// Thread-safe. Allocates only the failure it describes.
///
/// @param plan the recombination.
/// @param starless the starless image's description.
/// @param stars the star image's description.
/// @return the request to build the destination's layout from; or `ErrorCode::IncompatibleArguments` for images
///         that are not one or three channels, or of different extents.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ImageLayoutRequest>
starMaskRecombinationLayout(const StarMaskRecombinationPlan& plan, const ImageDescriptor& starless,
                            const ImageDescriptor& stars);

/// Recombines a starless image and its stars.
///
/// The stars are read, their edges tightened when the request asks, added to the starless image and blended over
/// it by the mask when one is given; every sample is computed in double and written once.
///
/// Thread-safe with respect to every source, which is only read. Runs on the operation's admitted threads, and the
/// answer does not depend on how many. Takes working memory from the execution's engine context. A stop may leave
/// the destination partly written.
///
/// @param execution the operation policy and engine-context lease.
/// @param plan the recombination.
/// @param starless the starless image: one or three channels.
/// @param stars the star image: the starless image's extent and channels and numeric domain.
/// @param mask the mask, one channel or the images' channel count, the images' width and height, in a normalized
///        domain; null for none.
/// @param destination the first byte written.
/// @param destinationLayout where the result goes: `starMaskRecombinationSampleType`'s element size and the
///        starless image's extent.
/// @return what was done; `ErrorCode::NotInitialized` for an empty execution; `ErrorCode::InvalidArgument` for a
///         null member of a source, a layout or pointer that does not describe its descriptor, a null destination,
///         a destination layout that does not describe the result's representation, or a validity mask missing,
///         unwanted or overlapping the destination; `ErrorCode::IncompatibleArguments` for images that are not one
///         or three channels, of different extents, channels or numeric domains or in a sensor-code domain, a mask
///         of another width, height or channel count or not in a normalized domain, a destination of another
///         extent or overlapping a source, or a validity mask that does not cover its source;
///         `ErrorCode::InvalidData` for a sample that is not finite in a pixel the validity admits; the working
///         memory's refusal; the edge tightening's failure; or the execution's cancellation or backend-selection
///         failure.
[[nodiscard]] AFTERNIGHTENGINE_API Result<StarMaskRecombinationResult>
recombineStarMask(const ExecutionContext& execution, const StarMaskRecombinationPlan& plan,
                  const StarMaskRecombinationSource& starless, const StarMaskRecombinationSource& stars,
                  const StarMaskRecombinationSource* mask, void* destination, const ImageLayout& destinationLayout);

/// What `recombineStarMask` does to the image it writes.
///
/// A new image whose representation may differ from either source's and whose samples are neither image's, so the
/// description and the meaning are restated; the pixel grid is the starless image's, so its astrometric solution
/// still describes the result. The validity combines every source's claims, since a pixel any of them rejects is
/// not recombined.
inline constexpr OperationEffect kStarMaskRecombinationEffect{
    .description = DescriptionEffect::Restates,
    .semantics = SemanticEffect::Restates,
    .validity = ValidityTransition{.policy = ValidityPolicy::Combine}};

} // namespace an
