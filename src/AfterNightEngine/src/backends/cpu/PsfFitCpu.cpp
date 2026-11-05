// AfterNightEngine - the two point-spread-function evaluations one star fit iterates over,
// compiled once per packaged instruction set.
//
// The models and their parameterisation are those `algorithms/stars/PsfFit.cpp` adapts from Siril
// (https://siril.org, `siril/src/algos/PSF.c`), whose file states
// `Copyright (C) 2005-2011 Francois Meyer (dulle at free.fr)` and
// `Copyright (C) 2012-2026 team free-astro (see more in AUTHORS file)` and is distributed under the GNU
// General Public License, version 3 or any later version; this file is part of that derived work and is
// distributed under the same licence. The lane-wise accumulation is AfterNight's. The provenance note is
// `docs/dev/STAR_DETECTION_PROVENANCE.md`.
//
// Its own translation unit rather than a family in the shared vector table, for the reason the
// demosaic estimates each have one: thirty-six accumulators of a seven-parameter fit held in vector
// registers is a large body, and a large body in the shared unit makes that unit's object size
// unattributable. Here the cost of this kernel, at the floor and with the ladder, is a line of its
// own in the budget record.
//
// Unlike most compiled rows here, the vector body is not the scalar authority compiled with
// different flags. It cannot be: the exponential is the whole cost of the evaluation, and a
// vector exponential is a different polynomial from the platform's scalar one. So the fit's own
// scalar text stays the authority, these are compared against it by a conformance case at a stated
// distance rather than for equality, and the distance is the exponential's - a few units in the
// last place of a residual, which moves no fitted parameter at any level a star is measured to.
//
// The Jacobian is never materialised. Both bodies accumulate what the solver needs in one pass:
// the trial body accumulates the residual sum alone, and the accepted body additionally accumulates
// the upper triangle of the normal-equation matrix and the gradient. Whatever is left after the
// last full vector goes through the same scalar text the fit itself uses as its authority.

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "backends/cpu/PsfFitCpu.h"

#ifndef HWY_DISABLED_TARGETS
#define HWY_DISABLED_TARGETS (~(HWY_AVX2 | HWY_AVX3))
#endif

#define HWY_HEADER_ONLY

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "backends/cpu/PsfFitCpu.cpp"
#include <hwy/foreach_target.h> // IWYU pragma: keep

#include <hwy/highway.h>

#include <hwy/contrib/math/math-inl.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

HWY_BEFORE_NAMESPACE();

