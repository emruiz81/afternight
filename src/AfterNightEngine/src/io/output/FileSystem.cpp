// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the output transaction's file operations on Windows and POSIX.
//
// Paths are decoded as UTF-8 on both systems. On Windows a payload reopened through a handle is
// renamed through that handle, so the file checked and flushed is the file renamed, while one the
// transaction created, or only inspected by its size, is renamed by name; on POSIX every rename is by
// name, and the transaction syncs the directory afterwards when the payload was flushed.

#include "io/output/FileSystem.h"

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <limits>
#include <new>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <io.h>
#include <process.h>
#include <share.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
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
// This byte/file boundary uses no Windows locale services.
#ifndef NONLS
#define NONLS
#endif
#include <Windows.h>
#else
#include <unistd.h>
#endif

namespace an
{

std::filesystem::path outputPathFromUtf8(std::string_view utf8)
{
    return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

std::string utf8FromOutputPath(const std::filesystem::path& path)
{
    const auto text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

int reservePendingFile(const std::filesystem::path& path) noexcept
{
#ifdef _WIN32
    int descriptor = -1;
    // Reservation creates a name, not a payload. A read-only handle avoids announcing a
    // completed write when this empty file is closed; the encoder owns the actual write.
    const errno_t error = _wsopen_s(&descriptor, path.c_str(), _O_RDONLY | _O_CREAT | _O_EXCL | _O_BINARY, _SH_DENYNO,
                                    _S_IREAD | _S_IWRITE);
    if (error != 0)
        return error;
    (void)_close(descriptor);
    return 0;
#else
    const int descriptor = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if (descriptor < 0)
        return errno;
    (void)::close(descriptor);
    return 0;
#endif
}

OutputFile::OutputFile(const std::filesystem::path& path, OutputFileMode mode, bool readable) : m_path(path)
{
    m_created = mode == OutputFileMode::Create;
#ifdef _WIN32
    if (mode == OutputFileMode::Inspect && !readable)
    {
        WIN32_FILE_ATTRIBUTE_DATA info{};
        if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info))
        {
            m_openError = static_cast<int>(GetLastError());
            return;
        }
        // A close-only transaction with no readback needs only the completed file's size.
        // Reparse points retain handle-based inspection so links still describe their target.
        if ((info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0u)
        {
            if ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0u)
                m_openError = ERROR_DIRECTORY;
            else
            {
                m_inspectedSize = (static_cast<std::uint64_t>(info.nFileSizeHigh) << 32u) | info.nFileSizeLow;
                m_inspected = true;
            }
            return;
        }
    }
    const bool writable = mode != OutputFileMode::Inspect;
    const auto handle =
        CreateFileW(path.c_str(),
                    (m_created ? 0u : DELETE) | FILE_READ_ATTRIBUTES | (writable ? GENERIC_WRITE : 0u) |
                        (readable ? GENERIC_READ : 0u),
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                    mode == OutputFileMode::Create ? CREATE_NEW : OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    m_handle = reinterpret_cast<std::intptr_t>(handle);
    if (handle == INVALID_HANDLE_VALUE)
        m_openError = static_cast<int>(GetLastError());
#else
    const auto flags = mode == OutputFileMode::Create ? (readable ? O_RDWR : O_WRONLY) | O_CREAT | O_EXCL | O_CLOEXEC
                                                      : O_RDONLY | O_CLOEXEC;
    if (mode == OutputFileMode::Inspect && !readable)
    {
        struct stat info{};
        if (::stat(path.c_str(), &info) != 0)
            m_openError = errno;
        else if (!S_ISREG(info.st_mode) || info.st_size < 0)
            m_openError = EINVAL;
        else
        {
            m_inspectedSize = static_cast<std::uint64_t>(info.st_size);
            m_inspected = true;
        }
        return;
    }
    m_handle = ::open(path.c_str(), flags, 0644);
    if (m_handle < 0)
        m_openError = errno;
#endif
}

OutputFile::~OutputFile()
{
    (void)close();
}

int OutputFile::close() noexcept
{
    if (m_handle == -1)
        return 0;
    const auto handle = m_handle;
    m_handle = -1;
#ifdef _WIN32
    return CloseHandle(reinterpret_cast<HANDLE>(handle)) ? 0 : static_cast<int>(GetLastError());
#else
    return ::close(static_cast<int>(handle)) == 0 ? 0 : errno;
#endif
}

std::error_code OutputFile::openError() const noexcept
{
    return std::error_code(m_openError, std::system_category());
}

bool OutputFile::write(std::span<const std::byte> bytes) noexcept
{
    if (m_handle == -1)
        return false;
    while (!bytes.empty())
    {
#ifdef _WIN32
        DWORD written = 0u;
        const auto count = bytes.size() > MAXDWORD ? MAXDWORD : static_cast<DWORD>(bytes.size());
        if (!WriteFile(reinterpret_cast<HANDLE>(m_handle), bytes.data(), count, &written, nullptr) || written == 0u)
            return false;
#else
        ssize_t written;
        do
            written = ::write(static_cast<int>(m_handle), bytes.data(), bytes.size());
        while (written < 0 && errno == EINTR);
        if (written <= 0)
            return false;
#endif
        bytes = bytes.subspan(static_cast<std::size_t>(written));
    }
    return true;
}

void OutputFile::reserve(std::uint64_t bytes) noexcept
{
#ifdef _WIN32
    if (m_handle == -1 || bytes > static_cast<std::uint64_t>((std::numeric_limits<LONGLONG>::max)()))
        return;
    FILE_ALLOCATION_INFO allocation{};
    allocation.AllocationSize.QuadPart = static_cast<LONGLONG>(bytes);
    (void)SetFileInformationByHandle(reinterpret_cast<HANDLE>(m_handle), FileAllocationInfo, &allocation,
                                     sizeof(allocation));
    FILE_END_OF_FILE_INFO extent{};
    extent.EndOfFile.QuadPart = static_cast<LONGLONG>(bytes);
    (void)SetFileInformationByHandle(reinterpret_cast<HANDLE>(m_handle), FileEndOfFileInfo, &extent, sizeof(extent));
#else
    (void)bytes;
#endif
}

std::uint64_t OutputFile::size(std::error_code& error) const noexcept
{
    error.clear();
    if (m_openError != 0)
    {
        error = std::error_code(m_openError, std::system_category());
        return 0u;
    }
    if (m_inspected)
        return m_inspectedSize;
#ifdef _WIN32
    FILE_STANDARD_INFO info{};
    if (!GetFileInformationByHandleEx(reinterpret_cast<HANDLE>(m_handle), FileStandardInfo, &info, sizeof(info)))
        error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
    else if (info.Directory || info.EndOfFile.QuadPart < 0)
        error = std::make_error_code(std::errc::invalid_argument);
    else
        return static_cast<std::uint64_t>(info.EndOfFile.QuadPart);
#else
    struct stat info{};
    if (::fstat(static_cast<int>(m_handle), &info) != 0)
        error = std::error_code(errno, std::system_category());
    else if (!S_ISREG(info.st_mode) || info.st_size < 0)
        error = std::make_error_code(std::errc::invalid_argument);
    else
        return static_cast<std::uint64_t>(info.st_size);
#endif
    return 0u;
}

int OutputFile::flush() const noexcept
{
    if (m_openError != 0)
        return m_openError;
#ifdef _WIN32
    return FlushFileBuffers(reinterpret_cast<HANDLE>(m_handle)) ? 0 : static_cast<int>(GetLastError());
#else
    return ::fsync(static_cast<int>(m_handle)) == 0 ? 0 : errno;
#endif
}

std::size_t OutputFile::read(void* bytes, std::size_t count, std::error_code& error) const noexcept
{
    error.clear();
#ifdef _WIN32
    DWORD received = 0u;
    if (count > MAXDWORD)
        error = std::make_error_code(std::errc::invalid_argument);
    else if (!ReadFile(reinterpret_cast<HANDLE>(m_handle), bytes, static_cast<DWORD>(count), &received, nullptr))
        error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
    else
        return received;
#else
    ssize_t received;
    do
        received = ::read(static_cast<int>(m_handle), bytes, count);
    while (received < 0 && errno == EINTR);
    if (received < 0)
        error = std::error_code(errno, std::system_category());
    else
        return static_cast<std::size_t>(received);
#endif
    return 0u;
}

int OutputFile::rewind() const noexcept
{
#ifdef _WIN32
    LARGE_INTEGER beginning{};
    return SetFilePointerEx(reinterpret_cast<HANDLE>(m_handle), beginning, nullptr, FILE_BEGIN)
               ? 0
               : static_cast<int>(GetLastError());
#else
    return ::lseek(static_cast<int>(m_handle), 0, SEEK_SET) < 0 ? errno : 0;
#endif
}

void OutputFile::publish(const std::filesystem::path& destination, std::error_code& error)
{
#ifdef _WIN32
    if (m_inspected || m_created)
    {
        if (!MoveFileExW(m_path.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING))
            error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
        else
            error.clear();
        return;
    }
    // A handle rename avoids reopening and resolving the source after its size/flush check.
    // Keep the variable-length record aligned and NUL terminated: the Win32 path parser reads
    // a string even though the record also carries its byte count. Allocate before renaming.
    const auto target = destination.is_absolute() ? destination : std::filesystem::absolute(destination);
    const auto nameBytes = target.native().size() * sizeof(wchar_t);
    const auto bytes = offsetof(FILE_RENAME_INFO, FileName) + nameBytes + sizeof(wchar_t);
    std::vector<std::uint64_t> storage((bytes + sizeof(std::uint64_t) - 1u) / sizeof(std::uint64_t));
    auto* info = ::new (storage.data()) FILE_RENAME_INFO{};
    info->ReplaceIfExists = TRUE;
    info->RootDirectory = nullptr;
    info->FileNameLength = static_cast<DWORD>(nameBytes);
    std::memcpy(info->FileName, target.c_str(), nameBytes);
    if (!SetFileInformationByHandle(reinterpret_cast<HANDLE>(m_handle), FileRenameInfo, info,
                                    static_cast<DWORD>(bytes)))
        error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
    else
        error.clear();
#else
    std::filesystem::rename(m_path, destination, error);
#endif
}

bool isTransientPublicationError(const std::error_code& error) noexcept
{
#ifdef _WIN32
    return error.category() == std::system_category() &&
           (error.value() == ERROR_SHARING_VIOLATION || error.value() == ERROR_LOCK_VIOLATION ||
            error.value() == ERROR_ACCESS_DENIED);
#else
    (void)error;
    return false;
#endif
}

void syncDirectoryBestEffort(const std::filesystem::path& directory) noexcept
{
#ifdef _WIN32
    (void)directory;
#else
    const std::filesystem::path target = directory.empty() ? std::filesystem::path{"."} : directory;
    const int descriptor = ::open(target.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (descriptor < 0)
        return;
    (void)::fsync(descriptor);
    (void)::close(descriptor);
#endif
}

unsigned long long currentProcessIdentity() noexcept
{
#ifdef _WIN32
    return static_cast<unsigned long long>(_getpid());
#else
    return static_cast<unsigned long long>(::getpid());
#endif
}

} // namespace an
