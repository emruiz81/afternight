// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the compiled CPU conversion kernels the dispatch table points at.
//
// The same two shapes as the reference next door - a conversion run and a strided copy - and the
// same answers to the sample. What differs is what the loop pays for, and each difference is a cost
// the reference deliberately accepts because it is an oracle rather than a hot path:
//
// * **The policy is read once instead of per sample.** The reference reads five enumerators out of a
//   referenced object inside the loop and switches on one of them per sample; that switch is
//   unpredictable to a branch predictor for a run whose rounding never changes. These kernels test
//   the policy once and, when it is the approved default - or, from Float32 to UInt16, the
//   truncating policy - run a body with the five decisions folded in as constants.
// * **A pair of identical integer representations is a copy.** No arithmetic can change such a
//   sample, so nothing walks it a sample at a time. The floating pairs are deliberately not copied,
//   because a conversion of one still has to report the non-finite samples it met.
// * **A strided copy with a contiguous side is written as a run.** A repack always has one, so the
//   general two-stride body only serves a caller that asked for something else.
//
// The byte swap is not here: reversing the bytes of a run reads no meaning at all, so it belongs
// with the other width-parameterised layout kernels in `backends/cpu/SimdKernelTables.cpp`, where
// one vector instruction reverses a whole register.
//
// Anything the fast body does not cover falls through to the reference, which covers everything.
// That is the arrangement rather than a stopgap: a policy nobody uses in an inner loop should cost
// its author nothing to support, and correctness comes from having one implementation of the rules.

#pragma once

#include "kernels/reference/ConversionKernels.h"

#include <AfterNightEngine/Image/SampleType.h>

namespace an
{

/// The compiled conversion kernel for one ordered pair of representations.
///
/// Thread-safe, reentrant, and allocation-free: the table is an immutable constant with static
/// storage duration.
///
/// @param source what the samples are.
/// @param destination what they are to become.
/// @return the kernel, or null when either value is outside the enumeration.
[[nodiscard]] ConvertRunFn cpuConvertRun(SampleType source, SampleType destination) noexcept;

/// The compiled strided-copy kernel for one representation.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param sampleType what the samples are, which is where the element width comes from.
/// @return the kernel, or null when the value is outside the enumeration.
[[nodiscard]] StrideCopyRunFn cpuStrideCopyRun(SampleType sampleType) noexcept;

} // namespace an
