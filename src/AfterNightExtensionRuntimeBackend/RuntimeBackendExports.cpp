// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The runtime backend's five C exports, each a call into the module seat (RuntimeSeat.h), which owns the engine
// session the full host's native windows and its Python extensions share.

#include "RuntimeBackendExports.h"

#include "RuntimeSeat.h"

// The interface version the host compares before it calls anything else. It moves when the binary interface between
// the host and this module changes incompatibly - a pybind11 major upgrade, a changed export, another Python ABI - and
// not when the engine behind the exports changes.
//
// 1 -> 2: added initialize/shutdown/last_error exports for the Python-free engine bootstrap split.
static constexpr std::uint32_t kRuntimeBackendAbiVersion = 2;

extern "C" {

AN_RUNTIME_BACKEND_EXPORT std::uint32_t an_runtime_backend_abi_version()
{
    return kRuntimeBackendAbiVersion;
}

AN_RUNTIME_BACKEND_EXPORT int an_runtime_backend_probe()
{
    return an::sdk::seatProbe();
}

AN_RUNTIME_BACKEND_EXPORT int an_runtime_backend_initialize()
{
    return an::sdk::seatInitialize();
}

AN_RUNTIME_BACKEND_EXPORT void an_runtime_backend_shutdown()
{
    an::sdk::seatShutdown();
}

AN_RUNTIME_BACKEND_EXPORT const char* an_runtime_backend_last_error()
{
    return an::sdk::seatLastError();
}

} // extern "C"
