// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#if defined(_WIN32) || defined(_WIN64)
#if defined(AFTERNIGHTSHELLCORE_EXPORTS)
#define AFTERNIGHTSHELLCORE_API __declspec(dllexport)
#else
#define AFTERNIGHTSHELLCORE_API __declspec(dllimport)
#endif
#else
#define AFTERNIGHTSHELLCORE_API __attribute__((visibility("default")))
#endif
