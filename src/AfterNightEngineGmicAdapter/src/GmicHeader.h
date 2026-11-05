// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngineGmicAdapter - the one place the G'MIC library's own header is included.
//
// It is a header of its own for the reason the Python adapter's equivalent is, and for one more that
// is specific to this library.
//
// **Only the C bridge is included, never the interpreter's own header.** `gmic.h` is a C++ header
// that pulls CImg in with it - tens of thousands of lines, an OpenMP surface, and a `min`/`max`
// vocabulary that argues with the Windows SDK. The C bridge is three upstream functions, the patch
// level this repository's copy adds, and two plain structures, and it is the whole of what this
// adapter needs. The cost of that choice is stated where it is felt: the bridge exposes no version of
// the library, so the capability report carries no version.
//
// **The library's structures define the calling convention.** On Windows the bridge's functions are
// `__stdcall` and imported, which its own header handles as long as `gmic_core` is not defined - and
// nothing here defines it, because that macro is for the library's own translation units.
//
// When no library is compiled in this header includes nothing, which is what lets the rest of the
// adapter be written once: the two configurations differ in what the call functions do, not in
// whether they exist.

#pragma once

#if defined(AFTERNIGHT_HAS_GMIC)

#if defined(_WIN32)
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#endif

#include <gmic/gmic_libc.h>

#endif

namespace an
{

/// Whether this build of the adapter contains a G'MIC library.
///
/// A compile-time constant rather than a query, because it is a property of the binary: the
/// configuration that defines `AFTERNIGHT_HAS_GMIC` links the library and the one that does not
/// contains none of it.
#if defined(AFTERNIGHT_HAS_GMIC)
inline constexpr bool kGmicLibraryCompiledIn = true;
#else
inline constexpr bool kGmicLibraryCompiledIn = false;
#endif

} // namespace an
