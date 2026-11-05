// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - validation, working planes and the gather/scatter shared by every
// neighbourhood filter.
//
// The spatial filters - convolution, Gaussian, order and general filters - and the families built
// the same way, among them the multiscale transforms, noise reduction, deconvolution and background
// modelling, read an image, work on one channel at a time as a Float32 plane, and write the answer
// back. Everything about that except the arithmetic itself is the same, and writing it once is what
// stops those operations disagreeing about which samples mean something, what a promise refuses, or
// when a copy can be skipped.
//
// **A Float32 channel that is a plane of its own is not copied.** That is the whole of the fast
// path: `PlaneView` needs a base pointer and a row stride, and a planar Float32 image already has
// both, so the filter reads and writes the caller's storage. Every other combination - an
// interleaved image, a mixed arrangement, or any of the seven other representations - is gathered
// into a working plane taken from the engine context and scattered back afterwards. The two are the
// same code path with a different plane, so a picture cannot come out differently because of how it
// happened to be stored.
//
// **The working numbers are the stored numbers.** The gather widens; it does not rescale. The
// scatter converts under the caller's policy with `DomainMapping::CodeValue`, so the round trip
// through the working plane is the identity for every value the destination can hold.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Algorithms/Spatial/SpatialFilter.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/PlaneView.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "kernels/reference/PlaneSourceAccess.h"

#include <cstdint>
#include <optional>
#include <utility>

/// Validation, working planes and the per-channel gather and scatter shared by the neighbourhood
/// filters and the families that read and write an image one Float32 channel plane at a time.
namespace an::spatial_detail
{

/// The picture two layouts have to agree about before a filter runs.
struct SpatialGeometry
{
    /// How many pixels one row holds.
    std::uint32_t width = 0u;

    /// How many rows the picture holds.
    std::uint32_t height = 0u;

    /// How many channels each side carries.
    std::uint32_t channelCount = 0u;

