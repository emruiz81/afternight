// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The values a sequence source and sink exchange: a frame with its position, a read's result, a seek
/// request, and a sink's running counts.
///
/// A position is a frame's place in presentation order - never a decoder's packet number - with
/// optional exact timing in the descriptor's ticks. A frame travels with its samples, metadata and
/// position together, and moving one moves its samples without copying or converting a pixel.

#pragma once

#include <AfterNightEngine/IO/SequenceDescriptor.h>
#include <AfterNightEngine/Image/ImageMetadata.h>

namespace an
{
/// Where a frame sits in presentation order, and optionally when.
struct SequencePosition
{
    std::uint64_t index = 0u; ///< The zero-based frame index; `UINT64_MAX` never names a readable frame.
    std::optional<std::int64_t> presentationTicks; ///< When the frame is presented, in time-base ticks; nonnegative.
    std::optional<std::int64_t> durationTicks; ///< How long it is presented, in the same ticks, if known; positive.
};

/// One complete frame.
///
/// A frame a source returned owns its samples or holds a mapping's or decoder's lifetime token, which
/// stays valid through later reads, seeks and the source's close. A sink borrows a frame for one call
/// and keeps no reference to it afterwards.
struct SequenceFrame
{
    RawFrame frame; ///< The samples, with their exact representation, byte order and memory layout.
    ImageMetadata metadata{}; ///< The source facts; a frame a source returns carries no output intent.
    SequencePosition position{}; ///< The index and the optional timing.
};

/// What one read returned: a frame, or end of stream. A failed read is a failed `Result` and never
/// exposes a partial frame.
struct SequenceReadResult
{
    std::optional<SequenceFrame> frame; ///< The frame; absent at end of stream, including for an empty sequence.
};

/// How a source resolves a seek; nothing converts between time and index through a floating-point
/// frame rate.
enum class SequenceSeekKind : std::uint8_t
{
    Index, ///< The exact index.
    TimeAtOrAfter, ///< The first frame presented at or after the requested ticks.
    TimeAtOrBefore ///< The last frame presented at or before the requested ticks.
};

/// A seek: an index or a time, whichever `kind` names; the field the kind does not use must be zero.
struct SequenceSeekRequest
{
    SequenceSeekKind kind = SequenceSeekKind::Index; ///< How the seek is resolved.
    std::uint64_t index = 0u; ///< The index, for an index seek only.
    std::int64_t ticks = 0; ///< The time in the descriptor's ticks, nonnegative, for a time seek only.
};

/// A sink's cumulative progress into its private output; it is not evidence of publication or of
/// durability.
///
/// Only a successful finish makes the whole private container readable, and publishing it is the
/// caller's separate step.
struct SequenceWriteResult
{
    std::uint64_t framesWritten = 0u; ///< The complete frames accepted so far; one more after each append.
    std::uint64_t bytesWritten = 0u; ///< The container bytes written so far, headers included; never decreases.
};
} // namespace an
