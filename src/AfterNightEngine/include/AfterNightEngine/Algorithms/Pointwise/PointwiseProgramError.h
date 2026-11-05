// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The reasons a pointwise program refuses the steps it was given.
///
/// A pointwise program is validated once and executed many times, which is the whole reason it
/// exists. That trade only pays if the validation is complete: an executor that has to re-check a
/// step is an executor that has to have an opinion about what to do when the check fails, in the
/// middle of a fused loop, per pixel. So everything that can be decided about a chain is decided
/// here, and the executor is left with arithmetic.
///
/// These are separate reasons rather than one "invalid program" because a caller acts on them
/// differently. A coefficient that is not a number is a caller's arithmetic to fix; a step naming a
/// source plane that was never supplied is a program assembled against the wrong inputs; a lookup
/// table in a position where its index has no meaning is a chain that has to be rewritten rather
/// than corrected. Collapsing those into one code would leave every caller re-deriving which of them
/// it was, which is the failure mode this vocabulary exists to prevent.

#pragma once

#include <AfterNightEngine/Core/Api.h>

namespace an
{

/// Why a pointwise program refused the steps and declarations it was given.
///
/// Returned by the builder's `check` entry point, which is where a caller goes for the reason; the
/// matching `build` entry point returns the program and a full failure. `None` is the only
/// enumerator that means the program was accepted.
///
/// The declarations are checked first - channel count, source count, precision, domains, then that
/// there are steps and not too many - and then each step in order, facts before combinations: a
/// step whose kind this build does not know is refused before any rule that would have to interpret
/// its coefficients, because a rule cannot judge a step it does not understand and reporting the
/// rule would name the wrong problem.
enum class PointwiseProgramError
{
    /// The steps and declarations describe a program this engine can execute.
    None,

    /// The chain has no steps. An empty program is not the identity: an identity program is one a
    /// caller wrote deliberately, and the difference matters when a chain was assembled by code
    /// that was supposed to add something to it.
    EmptyProgram,

    /// A step's kind is not one this build can execute.
    UnknownOpKind,

    /// A coefficient is not finite. Infinities and not-a-number are refused as coefficients even
    /// though they are ordinary sample values, because a coefficient is a statement about the
    /// operation and there is no operation an infinite multiplier describes.
    NonFiniteCoefficient,

    /// A step set a member its kind does not use. Refused rather than ignored, so that two steps
    /// that do the same thing compare equal and a canonicalizer can rely on that.
    UnusedFieldSet,

    /// A clamp's lower bound is above its upper bound, which admits no value at all.
    InvertedClampBounds,

    /// A blend weight lies outside the closed interval from zero to one.
    BlendWeightOutOfRange,

    /// A comparison step names no relation this build implements.
    UnknownComparison,

    /// A sampled curve names no interpolation this build implements.
    UnknownCurveSampling,

    /// A sampled curve names no out-of-domain policy this build implements.
    UnknownCurveDomainPolicy,

    /// A sampled curve's interval is not increasing, or a curve with lower-neighbour sampling states
    /// an out-of-domain policy other than clamping. An interval bound that is not finite is refused
    /// earlier, as `NonFiniteCoefficient`.
    InvalidCurveDomain,

    /// A step names a source plane the program was not told it would have.
    OperandOutOfRange,

    /// A step names a table the program does not carry.
    TableOutOfRange,

    /// A table is not the size its step requires: a channel matrix needs the square of the channel
    /// count, a lookup table needs one entry per value of the source representation, and a sampled
    /// curve needs at least two entries.
    TableSizeMismatch,

    /// A table holds a value that is not finite. A table is evaluated arithmetic rather than sample
    /// data, so an entry that is not a number is a table that was built wrongly.
    NonFiniteTableEntry,

    /// A lookup step is not the first step. Its index is the source sample itself, so anything
    /// before it has already replaced the value that would have been the index.
    LutNotFirstStep,

    /// A lookup step's program does not read an unsigned eight- or sixteen-bit integer
    /// representation, so there is no index without inventing a rounding or bias rule this engine
    /// has not stated.
    LutSourceNotIndexable,

    /// The channel count is zero or above `kMaxPointwiseChannels`, which is smaller than what one
    /// pixel of this engine's images can hold.
    ChannelCountUnsupported,

    /// The program declares no source plane, or more than `kMaxPointwiseSources`.
    SourceCountUnsupported,

    /// The chain has more than `kMaxPointwiseSteps` steps.
    TooManySteps,

    /// The declared arithmetic width is Float64 without one of the approved reasons, or Float32
    /// where the declared output needs more; also a source representation, width or output contract
    /// this build does not know.
    ComputePrecisionRefused,

    /// The program's input or output numeric domain is not one this build knows, or the program
    /// holds a power step while its input domain is neither `NumericDomain::NormalizedLinear` nor
    /// `NumericDomain::EncodedNonlinear`.
    NumericDomainRefused
};

/// The reason's stable name for diagnostics, failure messages, and tests.
///
/// Thread-safe, reentrant, and allocation-free. The returned pointer is a string literal that
/// outlives any caller.
///
/// @param error the reason, including `None` and values this build does not know.
/// @return the name, or "unknown" for a value outside the enumeration.
[[nodiscard]] AFTERNIGHTENGINE_API const char* pointwiseProgramErrorName(PointwiseProgramError error) noexcept;

} // namespace an
