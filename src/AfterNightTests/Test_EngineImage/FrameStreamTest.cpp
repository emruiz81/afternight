// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/IO/FrameStream.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>
#include <AfterNightEngine/Runtime/TraceSink.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <future>
#include <mutex>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <vector>

namespace
{
using namespace std::chrono_literals;
constexpr std::uint64_t kBytes = 256u;

an::RawFrameDescriptor description()
{
    return *an::RawFrameDescriptor::create({.extent = *an::ImageExtent::create(16u, 16u, 1u, an::defaultEngineLimits()),
                                            .sampleFormat = {1u, an::RawSampleClass::UnsignedInteger},
                                            .numericDomain = an::NumericDomain::RawSensorCode,
                                            .pixelAspectRatio = 1.0});
}
an::SequenceDescriptor sequence(std::uint64_t count)
{
    return {.format = an::SequenceFormat::ImageList, .fixedFrame = description(), .frameCount = count};
}
struct Control
{
    std::uint64_t count = 40u;
    std::uint64_t failRead = 999u;
    std::uint64_t failWrite = 999u;
    bool failFinish = false;
    bool throwRead = false;
    bool external = false;
    bool slowRead = false;
    bool slowWrite = false;
    bool waitForStop = false;
    bool blockWrite = false;
    std::atomic<bool> writing = false;
    std::atomic<bool> reading = false;
    std::atomic<bool> stopped = false;
    std::atomic<std::uint64_t> reads = 0u;
    std::uint64_t sourceCloses = 0u;
    std::uint64_t sinkCloses = 0u;
    std::uint64_t finishes = 0u;
    std::vector<std::uint64_t> written;
};
class Source final : public an::FrameSourceBackend
{
public:
    explicit Source(Control& control) : m_control(control) {}
    an::Result<an::SequenceReadResult> read(const an::EngineContext& engine, const an::ExecutionContext& execution,
                                            std::uint64_t index) override
    {
        ++m_control.reads;
        if (index == m_control.failRead)
        {
            if (m_control.throwRead)
                throw std::runtime_error("broken decoder");
            return an::Status::failure(an::ErrorCode::InvalidData);
        }
        if (m_control.waitForStop && index == 1u)
        {
            m_control.reading = true;
            std::mutex mutex;
            std::condition_variable_any changed;
            std::unique_lock lock(mutex);
            changed.wait_until(lock, execution.stopToken(), std::chrono::steady_clock::now() + 5s,
                               [] { return false; });
            m_control.stopped = execution.stopRequested();
            return execution.checkCancellation();
        }
        if (m_control.slowRead)
            std::this_thread::sleep_for(200us);
        const auto layout = *an::ImageLayout::create({description().extent(), 1u, 1u, 1u, 1u}, engine.limits());
        auto buffer = an::SampleBuffer::createOwned(engine, layout, an::StorageInitialization::Uninitialized);
        if (!buffer)
            return buffer.status();
        std::memset(buffer->mutableBytes()->data(), static_cast<int>(index & 255u), kBytes);
        if (m_control.external)
        {
            auto token = std::make_shared<std::array<std::byte, kBytes>>();
            token->fill(static_cast<std::byte>(index & 255u));
            buffer = an::SampleBuffer::external(an::SampleBufferOwnership::DecoderOwned, token->data(), kBytes, token,
                                                false);
        }
        auto raw = an::RawFrame::create(description(), layout, std::move(*buffer));
        if (!raw)
            return raw.status();
        return an::SequenceReadResult{an::SequenceFrame{std::move(*raw), {}, {index}}};
    }
    an::Status close() override
    {
        ++m_control.sourceCloses;
        return an::Status::success();
    }

private:
    Control& m_control;
};
class Sink final : public an::FrameSinkBackend
{
public:
    explicit Sink(Control& control) : m_control(control) {}
    an::Result<an::SequenceWriteResult> append(const an::EngineContext&, const an::ExecutionContext& execution,
                                               const an::SequenceFrame& frame) override
    {
        if (m_control.blockWrite)
        {
            m_control.writing = true;
            std::mutex mutex;
            std::condition_variable_any changed;
            std::unique_lock lock(mutex);
            changed.wait_until(lock, execution.stopToken(),
                               execution.deadline().value_or(std::chrono::steady_clock::now() + 5s),
                               [] { return false; });
            return execution.checkCancellation();
        }
        if (m_control.slowWrite)
            std::this_thread::sleep_for(300us);
        if (frame.position.index == m_control.failWrite)
            return an::Status::failure(an::ErrorCode::ExternalFailure);
        for (const auto byte : frame.frame.bytes())
            if (byte != static_cast<std::byte>(frame.position.index & 255u))
                return an::Status::failure(an::ErrorCode::InvalidData);
        m_control.written.push_back(frame.position.index);
        return an::SequenceWriteResult{m_control.written.size(), m_control.written.size() * kBytes};
    }
    an::Result<an::SequenceWriteResult> finish(const an::ExecutionContext&) override
    {
        ++m_control.finishes;
        if (m_control.failFinish)
            return an::Status::failure(an::ErrorCode::ExternalFailure);
        return an::SequenceWriteResult{m_control.written.size(), m_control.written.size() * kBytes};
    }
    an::Status close() override
    {
        ++m_control.sinkCloses;
        return an::Status::success();
    }

private:
    Control& m_control;
};
class Processor final : public an::FrameStreamProcessor
{
public:
    bool slow = false;
    bool fail = false;
    bool throwFailure = false;
    bool alterPosition = false;
    bool returnBorrow = false;
    Control* waitForReader = nullptr;
    std::vector<std::uint64_t> processed;
    an::Result<an::SequenceFrame> process(const an::EngineContext&, const an::ExecutionContext& execution,
                                          an::SequenceFrame frame) override
    {
        processed.push_back(frame.position.index);
        if (waitForReader)
        {
            const auto deadline = std::chrono::steady_clock::now() + 5s;
            while (!waitForReader->reading && std::chrono::steady_clock::now() < deadline)
                std::this_thread::yield();
        }
        if (fail)
            return an::Status::failure(an::ErrorCode::Unsupported);
        if (throwFailure)
            throw std::runtime_error("broken processor");
        if (slow)
            std::this_thread::sleep_for(400us);
        std::atomic<unsigned> items = 0u;
        const auto status = an::parallelFor(execution, 32u, {65536u, 1u}, [&](an::TaskRange range) {
            items += static_cast<unsigned>(range.end - range.begin);
            return an::Status::success();
        });
        if (status.failed())
            return status;
        if (items != 32u)
            return an::Status::failure(an::ErrorCode::Internal);
        if (alterPosition)
            ++frame.position.index;
        if (returnBorrow)
        {
            auto buffer = an::SampleBuffer::borrowConst(frame.frame.bytes());
            auto raw = an::RawFrame::create(*frame.frame.descriptor(), *frame.frame.layout(), std::move(*buffer));
            frame.frame = std::move(*raw);
        }
        return frame;
    }
};
an::FrameStreamOptions options()
{
    return {.readAheadDepth = 2u,
            .writeBehindDepth = 2u,
            .maxFrameBytes = kBytes,
            .readAheadBytes = 2u * kBytes,
            .writeBehindBytes = 2u * kBytes};
}
struct Harness
{
    Control control;
    an::EngineContext engine;
    an::ExecutionContext execution;
    an::FrameSource source;
    an::FrameSink sink;
    explicit Harness(std::uint32_t threads = 3u, const an::ExecutionOptions& policy = {})
    {
        engine = std::move(*an::EngineContext::create({.maxConcurrency = threads}));
        execution = std::move(*an::ExecutionContext::create(engine, policy));
        source =
            std::move(*an::FrameSource::create(engine, sequence(control.count), std::make_unique<Source>(control)));
        sink = std::move(*an::FrameSink::create(engine, sequence(control.count), std::make_unique<Sink>(control)));
    }
};

TEST(FrameStreamTest, PreservesEveryByteAndOrderWithBoundedSlowProducerAndConsumer)
{
    for (unsigned mode = 0u; mode < 4u; ++mode)
    {
        Harness h;
        h.control.slowRead = mode == 0u;
        h.control.slowWrite = mode == 1u;
        h.control.external = mode == 3u;
        Processor processor;
        processor.slow = mode == 2u;
        an::FrameStreamCounters counters;
        const auto before = h.engine.memoryResource()->counters().liveBytes;
        auto status = an::streamFrames(h.engine, h.execution, h.source, &h.sink, &processor, options(), counters);
        ASSERT_TRUE(status.ok()) << status.message();
        EXPECT_EQ(h.control.finishes, 1u);
        EXPECT_EQ(h.control.written, processor.processed);
        ASSERT_EQ(h.control.written.size(), h.control.count);
        for (std::uint64_t i = 0u; i < h.control.count; ++i)
            EXPECT_EQ(h.control.written[i], i);
        EXPECT_EQ(counters.framesRead, h.control.count);
        EXPECT_EQ(counters.framesProcessed, h.control.count);
        EXPECT_EQ(counters.framesWritten, h.control.count);
        EXPECT_EQ(counters.bytesRead, h.control.count * kBytes);
        EXPECT_LE(counters.readAhead.peakFrames, 2u);
        EXPECT_LE(counters.writeBehind.peakFrames, 2u);
        EXPECT_LE(counters.readAhead.peakReservedBytes, 2u * kBytes);
        EXPECT_LE(counters.writeBehind.peakReservedBytes, 2u * kBytes);
        EXPECT_EQ(h.engine.memoryResource()->counters().liveBytes, before);
    }
}

TEST(FrameStreamTest, InlineAndDisabledStagesMatchAndSingleThreadCannotDeadlock)
{
    for (const unsigned threads : {1u, 2u, 3u})
        for (unsigned mode = 0u; mode < 4u; ++mode)
        {
            Harness h(threads);
            auto request = options();
            request.readAheadDepth = mode & 1u;
            request.writeBehindDepth = (mode >> 1u) & 1u;
            an::FrameStreamCounters counters;
            ASSERT_TRUE(an::streamFrames(h.engine, h.execution, h.source, &h.sink, nullptr, request, counters).ok());
            EXPECT_EQ(counters.framesWritten, h.control.count);
            if (threads == 1u)
            {
                EXPECT_EQ(counters.readAhead.peakFrames, 0u);
                EXPECT_EQ(counters.writeBehind.peakFrames, 0u);
            }
        }
}

TEST(FrameStreamTest, BoundedBatchesResumeWithoutReadingPastTheRequestedEnd)
{
    Harness h;
    auto request = options();
    request.frameLimit = 7u;
    request.finishSink = false;
    an::FrameStreamCounters counters;
    ASSERT_TRUE(an::streamFrames(h.engine, h.execution, h.source, &h.sink, nullptr, request, counters).ok());
    EXPECT_EQ(h.source.nextIndex(), 7u);
    EXPECT_EQ(h.control.reads, 7u);
    ASSERT_TRUE(an::streamFrames(h.engine, h.execution, h.source, &h.sink, nullptr, request, counters).ok());
    EXPECT_EQ(h.source.nextIndex(), 14u);
    request.frameLimit = 0u;
    ASSERT_TRUE(an::streamFrames(h.engine, h.execution, h.source, &h.sink, nullptr, request, counters).ok());
    EXPECT_EQ(h.control.reads, 14u);
    EXPECT_EQ(counters.framesRead, 0u);
    request.frameLimit = 999u;
    request.finishSink = true;
    ASSERT_TRUE(an::streamFrames(h.engine, h.execution, h.source, &h.sink, nullptr, request, counters).ok());
    EXPECT_EQ(h.control.finishes, 1u);
    EXPECT_EQ(h.control.written.size(), 40u);
}

TEST(FrameStreamTest, DecoderWriterAndDelayedFinishFailuresAbortAndReleaseBothQueues)
{
    for (unsigned fault = 0u; fault < 4u; ++fault)
    {
        Harness h;
        if (fault < 2u)
        {
            h.control.failRead = 3u;
            h.control.throwRead = fault == 1u;
        }
        if (fault == 2u)
            h.control.failWrite = 3u;
        h.control.failFinish = fault == 3u;
        an::FrameStreamCounters counters;
        auto status = an::streamFrames(h.engine, h.execution, h.source, &h.sink, nullptr, options(), counters);
        EXPECT_TRUE(status.failed());
        EXPECT_EQ(h.source.state(), an::SequenceState::Closed);
        EXPECT_EQ(h.sink.state(), an::SequenceState::Closed);
        EXPECT_EQ(h.control.sourceCloses, 1u);
        EXPECT_EQ(h.control.sinkCloses, 1u);
        EXPECT_EQ(h.engine.memoryResource()->counters().liveBytes, 0u);
        ASSERT_NE(status.detail(), nullptr);
        EXPECT_EQ(status.detail()->context[0].key, an::ContextKey::FrameIndex);
        EXPECT_EQ(status.detail()->context[0].count, fault == 3u ? 40u : 3u);
        EXPECT_EQ(status.detail()->context[1].key, an::ContextKey::StreamStage);
    }
}

TEST(FrameStreamTest, ProcessingFailureCancelsAnInflightReadAndPreservesOriginalFailure)
{
    Harness h;
    h.control.waitForStop = true;
    Processor processor;
    processor.waitForReader = &h.control;
    processor.fail = true;
    an::FrameStreamCounters counters;
    auto status = an::streamFrames(h.engine, h.execution, h.source, &h.sink, &processor, options(), counters);
    EXPECT_EQ(status.code(), an::ErrorCode::Unsupported);
    EXPECT_TRUE(h.control.stopped);
    EXPECT_EQ(h.engine.memoryResource()->counters().liveBytes, 0u);
    EXPECT_EQ(h.control.finishes, 0u);
}

TEST(FrameStreamTest, RejectsInvalidProcessorOutputAndTranslatesExceptions)
{
    for (unsigned fault = 0u; fault < 3u; ++fault)
    {
        Harness h;
        Processor processor;
        processor.throwFailure = fault == 0u;
        processor.alterPosition = fault == 1u;
        processor.returnBorrow = fault == 2u;
        an::FrameStreamCounters counters;
        auto status = an::streamFrames(h.engine, h.execution, h.source, &h.sink, &processor, options(), counters);
        EXPECT_TRUE(status.failed());
        EXPECT_EQ(h.control.finishes, 0u);
        EXPECT_EQ(h.engine.memoryResource()->counters().liveBytes, 0u);
    }
}

TEST(FrameStreamTest, CancellationWakesAnEmptyQueueAndJoinsTheDecoder)
{
    std::stop_source stop;
    Harness h(3u, {.stopToken = stop.get_token()});
    h.control.waitForStop = true;
    an::FrameStreamCounters counters;
    std::jthread canceller([&] {
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (!h.control.reading && std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        stop.request_stop();
    });
    auto status = an::streamFrames(h.engine, h.execution, h.source, &h.sink, nullptr, options(), counters);
    EXPECT_EQ(status.code(), an::ErrorCode::Cancelled);
    EXPECT_TRUE(h.control.stopped);
    EXPECT_EQ(h.control.finishes, 0u);
    EXPECT_EQ(h.engine.memoryResource()->counters().liveBytes, 0u);
}

TEST(FrameStreamTest, DeadlineAndPreflightRefusalsLeaveUnusedSessionsUntouched)
{
    Harness h(3u, {.deadline = std::chrono::steady_clock::now() - 1ms});
    an::FrameStreamCounters counters;
    EXPECT_EQ(an::streamFrames(h.engine, h.execution, h.source, &h.sink, nullptr, options(), counters).code(),
              an::ErrorCode::DeadlineExceeded);
    EXPECT_EQ(h.control.reads, 0u);
    EXPECT_EQ(h.source.state(), an::SequenceState::Open);
    auto request = options();
    request.readAheadBytes = 1u;
    EXPECT_EQ(an::streamFrames(h.engine, h.execution, h.source, &h.sink, nullptr, request, counters).code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(h.source.state(), an::SequenceState::Open);
}

TEST(FrameStreamTest, StopAndDeadlineWakeFullQueuesAndJoinTheWriter)
{
    for (const bool deadline : {false, true})
    {
        std::stop_source stop;
        an::ExecutionOptions policy{.stopToken = stop.get_token()};
        if (deadline)
            policy.deadline = std::chrono::steady_clock::now() + 100ms;
        Harness h(3u, policy);
        h.control.blockWrite = true;
        an::FrameStreamCounters counters;
        std::jthread canceller([&] {
            const auto end = std::chrono::steady_clock::now() + 5s;
            while ((!h.control.writing || h.control.reads < 5u) && std::chrono::steady_clock::now() < end)
                std::this_thread::yield();
            if (!deadline)
                stop.request_stop();
        });
        const auto status = an::streamFrames(h.engine, h.execution, h.source, &h.sink, nullptr, options(), counters);
        EXPECT_EQ(status.code(), deadline ? an::ErrorCode::DeadlineExceeded : an::ErrorCode::Cancelled);
        EXPECT_EQ(counters.writeBehind.peakFrames, 2u);
        EXPECT_GT(counters.writeBehind.waits, 0u);
        EXPECT_EQ(h.control.finishes, 0u);
        EXPECT_EQ(h.control.sinkCloses, 1u);
        EXPECT_EQ(h.engine.memoryResource()->counters().liveBytes, 0u);
    }
}

TEST(FrameStreamTest, FrameReservationRefusalReleasesDecodedStorage)
{
    Harness h;
    auto request = options();
    request.maxFrameBytes = 128u;
    an::FrameStreamCounters counters;
    EXPECT_EQ(an::streamFrames(h.engine, h.execution, h.source, &h.sink, nullptr, request, counters).code(),
              an::ErrorCode::MemoryBudgetExceeded);
    EXPECT_EQ(h.engine.memoryResource()->counters().liveBytes, 0u);
}

TEST(FrameStreamTest, IndependentContextsCanStreamConcurrently)
{
    auto run = [] {
        Harness h(2u);
        Processor processor;
        an::FrameStreamCounters counters;
        return an::streamFrames(h.engine, h.execution, h.source, &h.sink, &processor, options(), counters).ok() &&
               counters.framesWritten == 40u;
    };
    auto first = std::async(std::launch::async, run);
    auto second = std::async(std::launch::async, run);
    EXPECT_TRUE(first.get());
    EXPECT_TRUE(second.get());
}
} // namespace
