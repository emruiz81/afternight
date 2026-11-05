// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - reading a video container through the engine's file handle, and indexing its stream.
//
// FFmpeg reads through a custom reader over `SequenceFile`, so it never opens a path itself, and both
// the reader and FFmpeg's interrupt callback refuse to continue once the bound operation is stopped or
// past its deadline. A container that asks to open another resource is refused twice over: by the
// open callback, and by an empty protocol whitelist that an inner demuxer inherits.
//
// Opening scans every packet of the chosen stream once, without decoding, so that the frame count is
// exact, an index names the picture a viewer sees at that position, and a time seek is a binary search.
// The scan builds the decode-order packet table, every packet's timestamp sorted for lookup after a
// seek, and the presentation-order frame table with the keyframe each frame decodes from; a frame with
// no such keyframe is hidden rather than delivered wrong. A palette handed over as side data is kept
// here, because the demuxer hands it over once and the scan is the only reader that sees it. Every
// table is engine-context memory, and the packet and palette tables grow by doubling.

#include "io/sequences/ffmpeg/FfmpegInput.h"

#include <AfterNightEngine/Runtime/TraceSink.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <utility>

namespace an::ffmpeg_detail
{
namespace
{

/// How many bytes FFmpeg's reader buffers between calls into the file boundary. Packets larger than
/// this are read straight into the packet, so the buffer only carries headers and small packets.
constexpr int kReaderBufferBytes = 256 * 1024;

/// How many packet records an index starts with when the container states no count.
constexpr std::uint64_t kInitialPackets = 64u;

/// The most packets a source indexes: ordinals are 32-bit.
constexpr std::uint64_t kMaxPackets = 0xFFFFFFFEu;

int interruptRequested(void* opaque) noexcept
{
    auto* input = static_cast<InputFile*>(opaque);
    const ExecutionContext* execution = input->bound();
    if (execution != nullptr && (execution->stopRequested() || execution->deadlineExceeded()))
    {
        input->markInterrupted();
        return 1;
    }
    return 0;
}

int refuseForeignOpen(AVFormatContext*, AVIOContext**, const char*, int, AVDictionary**) noexcept
{
    return errorNotPermitted();
}

Result<FormatContextPtr> openFormat(InputFile& input, const ExecutionContext& execution, const AVInputFormat* demuxer)
{
    AVFormatContext* raw = avformat_alloc_context();
    if (raw == nullptr)
        return Status::failure(ErrorCode::AllocationFailed);
    raw->pb = input.reader();
    raw->flags |= AVFMT_FLAG_CUSTOM_IO;
    raw->io_open = &refuseForeignOpen;
    raw->interrupt_callback.callback = &interruptRequested;
    raw->interrupt_callback.opaque = &input;
    // A demuxer that reads another resource - a concatenation list's clips, a playlist's segments - opens
    // it through FFmpeg's protocols in an inner context that inherits this one's protocol whitelist. The
    // container itself needs no protocol, because it is read through the reader above, so an empty
    // whitelist refuses every one of them.
    raw->protocol_whitelist = av_strdup("");
    raw->format_whitelist = av_strdup("avi,mov,matroska,webm,mpeg,mpegts,nut");
    raw->codec_whitelist = av_strdup(kVideoDecoders);
    if (raw->protocol_whitelist == nullptr || raw->format_whitelist == nullptr || raw->codec_whitelist == nullptr)
    {
        avformat_free_context(raw);
        return Status::failure(ErrorCode::AllocationFailed);
    }
    // On failure FFmpeg frees the context itself and leaves the caller's reader alone.
    if (const int opened = avformat_open_input(&raw, input.path().c_str(), demuxer, nullptr); opened < 0)
        return inputFailure(input, execution, opened, "the container could not be opened");
    return FormatContextPtr(raw);
}

bool isVideo(const AVStream& stream) noexcept
{
    return stream.codecpar->codec_type == AVMEDIA_TYPE_VIDEO && (stream.disposition & AV_DISPOSITION_ATTACHED_PIC) == 0;
}

void discardOtherStreams(AVFormatContext& format, int chosen) noexcept
{
    for (unsigned index = 0u; index < format.nb_streams; ++index)
        format.streams[index]->discard = static_cast<int>(index) == chosen ? AVDISCARD_DEFAULT : AVDISCARD_ALL;
}

bool codecReorders(AVCodecID codec) noexcept
{
    const AVCodecDescriptor* descriptor = avcodec_descriptor_get(codec);
    return descriptor != nullptr && (descriptor->props & AV_CODEC_PROP_REORDER) != 0;
}

template <typename RecordT> Result<AccountedBlock> allocateRecords(const EngineContext& engine, std::uint64_t count)
{
    auto block = AccountedBlock::allocate(engine, count * sizeof(RecordT), alignof(RecordT));
    if (!block.hasValue())
        return block.status();
    if (count != 0u)
        std::uninitialized_default_construct_n(static_cast<RecordT*>(block->data()), static_cast<std::size_t>(count));
    return block;
}

/// Keeps a palette handed over as side data. A demuxer hands a stored palette over once, with the first
/// packet read after it changes, so the scan that indexes the stream is the only reader that sees it.
Status keepPalette(const EngineContext& engine, StreamIndex& index, std::uint32_t packet, const std::uint8_t* palette)
{
    const std::uint64_t capacity = index.palettes.size() / sizeof(PaletteRecord);
    if (index.paletteCount == capacity)
    {
        auto larger = allocateRecords<PaletteRecord>(engine, capacity == 0u ? 4u : capacity * 2u);
        if (!larger.hasValue())
            return larger.status();
        if (index.paletteCount != 0u)
            std::memcpy(larger->data(), index.palettes.data(),
                        static_cast<std::size_t>(index.paletteCount * sizeof(PaletteRecord)));
        index.palettes = std::move(*larger);
    }
    PaletteRecord& record = static_cast<PaletteRecord*>(index.palettes.data())[index.paletteCount++];
    record.packet = packet;
    record.reserved = 0u;
    std::memcpy(record.entries.data(), palette, record.entries.size());
    return Status::success();
}

Status malformed(const char* message, const char* parameter, std::string_view path)
{
    return videoFailure(ErrorCode::InvalidData, SequenceFailure::MalformedContainer, message, parameter, path);
}

Result<StreamIndex> buildIndex(const EngineContext& engine, const ExecutionContext& execution, AVFormatContext& format,
                               int stream, InputFile& input)
{
    const std::string& path = input.path();
    const AVStream& chosen = *format.streams[stream];
    StreamIndex index;
    std::uint64_t capacity = kInitialPackets;
    if (chosen.nb_frames > 0 && static_cast<std::uint64_t>(chosen.nb_frames) < (1u << 20u))
        capacity = (std::max)(capacity, static_cast<std::uint64_t>(chosen.nb_frames) + 1u);
    auto packets = allocateRecords<PacketEntry>(engine, capacity);
    if (!packets.hasValue())
        return packets.status();
    index.packets = std::move(*packets);

    const PacketPtr packet(av_packet_alloc());
    if (!packet)
        return Status::failure(ErrorCode::AllocationFailed);
    std::uint64_t count = 0u;
    for (;;)
    {
        if (const Status stopped = execution.checkCancellation(); stopped.failed())
            return stopped;
        const int read = av_read_frame(&format, packet.get());
        if (read == errorEndOfFile())
            break;
        if (read < 0)
            return inputFailure(input, execution, read, "a packet could not be read while indexing");
        if (packet->stream_index != stream)
        {
            av_packet_unref(packet.get());
            continue;
        }
        if (count == kMaxPackets)
            return videoFailure(ErrorCode::LimitExceeded, SequenceFailure::DescriptorMismatch,
                                "the stream holds more packets than a source indexes", "frameCount", path);
        if (count == capacity)
        {
            auto larger = allocateRecords<PacketEntry>(engine, capacity * 2u);
            if (!larger.hasValue())
                return larger.status();
            std::memcpy(larger->data(), index.packets.data(), static_cast<std::size_t>(count * sizeof(PacketEntry)));
            index.packets = std::move(*larger);
            capacity *= 2u;
        }
        std::uint32_t flags = 0u;
        if ((packet->flags & AV_PKT_FLAG_KEY) != 0)
            flags |= kPacketKey;
        if ((packet->flags & AV_PKT_FLAG_DISCARD) != 0)
            flags |= kPacketHidden;
        static_cast<PacketEntry*>(index.packets.data())[count] =
            PacketEntry{packet->pts, packet->dts, packet->duration > 0 ? packet->duration : 0, flags, kNoPacket};
        std::size_t paletteBytes = 0u;
        if (const std::uint8_t* palette = av_packet_get_side_data(packet.get(), AV_PKT_DATA_PALETTE, &paletteBytes);
            palette != nullptr && paletteBytes >= AVPALETTE_SIZE)
            if (const Status kept = keepPalette(engine, index, static_cast<std::uint32_t>(count), palette);
                kept.failed())
                return kept;
        ++count;
        av_packet_unref(packet.get());
        execution.reportTrace(TraceEventKind::Counter, "ffmpeg", "packets-indexed", 1.0, "packets");
    }
    index.packetCount = count;
    const std::span<PacketEntry> entries = index.packetEntries();

    // Every packet states when it is shown, or none does and the stream shows packets as it decodes them.
    const bool allPresentation =
        std::all_of(entries.begin(), entries.end(), [](const PacketEntry& e) { return e.pts != kNoTimestamp; });
    if (!allPresentation)
    {
        const bool noPresentation =
            std::all_of(entries.begin(), entries.end(), [](const PacketEntry& e) { return e.pts == kNoTimestamp; });
        const bool allDecode =
            std::all_of(entries.begin(), entries.end(), [](const PacketEntry& e) { return e.dts != kNoTimestamp; });
        if (!noPresentation || !allDecode || codecReorders(chosen.codecpar->codec_id))
            return videoFailure(ErrorCode::InvalidData, SequenceFailure::InvalidTimestamps,
                                "the stream's packets cannot be put in presentation order", "presentationTimestamp",
                                path);
        for (PacketEntry& entry : entries)
            entry.pts = entry.dts;
        index.timestampsFromDecodeOrder = true;
    }

    // Every packet is recognisable by its timestamp after a seek.
    auto keys = allocateRecords<PacketKey>(engine, count);
    if (!keys.hasValue())
        return keys.status();
    index.keys = std::move(*keys);
    const std::span<PacketKey> packetKeys = index.packetKeys();
    for (std::uint64_t ordinal = 0u; ordinal < count; ++ordinal)
        packetKeys[ordinal] = PacketKey{entries[ordinal].pts, static_cast<std::uint32_t>(ordinal), 0u};
    std::sort(packetKeys.begin(), packetKeys.end(),
              [](const PacketKey& left, const PacketKey& right) { return left.pts < right.pts; });
    for (std::size_t position = 1u; position < packetKeys.size(); ++position)
        if (packetKeys[position].pts == packetKeys[position - 1u].pts)
            return malformed("two packets of the stream state the same presentation time", "presentationTimestamp",
                             path);

    // The last keyframe at or before each packet; nothing before the first keyframe can be decoded.
    std::uint32_t lastKey = kNoPacket;
    for (std::uint64_t ordinal = 0u; ordinal < count; ++ordinal)
    {
        if ((entries[ordinal].flags & kPacketKey) != 0u)
        {
            lastKey = static_cast<std::uint32_t>(ordinal);
            ++index.keyframeCount;
        }
        entries[ordinal].lastKey = lastKey;
    }

    std::uint64_t visible = 0u;
    for (const PacketEntry& entry : entries)
        if ((entry.flags & kPacketHidden) == 0u && entry.lastKey != kNoPacket)
            ++visible;
    auto frames = allocateRecords<FrameEntry>(engine, visible);
    if (!frames.hasValue())
        return frames.status();
    index.frames = std::move(*frames);
    std::span<FrameEntry> frameEntries(static_cast<FrameEntry*>(index.frames.data()),
                                       static_cast<std::size_t>(visible));
    std::size_t next = 0u;
    for (std::uint64_t ordinal = 0u; ordinal < count; ++ordinal)
        if ((entries[ordinal].flags & kPacketHidden) == 0u && entries[ordinal].lastKey != kNoPacket)
            frameEntries[next++] =
                FrameEntry{entries[ordinal].pts, entries[ordinal].duration, static_cast<std::uint32_t>(ordinal), 0u};
    std::sort(frameEntries.begin(), frameEntries.end(),
              [](const FrameEntry& left, const FrameEntry& right) { return left.pts < right.pts; });

    // A frame decodes from the last keyframe before it in decode order that is not shown after it; a
    // frame with no such keyframe - a leading frame whose references precede the stream - is never
    // delivered.
    std::size_t kept = 0u;
    for (const FrameEntry& frame : frameEntries)
    {
        std::uint32_t start = entries[frame.packet].lastKey;
        while (start != kNoPacket && entries[start].pts > frame.pts)
            start = start == 0u ? kNoPacket : entries[start - 1u].lastKey;
        if (start == kNoPacket)
        {
            entries[frame.packet].flags |= kPacketHidden;
            continue;
        }
        frameEntries[kept] = frame;
        frameEntries[kept].start = start;
        ++kept;
    }
    index.frameCount = kept;
    for (const PacketEntry& entry : entries)
        if ((entry.flags & kPacketHidden) != 0u || entry.lastKey == kNoPacket)
            ++index.hiddenCount;

    const std::span<const FrameEntry> delivered = index.frameEntries();
    for (std::size_t position = 1u; position < delivered.size(); ++position)
        if (delivered[position].packet < delivered[position - 1u].packet)
            index.reordered = true;
    if (!delivered.empty())
    {
        const std::int64_t first = delivered.front().pts;
        const std::int64_t last = delivered.back().pts;
        if (first < 0 && last > (std::numeric_limits<std::int64_t>::max)() + first)
            return malformed("the stream's presentation times span more ticks than a timeline holds",
                             "presentationTimestamp", path);
        const bool allDurations =
            std::all_of(delivered.begin(), delivered.end(), [](const FrameEntry& frame) { return frame.duration > 0; });
        if (allDurations)
        {
            std::int64_t end = 0;
            bool representable = true;
            for (const FrameEntry& frame : delivered)
            {
                const std::int64_t ticks = frame.pts - first;
                if (ticks > (std::numeric_limits<std::int64_t>::max)() - frame.duration)
                {
                    representable = false;
                    break;
                }
                end = (std::max)(end, ticks + frame.duration);
            }
            if (representable)
                index.timelineEnd = end;
        }
    }
    return index;
}

} // namespace

InputFile::~InputFile()
{
    static_cast<void>(close());
}

Result<std::unique_ptr<InputFile>> InputFile::open(std::string_view path)
{
    std::unique_ptr<InputFile> input(new InputFile());
    input->m_path = std::string(path);
    const std::filesystem::path native(std::u8string_view(reinterpret_cast<const char8_t*>(path.data()), path.size()));
    if (const int error = input->m_file.openForReading(native); error != 0)
        return ioFailure(error, "the video file could not be opened", path);
    auto* buffer = static_cast<unsigned char*>(av_malloc(kReaderBufferBytes));
    if (buffer == nullptr)
        return Status::failure(ErrorCode::AllocationFailed);
    input->m_reader =
        avio_alloc_context(buffer, kReaderBufferBytes, 0, input.get(), &InputFile::read, nullptr, &InputFile::seek);
    if (input->m_reader == nullptr)
    {
        av_free(buffer);
        return Status::failure(ErrorCode::AllocationFailed);
    }
    return input;
}

int InputFile::read(void* opaque, std::uint8_t* buffer, int size) noexcept
{
    auto* self = static_cast<InputFile*>(opaque);
    if (self->m_execution != nullptr && (self->m_execution->stopRequested() || self->m_execution->deadlineExceeded()))
    {
        self->m_interrupted = true;
        return errorExit();
    }
    if (size <= 0)
        return 0;
    std::size_t received = 0u;
    if (const int error = self->m_file.readAt(reinterpret_cast<std::byte*>(buffer), static_cast<std::size_t>(size),
                                              self->m_position, received);
        error != 0)
    {
        self->m_platformError = error;
        return errorInput();
    }
    if (received == 0u)
        return errorEndOfFile();
    self->m_position += received;
    if (self->m_execution != nullptr)
        self->m_execution->reportTrace(TraceEventKind::Counter, "ffmpeg", "input-bytes", static_cast<double>(received),
                                       "bytes");
    return static_cast<int>(received);
}

std::int64_t InputFile::seek(void* opaque, std::int64_t offset, int whence) noexcept
{
    auto* self = static_cast<InputFile*>(opaque);
    const int mode = whence & ~AVSEEK_FORCE;
    std::uint64_t size = 0u;
    if (mode == AVSEEK_SIZE || mode == SEEK_END)
    {
        if (const int error = self->m_file.size(size); error != 0)
        {
            self->m_platformError = error;
            return errorInput();
        }
        if (mode == AVSEEK_SIZE)
            return static_cast<std::int64_t>(size);
    }
    std::int64_t base = 0;
    if (mode == SEEK_CUR)
        base = static_cast<std::int64_t>(self->m_position);
    else if (mode == SEEK_END)
        base = static_cast<std::int64_t>(size);
    else if (mode != SEEK_SET)
        return errorInvalidArgument();
    if ((offset < 0 && -offset > base) || (offset > 0 && offset > (std::numeric_limits<std::int64_t>::max)() - base))
        return errorInvalidArgument();
    self->m_position = static_cast<std::uint64_t>(base + offset);
    return base + offset;
}

int InputFile::rewind() noexcept
{
    const std::int64_t moved = avio_seek(m_reader, 0, SEEK_SET);
    return moved < 0 ? static_cast<int>(moved) : 0;
}

int InputFile::close() noexcept
{
    if (m_reader != nullptr)
    {
        av_freep(&m_reader->buffer);
        avio_context_free(&m_reader);
    }
    return m_file.close();
}

std::span<PacketEntry> StreamIndex::packetEntries() const noexcept
{
    return {static_cast<PacketEntry*>(packets.data()), static_cast<std::size_t>(packetCount)};
}

std::span<FrameEntry> StreamIndex::frameEntries() const noexcept
{
    return {static_cast<FrameEntry*>(frames.data()), static_cast<std::size_t>(frameCount)};
}

std::span<PacketKey> StreamIndex::packetKeys() const noexcept
{
    return {static_cast<PacketKey*>(keys.data()), static_cast<std::size_t>(packetCount)};
}

std::optional<std::uint64_t> StreamIndex::frameAt(std::int64_t pts) const noexcept
{
    const std::span<FrameEntry> entries = frameEntries();
    const auto found = std::lower_bound(entries.begin(), entries.end(), pts,
                                        [](const FrameEntry& frame, std::int64_t value) { return frame.pts < value; });
    if (found == entries.end() || found->pts != pts)
        return std::nullopt;
    return static_cast<std::uint64_t>(found - entries.begin());
}

std::optional<std::uint32_t> StreamIndex::packetAt(std::int64_t pts) const noexcept
{
    const std::span<PacketKey> entries = packetKeys();
    const auto found = std::lower_bound(entries.begin(), entries.end(), pts,
                                        [](const PacketKey& key, std::int64_t value) { return key.pts < value; });
    if (found == entries.end() || found->pts != pts)
        return std::nullopt;
    return found->packet;
}

std::int64_t StreamIndex::ticksOf(std::uint64_t frame) const noexcept
{
    const std::span<FrameEntry> entries = frameEntries();
    return entries[static_cast<std::size_t>(frame)].pts - entries.front().pts;
}

const PaletteRecord* StreamIndex::paletteAt(std::uint32_t packet) const noexcept
{
    const auto* records = static_cast<const PaletteRecord*>(palettes.data());
    const PaletteRecord* found = nullptr;
    for (std::uint64_t record = 0u; record < paletteCount && records[record].packet <= packet; ++record)
        found = &records[record];
    return found;
}

Status inputFailure(const InputFile& input, const ExecutionContext& execution, int code, const char* message)
{
    if (input.interrupted() || code == errorExit())
    {
        if (const Status stopped = execution.checkCancellation(); stopped.failed())
            return stopped;
        return Status::failure(ErrorCode::Cancelled);
    }
    if (input.platformError() != 0)
        return ioFailure(input.platformError(), message, input.path());
    return libraryFailure(code, message, input.path());
}

Result<OpenedContainer> openContainer(const EngineContext& engine, const ExecutionContext& execution,
                                      std::string_view path, const FfmpegSourceOptions& options)
{
    auto input = InputFile::open(path);
    if (!input.hasValue())
        return input.status();
    InputFile& file = **input;
    const BoundOperation bound(file, execution);
    auto format = openFormat(file, execution, nullptr);
    if (!format.hasValue())
        return format.status();
    AVFormatContext& context = **format;
    if (const int found = avformat_find_stream_info(&context, nullptr); found < 0)
        return inputFailure(file, execution, found, "the container's streams could not be read");
    if (const Status stopped = execution.checkCancellation(); stopped.failed())
        return stopped;

    int chosen = -1;
    if (options.streamIndex)
    {
        if (*options.streamIndex >= context.nb_streams || !isVideo(*context.streams[*options.streamIndex]))
            return videoFailure(ErrorCode::IncompatibleArguments, SequenceFailure::DescriptorMismatch,
                                "the container holds no video stream with that index", "streamIndex", path);
        chosen = static_cast<int>(*options.streamIndex);
    }
    else
    {
        for (unsigned candidate = 0u; candidate < context.nb_streams && chosen < 0; ++candidate)
            if (isVideo(*context.streams[candidate]))
                chosen = static_cast<int>(candidate);
        if (chosen < 0)
            return videoFailure(ErrorCode::Unsupported, SequenceFailure::UnsupportedEncoding,
                                "the container holds no video stream", "streamIndex", path);
    }
    discardOtherStreams(context, chosen);
    auto index = buildIndex(engine, execution, context, chosen, file);
    if (!index.hasValue())
        return index.status();

    OpenedContainer opened;
    opened.input = std::move(*input);
    opened.format = std::move(*format);
    opened.stream = chosen;
    opened.index = std::move(*index);
    return opened;
}

Status reopenContainer(OpenedContainer& container, const ExecutionContext& execution)
{
    const AVInputFormat* demuxer = container.format ? container.format->iformat : nullptr;
    container.format.reset();
    if (const int rewound = container.input->rewind(); rewound < 0)
        return inputFailure(*container.input, execution, rewound, "the container could not be read from its start");
    auto format = openFormat(*container.input, execution, demuxer);
    if (!format.hasValue())
        return format.status();
    if (container.stream < 0 || static_cast<unsigned>(container.stream) >= (*format)->nb_streams)
        return malformed("the container's streams changed while it was read", "streamIndex", container.input->path());
    discardOtherStreams(**format, container.stream);
    container.format = std::move(*format);
    return Status::success();
}

Result<OpenedContainer> openAgain(const OpenedContainer& container, const ExecutionContext& execution)
{
    auto input = InputFile::open(container.input->path());
    if (!input.hasValue())
        return input.status();
    const BoundOperation bound(**input, execution);
    auto format = openFormat(**input, execution, container.format->iformat);
    if (!format.hasValue())
        return format.status();
    if (container.stream < 0 || static_cast<unsigned>(container.stream) >= (*format)->nb_streams)
        return malformed("the container's streams changed while it was read", "streamIndex", container.input->path());
    discardOtherStreams(**format, container.stream);
    OpenedContainer again;
    again.input = std::move(*input);
    again.format = std::move(*format);
    again.stream = container.stream;
    return again;
}

} // namespace an::ffmpeg_detail