    /// How many bytes one working plane of this picture needs.
    std::uint64_t planeBytes = 0u;
};

/// One failure from this family.
///
/// @param code which failure.
/// @param operation the entry point the caller is inside.
/// @param message what a person reads.
/// @return the failure.
[[nodiscard]] Status spatialFailure(ErrorCode code, const char* operation, const char* message);

/// One failure carrying what was asked for and what was allowed.
///
/// @param code which failure.
/// @param operation the entry point the caller is inside.
/// @param message what a person reads.
/// @param requested the number the caller supplied.
/// @param limit the number the contract allows.
/// @return the failure, carrying both counts.
[[nodiscard]] Status spatialLimitFailure(ErrorCode code, const char* operation, const char* message,
                                         std::uint64_t requested, std::uint64_t limit);

/// Whether an execution may run and one image is readable in a representation this family works in.
///
/// The half of the shared precondition that is about a single image: the execution and its
/// cancellation state, the representation and the layout, the promise the output policy makes, and
/// whether the selection is coherent. It exists separately because not every operation in these
/// families has two images - a multiscale decomposition reads one and produces layers, and a
/// reconstruction writes one and reads layers - and a second spelling of these rules would be free
/// to drift from this one.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType the representation the image claims.
/// @param samples where the samples start.
/// @param layout where they sit.
/// @param output what the result promises, and how a value becomes a sample.
/// @param selection which samples mean something.
/// @param operation the name carried by any failure.
/// @param geometry the picture, written on success.
/// @return success, or the first reason the call cannot proceed, in the order checked:
///         `ErrorCode::NotInitialized` for an empty execution; `ErrorCode::Cancelled` or
///         `ErrorCode::DeadlineExceeded` for a stopped one; `ErrorCode::InvalidArgument` for an
///         unknown representation, null samples, a layout whose element size is not the
///         representation's, or an unknown output contract; `ErrorCode::IncompatibleArguments` when
///         Float32 arithmetic cannot keep the promise the storage and contract make; the
///         `ErrorCode::InvalidArgument` `checkConversionPolicy` reports for a malformed write-back
///         policy; `ErrorCode::InvalidArgument` for a write-back that is not
///         `DomainMapping::CodeValue`, for the explicit-mask rule without a mask, or for a mask
///         given to a rule that reads none; and `ErrorCode::IncompatibleArguments` for a mask that
///         does not cover the image.
[[nodiscard]] Status checkSpatialImage(const ExecutionContext& execution, SampleType sampleType, const void* samples,
                                       const ImageLayout& layout, const SpatialOutputPolicy& output,
                                       const SpatialSelection& selection, const char* operation,
                                       SpatialGeometry& geometry);

/// Replaces every sample a validity rule rejects with a stated substitute, in place.
///
/// What a linear transform over a plane needs and a filter does not: a filter can leave a rejected
/// centre alone because its destination sample already exists, while a transform that decomposes a
/// plane into layers has no such sample to leave alone. Resolving the question once, at the door,
/// is what lets everything after it be arithmetic.
///
/// @param execution the operation policy and engine-context lease.
/// @param plane the working plane to repair.
/// @param validity the rule and optional mask, with the channel's mask plane already chosen.
/// @param substitute what stands in for a rejected sample.
/// @param substituted how many samples were replaced, added to whatever it already holds, and only
///        on success.
/// @param operation the caller's entry point. The only failures are the scheduler's, which do not
///        carry it.
/// @return success, or the failure `parallelFor` reports: `ErrorCode::NotInitialized`,
///         `ErrorCode::Cancelled`, `ErrorCode::DeadlineExceeded`, `ErrorCode::AllocationFailed` or
///         `ErrorCode::ExternalFailure`.
[[nodiscard]] Status substituteRejectedSamples(const ExecutionContext& execution, PlaneView<float> plane,
                                               const KernelValidityView& validity, float substitute,
                                               std::uint64_t& substituted, const char* operation);

/// Whether an execution may run and two images describe the same picture in a representation this
/// family can filter.
///
/// The whole of the shared precondition, in the order the family promises: the execution and its
/// cancellation state, then the representation and the two layouts, then the promise the output
/// policy makes, then whether the two buffers overlap.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType the representation both sides claim.
/// @param source where the source samples start.
/// @param sourceLayout where they sit.
/// @param destination where the filtered samples go.
/// @param destinationLayout where they sit.
/// @param output what the result promises, and how a value becomes a sample.
/// @param selection which samples mean something.
/// @param operation the name carried by any failure.
/// @param geometry the agreed picture, written on success.
/// @return success, or the first reason the call cannot proceed: any failure of
///         `checkSpatialImage` for the source; then `ErrorCode::InvalidArgument` for a null
///         destination or a destination layout whose element size is not the representation's; and
///         `ErrorCode::IncompatibleArguments` when the two are not the same picture or their
///         storage shares a byte.
[[nodiscard]] Status checkSpatialImages(const ExecutionContext& execution, SampleType sampleType, const void* source,
                                        const ImageLayout& sourceLayout, const void* destination,
                                        const ImageLayout& destinationLayout, const SpatialOutputPolicy& output,
                                        const SpatialSelection& selection, const char* operation,
                                        SpatialGeometry& geometry);

/// A Float32 plane a filter reads or writes, either the caller's storage or working memory.
///
/// It owns nothing when it borrows, so the two cases cost the same to pass around and the caller
/// asks `owned()` rather than tracking which it built.
class SpatialPlaneBuffer
{
public:
    /// A buffer holding nothing.
    SpatialPlaneBuffer() noexcept = default;

    /// Takes one working plane from the execution's engine context.
    ///
    /// @param execution the operation whose context owns the memory.
    /// @param geometry the picture the plane covers.
    /// @param operation the name carried by any failure.
    /// @return the buffer; any refusal `plane_detail::ScopedContextMemory::take` reports, of which
    ///         `ErrorCode::MemoryBudgetExceeded` carries the requested, available and limit
    ///         bytes; or `ErrorCode::InvalidArgument` when the geometry does not describe a plane
    ///         one view can address.
    [[nodiscard]] static Result<SpatialPlaneBuffer> own(const ExecutionContext& execution,
                                                        const SpatialGeometry& geometry, const char* operation);

