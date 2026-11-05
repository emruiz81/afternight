// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - a FITS file opened a second time for positioned reads of its payload.
//
// The FITS adapter reads an image the library would only copy or flip straight from its payload.
// This is the handle it reads through: the platform's own, beside the library's, so the platform
// header and the library's never meet in one translation unit.

#pragma once

#include <AfterNightEngine/Core/Status.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace an
{
/// A FITS file opened a second time, beside the library's handle, for positioned reads of its
/// payload. Closed on destruction; one per decode, and not shared between threads.
class FitsPayloadFile
{
public:
    /// Holds no file.
    FitsPayloadFile() = default;
    /// Closes the file.
    ~FitsPayloadFile();
    FitsPayloadFile(const FitsPayloadFile&) = delete;
    FitsPayloadFile& operator=(const FitsPayloadFile&) = delete;
    /// Whether a file is open.
    /// @return true after a successful `open`.
    [[nodiscard]] bool isOpen() const noexcept
    {
        return m_handle != -1;
    }
    /// Opens `path` for reading, telling the system the reads move forward through it.
    /// @param path the UTF-8 path.
    /// @return success, or the failure naming the file.
    Status open(std::string_view path);
    /// Reads exactly `bytes.size()` bytes at `offset`.
    /// @param bytes where they go.
    /// @param offset the file offset of the first.
    /// @return success, or a failure naming the file when it ends first.
    Status readAt(std::span<std::byte> bytes, std::uint64_t offset) const;

private:
    std::intptr_t m_handle = -1; ///< The platform handle, or -1.
    std::string m_path; ///< The path, for a failure's message.
};
} // namespace an
