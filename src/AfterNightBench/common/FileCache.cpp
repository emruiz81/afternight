// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "FileCache.h"

#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace an::bench
{
namespace
{

#if defined(_WIN32)
/// A UTF-8 path as the wide string the Windows file API takes, or empty when it does not convert.
std::wstring widePath(const std::string& path)
{
    const int length =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), static_cast<int>(path.size()), nullptr, 0);
    if (length <= 0)
    {
        return {};
    }
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), static_cast<int>(path.size()), wide.data(), length);
    return wide;
}
#endif

} // namespace

bool dropFilesFromCache(std::span<const std::string> paths, std::string& error)
{
    for (const std::string& path : paths)
    {
#if defined(_WIN32)
        // Opening a handle that bypasses the cache is what drops the file's cached pages: the cache
        // manager cannot keep them coherent with reads that do not go through it. Nothing is read.
        const HANDLE file =
            CreateFileW(widePath(path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        nullptr, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            error = "could not open '" + path + "' to drop it from the cache";
            return false;
        }
        CloseHandle(file);
#else
        const int file = open(path.c_str(), O_RDONLY | O_CLOEXEC);
        if (file < 0)
        {
            error = "could not open '" + path + "' to drop it from the cache";
            return false;
        }
        const int advised = posix_fadvise(file, 0, 0, POSIX_FADV_DONTNEED);
        close(file);
        if (advised != 0)
        {
            error = "the system refused to drop '" + path + "' from the cache";
            return false;
        }
#endif
    }
    return true;
}

bool readFilesThroughCache(std::span<const std::string> paths, std::string& error)
{
    std::vector<char> chunk(8u * 1024u * 1024u);
    for (const std::string& path : paths)
    {
#if defined(_WIN32)
        const HANDLE file = CreateFileW(widePath(path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                        FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            error = "could not open '" + path + "' to read it into the cache";
            return false;
        }
        DWORD read = 0;
        bool ok = true;
        do
        {
            ok = ReadFile(file, chunk.data(), static_cast<DWORD>(chunk.size()), &read, nullptr) != 0;
        } while (ok && read != 0u);
        CloseHandle(file);
#else
        const int file = open(path.c_str(), O_RDONLY | O_CLOEXEC);
        if (file < 0)
        {
            error = "could not open '" + path + "' to read it into the cache";
            return false;
        }
        ssize_t read = 0;
        do
        {
            read = ::read(file, chunk.data(), chunk.size());
        } while (read > 0);
        close(file);
        const bool ok = read == 0;
#endif
        if (!ok)
        {
            error = "could not read '" + path + "' to its end";
            return false;
        }
    }
    return true;
}

} // namespace an::bench
