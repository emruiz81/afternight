// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Whole images converted, repacked and byte-swapped, on a chosen backend.
///
/// `Image/SampleConversion.h` says what one sample becomes. This says what a whole image becomes, and
/// everything it adds is about the three things a run of samples does not know: where the samples
/// sit, how many threads may walk them, and which compiled implementation should do the walking.
///
/// **Which implementation ran is asked of the dispatch table, not returned from here.** These
/// entry points take an `ExecutionContext` and honour the backend and instruction set it was built
/// with, and a caller that needs to know which one it got asks `Runtime/BackendDispatch.h`. That is
/// the engine's general arrangement and it is what makes a conformance run possible: force
/// `ComputeBackend::Reference` and the scalar matrix runs; leave it automatic and the compiled
/// backend does; compare the two outputs and the comparison means something.
///
/// **A conversion changes a representation and nothing else.** It does not move samples between a
/// planar and an interleaved arrangement, it does not swap byte order, and it does not resize. Each
/// of those is a separate call here, because each has a different cost and a different failure, and a
/// function that quietly did all four would make an accidental full repack invisible in a profile.
///
/// **Nothing may alias.** A conversion whose destination overlaps its source reads samples the same
/// call has already written, and the answer depends on the order the rows happened to be scheduled
/// in - so it is refused rather than being correct on one thread and wrong on eight. The byte-order
/// swap is the exception and is in-place by definition.
///
/// **The counters are the same counters.** A conversion of a whole image accumulates exactly what the
/// scalar reference would over the same samples, whichever backend ran and however many threads it
/// used, because every count is a property of the samples rather than of the schedule. That holds
/// for a conversion that finishes. One that a rejecting policy or a stop ends early has counted what
/// the rows it had already walked met, which depends on the schedule.
///
/// **Nothing here is failure-atomic.** A refusal met partway through, a cancellation or a deadline
/// leaves the rows already walked written and the rest as they were.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/ImageView.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/SampleType.h>

