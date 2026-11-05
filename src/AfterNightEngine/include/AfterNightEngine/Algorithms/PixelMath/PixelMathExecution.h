// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Running a compiled expression over a whole image.
///
/// This is the specialized evaluator: the path an expression takes when it is not one of the chains
/// the fused pointwise executor can run. It exists because the fusable subset is a subset - a chain
/// that branches per pixel, reads a stated coordinate, draws a random number or assigns to a variable
/// is none of the fused executor's business, and pretending otherwise would mean either refusing
/// expressions the language has or teaching the fused IR to do things that are not element-wise.
///
/// What it shares with everything else in this engine is where its permission and its threads come
/// from. It runs on the operation's own arena, checks cancellation at chunk boundaries, reports what
/// it selected and wrote, and takes its working memory once per worker rather than once per pixel.
///
/// Whole-image statistics are computed before the walk, once per statistic per channel, through the
/// engine's own statistics operations. That is the difference between `med($T)` costing one pass over
/// the frame and costing one per pixel, and it is why the resolution collects them rather than the
/// evaluator discovering them.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Algorithms/PixelMath/PixelMathEvaluator.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/OperationEffect.h>

#include <cstdint>
#include <optional>
#include <span>

namespace an
{

/// The writable image a compiled expression is evaluated into.
struct PixelMathMutableImageView
{
    /// The resident representation the layout's element width describes.
    SampleType sampleType = SampleType::Float32;

    /// First writable byte of the image storage, borrowed for the call.
    void* samples = nullptr;

    /// The validated geometry and strides of `samples`, borrowed for the call.
    const ImageLayout* layout = nullptr;
};

/// Everything one execution binds.
struct PixelMathExecutionRequest
{
    /// Exactly `PixelMathProgram::images().size()` borrowed images, in the program's own order.
    /// Index zero is the target.
    std::span<const PixelMathImageView> images{};

    /// Where the result goes. The target's width and height, and its channel count too unless
    /// `destinationChannel` names one; either a separate image or exactly one of the bound images,
    /// same storage and same layout, when the program says it may be executed in place.
    PixelMathMutableImageView destination{};

    /// Which samples of the target participate, and the packed mask when the rule names one.
    KernelValidityView validity{};

    /// Which channel to write, or no value to write every channel of the destination.
    ///
    /// A caller that wants one expression per channel compiles one program per channel and states
    /// the channel here; a caller with one expression for the whole image states none. Both are
    /// ordinary uses, and neither is a mode the program carries.
    std::optional<std::uint32_t> destinationChannel{};

    /// The seed the random stream is derived from. Two executions with the same seed over the same
    /// images produce the same image, at any thread count.
    std::uint64_t randomSeed = 0u;
};

/// What one execution counted.
struct PixelMathExecutionResult
{
    /// Pixel selection and sample writes. Selection counts pixel-channel evaluations; writes count
    /// the samples that were stored.
    KernelRunResult run{};

    /// What narrowing the computed values to the destination representation encountered.
    ConversionCounters conversion{};

    /// How many passes over an image the whole-image statistics cost, before the walk began.
    std::uint64_t statisticPasses = 0u;
};

/// Evaluates a compiled expression over every selected sample of the destination.
///
/// Each bound image is read where it lies: planar, interleaved or mixed, with whatever padding its
/// layout describes. A sample of an image with one channel is broadcast to every destination
/// channel; a sample of an image with the destination's channels is read from the matching one.
///
/// A pixel participates only when every channel of the target satisfies the validity rule and, when
/// present, its mask bit is set. An invalid pixel leaves its destination samples untouched.
///
/// Whole-image statistics are computed first, once per request per channel of the target - every
/// channel, even when `destinationChannel` names one - through the engine's statistics operations
/// and therefore under their own selection and determinism contracts. The request's validity rule
/// and mask select for them too. A statistic over an empty selection fails the execution rather
/// than producing a value nothing stands behind.
///
/// Values are narrowed to the destination representation once, as code values, with nearest-even
/// rounding, saturation and the approved non-finite mapping. The counters are unconditional. There
/// is no clamp to the unit interval.
/// Cancellation or a per-pixel failure may leave previously scheduled samples written; the
/// destination is not failure-atomic.
///
/// An expression with no conditional is evaluated a run of adjacent samples at a time and one that
/// branches a sample at a time; forcing `ComputeBackend::Reference` evaluates every expression a
/// sample at a time, through what `evaluatePixelMathPixel` runs. The two write the same bits.
///
/// Thread-safe with respect to the program and the bound images, which are only read. Runs on the
/// execution's own threads and checks cancellation at chunk boundaries. Working memory is taken from
/// the engine context once per execution, a share per worker, never per pixel.
///
/// @param execution the operation policy and engine-context lease.
/// @param program the compiled expression.
/// @param request the bound images, destination, validity, channel and seed.
/// @return the counts; `ErrorCode::NotInitialized` for an empty execution context;
///         `ErrorCode::InvalidArgument` for an empty program, a null pointer, a wrong image count, a
///         layout that does not describe its declaration or its representation, a destination
///         channel the destination does not have, an overlap other than exact aliasing, in-place
///         execution of a program that does not support it, or a packed mask that is missing, not
///         needed, or overlaps the destination; `ErrorCode::IncompatibleArguments` when the
///         destination does not have the target's width and height, or its channel count when no
///         channel is stated, or the mask does not cover the target; or the execution's
///         cancellation, a statistics operation's failure, a colour-plan failure, the engine
///         context's refusal of the working memory, or a per-pixel evaluation failure.
[[nodiscard]] AFTERNIGHTENGINE_API Result<PixelMathExecutionResult>
executePixelMath(const ExecutionContext& execution, const PixelMathProgram& program,
                 const PixelMathExecutionRequest& request);

/// What `executePixelMath` does to the image it writes.
///
/// An expression can compute anything from any of the images bound to it, so this is the most
/// conservative declaration in the surface and deliberately so. The result claims what every bound
/// image claims together, and it states its own meaning, because nothing in a script tells the
/// engine that the numbers coming out mean what the numbers going in meant.
///
/// The description is preserved because the destination's extent, representation and channel schema
/// are the caller's, stated before the program ran; an expression changes sample values inside a
/// picture whose shape it was given.
inline constexpr OperationEffect kPixelMathExecutionEffect{
    .semantics = SemanticEffect::Restates, .validity = ValidityTransition{.policy = ValidityPolicy::Combine}};
} // namespace an