    /// Whether this buffer holds working memory rather than borrowing the caller's.
    ///
    /// @return true when the plane is working memory.
    [[nodiscard]] bool owned() const noexcept
    {
        return m_memory.bytes() != 0u;
    }

    /// How many bytes of working memory this buffer holds.
    ///
    /// @return the byte count, which is zero for a borrowed plane.
    [[nodiscard]] std::uint64_t bytes() const noexcept
    {
        return m_memory.bytes();
    }

    /// The writable plane.
    ///
    /// @return the view, valid until this buffer is destroyed or moved from. A buffer holding
    ///         nothing has none.
    [[nodiscard]] PlaneView<float> view() const noexcept
    {
        return *m_view;
    }

    /// The read-only plane.
    ///
    /// @return the view, valid until this buffer is destroyed or moved from.
    [[nodiscard]] PlaneView<const float> constView() const noexcept
    {
        return *m_view;
    }

private:
    /// Builds a buffer over one plane.
    ///
    /// @param memory the working memory, or an owner of nothing for a borrowed plane.
    /// @param view the plane itself.
    SpatialPlaneBuffer(plane_detail::ScopedContextMemory&& memory, PlaneView<float> view) noexcept
        : m_memory(std::move(memory)), m_view(view)
    {
    }

    /// The working memory, or an owner of nothing.
    plane_detail::ScopedContextMemory m_memory{};

