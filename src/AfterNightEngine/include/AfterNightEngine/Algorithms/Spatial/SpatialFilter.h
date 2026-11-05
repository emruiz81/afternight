// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What every neighbourhood filter shares: what it reads, what it promises,
/// and what it reports.
///
/// A neighbourhood filter reads more than one sample to write one. That single sentence is what
/// separates this family from everything in `Algorithms/Pointwise` and it is the reason the records
/// below exist once rather than once per operation: a convolution, a Gaussian and an order filter
/// disagree about arithmetic and agree about everything else, and three spellings of "which samples
/// mean something" would be free to drift apart.
///
/// **The working numbers are the stored numbers.** A filter widens a sample to the arithmetic type
/// and carries it across as the number it is; it does not rescale an integer code into a unit
/// interval on the way in and back out. A normalized kernel is scale-free, so a blur means the same
/// thing over UInt16 as over Float32, and a border constant, a substitute for a meaningless
/// neighbour and a stated no-data value are all written in the image's own units rather than in a
/// domain the caller has to convert into. Writing back is therefore a conversion under a stated
/// policy whose domain mapping is `DomainMapping::CodeValue`, and a policy that asks for
/// `DomainMapping::FullScale` is refused by name rather than silently ignored: a filter that rescaled
/// its input would be a filter and a domain change at once, and the caller could not tell which of
/// the two produced a number.
///
/// **This family computes in Float32 and accumulates in Float64.** The request states what the
/// output promises and `Image/ComputePrecision.h` decides whether Float32 can keep that promise.
/// Under `OutputPrecisionContract::NormalizedOrLossy` - which is what a blur, a sharpen and a median
/// are - every one of the eight resident representations is filtered, because the gap between two
/// adjacent code values is inside the declared tolerance. Under
/// `OutputPrecisionContract::ExactStoredValues` the three representations Float32 cannot distinguish
/// adjacent code values of - Float64, UInt32 and Int32 - are refused rather than rounded, which is
/// exactly what `requiresFloat64` names and is reported as `PrecisionError::PrecisionBelowContract`.
/// Two paths accumulate in Float32 instead, and each says so on its own entry point: the production
/// Gaussian's symmetric path and the real-time-preview box blur's fused path, both taken only for a
/// planar Float32 image in which every sample means something.
///
/// **An invalid centre leaves its destination sample untouched and an invalid neighbour is answered
/// by the border policy.** The two are different questions and are stated separately, as
/// `Algorithms/Kernel/StencilKernel.h` states them: a coordinate outside the image has no sample at
/// all, while a coordinate inside it has one the validity rule rejected, and substituting the border
/// value for the second would make an interior no-data pixel behave like an edge. Because a
/// meaningless sample is never spread and never created, the output claims what the input claimed and
/// the declared validity policy is `ValidityPolicy::Preserve`.
///
/// **The destination is a separate image.** Every entry point in this family refuses a destination
/// that shares memory with its source, because a neighbourhood read after a partial write would
/// depend on the order the rows happened to be scheduled in. A caller filtering in place allocates
/// the second image; the operation does not do it silently.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Image/ComputePrecision.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>

#include <cstdint>
#include <optional>

namespace an
{

/// Which of an image's samples a neighbourhood filter treats as meaning something.
///
/// One record for the whole family rather than one per operation. `validity` is always present and
/// defaults to every sample meaning something, which is what an image that has never been through a
/// calibration claims. `mask` is required exactly when the rule is `ValidityRule::ExplicitMask` and
/// must otherwise be null.
///
/// A mask with one bit-plane applies to every channel; a mask with one plane per image channel
/// selects the plane matching the channel being filtered. Both the mask and whatever owns it are
/// borrowed for one call and are read concurrently by the operation's workers, so a caller must not
/// change or destroy either until the call returns.
struct SpatialSelection
{
    /// The value or mask rule deciding whether a sample participates.
    ValidityDescriptor validity{};

    /// Full-image packed validity bits, or null when `validity` does not name an explicit mask.
    const ValidityMaskView<const std::uint8_t>* mask = nullptr;
};

/// What an operation promises about the values it writes, and how they reach the destination.
///
/// Two decisions rather than one record with a mode: what the output has to preserve is a promise
/// about the operation, and how a computed number becomes a stored sample is a property of the
/// destination representation. Both defaults are what a blur wants - a declared tolerance, and
/// nearest-even rounding that saturates at the representation's ends.
struct SpatialOutputPolicy
{
    /// What the result promises about the values it writes.
    ///
    /// `NormalizedOrLossy` admits every resident representation. `ExactStoredValues` refuses the
    /// three this family cannot serve in Float32, by name, before a sample is read.
    OutputPrecisionContract contract = OutputPrecisionContract::NormalizedOrLossy;