namespace an
{

/// One operation's policy and its permission to run. Defined in `Runtime/ExecutionContext.h`.
///
/// Named here rather than included, for the reason `Image/ImageStorage.h` names the engine context:
/// the image model sits above the runtime, and a header that converts an image should not put the
/// runtime's whole include closure into every translation unit that reads it. A caller reaching
/// these functions holds an execution context and has therefore already included the header that
/// defines it.
class ExecutionContext;

/// Converts every sample of one image into another representation.
///
/// The untyped entry point, and the one the exported library actually carries: the sixty-four
/// ordered pairs are resolved inside from the two representations rather than instantiated at every
/// call site, so a caller that knows its types at run time - a codec, a fixture loader - reaches the
/// same code as one that knows them at compile time.
///
/// The two images must have the same extent and the same arrangement. Strides may differ freely,
/// including a source that is contiguous and a destination that is not; what may not differ is how
/// many of a pixel's samples share a plane, because changing that is a repack and has its own call.
///
/// Thread-safe with respect to `execution` and the source, both of which are only read. Runs on the
/// threads the operation was admitted.
///
/// @param execution the operation this conversion belongs to. Must hold something.
/// @param sourceType what the source samples are.
/// @param source where the source samples start. Must not be null.
/// @param sourceLayout where the source samples sit. Its element size must be the source
///        representation's.
/// @param destinationType what the destination samples are to be.
/// @param destination where the destination samples start. Must not be null, and must not overlap
///        the source.
/// @param destinationLayout where the destination samples sit.
/// @param policy the five decisions one sample conversion makes.
/// @param counters what the conversion met, added to whatever they already hold. Added to on a
///        refusal or a stop as well, with what the rows walked before it met.
/// @return success; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::UnsupportedBackend` when the operation is constrained to
///         `ComputeBackend::Gpu`, which has no dispatch table for this walk;
///         `ErrorCode::InvalidArgument` for a null buffer, a representation this build does not
///         know, a layout whose element size is not its representation's, or a policy naming a
///         decision this build does not know; `ErrorCode::IncompatibleArguments` when the two
///         images differ in extent or arrangement or when they overlap; `ErrorCode::Cancelled` or
///         `ErrorCode::DeadlineExceeded` when the operation was stopped; `ErrorCode::InvalidData`
///         when `NonFinitePolicy::Reject` met a non-finite sample; or `ErrorCode::OutOfRange` when
///         `RangePolicy::Reject` met a value the destination cannot hold. A refusal carries the
///         refused sample's position in the image, which on more than one thread is the first one
///         its row range found rather than necessarily the first in image order.
[[nodiscard]] AFTERNIGHTENGINE_API Status convertImageSamples(const ExecutionContext& execution, SampleType sourceType,
                                                              const void* source, const ImageLayout& sourceLayout,
                                                              SampleType destinationType, void* destination,
                                                              const ImageLayout& destinationLayout,
                                                              const ConversionPolicy& policy,
                                                              ConversionCounters& counters);

/// Moves an image's samples between a planar and an interleaved arrangement.
///
/// The representation does not change and no value changes; only where each sample sits does. It is
/// separate from a conversion because it is a different cost - a strided gather rather than a
/// streaming read - and because a call that did both would hide one of them.
///
/// Thread-safe with respect to `execution` and the source, both of which are only read.
///
/// @param execution the operation this repack belongs to. Must hold something.
/// @param sampleType what the samples are, on both sides.
/// @param source where the source samples start. Must not be null.
/// @param sourceLayout where the source samples sit.
/// @param destination where the destination samples start. Must not be null, and must not overlap
///        the source.
/// @param destinationLayout where the destination samples sit. Its extent must be the source's; its
///        arrangement is what the repack is for.
/// @return success; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::UnsupportedBackend` when the operation is constrained to
///         `ComputeBackend::Gpu`, which has no dispatch table for this walk;
///         `ErrorCode::InvalidArgument` for a null buffer, a representation this build does not
///         know, or a layout whose element size is not that representation's;
///         `ErrorCode::IncompatibleArguments` when the two images differ in extent or when they
///         overlap; or `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` when the operation
///         was stopped.
[[nodiscard]] AFTERNIGHTENGINE_API Status repackImageSamples(const ExecutionContext& execution, SampleType sampleType,
                                                             const void* source, const ImageLayout& sourceLayout,
                                                             void* destination, const ImageLayout& destinationLayout);

/// Rewrites every sample of an image in the other byte order, in place.
///
/// What a boundary calls on a payload it has just read from a file whose order is not this host's,
/// and what an encoder calls on a copy it is about to write. It is in place because that is what the
/// callers want - a decoder has already allocated the buffer the samples arrived in - and because a
/// byte swap is its own inverse, so nothing is lost by not having a second buffer.
///
/// The samples are not interpreted. A swap of a run of `Float32` samples reverses four bytes at a
/// time whatever those bytes are, so a payload holding values this host would call not-a-number
/// survives the round trip unchanged.
///
/// Thread-safe with respect to `execution`, which is only read.
///
/// @param execution the operation this swap belongs to. Must hold something.
/// @param sampleType what the samples are, which is where the element width comes from.
/// @param samples where the samples start. Must not be null.
/// @param layout where the samples sit.
/// @return success; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::UnsupportedBackend` when the operation is constrained to
///         `ComputeBackend::Gpu`, which has no dispatch table for this walk;
///         `ErrorCode::InvalidArgument` for a null buffer, a representation this build does not
///         know, a layout whose element size is not that representation's, or a one-byte
///         representation, which has one order and nothing to swap; or `ErrorCode::Cancelled` or
///         `ErrorCode::DeadlineExceeded` when the operation was stopped.
[[nodiscard]] AFTERNIGHTENGINE_API Status swapImageByteOrder(const ExecutionContext& execution, SampleType sampleType,
                                                             void* samples, const ImageLayout& layout);

/// Converts every sample of one image view into another representation.
///
/// The typed spelling of `convertImageSamples`, for a caller that already holds checked views. It
/// adds no rule of its own: the two representations and the two layouts are read off the views, and
/// everything else is the untyped entry point's.
///
/// @tparam SourceT the representation converted from.
/// @tparam DestT the representation converted to.
/// @param execution the operation this conversion belongs to.
/// @param source the samples to convert.
/// @param destination where they go.
/// @param policy the five decisions one sample conversion makes.
/// @param counters what the conversion met, added to whatever they already hold.
/// @return whatever `convertImageSamples` reported.
template <ResidentSample SourceT, ResidentSample DestT>
[[nodiscard]] Status convertImage(const ExecutionContext& execution, const ImageView<const SourceT>& source,
                                  const ImageView<DestT>& destination, const ConversionPolicy& policy,
                                  ConversionCounters& counters)
{
    return convertImageSamples(execution, kSampleTypeOf<SourceT>, source.data(), source.layout(), kSampleTypeOf<DestT>,
                               destination.data(), destination.layout(), policy, counters);
}

/// Moves one image view's samples into another arrangement.
///
/// The typed spelling of `repackImageSamples`.
///
/// @tparam SampleT the representation, which does not change.
/// @param execution the operation this repack belongs to.
/// @param source the samples to move.
/// @param destination where they go.
/// @return whatever `repackImageSamples` reported.
template <ResidentSample SampleT>
[[nodiscard]] Status repackImage(const ExecutionContext& execution, const ImageView<const SampleT>& source,
                                 const ImageView<SampleT>& destination)
{
    return repackImageSamples(execution, kSampleTypeOf<SampleT>, source.data(), source.layout(), destination.data(),
                              destination.layout());
}

/// Rewrites one image view's samples in the other byte order, in place.
///
/// The typed spelling of `swapImageByteOrder`.
///
/// @tparam SampleT the representation, which is where the element width comes from.
/// @param execution the operation this swap belongs to.
/// @param samples the samples to rewrite.
/// @return whatever `swapImageByteOrder` reported.
template <ResidentSample SampleT>
[[nodiscard]] Status swapByteOrder(const ExecutionContext& execution, const ImageView<SampleT>& samples)
{
    return swapImageByteOrder(execution, kSampleTypeOf<SampleT>, samples.data(), samples.layout());
}

} // namespace an
