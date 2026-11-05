// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the compiled bilinear estimate, one per instruction set.

#pragma once

#include "kernels/reference/DemosaicKernels.h"

#include <AfterNightEngine/Runtime/CpuCapabilities.h>

namespace an
{

/// The bilinear estimate compiled for one instruction set.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param instructionSet which set.
/// @return the compiled run, or null when this build has none for that set.
[[nodiscard]] DemosaicRunFn simdBilinearRun(InstructionSet instructionSet) noexcept;

/// What the compiled bilinear body says it was built for.
///
/// Read out of the compiled code rather than from the table that points at it, so a unit wired to
/// the wrong rung disagrees with itself and a selection test sees that before a wrong sample does.
///
/// @param instructionSet which set the caller believes it selected.
/// @return the set the code declares, or `InstructionSet::Automatic` when this build has none.
[[nodiscard]] InstructionSet simdBilinearIsa(InstructionSet instructionSet) noexcept;

} // namespace an
