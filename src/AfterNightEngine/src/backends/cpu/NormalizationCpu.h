// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the compiled global-normalization rows, one pair per instruction set.

#pragma once

#include "kernels/reference/AlgorithmKernels.h"

#include <AfterNightEngine/Runtime/CpuCapabilities.h>

namespace an
{

/// The two global-normalization rows compiled for one instruction set.
struct NormalizationCpuRuns
{
    /// The Float32 row.
    NormalizationFloat32RunFn float32 = nullptr;

    /// The Float64 row.
    NormalizationFloat64RunFn float64 = nullptr;
};

/// The global-normalization rows compiled for one instruction set.
///
/// @param instructionSet which set.
/// @return the compiled rows, or null when this build has none for that set.
[[nodiscard]] const NormalizationCpuRuns* simdNormalizationRuns(InstructionSet instructionSet) noexcept;

} // namespace an
