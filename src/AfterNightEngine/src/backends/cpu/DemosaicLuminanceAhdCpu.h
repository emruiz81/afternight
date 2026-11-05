// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the compiled luminance-only adaptive homogeneity-directed estimate, one per instruction set.

#pragma once

#include "kernels/reference/DemosaicKernels.h"

#include <AfterNightEngine/Runtime/CpuCapabilities.h>

namespace an
{

/// The luminance-only adaptive homogeneity-directed estimate compiled for one instruction set.
///
/// @param instructionSet which set.
/// @return the compiled run, or null when this build has none for that set.
[[nodiscard]] DemosaicRunFn simdLuminanceAhdRun(InstructionSet instructionSet) noexcept;

} // namespace an
