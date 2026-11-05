// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The export decoration of the consumer runtime's public declarations.
///
/// The library is shared so that the application, the command-line tool, the full host and the dynamically loaded
/// runtime backend use one definition of the session, the operation scope and the image asset, and one copy of the
/// state behind them. On Windows a declaration is exported while the library itself is built and imported everywhere
/// else; elsewhere the library is compiled with hidden visibility and a declaration marked here is the only thing it
/// exports.
#pragma once

#if defined(_WIN32)
#if defined(AFTERNIGHTCONSUMERRUNTIME_EXPORTS)
#define AFTERNIGHTCONSUMERRUNTIME_API __declspec(dllexport)
#else
#define AFTERNIGHTCONSUMERRUNTIME_API __declspec(dllimport)
#endif
#else
#define AFTERNIGHTCONSUMERRUNTIME_API __attribute__((visibility("default")))
#endif
