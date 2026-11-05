// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Self-compile check for AfterNightEngine/Interop/OpenCvAdapter.h.
//
// This translation unit includes exactly one public header and nothing else, so it fails to
// compile if that header needs something a caller happened to include before it. A smoke client
// that includes the whole public surface together cannot find that: the first header to be
// included pulls in what the second one forgot, and both compile.
//
// The header is included twice deliberately. Including it once proves it is self-contained;
// including it again proves it is idempotent, which is the other half of what `#pragma once`
// promises and the half that a missing guard would break.

#include <AfterNightEngine/Interop/OpenCvAdapter.h>
#include <AfterNightEngine/Interop/OpenCvAdapter.h>
