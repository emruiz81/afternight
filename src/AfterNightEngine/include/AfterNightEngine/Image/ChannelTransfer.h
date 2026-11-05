// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// One channel of an image reached on its own, and separate channels joined
/// into one image.
///
/// `Image/ImageTransfer.h` next door moves a whole picture and `Image/ImageConversion.h` changes what
/// its samples are. This owns the third family that moves samples without computing anything: taking
/// one channel out of an image, and building a multi-channel image out of images that each hold one.
/// A codec writing one monochrome file per filter does the first; a colour composition of three
/// registered monochrome frames does the second.
///
/// **A channel is addressed, never assumed to be a plane.** Which plane a channel lives in and where
/// inside that plane it starts are arithmetic over the layout's samples per pixel, so the same call
/// reaches channel one of a planar image, of an interleaved one, and of a luminance plane beside an
/// interleaved chroma plane. An operation that only worked on planar images would push every
/// interleaved caller into repacking a whole picture to read a third of it.
///
/// **A compose is what its destination layout says it is, and nothing here re-describes it.** Three
/// monochrome sources joined into a one-plane destination are interleaved and joined into a
/// three-plane destination are planar; `Image/StorageArrangement.h` is what names the result, from
/// the layout the caller supplied and never from a claim made here. That matters because the failure
/// this prevents is not a wrong pixel: it is a buffer whose samples are in one arrangement being
/// described everywhere downstream as the other, which nothing detects until a reader looks at the
/// bytes.
///
/// **Nothing here reads a sample's meaning.** These operations have no numeric domain, no validity
/// rule and no accuracy tolerance, for the reason a copy has none - every sample is reproduced
/// exactly, not-a-number patterns included, because none of them is put through an arithmetic unit.
/// What a channel *means* belongs to `Image/ChannelSemantics.h` and travels with the descriptor;
/// moving samples does not consult it and cannot change it.
///
/// **A destination may not alias a source.** Overlapping buffers would make what a sample ends up as
/// depend on the order the rows happened to be scheduled in, so it is refused rather than being
/// correct on one thread and wrong on eight. Two *sources* may overlap freely, including two that are
/// the same image: broadcasting one monochrome frame into every channel of a colour image is a
/// composition a caller actually asks for.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ImageView.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <cstdint>
#include <span>

namespace an
{

/// One operation's policy and its permission to run. Defined in `Runtime/ExecutionContext.h`.
///
/// Named here rather than included, for the reason `Image/ImageTransfer.h` names it: the image model
/// sits above the runtime, and a header that moves an image should not put the runtime's whole
/// include closure into every translation unit that reads it.
class ExecutionContext;

/// One single-channel image bound as a source of a composition.
///
/// A pair rather than an `ImageView` because the entry point that takes it is the untyped one, and
/// the callers that need it most - a codec assembling a frame it has just decoded plane by plane -
/// know their representation only at run time. The typed spelling below builds these from views.
///
/// Both members are borrowed for the duration of one call. The layout in particular is held by
/// address rather than by value, exactly as a bound pointwise source is, because a layout has no
/// default state to fill a struct with; a caller therefore keeps whatever owns it alive across the
/// call.
struct ChannelSource
{
    /// First byte of this channel's samples, borrowed for the call. Must not be null.
    const void* samples = nullptr;

