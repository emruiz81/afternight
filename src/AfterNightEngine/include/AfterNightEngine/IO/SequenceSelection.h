// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Which sequence container a path names, by its extension.
///
/// A sequence descriptor never infers its container: the format a source or a sink states is the one its
/// caller chose. A consumer that opens a file by name still needs to choose it from the path's extension -
/// `.ser` for SER, `.anseq` for an image list, and seven video extensions read through FFmpeg. This is that
/// table, asked for by name, with no file access, so the choice stays the caller's and the rule it follows
/// is written down once.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/IO/SequenceDescriptor.h>

#include <optional>
#include <string_view>

namespace an
{

/// The sequence container a path's extension names.
///
/// The extension is what follows the last dot of the last path component, compared without regard to
/// ASCII case: `ser` is `SequenceFormat::Ser`, `anseq` is `SequenceFormat::ImageList`, `avi` is
/// `SequenceFormat::Avi`, `mp4` and `mov` - one container family, which the FFmpeg source reports as MP4 -
/// are `SequenceFormat::Mp4`, and `mpg`, `mpeg`, `mkv` and `webm` are `SequenceFormat::Video`. Nothing is
/// opened, so the answer says what the name claims, not what the file holds.
///
/// Thread-safe and allocation-free.
///
/// @param path a UTF-8 path; either separator ends a component.
/// @return the container; or an empty optional for any other extension, for none, and for a path that is
///         empty or holds a NUL.
[[nodiscard]] AFTERNIGHTENGINE_API std::optional<SequenceFormat> sequenceFormatForPath(std::string_view path) noexcept;

/// Whether a container is read through the FFmpeg source.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param format the container.
/// @return true for `SequenceFormat::Avi`, `SequenceFormat::Mp4` and `SequenceFormat::Video`.
[[nodiscard]] constexpr bool readsThroughFfmpeg(SequenceFormat format) noexcept
{
    return format == SequenceFormat::Avi || format == SequenceFormat::Mp4 || format == SequenceFormat::Video;
}

} // namespace an