namespace an
{
namespace psf_fit_simd
{
namespace HWY_NAMESPACE
{

namespace hn = hwy::HWY_NAMESPACE;

// The scalar authority for both evaluations, shared verbatim with the fit that owns it.
#include "kernels/reference/PsfFitRow.inl"

/// One model's residual sum of squares over a compacted sample run.
///
/// @param samples the run: values and both coordinates, already free of saturated samples.
/// @param terms the model, already derived from the solver's parameter vector.
/// @return the sum over the run of the squared difference between model and sample.
[[nodiscard]] double psfResidualRun(const PsfSampleRun& samples, const PsfModelTerms& terms) noexcept
{
    const hn::ScalableTag<double> tag;
    const std::uint64_t lanes = static_cast<std::uint64_t>(hn::Lanes(tag));
    const std::uint64_t count = samples.count;
    const auto centreX = hn::Set(tag, terms.centreX);
    const auto centreY = hn::Set(tag, terms.centreY);
    const auto angleCosine = hn::Set(tag, terms.angleCosine);
    const auto angleSine = hn::Set(tag, terms.angleSine);
    const auto inverseScaleX = hn::Set(tag, terms.inverseScaleX);
    const auto inverseScaleY = hn::Set(tag, terms.inverseScaleY);
    const auto amplitude = hn::Set(tag, terms.amplitude);
    const auto background = hn::Set(tag, terms.background);
    const auto one = hn::Set(tag, 1.0);
    const auto negatedBeta = hn::Set(tag, -terms.beta);
    auto accumulated = hn::Zero(tag);
    std::uint64_t index = 0u;
    for (; index + lanes <= count; index += lanes)
    {
        const auto dx = hn::Sub(hn::LoadU(tag, samples.x + index), centreX);
        const auto dy = hn::Sub(hn::LoadU(tag, samples.y + index), centreY);
        const auto tx = hn::Sub(hn::Mul(angleCosine, dx), hn::Mul(angleSine, dy));
        const auto ty = hn::Add(hn::Mul(angleSine, dx), hn::Mul(angleCosine, dy));
        const auto quadratic =
            hn::Add(hn::Mul(hn::Mul(tx, tx), inverseScaleX), hn::Mul(hn::Mul(ty, ty), inverseScaleY));
        const auto shape = terms.moffat ? hn::Exp(tag, hn::Mul(negatedBeta, hn::Log(tag, hn::Add(one, quadratic))))
                                        : hn::Exp(tag, hn::Neg(quadratic));
        const auto residual =
            hn::Sub(hn::Add(hn::Mul(amplitude, shape), background), hn::LoadU(tag, samples.values + index));
        accumulated = hn::MulAdd(residual, residual, accumulated);
    }
    return hn::ReduceSum(tag, accumulated) + psfResidualScalarRun(samples, index, terms);
}

/// One model's residual sum, normal-equation matrix and gradient over a compacted sample run.
///
/// @param samples the run.
/// @param terms the model.
/// @param out where the three accumulations go. Every entry it writes is overwritten, so the
///        caller need not clear it.
void psfNormalEquationsRun(const PsfSampleRun& samples, const PsfModelTerms& terms, PsfNormalEquations& out) noexcept
{
    const hn::ScalableTag<double> tag;
    const std::uint64_t lanes = static_cast<std::uint64_t>(hn::Lanes(tag));
    const std::uint64_t count = samples.count;
    const std::size_t parameters = static_cast<std::size_t>(terms.parameterCount);
    const auto centreX = hn::Set(tag, terms.centreX);
    const auto centreY = hn::Set(tag, terms.centreY);
    const auto angleCosine = hn::Set(tag, terms.angleCosine);
    const auto angleSine = hn::Set(tag, terms.angleSine);
    const auto inverseScaleX = hn::Set(tag, terms.inverseScaleX);
    const auto inverseScaleY = hn::Set(tag, terms.inverseScaleY);
    const auto inverseRatio = hn::Set(tag, terms.inverseRatio);
    const auto amplitude = hn::Set(tag, terms.amplitude);
    const auto background = hn::Set(tag, terms.background);
    const auto one = hn::Set(tag, 1.0);
    const auto negatedBeta = hn::Set(tag, -terms.beta);
    const auto beta = hn::Set(tag, terms.beta);
    const auto twiceAmplitude = hn::Set(tag, 2.0 * terms.amplitude);
    const auto scaleDifference = hn::Set(tag, terms.inverseScaleX - terms.inverseScaleY);
    const auto roundnessFactor = hn::Set(tag, -terms.ratioSine * terms.inverseScaleY * terms.inverseRatio);
    const auto exponentFactor = hn::Set(tag, terms.amplitude * terms.betaDerivativeScale);

    auto residualSum = hn::Zero(tag);
    hn::Vec<decltype(tag)> matrix[kPsfMaxFitParameters][kPsfMaxFitParameters];
    hn::Vec<decltype(tag)> gradient[kPsfMaxFitParameters];
    for (std::size_t row = 0u; row < parameters; ++row)
    {
        gradient[row] = hn::Zero(tag);
        for (std::size_t column = row; column < parameters; ++column)
        {
            matrix[row][column] = hn::Zero(tag);
        }
    }

    std::uint64_t index = 0u;
    for (; index + lanes <= count; index += lanes)
    {
        const auto dx = hn::Sub(hn::LoadU(tag, samples.x + index), centreX);
        const auto dy = hn::Sub(hn::LoadU(tag, samples.y + index), centreY);
        const auto tx = hn::Sub(hn::Mul(angleCosine, dx), hn::Mul(angleSine, dy));
        const auto ty = hn::Add(hn::Mul(angleSine, dx), hn::Mul(angleCosine, dy));
        const auto scaledX = hn::Mul(tx, inverseScaleX);
        const auto scaledY = hn::Mul(ty, inverseScaleY);
        const auto quadratic = hn::Add(hn::Mul(tx, scaledX), hn::Mul(ty, scaledY));
        const auto base = hn::Add(one, quadratic);
        const auto logarithm = terms.moffat ? hn::Log(tag, base) : hn::Zero(tag);
        const auto shape =
            terms.moffat ? hn::Exp(tag, hn::Mul(negatedBeta, logarithm)) : hn::Exp(tag, hn::Neg(quadratic));
        const auto residual =
            hn::Sub(hn::Add(hn::Mul(amplitude, shape), background), hn::LoadU(tag, samples.values + index));
        residualSum = hn::MulAdd(residual, residual, residualSum);

        // The Gaussian differentiates the exponential and the Moffat differentiates the power, and
        // the two collapse onto the same six expressions once the common factor is named: twice the
        // amplitude times the shape for one, and twice the amplitude times beta times the shape
        // over its base for the other.
        const auto common = terms.moffat ? hn::Mul(hn::Mul(twiceAmplitude, beta), hn::Div(shape, base))
                                         : hn::Mul(twiceAmplitude, shape);
        const auto ratioY = hn::Mul(hn::Mul(ty, inverseScaleX), inverseRatio);
        hn::Vec<decltype(tag)> jacobian[kPsfMaxFitParameters];
        jacobian[0] = one;
        jacobian[1] = shape;
        jacobian[2] = hn::Mul(common, hn::Add(hn::Mul(scaledX, angleCosine), hn::Mul(scaledY, angleSine)));
        jacobian[3] = hn::Mul(common, hn::Sub(hn::Mul(scaledY, angleCosine), hn::Mul(scaledX, angleSine)));
        jacobian[4] =
            hn::Mul(hn::Mul(common, hn::Set(tag, 0.5)), hn::Add(hn::Mul(scaledX, scaledX), hn::Mul(ratioY, ratioY)));
        jacobian[5] = hn::Mul(hn::Mul(common, hn::Set(tag, 0.5)), hn::Mul(roundnessFactor, hn::Mul(ty, ty)));
        jacobian[6] = hn::Mul(common, hn::Mul(tx, hn::Mul(ty, scaleDifference)));
        if (parameters > 7u)
        {
            jacobian[7] = hn::Mul(exponentFactor, hn::Mul(logarithm, shape));
        }
        for (std::size_t row = 0u; row < parameters; ++row)
        {
            gradient[row] = hn::MulAdd(jacobian[row], residual, gradient[row]);
            for (std::size_t column = row; column < parameters; ++column)
            {
                matrix[row][column] = hn::MulAdd(jacobian[row], jacobian[column], matrix[row][column]);
            }
        }
    }

    out.residualSumOfSquares = hn::ReduceSum(tag, residualSum);
    for (std::size_t row = 0u; row < parameters; ++row)
    {
        out.gradient[row] = hn::ReduceSum(tag, gradient[row]);
        for (std::size_t column = row; column < parameters; ++column)
        {
            out.matrix[row][column] = hn::ReduceSum(tag, matrix[row][column]);
        }
    }

    psfNormalEquationsScalarAccumulate(samples, index, terms, out);
}

} // namespace HWY_NAMESPACE
} // namespace psf_fit_simd
} // namespace an

