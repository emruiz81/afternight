// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - a frame source over a list of image files, decoded lazily, eagerly or through a cache.
//
// Every path is made absolute and copied when the source is created, and nothing is opened until a
// frame is read - except in eager mode, which decodes the whole list during creation and fails as a
// whole when the list does not fit its byte budget. Cached mode keeps the least recently used frames
// within its budget, evicting before it decodes the frame it expects, and a frame larger than the whole
// budget passes through uncached. A retained frame is handed out behind a lifetime token, so an
// eviction, a seek or a close never invalidates a frame a caller holds, and a frame a decoder or a
// mapping owns is copied into engine memory before it is retained.

#include <AfterNightEngine/IO/ImageListSource.h>

#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/Runtime/TraceSink.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include "core/Utf8.h"
#include "io/sequences/SequenceInternal.h"

#include <cstring>
#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>

namespace an
{
namespace
{
Status entryFailure(std::uint64_t index, std::string_view path, Status cause)
{
    const auto code = cause.code();
    return Status::failure(code,
                           {.message = "image-list entry failed",
                            .operation = "image-list",
                            .domain = ErrorDomain::Io,
                            .context = {StatusContextField::ofText(ContextKey::Parameter, "frameIndex"),
                                        StatusContextField::ofCount(ContextKey::Requested, index),
                                        StatusContextField::ofText(ContextKey::Path, std::string(path))}},
                           std::move(cause));
}

Status cacheFailure(std::uint64_t requested, std::uint64_t available, std::uint64_t limit)
{
    return Status::failure(ErrorCode::MemoryBudgetExceeded,
                           {.message = "image-list cache budget exceeded",
                            .operation = "image-list cache",
                            .domain = ErrorDomain::Io,
                            .context = {StatusContextField::ofCount(ContextKey::Requested, requested),
                                        StatusContextField::ofCount(ContextKey::Available, available),
                                        StatusContextField::ofCount(ContextKey::Limit, limit)}});
}

constexpr std::uint64_t kNoEntry = (std::numeric_limits<std::uint64_t>::max)();

struct ListEntry
{
    CodecReadRequest request;
    std::shared_ptr<CodecDecodeResult> decoded;
    std::uint64_t previous = kNoEntry;
    std::uint64_t next = kNoEntry;
};

// One allocation from the engine context holds the entry table, its least-recently-used links and every
// copied path, so the bookkeeping that grows with the frame count is counted against the engine's budget;
// only the small control objects of a decoded frame use ordinary allocations.
class EntryTable
{
public:
    EntryTable(EngineContextLease lease, MemoryResource* resource) : m_lease(std::move(lease)), m_resource(resource) {}
    ~EntryTable()
    {
        std::destroy_n(m_entries.data(), m_entries.size());
        m_resource->deallocate(m_entries.data(), m_bytes, alignof(ListEntry));
    }
    EntryTable(const EntryTable&) = delete;
    EntryTable& operator=(const EntryTable&) = delete;

