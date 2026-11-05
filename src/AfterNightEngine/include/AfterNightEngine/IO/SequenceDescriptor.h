// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What a sequence of frames is: its container, its frames' description, its count and its timeline.
///
/// A `SequenceDescriptor` is what a sequence source or sink states about itself when it is opened, and
/// what the checked handles hold every frame and every seek to. Absence always means unknown, never
/// zero: a missing count is a sequence whose end is found by reading, and a count of zero is an empty
/// one. A fixed frame description makes every frame match it; leaving it out is how a sequence states
/// that geometry and representation may change from frame to frame. Time is exact - a rational time
/// base and integer ticks - and a nominal frame rate is informational and never used to invent a
/// timestamp.

#pragma once

#include <AfterNightEngine/IO/SequenceError.h>
#include <AfterNightEngine/Image/RawFrame.h>

#include <optional>

namespace an
{
/// A sequence's container; it describes, and promises nothing about an installed adapter.
enum class SequenceFormat : std::uint8_t
{
    ImageList, ///< An explicit list of still images.
    Ser, ///< SER astronomical video.
    Avi, ///< AVI video, including uncompressed astronomical recordings.
    Mp4, ///< MP4 video.
    Fits, ///< A FITS frame container.
    Dummy, ///< Deterministic generated frames.
    Video ///< Another explicitly selected video container.
};

/// A sequence handle's lifecycle. A default handle is `Closed`; a successful factory returns `Open`.
enum class SequenceState : std::uint8_t
{
    Closed, ///< Resources released; closing again does nothing.
    Open, ///< Ready to read, seek or append.
    EndOfStream, ///< A source reached its end; reads return the end again, and a seek leaves it.
    Finalized, ///< A sink completed its private output; appending is refused.
    Failed ///< A backend call may have changed private state; only close and destruction remain.
};

/// An exact positive rational; the field holding it says what it measures, and nothing rounds it.
struct SequenceRate
{
    std::uint32_t numerator = 1u; ///< The numerator; positive, not necessarily reduced.
    std::uint32_t denominator = 1u; ///< The denominator; positive.
};

/// What a sequence is and what its handle can do, copied by the factory that opens it.
///
/// Holds no pixels and no external handle. The caller must not change a descriptor while a factory is
/// reading it.
struct SequenceDescriptor
{
    SequenceFormat format = SequenceFormat::ImageList; ///< The declared container, never inferred from an extension.
    /// The description every frame must match; absent when frames describe themselves one by one.
    std::optional<RawFrameDescriptor> fixedFrame;
    /// The exact number of frames, zero included; absent when unknown. End of stream is not counted.
    std::optional<std::uint64_t> frameCount;
    /// Seconds per tick; timestamps count ticks from the sequence's origin. Absent when frames carry
    /// no time.
    std::optional<SequenceRate> timeBase;
    /// Frames per second, informational only; never used to make up a timestamp.
    std::optional<SequenceRate> nominalFrameRate;
    std::optional<std::int64_t> durationTicks; ///< The timeline's exclusive end, nonnegative; needs `timeBase`.
    bool timestampsRequired = false; ///< Whether every frame must carry presentation ticks; needs `timeBase`.
    /// Whether a source can seek to an exact zero-based index, including a stated `frameCount`, which is
    /// end of stream.
    bool seekByIndex = false;
    bool seekByTime = false; ///< Whether a source supports the two time seeks; needs `timeBase`.
};

/// Checks a descriptor's own coherence, without touching any storage.
///
/// Frame geometry and byte size are checked against the engine context's limits separately, when a
/// handle is created and on every frame. Thread-safe.
///
/// @param descriptor the description to check.
/// @return success; or `ErrorCode::InvalidArgument` with `SequenceFailure::InvalidDescriptor` for a
///         format outside the enumeration, a time base or nominal frame rate with a zero term, a
///         negative duration, or a time seek, required timestamps or a duration with no time base.
[[nodiscard]] AFTERNIGHTENGINE_API Status validateSequenceDescriptor(const SequenceDescriptor& descriptor) noexcept;
} // namespace an
