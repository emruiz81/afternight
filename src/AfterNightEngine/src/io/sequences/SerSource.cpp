// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the SER source: frames read where their index places them, the trailer as their timeline.
//
// A frame's place follows from its index, so a read never scans and a seek by index moves nothing. Each
// read allocates the frame in engine memory and fills it in bounded transfers with a cancellation check
// before each, and a BGR container is exchanged into RGB row by row. Every frame's metadata carries the
// header's observer, instrument and telescope, and its exposure start whenever its trailer entry names
// an instant, whether or not the trailer is the timeline. Time seeks are a binary search over the
// trailer and are offered only when it is the timeline.

#include <AfterNightEngine/IO/SerSequence.h>

#include <AfterNightEngine/Runtime/TraceSink.h>

#include "io/sequences/SequenceInternal.h"
#include "io/sequences/SerFormat.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>

namespace an
{
namespace
{
class SerSourceBackend final : public FrameSourceBackend
{
public:
    SerSourceBackend(ser_detail::OpenedContainer opened, std::string path, AcquisitionMetadataRequest acquisition,
                     CodecMetadata codec, ImageMetadata untimed)
        : m_file(std::move(opened.file)), m_container(std::move(opened.container)), m_ticks(std::move(opened.ticks)),
          m_timeline(opened.timeline), m_path(std::move(path)), m_acquisition(std::move(acquisition)),
          m_codec(std::move(codec)), m_untimed(std::move(untimed))
    {
    }

    Result<SequenceReadResult> read(const EngineContext& engine, const ExecutionContext& execution,
                                    std::uint64_t index) override
    {
        auto buffer = SampleBuffer::createOwned(engine, m_container.layout, StorageInitialization::Uninitialized);
        if (!buffer.hasValue())
            return buffer.status();
        const std::span<std::byte> bytes = *buffer->mutableBytes();
        const std::uint64_t frameBytes = m_container.layout.totalBytes();
        const std::uint64_t offset = kSerHeaderBytes + index * frameBytes;
        for (std::uint64_t done = 0u; done < frameBytes;)
        {
            if (const Status stopped = execution.checkCancellation(); stopped.failed())
                return stopped;
            const auto chunk = static_cast<std::size_t>((std::min)(frameBytes - done, ser_detail::kTransferBytes));
            std::size_t received = 0u;
            if (const int error = m_file.readAt(bytes.data() + done, chunk, offset + done, received); error != 0)
                return ser_detail::ioFailure(error, "a SER frame could not be read", m_path);
            if (received != chunk)
                return ser_detail::serFailure(
                    ErrorCode::InvalidData, SequenceFailure::TruncatedContainer, "the container ended inside a frame",
                    "frameIndex", m_path,
                    {StatusContextField::ofCount(ContextKey::Requested, offset + frameBytes),
                     StatusContextField::ofCount(ContextKey::Available, offset + done + received)});
            execution.reportTrace(TraceEventKind::Counter, "ser", "transfer-bytes", static_cast<double>(chunk),
                                  "bytes");
            done += chunk;
        }
        if (m_container.channelOrderExchanged)
            if (const Status exchanged = exchangeBlueAndRed(execution, bytes); exchanged.failed())
                return exchanged;
        execution.reportTrace(TraceEventKind::Counter, "ser", "frame-bytes-read", static_cast<double>(frameBytes),
                              "bytes");
        auto raw = RawFrame::create(m_container.frame, m_container.layout, std::move(*buffer));
        if (!raw.hasValue())
            return raw.status();
        auto metadata = metadataFor(index);
        if (!metadata.hasValue())
            return metadata.status();
        return SequenceReadResult{
            SequenceFrame{std::move(*raw), std::move(*metadata), {index, ticksFor(index), std::nullopt}}};
    }

    Result<SequencePosition> seek(const ExecutionContext&, const SequenceSeekRequest& request) override
    {
        if (request.kind == SequenceSeekKind::Index)
            return SequencePosition{request.index,
                                    request.index < m_container.frameCount ? ticksFor(request.index) : std::nullopt,
                                    std::nullopt};
        // Time seeks are advertised only with a timeline, whose ticks never decrease, so both rules are
        // a binary search over the trailer. A tick count relative to the first frame never overflows:
        // the first frame's instant is representable and the request is a nonnegative 64-bit count.
        const std::span<const std::uint64_t> ticks(m_ticks.values(), static_cast<std::size_t>(m_container.frameCount));
        const std::uint64_t target = ticks.front() + static_cast<std::uint64_t>(request.ticks);
        if (request.kind == SequenceSeekKind::TimeAtOrAfter)
        {
            const auto found = std::lower_bound(ticks.begin(), ticks.end(), target);
            if (found == ticks.end())
                return sequenceFailure(ErrorCode::OutOfRange, SequenceFailure::InvalidPosition);
            const auto index = static_cast<std::uint64_t>(found - ticks.begin());
            return SequencePosition{index, ticksFor(index), std::nullopt};
        }
        const auto found = std::upper_bound(ticks.begin(), ticks.end(), target);
        if (found == ticks.begin())
            return sequenceFailure(ErrorCode::OutOfRange, SequenceFailure::InvalidPosition);
        const auto index = static_cast<std::uint64_t>(found - ticks.begin()) - 1u;
        return SequencePosition{index, ticksFor(index), std::nullopt};
    }