    Status allocate(std::size_t count, std::uint64_t pathBytes)
    {
        m_bytes = count * sizeof(ListEntry) + pathBytes;
        if (m_bytes == 0u)
            return Status::success();
        auto allocation = m_resource->allocate(m_bytes, alignof(ListEntry));
        if (!allocation)
            return allocation.status();
        m_entries = {static_cast<ListEntry*>(*allocation), count};
        std::uninitialized_value_construct_n(m_entries.data(), count);
        m_paths = {reinterpret_cast<char*>(m_entries.data() + count), static_cast<std::size_t>(pathBytes)};
        return Status::success();
    }
    std::span<ListEntry> entries() noexcept
    {
        return m_entries;
    }
    std::span<char> paths() noexcept
    {
        return m_paths;
    }

private:
    EngineContextLease m_lease;
    MemoryResource* m_resource;
    std::span<ListEntry> m_entries;
    std::span<char> m_paths;
    std::uint64_t m_bytes = 0u;
};

Result<std::string> absolutePath(const CodecReadRequest& request, std::uint64_t index)
{
    if (!isBoundaryPath(request.path) || !isCodecFormat(request.format))
        return entryFailure(index, request.path.substr(0u, 131072u), Status::failure(ErrorCode::InvalidArgument));
    const auto native = std::filesystem::path(
        std::u8string_view(reinterpret_cast<const char8_t*>(request.path.data()), request.path.size()));
    std::error_code error;
    const auto absolute = std::filesystem::absolute(native, error);
    if (error)
        return entryFailure(index, request.path, Status::failure(ErrorCode::ExternalFailure));
    const auto text = absolute.u8string();
    std::string path(reinterpret_cast<const char*>(text.data()), text.size());
    if (!isBoundaryPath(path))
        return entryFailure(index, path, Status::failure(ErrorCode::LimitExceeded));
    return path;
}

class ImageListBackend final : public FrameSourceBackend
{
public:
    ImageListBackend(std::unique_ptr<EntryTable> table, const ImageListSourceOptions& options)
        : m_table(std::move(table)), m_entries(m_table->entries()), m_options(options)
    {
    }

    Status preload(const EngineContext& engine, const ExecutionContext& execution)
    {
        for (std::uint64_t index = 0u; index < m_entries.size(); ++index)
        {
            if (const auto stopped = execution.checkCancellation(); stopped.failed())
                return stopped;
            auto result = read(engine, execution, index);
            if (!result)
                return result.status();
        }
        return execution.checkCancellation();
    }

