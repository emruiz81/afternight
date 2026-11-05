// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Export and import decoration for the public surface.
///
/// The macros take the library's name, as its binary and its include root do.

#pragma once

/// @def AFTERNIGHTENGINE_API
/// Marks a declaration the engine library exports and every other binary imports.
///
/// While the engine itself is compiled (`AFTERNIGHTENGINE_EXPORTS` defined) it is
/// `__declspec(dllexport)`, and everywhere else on Windows `__declspec(dllimport)`. On other
/// platforms it is default symbol visibility: the engine is built with hidden visibility, so a
/// declaration without this marker is not published at all.
#if defined(_WIN32)
#if defined(AFTERNIGHTENGINE_EXPORTS)
#define AFTERNIGHTENGINE_API __declspec(dllexport)
#else
#define AFTERNIGHTENGINE_API __declspec(dllimport)
#endif
#else
#define AFTERNIGHTENGINE_API __attribute__((visibility("default")))
#endif

/// @def AFTERNIGHTENGINE_INTERFACE
/// A polymorphic type whose identity this library owns and that a caller outside it derives from, or
/// constructs or moves in its own code - an implicit move writes the virtual-table pointer there.
///
/// This is a narrower promise than the one above, and the two platforms need different things for
/// it, so the macro deliberately says nothing on one of them. A class whose first non-inline virtual
/// function is defined in this library has its vtable and its type information emitted in that one
/// translation unit under the Itanium ABI, and nowhere else; a class implemented outside this
/// library takes both from it and so needs them visible in it. The Microsoft compiler emits both
/// into every translation unit that needs them, so there they already sit beside the derived class,
/// and decorating the type would instead export every one of its members - a wider promise than the
/// identity these types need, and one nothing has asked for.
#if defined(_WIN32)
#define AFTERNIGHTENGINE_INTERFACE
#else
#define AFTERNIGHTENGINE_INTERFACE __attribute__((visibility("default")))
#endif
