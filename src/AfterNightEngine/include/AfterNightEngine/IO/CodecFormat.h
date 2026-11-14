// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The container formats the codec facade knows by name, and the extension table that maps a path
/// onto one.
///
/// A `CodecFormat` names a container, not an installed codec: whether a format can be read or written
/// in an engine context depends on the adapters its `CodecRegistry` holds. The extension table holds
/// every format family and every suffix the engine recognises. Matching a path
/// against it is a hint that selects an adapter, never a content probe, and the developer dump format
/// reserves no extension, so it is only ever selected by name.

#pragma once

#include <AfterNightEngine/Core/Api.h>

#include <cstdint>
#include <span>
#include <string_view>

namespace an
{
/// A container format, named whether or not an adapter for it is installed in an engine context.
enum class CodecFormat : std::uint8_t
{
    Unknown, ///< No format stated; a read or write request resolves one from its path's extension.
    Fits, ///< Flexible Image Transport System.
    Xisf, ///< Extensible Image Serialization Format.
    Raw, ///< Camera raw containers; the adapter identifies the particular camera and container.
    Png, ///< Portable Network Graphics.
    Jpeg, ///< Joint Photographic Experts Group image containers.
    Tiff, ///< Tagged Image File Format.
    Dump ///< The engine's developer dump format; it reserves no extension, so it is selected by name.
};

/// The number of named formats, `Unknown` excluded; it bounds a registry's storage.
inline constexpr std::uint32_t kCodecFormatCount = 7u;

/// Whether a value is one of this build's enumerators, `Unknown` included.
///
/// Allocation-free and thread-safe.
///
/// @param format the value to test.
/// @return true for `Unknown` through `Dump`.
[[nodiscard]] constexpr bool isCodecFormat(CodecFormat format) noexcept
{
    return static_cast<std::uint8_t>(format) <= static_cast<std::uint8_t>(CodecFormat::Dump);
}

/// How a container whose header is markup opens that header.
///
/// XISF is the one such container: its header is an XML document, which its specification opens with an XML
/// declaration and a comment naming the writer. A virus scanner that takes a file for markup by that opening goes
/// through the whole file for tags, and the bytes of normalized 32-bit floating samples are dense in the two
/// characters a tag opens and closes with: Windows' scanner spends up to half a second of a processor on every
/// such file when it is closed, and an open of the file during that pass waits for its end. A writer of many
/// large files that are read again at once - an undo history's steps - asks for the header without that opening.
enum class OutputHeaderProlog : std::uint8_t
{
    /// As the container's specification writes it. The default, and what every reader of the format expects.
    Specified,

    /// Left blank: the declaration and the comment are spaces, of the same length, so nothing else of the unit
    /// moves. The header is still a well-formed XML document and this engine, and PixInsight, read the unit as
    /// they read one written as specified; it departs from the XISF specification, which requires the declaration.
    /// Only the XISF adapter writes it, and only for an image encoded whole: every other container, and a reserved
    /// image, refuse it.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Blank
};

/// Whether a value is one of the prologs this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param prolog the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isOutputHeaderProlog(OutputHeaderProlog prolog) noexcept
{
    return static_cast<std::uint8_t>(prolog) <= static_cast<std::uint8_t>(OutputHeaderProlog::Blank);
}

/// The display name of a format, which says nothing about whether it can be read or written.
///
/// Allocation-free and thread-safe.
///
/// @param format the format to name.
/// @return a string literal with process lifetime - `FITS`, `XISF`, `RAW`, `PNG`, `JPEG`, `TIFF` or
///         `Dump`; `unknown` for `Unknown`; or `UnknownCodecFormat` for a value outside the
///         enumeration.
[[nodiscard]] AFTERNIGHTENGINE_API const char* codecFormatName(CodecFormat format) noexcept;

/// The lowercase filename extensions a format is recognised by, each with its leading dot.
///
/// Allocation-free and thread-safe.
///
/// @param format the format to look up.
/// @return a view of a static table that lives as long as the process; empty for `Unknown`, for
///         `Dump` and for a value outside the enumeration.
[[nodiscard]] AFTERNIGHTENGINE_API std::span<const std::string_view> codecExtensions(CodecFormat format) noexcept;

/// The format a path's extension names, found without opening the file.
///
/// Only the last extension of the final path component is examined - forward slashes and backslashes
/// both separate components - and it is compared with every format's table under ASCII case folding,
/// so neither the process locale nor non-ASCII filename bytes affect the match. A match is a hint for
/// selecting an adapter, not evidence of the file's content or of an installed adapter. Allocation-free
/// and thread-safe.
///
/// @param path a UTF-8 path, borrowed for the call.
/// @return the format the extension names; or `Unknown` for an empty path, a path holding a NUL, a
///         final component with no extension, or an extension no format lists. A caller that knows
///         the format behind an unrecognised suffix states it in the request instead.
[[nodiscard]] AFTERNIGHTENGINE_API CodecFormat codecFormatFromPath(std::string_view path) noexcept;
} // namespace an
