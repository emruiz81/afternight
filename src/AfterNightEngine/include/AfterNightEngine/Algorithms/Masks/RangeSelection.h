// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Soft range selection: an image of weights saying how far inside a stated range each sample lies.
///
/// The operation reads a Float32 or Float64 working image in the normalized domain and writes one of
/// the same representation whose samples are weights - one inside the range, zero outside it, and a
/// linear ramp across each edge when the request asks for fuzziness - which screening and inversion
/// then shape, in that order.
///
/// The result is an ordinary floating image, not a packed validity mask: a weight of one half is a
/// strength, not a claim about whether a sample exists. `Algorithms/Masks/MaskOperations.h` is where
/// yes-or-no masks are built. The source's own validity decides which samples are weighed at all,
/// and a sample it excludes leaves its destination sample exactly as it was.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/SampleType.h>

namespace an
{

/// Parameters of one normalized soft range selection.
///
/// The operation produces a regular floating image rather than a packed validity mask: values
/// between the limits are weights, not yes/no claims about whether a source sample exists. Smoothing
/// the selection is deliberately a public composition: call `rangeSelectImage` with inversion
/// disabled, then production `blurImageGaussian`, then complement the result when inversion was
/// requested. A real-time control may explicitly substitute
/// `blurImageRealtimePreviewGaussian`; a saved mask never acquires that approximation implicitly.
/// Keeping those stages visible preserves their order without hiding full-image passes or scratch.
struct RangeSelectionRequest
{
    /// Inclusive lower limit in the normalized domain, within `[0, 1]`.
    double lowerLimit = 0.0;

    /// Inclusive upper limit in the normalized domain, within `[0, 1]` and not below `lowerLimit`.
    double upperLimit = 1.0;

    /// Fraction of the selected width occupied by both edge ramps, within `[0, 1]`.
    double fuzziness = 0.0;

    /// Multiply each selected weight by its source value - screening - instead of producing a white
    /// plateau.
    bool screening = false;

    /// Complement the completed weight after screening.
    bool invert = false;
};

/// Counts from one soft range-selection execution.
struct RangeSelectionResult
{
    /// Samples considered and admitted, and samples written.
    KernelRunResult run{};

    /// Channels processed.
    std::uint32_t channelCount = 0u;
};

/// Produces soft range-selection weights over a floating working image.
///
/// Source and destination may be planar, interleaved or mixed and may use different strides.
/// Float32 and Float64 are accepted; integer storage is converted explicitly through
/// `Image/ImageConversion.h`, so its signed and normalized-domain decision remains visible. Exact
/// in-place use is supported and partial overlap is refused. Each channel is selected independently.
/// A validity mask applies to all channels through `validity.maskPlane`; an invalid sample leaves
/// its destination sample untouched. Cancellation, or a non-finite sample the validity rule admits,
/// may leave part of the destination written.
///
/// The edge-ramp width on either side is
/// `(upperLimit - lowerLimit) * fuzziness / 2`. A sample in the central interval receives one; a
/// sample on an edge ramp receives its distance from the nearer outer limit divided by the ramp
/// width, rising from zero at the limit to one where the central interval starts; and a sample
/// outside receives zero. With no fuzziness every sample between the limits receives one. Screening
/// multiplies that answer by the source sample, then inversion complements it. Every layout
/// multiplies ramp offsets by the same Float64 reciprocal before adding one; validity selection does
/// not change the arithmetic.
///
/// Thread-safe with respect to the execution, source and validity, which are only read. Runs on the
/// operation's admitted threads.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType Float32 or Float64, shared by source and destination.
/// @param sourceSamples first byte of the source image, aligned to the sample size.
/// @param sourceLayout source geometry and strides.
/// @param destinationSamples first writable byte of the destination, aligned to the sample size.
/// @param destinationLayout destination geometry and strides, with the source's extent.
/// @param request limits, ramp width, screening and inversion.
/// @param validity source validity and optional packed mask. The mask is required exactly for
///        `ValidityRule::ExplicitMask`, must cover the image and the selected plane, and must not
///        overlap the destination.
/// @return counts on success; `ErrorCode::NotInitialized` for an empty execution;
///         `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` when the operation was stopped;
///         `ErrorCode::UnsupportedBackend` when the execution is constrained to a backend this build
///         has no dispatch table for; `ErrorCode::InvalidArgument` for a representation other than
///         Float32 or Float64, a null or misaligned buffer, a layout whose element size is not the
///         representation's, a mask missing for the mask rule or supplied for another rule, a mask
///         overlapping the destination, or a limit or fuzziness that is not finite or lies outside
///         its interval; `ErrorCode::IncompatibleArguments` for unequal extents, partial overlap of
///         source and destination, or a mask that does not cover the image and the selected plane;
///         `ErrorCode::InvalidData` for a non-finite sample the validity rule admits; or the
///         scheduler's own failures as `parallelFor` states them.
[[nodiscard]] AFTERNIGHTENGINE_API Result<RangeSelectionResult>
rangeSelectImage(const ExecutionContext& execution, SampleType sampleType, const void* sourceSamples,
                 const ImageLayout& sourceLayout, void* destinationSamples, const ImageLayout& destinationLayout,
                 const RangeSelectionRequest& request, const KernelValidityView& validity = {});

/// What `rangeSelectImage` does to the image it writes.
///
/// The result is a weight image rather than a picture of what the source showed, so its meaning is
/// restated, the source's header cards are dropped and any output decision is withdrawn. Its
/// validity is preserved from the source: a sample the source's validity excludes is not written,
/// so it still means nothing.
inline constexpr OperationEffect kRangeSelectionEffect{.samples = SampleEffect::Writes,
                                                       .semantics = SemanticEffect::Restates,
                                                       .codecMetadata = CodecMetadataEffect::Drops,
                                                       .outputIntent = OutputIntentEffect::Withdraws};

} // namespace an
