// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the sequence containers' file handle on Windows and on POSIX systems.
//
// Every transfer is split into platform calls of at most one gibibyte, so a count always fits the
// 32-bit size Windows takes, and a POSIX call a signal interrupts is retried. A read that reaches the
// end of the file is not a failure; a call that fails with no error the platform states reports
// `SequenceFile::kShortTransfer` instead of zero.

#include "io/sequences/SequenceFile.h"

#include <algorithm>
#include <cerrno>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
// This byte boundary needs no GUI, service, locale or input-method declarations.
#ifndef NOGDI
#define NOGDI
#endif
#ifndef NOUSER
#define NOUSER
#endif
#ifndef NOSERVICE
#define NOSERVICE
#endif
#ifndef NOMCX
#define NOMCX
#endif
#ifndef NOIME
#define NOIME
#endif
#ifndef NONLS
#define NONLS
#endif
#include <Windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace an
{
namespace
{
// One platform call transfers at most this much, which keeps every count inside a 32-bit DWORD.
constexpr std::size_t kMaxCallBytes = std::size_t{1} << 30u;

#ifdef _WIN32
HANDLE handleOf(std::intptr_t value) noexcept
{
    return reinterpret_cast<HANDLE>(value);
}

int lastError() noexcept
{
    const auto error = static_cast<int>(GetLastError());
    return error == 0 ? SequenceFile::kShortTransfer : error;
}
#else
int lastError() noexcept
{
    return errno == 0 ? SequenceFile::kShortTransfer : errno;
}
#endif
} // namespace

SequenceFile::~SequenceFile()
{
    (void)close();
}

SequenceFile::SequenceFile(SequenceFile&& other) noexcept : m_handle(std::exchange(other.m_handle, -1)) {}

SequenceFile& SequenceFile::operator=(SequenceFile&& other) noexcept
{
    if (this != &other)
    {
        (void)close();
        m_handle = std::exchange(other.m_handle, -1);
    }
    return *this;
}

int SequenceFile::openForReading(const std::filesystem::path& path) noexcept
{
    (void)close();
#ifdef _WIN32
    const HANDLE handle =
        CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return lastError();
    if (GetFileType(handle) != FILE_TYPE_DISK)
    {
        (void)CloseHandle(handle);
        return ERROR_BAD_FILE_TYPE;
    }
    m_handle = reinterpret_cast<std::intptr_t>(handle);
#else
    // Opening a FIFO or a device for reading blocks until something else opens its other end, where no
    // cancellation can reach. The flag makes that open return at once; a regular file ignores it, and the
    // descriptor goes back to blocking mode below.
    const int descriptor = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (descriptor < 0)
        return lastError();
    struct stat info{};
    if (::fstat(descriptor, &info) != 0)
    {
        const int error = lastError();
        (void)::close(descriptor);
        return error;
    }
    if (!S_ISREG(info.st_mode))
    {
        (void)::close(descriptor);
        return S_ISDIR(info.st_mode) ? EISDIR : EINVAL;
    }
    if (const int flags = ::fcntl(descriptor, F_GETFL); flags >= 0)
        (void)::fcntl(descriptor, F_SETFL, flags & ~O_NONBLOCK);
#ifdef POSIX_FADV_SEQUENTIAL
    (void)::posix_fadvise(descriptor, 0, 0, POSIX_FADV_SEQUENTIAL);
#endif
    m_handle = descriptor;
#endif
    return 0;
}

int SequenceFile::openForWriting(const std::filesystem::path& path) noexcept
{
    (void)close();
    // The reservation is reopened by name, so the name must still be the file that was reserved: a link
    // is opened as itself rather than followed, and anything but a regular file with one name is refused
    // before a byte of it is discarded.
#ifdef _WIN32
    const HANDLE handle = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return lastError();
    FILE_ATTRIBUTE_TAG_INFO tag{};
    FILE_STANDARD_INFO standard{};
    if (GetFileType(handle) != FILE_TYPE_DISK ||
        !GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &tag, sizeof(tag)) ||
        !GetFileInformationByHandleEx(handle, FileStandardInfo, &standard, sizeof(standard)) ||
        (tag.FileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) != 0u ||
        standard.NumberOfLinks != 1u)
    {
        (void)CloseHandle(handle);
        return ERROR_BAD_FILE_TYPE;
    }
    m_handle = reinterpret_cast<std::intptr_t>(handle);
#else
    const int descriptor = ::open(path.c_str(), O_WRONLY | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW);
    if (descriptor < 0)
        return lastError();
    struct stat info{};
    if (::fstat(descriptor, &info) != 0)
    {
        const int error = lastError();
        (void)::close(descriptor);
        return error;
    }
    if (!S_ISREG(info.st_mode) || info.st_nlink != 1)
    {
        (void)::close(descriptor);
        return EINVAL;
    }
    if (const int flags = ::fcntl(descriptor, F_GETFL); flags >= 0)
        (void)::fcntl(descriptor, F_SETFL, flags & ~O_NONBLOCK);
    m_handle = descriptor;
#endif
    if (const int error = truncate(); error != 0)
    {
        (void)close();
        return error;
    }
    return 0;
}

