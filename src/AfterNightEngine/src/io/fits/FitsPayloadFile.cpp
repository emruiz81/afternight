// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the platform handle the FITS adapter reads an image's payload through.

#include "io/fits/FitsPayloadFile.h"

#include <algorithm>
#include <filesystem>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOGDI
#define NOGDI
#endif
#ifndef NOUSER
#define NOUSER
#endif
#include <Windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace an
{
namespace
{

/// The largest transfer one system call is asked for.
constexpr std::size_t kMaximumCallBytes = std::size_t{1} << 30;

/// A failure opening or reading the payload, naming the file.
Status payloadFailure(const char* what, std::string_view path)
{
    return Status::failure(ErrorCode::ExternalFailure, {.message = std::string(what) + " '" + std::string(path) + "'",
                                                        .operation = "read FITS payload",
                                                        .domain = ErrorDomain::Io});
}

} // namespace

FitsPayloadFile::~FitsPayloadFile()
{
#if defined(_WIN32)
    if (m_handle != -1)
    {
        CloseHandle(reinterpret_cast<HANDLE>(m_handle));
    }
#else
    if (m_handle != -1)
    {
        ::close(static_cast<int>(m_handle));
    }
#endif
}

Status FitsPayloadFile::open(std::string_view path)
{
    const std::filesystem::path native(std::u8string_view(reinterpret_cast<const char8_t*>(path.data()), path.size()));
#if defined(_WIN32)
    const HANDLE handle =
        CreateFileW(native.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                    OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        return payloadFailure("cannot open the payload of", path);
    }
    m_handle = reinterpret_cast<std::intptr_t>(handle);
#else
    const int descriptor = ::open(native.c_str(), O_RDONLY | O_CLOEXEC);
    if (descriptor < 0)
    {
        return payloadFailure("cannot open the payload of", path);
    }
#ifdef POSIX_FADV_SEQUENTIAL
    (void)::posix_fadvise(descriptor, 0, 0, POSIX_FADV_SEQUENTIAL);
#endif
    m_handle = descriptor;
#endif
    m_path = std::string(path);
    return Status::success();
}

Status FitsPayloadFile::readAt(std::span<std::byte> bytes, std::uint64_t offset) const
{
    std::size_t received = 0u;
    while (received < bytes.size())
    {
        const std::size_t chunk = (std::min)(bytes.size() - received, kMaximumCallBytes);
        const std::uint64_t position = offset + received;
#if defined(_WIN32)
        OVERLAPPED overlapped{};
        overlapped.Offset = static_cast<DWORD>(position);
        overlapped.OffsetHigh = static_cast<DWORD>(position >> 32u);
        DWORD got = 0u;
        if (!ReadFile(reinterpret_cast<HANDLE>(m_handle), bytes.data() + received, static_cast<DWORD>(chunk), &got,
                      &overlapped) ||
            got == 0u)
        {
            return payloadFailure("the payload ends early in", m_path);
        }
#else
        const ssize_t got =
            ::pread(static_cast<int>(m_handle), bytes.data() + received, chunk, static_cast<off_t>(position));
        if (got < 0 && errno == EINTR)
        {
            continue;
        }
        if (got <= 0)
        {
            return payloadFailure("the payload ends early in", m_path);
        }
#endif
        received += static_cast<std::size_t>(got);
    }
    return Status::success();
}

} // namespace an
