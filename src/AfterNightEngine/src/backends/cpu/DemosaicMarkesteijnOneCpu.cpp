// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the compiled one-pass Markesteijn estimate, one per instruction set.
//
// The estimate is the port in `kernels/reference/DemosaicKernelsMarkesteijn.cpp`, derived from Frank
// Markesteijn's X-Trans algorithm as adapted to RawTherapee by Ingo Weyrich in 2014 and carried by
// librtprocess (`demosaic/markesteijn.cc`, which names no copyright holder), distributed under the GNU
// General Public License, version 3 or any later version; this file is part of that derived work and is
// distributed under the same licence. What is here is AfterNight's: the lane-wise spelling of the
// reference's passes in its order of evaluation. The provenance note is
// `docs/dev/DEMOSAIC_PROVENANCE.md`.
//
// The luminance and colour-difference transform and the directional curvature cover every
// candidate image, and are compiled once per target through `DemosaicMarkesteijnCpuTarget.inl`; a
// tile that asks for the perceptual space keeps that conversion in the scalar authority. The
// reconstruction and homogeneity choices remain shared with the scalar authority, so a lane does
// not acquire a different branch tree.

#include "backends/cpu/DemosaicMarkesteijnOneCpu.h"

#include "kernels/reference/DemosaicKernelVectorOps.h"

#include <cstdint>

#ifndef HWY_DISABLED_TARGETS
#define HWY_DISABLED_TARGETS (~(HWY_AVX2 | HWY_AVX3))
#endif

#define HWY_HEADER_ONLY

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "backends/cpu/DemosaicMarkesteijnOneCpu.cpp"
#include <hwy/foreach_target.h> // IWYU pragma: keep

#include <hwy/highway.h>

HWY_BEFORE_NAMESPACE();

#define AN_MARK_SIMD_NAMESPACE demosaic_markesteijn_one_simd
#define AN_MARK_ISA_NAME isaOfCompiledMarkesteijnOne
#define AN_MARK_RUN_NAME markesteijnOneRun
#define AN_MARK_PASSES 1u
#include "backends/cpu/DemosaicMarkesteijnCpuTarget.inl"
#undef AN_MARK_PASSES
#undef AN_MARK_RUN_NAME
#undef AN_MARK_ISA_NAME
#undef AN_MARK_SIMD_NAMESPACE

HWY_AFTER_NAMESPACE();

#if HWY_ONCE

namespace an
{
namespace demosaic_markesteijn_one_simd
{
namespace
{

constexpr DemosaicRunFn kMarkesteijnOneAvx2 = HWY_CHOOSE_AVX2(markesteijnOneRun);
constexpr InstructionSet (*kMarkesteijnOneAvx2Isa)() noexcept = HWY_CHOOSE_AVX2(isaOfCompiledMarkesteijnOne);

#if (HWY_TARGETS & HWY_AVX3) != 0
constexpr DemosaicRunFn kMarkesteijnOneAvx3 = HWY_CHOOSE_AVX3(markesteijnOneRun);
constexpr InstructionSet (*kMarkesteijnOneAvx3Isa)() noexcept = HWY_CHOOSE_AVX3(isaOfCompiledMarkesteijnOne);
#endif

} // namespace
} // namespace demosaic_markesteijn_one_simd

DemosaicRunFn simdMarkesteijnOneRun(InstructionSet instructionSet) noexcept
{
    switch (instructionSet)
    {
    case InstructionSet::Avx2:
        return demosaic_markesteijn_one_simd::kMarkesteijnOneAvx2;
    case InstructionSet::Avx512Skylake:
#if (HWY_TARGETS & HWY_AVX3) != 0
        return demosaic_markesteijn_one_simd::kMarkesteijnOneAvx3;
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

InstructionSet simdMarkesteijnOneIsa(InstructionSet instructionSet) noexcept
{
    switch (instructionSet)
    {
    case InstructionSet::Avx2:
        return demosaic_markesteijn_one_simd::kMarkesteijnOneAvx2Isa();
    case InstructionSet::Avx512Skylake:
#if (HWY_TARGETS & HWY_AVX3) != 0
        return demosaic_markesteijn_one_simd::kMarkesteijnOneAvx3Isa();
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