    Status close() override
    {
        m_ticks.release();
        if (const int error = m_file.close(); error != 0)
            return ser_detail::ioFailure(error, "the SER container could not be closed", m_path);
        return Status::success();
    }

private:
    Status exchangeBlueAndRed(const ExecutionContext& execution, std::span<std::byte> bytes) const
    {
        const std::size_t sample = m_container.frame.sampleFormat().sizeBytes;
        const std::size_t pixelBytes = 3u * sample;
        const std::size_t rowBytes = static_cast<std::size_t>(m_container.layout.rowStrideBytes());
        const std::uint32_t rows = m_container.layout.extent().height();
        for (std::uint32_t row = 0u; row < rows; ++row)
        {
            if (const Status stopped = execution.checkCancellation(); stopped.failed())
                return stopped;
            std::byte* pixel = bytes.data() + static_cast<std::size_t>(row) * rowBytes;
            const std::byte* const end = pixel + rowBytes;
            for (; pixel < end; pixel += pixelBytes)
                std::swap_ranges(pixel, pixel + sample, pixel + 2u * sample);
        }
        return Status::success();
    }

    Result<ImageMetadata> metadataFor(std::uint64_t index) const
    {
        const std::optional<UtcInstant> instant =
            m_ticks.size() != 0u ? utcInstantFromSerTicks(m_ticks.values()[index]) : std::nullopt;
        if (!instant)
            return m_untimed;
        AcquisitionMetadataRequest request = m_acquisition;
        request.exposureStart = *instant;
        auto acquisition = AcquisitionMetadata::create(request);
        if (!acquisition.hasValue())
            return acquisition.status();
        return ImageMetadata::create(std::move(*acquisition), {}, {}, {}, m_codec, std::nullopt);
    }

    std::optional<std::int64_t> ticksFor(std::uint64_t index) const noexcept
    {
        if (!m_timeline)
            return std::nullopt;
        return static_cast<std::int64_t>(m_ticks.values()[index] - m_ticks.values()[0]);
    }

    SequenceFile m_file;
    SerContainer m_container;
    ser_detail::AccountedBlock m_ticks;
    bool m_timeline;
    std::string m_path;
    AcquisitionMetadataRequest m_acquisition;
    CodecMetadata m_codec;
    ImageMetadata m_untimed;
};
} // namespace

Result<FrameSource> createSerSource(const EngineContext& engine, const ExecutionContext& execution,
                                    std::string_view path, const SerSourceOptions& options)
{
    return sequence_detail::guarded([&]() -> Result<FrameSource> {
        auto opened = ser_detail::open(engine, execution, path, options);
        if (!opened.hasValue())
            return opened.status();
        const SerContainer& container = opened->container;
        AcquisitionMetadataRequest acquisition;
        acquisition.observerName = container.header.observer;
        acquisition.instrumentName = container.header.instrument;
        acquisition.telescopeName = container.header.telescope;
        auto codec = ser_detail::sourceCodecMetadata(container);
        if (!codec.hasValue())
            return codec.status();
        auto untimedAcquisition = AcquisitionMetadata::create(acquisition);
        if (!untimedAcquisition.hasValue())
            return untimedAcquisition.status();
        ImageMetadata untimed = ImageMetadata::create(std::move(*untimedAcquisition), {}, {}, {}, *codec, std::nullopt);
        SequenceDescriptor descriptor{.format = SequenceFormat::Ser,
                                      .fixedFrame = container.frame,
                                      .frameCount = container.frameCount,
                                      .seekByIndex = true};
        if (opened->timeline)
        {
            descriptor.timeBase = SequenceRate{1u, static_cast<std::uint32_t>(kSerTicksPerSecond)};
            descriptor.timestampsRequired = true;
            descriptor.seekByTime = true;
        }
        auto backend = std::make_unique<SerSourceBackend>(std::move(*opened), std::string(path), std::move(acquisition),
                                                          std::move(*codec), std::move(untimed));
        return FrameSource::create(engine, descriptor, std::move(backend));
    });
}
} // namespace an
