// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Rewriting a pointwise chain into an equivalent one, bit for bit.
///
/// Canonicalization here means one thing and refuses to mean anything else: the rewritten chain
/// produces, for every input, the identical bits the original produced. Not "within tolerance", not
/// "within the accuracy contract". The reason is measured rather than theoretical - replacing a
/// division by a multiplication by the reciprocal is two units in the last place in Float32, passes
/// every float comparison, and becomes a visible code value once the result is quantized to sixteen
/// bits. A rewrite that is safe until someone writes the image out is not a rewrite.
///
/// That standard rules out almost every algebraic identity anyone would reach for, and saying which
/// and why is more useful than a list of what is implemented:
///
/// Two adjacent scales do not merge. Multiplication is not associative in floating point, so
/// `(x * a) * b` and `x * (a * b)` differ for some x even when `a * b` is exact.
///
/// A bias of zero is not dropped. `-0.0 + 0.0` is `+0.0`, so the step that looks like the identity
/// is the one that erases a signed zero.
///
/// Two adjacent channel matrices do not merge, because the product's entries are sums and every sum
/// is a rounding.
///
/// What survives is small and provable, and one rule is worth all of it. Collapsing an eligible
/// chain into a lookup table is exact by construction rather than by argument: the table is built by
/// running this engine's own scalar evaluator at every value the source can hold, so the rewritten
/// chain does not approximate the original, it memoizes it - with the one exception of contraction
/// that `canonicalizePointwiseProgram` states. That is only available because the input
/// is an integer of at most sixteen bits, which is also the case where it is worth the most.

#pragma once

#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgram.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>

namespace an
{

/// Combines consecutive compatible programs into one pointwise image pass.
///
/// Every stage must have the same representation, channels, arithmetic width, output precision
/// promise and contraction policy. The combined source count is the widest stage's count, and a
/// stage needing fewer sources simply does not read the extras. A stage's output domain must equal
/// the next stage's input domain. Tables are copied and their step indices are remapped, so the
/// returned program owns everything it executes. An integer lookup remains subject to the rule
/// that it can only be the first step of the combined chain. The combined program reads the first
/// stage's input domain and writes the last stage's output domain.
///
/// Thread-safe and reentrant.
///
/// @param programs two or more validated programs in execution order, borrowed for the call.
/// @return the combined program; `ErrorCode::InvalidArgument` for fewer than two stages;
///         `ErrorCode::IncompatibleArguments` for incompatible declarations; or the builder's
///         validation failure if the combined step sequence itself is not legal.
[[nodiscard]] AFTERNIGHTENGINE_API Result<PointwiseProgram>
fusePointwisePrograms(std::span<const PointwiseProgram> programs);

/// An equivalent program, rewritten where a rewrite is provably bit-preserving.
///
/// Applies, in this order: a scale by exactly one is dropped; adjacent clamps become the one clamp
/// that is their intersection, when that intersection admits a value; and a chain of per-channel
/// arithmetic over an enumerable integer source collapses into a single lookup table built from
/// this engine's own scalar evaluator. Tables no step reads afterwards are removed, so two chains
/// that canonicalize to the same computation also compare equal.
///
/// The result is canonical: running this on its own output returns a program equal to its input.
/// No rule reads `PointwiseProgram::contractionAllowed`, which permits a fused multiply-add in an
/// executor and is not a rewrite of the chain, and the result carries the flag unchanged. A table
/// is nevertheless built by evaluating the chain without contraction, so for a chain that allows
/// it and scales then biases, the table can differ in the last place from what the evaluator
/// computes with the fused multiply-add.
///
/// Thread-safe and reentrant.
///
/// @param program the validated chain to rewrite.
/// @return the equivalent program; the scalar evaluator's failure when a chain being collapsed
///         refuses one of the source's values, such as `ErrorCode::OutOfRange` from a sampled curve
///         whose policy rejects a code outside its interval; or `ErrorCode::Internal` if a rewrite
///         produced a chain this engine would not accept, which is a defect in a rule rather than in
///         the caller's input.
[[nodiscard]] AFTERNIGHTENGINE_API Result<PointwiseProgram>
canonicalizePointwiseProgram(const PointwiseProgram& program);

/// Whether a chain would collapse into a single lookup table.
///
/// Answers the eligibility question on its own, because a caller deciding whether to plan a chain
/// as a table wants the answer without paying for the table. Eligible means: one source plane, an
/// unsigned eight- or sixteen-bit source representation, no channel matrix, which reads the other
/// channels, and none of the steps that read a source plane - mask, blend, add, subtract, multiply,
/// divide, minimum, maximum and compare - even though with one plane bound they could only read the
/// input itself.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param program the validated chain.
/// @return true when `canonicalizePointwiseProgram` would produce a single lookup step.
[[nodiscard]] AFTERNIGHTENGINE_API bool pointwiseProgramCollapsesToLut(const PointwiseProgram& program) noexcept;

} // namespace an
