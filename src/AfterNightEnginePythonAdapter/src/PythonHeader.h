// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEnginePythonAdapter - the one place Python's own header is included.
//
// It is a header of its own for two reasons that are both about include order.
//
// **Python.h has to come first.** It defines the feature macros a C library's headers read -
// `_POSIX_C_SOURCE` and `_XOPEN_SOURCE` among them - and it redefines them if a standard header got
// there first. A header that is included before everything else is easier to keep first than an
// include line inside a translation unit that also has a layering rule about which include comes
// first.
//
// **A debug build of this adapter links the release runtime.** Python's header asks the linker for
// `python3xx_d.lib` when `_DEBUG` is defined, and no deployment of this repository ships a debug
// build of CPython. Undefining `_DEBUG` around the include is how every embedder handles that, and
// it is done here once rather than at each use.
//
// When no runtime is compiled in this header includes nothing, which is what lets the rest of the
// adapter be written once: the two configurations differ in what the interpreter functions do, not
// in whether they exist.

#pragma once

#if defined(AFTERNIGHT_HAS_PYTHON)

#if defined(_WIN32)
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#endif

#if defined(_MSC_VER) && defined(_DEBUG)
#define AFTERNIGHT_RESTORE_DEBUG_AFTER_PYTHON_H
#undef _DEBUG
#endif

#include <Python.h>

#if defined(AFTERNIGHT_RESTORE_DEBUG_AFTER_PYTHON_H)
#define _DEBUG
#undef AFTERNIGHT_RESTORE_DEBUG_AFTER_PYTHON_H
#endif

#endif

namespace an
{

/// Whether this build of the adapter contains a Python runtime.
///
/// A compile-time constant rather than a query, because it is a property of the binary: the
/// configuration that defines `AFTERNIGHT_HAS_PYTHON` links the runtime and the one that does not
/// contains none of it.
#if defined(AFTERNIGHT_HAS_PYTHON)
inline constexpr bool kPythonRuntimeCompiledIn = true;

/// The major version of the runtime this adapter was compiled against, from Python's own header.
inline constexpr int kPythonBuildMajor = PY_MAJOR_VERSION;

/// The minor version of the runtime this adapter was compiled against.
inline constexpr int kPythonBuildMinor = PY_MINOR_VERSION;

/// The patch version of the runtime this adapter was compiled against, which Python calls `micro`.
inline constexpr int kPythonBuildPatch = PY_MICRO_VERSION;
#else
inline constexpr bool kPythonRuntimeCompiledIn = false;

/// Zero, because a build with no runtime was compiled against no version.
inline constexpr int kPythonBuildMajor = 0;

/// Zero, for the reason `kPythonBuildMajor` is.
inline constexpr int kPythonBuildMinor = 0;

/// Zero, for the reason `kPythonBuildMajor` is.
inline constexpr int kPythonBuildPatch = 0;
#endif

} // namespace an
