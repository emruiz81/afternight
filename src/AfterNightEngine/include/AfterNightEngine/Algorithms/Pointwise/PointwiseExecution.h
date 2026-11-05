// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Fused execution of a validated pointwise program over image storage.
///
/// `executePointwiseProgram` walks the destination once and runs every step of the chain on each
/// pixel before moving on, so a chain of any length costs one read of each source and one write of
/// the destination rather than one pass per step. What each pixel computes is defined by
/// `PointwiseEvaluator.h`; this header adds the images, the validity selection, the narrowing to the
/// resident representation and the threads.
///
/// Where the operation's dispatch table has a compiled run for the chain - Float32 or Float64
/// storage computed at its own width, all-valid input, no lookup, channel-matrix or sampled-curve
/// step, and a power step only in Float32 - the whole vectors of each row take it; everything else,
/// including the tail of a row, is evaluated pixel by pixel by the scalar evaluator. The compiled run
/// is held to the evaluator's answer within the program's stated accuracy, so which one a sample
/// took is not observable in an exact program. Fusing was measured rather than assumed: a chain of cheap
/// steps - a scale, a clamp - is dominated by reading and writing the frame, and runs several times
/// faster in one pass than in one pass per step, while a chain with real arithmetic per pixel, such as a
/// colour matrix, gains little.
///
/// `pointwiseProgramEffect` states what a chain does to an image's description and metadata, read
/// off the program before it runs.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgram.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/OperationEffect.h>

#include <span>

namespace an
{

/// One read-only image bound to a pointwise program.
///
/// The samples and layout are borrowed for one call. The layout must be planar, have the program's
/// channel count and the destination's extent, and describe the program's source representation,
/// and `samples` must be aligned for that representation. A null member is refused.
struct PointwiseImageView
{
    /// First byte of the image storage, borrowed for the call.
    const void* samples = nullptr;

    /// The validated geometry and strides of `samples`, borrowed for the call.
    const ImageLayout* layout = nullptr;
};

/// The writable image bound as a pointwise program's destination.
///
/// It has the same representation and extent as every source. Exact aliasing with a source is a
/// supported in-place invocation; every other overlap is refused before output begins.
struct PointwiseMutableImageView
{
    /// First writable byte of the image storage, borrowed for the call.
    void* samples = nullptr;

    /// The validated geometry and strides of `samples`, borrowed for the call.
    const ImageLayout* layout = nullptr;
};

/// Counts produced by one fused pointwise execution.
struct PointwiseExecutionResult
{
    /// Pixel selection and sample writes. Selection counts pixels; writes count channel samples.
    KernelRunResult run{};

    /// What narrowing the computed code values back to the resident representation encountered.
    ConversionCounters conversion{};
};

/// Executes every step of a pointwise program in one image pass.
///
/// Sources are bound in the same order as the program's plane-major scalar evaluator: source zero
/// is primary and subsequent sources provide the operands of mask, blend, arithmetic, extremum and
/// comparison steps. Each bound image is planar,
/// with one plane per channel; row and plane padding are preserved. A pixel participates only when
/// every channel of its primary source satisfies the value rule and, when present, its shared mask
/// bit is set. An invalid pixel leaves every destination channel untouched.
///
/// Every program computes at its declared precision. Results are narrowed once, as code values,
/// with nearest-even rounding, saturation, and the approved non-finite mapping. The counters are
/// unconditional. Exact in-place execution is safe because all sources of a pixel are loaded
/// before any channel of that pixel is written. A packed validity mask must not overlap the
/// destination. Cancellation or a per-pixel evaluation failure may leave previously scheduled
/// samples written; the destination is not failure-atomic.
///
/// The selected backend is the operation's dispatch table. `instructionSetOfDispatchedCode` reports
/// its actual compiled instruction set; unsupported program shapes take that table's scalar tail
/// without changing the reported selection.
///
/// A program containing lower-neighbour curve sampling is refused: that approximation belongs to
/// the opaque real-time-preview gamma plan and runs only through `executeRealtimePreviewGamma`.
///
/// Thread-safe with respect to the immutable program and the borrowed sources. Different calls may
/// run concurrently when their destinations do not overlap.
///
/// @param execution the operation policy and engine-context lease.
/// @param program the immutable validated chain.
/// @param sources exactly `program.sourceCount()` borrowed planar images.
/// @param destination the equally shaped writable planar image.
/// @param validity the primary input's validity description and optional shared packed mask.
/// @return counts on success; `ErrorCode::NotInitialized` for an empty execution context; the
///         execution's cancellation or backend-selection failure; `ErrorCode::InvalidArgument` for a
///         wrong number of sources, a null or misaligned pointer, an overlap other than exact
///         aliasing, or a packed mask that is missing, not needed or overlapping the destination;
///         `ErrorCode::IncompatibleArguments` for an image that is not planar in the program's
///         representation and channel count, a source whose extent differs from the destination's,
///         a mask that does not cover the destination, or a program holding a lower-neighbour
///         curve; or the scalar evaluator's `ErrorCode::OutOfRange` for a pixel it refuses. A
///         failing invocation may have partially written the destination before the failure became
///         observable.
[[nodiscard]] AFTERNIGHTENGINE_API Result<PointwiseExecutionResult>
executePointwiseProgram(const ExecutionContext& execution, const PointwiseProgram& program,
                        std::span<const PointwiseImageView> sources, PointwiseMutableImageView destination,
                        const KernelValidityView& validity);

/// What running this program does to the image it writes.
///
/// A declaration rather than a constant, because a pointwise program is a chain a caller assembled
/// and two chains do different things: scaling every sample changes no description, while raising it
/// through a transfer function changes what the numbers mean and a channel matrix changes which
/// basis they are in. Everything this needs is decided when the program is built, so the answer is
/// read off the immutable chain rather than measured while it runs.
///
/// The four rules, in full:
///
/// * the description is restated when the program writes a different numeric domain than it reads,
///   or when any step is a channel matrix, because both of those facts live in an image's
///   description and neither can be inferred from the samples afterwards;
/// * the meaning is restated when any step is a gamma, a lookup table or a sampled curve, because
///   each of the three changes the relationship between a sample and the quantity it stands for;
/// * the preserved source cards and any output decision are withdrawn exactly when the description
///   is restated, since each described the representation or basis that has just changed; and
/// * the validity is preserved for a one-source program and combined for a chain that reads several,
///   because a blend or a mask step brings a second image's claim with it.
///
/// An astrometric solution always survives: no pointwise step moves a pixel.
///
/// Thread-safe and allocation-free.
///
/// @param program the immutable validated chain that is about to run, or that just did.
/// @return the declaration for that chain. Always consistent, so `checkOperationEffect` on the
///         result reports `OperationEffectError::None`.
[[nodiscard]] AFTERNIGHTENGINE_API OperationEffect pointwiseProgramEffect(const PointwiseProgram& program) noexcept;

} // namespace an