bool SequenceFile::isOpen() const noexcept
{
    return m_handle != -1;
}

int SequenceFile::size(std::uint64_t& bytes) const noexcept
{
#ifdef _WIN32
    LARGE_INTEGER value{};
    if (!GetFileSizeEx(handleOf(m_handle), &value))
        return lastError();
    if (value.QuadPart < 0)
        return kShortTransfer;
    bytes = static_cast<std::uint64_t>(value.QuadPart);
#else
    struct stat info{};
    if (::fstat(static_cast<int>(m_handle), &info) != 0)
        return lastError();
    if (info.st_size < 0)
        return kShortTransfer;
    bytes = static_cast<std::uint64_t>(info.st_size);
#endif
    return 0;
}

int SequenceFile::readAt(std::byte* destination, std::size_t count, std::uint64_t offset,
                         std::size_t& received) const noexcept
{
    received = 0u;
    while (received < count)
    {
        const std::size_t chunk = (std::min)(count - received, kMaxCallBytes);
        const std::uint64_t position = offset + received;
#ifdef _WIN32
        // The handle is synchronous; OVERLAPPED supplies the offset without a separate seek.
        OVERLAPPED overlapped{};
        overlapped.Offset = static_cast<DWORD>(position);
        overlapped.OffsetHigh = static_cast<DWORD>(position >> 32u);
        DWORD got = 0u;
        if (!ReadFile(handleOf(m_handle), destination + received, static_cast<DWORD>(chunk), &got, &overlapped))
        {
            const DWORD error = GetLastError();
            if (error == ERROR_HANDLE_EOF)
                return 0;
            return error == 0u ? kShortTransfer : static_cast<int>(error);
        }
#else
        const ssize_t got =
            ::pread(static_cast<int>(m_handle), destination + received, chunk, static_cast<off_t>(position));
        if (got < 0)
        {
            if (errno == EINTR)
                continue;
            return lastError();
        }
#endif
        if (got == 0)
            return 0;
        received += static_cast<std::size_t>(got);
    }
    return 0;
}

int SequenceFile::write(std::span<const std::byte> bytes) noexcept
{
    while (!bytes.empty())
    {
        const std::size_t chunk = (std::min)(bytes.size(), kMaxCallBytes);
#ifdef _WIN32
        DWORD written = 0u;
        if (!WriteFile(handleOf(m_handle), bytes.data(), static_cast<DWORD>(chunk), &written, nullptr))
            return lastError();
#else
        const ssize_t written = ::write(static_cast<int>(m_handle), bytes.data(), chunk);
        if (written < 0)
        {
            if (errno == EINTR)
                continue;
            return lastError();
        }
#endif
        if (written == 0)
            return kShortTransfer;
        bytes = bytes.subspan(static_cast<std::size_t>(written));
    }
    return 0;
}

int SequenceFile::writeAt(std::span<const std::byte> bytes, std::uint64_t offset) noexcept
{
    while (!bytes.empty())
    {
        const std::size_t chunk = (std::min)(bytes.size(), kMaxCallBytes);
#ifdef _WIN32
        OVERLAPPED overlapped{};
        overlapped.Offset = static_cast<DWORD>(offset);
        overlapped.OffsetHigh = static_cast<DWORD>(offset >> 32u);
        DWORD written = 0u;
        if (!WriteFile(handleOf(m_handle), bytes.data(), static_cast<DWORD>(chunk), &written, &overlapped))
            return lastError();
#else
        const ssize_t written = ::pwrite(static_cast<int>(m_handle), bytes.data(), chunk, static_cast<off_t>(offset));
        if (written < 0)
        {
            if (errno == EINTR)
                continue;
            return lastError();
        }
#endif
        if (written == 0)
            return kShortTransfer;
        bytes = bytes.subspan(static_cast<std::size_t>(written));
        offset += static_cast<std::uint64_t>(written);
    }
    return 0;
}

int SequenceFile::truncate() noexcept
{
#ifdef _WIN32
    LARGE_INTEGER start{};
    if (!SetFilePointerEx(handleOf(m_handle), start, nullptr, FILE_BEGIN) || !SetEndOfFile(handleOf(m_handle)))
        return lastError();
#else
    if (::ftruncate(static_cast<int>(m_handle), 0) != 0)
        return lastError();
    (void)::lseek(static_cast<int>(m_handle), 0, SEEK_SET);
#endif
    return 0;
}

int SequenceFile::close() noexcept
{
    if (m_handle == -1)
        return 0;
    const std::intptr_t handle = std::exchange(m_handle, -1);
#ifdef _WIN32
    return CloseHandle(handleOf(handle)) ? 0 : lastError();
#else
    return ::close(static_cast<int>(handle)) == 0 ? 0 : lastError();
#endif
}

} // namespace an
