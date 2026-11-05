// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Whole images copied, filled and cleared, on a chosen backend.
///
/// `Image/ImageConversion.h` next door owns the operations that change what a sample *is*. This owns
/// the three that change nothing about it at all: a picture copied somewhere else, a picture set to
/// one value, and a picture set to zero. They are separate calls for the same reason a repack is
/// separate from a conversion - each has a different cost, and a function that quietly did more than
/// one of them would hide the expensive half in a profile.
///
/// **Nothing here reads a sample's meaning, and that is a contract rather than an implementation
/// note.** A copy of a `Float32` image reproduces every bit of every sample, including the ones this
/// host would call not-a-number and including a signalling pattern that an implementation putting
/// them through a floating register would be free to change. A fill writes the bytes it was given.
/// None of the three has a numeric domain, a validity rule or an accuracy tolerance, because none of
/// them computes anything.
///
/// **What is written is what the layout describes, and never what the buffer happens to hold.** An
/// image's memory and its description are separate numbers - storage keeps whatever capacity it was
/// allocated with and describes only as much of it as its current layout names - so an operation has
/// two candidate extents and only one of them is the picture. These write the described one. A clear
/// after a re-description therefore leaves every retained sample beyond it exactly as it was, which
/// is the difference between zeroing an image and zeroing an allocation.
///
/// **Which implementation ran is asked of the dispatch table, not returned from here.** These entry
/// points take an `ExecutionContext` and honour the backend and instruction set it was built with,
/// and a caller that needs to know which one it got asks `Runtime/BackendDispatch.h`. Force
/// `ComputeBackend::Reference` and the scalar oracle runs; leave it automatic and the vector kernels
/// do; compare the two and the comparison means something.
///
/// **A copy may not alias.** Overlapping source and destination would make what a sample becomes
/// depend on the order the rows happened to be scheduled in, so it is refused rather than being
/// correct on one thread and wrong on eight. A fill has one buffer and no such question.
///
/// **None of the three is failure-atomic.** A cancellation or a deadline leaves the rows already
/// walked written and the rest as they were.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ImageView.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/SampleType.h>

