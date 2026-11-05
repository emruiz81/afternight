// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Export and import decoration for the OpenCL proof backend.
///
/// A fourth marker beside the engine's and the two adapters', for the reason each of those is its own: a
/// declaration here is exported by the backend library and imported from it, so a consumer that links
/// the engine alone resolves nothing from this header and finds out at link time rather than at run
/// time. Reusing the engine's marker would make every declaration here claim to live in the engine's
/// binary, which is exactly the coupling an opt-in module must not have.
///
/// The macro takes the library's name, as the engine's does.

#pragma once

/// @def AFTERNIGHTENGINEOPENCLBACKEND_API
/// Marks a declaration the backend library exports and every other binary imports.
///
/// While the backend itself is compiled (`AFTERNIGHTENGINEOPENCLBACKEND_EXPORTS` defined) it is
/// `__declspec(dllexport)`, and everywhere else on Windows `__declspec(dllimport)`. On other
/// platforms it is default symbol visibility: the backend is built with hidden visibility, so a
/// declaration without this marker is not published at all.
#if defined(_WIN32)
#if defined(AFTERNIGHTENGINEOPENCLBACKEND_EXPORTS)
#define AFTERNIGHTENGINEOPENCLBACKEND_API __declspec(dllexport)
#else
#define AFTERNIGHTENGINEOPENCLBACKEND_API __declspec(dllimport)
#endif
#else
#define AFTERNIGHTENGINEOPENCLBACKEND_API __attribute__((visibility("default")))
#endif
