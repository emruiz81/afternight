// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the SER sink: frames appended after a fixed header, with an optional trailer of timestamps.
//
// Everything that can refuse is decided before the pending payload is opened, and the header is then
// written with the descriptor's frame count, or zero when it states none; samples keep the frame's own
// byte order, which the header's flag states under the requested convention. Frames follow as
// interleaved rows: a frame already stored that way is written from its own bytes, and padded rows or
// planar colour are packed through a scratch block. Each frame's timestamp - its presentation ticks
// from a stated origin, or its exposure start, zero being SER's unknown - is kept in engine-context
// memory. `finish` writes the trailer, rewrites the header's frame count, closes the file and only then
// completes the output's claim; an unfinished container is truncated to nothing.

#include <AfterNightEngine/IO/SerSequence.h>

#include <AfterNightEngine/IO/SequenceOutput.h>
#include <AfterNightEngine/Runtime/TraceSink.h>

#include "core/Utf8.h"
#include "io/sequences/SequenceInternal.h"
#include "io/sequences/SerFormat.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <utility>

namespace an
{
namespace
{
constexpr std::uint64_t kMaxStoredFrames = static_cast<std::uint64_t>((std::numeric_limits<std::int32_t>::max)());
constexpr std::uint64_t kMaxStoredTicks = static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)());
constexpr std::uint64_t kInitialTicks = 64u;

Status unsupported(const char* message, const char* parameter)
{
    return ser_detail::serFailure(ErrorCode::Unsupported, SequenceFailure::UnsupportedEncoding, message, parameter, {});
}

Status descriptorArgument(const char* message, const char* parameter)
{
    return ser_detail::serFailure(ErrorCode::InvalidArgument, SequenceFailure::InvalidDescriptor, message, parameter,
                                  {});
}

Status checkText(std::string_view text, std::size_t bound, const char* parameter)
{
    if (text.size() > bound || !isValidUtf8WithoutNul(text))
        return ser_detail::argumentFailure("a SER text field is bounded UTF-8 without NUL", parameter);
    return Status::success();
}

void putText(ser_detail::HeaderBytes& header, std::size_t offset, std::string_view text) noexcept
{
    if (!text.empty())
        std::memcpy(header.data() + offset, text.data(), text.size());
}

/// Everything a sink decides once, before it writes a byte.
struct SinkPlan
{
    ser_detail::HeaderBytes header{};
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::uint32_t channels = 0u;
    std::uint32_t sampleBytes = 0u;
    std::uint64_t initialTicks = 0u;
};

