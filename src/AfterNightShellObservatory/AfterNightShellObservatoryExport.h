// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#if defined(_WIN32) || defined(_WIN64)
#if defined(AFTERNIGHTSHELLOBSERVATORY_EXPORTS)
#define AFTERNIGHTSHELLOBSERVATORY_API __declspec(dllexport)
#else
#define AFTERNIGHTSHELLOBSERVATORY_API __declspec(dllimport)
#endif
#else
#define AFTERNIGHTSHELLOBSERVATORY_API __attribute__((visibility("default")))
#endif
