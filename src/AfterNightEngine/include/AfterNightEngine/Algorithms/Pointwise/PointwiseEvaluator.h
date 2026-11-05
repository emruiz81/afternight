// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The authoritative scalar meaning of a pointwise program.
///
/// This is the oracle, and it is part of the deliverable rather than scaffolding. Everything else
/// that will ever execute a pointwise program - a fused loop, a vector kernel, a future device
/// backend, a chain rewritten into a lookup table - is defined as producing what this produces. It
/// takes one pixel at a time and does the obvious thing in the obvious order, because the only
/// property it needs is that its behaviour can be read off it.
///
/// Two conventions are stated here rather than inferred, because everything downstream depends on
/// them being the same everywhere.
///
/// A sample's value is its own code value. An eight- or sixteen-bit source contributes the integer
/// it stores, not that integer divided by a full scale. This engine does not normalize behind a
/// caller's back, and it is what makes a lookup step's index be the sample rather than a rounding
/// decision.
///
/// Not-a-number propagates through arithmetic and its payload does not survive. Arithmetic steps
/// quiet a signalling not-a-number, which is what the hardware does. Three kinds of step answer
/// otherwise, each by its own definition: a clamp leaves any not-a-number exactly as it found it,
/// because both of its comparisons are false for one and choosing a bound would be inventing an
/// answer; a minimum or maximum follows `fmin` and `fmax`, so a number wins over a not-a-number; and
/// a comparison answers zero, or one for `NotEqual`. A program that must preserve payloads is not a
/// program that computes.
///
/// The scalar functions round as the C++ library does at the program's width, so a power step here
/// is `std::pow`. A compiled kernel is held to this: within the accuracy `pointwiseProgramAccuracy`
/// states when the program raises to a power, and exactly otherwise.

#pragma once

#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgram.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Status.h>

#include <span>

namespace an
{

/// Runs one pixel through a program's steps at the program's declared width.
///
/// The sources are plane-major: the primary input's channels first, then each further plane's, so
/// the span holds `sourceCount() * channelCount()` values. The working pixel starts as a copy of
/// the primary input's channels and every step transforms it in place; the result is written to
/// `output`, which may not overlap `sources`.
///
/// Thread-safe and reentrant. Allocation-free, and free of any dependence on the execution context:
/// this is the definition of the arithmetic, not an operation over an image.
///
/// @param program the validated chain.
/// @param sources `sourceCount() * channelCount()` values, plane-major, as code values.
/// @param output `channelCount()` values, written on success and untouched on failure.
/// @return success; `ErrorCode::InvalidArgument` when either span is the wrong length or they
///         overlap; `ErrorCode::OutOfRange` when a lookup step's source sample is not a whole,
///         non-negative index into its table, or a sampled curve whose policy is
///         `PointwiseCurveDomainPolicy::Reject` is given a value outside its interval; or
///         `ErrorCode::Internal` for a step this build cannot execute, which validation should
///         have refused.
[[nodiscard]] AFTERNIGHTENGINE_API Status evaluatePointwisePixel(const PointwiseProgram& program,
                                                                 std::span<const double> sources,
                                                                 std::span<double> output);

} // namespace an
