// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Runtime/EngineCapabilities.h>

#include <cstdint>
#include <thread>

// What the build can do, answered from constants and one query of the machine.
//
// The host concurrency is the only value here that is not fixed at compile time, and it is the one
// worth a line of explanation. `std::thread::hardware_concurrency` is documented to return zero
// when the value is not computable, which is a real answer on a container with an unusual cgroup
// and on platforms that do not expose the number at all. Reporting that zero would push it into
// every caller that resolves a concurrency setting from it, and each of them would either repeat
// this check or configure an engine to run nothing. So the floor is applied once, here.

namespace an
{

EngineCapabilities engineCapabilities() noexcept
{
    EngineCapabilities capabilities;

    // Taken from the exported accessors rather than from the constants directly, so that these are
    // the loaded library's numbers by construction and not the ones some caller's header declared.
    capabilities.version = engineVersion();
    capabilities.abiGeneration = engineAbiGeneration();

    const unsigned int reported = std::thread::hardware_concurrency();
    capabilities.hostConcurrency = reported == 0u ? 1u : static_cast<std::uint32_t>(reported);

    // Implicit GPU dispatch is reserved. Device backends are supplied explicitly to Device.
    capabilities.gpuBackend = false;

    return capabilities;
}

} // namespace an
