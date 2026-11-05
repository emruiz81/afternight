// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the compiled three-pass Markesteijn estimate, one per instruction set.
//
// The estimate is the port in `kernels/reference/DemosaicKernelsMarkesteijn.cpp`, derived from Frank
// Markesteijn's X-Trans algorithm as adapted to RawTherapee by Ingo Weyrich in 2014 and carried by
// librtprocess (`demosaic/markesteijn.cc`, which names no copyright holder), distributed under the GNU
// General Public License, version 3 or any later version; this file is part of that derived work and is
// distributed under the same licence. What is here is AfterNight's: the lane-wise spelling of the
// reference's passes in its order of evaluation. The provenance note is
// `docs/dev/DEMOSAIC_PROVENANCE.md`.
//
// The three-pass method owns a separate target-instantiated object so the object each
// instruction-set rung costs it is measurable on its own. It shares the lane passes with the
// one-pass method, through `DemosaicMarkesteijnCpuTarget.inl`, and changes only the number of
// candidate directions constructed by the scalar authority.

#include "backends/cpu/DemosaicMarkesteijnThreeCpu.h"

#include "kernels/reference/DemosaicKernelVectorOps.h"

#include <cstdint>

#ifndef HWY_DISABLED_TARGETS
#define HWY_DISABLED_TARGETS (~(HWY_AVX2 | HWY_AVX3))
#endif

#define HWY_HEADER_ONLY

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "backends/cpu/DemosaicMarkesteijnThreeCpu.cpp"
#include <hwy/foreach_target.h> // IWYU pragma: keep

#include <hwy/highway.h>

HWY_BEFORE_NAMESPACE();

#define AN_MARK_SIMD_NAMESPACE demosaic_markesteijn_three_simd
#define AN_MARK_ISA_NAME isaOfCompiledMarkesteijnThree
#define AN_MARK_RUN_NAME markesteijnThreeRun
#define AN_MARK_PASSES 3u
#include "backends/cpu/DemosaicMarkesteijnCpuTarget.inl"
#undef AN_MARK_PASSES
#undef AN_MARK_RUN_NAME
#undef AN_MARK_ISA_NAME
#undef AN_MARK_SIMD_NAMESPACE

HWY_AFTER_NAMESPACE();

#if HWY_ONCE

namespace an
{
namespace demosaic_markesteijn_three_simd
{
namespace
{

constexpr DemosaicRunFn kMarkesteijnThreeAvx2 = HWY_CHOOSE_AVX2(markesteijnThreeRun);
constexpr InstructionSet (*kMarkesteijnThreeAvx2Isa)() noexcept = HWY_CHOOSE_AVX2(isaOfCompiledMarkesteijnThree);

#if (HWY_TARGETS & HWY_AVX3) != 0
constexpr DemosaicRunFn kMarkesteijnThreeAvx3 = HWY_CHOOSE_AVX3(markesteijnThreeRun);
constexpr InstructionSet (*kMarkesteijnThreeAvx3Isa)() noexcept = HWY_CHOOSE_AVX3(isaOfCompiledMarkesteijnThree);
#endif

} // namespace
} // namespace demosaic_markesteijn_three_simd

DemosaicRunFn simdMarkesteijnThreeRun(InstructionSet instructionSet) noexcept
{
    switch (instructionSet)
    {
    case InstructionSet::Avx2:
        return demosaic_markesteijn_three_simd::kMarkesteijnThreeAvx2;
    case InstructionSet::Avx512Skylake:
#if (HWY_TARGETS & HWY_AVX3) != 0
        return demosaic_markesteijn_three_simd::kMarkesteijnThreeAvx3;
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

InstructionSet simdMarkesteijnThreeIsa(InstructionSet instructionSet) noexcept
{
    switch (instructionSet)
    {
    case InstructionSet::Avx2:
        return demosaic_markesteijn_three_simd::kMarkesteijnThreeAvx2Isa();
    case InstructionSet::Avx512Skylake:
#if (HWY_TARGETS & HWY_AVX3) != 0
        return demosaic_markesteijn_three_simd::kMarkesteijnThreeAvx3Isa();
#else
        return InstructionSet::Automatic;
#endif
    case InstructionSet::Automatic:
    case InstructionSet::Scalar:
    case InstructionSet::Sse2:
    case InstructionSet::Sse42:
    case InstructionSet::Avx:
    case InstructionSet::Neon:
        return InstructionSet::Automatic;
    }
    return InstructionSet::Automatic;
}

} // namespace an

#endif // HWY_ONCE
