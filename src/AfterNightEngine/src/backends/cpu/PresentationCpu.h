// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the compiled rows of a linear presentation of narrow codes, one pair per instruction set.

#pragma once

#include "kernels/reference/AlgorithmKernels.h"

#include <AfterNightEngine/Runtime/CpuCapabilities.h>

namespace an
{

/// The presentation rows compiled for one instruction set.
struct PresentationCpuRuns
{
    /// The row of eight-bit codes.
    PresentationCodeRowRunFn uint8 = nullptr;

    /// The row of sixteen-bit codes.
    PresentationCodeRowRunFn uint16 = nullptr;
};

/// The presentation rows compiled for one instruction set.
///
/// @param instructionSet which set.
/// @return the compiled rows, or null when this build has none for that set.
[[nodiscard]] const PresentationCpuRuns* simdPresentationRuns(InstructionSet instructionSet) noexcept;

} // namespace an
