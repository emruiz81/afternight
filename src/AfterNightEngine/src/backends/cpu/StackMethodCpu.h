// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the compiled column-combination rows, one pair per instruction set.

#pragma once

#include "kernels/reference/AlgorithmKernels.h"

#include <AfterNightEngine/Runtime/CpuCapabilities.h>

namespace an
{

/// The two column-combination rows compiled for one instruction set.
struct StackMethodCpuRuns
{
    /// The row over Float32 samples.
    StackColumnFloat32RunFn float32 = nullptr;

    /// The row over Float64 samples.
    StackColumnFloat64RunFn float64 = nullptr;
};

/// The column-combination rows compiled for one instruction set.
///
/// @param instructionSet which set.
/// @return the compiled rows, or null when this build has none for that set.
[[nodiscard]] const StackMethodCpuRuns* simdStackMethodRuns(InstructionSet instructionSet) noexcept;

} // namespace an
