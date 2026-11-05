// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Self-compile check for AfterNightEngineOpenClBackend/OpenClBackend.h.
//
// This translation unit includes exactly one public header of the opt-in OpenCL backend and nothing
// else, so it fails to compile if that header needs something a caller happened to include before it
// - including anything of OpenCL's, which is the property that lets a caller decide whether to create
// a backend without an OpenCL SDK on its include path.
//
// The header is included twice deliberately. Including it once proves it is self-contained;
// including it again proves it is idempotent, which is the other half of what `#pragma once`
// promises and the half that a missing guard would break.

#include <AfterNightEngineOpenClBackend/OpenClBackend.h>
#include <AfterNightEngineOpenClBackend/OpenClBackend.h>
