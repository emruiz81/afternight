// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the FFmpeg container boundary: a demuxer reading through the engine's own file
// handle, the stream a source reads, and that stream's presentation index.
//
// FFmpeg never opens a file here. The container is read through `SequenceFile`, which refuses anything
// but a regular file without waiting, and a demuxer that asks to open a second resource - a playlist,
// a concatenation list, an external index - is refused. The reads FFmpeg makes observe the bound
// operation's cancellation, so opening, probing and indexing a large or slow file can be stopped.

#pragma once

#include <AfterNightEngine/IO/FfmpegSequence.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "io/sequences/AccountedBlock.h"
#include "io/sequences/SequenceFile.h"
#include "io/sequences/ffmpeg/FfmpegLibrary.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace an::ffmpeg_detail
{

/// Decoders admitted for file-backed astronomical and ordinary camera video. Audio, subtitles and
/// unrelated formats - games, obsolete codecs - are never decoded, including during stream discovery.
inline constexpr const char* kVideoDecoders =
    "rawvideo,ffv1,huffyuv,ffvhuff,utvideo,mjpeg,mjpegb,ljpeg,h264,hevc,mpeg4,mpeg1video,mpeg2video,"
    "vp8,vp9,av1,libdav1d,prores,dnxhd,dvvideo,cinepak,msrle,msvideo1,qtrle,v210,r210";

/// One allocation from an engine context's memory resource, shared with the other sequence adapters.
using AccountedBlock = sequence_detail::AccountedBlock;

/// The engine's file seen through FFmpeg's reader interface. Owns the handle, the reader and its
/// buffer. Heap-allocated so the reader's pointer back to it stays valid; not thread-safe, like the
/// session it belongs to.
class InputFile
{
public:
    /// Releases the reader and closes the file.
    ~InputFile();

    InputFile(const InputFile&) = delete;
    InputFile& operator=(const InputFile&) = delete;

    /// Opens a regular file for reading through FFmpeg.
    ///
    /// A file the platform refuses to open, or that is not a regular file, is `ErrorCode::ExternalFailure`
    /// with `SequenceFailure::IoFailure`; the reader's buffer failing to allocate is
    /// `ErrorCode::AllocationFailed`.
    [[nodiscard]] static Result<std::unique_ptr<InputFile>> open(std::string_view path);

    /// The reader a demuxer is given.
    [[nodiscard]] AVIOContext* reader() const noexcept
    {
        return m_reader;
    }

    /// The operation whose cancellation reads observe and whose trace receives their byte counts.
    void bind(const ExecutionContext* execution) noexcept
    {
        m_execution = execution;
        m_interrupted = false;
        m_platformError = 0;
    }

    /// The bound operation, or null.
    [[nodiscard]] const ExecutionContext* bound() const noexcept
    {
        return m_execution;
    }

    /// Whether a read or an interrupt check refused because the bound operation was stopped.
    [[nodiscard]] bool interrupted() const noexcept
    {
        return m_interrupted;
    }

    /// Records that FFmpeg's interrupt check saw a stop.
    void markInterrupted() noexcept
    {
        m_interrupted = true;
    }

    /// The operating system's error from the last failed read, or zero.
    [[nodiscard]] int platformError() const noexcept
    {
        return m_platformError;
    }

    /// Moves the reader back to the first byte, discarding what it buffered.
    ///
    /// @return zero, or FFmpeg's negative code for the failed seek.
    [[nodiscard]] int rewind() noexcept;

    /// Releases the reader and closes the file; a second call reports success.
    ///
    /// @return zero, or the operating system's error from closing the file.
    [[nodiscard]] int close() noexcept;

    /// The path the file was opened with.
    [[nodiscard]] const std::string& path() const noexcept
    {
        return m_path;
    }

private:
    InputFile() = default;
    static int read(void* opaque, std::uint8_t* buffer, int size) noexcept;
    static std::int64_t seek(void* opaque, std::int64_t offset, int whence) noexcept;

    SequenceFile m_file;
    AVIOContext* m_reader = nullptr;
    const ExecutionContext* m_execution = nullptr;
    std::uint64_t m_position = 0u;
    int m_platformError = 0;
    bool m_interrupted = false;
    std::string m_path;
};

/// Binds an operation to an input for the life of a scope.
class BoundOperation
{
public:
    /// Binds `execution` to `input` until this object is destroyed; both must outlive it.
    BoundOperation(InputFile& input, const ExecutionContext& execution) noexcept : m_input(input)
    {
        m_input.bind(&execution);
    }
    ~BoundOperation()
    {
        m_input.bind(nullptr);
    }
    BoundOperation(const BoundOperation&) = delete;
    BoundOperation& operator=(const BoundOperation&) = delete;

private:
    InputFile& m_input;
};

/// A packet is a keyframe.
inline constexpr std::uint32_t kPacketKey = 1u;

/// A packet is decoded when it is needed and never delivered.
inline constexpr std::uint32_t kPacketHidden = 2u;

/// No keyframe precedes a packet.
inline constexpr std::uint32_t kNoPacket = 0xFFFFFFFFu;

/// One packet of the stream, in decode order.
struct PacketEntry
{
    std::int64_t pts; ///< Presentation timestamp, or the decode timestamp for a stream that states none.
    std::int64_t dts; ///< Decode timestamp as the demuxer reported it while indexing, or `kNoTimestamp`.
    std::int64_t duration; ///< Positive duration, or zero when unstated.
    std::uint32_t flags; ///< `kPacketKey` and `kPacketHidden`.
    std::uint32_t lastKey; ///< The last keyframe at or before this packet in decode order, or `kNoPacket`.
};

/// One delivered frame, in presentation order.
struct FrameEntry
{
    std::int64_t pts; ///< Presentation timestamp.
    std::int64_t duration; ///< Positive duration, or zero when unstated.
    std::uint32_t packet; ///< The packet it is decoded from.
    std::uint32_t start; ///< The keyframe decoding must start from to produce it.
};

/// A palette a container hands over beside a packet rather than inside it, and the packet it came with.
struct PaletteRecord
{
    std::uint32_t packet; ///< The decode ordinal of the packet the palette arrived with.
    std::uint32_t reserved; ///< Always zero; the record's padding, stated.
    std::array<std::uint8_t, AVPALETTE_SIZE> entries; ///< The 256 entries as FFmpeg hands them over.
};

/// A packet's timestamp and its place in decode order, sorted by timestamp.
struct PacketKey
{
    std::int64_t pts; ///< The packet's presentation timestamp, as `PacketEntry::pts` states it.
    std::uint32_t packet; ///< The packet's decode ordinal.
    std::uint32_t reserved; ///< Always zero; the record's padding, stated.
};

/// The presentation index of one stream. Every block is memory from the engine context, and a block
/// may hold more records than its count says are in use.
struct StreamIndex
{
    AccountedBlock packets; ///< `PacketEntry` records of the stream's packets, in decode order.
    AccountedBlock frames; ///< `FrameEntry` records of the delivered frames, in presentation order.
    AccountedBlock keys; ///< `PacketKey` records of every packet, sorted by presentation timestamp.
    AccountedBlock palettes; ///< `PaletteRecord` records, in the order the palettes arrived.
    std::uint64_t paletteCount = 0u; ///< Palettes handed over as side data.
    std::uint64_t packetCount = 0u; ///< Packets of the chosen stream.
    std::uint64_t frameCount = 0u; ///< Frames a source delivers.
    std::uint64_t keyframeCount = 0u; ///< Packets that are keyframes.
    std::uint64_t hiddenCount = 0u; ///< Packets never delivered: marked hidden, or with no keyframe to start from.
    bool reordered = false; ///< Presentation order differs from decode order among the delivered frames.
    bool timestampsFromDecodeOrder = false; ///< The stream states no presentation times; its decode times stand in.
    /// The end of the last frame in ticks from the first frame, when every delivered frame states a
    /// duration and the end is representable; otherwise empty.
    std::optional<std::int64_t> timelineEnd;

    /// The packet records in use, in decode order.
    [[nodiscard]] std::span<PacketEntry> packetEntries() const noexcept;

    /// The delivered frames, in presentation order.
    [[nodiscard]] std::span<FrameEntry> frameEntries() const noexcept;

    /// Every packet's timestamp and ordinal, sorted by timestamp.
    [[nodiscard]] std::span<PacketKey> packetKeys() const noexcept;

    /// The presentation index of the frame with this timestamp.
    [[nodiscard]] std::optional<std::uint64_t> frameAt(std::int64_t pts) const noexcept;

    /// The decode ordinal of the packet with this timestamp.
    [[nodiscard]] std::optional<std::uint32_t> packetAt(std::int64_t pts) const noexcept;

    /// A frame's presentation ticks from the first frame. `frame` must be below `frameCount`.
    [[nodiscard]] std::int64_t ticksOf(std::uint64_t frame) const noexcept;

    /// The side-data palette in effect for a packet: the last one handed over at or before it, or null.
    [[nodiscard]] const PaletteRecord* paletteAt(std::uint32_t packet) const noexcept;
};

/// An opened container with its stream chosen and indexed. The demuxer is declared after the input so
/// it is released first.
struct OpenedContainer
{
    std::unique_ptr<InputFile> input; ///< The file the demuxer reads through.
    FormatContextPtr format; ///< The demuxer, with every stream but the chosen one discarded.
    int stream = -1; ///< The chosen stream's index in the container.
    StreamIndex index; ///< The chosen stream's index; empty for a container opened by `openAgain`.
};

/// Opens a container through the engine's file handle, chooses the stream and indexes it.
///
/// Every packet of the chosen stream is read once, without decoding, to build the index. Beside the
/// refusals of `InputFile::open` and `inputFailure`, a requested stream that is not video is
/// `ErrorCode::IncompatibleArguments` with `SequenceFailure::DescriptorMismatch`, and a container with
/// no video stream is `ErrorCode::Unsupported`. More packets than a 32-bit ordinal counts is
/// `ErrorCode::LimitExceeded`. Packets that cannot be put in presentation order are
/// `ErrorCode::InvalidData` with `SequenceFailure::InvalidTimestamps`, and two packets with one
/// presentation time, or times spanning more ticks than a timeline holds, are `ErrorCode::InvalidData`
/// with `SequenceFailure::MalformedContainer`. The engine context's refusal of the index's memory is
/// returned as it stands.
[[nodiscard]] Result<OpenedContainer> openContainer(const EngineContext& engine, const ExecutionContext& execution,
                                                    std::string_view path, const FfmpegSourceOptions& options);

/// Opens the container again from its first byte with the demuxer it was read with and without probing
/// its streams: the fallback when a seek cannot land at or before a keyframe.
[[nodiscard]] Status reopenContainer(OpenedContainer& container, const ExecutionContext& execution);

/// Opens a second reader of an opened container's file with the demuxer it was read with, at the first byte and
/// without probing or indexing, so a trial decode leaves the container's own reader where it is.
[[nodiscard]] Result<OpenedContainer> openAgain(const OpenedContainer& container, const ExecutionContext& execution);

/// What a failure FFmpeg reported during a bound operation means, preferring the operation's own
/// cancellation and the operating system's error to FFmpeg's code: a stopped operation answers with its
/// own cancellation (or `ErrorCode::Cancelled`), a failed read with `ioFailure`, and anything else with
/// `libraryFailure`.
[[nodiscard]] Status inputFailure(const InputFile& input, const ExecutionContext& execution, int code,
                                  const char* message);

} // namespace an::ffmpeg_detail
