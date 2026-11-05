// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the compiled colour-adjustment rows, one set per instruction set.

#pragma once

#include "kernels/reference/ColorAdjustmentKernels.h"

#include <AfterNightEngine/Runtime/CpuCapabilities.h>

namespace an
{

/// The colour-adjustment rows compiled for one instruction set.
struct ColorAdjustmentCpuRuns
{
    /// The Float32 LCh adjustment row.
    LchAdjustFloat32RunFn lchAdjustFloat32 = nullptr;
};

/// The colour-adjustment rows compiled for one instruction set.
///
/// @param instructionSet which set.
/// @return the compiled rows, or null when this build has none for that set.
[[nodiscard]] const ColorAdjustmentCpuRuns* simdColorAdjustmentRuns(InstructionSet instructionSet) noexcept;

} // namespace an
