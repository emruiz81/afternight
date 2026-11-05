// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - a frame source that produces deterministic blank frames and reads nothing.
//
// Each read allocates one frame zero-filled from the engine context, so no two reads share storage
// and no read depends on another. Full-scale black for a signed integer representation then sets the
// most significant byte of every sample to 0x80, which makes the sample its representation's minimum
// in two's complement whatever the sample width and byte order, and leaves row and plane padding zero.

#include <AfterNightEngine/IO/DummySource.h>

#include "io/sequences/SequenceInternal.h"

namespace an
{
namespace
{
class DummyBackend final : public FrameSourceBackend
{
public:
    explicit DummyBackend(const DummySourceRequest& request) : m_request(request) {}

    Result<SequenceReadResult> read(const EngineContext& engine, const ExecutionContext& execution,
                                    std::uint64_t index) override
    {
        // The context allocator supplies zero values directly, including padding. It may use
        // already-zero pages without a full-frame write; allocation itself is not interruptible.
        auto buffer = SampleBuffer::createOwned(engine, m_request.layout, StorageInitialization::Zeroed);
        if (!buffer)
            return buffer.status();
        if (const auto stopped = execution.checkCancellation(); stopped.failed())
            return stopped;
        auto bytes = *buffer->mutableBytes();
        if (m_request.fillMode == DummyFillMode::FullScaleBlack &&
            m_request.descriptor.sampleFormat().sampleClass == RawSampleClass::SignedInteger)
        {
            const auto& layout = m_request.layout;
            const auto sampleBytes = layout.elementSizeBytes();
            const auto byteOrder = m_request.descriptor.byteOrder() == ByteOrder::Native
                                       ? nativeByteOrder()
                                       : m_request.descriptor.byteOrder();
            const auto mostSignificantByte = byteOrder == ByteOrder::Big ? 0u : sampleBytes - 1u;
            const auto rows = std::uint64_t{layout.extent().height()} * layout.planeCount();
            const auto samples = std::uint64_t{layout.extent().width()} * layout.samplesPerPixel();
            auto patterned = parallelFor(execution, rows, {1u, 1u}, [&](TaskRange range) -> Status {
                for (auto row = range.begin; row < range.end; ++row)
                {
                    const auto offset = (row / layout.extent().height()) * layout.planeStrideBytes() +
                                        (row % layout.extent().height()) * layout.rowStrideBytes();
                    for (std::uint64_t sample = 0u; sample < samples; ++sample)
                    {
                        if (sample % 4096u == 0u)
                            if (const auto stopped = execution.checkCancellation(); stopped.failed())
                                return stopped;
                        bytes[offset + sample * sampleBytes + mostSignificantByte] = std::byte{0x80};
                    }
                }
                return Status::success();
            });
            if (patterned.failed())
                return patterned;
        }
        auto raw = RawFrame::create(m_request.descriptor, m_request.layout, std::move(*buffer));
        if (!raw)
            return raw.status();
        return SequenceReadResult{SequenceFrame{std::move(*raw), {}, {index, {}, {}}}};
    }
    Result<SequencePosition> seek(const ExecutionContext&, const SequenceSeekRequest& request) override
    {
        return SequencePosition{request.index, {}, {}};
    }
    Status close() override
    {
        return Status::success();
    }

private:
    DummySourceRequest m_request;
};
} // namespace

Result<FrameSource> createDummySource(const EngineContext& engine, const ExecutionContext& execution,
                                      const DummySourceRequest& request)
{
    return sequence_detail::guarded([&]() -> Result<FrameSource> {
        if (!engine.valid())
            return Status::failure(ErrorCode::NotInitialized);
        if (const auto checked = sequence_detail::checkContext(engine.id(), execution); checked.failed())
            return checked;
        if (request.fillMode > DummyFillMode::FullScaleBlack)
            return Status::failure(ErrorCode::InvalidArgument);
        const auto& layout = request.layout;
        if (layout.extent() != request.descriptor.extent() ||
            layout.elementSizeBytes() != request.descriptor.sampleFormat().sizeBytes)
            return sequenceFailure(ErrorCode::IncompatibleArguments, SequenceFailure::DescriptorMismatch);
        if (ImageLayout::checkWithStrides(
                {layout.extent(), layout.elementSizeBytes(), layout.samplesPerPixel(), layout.planeCount(), 1u},
                layout.rowStrideBytes(), layout.planeStrideBytes(), engine.limits()) != GeometryError::None)
            return sequenceFailure(ErrorCode::LimitExceeded, SequenceFailure::DescriptorMismatch);
        return FrameSource::create(engine,
                                   {.format = SequenceFormat::Dummy,
                                    .fixedFrame = request.descriptor,
                                    .frameCount = request.frameCount,
                                    .nominalFrameRate = SequenceRate{1u, 1u},
                                    .seekByIndex = true},
                                   std::make_unique<DummyBackend>(request));
    });
}
} // namespace an