Result<SinkPlan> plan(const SequenceDescriptor& descriptor, const SerSinkRequest& request)
{
    if (static_cast<std::uint8_t>(request.endianConvention) >
        static_cast<std::uint8_t>(SerEndianConvention::Specification))
        return ser_detail::argumentFailure("the byte-order convention is not one this build knows", "endianConvention");
    if (static_cast<std::uint8_t>(request.timestamps) > static_cast<std::uint8_t>(SerTimestampSource::ExposureStart))
        return ser_detail::argumentFailure("the timestamp source is not one this build knows", "timestamps");
    if (descriptor.format != SequenceFormat::Ser)
        return descriptorArgument("a SER sink writes a SER container", "format");
    if (!descriptor.fixedFrame)
        return descriptorArgument("a SER container stores one frame description for every frame", "fixedFrame");
    if (descriptor.seekByIndex)
        return descriptorArgument("a sink cannot seek", "seekByIndex");
    if (descriptor.seekByTime)
        return descriptorArgument("a sink cannot seek", "seekByTime");
    if (const Status checked = validateSequenceDescriptor(descriptor); checked.failed())
        return checked;
    if (descriptor.frameCount && *descriptor.frameCount > kMaxStoredFrames)
        return ser_detail::serFailure(ErrorCode::LimitExceeded, SequenceFailure::UnsupportedEncoding,
                                      "a SER container counts at most INT32_MAX frames", "frameCount", {});
    const RawFrameDescriptor& frame = *descriptor.fixedFrame;
    const RawSampleFormat format = frame.sampleFormat();
    if (format.sampleClass != RawSampleClass::UnsignedInteger || (format.sizeBytes != 1u && format.sizeBytes != 2u))
        return unsupported("SER stores one or two unsigned bytes per sample", "sampleFormat");
    if (frame.channelSchema() != ChannelSchema::Mono && frame.channelSchema() != ChannelSchema::Rgb)
        return unsupported("SER stores mono, mosaic and RGB frames", "channelSchema");
    const std::optional<std::int32_t> colorId = ser_detail::colorIdOf(frame);
    if (!colorId)
        return unsupported("SER stores no filter array but the two-by-two Bayer mosaics", "cfa");
    if (frame.coordinateOrigin() != CoordinateOrigin::TopLeft)
        return unsupported("SER stores rows from the top", "coordinateOrigin");
    if (frame.pixelAspectRatio() != 1.0)
        return unsupported("SER stores square pixels", "pixelAspectRatio");
    if (frame.extent().width() > kMaxStoredFrames || frame.extent().height() > kMaxStoredFrames)
        return unsupported("SER stores signed 32-bit dimensions", "extent");
    std::uint32_t depth = request.pixelDepthPerPlane;
    if (depth == 0u)
        depth = format.sizeBytes * 8u;
    const bool depthFits = format.sizeBytes == 1u ? depth >= 1u && depth <= 8u : depth >= 9u && depth <= 16u;
    if (!depthFits)
        return ser_detail::argumentFailure("the depth decides the stored sample width", "pixelDepthPerPlane");
    if (const Status checked = checkText(request.fileId, ser_detail::kFileIdBytes, "fileId"); checked.failed())
        return checked;
    if (const Status checked = checkText(request.observer, ser_detail::kTextBytes, "observer"); checked.failed())
        return checked;
    if (const Status checked = checkText(request.instrument, ser_detail::kTextBytes, "instrument"); checked.failed())
        return checked;
    if (const Status checked = checkText(request.telescope, ser_detail::kTextBytes, "telescope"); checked.failed())
        return checked;
    if (request.dateTimeTicks > kMaxStoredTicks)
        return ser_detail::argumentFailure("SER stores signed 64-bit dates", "dateTimeTicks");
    if (request.dateTimeUtcTicks > kMaxStoredTicks)
        return ser_detail::argumentFailure("SER stores signed 64-bit dates", "dateTimeUtcTicks");
    if (request.timestamps == SerTimestampSource::PresentationTicks)
    {
        if (request.timelineOriginTicks == 0u || request.timelineOriginTicks > kMaxStoredTicks)
            return ser_detail::argumentFailure("presentation ticks need a nonzero stored origin",
                                               "timelineOriginTicks");
        if (!descriptor.timeBase ||
            std::uint64_t{descriptor.timeBase->numerator} * kSerTicksPerSecond != descriptor.timeBase->denominator)
            return Status::failure(ErrorCode::IncompatibleArguments,
                                   {.message = "presentation ticks become SER ticks only in a time base of one tick",
                                    .operation = "ser",
                                    .domain = ErrorDomain::Io,
                                    .context = {StatusContextField::ofText(ContextKey::Parameter, "timeBase")}});
        if (!descriptor.timestampsRequired)
            return Status::failure(
                ErrorCode::IncompatibleArguments,
                {.message = "a trailer from presentation ticks needs every frame to carry them",
                 .operation = "ser",
                 .domain = ErrorDomain::Io,
                 .context = {StatusContextField::ofText(ContextKey::Parameter, "timestampsRequired")}});
    }
    else if (request.timelineOriginTicks != 0u)
        return ser_detail::argumentFailure("an origin applies only to presentation ticks", "timelineOriginTicks");

    SinkPlan result;
    result.width = frame.extent().width();
    result.height = frame.extent().height();
    result.channels = frame.extent().channelCount();
    result.sampleBytes = format.sizeBytes;
    ByteOrder order = frame.byteOrder();
    if (order == ByteOrder::Native)
        order = nativeByteOrder();
    const bool big = format.sizeBytes == 2u && order == ByteOrder::Big;
    const std::uint32_t flag =
        request.endianConvention == SerEndianConvention::Compatible ? (big ? 1u : 0u) : (big ? 0u : 1u);
    auto& header = result.header;
    putText(header, 0u, request.fileId);
    ser_detail::writeLe32(header, ser_detail::kLuIdOffset, static_cast<std::uint32_t>(request.luId));
    ser_detail::writeLe32(header, ser_detail::kColorIdOffset, static_cast<std::uint32_t>(*colorId));
    ser_detail::writeLe32(header, ser_detail::kLittleEndianOffset, flag);
    ser_detail::writeLe32(header, ser_detail::kWidthOffset, result.width);
    ser_detail::writeLe32(header, ser_detail::kHeightOffset, result.height);
    ser_detail::writeLe32(header, ser_detail::kDepthOffset, depth);
    ser_detail::writeLe32(header, ser_detail::kFrameCountOffset,
                          static_cast<std::uint32_t>(descriptor.frameCount.value_or(0u)));
    putText(header, ser_detail::kObserverOffset, request.observer);
    putText(header, ser_detail::kInstrumentOffset, request.instrument);
    putText(header, ser_detail::kTelescopeOffset, request.telescope);
    ser_detail::writeLe64(header, ser_detail::kDateTimeOffset, request.dateTimeTicks);
    ser_detail::writeLe64(header, ser_detail::kDateTimeUtcOffset, request.dateTimeUtcTicks);
    if (request.timestamps != SerTimestampSource::None)
        result.initialTicks = (std::min)(descriptor.frameCount.value_or(kInitialTicks), std::uint64_t{1u} << 20u);
    return result;
}