    /// The validated geometry and strides of `samples`, borrowed for the call. Must not be null,
    /// and must describe a one-channel image of the destination's extent and representation.
    const ImageLayout* layout = nullptr;
};

/// Copies one channel of an image into a single-channel image of its own.
///
/// The untyped entry point, and the one the exported library carries: a caller that knows its
/// representation only at run time reaches the same code as one that knows it at compile time.
///
/// The source may hold its channels any way a layout can describe - one plane per channel, one plane
/// holding every channel, or a mixture - and the channel is addressed through that arrangement
/// rather than through an assumption about it. The destination holds exactly one channel of the same
/// width and height and the same representation. Strides may differ freely on both sides.
///
/// Thread-safe with respect to `execution` and the source, both of which are only read. Runs on the
/// threads the operation was admitted. Cancellation may leave part of the destination written: this
/// operation is not failure-atomic.
///
/// @param execution the operation this extraction belongs to. Must hold something.
/// @param sampleType what the samples are, on both sides.
/// @param source where the source image's samples start. Must not be null.
/// @param sourceLayout where the source samples sit. Its element size must be that
///        representation's.
/// @param channelIndex which channel to take, below the source's channel count. Channels are
///        counted across the whole image in plane-major order, so channel one of a three-plane
///        planar image is plane one, and channel one of an interleaved image is the second sample
///        of every pixel.
/// @param destination where the channel's samples go. Must not be null, and must not overlap the
///        source.
/// @param destinationLayout where they sit. Its extent must name exactly one channel.
/// @return success; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::UnsupportedBackend` when the operation is constrained to
///         `ComputeBackend::Gpu`, which has no dispatch table for this walk;
///         `ErrorCode::InvalidArgument` for a null buffer, a representation this build does not
///         know, a layout whose element size is not that representation's, or a channel index the
///         source does not have; `ErrorCode::IncompatibleArguments` when the destination is not a
///         one-channel image of the source's width and height, or when the two overlap; or
///         `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` when the operation was stopped.
[[nodiscard]] AFTERNIGHTENGINE_API Status extractImageChannel(const ExecutionContext& execution, SampleType sampleType,
                                                              const void* source, const ImageLayout& sourceLayout,
                                                              std::uint32_t channelIndex, void* destination,
                                                              const ImageLayout& destinationLayout);

/// Joins one single-channel image per channel into one multi-channel image.
///
/// The untyped entry point. Sources are bound in channel order: source zero becomes channel zero of
/// the destination, and there are exactly as many of them as the destination has channels. Each
/// holds one channel of the destination's width, height and representation, with strides of its
/// own.
///
/// The destination's arrangement is read from its layout and is not restricted: one plane per
/// channel, one plane holding every channel, and a mixture of the two are all built by this call,
/// and which one was built is what `Image/StorageArrangement.h` reports about the layout that was
/// supplied. A destination whose channels are adjacent is written a pixel at a time rather than a
/// channel at a time where the arrangement admits it, which is one pass over the destination instead
/// of one per channel; that is a performance property and changes no sample.
///
/// Thread-safe with respect to `execution` and the sources, all of which are only read. Runs on the
/// threads the operation was admitted. Cancellation may leave part of the destination written: this
/// operation is not failure-atomic.
///
/// @param execution the operation this composition belongs to. Must hold something.
/// @param sampleType what the samples are, in every source and in the destination.
/// @param sources one single-channel image per destination channel, in channel order. Sources may
///        overlap each other, including being the same image twice; none may overlap the
///        destination.
/// @param destination where the joined samples go. Must not be null.
/// @param destinationLayout where they sit. Its channel count is how many sources are expected.
/// @return success; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::UnsupportedBackend` when the operation is constrained to
///         `ComputeBackend::Gpu`, which has no dispatch table for this walk;
///         `ErrorCode::InvalidArgument` for a null buffer, a null source member, a representation
///         this build does not know, or a layout whose element size is not that representation's;
///         `ErrorCode::IncompatibleArguments` when the source count is not the destination's
///         channel count, when a source is not a one-channel image of the destination's width and
///         height, or when a source overlaps the destination; or `ErrorCode::Cancelled` or
///         `ErrorCode::DeadlineExceeded` when the operation was stopped.
[[nodiscard]] AFTERNIGHTENGINE_API Status composeImageChannels(const ExecutionContext& execution, SampleType sampleType,
                                                               std::span<const ChannelSource> sources,
                                                               void* destination, const ImageLayout& destinationLayout);

/// Copies one channel of an image view into a single-channel image view.
///
/// The typed spelling of `extractImageChannel`, for a caller that already holds checked views. It
/// adds no rule of its own: the representation and the two layouts are read off the views, and
/// everything else is the untyped entry point's.
///
/// @tparam SampleT the representation, which does not change.
/// @param execution the operation this extraction belongs to.
/// @param source the image to take a channel of.
/// @param channelIndex which channel to take.
/// @param destination the one-channel image it goes into.
/// @return whatever `extractImageChannel` reported.
template <ResidentSample SampleT>
[[nodiscard]] Status extractChannel(const ExecutionContext& execution, const ImageView<const SampleT>& source,
                                    std::uint32_t channelIndex, const ImageView<SampleT>& destination)
{
    return extractImageChannel(execution, kSampleTypeOf<SampleT>, source.data(), source.layout(), channelIndex,
                               destination.data(), destination.layout());
}

/// Joins one single-channel source per channel into one multi-channel image view.
///
/// The typed spelling of `composeImageChannels`. The sources stay untyped pairs because they are
/// what a caller assembles from several images that need not have one common owner; what this adds
/// is that the destination's representation is read off its view rather than restated.
///
/// @tparam SampleT the representation, which does not change.
/// @param execution the operation this composition belongs to.
/// @param sources one single-channel image per destination channel, in channel order.
/// @param destination the multi-channel image they are joined into.
/// @return whatever `composeImageChannels` reported.
template <ResidentSample SampleT>
[[nodiscard]] Status composeChannels(const ExecutionContext& execution, std::span<const ChannelSource> sources,
                                     const ImageView<SampleT>& destination)
{
    return composeImageChannels(execution, kSampleTypeOf<SampleT>, sources, destination.data(), destination.layout());
}

/// What `extractImageChannel` and `extractChannel` do to the image they write.
///
/// One channel of a picture is not that picture. The result's channel schema, and with it its colour
/// basis, is the operation's to state rather than the source's to keep, so a caller takes the
/// destination's description from what it asked for. The samples themselves are unchanged, so what
/// they mean and any astrometric solution both survive; the preserved source cards and any decision
/// about how the image is to be written do not, because each described a frame with more channels
/// than this one has.
inline constexpr OperationEffect kChannelExtractEffect{.description = DescriptionEffect::Restates,
                                                       .codecMetadata = CodecMetadataEffect::Drops,
                                                       .outputIntent = OutputIntentEffect::Withdraws};

/// What `composeImageChannels` and `composeChannels` do to the image they write.
///
/// The extraction's mirror, with one difference that is not cosmetic: there are several inputs, so
/// the result claims what all of them claim together rather than what any one of them claimed. Two
/// sources whose validity rules disagree combine to `ValidityRule::ExplicitMask`, the only
/// description that can carry the union sample by sample. That resolved claim travels inside the
/// result's `ImageSemantics`, which is why this declaration also restates the meaning: a composed
/// image that kept one source's semantics would be claiming that source's validity for all of them.
inline constexpr OperationEffect kChannelComposeEffect{.description = DescriptionEffect::Restates,
                                                       .semantics = SemanticEffect::Restates,
                                                       .codecMetadata = CodecMetadataEffect::Drops,
                                                       .outputIntent = OutputIntentEffect::Withdraws,
                                                       .validity =
                                                           ValidityTransition{.policy = ValidityPolicy::Combine}};
} // namespace an
