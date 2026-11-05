// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the compiled stack-accumulation rows, one pair per instruction set.

#pragma once

#include "kernels/reference/AlgorithmKernels.h"

#include <AfterNightEngine/Runtime/CpuCapabilities.h>

namespace an
{

/// The two stack-accumulation rows compiled for one instruction set.
struct StackAccumulatorCpuRuns
{
    /// The row over Float32 samples.
    StackAccumulateFloat32RunFn float32 = nullptr;

    /// The row over Float64 samples.
    StackAccumulateFloat64RunFn float64 = nullptr;

    /// The rows over every band of a tile at once.
    StackAccumulateBandsRuns bands{};
};

/// The stack-accumulation rows compiled for one instruction set.
///
/// @param instructionSet which set.
/// @return the compiled rows, or null when this build has none for that set.
[[nodiscard]] const StackAccumulatorCpuRuns* simdStackAccumulatorRuns(InstructionSet instructionSet) noexcept;

} // namespace an
