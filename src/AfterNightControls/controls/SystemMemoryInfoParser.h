// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <cstdint>
#include <string_view>

namespace an::ui::system_memory
{
struct MemorySnapshot
{
    std::int64_t totalBytes = 0;
    std::int64_t availableBytes = 0;
};

[[nodiscard]] AFTERNIGHTCONTROLS_API MemorySnapshot parseLinuxFreeBytesOutput(std::string_view output);
[[nodiscard]] AFTERNIGHTCONTROLS_API MemorySnapshot parseLinuxMeminfo(std::string_view content);
[[nodiscard]] AFTERNIGHTCONTROLS_API MemorySnapshot buildWindowsSnapshot(std::uint64_t totalPhysBytes,
                                                                         std::uint64_t availPhysBytes);
[[nodiscard]] AFTERNIGHTCONTROLS_API MemorySnapshot clampSnapshot(MemorySnapshot snapshot);
[[nodiscard]] AFTERNIGHTCONTROLS_API int calculateUsagePercent(const MemorySnapshot& snapshot);
} // namespace an::ui::system_memory