    Result<SequenceReadResult> read(const EngineContext& engine, const ExecutionContext& execution,
                                    std::uint64_t index) override
    {
        auto& entry = m_entries[static_cast<std::size_t>(index)];
        if (entry.decoded)
        {
            unlink(index);
            linkLast(index);
            execution.reportTrace(TraceEventKind::Instant, "image-list", "cache-hit");
            return share(index, entry.decoded);
        }
        execution.reportTrace(TraceEventKind::Instant, "image-list", "cache-miss");
        auto result = load(engine, execution, index, entry.request);
        if (!result)
            return entryFailure(index, entry.request.path, result.status());
        return result;
    }
    Result<SequencePosition> seek(const ExecutionContext&, const SequenceSeekRequest& request) override
    {
        return SequencePosition{request.index, {}, {}};
    }
    Status close() override
    {
        m_table.reset();
        m_entries = {};
        m_oldest = m_newest = kNoEntry;
        m_cacheBytes = 0u;
        return Status::success();
    }

private:
    void unlink(std::uint64_t index)
    {
        auto& entry = m_entries[index];
        if (entry.previous == kNoEntry)
            m_oldest = entry.next;
        else
            m_entries[entry.previous].next = entry.next;
        if (entry.next == kNoEntry)
            m_newest = entry.previous;
        else
            m_entries[entry.next].previous = entry.previous;
        entry.previous = entry.next = kNoEntry;
    }
    void linkLast(std::uint64_t index)
    {
        auto& entry = m_entries[index];
        entry.previous = m_newest;
        if (m_newest == kNoEntry)
            m_oldest = index;
        else
            m_entries[m_newest].next = index;
        m_newest = index;
    }
    void evictFor(std::uint64_t bytes, const ExecutionContext& execution)
    {
        while (m_oldest != kNoEntry && bytes > m_options.cacheBudgetBytes - m_cacheBytes)
        {
            auto& oldest = m_entries[m_oldest];
            m_cacheBytes -= oldest.decoded->frame.buffer().byteCount();
            unlink(m_oldest);
            oldest.decoded.reset();
            execution.reportTrace(TraceEventKind::Instant, "image-list", "cache-eviction");
        }
        reportBytes(execution);
    }
    void reportBytes(const ExecutionContext& execution) const
    {
        execution.reportTrace(TraceEventKind::Counter, "image-list", "cache-bytes", static_cast<double>(m_cacheBytes),
                              "bytes");
    }
    Result<SequenceReadResult> share(std::uint64_t index, const std::shared_ptr<CodecDecodeResult>& decoded)
    {
        auto buffer = SampleBuffer::external(SampleBufferOwnership::DecoderOwned,
                                             const_cast<std::byte*>(decoded->frame.bytes().data()),
                                             decoded->frame.buffer().byteCount(), decoded, false);
        if (!buffer)
            return buffer.status();
        auto raw = RawFrame::create(*decoded->frame.descriptor(), *decoded->frame.layout(), std::move(*buffer));
        if (!raw)
            return raw.status();
        return SequenceReadResult{SequenceFrame{std::move(*raw), decoded->metadata, {index, {}, {}}}};
    }
    Result<SequenceReadResult> load(const EngineContext& engine, const ExecutionContext& execution, std::uint64_t index,
                                    const CodecReadRequest& request)
    {
        const bool retaining = m_options.mode != ImageListLoadMode::Lazy;
        if (retaining)
        {
            auto headerRequest = request;
            headerRequest.validatePayload = false;
            auto info = probeImage(engine, execution, headerRequest);
            if (!info)
                return info.status();
            if (m_options.fixedFrame && *m_options.fixedFrame != info->descriptor)
                return sequenceFailure(ErrorCode::IncompatibleArguments, SequenceFailure::DescriptorMismatch);
            const auto required = info->decodedLayout.totalBytes();
            if (m_options.mode == ImageListLoadMode::Eager && required > m_options.cacheBudgetBytes - m_cacheBytes)
                return cacheFailure(required, m_options.cacheBudgetBytes - m_cacheBytes, m_options.cacheBudgetBytes);
            if (m_options.mode == ImageListLoadMode::Cached && required <= m_options.cacheBudgetBytes)
                evictFor(required, execution);
        }
        auto decoded = decodeImage(engine, execution, request);
        if (!decoded)
            return decoded.status();
        if (m_options.fixedFrame && *m_options.fixedFrame != *decoded->frame.descriptor())
            return sequenceFailure(ErrorCode::IncompatibleArguments, SequenceFailure::DescriptorMismatch);
        auto bytes = decoded->frame.buffer().byteCount();
        if (retaining && decoded->frame.buffer().ownership() != SampleBufferOwnership::Owned)
            bytes = decoded->frame.layout()->totalBytes();
        if (m_options.mode == ImageListLoadMode::Eager && bytes > m_options.cacheBudgetBytes - m_cacheBytes)
            return cacheFailure(bytes, m_options.cacheBudgetBytes - m_cacheBytes, m_options.cacheBudgetBytes);
        if (!retaining || bytes > m_options.cacheBudgetBytes)
            return SequenceReadResult{
                SequenceFrame{std::move(decoded->frame), std::move(decoded->metadata), {index, {}, {}}}};
        if (m_options.mode == ImageListLoadMode::Cached)
            evictFor(bytes, execution);
        if (decoded->frame.buffer().ownership() != SampleBufferOwnership::Owned)
        {
            auto buffer =
                SampleBuffer::createOwned(engine, *decoded->frame.layout(), StorageInitialization::Uninitialized);
            if (!buffer)
                return buffer.status();
            auto destination = *buffer->mutableBytes();
            constexpr std::size_t kCopyChunk = 1024u * 1024u;
            for (std::size_t offset = 0u; offset < destination.size();)
            {
                if (const auto stopped = execution.checkCancellation(); stopped.failed())
                    return stopped;
                const auto count = (std::min)(kCopyChunk, destination.size() - offset);
                std::memcpy(destination.data() + offset, decoded->frame.bytes().data() + offset, count);
                offset += count;
            }
            auto raw = RawFrame::create(*decoded->frame.descriptor(), *decoded->frame.layout(), std::move(*buffer));
            if (!raw)
                return raw.status();
            decoded->frame = std::move(*raw);
            execution.reportTrace(TraceEventKind::Counter, "image-list", "cache-copy-bytes", static_cast<double>(bytes),
                                  "bytes");
        }
        auto stored = std::make_shared<CodecDecodeResult>(std::move(*decoded));
        m_entries[index].decoded = stored;
        linkLast(index);
        m_cacheBytes += bytes;
        reportBytes(execution);
        return share(index, stored);
    }

