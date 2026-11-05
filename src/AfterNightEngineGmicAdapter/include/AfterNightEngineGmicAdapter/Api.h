// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Export and import decoration for this adapter's surface.
///
/// A third marker beside the engine's and the Python adapter's, and the reason there are three is
/// the reason each adapter is a separate binary: a declaration here is exported by the G'MIC adapter
/// library and imported from it, so a consumer that links the engine alone resolves nothing from
/// this header and finds out at link time rather than at run time. Reusing the engine's marker would
/// make every declaration here claim to live in the engine's binary, which is exactly the coupling
/// the optional adapter removes.
///
/// The macro takes the library's name, as the engine's does.

#pragma once

/// @def AFTERNIGHTENGINEGMICADAPTER_API
/// Marks a declaration the G'MIC adapter library exports and every other binary imports.
///
/// While the adapter itself is compiled (`AFTERNIGHTENGINEGMICADAPTER_EXPORTS` defined) it is
/// `__declspec(dllexport)`, and everywhere else on Windows `__declspec(dllimport)`. On other
/// platforms it is default symbol visibility: the adapter is built with hidden visibility, so a
/// declaration without this marker is not published at all.
#if defined(_WIN32)
#if defined(AFTERNIGHTENGINEGMICADAPTER_EXPORTS)
#define AFTERNIGHTENGINEGMICADAPTER_API __declspec(dllexport)
#else
#define AFTERNIGHTENGINEGMICADAPTER_API __declspec(dllimport)
#endif
#else
#define AFTERNIGHTENGINEGMICADAPTER_API __attribute__((visibility("default")))
#endif