    /// The plane, which is either inside `m_memory` or inside the caller's image.
    std::optional<PlaneView<float>> m_view{};
};

/// Whether one channel of an image is a Float32 plane a filter can read where it lies.
///
/// @param sampleType the representation.
/// @param layout where the samples sit.
/// @return true when the representation is Float32 and each pixel holds one sample, so a channel is
///         a plane with a row stride.
[[nodiscard]] bool channelIsAddressablePlane(SampleType sampleType, const ImageLayout& layout) noexcept;

/// Widens one channel of an image into a Float32 working plane.
///
/// Reads the channel through the layout's arrangement and carries every sample across as the number
/// it is. Runs on the operation's threads and checks cancellation at chunk boundaries.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType the representation the layout describes.
/// @param samples where the image's samples start.
/// @param layout where they sit.
/// @param channelIndex which channel to read.
/// @param destination the working plane, which must have the layout's extent.
/// @param operation the name carried by any failure.
/// @return success; `ErrorCode::InvalidArgument` for a channel the image does not have or an
///         unknown representation; or the failure `parallelFor` reports for an empty or stopped
///         execution.
[[nodiscard]] Status gatherChannelPlane(const ExecutionContext& execution, SampleType sampleType, const void* samples,
                                        const ImageLayout& layout, std::uint32_t channelIndex,
                                        PlaneView<float> destination, const char* operation);

/// Writes a Float32 working plane back into one channel of an image, where the centre means
/// something.
///
/// The scatter half of the gather, and the place the family's "an invalid centre leaves its
/// destination sample untouched" rule is applied for an image the filter could not write in place.
/// The rule is evaluated on the *filter's input* plane rather than on the result, because that is
/// the sample the rule describes.
///
/// @param execution the operation policy and engine-context lease.
/// @param filtered the working plane holding the filter's answer.
/// @param filterInput the plane the filter read, whose samples the validity rule describes.
/// @param validity the rule and optional packed mask, with the channel's mask plane already chosen.
/// @param sampleType the destination representation.
/// @param samples where the destination's samples start.
/// @param layout where they sit.
/// @param channelIndex which channel to write.
/// @param conversion how a computed value becomes a stored sample.
/// @param operation the name carried by any failure.
/// @param originalSamples optional source storage to compare after conversion, read only when
///        `samplesChanged` is given.
/// @param originalLayout layout of `originalSamples`, required with it.
/// @param samplesChanged optional count to add exact stored-value changes to, and only on success.
/// @return success; `ErrorCode::InvalidArgument` for a channel the image does not have, an unknown
///         representation, or a change count asked for without the source storage and its layout;
///         or the failure `parallelFor` reports for an empty or stopped execution, in which case
///         some rows may already have been written.
[[nodiscard]] Status scatterChannelPlane(const ExecutionContext& execution, PlaneView<const float> filtered,
                                         PlaneView<const float> filterInput, const KernelValidityView& validity,
                                         SampleType sampleType, void* samples, const ImageLayout& layout,
                                         std::uint32_t channelIndex, const ConversionPolicy& conversion,
                                         const char* operation, const void* originalSamples = nullptr,
                                         const ImageLayout* originalLayout = nullptr,
                                         std::uint64_t* samplesChanged = nullptr);

/// Commits a Float32 working plane into one channel without a cancellation checkpoint.
///
/// The caller checks cancellation after every fallible computation and before calling this. Once
/// the commit begins it walks every row, so a stop arriving during write-back produces a complete
/// successful result rather than a cancelled operation with a partially changed destination.
///
/// @param filtered the working plane holding the filter's answer.
/// @param filterInput the plane the filter read, whose samples the validity rule describes.
/// @param validity the rule and optional packed mask, with the channel's mask plane already chosen.
/// @param sampleType the destination representation.
/// @param samples where the destination's samples start.
/// @param layout where they sit.
/// @param channelIndex which channel to write.
/// @param conversion how a computed value becomes a stored sample.
/// @param operation the name carried by any failure.
/// @param originalSamples optional source storage to compare after conversion, read only when
///        `samplesChanged` is given.
/// @param originalLayout layout of `originalSamples`, required with it.
/// @param samplesChanged optional count to add exact stored-value changes to.
/// @return success, or `ErrorCode::InvalidArgument` - for a channel the image does not have, an
///         unknown representation, or a change count asked for without the source storage and its
///         layout - detected before the first write.
[[nodiscard]] Status commitChannelPlane(PlaneView<const float> filtered, PlaneView<const float> filterInput,
                                        const KernelValidityView& validity, SampleType sampleType, void* samples,
                                        const ImageLayout& layout, std::uint32_t channelIndex,
                                        const ConversionPolicy& conversion, const char* operation,
                                        const void* originalSamples = nullptr,
                                        const ImageLayout* originalLayout = nullptr,
                                        std::uint64_t* samplesChanged = nullptr);

/// Counts accepted Float32 answers that differ from their source samples.
///
/// This is the in-place counterpart of `scatterChannelPlane`'s post-conversion count.
///
/// @param execution the operation policy and engine-context lease.
/// @param filtered the stored answers.
/// @param filterInput the source samples.
/// @param validity which source centres were eligible to be written.
/// @param changed the count to add to, only on success.
/// @return success, or the failure `parallelFor` reports for an empty or stopped execution.
[[nodiscard]] Status countChangedPlane(const ExecutionContext& execution, PlaneView<const float> filtered,
                                       PlaneView<const float> filterInput, const KernelValidityView& validity,
                                       std::uint64_t& changed);

/// Copies one Float32 plane onto another of the same picture.
///
/// Used where a multi-pass filter has to carry a sample the rule rejected forward into the pass that
/// follows, so that "untouched" means "unchanged" rather than "whatever the buffer held".
///
/// @param execution the operation policy and engine-context lease.
/// @param source the plane to read.
/// @param destination the plane to write, of the same extent.
/// @param operation the name carried by any failure.
/// @return success; `ErrorCode::IncompatibleArguments` when the extents differ; or the failure
///         `parallelFor` reports for an empty or stopped execution.
[[nodiscard]] Status copyPlane(const ExecutionContext& execution, PlaneView<const float> source,
                               PlaneView<float> destination, const char* operation);

/// The validity inputs for one channel of an image.
///
/// A mask with one bit-plane applies to every channel; a mask with one plane per channel selects the
/// plane matching the channel.
///
/// @param selection the rule and optional whole-image mask.
/// @param channelIndex which channel is being filtered.
/// @return the kernel's validity view for that channel.
[[nodiscard]] KernelValidityView channelValidity(const SpatialSelection& selection,
                                                 std::uint32_t channelIndex) noexcept;

/// Blends a filtered plane back towards the plane the filter read.
///
/// `amount` of one leaves the filtered plane alone, which is why the caller skips this entirely for
/// the ordinary case. Anything else writes `amount * filtered + (1 - amount) * input` into the
/// filtered plane, in Float64, narrowing once.
///
/// @param execution the operation policy and engine-context lease.
/// @param input the plane the filter read.
/// @param filtered the plane holding the answer, and where the blend is written.
/// @param validity the rule and optional mask, so a centre the filter left untouched stays
///        untouched rather than being blended with whatever the buffer held.
/// @param amount how much of the filtered value survives, within `[0, 1]`; the caller checks that
///        range, and this function does not.
/// @param operation the name carried by any failure.
/// @return success; `ErrorCode::IncompatibleArguments` when the extents differ; or the failure
///         `parallelFor` reports for an empty or stopped execution.
[[nodiscard]] Status blendPlane(const ExecutionContext& execution, PlaneView<const float> input,
                                PlaneView<float> filtered, const KernelValidityView& validity, double amount,
                                const char* operation);

/// Borrows one channel of a planar Float32 image as a read-only plane.
///
/// @param samples where the image samples start.
/// @param layout where they sit.
/// @param channelIndex which channel to reach.
/// @param sampleType the representation the layout describes.
/// @param geometry the picture the plane covers.
/// @return the plane, or no value when this channel is not addressable as one.
[[nodiscard]] std::optional<PlaneView<const float>> borrowSourcePlane(const void* samples, const ImageLayout& layout,
                                                                      std::uint32_t channelIndex, SampleType sampleType,
                                                                      const SpatialGeometry& geometry) noexcept;

/// Borrows one channel of a planar Float32 image as a writable plane.
///
/// @param samples where the image samples start.
/// @param layout where they sit.
/// @param channelIndex which channel to reach.
/// @param sampleType the representation the layout describes.
/// @param geometry the picture the plane covers.
/// @return the plane, or no value when this channel is not addressable as one.
[[nodiscard]] std::optional<PlaneView<float>> borrowDestinationPlane(void* samples, const ImageLayout& layout,
                                                                     std::uint32_t channelIndex, SampleType sampleType,
                                                                     const SpatialGeometry& geometry) noexcept;

/// Runs one plane filter over every channel of an image.
///
/// The whole of the family outer loop: it decides once whether either side can be reached where it
/// lies, takes at most two working planes whatever the channel count, and for each channel gathers,
/// filters and scatters. The filter itself is a callable so that a convolution, a Gaussian and an
/// order filter share this and disagree only about the arithmetic between two planes.
///
/// A template rather than an erased callable because this is the outer loop of every neighbourhood
/// filter in the engine, and an indirect call the compiler cannot see into is exactly what the
/// estimator families were written to avoid. It is a private header, so no exported symbol depends
/// on the instantiation.
///
/// @tparam FilterT a callable invoked as
///         `filter(PlaneView<const float>, PlaneView<float>, const KernelValidityView&)` returning a
///         `Status`, which accumulates whatever it reports into its own capture.
/// @param execution the operation policy and engine-context lease.
/// @param sampleType what the samples are, on both sides.
/// @param source where the source samples start.
/// @param sourceLayout where they sit.
/// @param destination where the filtered samples go.
/// @param destinationLayout where they sit.
/// @param selection which samples mean something.
/// @param writeBack how a computed value becomes a stored sample.
/// @param geometry the picture both sides agree about.
/// @param operation the name carried by any failure.
/// @param result the working-memory and in-place fields, written as the loop decides them.
/// @param filter what to run between two planes.
/// @param samplesChanged optional count of exact stored-value changes.
/// @return success, or the first failure: a refusal of working memory, `ErrorCode::InvalidArgument`
///         for a channel that cannot be borrowed as a plane, or whatever the gather, the filter, the
///         change count or the scatter reported.
template <typename FilterT>
[[nodiscard]] Status filterEveryChannel(const ExecutionContext& execution, SampleType sampleType, const void* source,
                                        const ImageLayout& sourceLayout, void* destination,
                                        const ImageLayout& destinationLayout, const SpatialSelection& selection,
                                        const ConversionPolicy& writeBack, const SpatialGeometry& geometry,
                                        const char* operation, SpatialFilterResult& result, FilterT&& filter,
                                        std::uint64_t* samplesChanged = nullptr)
{
    const bool sourceInPlace = channelIsAddressablePlane(sampleType, sourceLayout);
    const bool destinationInPlace = channelIsAddressablePlane(sampleType, destinationLayout);

    SpatialPlaneBuffer sourceWorking;
    if (!sourceInPlace)
    {
        Result<SpatialPlaneBuffer> taken = SpatialPlaneBuffer::own(execution, geometry, operation);
        if (!taken.hasValue())
        {
            return taken.status();
        }
        sourceWorking = std::move(taken).value();
    }
    SpatialPlaneBuffer destinationWorking;
    if (!destinationInPlace)
    {
        Result<SpatialPlaneBuffer> taken = SpatialPlaneBuffer::own(execution, geometry, operation);
        if (!taken.hasValue())
        {
            return taken.status();
        }
        destinationWorking = std::move(taken).value();
    }

    result.readInPlace = sourceInPlace;
    result.wroteInPlace = destinationInPlace;
    result.workingBytes += sourceWorking.bytes() + destinationWorking.bytes();
    result.channelCount = geometry.channelCount;

    for (std::uint32_t channel = 0u; channel < geometry.channelCount; ++channel)
    {
        if (sourceWorking.owned())
        {
            const Status gathered = gatherChannelPlane(execution, sampleType, source, sourceLayout, channel,
                                                       sourceWorking.view(), operation);
            if (gathered.failed())
            {
                return gathered;
            }
        }
        const std::optional<PlaneView<const float>> borrowedInput =
            sourceWorking.owned() ? std::nullopt
                                  : borrowSourcePlane(source, sourceLayout, channel, sampleType, geometry);
        const std::optional<PlaneView<float>> borrowedOutput =
            destinationWorking.owned()
                ? std::nullopt
                : borrowDestinationPlane(destination, destinationLayout, channel, sampleType, geometry);
        if ((!sourceWorking.owned() && !borrowedInput.has_value()) ||
            (!destinationWorking.owned() && !borrowedOutput.has_value()))
        {
            return spatialFailure(ErrorCode::InvalidArgument, operation,
                                  "a channel of this image is not addressable as a plane");
        }

        const PlaneView<const float> input = sourceWorking.owned() ? sourceWorking.constView() : *borrowedInput;
        const PlaneView<float> output = destinationWorking.owned() ? destinationWorking.view() : *borrowedOutput;
        const KernelValidityView validity = channelValidity(selection, channel);
        const Status filtered = filter(input, output, validity);
        if (filtered.failed())
        {
            return filtered;
        }

        if (samplesChanged != nullptr && destinationInPlace)
        {
            const Status counted = countChangedPlane(execution, output.constView(), input, validity, *samplesChanged);
            if (counted.failed())
            {
                return counted;
            }
        }

        if (destinationWorking.owned())
        {
            const Status scattered = scatterChannelPlane(execution, destinationWorking.constView(), input, validity,
                                                         sampleType, destination, destinationLayout, channel, writeBack,
                                                         operation, source, &sourceLayout, samplesChanged);
            if (scattered.failed())
            {
                return scattered;
            }
        }
    }
    return Status::success();
}

} // namespace an::spatial_detail
