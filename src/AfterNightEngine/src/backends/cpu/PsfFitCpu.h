// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the compiled point-spread-function evaluations, one per instruction set.

#pragma once

#include "kernels/reference/AlgorithmKernels.h"

#include <AfterNightEngine/Runtime/CpuCapabilities.h>

namespace an
{

/// The residual evaluation compiled for one instruction set.
///
/// @param instructionSet which set.
/// @return the compiled run, or null when this build has none for that set.
[[nodiscard]] PsfResidualRunFn simdPsfResidualRun(InstructionSet instructionSet) noexcept;

/// The normal-equation evaluation compiled for one instruction set.
///
/// @param instructionSet which set.
/// @return the compiled run, or null when this build has none for that set.
[[nodiscard]] PsfNormalEquationsRunFn simdPsfNormalEquationsRun(InstructionSet instructionSet) noexcept;

} // namespace an
