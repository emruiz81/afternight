// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#ifndef AFTERNIGHT_TESTS
#include "stdafx.hpp"
#endif
#include "SystemMemoryInfoParser.h"

#include <algorithm>
#include <charconv>
#include <string>
#include <vector>

namespace an::ui::system_memory
{
namespace
{
std::string_view trim(std::string_view s)
{
    const auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos)
    {
        return {};
    }
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

std::vector<std::string_view> splitLines(std::string_view s)
{
    std::vector<std::string_view> lines;
    while (!s.empty())
    {
        const auto pos = s.find('\n');
        if (pos == std::string_view::npos)
        {
            lines.push_back(s);
            break;
        }
        lines.push_back(s.substr(0, pos));
        s.remove_prefix(pos + 1);
    }
    return lines;
}

std::vector<std::string_view> splitWhitespace(std::string_view s)
{
    std::vector<std::string_view> tokens;
    s = trim(s);
    while (!s.empty())
    {
        const auto sep = s.find_first_of(" \t");
        if (sep == std::string_view::npos)
        {
            tokens.push_back(s);
            break;
        }
        tokens.push_back(s.substr(0, sep));
        s.remove_prefix(sep);
        s = trim(s);
    }
    return tokens;
}

std::int64_t parseInt64(std::string_view token)
{
    const auto v = trim(token);
    if (v.empty())
    {
        return 0;
    }

    std::int64_t result = 0;
    const auto* first = v.data();
    const auto* last = v.data() + v.size();
    const auto [ptr, ec] = std::from_chars(first, last, result);
    if (ec != std::errc{} || ptr != last)
    {
        return 0;
    }
    return result;
}

std::int64_t parseMeminfoValueKiB(std::string_view line)
{
    const auto colon = line.find(':');
    if (colon == std::string_view::npos)
    {
        return 0;
    }

    auto rhs = trim(line.substr(colon + 1));
    const auto tokens = splitWhitespace(rhs);
    if (tokens.empty())
    {
        return 0;
    }

    return parseInt64(tokens.front());
}
} // namespace

MemorySnapshot clampSnapshot(MemorySnapshot snapshot)
{
    if (snapshot.totalBytes < 0)
    {
        snapshot.totalBytes = 0;
    }

    if (snapshot.availableBytes < 0)
    {
        snapshot.availableBytes = 0;
    }

    if (snapshot.totalBytes > 0 && snapshot.availableBytes > snapshot.totalBytes)
    {
        snapshot.availableBytes = snapshot.totalBytes;
    }

    return snapshot;
}

int calculateUsagePercent(const MemorySnapshot& snapshot)
{
    if (snapshot.totalBytes <= 0)
    {
        return 0;
    }

    const auto clamped = clampSnapshot(snapshot);
    const std::int64_t used = clamped.totalBytes - clamped.availableBytes;
    const auto percent = static_cast<int>((used * 100) / clamped.totalBytes);
    return std::clamp(percent, 0, 100);
}

MemorySnapshot parseLinuxFreeBytesOutput(std::string_view output)
{
    MemorySnapshot snapshot;
    const auto lines = splitLines(output);
    for (const auto rawLine : lines)
    {
        const auto line = trim(rawLine);
        if (!line.starts_with("Mem:"))
        {
            continue;
        }

        const auto fields = splitWhitespace(line);
        // free --bytes output:
        // Mem: total used free shared buff/cache available
        if (fields.size() >= 7)
        {
            snapshot.totalBytes = parseInt64(fields[1]);
            snapshot.availableBytes = parseInt64(fields[6]);
        }
        break;
    }

    return clampSnapshot(snapshot);
}

MemorySnapshot parseLinuxMeminfo(std::string_view content)
{
    MemorySnapshot snapshot;
    std::int64_t memFreeKiB = 0;
    std::int64_t buffersKiB = 0;
    std::int64_t cachedKiB = 0;
    std::int64_t sReclaimableKiB = 0;
    std::int64_t shmemKiB = 0;

    const auto lines = splitLines(content);
    for (const auto rawLine : lines)
    {
        const auto line = trim(rawLine);
        if (line.starts_with("MemTotal:"))
        {
            snapshot.totalBytes = parseMeminfoValueKiB(line) * 1024LL;
        }
        else if (line.starts_with("MemAvailable:"))
        {
            snapshot.availableBytes = parseMeminfoValueKiB(line) * 1024LL;
        }
        else if (line.starts_with("MemFree:"))
        {
            memFreeKiB = parseMeminfoValueKiB(line);
        }
        else if (line.starts_with("Buffers:"))
        {
            buffersKiB = parseMeminfoValueKiB(line);
        }
        else if (line.starts_with("Cached:"))
        {
            cachedKiB = parseMeminfoValueKiB(line);
        }
        else if (line.starts_with("SReclaimable:"))
        {
            sReclaimableKiB = parseMeminfoValueKiB(line);
        }
        else if (line.starts_with("Shmem:"))
        {
            shmemKiB = parseMeminfoValueKiB(line);
        }
    }

    if (snapshot.availableBytes <= 0)
    {
        const std::int64_t fallbackAvailableKiB = memFreeKiB + buffersKiB + cachedKiB + sReclaimableKiB - shmemKiB;
        if (fallbackAvailableKiB > 0)
        {
            snapshot.availableBytes = fallbackAvailableKiB * 1024LL;
        }
    }

    return clampSnapshot(snapshot);
}

MemorySnapshot buildWindowsSnapshot(std::uint64_t totalPhysBytes, std::uint64_t availPhysBytes)
{
    MemorySnapshot snapshot;
    snapshot.totalBytes = static_cast<std::int64_t>(totalPhysBytes);
    snapshot.availableBytes = static_cast<std::int64_t>(availPhysBytes);
    return clampSnapshot(snapshot);
}
} // namespace an::ui::system_memory
