// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the vector abstraction's version, and nothing else.
//
// It is a translation unit of its own rather than a few lines inside the feature detection beside it,
// and the reason is measured rather than stylistic: `hwy/base.h` is the smallest part of that library
// that carries its version constants, and including it in `CpuCapabilities.cpp` took that unit from
// 40,151 preprocessed lines to 99,044 and from 164 ms to 393 ms. Asking a library what version it is
// has no business making the unit that detects what the processor supports twice as heavy, so the
// question gets its own unit, where the cost is the cost of the question.
//
// This unit compiles at the packaged floor like every other file of the engine. It includes none of
// the vector abstraction's target machinery - no `foreach_target.h`, no `highway.h` - so there is
// nothing here to compile once per instruction set.

#include "runtime/DependencyVersions.h"

#include <hwy/base.h>

namespace an::dependency_detail
{

void reportHighway(DependencyVersion& entry) noexcept
{
    entry.name = "highway";
    // Header-only for the way this engine uses it: per-target compilation and this engine's own
    // dispatch table, never the library's own runtime dispatch, so there is nothing to link, nothing
    // to deploy, and nothing loaded to ask for a version.
    entry.linkage = DependencyLinkage::HeaderOnly;
    setVersionTextFromParts(entry.compiled, HWY_MAJOR, HWY_MINOR, HWY_PATCH);
}

} // namespace an::dependency_detail
