// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the compiled rejection rows, one set per instruction set.
//
// One row per frame count from two to eight judges and combines a whole row of a uniform Float32
// band under any of six rejection families: sigma clipping, median-sigma replacement, winsorized
// sigma clipping, linear-fit clipping, percentile clipping and the generalized extreme
// Studentized deviate test.

#pragma once

#include "kernels/reference/AlgorithmKernels.h"

#include <AfterNightEngine/Runtime/CpuCapabilities.h>

namespace an
{

/// The compiled Float32 rejection row for one instruction set and frame count.
///
/// Looked up from the unit that compiles it rather than carried as a shared table entry, so the
/// unit's object size stays attributable to the family.
///
/// @param instructionSet the rung the execution's table selected.
/// @param frames the frames of the stack.
/// @return the row, or null for a rung or a frame count this build does not compile one for.
[[nodiscard]] StackRejectionFloat32RowFn simdStackRejectionFloat32Row(InstructionSet instructionSet,
                                                                      std::uint32_t frames) noexcept;

} // namespace an
