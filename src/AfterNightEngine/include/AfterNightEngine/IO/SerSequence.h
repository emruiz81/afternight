// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// SER astronomical video, read and written as frames that say what they are.
///
/// SER is a fixed 178-byte header, a run of equally sized frames and an optional trailer of one
/// timestamp per frame. The format is simple enough to read with one seek and one `fread` into
/// whatever the header claims, which is why this adapter judges every field before a byte of pixel
/// memory is requested - a negative width, a count the payload cannot hold and a trailer shorter than
/// the frames it describes never reach arithmetic or allocation - and a refusal names the field it
/// refused.
///
/// **A frame arrives as the file stored it.** Two-byte samples keep their stored byte order and the
/// frame states which; nothing is swapped on read, because a swap is a conversion with a cost and
/// `Image/ImageConversion.h` is where conversions are paid for. The one rearrangement is BGR order,
/// which the channel vocabulary cannot state: those frames are delivered as RGB, and the container
/// description says the order was exchanged.
///
/// **The byte-order flag is read under a stated convention.** The published specification says a flag
/// of one means little-endian samples; the first programs to write SER did the opposite, and most
/// readers since follow those programs. Both readings exist in files, so neither is assumed silently:
/// `SerEndianConvention` names the one a caller wants, and its default is the one the files in
/// circulation were written under.
///
/// **Timestamps are facts first and a timeline only when they are one.** A frame's trailer entry
/// becomes its acquisition record's exposure start whenever it names an instant. The entries become the
/// sequence's presentation timeline only when every frame has one and none goes backwards, because a
/// decreasing timeline breaks the order every source promises; a caller that needs a timeline can
/// require one and be refused instead.
///
/// **Writing goes through a private output.** A sink writes its container into the pending payload an
/// `IO/SequenceOutput.h` reserved, and the caller publishes it once the sink has finished. Samples must
/// already be what the container stores - eight or sixteen unsigned bits, mono, Bayer or RGB - because
/// a sink converts nothing; it only arranges interleaved RGB rows and drops row padding, which is how
/// the container stores what it is given.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/IO/FrameSink.h>
#include <AfterNightEngine/IO/FrameSource.h>
#include <AfterNightEngine/Image/AcquisitionMetadata.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/RawFrame.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace an
{

class SequenceOutput;

/// The size of a SER container's fixed header, in bytes. The first frame starts immediately after it.
inline constexpr std::size_t kSerHeaderBytes = 178u;

/// How many SER ticks lie between 0001-01-01T00:00:00 and the Unix epoch. One tick is 100 nanoseconds.
inline constexpr std::uint64_t kSerUnixEpochTicks = 621355968000000000u;

/// How many SER ticks make one second.
inline constexpr std::uint64_t kSerTicksPerSecond = 10000000u;

/// Which reading of the header's byte-order flag applies to two-byte samples.
enum class SerEndianConvention : std::uint8_t
{
    /// A flag of one means big-endian samples and any other value little-endian ones: the reading the
    /// earliest writers used and most readers follow. A writer under it stores zero for little-endian
    /// samples and one for big-endian ones. The default.
    Compatible,

    /// A flag of zero means big-endian samples and any other value little-endian ones, as the
    /// published specification states. A writer under it stores one for little-endian samples.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Specification
};

/// How a source decides how many frames a container holds.
enum class SerFrameCountPolicy : std::uint8_t
{
    /// The header's count is the count. A payload too short for it is refused, and so is a count of
    /// zero over a payload that holds a whole frame. The default.
    Exact,

    /// When the header's count is zero or more than the payload holds, the count is the number of
    /// complete frames the payload holds - the answer for a recording that stopped before its count was
    /// written. A partial last frame is ignored and no trailer is read. A count the payload holds is
    /// used as it stands, exactly as under `Exact`.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    CompleteFrames
};

/// Whether a trailer's timestamps become a source's presentation timeline.
enum class SerTimelinePolicy : std::uint8_t
{
    /// A timeline when a trailer is present, every entry names an instant and none decreases;
    /// otherwise none. The default.
    WhenOrdered,

    /// No timeline, whatever the trailer holds. Exposure starts are still reported per frame.
    Omit,

    /// A timeline, or the container is refused with `SequenceFailure::InvalidTimestamps`.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Require
};

/// What a source does with a cyan, yellow and magenta mosaic, which the filter-array vocabulary cannot
/// describe.
enum class SerCmyMosaicPolicy : std::uint8_t
{
    /// Refused with `SequenceFailure::UnsupportedEncoding`. The default.
    Refuse,

    /// Delivered as one plane of sensor codes claiming no filter array; the stored colour identifier
    /// stays in each frame's cards. A consumer that treats such a frame as monochrome sees the
    /// mosaic as structure.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    DescribeAsMono
};

/// Where a sink takes its trailer's timestamps from.
enum class SerTimestampSource : std::uint8_t
{
    /// No trailer is written. The default.
    None,

    /// Each frame's presentation ticks added to `SerSinkRequest::timelineOriginTicks`. The sink's
    /// descriptor must state a time base of exactly one SER tick and require timestamps.
    PresentationTicks,

    /// Each frame's acquisition exposure start, converted by `serTicksFromUtcInstant`; a frame that
    /// states none is written as zero, which a reader treats as unknown.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    ExposureStart
};

/// How a source reads a container. Copied by every entry point that takes one.
struct SerSourceOptions
{
    /// How the byte-order flag is read.
    SerEndianConvention endianConvention = SerEndianConvention::Compatible;

    /// How the frame count is decided.
    SerFrameCountPolicy frameCountPolicy = SerFrameCountPolicy::Exact;

    /// Whether the timestamps become a timeline.
    SerTimelinePolicy timelinePolicy = SerTimelinePolicy::WhenOrdered;

    /// What happens to a CMY mosaic.
    SerCmyMosaicPolicy cmyMosaicPolicy = SerCmyMosaicPolicy::Refuse;

    /// The basis of a colour container's channels. SER states none, so the default claims none; a mono
    /// or mosaic container admits nothing else.
    ColorSpace colorSpace = ColorSpace::Unspecified;

    /// What the stored numbers mean. SER states nothing, and a recording holds what its sensor
    /// reported, so the default is a sensor code; a physically scaled domain is refused because the
    /// samples are integers.
    NumericDomain numericDomain = NumericDomain::RawSensorCode;
};

/// The fixed header fields, as a container stores them.
///
/// Numbers are the stored little-endian values whether or not they make sense; the inspection entry
/// points are what judge them. A text field is the bytes before its first NUL with trailing space
/// padding removed, read as UTF-8 when it is valid UTF-8 and as Latin-1 otherwise, so every string
/// here is valid UTF-8 and a byte the file held is never dropped.
struct SerHeader
{
    /// The 14-byte format identifier, conventionally `LUCAM-RECORDER`. Reported and never validated.
    std::string fileId{};

    /// The camera series identifier. Reported and never interpreted.
    std::int32_t luId = 0;

    /// The colour identifier: 0 mono, 8 to 11 the four Bayer mosaics, 16 to 19 the CMY mosaics, 100 RGB
    /// and 101 BGR.
    std::int32_t colorId = 0;

    /// The byte-order flag, whose meaning is a `SerEndianConvention`.
    std::int32_t littleEndian = 0;

    /// Pixels per row, as stored.
    std::int32_t imageWidth = 0;

    /// Rows per frame, as stored.
    std::int32_t imageHeight = 0;

    /// Significant bits per sample: 1 to 8 store one byte and 9 to 16 store two. Samples are never
    /// shifted by it.
    std::int32_t pixelDepthPerPlane = 0;

    /// The stored frame count, read as unsigned.
    std::uint32_t frameCount = 0u;

    /// Who recorded the sequence, from the 40-byte field.
    std::string observer{};

    /// The camera, from the 40-byte field.
    std::string instrument{};

    /// The telescope, from the 40-byte field.
    std::string telescope{};

    /// The recording's local start time in SER ticks; zero means unknown.
    std::uint64_t dateTimeTicks = 0u;

    /// The recording's UTC start time in SER ticks; zero means unknown.
    std::uint64_t dateTimeUtcTicks = 0u;
};

/// What a source makes of a container under a set of options.
///
/// Built only by the inspection entry points, so every value in one is consistent with the others:
/// `kSerHeaderBytes + frameCount * layout.totalBytes()`, plus eight bytes per frame when
/// `trailerPresent`, plus `trailingBytes`, is exactly `fileBytes`.
struct SerContainer
{
    /// The header as stored.
    SerHeader header;

    /// Every frame's description: its extent, representation, stored byte order, channels, filter
    /// array, a top-left origin and square pixels.
    RawFrameDescriptor frame;

    /// Every frame's layout, without row padding: one plane, with the channels interleaved.
    ImageLayout layout;

    /// How many frames a source delivers.
    std::uint64_t frameCount;

    /// The container's size in bytes.
    std::uint64_t fileBytes;

    /// Bytes after the frames, and after the trailer when there is one, that are neither: a partial
    /// frame, a partial trailer or data some writer appended. Never read.
    std::uint64_t trailingBytes;

    /// Whether a complete trailer of one eight-byte timestamp per frame follows the frames.
    bool trailerPresent;

    /// Whether `SerFrameCountPolicy::CompleteFrames` replaced the header's count.
    bool frameCountRecovered;

    /// Whether the container stores BGR order and frames are delivered with blue and red exchanged.
    bool channelOrderExchanged;
};

/// What a container file holds, including what its trailer says.
struct SerFileInfo
{
    /// The container, as a source reads it.
    SerContainer container;

    /// How many trailer entries name an instant. Zero when there is no trailer.
    std::uint64_t timestampCount;

    /// Whether a trailer is present, every entry names an instant and none decreases.
    bool timestampsOrdered;

    /// Whether a source reading under the same options states a presentation timeline.
    bool timeline;

    /// The first frame's exposure start, when its entry names one.
    std::optional<UtcInstant> firstTimestamp;

    /// The last frame's exposure start, when its entry names one.
    std::optional<UtcInstant> lastTimestamp;

    /// Frames per second over the instants the trailer names: one less than their count, divided by the
    /// seconds between the earliest and the latest.
    /// Absent below two instants or when they do not differ. Informational; nothing is derived from it.
    std::optional<double> timestampFrameRate;
};

/// How a sink writes a container's header and trailer. Borrowed by `createSerSink` and copied.
struct SerSinkRequest
{
    /// How the byte-order flag is written for two-byte samples; one-byte samples store the value this
    /// convention gives little-endian ones.
    SerEndianConvention endianConvention = SerEndianConvention::Compatible;

    /// Significant bits per sample: zero states the full sample width, and otherwise 1 to 8 for one-byte
    /// samples and 9 to 16 for two-byte ones. Samples are written as they are, never shifted.
    std::uint32_t pixelDepthPerPlane = 0u;

    /// The format identifier: at most 14 bytes of UTF-8 without NUL.
    std::string_view fileId = "LUCAM-RECORDER";

    /// The camera series identifier, stored as given.
    std::int32_t luId = 0;

    /// Who recorded the sequence: at most 40 bytes of UTF-8 without NUL.
    std::string_view observer{};

    /// The camera: at most 40 bytes of UTF-8 without NUL.
    std::string_view instrument{};

    /// The telescope: at most 40 bytes of UTF-8 without NUL.
    std::string_view telescope{};

    /// The recording's local start time in SER ticks; zero states it unknown. At most `INT64_MAX`.
    std::uint64_t dateTimeTicks = 0u;

    /// The recording's UTC start time in SER ticks; zero states it unknown. At most `INT64_MAX`.
    std::uint64_t dateTimeUtcTicks = 0u;

    /// Where the trailer's timestamps come from.
    SerTimestampSource timestamps = SerTimestampSource::None;

    /// The SER ticks presentation tick zero stands for. Nonzero with
    /// `SerTimestampSource::PresentationTicks`, so no frame is written as unknown, and zero otherwise.
    std::uint64_t timelineOriginTicks = 0u;
};

/// The instant a SER tick count names.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param ticks 100-nanosecond ticks since 0001-01-01T00:00:00 UTC.
/// @return the instant; or an empty optional for zero, which SER uses for unknown, and for a count
///         whose instant lies outside the nanosecond range `UtcInstant` holds, roughly 1677 to 2262.
[[nodiscard]] constexpr std::optional<UtcInstant> utcInstantFromSerTicks(std::uint64_t ticks) noexcept
{
    constexpr std::uint64_t kMaxTickDistance = 92233720368547758u;
    if (ticks == 0u)
    {
        return std::nullopt;
    }
    if (ticks >= kSerUnixEpochTicks)
    {
        const std::uint64_t after = ticks - kSerUnixEpochTicks;
        if (after > kMaxTickDistance)
        {
            return std::nullopt;
        }
        return UtcInstant{static_cast<std::int64_t>(after) * 100};
    }
    const std::uint64_t before = kSerUnixEpochTicks - ticks;
    if (before > kMaxTickDistance)
    {
        return std::nullopt;
    }
    return UtcInstant{-static_cast<std::int64_t>(before) * 100};
}

/// The SER tick count of an instant.
///
/// A remainder finer than one tick is discarded toward the earlier tick, except for the eight instants
/// before the earliest tick `utcInstantFromSerTicks` can read back, which take that tick. Every result
/// therefore reads back as an instant less than one tick from the original, and none of them is zero,
/// so no result reads back as unknown.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param instant the moment to express.
/// @return 100-nanosecond ticks since 0001-01-01T00:00:00 UTC.
[[nodiscard]] constexpr std::uint64_t serTicksFromUtcInstant(UtcInstant instant) noexcept
{
    constexpr std::int64_t kEarliestTick = -92233720368547758;
    std::int64_t ticks = instant.nanosecondsSinceEpoch / 100;
    if (instant.nanosecondsSinceEpoch % 100 < 0 && ticks > kEarliestTick)
    {
        --ticks;
    }
    return static_cast<std::uint64_t>(static_cast<std::int64_t>(kSerUnixEpochTicks) + ticks);
}

/// Judges a container from its header bytes and its size, without reading anything else.
///
/// The checks run in a fixed order, so a container with two problems reports the same one on every
/// build: the options, the size against the header, the colour identifier, the depth, the width, the
/// height, the extent and layout against the limits, the stated meaning, and then the frame count
/// against the payload under the count policy. Trailer presence is decided from the size alone.
///
/// Thread-safe; allocates only the header's text and a failure's detail.
///
/// @param header at least `kSerHeaderBytes` bytes; only the first `kSerHeaderBytes` are read.
/// @param fileBytes the container's whole size in bytes.
/// @param options how the container is to be read.
/// @param limits the extent and byte limits the frames must satisfy.
/// @return the container; `ErrorCode::InvalidArgument` naming a short header span or an option outside
///         its enumeration; `ErrorCode::InvalidData` with `SequenceFailure::TruncatedContainer` carrying
///         the requested and available bytes, or `SequenceFailure::MalformedContainer` naming the field;
///         `ErrorCode::Unsupported` with `SequenceFailure::UnsupportedEncoding` for a refused CMY
///         mosaic; `ErrorCode::LimitExceeded` with `SequenceFailure::DescriptorMismatch` for frames
///         beyond the limits; or `ErrorCode::IncompatibleArguments` with
///         `SequenceFailure::DescriptorMismatch` naming the stated meaning a container cannot have.
[[nodiscard]] AFTERNIGHTENGINE_API Result<SerContainer> inspectSerContainer(std::span<const std::byte> header,
                                                                            std::uint64_t fileBytes,
                                                                            const SerSourceOptions& options,
                                                                            const EngineLimits& limits);

/// Judges a container file and summarizes its trailer, without opening a session or reading a frame.
///
/// Refuses exactly what `createSerSource` refuses under the same options. A trailer is read into one
/// allocation from the engine context's memory resource and released before return.
///
/// @param engine the context whose limits apply and whose memory holds the trailer while it is read.
/// @param execution an operation of that context; its cancellation is checked between reads.
/// @param path nonempty UTF-8 without NUL; borrowed for this call.
/// @param options how the container is to be read.
/// @return the summary, or what `inspectSerContainer` refuses; `ErrorCode::InvalidArgument` naming the
///         path; `ErrorCode::ExternalFailure` with `SequenceFailure::IoFailure` carrying the path and
///         the operating system's code; `ErrorCode::InvalidData` with
///         `SequenceFailure::InvalidTimestamps` when a timeline is required and the trailer is not one;
///         a memory-budget refusal; or the execution's cancellation.
[[nodiscard]] AFTERNIGHTENGINE_API Result<SerFileInfo> inspectSerFile(const EngineContext& engine,
                                                                      const ExecutionContext& execution,
                                                                      std::string_view path,
                                                                      const SerSourceOptions& options);

/// Opens a SER container as an index-seekable source of its frames, and time-seekable when it states a
/// timeline.
///
/// The file stays open, shared for reading, writing and deletion by other handles, until the source
/// closes; frames are read with positioned reads, so independent sources over one file run
/// concurrently. Each read allocates one frame from the engine context, reads it with cancellation
/// checkpoints at most 16 MiB apart and exchanges blue and red for BGR containers. A frame carries the
/// header's observer, instrument and telescope and its own exposure start in its acquisition record,
/// the stored representation and byte order as its source encoding, and the header's identifier,
/// colour, byte-order, depth and date fields as `CodecNamespace::Ser` cards; it never carries an output
/// intent. With a timeline, presentation ticks count SER ticks from the first frame. The trailer, when
/// present, is held in one allocation from the context for the life of the source. Returned frames own
/// their samples and survive the source.
///
/// @param engine the context owning the session and every frame allocation.
/// @param execution an operation of that context, borrowed for this call.
/// @param path nonempty UTF-8 without NUL; borrowed for this call.
/// @param options how the container is read; copied.
/// @return an open source, or what `inspectSerFile` refuses. A read that meets a file shortened since
///         it was opened fails with `SequenceFailure::TruncatedContainer` or
///         `SequenceFailure::IoFailure` beneath the session's own failure, and publishes no frame.
[[nodiscard]] AFTERNIGHTENGINE_API Result<FrameSource> createSerSource(const EngineContext& engine,
                                                                       const ExecutionContext& execution,
                                                                       std::string_view path,
                                                                       const SerSourceOptions& options);

/// Opens a SER writer over an output's pending payload and writes the header.
///
/// The descriptor must name `SequenceFormat::Ser` and a fixed frame the container can store: one or two
/// unsigned bytes per sample; mono with no filter array or a two-by-two Bayer mosaic at any phase, which
/// is stored as the pattern its phase gives; or three-channel RGB; a top-left origin; square pixels.
/// Colour basis and numeric domain are not stored. A known frame count is written at once and at most
/// `INT32_MAX` frames are accepted.
///
/// Nothing is written until the request, the descriptor and the engine's limits have all been accepted,
/// so a refused request leaves the payload exactly as it was. The sink then claims the payload through
/// `SequenceOutput::claimWriter` and reopens it by name, refusing a name that no longer holds a regular
/// file with one link rather than following it. Appends write interleaved samples without row padding,
/// arranging planar RGB row by row, with cancellation checkpoints at most 16 MiB apart. Finish encodes
/// and writes timestamps in blocks of at most 64 KiB, checking cancellation before encoding, before each
/// write and before the final count. It closes the payload and completes the claim, after which the caller
/// commits the output. Until then the output refuses to commit, and an output aborted or destroyed
/// meanwhile leaves removing the payload to the sink. Closing an unfinished sink discards what it wrote
/// and returns the payload to the output unpublished.
///
/// @param engine the context the output was reserved for; owns the sink's workspace.
/// @param execution an operation of that context; its cancellation is checked before the header is
///        written.
/// @param output a valid, uncommitted output of the same context with no writer; claimed until the sink
///        finishes or closes.
/// @param descriptor the container's description; copied.
/// @param request the header's text, dates, depth and trailer source; copied.
/// @return an open sink; `ErrorCode::InvalidArgument` naming an option, a text field, the depth, the
///         origin ticks, or a descriptor that is not SER or states no fixed frame;
///         `ErrorCode::InvalidState` for an output that holds nothing or already has a writer;
///         `ErrorCode::IncompatibleArguments` for an output or execution of another context, or a
///         timestamp source the descriptor's timing cannot supply; `ErrorCode::Unsupported` with
///         `SequenceFailure::UnsupportedEncoding` naming what the container cannot store;
///         `ErrorCode::LimitExceeded` for a frame count above `INT32_MAX`, or with
///         `SequenceFailure::DescriptorMismatch` for frames beyond the engine's limits;
///         `ErrorCode::ExternalFailure` with `SequenceFailure::IoFailure` carrying the operating system's
///         code; a memory-budget refusal; or the execution's cancellation.
[[nodiscard]] AFTERNIGHTENGINE_API Result<FrameSink>
createSerSink(const EngineContext& engine, const ExecutionContext& execution, SequenceOutput& output,
              const SequenceDescriptor& descriptor, const SerSinkRequest& request);

} // namespace an
