// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Export and import decoration for this adapter's surface.
///
/// A second marker beside the engine's, and the reason there are two is the reason this adapter is a
/// separate binary: a declaration here is exported by the adapter library and imported from it, so a
/// consumer that links the engine alone resolves nothing from this header and finds out at link time
/// rather than at run time. Reusing the engine's marker would make every declaration here claim to
/// live in the engine's binary, which is exactly the coupling the optional adapter removes.
///
/// The macro takes the library's name, as the engine's does.

#pragma once

/// @def AFTERNIGHTENGINEPYTHONADAPTER_API
/// Marks a declaration the Python adapter library exports and every other binary imports.
///
/// While the adapter itself is compiled (`AFTERNIGHTENGINEPYTHONADAPTER_EXPORTS` defined) it is
/// `__declspec(dllexport)`, and everywhere else on Windows `__declspec(dllimport)`. On other
/// platforms it is default symbol visibility: the adapter is built with hidden visibility, so a
/// declaration without this marker is not published at all.
#if defined(_WIN32)
#if defined(AFTERNIGHTENGINEPYTHONADAPTER_EXPORTS)
#define AFTERNIGHTENGINEPYTHONADAPTER_API __declspec(dllexport)
#else
#define AFTERNIGHTENGINEPYTHONADAPTER_API __declspec(dllimport)
#endif
#else
#define AFTERNIGHTENGINEPYTHONADAPTER_API __attribute__((visibility("default")))
#endif