class SerSinkBackend final : public FrameSinkBackend
{
public:
    SerSinkBackend(SequenceFile file, SequenceOutputClaim claim, const SinkPlan& plan, const SerSinkRequest& request)
        : m_file(std::move(file)), m_claim(std::move(claim)), m_pendingPath(m_claim.pendingPath()), m_width(plan.width),
          m_height(plan.height), m_channels(plan.channels), m_sampleBytes(plan.sampleBytes),
          m_rowBytes(std::uint64_t{plan.width} * plan.channels * plan.sampleBytes),
          m_frameBytes(m_rowBytes * plan.height), m_timestamps(request.timestamps),
          m_origin(request.timelineOriginTicks), m_initialTicks(plan.initialTicks)
    {
    }

    ~SerSinkBackend() override
    {
        (void)discard();
        m_claim.release();
    }

    SerSinkBackend(const SerSinkBackend&) = delete;
    SerSinkBackend& operator=(const SerSinkBackend&) = delete;

    Result<SequenceWriteResult> append(const EngineContext& engine, const ExecutionContext& execution,
                                       const SequenceFrame& frame) override
    {
        if (m_frames >= kMaxStoredFrames)
            return ser_detail::serFailure(ErrorCode::LimitExceeded, SequenceFailure::UnsupportedEncoding,
                                          "a SER container counts at most INT32_MAX frames", "frameCount",
                                          m_pendingPath);
        std::uint64_t stamp = 0u;
        if (m_timestamps == SerTimestampSource::PresentationTicks)
        {
            const auto ticks = static_cast<std::uint64_t>(*frame.position.presentationTicks);
            if (ticks > kMaxStoredTicks - m_origin)
                return ser_detail::serFailure(ErrorCode::OutOfRange, SequenceFailure::InvalidTimestamps,
                                              "a presentation tick overflows the stored timestamp", "presentationTicks",
                                              m_pendingPath);
            stamp = m_origin + ticks;
        }
        else if (m_timestamps == SerTimestampSource::ExposureStart)
        {
            if (const std::optional<UtcInstant> instant = frame.metadata.acquisition().exposureStart())
                stamp = serTicksFromUtcInstant(*instant);
        }
        if (m_timestamps != SerTimestampSource::None)
            if (const Status reserved = reserveTicks(engine, m_frames + 1u); reserved.failed())
                return reserved;
        if (const Status written = writeFrame(engine, execution, frame.frame); written.failed())
            return written;
        if (m_timestamps != SerTimestampSource::None)
            m_ticks.values()[m_frames] = stamp;
        ++m_frames;
        execution.reportTrace(TraceEventKind::Counter, "ser", "frame-bytes-written", static_cast<double>(m_frameBytes),
                              "bytes");
        return SequenceWriteResult{m_frames, kSerHeaderBytes + m_frames * m_frameBytes};
    }

