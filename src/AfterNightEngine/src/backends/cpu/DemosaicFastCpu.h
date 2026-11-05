// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the compiled gradient-weighted estimate, one per instruction set.

#pragma once

#include "kernels/reference/DemosaicKernels.h"

#include <AfterNightEngine/Runtime/CpuCapabilities.h>

namespace an
{

/// The gradient-weighted estimate compiled for one instruction set.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param instructionSet which set.
/// @return the compiled run, or null when this build has none for that set.
[[nodiscard]] DemosaicRunFn simdFastRun(InstructionSet instructionSet) noexcept;

/// What the compiled body says it was built for.
///
/// @param instructionSet which set the caller believes it selected.
/// @return the set the code declares, or `InstructionSet::Automatic` when this build has none.
[[nodiscard]] InstructionSet simdFastIsa(InstructionSet instructionSet) noexcept;

} // namespace an
