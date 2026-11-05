// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the compiled local-normalization rows, one pair per instruction set.

#pragma once

#include "kernels/reference/AlgorithmKernels.h"

#include <AfterNightEngine/Runtime/CpuCapabilities.h>

namespace an
{

/// The two local-normalization rows compiled for one instruction set.
struct LocalNormalizationCpuRuns
{
    /// The Float32 row.
    LocalNormalizationFloat32RunFn float32 = nullptr;

    /// The Float64 row.
    LocalNormalizationFloat64RunFn float64 = nullptr;
};

/// The local-normalization rows compiled for one instruction set.
///
/// @param instructionSet which set.
/// @return the compiled rows, or null when this build has none for that set.
[[nodiscard]] const LocalNormalizationCpuRuns* simdLocalNormalizationRuns(InstructionSet instructionSet) noexcept;

} // namespace an
