// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the bounded read-process-write stream over a frame source and sink.
//
// Up to three cooperative stages run: processing on the calling thread, and reading ahead and writing
// behind on coordinator threads when the execution admits more than one thread. The stages meet at two
// fixed-size queues allocated from the engine context before any frame is read. A producer reserves
// a slot before it reads or hands over a frame and the consumer releases it only once the frame is
// gone, so the frames alive in a queue never exceed its depth or its byte budget. Every failure is
// wrapped with its stage and frame index at the point it happens, and the first one to arrive stops
// the others.

#include <AfterNightEngine/IO/FrameStream.h>

#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "runtime/ScheduledCall.h"
#include "runtime/CooperativeTasks.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace an
{
namespace
{
using Clock = std::chrono::steady_clock;

void add(std::uint64_t& value, std::uint64_t increment) noexcept
{
    value += (std::min)(increment, (std::numeric_limits<std::uint64_t>::max)() - value);
}
std::uint64_t nanoseconds(Clock::time_point start) noexcept
{
    return static_cast<std::uint64_t>((
        std::max)(std::int64_t{0}, std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count()));
}
Status atFrame(Status status, FrameStreamStage stage, std::uint64_t index)
{
    if (status.ok())
        return status;
    // Read before the cause is moved into the new status: the two arguments have no fixed order.
    const ErrorCode code = status.code();
    return Status::failure(
        code,
        {.operation = "streamFrames",
         .domain = ErrorDomain::Io,
         .context = {StatusContextField::ofCount(ContextKey::FrameIndex, index),
                     StatusContextField::ofCount(ContextKey::StreamStage, static_cast<std::uint64_t>(stage))}},
        std::move(status));
}
Status frameLimit(std::uint64_t bytes, std::uint64_t limit)
{
    return Status::failure(ErrorCode::MemoryBudgetExceeded,
                           {.operation = "stream frame reservation",
                            .domain = ErrorDomain::Io,
                            .context = {StatusContextField::ofCount(ContextKey::Requested, bytes),
                                        StatusContextField::ofCount(ContextKey::Limit, limit)}});
}

// The ring and reservations are one bounded allocation, independent of the sequence length.
// Reserving BEFORE decode prevents a slow consumer from allowing an extra unaccounted frame.
class FrameQueue
{
public:
    FrameQueue(MemoryResource& memory, FrameQueueCounters& counters) : m_memory(memory), m_counters(counters) {}
    ~FrameQueue()
    {
        std::destroy_n(m_slots.data(), m_slots.size());
        m_memory.deallocate(m_slots.data(), m_slots.size_bytes(), alignof(Slot));
    }
    FrameQueue(const FrameQueue&) = delete;
    FrameQueue& operator=(const FrameQueue&) = delete;

    Status allocate(std::uint32_t depth, std::uint64_t budget, std::uint64_t frameBytes, std::uint64_t limit)
    {
        const auto capacity = (std::min)({std::uint64_t{depth}, budget / frameBytes, limit});
        m_frameBytes = frameBytes;
        if (capacity == 0u)
            return Status::success();
        auto allocated = m_memory.allocate(capacity * sizeof(Slot), alignof(Slot));
        if (!allocated)
            return allocated.status();
        m_slots = {static_cast<Slot*>(*allocated), static_cast<std::size_t>(capacity)};
        std::uninitialized_value_construct_n(m_slots.data(), m_slots.size());
        return Status::success();
    }
    Status reserve(const ExecutionContext& execution)
    {
        std::unique_lock lock(m_mutex);
        auto ready = [&] { return m_reserved < m_slots.size(); };
        if (const auto waited = wait(execution, lock, ready); waited.failed())
            return waited;
        ++m_reserved;
        m_counters.peakFrames = (std::max)(m_counters.peakFrames, m_reserved);
        m_counters.peakReservedBytes = (std::max)(m_counters.peakReservedBytes, m_reserved * m_frameBytes);
        return Status::success();
    }
    void push(SequenceFrame frame)
    {
        {
            const std::lock_guard lock(m_mutex);
            m_slots[m_tail].emplace(std::move(frame));
            m_tail = (m_tail + 1u) % m_slots.size();
            ++m_ready;
        }
        m_changed.notify_all();
    }
    Result<SequenceReadResult> pop(const ExecutionContext& execution, bool releaseReservation)
    {
        std::unique_lock lock(m_mutex);
        if (const auto waited = wait(execution, lock, [&] { return m_ready != 0u || m_finished; }); waited.failed())
            return waited;
        if (m_ready == 0u)
            return SequenceReadResult{};
        SequenceReadResult result{std::move(m_slots[m_head])};
        m_slots[m_head].reset();
        m_head = (m_head + 1u) % m_slots.size();
        --m_ready;
        if (releaseReservation)
            --m_reserved;
        lock.unlock();
        m_changed.notify_all();
        return result;
    }
    void release()
    {
        {
            const std::lock_guard lock(m_mutex);
            --m_reserved;
        }
        m_changed.notify_all();
    }
    void finish()
    {
        {
            const std::lock_guard lock(m_mutex);
            m_finished = true;
        }
        m_changed.notify_all();
    }

private:
    template <typename Predicate>
    Status wait(const ExecutionContext& execution, std::unique_lock<std::mutex>& lock, Predicate ready)
    {
        if (const auto stopped = execution.checkCancellation(); stopped.failed())
            return stopped;
        if (ready())
            return Status::success();
        const auto start = Clock::now();
        add(m_counters.waits, 1u);
        if (execution.deadline())
            m_changed.wait_until(lock, execution.stopToken(), *execution.deadline(), ready);
        else
            m_changed.wait(lock, execution.stopToken(), ready);
        const auto elapsed = nanoseconds(start);
        add(m_counters.waitNanoseconds, elapsed);
        m_counters.longestWaitNanoseconds = (std::max)(m_counters.longestWaitNanoseconds, elapsed);
        return execution.checkCancellation();
    }

    using Slot = std::optional<SequenceFrame>;
    MemoryResource& m_memory;
    FrameQueueCounters& m_counters;
    std::span<Slot> m_slots;
    std::mutex m_mutex;
    std::condition_variable_any m_changed;
    std::size_t m_head = 0u;
    std::size_t m_tail = 0u;
    std::size_t m_ready = 0u;
    std::uint64_t m_reserved = 0u;
    std::uint64_t m_frameBytes = 0u;
    bool m_finished = false;
};

class StreamRun final : public CooperativeTaskBody
{
public:
    StreamRun(const EngineContext& engine, FrameSource& source, FrameSink* sink, FrameStreamProcessor* processor,
              const FrameStreamOptions& options, FrameStreamCounters& counters, bool readAhead, bool writeBehind)
        : m_engine(engine), m_source(source), m_sink(sink), m_processor(processor), m_options(options),
          m_counters(counters), m_input(*engine.memoryResource(), counters.readAhead),
          m_output(*engine.memoryResource(), counters.writeBehind), m_readAhead(readAhead), m_writeBehind(writeBehind)
    {
    }
    Status allocate()
    {
        if (m_readAhead)
            if (auto status = m_input.allocate(m_options.readAheadDepth, m_options.readAheadBytes,
                                               m_options.maxFrameBytes, m_options.frameLimit);
                status.failed())
                return status;
        if (m_writeBehind)
            return m_output.allocate(m_options.writeBehindDepth, m_options.writeBehindBytes, m_options.maxFrameBytes,
                                     m_options.frameLimit);
        return Status::success();
    }
    Status run(const ExecutionContext& execution, std::uint32_t stage) override
    {
        if (stage == 0u)
            return process(execution);
        if (m_readAhead && stage == 1u)
            return readAhead(execution);
        return writeBehind(execution);
    }

private:
    Result<SequenceReadResult> read(const ExecutionContext& execution)
    {
        const auto index = m_source.nextIndex();
        const auto start = Clock::now();
        auto result = m_source.read(m_engine, execution);
        add(m_counters.readNanoseconds, nanoseconds(start));
        if (!result)
            return atFrame(result.status(), FrameStreamStage::Read, index);
        if (!result->frame)
            return std::move(*result);
        auto& frame = result->frame->frame;
        const auto bytes = frame.buffer().byteCount();
        if (bytes > m_options.maxFrameBytes)
            return atFrame(frameLimit(bytes, m_options.maxFrameBytes), FrameStreamStage::Read, index);
        // A decoder token proves lifetime, not accounting. Copy the exact layout once, without
        // conversion or repacking, so the queued payload really belongs to this context's budget.
        if (frame.buffer().ownership() != SampleBufferOwnership::Owned)
        {
            const auto layout = *frame.layout();
            auto buffer = SampleBuffer::createOwned(m_engine, layout, StorageInitialization::Uninitialized);
            if (!buffer)
                return atFrame(buffer.status(), FrameStreamStage::Read, index);
            auto destination = *buffer->mutableBytes();
            constexpr std::size_t kCopyBytes = 4u * 1024u * 1024u;
            for (std::size_t offset = 0u; offset < destination.size(); offset += kCopyBytes)
            {
                if (auto status = execution.checkCancellation(); status.failed())
                    return atFrame(std::move(status), FrameStreamStage::Read, index);
                const auto count = (std::min)(kCopyBytes, destination.size() - offset);
                std::memcpy(destination.data() + offset, frame.bytes().data() + offset, count);
            }
            auto owned = RawFrame::create(*frame.descriptor(), layout, std::move(*buffer));
            if (!owned)
                return atFrame(owned.status(), FrameStreamStage::Read, index);
            frame = std::move(*owned);
        }
        add(m_counters.framesRead, 1u);
        add(m_counters.bytesRead, bytes);
        return std::move(*result);
    }
    Status append(const ExecutionContext& execution, const SequenceFrame& frame)
    {
        const auto start = Clock::now();
        auto result = m_sink->append(m_engine, execution, frame);
        add(m_counters.writeNanoseconds, nanoseconds(start));
        if (!result)
            return atFrame(result.status(), FrameStreamStage::Write, frame.position.index);
        add(m_counters.framesWritten, 1u);
        add(m_counters.bytesWritten, frame.frame.buffer().byteCount());
        return Status::success();
    }
    Status finish(const ExecutionContext& execution)
    {
        if (!m_sink || !m_options.finishSink)
            return Status::success();
        const auto index = m_sink->progress().framesWritten;
        const auto start = Clock::now();
        auto result = m_sink->finish(execution);
        add(m_counters.writeNanoseconds, nanoseconds(start));
        return result ? Status::success() : atFrame(result.status(), FrameStreamStage::Finish, index);
    }
    Status readAhead(const ExecutionContext& execution)
    {
        for (std::uint64_t count = 0u; count < m_options.frameLimit; ++count)
        {
            if (auto status = m_input.reserve(execution); status.failed())
                return status;
            auto result = read(execution);
            if (!result || !result->frame)
            {
                m_input.release();
                if (!result)
                    return result.status();
                break;
            }
            m_input.push(std::move(*result->frame));
        }
        m_input.finish();
        return Status::success();
    }
    Status writeBehind(const ExecutionContext& execution)
    {
        for (;;)
        {
            auto result = m_output.pop(execution, false);
            if (!result)
                return result.status();
            if (!result->frame)
                break;
            auto status = append(execution, *result->frame);
            result->frame.reset();
            m_output.release();
            if (status.failed())
                return status;
        }
        return finish(execution);
    }
    Status process(const ExecutionContext& execution)
    {
        for (std::uint64_t count = 0u; count < m_options.frameLimit; ++count)
        {
            auto input = m_readAhead ? m_input.pop(execution, true) : read(execution);
            if (!input)
                return input.status();
            if (!input->frame)
                break;
            const auto position = input->frame->position;
            const auto start = Clock::now();
            Result<SequenceFrame> output = std::move(*input->frame);
            if (m_processor)
            {
                bool entered = false;
                output = scheduled_detail::call(execution, entered, false, [&] {
                    return m_processor->process(m_engine, execution, std::move(*output));
                });
            }
            add(m_counters.processNanoseconds, nanoseconds(start));
            if (!output)
                return atFrame(output.status(), FrameStreamStage::Process, position.index);
            if (output->position.index != position.index ||
                output->position.presentationTicks != position.presentationTicks ||
                output->position.durationTicks != position.durationTicks || !output->frame.valid() ||
                output->frame.buffer().contextId() != m_engine.id())
                return atFrame(Status::failure(ErrorCode::IncompatibleArguments), FrameStreamStage::Process,
                               position.index);
            const auto bytes = output->frame.buffer().byteCount();
            if (bytes > m_options.maxFrameBytes)
                return atFrame(frameLimit(bytes, m_options.maxFrameBytes), FrameStreamStage::Process, position.index);
            add(m_counters.framesProcessed, 1u);
            if (m_sink)
            {
                if (m_writeBehind)
                {
                    if (auto status = m_output.reserve(execution); status.failed())
                        return atFrame(std::move(status), FrameStreamStage::Write, position.index);
                    m_output.push(std::move(*output));
                }
                else if (auto status = append(execution, *output); status.failed())
                    return status;
            }
        }
        if (m_writeBehind)
        {
            m_output.finish();
            return Status::success();
        }
        return finish(execution);
    }

    const EngineContext& m_engine;
    FrameSource& m_source;
    FrameSink* m_sink;
    FrameStreamProcessor* m_processor;
    const FrameStreamOptions& m_options;
    FrameStreamCounters& m_counters;
    FrameQueue m_input;
    FrameQueue m_output;
    bool m_readAhead;
    bool m_writeBehind;
};
} // namespace

Status streamFrames(const EngineContext& engine, const ExecutionContext& execution, FrameSource& source,
                    FrameSink* sink, FrameStreamProcessor* processor, const FrameStreamOptions& options,
                    FrameStreamCounters& counters)
{
    counters = {};
    return scheduled_detail::guarded([&]() -> Status {
        if (!engine.valid() || !execution.valid())
            return Status::failure(ErrorCode::NotInitialized);
        if (engine.id() != execution.engineContextId() || source.contextId() != engine.id() ||
            (sink && sink->contextId() != engine.id()))
            return Status::failure(ErrorCode::IncompatibleArguments);
        if ((source.state() != SequenceState::Open && source.state() != SequenceState::EndOfStream) ||
            (sink && sink->state() != SequenceState::Open))
            return Status::failure(ErrorCode::InvalidState);
        if (options.maxFrameBytes == 0u || options.readAheadDepth > 4096u || options.writeBehindDepth > 4096u ||
            (options.readAheadDepth != 0u && options.readAheadBytes < options.maxFrameBytes) ||
            (sink && options.writeBehindDepth != 0u && options.writeBehindBytes < options.maxFrameBytes))
            return Status::failure(ErrorCode::InvalidArgument);
        if (auto status = execution.checkCancellation(); status.failed())
            return status;

        const bool parallel = execution.admittedConcurrency() > 1u && options.frameLimit != 0u;
        const bool readAhead = parallel && options.readAheadDepth != 0u;
        const bool writeBehind = parallel && sink && options.writeBehindDepth != 0u;
        StreamRun run(engine, source, sink, processor, options, counters, readAhead, writeBehind);
        if (auto status = run.allocate(); status.failed())
            return status;
        const auto count = 1u + static_cast<std::uint32_t>(readAhead) + static_cast<std::uint32_t>(writeBehind);
        auto status = scheduled_detail::guarded([&] { return runCooperativeTasks(execution, count, run); });
        if (status.failed())
        {
            (void)source.close();
            if (sink)
                (void)sink->close();
        }
        return status;
    });
}
} // namespace an
