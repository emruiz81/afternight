// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

// AfterNightExtensionRuntimeBackend — public C ABI surface
//
// This shared library is loaded at runtime via dlopen (Linux) / LoadLibrary
// (Windows) by AfterNightExtensionHost.  It must NOT be linked at build time.
// Only the symbols declared here are part of the stable loading contract;
// everything else is an implementation detail. The embedded `_afternight_runtime`
// Python module registers itself when the module is loaded, so a host loads it
// before it starts its interpreter.

#include <cstdint>

#if defined(_WIN32)
#define AN_RUNTIME_BACKEND_EXPORT __declspec(dllexport)
#else
#define AN_RUNTIME_BACKEND_EXPORT __attribute__((visibility("default")))
#endif

extern "C" {

// ABI version integer set at build time.  The host compares this against
// its expected version during module loading; a mismatch prevents the
// session from proceeding.
AN_RUNTIME_BACKEND_EXPORT std::uint32_t an_runtime_backend_abi_version();

// Probe: returns 1 when the module can run in this process, 0 with the reason in
// an_runtime_backend_last_error() otherwise. Before initialization it builds a
// temporary engine context, allocates a 1x1 image in it and releases both; after
// it, it allocates in the session initialization opened. It starts no Python
// interpreter.
AN_RUNTIME_BACKEND_EXPORT int an_runtime_backend_probe();

// Opens the engine session the host's native windows and Python extensions share.
// Idempotent: a second call while the session is open succeeds and changes nothing.
// Starts no Python interpreter. Returns 1 on success, 0 with the reason in
// an_runtime_backend_last_error() otherwise.
AN_RUNTIME_BACKEND_EXPORT int an_runtime_backend_initialize();

// Shuts the session down. While anything still holds it - a session capsule, an
// image or array the interpreter holds, running work - the shutdown is refused:
// the session stays open and an_runtime_backend_last_error() says what holds it,
// so the host finalizes its interpreter and calls this again before unloading.
AN_RUNTIME_BACKEND_EXPORT void an_runtime_backend_shutdown();

// Returns the most recent human-readable runtime backend error message, or an
// empty string after a call that succeeded. Valid until the next probe,
// initialize or shutdown call.
AN_RUNTIME_BACKEND_EXPORT const char* an_runtime_backend_last_error();

} // extern "C"
