// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <algorithm>
#include <cstdint>
#include <thread>

namespace ImageIntegration::core
{
[[nodiscard]] inline int detectedParallelCoreCount()
{
    const unsigned int cores = std::thread::hardware_concurrency();
    return (std::max)(1, static_cast<int>(cores));
}

[[nodiscard]] inline int clampParallelCoreLimit(const int requested_cores)
{
    const int detected_cores = detectedParallelCoreCount();
    if (requested_cores <= 0)
    {
        return detected_cores;
    }

    return (std::min)(detected_cores, (std::max)(1, requested_cores));
}

[[nodiscard]] inline bool shouldLimitParallelCores(const int requested_cores)
{
    return requested_cores > 0 && clampParallelCoreLimit(requested_cores) < detectedParallelCoreCount();
}

/// The most threads one operation of a run may use under its parallel-cores setting: the setting, held to the cores
/// this host has, where it limits the run, and zero - as many as the operation's purpose may have - where it does not.
/// Every stage operation and every measurement the run's report makes is admitted at it.
[[nodiscard]] inline std::uint32_t parallelCoreWidth(const int requested_cores)
{
    return shouldLimitParallelCores(requested_cores)
               ? static_cast<std::uint32_t>(clampParallelCoreLimit(requested_cores))
               : 0u;
}
} // namespace ImageIntegration::core