    std::unique_ptr<EntryTable> m_table;
    std::span<ListEntry> m_entries;
    ImageListSourceOptions m_options;
    std::uint64_t m_oldest = kNoEntry;
    std::uint64_t m_newest = kNoEntry;
    std::uint64_t m_cacheBytes = 0u;
};
} // namespace

Result<FrameSource> createImageListSource(const EngineContext& engine, const ExecutionContext& execution,
                                          std::span<const CodecReadRequest> images,
                                          const ImageListSourceOptions& options)
{
    return sequence_detail::guarded([&]() -> Result<FrameSource> {
        if (!engine.valid())
            return Status::failure(ErrorCode::NotInitialized);
        if (const auto checked = sequence_detail::checkContext(engine.id(), execution); checked.failed())
            return checked;
        if (options.mode > ImageListLoadMode::Cached ||
            ((options.mode == ImageListLoadMode::Lazy) != (options.cacheBudgetBytes == 0u)))
            return Status::failure(ErrorCode::InvalidArgument);
        if (images.size() > 1048576u)
            return Status::failure(ErrorCode::LimitExceeded);
        const SequenceDescriptor descriptor{.format = SequenceFormat::ImageList,
                                            .fixedFrame = options.fixedFrame,
                                            .frameCount = images.size(),
                                            .seekByIndex = true};
        if (const auto checked = sequence_detail::checkDescription(engine, descriptor); checked.failed())
            return checked;
        std::uint64_t pathBytes = 0u;
        constexpr std::uint64_t kPathBudgetBytes = 64u * 1024u * 1024u;
        for (std::size_t index = 0u; index < images.size(); ++index)
        {
            if (const auto stopped = execution.checkCancellation(); stopped.failed())
                return stopped;
            auto path = absolutePath(images[index], index);
            if (!path)
                return path.status();
            if (path->size() > kPathBudgetBytes - pathBytes)
                return entryFailure(index, *path, Status::failure(ErrorCode::LimitExceeded));
            pathBytes += path->size();
        }
        auto lease = engine.acquireLease();
        if (!lease)
            return lease.status();
        auto table = std::make_unique<EntryTable>(std::move(*lease), engine.memoryResource());
        if (const auto allocated = table->allocate(images.size(), pathBytes); allocated.failed())
            return allocated;
        auto remaining = table->paths();
        for (std::size_t index = 0u; index < images.size(); ++index)
        {
            if (const auto stopped = execution.checkCancellation(); stopped.failed())
                return stopped;
            auto path = absolutePath(images[index], index);
            if (!path)
                return path.status();
            if (path->size() > remaining.size())
                return entryFailure(index, *path, Status::failure(ErrorCode::InvalidState));
            std::memcpy(remaining.data(), path->data(), path->size());
            auto& entry = table->entries()[index];
            entry.request = images[index];
            entry.request.path = {remaining.data(), path->size()};
            remaining = remaining.subspan(path->size());
        }
        auto backend = std::make_unique<ImageListBackend>(std::move(table), options);
        if (options.mode == ImageListLoadMode::Eager)
        {
            const auto loaded = parallelFor(execution, 1u, {1u, 1u}, [&](TaskRange) {
                return sequence_detail::guarded([&] { return backend->preload(engine, execution); });
            });
            if (loaded.failed())
                return loaded;
        }
        if (const auto stopped = execution.checkCancellation(); stopped.failed())
            return stopped;
        return FrameSource::create(engine, descriptor, std::move(backend));
    });
}
} // namespace an
