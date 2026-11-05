// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Uncompressed AVI, written frame by frame into a private output.
///
/// An uncompressed AVI - one device-independent bitmap per frame, eight-bit grey through a grey palette
/// or twenty-four-bit colour - is what planetary capture programs record and what stacking programs
/// accept most widely, so writing one is a capability worth keeping exactly. The writer being replaced
/// kept every open file in a process-wide table of sixteen slots while its caller wrote to and closed
/// slot zero whatever slot it had been given, so a second export running at the same time failed and
/// leaked its slot. It stored rows in the order it was handed them into a bitmap the format reads from
/// the bottom, left the bytes that pad a row to four bytes as whatever the heap held, truncated the frame
/// rate to an integer, and past its sixty-second section closed the file and carried on in another one
/// whose name nobody was told.
///
/// **Rows are stored the way the bitmap is read.** A frame described from its top-left corner is written
/// last row first and one described from its bottom-left corner in the order it arrives, so both play
/// the right way up, and every padding byte is zero.
///
/// **The container is one file with room for its whole index.** Frames are grouped into RIFF sections of
/// at most a stated size behind an OpenDML master index with room for a stated number of them. A frame
/// that would need one section more than that is refused rather than written anywhere else, and a frame
/// count stated up front is checked against that room before a byte is written.
///
/// **The frame rate is the descriptor's own fraction.** The container stores a rate as two integers, so
/// 30000/1001 frames per second is stored as exactly that.
///
/// **A sink converts nothing.** Samples must already be eight unsigned bits of mono or RGB; storing
/// colour as blue, green and red and padding each row is how the container holds what it is given.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/IO/FrameSink.h>

#include <cstdint>

namespace an
{

class SequenceOutput;

/// The most bytes one RIFF section of a written container holds, its own header included: one gibibyte,
/// the section size readers with 32-bit offsets were written for. Also the largest a request may state.
inline constexpr std::uint64_t kAviDefaultRiffBytes = 1073741824u;

/// The fewest bytes a request may bound a RIFF section to.
inline constexpr std::uint64_t kAviMinimumRiffBytes = 1048576u;

/// How many RIFF sections a master index has room for when neither the request nor a stated frame count
/// decides it.
inline constexpr std::uint32_t kAviDefaultRiffCapacity = 1024u;

/// The most RIFF sections a request may reserve master-index room for.
inline constexpr std::uint32_t kAviMaximumRiffCapacity = 65536u;

/// The widest and tallest frame an AVI stream header can describe: its frame rectangle holds signed
/// 16-bit coordinates.
inline constexpr std::uint32_t kAviMaximumFrameDimension = 32767u;

/// What a sink does with frames whose description states a colour filter array, which a device-independent
/// bitmap cannot state.
enum class AviMosaicPolicy : std::uint8_t
{
    /// The description is refused with `SequenceFailure::UnsupportedEncoding` naming `cfa`. The default.
    Refuse,

    /// The samples are stored as eight-bit grey and the pattern is not stored, which is what a
    /// stacking program told the pattern by its user expects.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    WriteSamplesAsMono
};

/// How a sink lays out a container. Borrowed by `createAviSink` and copied.
struct AviSinkRequest
{
    /// What happens to frames whose description states a filter array.
    AviMosaicPolicy mosaicFrames = AviMosaicPolicy::Refuse;

    /// The most bytes each RIFF section holds, its own header included: from `kAviMinimumRiffBytes` to
    /// `kAviDefaultRiffBytes`. A smaller bound makes more, smaller sections of the same frames.
    std::uint64_t riffBytes = kAviDefaultRiffBytes;

    /// How many RIFF sections the master index has room for, at most `kAviMaximumRiffCapacity`. Zero
    /// decides: the sections a stated frame count needs, or `kAviDefaultRiffCapacity` when the
    /// descriptor states no count. Each section costs sixteen bytes of header whether or not it is used.
    std::uint32_t riffCapacity = 0u;
};

/// Opens an uncompressed AVI writer over an output's pending payload and writes the header.
///
/// The descriptor must name `SequenceFormat::Avi`, a fixed frame and a nominal frame rate, and advertise
/// no seek; every frame is presented for one period of that rate, so a descriptor that requires
/// timestamps is refused, and a frame's own presentation ticks are not stored. What the container
/// stores:
///
/// | Frame property | Stored | Refused |
/// |---|---|---|
/// | Samples | one unsigned byte | every other representation: `sampleFormat` |
/// | Channels | mono as 8-bit grey through a grey palette; RGB as 24-bit BGR | other schemas: `channelSchema` |
/// | Filter array | none; any pattern under `AviMosaicPolicy::WriteSamplesAsMono` | any pattern by default: `cfa` |
/// | Geometry | either origin, square pixels, sides up to `kAviMaximumFrameDimension` | `pixelAspectRatio`, `extent` |
/// | Arrangement | one plane, or three RGB planes, with or without row padding | others, at append: `layout` |
///
/// Colour basis and numeric domain are not stored. Nothing is written until the request, the
/// descriptor, the engine's limits and the stated frame count have all been accepted, so a refused
/// request leaves the payload as it was. The sink then claims the payload through
/// `SequenceOutput::claimWriter` and reopens it by name, refusing a name that no longer holds a regular
/// file with one link. Appends write each frame with cancellation checkpoints at most 16 MiB apart and
/// close a RIFF section, writing its standard index, whenever the next frame would not fit in it.
/// Finish writes the last section's indexes, the master index and the frame counts, closes the payload
/// and completes the claim, after which the caller commits the output. Closing an unfinished sink
/// discards what it wrote and returns the payload to the output unpublished.
///
/// @param engine the context the output was reserved for; owns the sink's workspace.
/// @param execution an operation of that context; its cancellation is checked before the header is
///        written.
/// @param output a valid, uncommitted output of the same context with no writer; claimed until the sink
///        finishes or closes.
/// @param descriptor the container's description; copied.
/// @param request the container's layout; copied.
/// @return an open sink; `ErrorCode::NotInitialized` for an engine context or execution that is not
///         valid; `ErrorCode::InvalidArgument` naming an option outside its enumeration,
///         `riffBytes` or `riffCapacity` outside its range, or a descriptor that is not AVI, states no
///         fixed frame, advertises a seek or states no nominal frame rate; `ErrorCode::InvalidState` for
///         an output that holds nothing or already has a writer; `ErrorCode::IncompatibleArguments` for
///         an output or execution of another context; `ErrorCode::Unsupported` with
///         `SequenceFailure::UnsupportedEncoding` naming what the container cannot store, required
///         timestamps included; `ErrorCode::LimitExceeded` naming `frameCount` for more frames than the
///         master index has room for, `riffBytes` for a frame that does not fit in one section, or with
///         `SequenceFailure::DescriptorMismatch` for frames beyond the engine's limits;
///         `ErrorCode::ExternalFailure` with `SequenceFailure::IoFailure` carrying the operating system's
///         code; a memory-budget refusal; or the execution's cancellation. An append past the room the
///         master index has fails with `ErrorCode::LimitExceeded` naming `frameCount`.
[[nodiscard]] AFTERNIGHTENGINE_API Result<FrameSink>
createAviSink(const EngineContext& engine, const ExecutionContext& execution, SequenceOutput& output,
              const SequenceDescriptor& descriptor, const AviSinkRequest& request);

} // namespace an
