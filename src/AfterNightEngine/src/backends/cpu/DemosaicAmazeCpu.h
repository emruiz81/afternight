// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the compiled AMaZE estimate, one per instruction set.

#pragma once

#include "kernels/reference/DemosaicKernels.h"

#include <AfterNightEngine/Runtime/CpuCapabilities.h>

namespace an
{

/// The aliasing-minimised estimate compiled for one instruction set.
///
/// @param instructionSet which set.
/// @return the compiled run, or null when this build has none for that set.
[[nodiscard]] DemosaicRunFn simdAmazeRun(InstructionSet instructionSet) noexcept;

/// What the compiled AMaZE body says it was built for.
///
/// @param instructionSet which set the caller believes it selected.
/// @return the set the code declares, or `InstructionSet::Automatic` when this build has none.
[[nodiscard]] InstructionSet simdAmazeIsa(InstructionSet instructionSet) noexcept;

} // namespace an
