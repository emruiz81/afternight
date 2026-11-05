// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Self-compile check for AfterNightEnginePythonAdapter/PythonRuntimeAdapter.h.
//
// This translation unit includes exactly one public header of the optional adapter and nothing
// else, so it fails to compile if that header needs something a caller happened to include before
// it - including anything of Python's, which is the property that makes this adapter's surface
// compilable by a consumer that has no Python toolchain installed.
//
// The header is included twice deliberately. Including it once proves it is self-contained;
// including it again proves it is idempotent, which is the other half of what `#pragma once`
// promises and the half that a missing guard would break.

#include <AfterNightEnginePythonAdapter/PythonRuntimeAdapter.h>
#include <AfterNightEnginePythonAdapter/PythonRuntimeAdapter.h>
