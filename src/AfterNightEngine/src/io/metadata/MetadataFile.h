// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - a whole file held read-only for a metadata parser.
//
// The metadata a TIFF or a camera RAW file carries addresses its own bytes by absolute offset, so a parser is handed
// the whole file: a part of it cannot be proved to reach every field. The file is mapped copy-on-write and its pages
// belong to the operating system's cache - a parser touches only the pages it reads, no second image-sized block is
// taken, and nothing is counted against the engine context's budget. The bytes are this object's own to write: a parser
// that removes a directory entry before another library reads the structure writes it where it stands, and neither the
// file nor anyone else's view of it changes.

#pragma once

#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/IO/CodecFormat.h>

#include <cstddef>
#include <memory>
#include <span>
#include <string_view>

namespace an
{
class EngineContext;

/// A whole file's bytes, held as this object's own for as long as it lives.
///
/// Move-only. One belongs to one execution; nothing in it is synchronized.
class MetadataFile
{
public:
    /// Holds a whole file for a parser.
    ///
    /// @param engine the context whose largest image bounds the file.
    /// @param path literal UTF-8 disk path.
    /// @param format the codec reading it, which a failure names.
    /// @param container how a failure's operation names the container, as `TIFF`.
    /// @return the file, holding no bytes for an empty one; `ErrorCode::LimitExceeded` carrying the size and the bound
    ///         for a file longer than the context's largest image in bytes; or `ErrorCode::ExternalFailure` when the
    ///         file cannot be measured or opened, and with `CodecFailure::MetadataUnreadable` when it cannot be mapped
    ///         at the size measured.
    [[nodiscard]] static Result<MetadataFile> open(const EngineContext& engine, std::string_view path,
                                                   CodecFormat format, std::string_view container);

    /// Takes over another file's bytes, leaving it holding none.
    ///
    /// @param other the file to take over.
    MetadataFile(MetadataFile&& other) noexcept;

    /// Lets go of what this file held and takes over another's bytes, leaving it holding none.
    ///
    /// @param other the file to take over.
    /// @return this file.
    MetadataFile& operator=(MetadataFile&& other) noexcept;

    MetadataFile(const MetadataFile&) = delete;
    MetadataFile& operator=(const MetadataFile&) = delete;

    /// Unmaps the file.
    ~MetadataFile();

    /// The whole file, writable: a write reaches neither the file nor any other view of it, and copies the one page it
    /// lands in.
    ///
    /// @return its bytes, valid while this object lives and holds them; empty for an empty file or one moved from.
    [[nodiscard]] std::span<std::byte> bytes() const noexcept;

private:
    /// What holds the bytes: a mapping, or a counted block.
    struct State;

    /// Takes the state that holds the bytes.
    ///
    /// @param state the mapping or the block; null holds nothing.
    explicit MetadataFile(std::unique_ptr<State> state) noexcept;

    /// The mapping or the block; null for an empty file or one moved from.
    std::unique_ptr<State> m_state;
};

} // namespace an
