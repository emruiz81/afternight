// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The physical-memory query, per operating system.

#include <AfterNightPlatform/SystemMemory.h>

#include <stdexcept>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#elif defined(__linux__)
#include <cstdio>
#include <cstring>
#include <sys/sysinfo.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#endif

namespace an::platform
{

SystemMemory querySystemMemory()
{
    SystemMemory memory;

#if defined(_WIN32)
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (!GlobalMemoryStatusEx(&status))
    {
        throw std::runtime_error("GlobalMemoryStatusEx failed");
    }

    memory.totalPhysicalBytes = status.ullTotalPhys;
    memory.availablePhysicalBytes = status.ullAvailPhys;

#elif defined(__linux__)
    // sysinfo's free and buffer pages are the fallback; MemAvailable, which Linux reports from 3.14, also
    // counts the page cache the kernel would reclaim.
    struct sysinfo info{};
    if (sysinfo(&info) != 0)
    {
        throw std::runtime_error("sysinfo failed");
    }

    memory.totalPhysicalBytes = static_cast<std::uint64_t>(info.totalram) * info.mem_unit;
    memory.availablePhysicalBytes = static_cast<std::uint64_t>(info.freeram + info.bufferram) * info.mem_unit;

    if (FILE* meminfo = std::fopen("/proc/meminfo", "r"); meminfo != nullptr)
    {
        char line[256];
        while (std::fgets(line, sizeof(line), meminfo))
        {
            if (std::strncmp(line, "MemAvailable:", 13) == 0)
            {
                unsigned long kibibytes = 0;
                if (std::sscanf(line + 13, "%lu", &kibibytes) == 1)
                {
                    memory.availablePhysicalBytes = static_cast<std::uint64_t>(kibibytes) * 1024ULL;
                    break;
                }
            }
        }
        std::fclose(meminfo);
    }

#elif defined(__APPLE__)
    std::uint64_t total = 0;
    size_t length = sizeof(total);
    if (sysctlbyname("hw.memsize", &total, &length, nullptr, 0) != 0)
    {
        throw std::runtime_error("sysctl hw.memsize failed");
    }
    memory.totalPhysicalBytes = total;

    // Inactive pages are reclaimable, so free plus inactive is what an allocation can have.
    mach_msg_type_number_t count = HOST_VM_INFO_COUNT;
    vm_statistics64_data_t statistics;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO, reinterpret_cast<host_info64_t>(&statistics), &count) !=
        KERN_SUCCESS)
    {
        throw std::runtime_error("host_statistics64 failed");
    }

    std::uint64_t pageSize = 0;
    length = sizeof(pageSize);
    if (sysctlbyname("hw.pagesize", &pageSize, &length, nullptr, 0) != 0)
    {
        throw std::runtime_error("sysctl hw.pagesize failed");
    }

    memory.availablePhysicalBytes = statistics.free_count * pageSize + statistics.inactive_count * pageSize;

#else
    throw std::runtime_error("Unsupported platform");
#endif

    return memory;
}

} // namespace an::platform