    /// How a computed Float32 value becomes a sample of the destination representation.
    ///
    /// `DomainMapping::CodeValue` is the default and the only mapping this family accepts: the
    /// working numbers are the stored numbers, so the write back carries the value across as the
    /// number it is. A policy naming `DomainMapping::FullScale` is refused.
    ConversionPolicy writeBack{.domain = DomainMapping::CodeValue};
};

/// What one neighbourhood filter did, and the plan it did it with.
///
/// The counts are summed over every channel the call filtered, so `run.selection.total` is pixels
/// times channels. The plan fields are the same for every channel, because the geometry a filter
/// runs on is a property of the image rather than of a channel.
struct SpatialFilterResult
{
    /// Pixel selection and writes, summed over the filtered channels. A centre the validity rule
    /// rejected leaves its destination sample untouched and is not counted as an output.
    KernelRunResult run{};

    /// How many channels were filtered, which is the source's channel count.
    std::uint32_t channelCount = 0u;

    /// Half the horizontal extent actually applied, in pixels.
    std::uint32_t horizontalRadius = 0u;

    /// Half the vertical extent actually applied, in pixels.
    std::uint32_t verticalRadius = 0u;

    /// How many times the operation walked the whole image.
    ///
    /// The number that says what a plan costs before a clock is read: a dense convolution is one
    /// pass, a separable one is two, and a three-pass box approximation is six. It counts passes
    /// over the samples and not the gather and scatter a representation other than Float32 also
    /// pays.
    std::uint64_t imagePasses = 0u;

    /// Context memory held at once, in bytes, across the working planes and whatever scratch the
    /// arithmetic itself needed.
    ///
    /// A planar Float32 image is read and written where it lies, so it contributes no working
    /// plane; a separable convolution still holds one intermediate band per concurrently running
    /// tile, and a repeated-box blur still holds its ping-pong plane. Only a dense convolution over
    /// a planar Float32 image holds nothing at all.
    std::uint64_t workingBytes = 0u;

    /// Workers the operation could actually use, which is never above its admitted concurrency.
    std::uint32_t plannedWorkers = 0u;

    /// The width the arithmetic ran at, which this family always reports as
    /// `ComputePrecision::Float32`.
    ComputePrecision precision = ComputePrecision::Float32;

    /// Whether the source was read where it lay rather than gathered into a working plane.
    bool readInPlace = false;

    /// Whether the result was written where it lies rather than scattered from a working plane.
    bool wroteInPlace = false;
};

/// What every operation in this family does to the image it writes.
///
/// A neighbourhood filter changes samples and nothing else. The extent, the representation and the
/// channel schema are the source's, so the description is preserved; a blurred frame is still a
/// frame of the same thing, so its meaning is preserved; nothing moves a star, so an astrometric
/// solution still describes the result; and no source card and no output decision is invalidated by
/// filtering.
///
/// The validity policy is `ValidityPolicy::Preserve` and that is a claim about the arithmetic rather
/// than a default that was never examined: a meaningless neighbour is answered by the border policy
/// instead of being spread, and a meaningless centre keeps the sample it had, so the operation can
/// neither create nor destroy meaninglessness.
inline constexpr OperationEffect kSpatialFilterEffect{};

/// Whether a filter reading this representation may keep this promise while computing in Float32.
///
/// The precision rule of the whole family in one call, so that three operations cannot answer it
/// three ways. It is `checkComputePrecision` asked with the width this family computes at, which is
/// why it takes no precision argument: there is nothing for a caller to choose.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param storage what the samples are stored as.
/// @param contract what the operation promises about its output.
/// @return `PrecisionError::None` when the filter may run; `PrecisionError::PrecisionBelowContract`
///         for an exact promise over Float64, UInt32 or Int32 storage; and the membership failures
///         for a representation or a contract this build does not know.
[[nodiscard]] constexpr PrecisionError spatialPrecisionError(SampleType storage,
                                                             OutputPrecisionContract contract) noexcept
{
    return checkComputePrecision(storage, contract, ComputePrecision::Float32, std::nullopt);
}

} // namespace an
