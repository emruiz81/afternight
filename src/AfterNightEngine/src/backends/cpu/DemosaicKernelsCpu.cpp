// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - which demosaic method has a compiled kernel, in one place.
//
// Each method that has one lives in its own translation unit, and this is the only file that knows
// the whole set - so adding a method's vector kernel is a new unit and one case here, and the object
// each rung of the ladder costs stays attributable to the method that spent it.
//
// The switch has no default label, so a method added to the family without a decision about its
// vector kernel is a compile failure here rather than a silent absence.

#include "backends/cpu/DemosaicKernelsCpu.h"

#include "backends/cpu/DemosaicAmazeCpu.h"
#include "backends/cpu/DemosaicBilinearCpu.h"
#include "backends/cpu/DemosaicFastCpu.h"
#include "backends/cpu/DemosaicLuminanceAhdCpu.h"
#include "backends/cpu/DemosaicMarkesteijnOneCpu.h"
#include "backends/cpu/DemosaicMarkesteijnThreeCpu.h"
#include "backends/cpu/DemosaicRcdCpu.h"
#include "backends/cpu/DemosaicXTransFastCpu.h"

namespace an
{

DemosaicRunFn simdDemosaicRun(InstructionSet instructionSet, DemosaicMethod method) noexcept
{
    switch (method)
    {
    case DemosaicMethod::Bilinear:
        return simdBilinearRun(instructionSet);
    case DemosaicMethod::Fast:
        return simdFastRun(instructionSet);
    case DemosaicMethod::Rcd:
        return simdRcdRun(instructionSet);
    case DemosaicMethod::Amaze:
        return simdAmazeRun(instructionSet);
    case DemosaicMethod::XTransFast:
        return simdXTransFastRun(instructionSet);
    case DemosaicMethod::MarkesteijnOnePass:
        return simdMarkesteijnOneRun(instructionSet);
    case DemosaicMethod::MarkesteijnThreePass:
        return simdMarkesteijnThreeRun(instructionSet);
    case DemosaicMethod::LuminanceAhd:
        return simdLuminanceAhdRun(instructionSet);
    case DemosaicMethod::Lmmse:
    case DemosaicMethod::Vng4:
        // These specialised choices start with their scalar authorities. Automatic dispatch falls
        // back truthfully to Reference; a required CPU backend is refused until profiling earns a
        // target-specific kernel.
        return nullptr;
    }
    return nullptr;
}

} // namespace an
