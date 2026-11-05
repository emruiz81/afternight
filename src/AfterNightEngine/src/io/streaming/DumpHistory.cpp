// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - reading one region's history across a list of developer dumps.
//
// Every argument, the region and the working-set bound are checked, and the history is allocated in
// one 64-byte-aligned block, before any file is opened. Each dump's region is then decoded by its
// reader and copied into the history on the operation's scheduler, one sample at a time from the
// decoded layout - planar or interleaved - to the canonical planar index, so the history's arrangement
// never depends on how a dump stored its channels. In the blocked arrangement a block of samples holds
// every frame's copy of it, and the last block is as short as the samples left.

#include <AfterNightEngine/IO/DumpHistory.h>

#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "runtime/ScheduledCall.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace an
{
struct DumpHistory::Impl
{
    EngineContextLease lease;
    MemoryResource* memory = nullptr;
    std::span<std::byte> bytes;
    std::uint64_t frames = 0u;
    std::uint64_t samples = 0u;
    std::uint32_t sampleBytes = 0u;
    std::uint32_t blockSamples = 0u;
    DumpHistoryLayout layout = DumpHistoryLayout::FrameMajor;
    DumpHistoryLayoutReason reason = DumpHistoryLayoutReason::AutomaticReference;
    ~Impl()
    {
        if (memory)
            memory->deallocate(bytes.data(), bytes.size(), 64u);
    }
    std::uint64_t offset(std::uint64_t frame, std::uint64_t sample) const noexcept
    {
        if (layout == DumpHistoryLayout::FrameMajor)
            return (frame * samples + sample) * sampleBytes;
        const auto begin = sample / blockSamples * blockSamples;
        const auto count = (std::min)(std::uint64_t{blockSamples}, samples - begin);
        return (begin * frames + frame * count + sample - begin) * sampleBytes;
    }
};
DumpHistory::DumpHistory() noexcept = default;
DumpHistory::~DumpHistory() = default;
DumpHistory::DumpHistory(DumpHistory&& other) noexcept = default;
DumpHistory& DumpHistory::operator=(DumpHistory&& other) noexcept = default;
Result<DumpHistory> DumpHistory::read(const EngineContext& engine, const ExecutionContext& execution,
                                      std::span<const std::string_view> paths, const RawFrameDescriptor& expected,
                                      const CodecRegion& region, const DumpHistoryOptions& options,
                                      DumpReaderPool* pool)
{
    return scheduled_detail::guarded([&]() -> Result<DumpHistory> {
        if (!engine.valid() || !execution.valid())
            return Status::failure(ErrorCode::NotInitialized);
        if (engine.id() != execution.engineContextId())
            return Status::failure(ErrorCode::IncompatibleArguments);
        if (paths.empty() || options.blockSamples == 0u || options.blockSamples > 65536u ||
            options.workingSetBytes == 0u ||
            static_cast<unsigned>(options.layout) > static_cast<unsigned>(DumpHistoryLayout::BlockedPixelMajor) ||
            static_cast<unsigned>(options.readers) > static_cast<unsigned>(DumpHistoryReaderPolicy::Pooled) ||
            ((options.readers == DumpHistoryReaderPolicy::Pooled) != (pool != nullptr)))
            return Status::failure(ErrorCode::InvalidArgument);
        const auto extent = expected.extent();
        if (region.width == 0u || region.height == 0u || region.width > extent.width() ||
            region.height > extent.height() || region.x > extent.width() - region.width ||
            region.y > extent.height() - region.height)
            return Status::failure(ErrorCode::OutOfRange);
        const auto cropped = ImageExtent::create(region.width, region.height, extent.channelCount(), engine.limits());
        if (!cropped)
            return Status::failure(ErrorCode::LimitExceeded);
        const auto samples = cropped->sampleCount();
        const auto size = expected.sampleFormat().sizeBytes;
        const auto maxBytes = (std::numeric_limits<std::uint64_t>::max)();
        if (samples > maxBytes / size || paths.size() == maxBytes || paths.size() + 1u > maxBytes / (samples * size))
            return Status::failure(ErrorCode::LimitExceeded);
        const auto bytes = paths.size() * samples * size;
        const auto required = bytes + samples * size;
        if (required > options.workingSetBytes)
            return Status::failure(
                ErrorCode::MemoryBudgetExceeded,
                {.operation = "dump history region",
                 .domain = ErrorDomain::Io,
                 .context = {StatusContextField::ofCount(ContextKey::Requested, required),
                             StatusContextField::ofCount(ContextKey::Limit, options.workingSetBytes)}});
        if (auto stopped = execution.checkCancellation(); stopped.failed())
            return stopped;
        auto lease = engine.acquireLease();
        if (!lease)
            return lease.status();
        DumpHistory history;
        history.m_impl = std::make_unique<Impl>();
        auto& data = *history.m_impl;
        data.lease = std::move(*lease);
        data.memory = engine.memoryResource();
        auto allocated = data.memory->allocate(bytes, 64u);
        if (!allocated)
            return allocated.status();
        data.bytes = {static_cast<std::byte*>(*allocated), static_cast<std::size_t>(bytes)};
        data.frames = paths.size();
        data.samples = samples;
        data.sampleBytes = size;
        data.blockSamples = options.blockSamples;
        if (options.layout == DumpHistoryLayout::BlockedPixelMajor)
        {
            data.layout = DumpHistoryLayout::BlockedPixelMajor;
            data.reason = DumpHistoryLayoutReason::ForcedBlocked;
        }
        else if (options.layout == DumpHistoryLayout::FrameMajor)
            data.reason = DumpHistoryLayoutReason::ForcedFrameMajor;

        for (std::size_t index = 0u; index < paths.size(); ++index)
        {
            DumpReader reference;
            DumpReaderLease held;
            DumpReader* reader = nullptr;
            if (pool)
            {
                auto acquired = pool->acquire(engine, execution, paths[index]);
                if (!acquired)
                    return acquired.status();
                held = std::move(*acquired);
                reader = held.reader();
            }
            else
            {
                auto opened = DumpReader::open(engine, execution, paths[index]);
                if (!opened)
                    return opened.status();
                reference = std::move(*opened);
                reader = &reference;
            }
            if (reader->info()->descriptor != expected)
                return Status::failure(ErrorCode::IncompatibleArguments);
            auto decoded = reader->read(engine, execution, &region);
            if (!decoded)
                return decoded.status();
            const auto layout = *decoded->frame.layout();
            const auto source = decoded->frame.bytes();
            auto copied = parallelFor(execution, samples, {size * 2u, 1u}, [&](TaskRange range) {
                for (auto sample = range.begin; sample < range.end; ++sample)
                {
                    const auto channel = sample / (std::uint64_t{region.width} * region.height);
                    const auto pixel = sample % (std::uint64_t{region.width} * region.height);
                    const auto sourceOffset =
                        channel / layout.samplesPerPixel() * layout.planeStrideBytes() +
                        pixel / region.width * layout.rowStrideBytes() +
                        (pixel % region.width * layout.samplesPerPixel() + channel % layout.samplesPerPixel()) * size;
                    std::memcpy(data.bytes.data() + data.offset(index, sample), source.data() + sourceOffset, size);
                }
                return Status::success();
            });
            if (copied.failed())
                return copied;
        }
        return history;
    });
}
DumpHistoryLayout DumpHistory::layout() const noexcept
{
    return m_impl ? m_impl->layout : DumpHistoryLayout::FrameMajor;
}
DumpHistoryLayoutReason DumpHistory::layoutReason() const noexcept
{
    return m_impl ? m_impl->reason : DumpHistoryLayoutReason::AutomaticReference;
}
std::uint64_t DumpHistory::frameCount() const noexcept
{
    return m_impl ? m_impl->frames : 0u;
}
std::uint64_t DumpHistory::samplesPerFrame() const noexcept
{
    return m_impl ? m_impl->samples : 0u;
}
std::uint64_t DumpHistory::byteCount() const noexcept
{
    return m_impl ? m_impl->bytes.size() : 0u;
}
Result<std::span<const std::byte>> DumpHistory::sample(std::uint64_t frameIndex, std::uint64_t sampleIndex) const
{
    if (!m_impl)
        return Status::failure(ErrorCode::NotInitialized);
    if (frameIndex >= m_impl->frames || sampleIndex >= m_impl->samples)
        return Status::failure(ErrorCode::OutOfRange);
    return std::span<const std::byte>{m_impl->bytes.data() + m_impl->offset(frameIndex, sampleIndex),
                                      m_impl->sampleBytes};
}
} // namespace an
