#pragma once

// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#if defined(_WIN32) || defined(_WIN64)
#if defined(AFTERNIGHTEXTENSIONPROTOCOL_EXPORTS)
#define AFTERNIGHTEXTENSIONPROTOCOL_API __declspec(dllexport)
#else
#define AFTERNIGHTEXTENSIONPROTOCOL_API
#endif
#else
#define AFTERNIGHTEXTENSIONPROTOCOL_API __attribute__((visibility("default")))
#endif
