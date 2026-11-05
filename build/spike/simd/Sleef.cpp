// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// SLEEF, with plain AVX2 intrinsics for the load/store around it.
//
// SLEEF is not a SIMD abstraction and does not compete with Highway or xsimd: it supplies vector
// transcendentals and nothing else. So this candidate implements only the gamma kernel, and the
// remaining four are reported as not provided rather than filled in with something SLEEF did not
// contribute. Pairing it with raw intrinsics here is deliberate - it is what using SLEEF alongside
// any abstraction, or none, actually looks like.
//
// The u10 variants are the 1.0-ULP accuracy tier. SLEEF also ships u35 (3.5 ULP) versions that are
// faster; the report records which tier was measured, because comparing a 3.5-ULP implementation
// against a correctly-rounded one and calling the difference a speedup is exactly the mistake this
// spike exists to avoid.

#include "Kernels.h"

#include <immintrin.h>
#include <sleef.h>

#include <cmath>

namespace spike
{
namespace
{

void gamma(const float* src, float* dst, std::size_t count, float exponent)
{
    constexpr std::size_t kLanes = 8;
    const __m256 power = _mm256_set1_ps(exponent);

    std::size_t i = 0;
    for (; i + kLanes <= count; i += kLanes)
    {
        const __m256 value = _mm256_loadu_ps(src + i);
        _mm256_storeu_ps(dst + i, Sleef_powf8_u10avx2(value, power));
    }
    for (; i < count; ++i)
    {
        dst[i] = std::pow(src[i], exponent);
    }
}

// Only gamma is provided; the four null members are reported as "not provided".
const KernelSet kSet{"sleef+intrinsics", nullptr, nullptr, nullptr, nullptr, &gamma};

} // namespace

const KernelSet& sleefKernels()
{
    return kSet;
}

} // namespace spike