    Result<SequenceWriteResult> finish(const ExecutionContext& execution) override
    {
        if (const Status stopped = execution.checkCancellation(); stopped.failed())
            return stopped;
        std::uint64_t total = kSerHeaderBytes + m_frames * m_frameBytes;
        if (m_timestamps != SerTimestampSource::None && m_frames != 0u)
        {
            // Encode in place, a bounded block at a time. Checking only at entry would leave an
            // arbitrarily long trailer uninterruptible, even though its payload is still private.
            constexpr std::uint64_t kTicksPerTransfer = 8192u;
            const std::uint64_t* const values = m_ticks.values();
            std::byte* const encoded = reinterpret_cast<std::byte*>(m_ticks.values());
            for (std::uint64_t first = 0u; first < m_frames;)
            {
                if (const Status stopped = execution.checkCancellation(); stopped.failed())
                    return stopped;
                const auto count = (std::min)(m_frames - first, kTicksPerTransfer);
                const auto bytes = static_cast<std::size_t>(count * 8u);
                std::span<std::byte> chunk(encoded + first * 8u, bytes);
                for (std::uint64_t index = 0u; index < count; ++index)
                    ser_detail::writeLe64(chunk, static_cast<std::size_t>(index * 8u), values[first + index]);
                if (const Status stopped = execution.checkCancellation(); stopped.failed())
                    return stopped;
                if (const int error = m_file.write(chunk); error != 0)
                    return ser_detail::ioFailure(error, "the SER trailer could not be written", m_pendingPath);
                execution.reportTrace(TraceEventKind::Counter, "ser", "trailer-bytes-written",
                                      static_cast<double>(bytes), "bytes");
                first += count;
            }
            total += m_frames * 8u;
        }
        if (const Status stopped = execution.checkCancellation(); stopped.failed())
            return stopped;
        std::array<std::byte, 4u> count{};
        ser_detail::writeLe32(count, 0u, static_cast<std::uint32_t>(m_frames));
        if (const int error = m_file.writeAt(count, ser_detail::kFrameCountOffset); error != 0)
            return ser_detail::ioFailure(error, "the SER frame count could not be written", m_pendingPath);
        if (const int error = m_file.close(); error != 0)
            return ser_detail::ioFailure(error, "the SER container could not be closed", m_pendingPath);
        m_finished = true;
        m_ticks.release();
        m_scratch.release();
        // The payload is complete and closed, so its output may commit it.
        m_claim.complete();
        m_claim.release();
        return SequenceWriteResult{m_frames, total};
    }

    Status close() override
    {
        const int error = discard();
        m_claim.release();
        if (error != 0)
            return ser_detail::ioFailure(error, "the unfinished SER container could not be discarded", m_pendingPath);
        return Status::success();
    }

private:
    /// Discards an unfinished container down to nothing and closes it, reporting only a platform code,
    /// so the destructor can call it without building a failure. The output's reservation stands.
    int discard() noexcept
    {
        m_ticks.release();
        m_scratch.release();
        if (m_finished || !m_file.isOpen())
            return 0;
        const int truncated = m_file.truncate();
        const int closed = m_file.close();
        return truncated != 0 ? truncated : closed;
    }

    Status reserveTicks(const EngineContext& engine, std::uint64_t needed)
    {
        if (needed <= m_tickCapacity)
            return Status::success();
        const std::uint64_t capacity = (std::max)({needed, m_tickCapacity * 2u, m_initialTicks});
        auto block = ser_detail::AccountedBlock::allocate(engine, capacity * 8u, alignof(std::uint64_t));
        if (!block.hasValue())
            return block.status();
        std::uninitialized_value_construct_n(block->values(), static_cast<std::size_t>(capacity));
        if (m_frames != 0u)
            std::memcpy(block->values(), m_ticks.values(), static_cast<std::size_t>(m_frames * 8u));
        m_ticks = std::move(*block);
        m_tickCapacity = capacity;
        return Status::success();
    }

    Status writeBytes(const ExecutionContext& execution, std::span<const std::byte> bytes)
    {
        while (!bytes.empty())
        {
            if (const Status stopped = execution.checkCancellation(); stopped.failed())
                return stopped;
            const auto chunk = static_cast<std::size_t>(
                (std::min)(static_cast<std::uint64_t>(bytes.size()), ser_detail::kTransferBytes));
            if (const int error = m_file.write(bytes.first(chunk)); error != 0)
                return ser_detail::ioFailure(error, "a SER frame could not be written", m_pendingPath);
            execution.reportTrace(TraceEventKind::Counter, "ser", "transfer-bytes", static_cast<double>(chunk),
                                  "bytes");
            bytes = bytes.subspan(chunk);
        }
        return Status::success();
    }

