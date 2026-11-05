// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the compiled coordinate-warp row, one per instruction set.

#pragma once

#include "kernels/reference/AlgorithmKernels.h"

#include <AfterNightEngine/Runtime/CpuCapabilities.h>

namespace an
{

/// The warp rows compiled for one instruction set.
struct WarpCpuRuns
{
    /// The Float32 cubic row.
    WarpCubicRowRunFn cubicFloat32 = nullptr;

    /// The Float32 Lanczos-4 row.
    WarpLanczos4RowRunFn lanczos4Float32 = nullptr;
};

/// The warp rows compiled for one instruction set.
///
/// @param instructionSet which set.
/// @return the compiled rows, or null when this build has none for that set.
[[nodiscard]] const WarpCpuRuns* simdWarpRuns(InstructionSet instructionSet) noexcept;

} // namespace an
