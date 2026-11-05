// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the dispatch tables whose kernels are compiled for one instruction set.
//
// `backends/cpu/CpuKernelTable.h` says what a table is. This says which of them this build actually
// contains, and the answer is not a constant anybody wrote down: it is read out of the vector
// library's own compiled target set. That matters more than it sounds, because the set is not the
// same on the two compilers this engine ships on - the portable abstraction refuses to compile
// AVX-512 under MSVC, and says so in its own headers - so a build that claimed both would be
// claiming a kernel that is not in the binary.
//
// **The tables are constants, not something built at startup.** Each one is constant-initialised in
// the translation unit that compiles its kernels, so there is no initialisation for a forced
// selection to race against and no order dependency between this and the feature detection. What
// crosses this header is a pointer to one of them, or null.
//
// **Nothing here executes a kernel.** Asking which tables exist happens on every host, including one
// that cannot run the instructions in them, so the accessor below is ordinary baseline code that
// only compares an enumerator. The kernels themselves are reached through the returned table, and
// only after `Runtime/CpuCapabilities.h` has agreed the host can run them.

#pragma once

#include "backends/cpu/CpuKernelTable.h"

#include <AfterNightEngine/Runtime/CpuCapabilities.h>

namespace an
{

/// The table whose kernels were compiled for one instruction set.
///
/// Thread-safe, reentrant, and allocation-free. Safe to call on a host that cannot run the
/// instruction set asked about: the answer is about this binary, not about this processor.
///
/// @param instructionSet which set. `InstructionSet::Scalar` is deliberately not one of them - the
///        scalar table is the reference implementation and is owned by `CpuKernelTable.cpp`.
/// @return the table, which outlives every caller, or null when this build compiled no kernels for
///         that set.
[[nodiscard]] const CpuKernelTable* simdCpuKernelTable(InstructionSet instructionSet) noexcept;

} // namespace an
