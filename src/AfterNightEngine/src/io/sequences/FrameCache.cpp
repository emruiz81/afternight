// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - a least-recently-used frame cache in front of an index-seekable source.
//
// The cache's state is shared by the cached source and every control handed out for it, and one mutex
// serialises it, so a control may clear, resize or retune the cache from another thread while the
// source reads. A retained frame is handed out behind a lifetime token rather than copied, so a
// delivered frame keeps its samples through an eviction, a clear and the source's close. A requested
// frame may evict the least recently used frames to fit; a frame read ahead fills only unused capacity
// and never evicts anything.

#include <AfterNightEngine/IO/FrameCache.h>

#include <AfterNightEngine/IO/SequenceError.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TraceSink.h>

#include "io/sequences/SequenceInternal.h"

#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace an
{

/// A cache's retained frames, bounds and counters, shared by its source and every control.
class FrameCacheState
{
public:
    explicit FrameCacheState(const FrameCacheOptions& options) : m_options(options) {}

    /// The retained frame at an index, made the most recently used, or null.
    std::shared_ptr<SequenceFrame> find(std::uint64_t index)
    {
        const std::lock_guard lock(m_mutex);
        const auto found = m_entries.find(index);
        if (found == m_entries.end())
            return nullptr;
        m_order.splice(m_order.end(), m_order, found->second.place);
        ++m_counters.hits;
        return found->second.frame;
    }

    bool contains(std::uint64_t index) const
    {
        const std::lock_guard lock(m_mutex);
        return m_entries.contains(index);
    }

    void countMiss() noexcept
    {
        const std::lock_guard lock(m_mutex);
        ++m_counters.misses;
    }

    /// Keeps a requested frame when the bounds admit it, evicting old frames to make room.
    /// @return how many frames were evicted and whether the requested frame was retained.
    std::pair<std::uint64_t, bool> insert(std::uint64_t index, std::shared_ptr<SequenceFrame> frame)
    {
        const std::uint64_t bytes = frame->frame.buffer().byteCount();
        const std::lock_guard lock(m_mutex);
        if (m_options.maxFrames == 0u || (m_options.maxBytes != 0u && bytes > m_options.maxBytes) ||
            m_entries.contains(index))
            return {0u, m_entries.contains(index)};
        std::uint64_t evicted = 0u;
        while (!m_order.empty() && (m_entries.size() >= m_options.maxFrames ||
                                    (m_options.maxBytes != 0u && bytes > m_options.maxBytes - m_counters.bytes)))
        {
            dropOldest();
            ++evicted;
        }
        m_order.push_back(index);
        m_entries.emplace(index, Entry{std::move(frame), bytes, std::prev(m_order.end())});
        m_counters.bytes += bytes;
        m_counters.frames = m_entries.size();
        m_counters.evictions += evicted;
        return {evicted, true};
    }

    /// Whether another read-ahead decode could fit a frame by count and possibly by bytes.
    bool hasReadAheadCapacity() const
    {
        const std::lock_guard lock(m_mutex);
        return m_options.maxFrames != 0u && m_entries.size() < m_options.maxFrames &&
               (m_options.maxBytes == 0u || m_counters.bytes < m_options.maxBytes);
    }

    /// Keeps a frame read ahead only in unused capacity; it never evicts a requested frame.
    bool insertReadAhead(std::uint64_t index, std::shared_ptr<SequenceFrame> frame)
    {
        const std::uint64_t bytes = frame->frame.buffer().byteCount();
        const std::lock_guard lock(m_mutex);
        if (m_options.maxFrames == 0u || m_entries.size() >= m_options.maxFrames || m_entries.contains(index) ||
            (m_options.maxBytes != 0u && (bytes > m_options.maxBytes || bytes > m_options.maxBytes - m_counters.bytes)))
            return false;
        m_order.push_back(index);
        m_entries.emplace(index, Entry{std::move(frame), bytes, std::prev(m_order.end())});
        m_counters.bytes += bytes;
        m_counters.frames = m_entries.size();
        ++m_counters.framesReadAhead;
        return true;
    }

    void clear() noexcept
    {
        const std::lock_guard lock(m_mutex);
        m_entries.clear();
        m_order.clear();
        m_counters.bytes = 0u;
        m_counters.frames = 0u;
    }

    void resize(std::uint64_t maxFrames, std::uint64_t maxBytes) noexcept
    {
        const std::lock_guard lock(m_mutex);
        m_options.maxFrames = maxFrames;
        m_options.maxBytes = maxBytes;
        while (!m_order.empty() && (m_entries.size() > maxFrames || (maxBytes != 0u && m_counters.bytes > maxBytes)))
        {
            dropOldest();
            ++m_counters.evictions;
        }
        m_counters.frames = m_entries.size();
    }

    void setReadAhead(std::uint32_t frames) noexcept
    {
        const std::lock_guard lock(m_mutex);
        m_options.readAhead = frames;
    }

    FrameCacheOptions options() const noexcept
    {
        const std::lock_guard lock(m_mutex);
        return m_options;
    }

    FrameCacheCounters counters() const noexcept
    {
        const std::lock_guard lock(m_mutex);
        return m_counters;
    }

private:
    struct Entry
    {
        std::shared_ptr<SequenceFrame> frame;
        std::uint64_t bytes = 0u;
        std::list<std::uint64_t>::iterator place;
    };

    void dropOldest() noexcept
    {
        const auto found = m_entries.find(m_order.front());
        m_counters.bytes -= found->second.bytes;
        m_entries.erase(found);
        m_order.pop_front();
    }

    mutable std::mutex m_mutex;
    FrameCacheOptions m_options;
    FrameCacheCounters m_counters{};
    std::unordered_map<std::uint64_t, Entry> m_entries;
    std::list<std::uint64_t> m_order;
};

/// Builds controls for the one function that creates caches.
class FrameCacheAccess
{
public:
    static FrameCacheControl control(std::shared_ptr<FrameCacheState> state) noexcept
    {
        return FrameCacheControl(std::move(state));
    }
};

namespace
{
/// Hands out a retained frame behind a lifetime token, so the samples outlive an eviction, a clear and the
/// source.
Result<SequenceReadResult> share(const std::shared_ptr<SequenceFrame>& retained)
{
    const RawFrame& raw = retained->frame;
    auto buffer =
        SampleBuffer::external(SampleBufferOwnership::DecoderOwned, const_cast<std::byte*>(raw.bytes().data()),
                               raw.buffer().byteCount(), retained, false);
    if (!buffer.hasValue())
        return buffer.status();
    auto frame = RawFrame::create(*raw.descriptor(), *raw.layout(), std::move(*buffer));
    if (!frame.hasValue())
        return frame.status();
    return SequenceReadResult{SequenceFrame{std::move(*frame), retained->metadata, retained->position}};
}

class CachedSourceBackend final : public FrameSourceBackend
{
public:
    CachedSourceBackend(FrameSource source, std::shared_ptr<FrameCacheState> state, std::uint64_t frameCount) noexcept
        : m_source(std::move(source)), m_state(std::move(state)), m_frameCount(frameCount)
    {
    }

    Result<SequenceReadResult> read(const EngineContext& engine, const ExecutionContext& execution,
                                    std::uint64_t index) override
    {
        if (std::shared_ptr<SequenceFrame> hit = m_state->find(index))
        {
            execution.reportTrace(TraceEventKind::Counter, "frame-cache", "hit", 1.0, "frames");
            return share(hit);
        }
        m_state->countMiss();
        execution.reportTrace(TraceEventKind::Counter, "frame-cache", "miss", 1.0, "frames");
        auto asked = readFromSource(engine, execution, index);
        if (!asked.hasValue())
            return asked.status();
        if (!asked->frame)
            return SequenceReadResult{};
        auto retained = std::make_shared<SequenceFrame>(std::move(*asked->frame));
        const auto [evicted, kept] = m_state->insert(index, retained);
        report(execution, evicted);
        auto delivered = share(retained);
        if (!delivered.hasValue())
            return delivered.status();
        if (!kept)
        {
            execution.reportTrace(TraceEventKind::Counter, "frame-cache", "bytes",
                                  static_cast<double>(m_state->counters().bytes), "bytes");
            return delivered;
        }
        const std::uint32_t readAhead = m_state->options().readAhead;
        for (std::uint64_t ahead = 1u; ahead <= readAhead; ++ahead)
        {
            const std::uint64_t following = index + ahead;
            if (following >= m_frameCount || m_state->contains(following) || !m_state->hasReadAheadCapacity())
                break;
            if (const Status stopped = execution.checkCancellation(); stopped.failed())
                return stopped;
            auto read = readFromSource(engine, execution, following);
            if (!read.hasValue())
                return read.status();
            if (!read->frame)
                break;
            if (!m_state->insertReadAhead(following, std::make_shared<SequenceFrame>(std::move(*read->frame))))
                break;
            execution.reportTrace(TraceEventKind::Counter, "frame-cache", "read-ahead", 1.0, "frames");
        }
        execution.reportTrace(TraceEventKind::Counter, "frame-cache", "bytes",
                              static_cast<double>(m_state->counters().bytes), "bytes");
        return delivered;
    }

    Result<SequencePosition> seek(const ExecutionContext& execution, const SequenceSeekRequest& request) override
    {
        return m_source.seek(execution, request);
    }

    Status close() override
    {
        return m_source.close();
    }

private:
    Result<SequenceReadResult> readFromSource(const EngineContext& engine, const ExecutionContext& execution,
                                              std::uint64_t index)
    {
        if (m_source.nextIndex() != index)
        {
            auto positioned = m_source.seek(execution, {.kind = SequenceSeekKind::Index, .index = index});
            if (!positioned.hasValue())
                return positioned.status();
        }
        return m_source.read(engine, execution);
    }

    static void report(const ExecutionContext& execution, std::uint64_t evicted)
    {
        if (evicted != 0u)
            execution.reportTrace(TraceEventKind::Counter, "frame-cache", "eviction", static_cast<double>(evicted),
                                  "frames");
    }

    FrameSource m_source;
    std::shared_ptr<FrameCacheState> m_state;
    std::uint64_t m_frameCount;
};

Status sourceFailure(ErrorCode code, const char* message)
{
    return Status::failure(code, {.message = message,
                                  .operation = "frame-cache",
                                  .domain = ErrorDomain::Io,
                                  .context = {StatusContextField::ofText(ContextKey::Parameter, "source")}});
}

Status descriptorFailure(SequenceFailure reason, const char* message, const char* parameter)
{
    return Status::failure(
        ErrorCode::InvalidArgument,
        {.message = message,
         .operation = "frame-cache",
         .domain = ErrorDomain::Io,
         .context = {StatusContextField::ofCount(ContextKey::SequenceReason, static_cast<std::uint64_t>(reason)),
                     StatusContextField::ofText(ContextKey::Parameter, parameter)}});
}
} // namespace

FrameCacheControl::FrameCacheControl() noexcept = default;
FrameCacheControl::~FrameCacheControl() = default;
FrameCacheControl::FrameCacheControl(const FrameCacheControl& other) noexcept = default;
FrameCacheControl& FrameCacheControl::operator=(const FrameCacheControl& other) noexcept = default;
FrameCacheControl::FrameCacheControl(FrameCacheControl&& other) noexcept = default;
FrameCacheControl& FrameCacheControl::operator=(FrameCacheControl&& other) noexcept = default;

FrameCacheControl::FrameCacheControl(std::shared_ptr<FrameCacheState> state) noexcept : m_state(std::move(state)) {}

void FrameCacheControl::clear() noexcept
{
    if (m_state)
        m_state->clear();
}

void FrameCacheControl::resize(std::uint64_t maxFrames, std::uint64_t maxBytes) noexcept
{
    if (m_state)
        m_state->resize(maxFrames, maxBytes);
}

void FrameCacheControl::setReadAhead(std::uint32_t frames) noexcept
{
    if (m_state)
        m_state->setReadAhead(frames);
}

FrameCacheOptions FrameCacheControl::options() const noexcept
{
    return m_state ? m_state->options() : FrameCacheOptions{};
}

FrameCacheCounters FrameCacheControl::counters() const noexcept
{
    return m_state ? m_state->counters() : FrameCacheCounters{};
}

bool FrameCacheControl::valid() const noexcept
{
    return static_cast<bool>(m_state);
}

Result<CachedFrameSource> createCachedFrameSource(const EngineContext& engine, const ExecutionContext& execution,
                                                  FrameSource source, const FrameCacheOptions& options)
{
    return sequence_detail::guarded([&]() -> Result<CachedFrameSource> {
        if (!engine.valid())
            return Status::failure(ErrorCode::NotInitialized);
        if (const Status checked = sequence_detail::checkContext(engine.id(), execution); checked.failed())
            return checked;
        const SequenceState state = source.state();
        if (state != SequenceState::Open && state != SequenceState::EndOfStream)
            return sourceFailure(ErrorCode::InvalidState, "a cache is put in front of an open source");
        if (source.contextId() != engine.id())
            return sourceFailure(ErrorCode::IncompatibleArguments, "the source belongs to another engine context");
        const SequenceDescriptor descriptor = *source.descriptor();
        if (!descriptor.seekByIndex)
            return descriptorFailure(SequenceFailure::UnsupportedSeek, "a cache reads a source it can seek by index",
                                     "seekByIndex");
        if (!descriptor.frameCount)
            return descriptorFailure(SequenceFailure::InvalidDescriptor, "a cache reads a source of a stated length",
                                     "frameCount");
        auto shared = std::make_shared<FrameCacheState>(options);
        auto backend = std::make_unique<CachedSourceBackend>(std::move(source), shared, *descriptor.frameCount);
        auto cached = FrameSource::create(engine, descriptor, std::move(backend));
        if (!cached.hasValue())
            return cached.status();
        return CachedFrameSource{std::move(*cached), FrameCacheAccess::control(std::move(shared))};
    });
}

} // namespace an