namespace an
{

/// One operation's policy and its permission to run. Defined in `Runtime/ExecutionContext.h`.
///
/// Named here rather than included, for the reason `Image/ImageConversion.h` names it: the image
/// model sits above the runtime, and a header that moves an image should not put the runtime's whole
/// include closure into every translation unit that reads it. A caller reaching these functions
/// holds an execution context and has therefore already included the header that defines it.
class ExecutionContext;

/// Copies every sample of one image into another.
///
/// The untyped entry point, and the one the exported library carries: a caller that knows its
/// representation only at run time - a codec, a fixture loader - reaches the same code as one that
/// knows it at compile time.
///
/// The two images must have the same extent, the same arrangement and the same representation.
/// Strides may differ freely, including a source that is contiguous and a destination that is not,
/// because that is exactly what a caller moving a picture into a padded buffer is doing.
///
/// Thread-safe with respect to `execution` and the source, both of which are only read. Runs on the
/// threads the operation was admitted.
///
/// @param execution the operation this copy belongs to. Must hold something.
/// @param sampleType what the samples are, on both sides.
/// @param source where the source samples start. Must not be null.
/// @param sourceLayout where the source samples sit. Its element size must be that
///        representation's.
/// @param destination where the copies go. Must not be null, and must not overlap the source.
/// @param destinationLayout where the copies sit.
/// @return success; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::UnsupportedBackend` when the operation is constrained to
///         `ComputeBackend::Gpu`, which has no dispatch table for this walk;
///         `ErrorCode::InvalidArgument` for a null buffer, a representation this build does not
///         know, or a layout whose element size is not that representation's;
///         `ErrorCode::IncompatibleArguments` when the two images differ in extent or arrangement
///         or when they overlap; or `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` when
///         the operation was stopped.
[[nodiscard]] AFTERNIGHTENGINE_API Status copyImageSamples(const ExecutionContext& execution, SampleType sampleType,
                                                           const void* source, const ImageLayout& sourceLayout,
                                                           void* destination, const ImageLayout& destinationLayout);

/// Writes one sample value over every sample of an image.
///
/// The value is read as the bytes of one sample of the stated representation, so a caller filling a
/// `Float32` image with a quiet not-a-number gets exactly the pattern it supplied rather than
/// whatever a floating register would have handed back.
///
/// Padding between rows is never written. A layout with a row stride wider than its row describes
/// memory the image does not own the meaning of, and a fill that covered it would destroy whatever
/// a caller had put there.
///
/// Thread-safe with respect to `execution` and `value`, both of which are only read.
///
/// @param execution the operation this fill belongs to. Must hold something.
/// @param sampleType what the samples are.
/// @param value one sample, in that representation. Must not be null, and must not sit inside the
///        image being filled.
/// @param destination where the samples start. Must not be null.
/// @param layout where the samples sit.
/// @return success; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::UnsupportedBackend` when the operation is constrained to
///         `ComputeBackend::Gpu`, which has no dispatch table for this walk;
///         `ErrorCode::InvalidArgument` for a null buffer, a representation this build does not
///         know, or a layout whose element size is not that representation's;
///         `ErrorCode::IncompatibleArguments` when `value` sits inside the image; or
///         `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` when the operation was stopped.
[[nodiscard]] AFTERNIGHTENGINE_API Status fillImageSamples(const ExecutionContext& execution, SampleType sampleType,
                                                           const void* value, void* destination,
                                                           const ImageLayout& layout);

/// Writes the representation's zero over every sample of an image.
///
/// A fill that needs no value, which is what makes it a different call rather than a convenience. A
/// caller holding a run-time `SampleType` has nowhere to put the one correctly-shaped sample a fill
/// wants; asking for zero states the whole request. Every representation this engine holds has one
/// zero and it is all-bits-zero - the six integer ones by definition, the two floating ones because
/// a binary interchange format spells positive zero that way - so there is no per-representation
/// choice hidden here and nothing to be surprised by.
///
/// The padding between rows and planes is not written, and neither is any byte past what `layout`
/// describes. That second half is the one worth stating: a storage that was allocated for a larger
/// image and then re-described holds retained capacity beyond its current layout, and a clear that
/// covered the allocation instead of the image would overwrite memory the caller may still be using
/// and would cost time proportional to what was allocated rather than to what is there.
///
/// Thread-safe with respect to `execution`, which is only read. Runs on the threads the operation
/// was admitted.
///
/// @param execution the operation this clear belongs to. Must hold something.
/// @param sampleType what the samples are.
/// @param destination where the samples start. Must not be null.
/// @param layout where the samples sit, and how many of them there are.
/// @return success; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::UnsupportedBackend` when the operation is constrained to
///         `ComputeBackend::Gpu`, which has no dispatch table for this walk;
///         `ErrorCode::InvalidArgument` for a null buffer, a representation this build does not
///         know, or a layout whose element size is not that representation's; or
///         `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` when the operation was stopped.
[[nodiscard]] AFTERNIGHTENGINE_API Status clearImageSamples(const ExecutionContext& execution, SampleType sampleType,
                                                            void* destination, const ImageLayout& layout);

/// Copies every sample of one image view into another.
///
/// The typed spelling of `copyImageSamples`, for a caller that already holds checked views. It adds
/// no rule of its own: the representation and the two layouts are read off the views, and everything
/// else is the untyped entry point's.
///
/// @tparam SampleT the representation, which does not change.
/// @param execution the operation this copy belongs to.
/// @param source the samples to copy.
/// @param destination where they go.
/// @return whatever `copyImageSamples` reported.
template <ResidentSample SampleT>
[[nodiscard]] Status copyImage(const ExecutionContext& execution, const ImageView<const SampleT>& source,
                               const ImageView<SampleT>& destination)
{
    return copyImageSamples(execution, kSampleTypeOf<SampleT>, source.data(), source.layout(), destination.data(),
                            destination.layout());
}

/// Writes one value over every sample of one image view.
///
/// The typed spelling of `fillImageSamples`. The value is taken by value and its bytes are what the
/// image receives, so a caller stating a `float` states a `float`.
///
/// @tparam SampleT the representation.
/// @param execution the operation this fill belongs to.
/// @param destination the samples to write.
/// @param value the sample to repeat.
/// @return whatever `fillImageSamples` reported.
template <ResidentSample SampleT>
[[nodiscard]] Status fillImage(const ExecutionContext& execution, const ImageView<SampleT>& destination, SampleT value)
{
    return fillImageSamples(execution, kSampleTypeOf<SampleT>, &value, destination.data(), destination.layout());
}

/// Writes zero over every sample of one image view.
///
/// The typed spelling of `clearImageSamples`. It is not `fillImage(execution, destination, SampleT{})`
/// with a shorter name: that spelling puts the value on the caller's stack and makes the fill prove
/// it does not sit inside the image, and this one has no value to place or to check.
///
/// @tparam SampleT the representation.
/// @param execution the operation this clear belongs to.
/// @param destination the samples to zero.
/// @return whatever `clearImageSamples` reported.
template <ResidentSample SampleT>
[[nodiscard]] Status clearImage(const ExecutionContext& execution, const ImageView<SampleT>& destination)
{
    return clearImageSamples(execution, kSampleTypeOf<SampleT>, destination.data(), destination.layout());
}

/// What every operation in this header does to the image it writes.
///
/// One declaration for all three, because a fill, a clear and a copy differ in which samples arrive
/// and in nothing else. Each writes samples and changes no metadata object: the result is the same
/// picture, described the same way, with different numbers in it.
///
/// The validity policy is `ValidityPolicy::Preserve` rather than `ValidityPolicy::Regenerate`, and
/// the clear is where that distinction earns its keep. A validity rule states how to read samples,
/// not how many currently pass it, so an image whose rule is `ValidityRule::ZeroIsNoData` keeps that
/// rule after being zeroed - and correctly claims afterwards that none of its samples mean anything.
/// An operation that regenerated the rule here would be replacing the caller's description of its
/// own image with a guess.
inline constexpr OperationEffect kImageTransferEffect{};

} // namespace an
