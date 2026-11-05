// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - which demosaic estimates this build has compiled vector kernels for.
//
// One translation unit per method, so the object a rung of the instruction-set ladder costs stays
// attributable to the method that spent it: the budget tooling compiles a unit for one target at a
// time and reads the difference, and a single file holding every method would report one number for
// seven decisions.
//
// Seven methods - bilinear, fast, RCD, AMaZE, fast X-Trans and one- and three-pass Markesteijn -
// have a compiled entry for every target the build packages, because a caller that requires the CPU
// backend for one of them has to run a CPU body rather than be narrowed to the reference. Methods
// with data-dependent branch trees share those decisions with the scalar authority and substitute
// target-specific lane passes at explicit seams; methods whose whole estimate is lane-stable keep a
// complete Highway body. LMMSE, VNG4 and luminance AHD are scalar authorities only: their uses are
// specialised, and none has shown a hot path worth a second body. A missing target still answers
// null rather than borrowing another rung.

#pragma once

#include "kernels/reference/DemosaicKernels.h"

#include <AfterNightEngine/Runtime/CpuCapabilities.h>

namespace an
{

/// The compiled tile estimate one instruction set has for one method.
///
/// Thread-safe, reentrant, and allocation-free. Safe to call on a host that cannot run the
/// instruction set asked about: the answer is about this binary, not about this processor.
///
/// @param instructionSet which set. The scalar reference is not one of them.
/// @param method which estimate.
/// @return the compiled run, or null when this build has no vector kernel for that pairing - always
///         for LMMSE, VNG4 and luminance AHD.
[[nodiscard]] DemosaicRunFn simdDemosaicRun(InstructionSet instructionSet, DemosaicMethod method) noexcept;

} // namespace an
