// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the compiled image-calibration rows, one pair per instruction set.

#pragma once

#include "kernels/reference/AlgorithmKernels.h"

#include <AfterNightEngine/Runtime/CpuCapabilities.h>

namespace an
{

/// The two calibration rows compiled for one instruction set.
struct CalibrationCpuRuns
{
    /// The Float32 row.
    CalibrationFloat32RunFn float32 = nullptr;

    /// The Float64 row.
    CalibrationFloat64RunFn float64 = nullptr;
};

/// The calibration rows compiled for one instruction set.
///
/// @param instructionSet which set.
/// @return the compiled rows, or null when this build has none for that set.
[[nodiscard]] const CalibrationCpuRuns* simdCalibrationRuns(InstructionSet instructionSet) noexcept;

} // namespace an
