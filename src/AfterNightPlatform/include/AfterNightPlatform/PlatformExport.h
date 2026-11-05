// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The export decoration of the platform library's public declarations.
///
/// The library is shared so that the logger and the crash handler exist once per process, whichever
/// module logs or annotates. On Windows a declaration is exported while the library itself is built and
/// imported everywhere else; elsewhere the library is compiled with hidden visibility and a declaration
/// marked here is the only thing it exports.
#pragma once

#if defined(_WIN32)
#if defined(AFTERNIGHTPLATFORM_EXPORTS)
#define AFTERNIGHTPLATFORM_API __declspec(dllexport)
#else
#define AFTERNIGHTPLATFORM_API __declspec(dllimport)
#endif
#else
#define AFTERNIGHTPLATFORM_API __attribute__((visibility("default")))
#endif