    Status writeFrame(const EngineContext& engine, const ExecutionContext& execution, const RawFrame& frame)
    {
        const ImageLayout layout = *frame.layout();
        const bool interleaved = layout.planeCount() == 1u && layout.samplesPerPixel() == m_channels;
        const bool planar = m_channels == 3u && layout.planeCount() == 3u && layout.samplesPerPixel() == 1u;
        if (!interleaved && !planar)
            return ser_detail::serFailure(ErrorCode::Unsupported, SequenceFailure::UnsupportedEncoding,
                                          "SER stores one plane of interleaved samples", "layout", m_pendingPath);
        if (interleaved && layout.rowStrideBytes() == m_rowBytes)
            return writeBytes(execution, frame.bytes().first(static_cast<std::size_t>(m_frameBytes)));
        // Padded rows and planar colour are packed into bounded batches of interleaved rows.
        if (m_scratch.size() == 0u)
        {
            const std::uint64_t scratchBytes =
                (std::max)(m_rowBytes, (std::min)(ser_detail::kTransferBytes, m_frameBytes));
            auto block = ser_detail::AccountedBlock::allocate(engine, scratchBytes, 1u);
            if (!block.hasValue())
                return block.status();
            m_scratch = std::move(*block);
        }
        const auto rowBytes = static_cast<std::size_t>(m_rowBytes);
        const auto capacity = static_cast<std::size_t>(m_scratch.size());
        std::size_t filled = 0u;
        for (std::uint32_t y = 0u; y < m_height; ++y)
        {
            if (filled + rowBytes > capacity)
            {
                if (const Status written = writeBytes(execution, {m_scratch.bytes(), filled}); written.failed())
                    return written;
                filled = 0u;
            }
            std::byte* const target = m_scratch.bytes() + filled;
            if (interleaved)
            {
                const auto row = *frame.row(0u, y);
                std::memcpy(target, row.data(), rowBytes);
            }
            else
            {
                const auto red = *frame.row(0u, y);
                const auto green = *frame.row(1u, y);
                const auto blue = *frame.row(2u, y);
                const std::size_t sample = m_sampleBytes;
                for (std::size_t x = 0u; x < m_width; ++x)
                {
                    std::memcpy(target + x * 3u * sample, red.data() + x * sample, sample);
                    std::memcpy(target + x * 3u * sample + sample, green.data() + x * sample, sample);
                    std::memcpy(target + x * 3u * sample + 2u * sample, blue.data() + x * sample, sample);
                }
            }
            filled += rowBytes;
        }
        return writeBytes(execution, {m_scratch.bytes(), filled});
    }

    SequenceFile m_file;
    SequenceOutputClaim m_claim;
    std::string m_pendingPath;
    std::uint32_t m_width;
    std::uint32_t m_height;
    std::uint32_t m_channels;
    std::uint32_t m_sampleBytes;
    std::uint64_t m_rowBytes;
    std::uint64_t m_frameBytes;
    SerTimestampSource m_timestamps;
    std::uint64_t m_origin;
    std::uint64_t m_initialTicks;
    ser_detail::AccountedBlock m_ticks;
    std::uint64_t m_tickCapacity = 0u;
    ser_detail::AccountedBlock m_scratch;
    std::uint64_t m_frames = 0u;
    bool m_finished = false;
};
} // namespace

Result<FrameSink> createSerSink(const EngineContext& engine, const ExecutionContext& execution, SequenceOutput& output,
                                const SequenceDescriptor& descriptor, const SerSinkRequest& request)
{
    return sequence_detail::guarded([&]() -> Result<FrameSink> {
        if (!engine.valid())
            return Status::failure(ErrorCode::NotInitialized);
        if (const Status checked = sequence_detail::checkContext(engine.id(), execution); checked.failed())
            return checked;
        if (!output.valid())
            return Status::failure(ErrorCode::InvalidState,
                                   {.message = "a SER sink writes the pending payload of an uncommitted output",
                                    .operation = "ser",
                                    .domain = ErrorDomain::Io,
                                    .context = {StatusContextField::ofText(ContextKey::Parameter, "output")}});
        if (output.engineContextId() != engine.id())
            return Status::failure(ErrorCode::IncompatibleArguments,
                                   {.message = "the output was reserved for another engine context",
                                    .operation = "ser",
                                    .domain = ErrorDomain::Io,
                                    .context = {StatusContextField::ofText(ContextKey::Parameter, "output")}});
        auto planned = plan(descriptor, request);
        if (!planned.hasValue())
            return planned.status();
        // The sequence handle checks these again when it adopts the backend; checking them first keeps every
        // refusal ahead of the first byte written.
        if (const Status checked = sequence_detail::checkDescription(engine, descriptor); checked.failed())
            return checked;
        auto claim = output.claimWriter();
        if (!claim.hasValue())
            return claim.status();
        const std::string pendingPath(claim->pendingPath());
        SequenceFile file;
        const std::filesystem::path native(
            std::u8string_view(reinterpret_cast<const char8_t*>(pendingPath.data()), pendingPath.size()));
        if (const int error = file.openForWriting(native); error != 0)
            return ser_detail::ioFailure(error, "the pending SER payload could not be opened", pendingPath);
        if (const int error = file.write(planned->header); error != 0)
        {
            (void)file.truncate();
            return ser_detail::ioFailure(error, "the SER header could not be written", pendingPath);
        }
        auto backend = std::make_unique<SerSinkBackend>(std::move(file), std::move(*claim), *planned, request);
        return FrameSink::create(engine, descriptor, std::move(backend));
    });
}
} // namespace an
