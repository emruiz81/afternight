// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the compiled generalized-stretch rows, one set per instruction set.

#pragma once

#include "kernels/reference/AlgorithmKernels.h"

#include <AfterNightEngine/Runtime/CpuCapabilities.h>

namespace an
{

/// The stretch rows compiled for one instruction set.
struct StretchCpuRuns
{
    /// The Float32 forward asinh row.
    AsinhFloat32RunFn asinhFloat32 = nullptr;

    /// The Float32 inverse asinh row.
    AsinhFloat32RunFn asinhInverseFloat32 = nullptr;

    /// The Float32 forward generalized-hyperbolic row.
    GeneralizedHyperbolicFloat32RunFn generalizedHyperbolicFloat32 = nullptr;

    /// The Float32 inverse generalized-hyperbolic row.
    GeneralizedHyperbolicFloat32RunFn generalizedHyperbolicInverseFloat32 = nullptr;

    /// The Float32 midtones transfer row.
    MidtonesTransferFloat32RunFn midtonesTransferFloat32 = nullptr;
};

/// The stretch rows compiled for one instruction set.
///
/// @param instructionSet which set.
/// @return the compiled rows, or null when this build has none for that set.
[[nodiscard]] const StretchCpuRuns* simdStretchRuns(InstructionSet instructionSet) noexcept;

} // namespace an