HWY_AFTER_NAMESPACE();

#if HWY_ONCE

namespace an
{
namespace psf_fit_simd
{
namespace
{

constexpr PsfResidualRunFn kResidualAvx2 = HWY_CHOOSE_AVX2(psfResidualRun);
constexpr PsfNormalEquationsRunFn kNormalEquationsAvx2 = HWY_CHOOSE_AVX2(psfNormalEquationsRun);

#if (HWY_TARGETS & HWY_AVX3) != 0
constexpr PsfResidualRunFn kResidualAvx3 = HWY_CHOOSE_AVX3(psfResidualRun);
constexpr PsfNormalEquationsRunFn kNormalEquationsAvx3 = HWY_CHOOSE_AVX3(psfNormalEquationsRun);
#endif

} // namespace
} // namespace psf_fit_simd

PsfResidualRunFn simdPsfResidualRun(InstructionSet instructionSet) noexcept
{
    switch (instructionSet)
    {
    case InstructionSet::Avx2:
        return psf_fit_simd::kResidualAvx2;
    case InstructionSet::Avx512Skylake:
#if (HWY_TARGETS & HWY_AVX3) != 0
        return psf_fit_simd::kResidualAvx3;
#else
        return nullptr;
#endif
    case InstructionSet::Automatic:
    case InstructionSet::Scalar:
    case InstructionSet::Sse2:
    case InstructionSet::Sse42:
    case InstructionSet::Avx:
    case InstructionSet::Neon:
        return nullptr;
    }
    return nullptr;
}

PsfNormalEquationsRunFn simdPsfNormalEquationsRun(InstructionSet instructionSet) noexcept
{
    switch (instructionSet)
    {
    case InstructionSet::Avx2:
        return psf_fit_simd::kNormalEquationsAvx2;
    case InstructionSet::Avx512Skylake:
#if (HWY_TARGETS & HWY_AVX3) != 0
        return psf_fit_simd::kNormalEquationsAvx3;
#else
        return nullptr;
#endif
    case InstructionSet::Automatic:
    case InstructionSet::Scalar:
    case InstructionSet::Sse2:
    case InstructionSet::Sse42:
    case InstructionSet::Avx:
    case InstructionSet::Neon:
        return nullptr;
    }
    return nullptr;
}

} // namespace an

#endif // HWY_ONCE
