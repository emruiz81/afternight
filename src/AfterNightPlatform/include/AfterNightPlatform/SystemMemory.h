// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// How much physical memory the machine has, and how much of it is available now.
#pragma once

#include <AfterNightPlatform/PlatformExport.h>

#include <cstdint>

namespace an::platform
{

/// The machine's physical memory, in bytes.
struct SystemMemory
{
    std::uint64_t totalPhysicalBytes = 0; ///< Installed physical memory.

    /// Memory a new allocation can have without the system swapping: `MemAvailable` on Linux, which counts
    /// reclaimable page cache; the free and buffer pages when the kernel does not report it; the system's
    /// available figure on Windows; and free plus inactive pages on macOS.
    std::uint64_t availablePhysicalBytes = 0;
};

/// Asks the operating system for the machine's physical memory.
/// @return Both figures.
/// @throws std::runtime_error when the system refuses the query or the platform is not supported.
[[nodiscard]] AFTERNIGHTPLATFORM_API SystemMemory querySystemMemory();

} // namespace an::platform
