// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the output transaction's platform boundary: literal paths, exclusive
// reservation, and a handle retained across inspection, readback and flushing, with the writer's
// close checked before publication.
//
// Every platform call the transaction makes is here, so the transaction reasons about files and
// error numbers and the two operating systems differ in one translation unit.

#pragma once

#include "io/registry/PreparedCodecWrite.h"

#include <filesystem>
#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>

namespace an
{
class ExecutionContext;
class Status;

/// The native path for UTF-8 text, decoded as UTF-8 on every platform.
///
/// Constructing a path from a narrow `std::string` decodes it in the process code page on
/// Windows, which is not what a UTF-8 boundary means; this always decodes as UTF-8.
///
/// @param utf8 well-formed UTF-8.
/// @return the native path.
[[nodiscard]] std::filesystem::path outputPathFromUtf8(std::string_view utf8);

/// The UTF-8 text of a native path.
///
/// @param path a native path.
/// @return its UTF-8 spelling.
[[nodiscard]] std::string utf8FromOutputPath(const std::filesystem::path& path);

/// Creates an empty file that must not exist yet, and closes it.
///
/// @param path where to create it.
/// @return zero, or the `errno` value the operating system reported: `EEXIST` when the file is
///         already there, `ENOENT` when its directory is not.
[[nodiscard]] int reservePendingFile(const std::filesystem::path& path) noexcept;

/// The access needed for one pending file.
enum class OutputFileMode
{
    Inspect, ///< An existing payload whose size is read, and whose bytes are read back when asked.
    Flush, ///< An existing payload held open through a handle, so it can be flushed before publication.
    Create ///< A new payload created exclusively, which a streaming encoder writes through this handle.
};

/// One pending output, created or inspected, optionally flushed, and atomically published.
/// Its owning path and any rename storage are prepared before publication can succeed.
class OutputFile final : public CodecOutput
{
public:
    /// Opens the pending payload; size() reports an open failure through its error argument.
    OutputFile(const std::filesystem::path& path, OutputFileMode mode, bool readable = false);
    ~OutputFile() override;
    OutputFile(const OutputFile&) = delete;
    OutputFile& operator=(const OutputFile&) = delete;
    /// The system's open failure, or an empty error for an open file.
    [[nodiscard]] std::error_code openError() const noexcept;
    /// Writes the complete span synchronously, without buffering, or returns false.
    bool write(std::span<const std::byte> bytes) noexcept override;
    /// Preallocates a new payload where supported; actual writes and close remain checked.
    void reserve(std::uint64_t bytes) noexcept override;
    /// Closes once and reports a close failure; the destructor closes an unfinished handle.
    [[nodiscard]] int close() noexcept;
    /// The actual file size, or zero with a system error, including a non-file input.
    [[nodiscard]] std::uint64_t size(std::error_code& error) const noexcept;
    /// Flushes this same file; returns zero or the platform error number.
    [[nodiscard]] int flush() const noexcept;
    /// Reads up to the supplied byte count from the current offset; zero is EOF or an error.
    [[nodiscard]] std::size_t read(void* bytes, std::size_t count, std::error_code& error) const noexcept;
    /// Positions readback at the beginning; returns zero or the platform error number.
    [[nodiscard]] int rewind() const noexcept;
    /// Atomically replaces the destination. Any allocations precede the rename.
    void publish(const std::filesystem::path& destination, std::error_code& error);

private:
    std::filesystem::path m_path;
    std::intptr_t m_handle = -1;
    int m_openError = 0;
    std::uint64_t m_inspectedSize = 0u;
    bool m_inspected = false;
    bool m_created = false;
};

/// Whether a native publication failure may clear when another Windows handle closes.
/// @param error the error returned by OutputFile::publish.
/// @return true for a Windows sharing, lock or access-denied failure; always false on other platforms.
[[nodiscard]] bool isTransientPublicationError(const std::error_code& error) noexcept;

/// Renames the payload over its destination; on Windows a sharing, lock or access conflict is
/// retried every 5 ms for at most 250 ms, and elsewhere the rename is tried once.
///
/// The execution's cancellation and deadline are checked before every attempt and every wait, and a
/// rename that succeeds is a success whatever arrives after it.
///
/// @param file the private payload, already checked and optionally flushed.
/// @param execution the operation authorizing publication.
/// @param destination the final path.
/// @param operation the caller's operation name, copied only into a failure.
/// @return success; the execution's cancellation or deadline; or `ErrorCode::ExternalFailure` with
///         `CodecFailure::PublishFailure` carrying the last platform code.
[[nodiscard]] Status publishOutputFile(OutputFile& file, const ExecutionContext& execution,
                                       const std::filesystem::path& destination, const char* operation);

/// Flushes a directory's entries to the device where the platform supports it, and says nothing
/// where it does not.
///
/// A rename is durable on POSIX only once the directory that holds the new entry is synced;
/// Windows has no equivalent call and needs none. A refusal is not reported: the file's own flush
/// is the guarantee the transaction states, and this is the additional step where it exists.
///
/// @param directory the directory that holds the renamed entry.
void syncDirectoryBestEffort(const std::filesystem::path& directory) noexcept;

/// This process's identity, for a pending name two processes cannot share.
///
/// @return the process identifier.
[[nodiscard]] unsigned long long currentProcessIdentity() noexcept;

} // namespace an
