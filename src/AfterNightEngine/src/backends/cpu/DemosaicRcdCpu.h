// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the compiled ratio-corrected estimate, one per instruction set.

#pragma once

#include "kernels/reference/DemosaicKernels.h"

#include <AfterNightEngine/Runtime/CpuCapabilities.h>

namespace an
{

/// The ratio-corrected estimate compiled for one instruction set.
///
/// @param instructionSet which set.
/// @return the compiled run, or null when this build has none for that set.
[[nodiscard]] DemosaicRunFn simdRcdRun(InstructionSet instructionSet) noexcept;

/// What the compiled ratio-corrected body says it was built for.
///
/// @param instructionSet which set the caller believes it selected.
/// @return the set the code declares, or `InstructionSet::Automatic` when this build has none.
[[nodiscard]] InstructionSet simdRcdIsa(InstructionSet instructionSet) noexcept;

} // namespace an
