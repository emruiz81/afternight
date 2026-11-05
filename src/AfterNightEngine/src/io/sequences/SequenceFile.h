// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the sequence containers' platform file boundary: one handle, positioned reads,
// sequential and positioned writes, truncation, and the operating system's own error numbers.
//
// Every platform call a sequence adapter makes is here, so an adapter handles bytes and offsets and
// never a handle type, and the two operating systems differ in one translation unit rather than in
// each format.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>

namespace an
{

/// One container file, opened for positioned reads of an existing file or for writing a reserved one.
///
/// Move-only, and closed on destruction. Not thread-safe: a handle belongs to one session, which
/// serializes its calls.
class SequenceFile
{
public:
    /// What an operation reports when a transfer ended early and the platform stated no error.
    static constexpr int kShortTransfer = -1;

    /// A handle that holds nothing.
    SequenceFile() noexcept = default;

    /// Closes the handle, ignoring a close failure; `close` is how to observe one.
    ~SequenceFile();

    /// Takes over another handle, leaving it holding nothing.
    ///
    /// @param other the handle to take over.
    SequenceFile(SequenceFile&& other) noexcept;

    /// Closes this handle and takes over another.
    ///
    /// @param other the handle to take over.
    /// @return this handle.
    SequenceFile& operator=(SequenceFile&& other) noexcept;

    /// A handle has one owner.
    SequenceFile(const SequenceFile&) = delete;

    /// A handle has one owner.
    SequenceFile& operator=(const SequenceFile&) = delete;

    /// Opens an existing regular file for positioned reads, closing whatever this handle held.
    ///
    /// The file stays shared for reading, writing and deletion by other handles, and the platform is
    /// told that reads will mostly be sequential. The open itself never waits: a named pipe or a device
    /// that would block until something opened its other end is refused at once.
    ///
    /// @param path the file.
    /// @return zero, or the platform's error number; a directory, a pipe or a device is refused.
    [[nodiscard]] int openForReading(const std::filesystem::path& path) noexcept;

    /// Opens an existing regular file for writing from its first byte, discarding what it holds.
    ///
    /// A file reserved earlier is reopened by name, so the name must still be that file: a symbolic link
    /// or other reparse point is opened as itself rather than followed, and it, a directory, a pipe, a
    /// device and a file with a second name are all refused before a byte is discarded. On Windows the
    /// handle is shared for reading only, so nothing renames or removes the file while it is written.
    ///
    /// @param path the file, which must exist.
    /// @return zero, or the platform's error number.
    [[nodiscard]] int openForWriting(const std::filesystem::path& path) noexcept;

    /// Whether the handle holds a file.
    ///
    /// @return true between a successful open and `close`.
    [[nodiscard]] bool isOpen() const noexcept;

    /// The file's current size.
    ///
    /// @param bytes receives the size on success.
    /// @return zero, or the platform's error number.
    [[nodiscard]] int size(std::uint64_t& bytes) const noexcept;

    /// Reads up to a count of bytes at an offset, stopping early only at the end of the file.
    ///
    /// @param destination where the bytes go; at least `count` bytes.
    /// @param count how many bytes to read.
    /// @param offset where in the file to start.
    /// @param received receives how many bytes arrived, which is less than `count` only at the end.
    /// @return zero, or the platform's error number.
    [[nodiscard]] int readAt(std::byte* destination, std::size_t count, std::uint64_t offset,
                             std::size_t& received) const noexcept;

    /// Writes every byte at the current position.
    ///
    /// @param bytes what to write.
    /// @return zero, `kShortTransfer` when the platform accepted nothing without an error, or the
    ///         platform's error number.
    [[nodiscard]] int write(std::span<const std::byte> bytes) noexcept;

    /// Writes every byte at an offset. Where the platform's positioned write moves the current
    /// position, the position afterwards is unspecified.
    ///
    /// @param bytes what to write.
    /// @param offset where in the file to start.
    /// @return zero, `kShortTransfer`, or the platform's error number.
    [[nodiscard]] int writeAt(std::span<const std::byte> bytes, std::uint64_t offset) noexcept;

    /// Discards the file's contents, leaving it empty and positioned at its start.
    ///
    /// @return zero, or the platform's error number.
    [[nodiscard]] int truncate() noexcept;

    /// Closes once; a later call and a handle holding nothing report success.
    ///
    /// @return zero, or the platform's error number.
    int close() noexcept;

private:
    /// The platform handle or descriptor, or -1.
    std::intptr_t m_handle = -1;
};

} // namespace an
