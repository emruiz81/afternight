// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/IO/FrameCache.h>

#include <AfterNightEngine/IO/SequenceError.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include "../HeapFailure.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace
{
/// What a counting source was asked to do.
struct SourceLog
{
    std::vector<std::uint64_t> reads;
    std::uint64_t seeks = 0u;
    std::optional<std::uint64_t> failAt;
};

/// A source whose frame `i` is filled with `i + 1` and which records every read it is asked for.
class CountingBackend final : public an::FrameSourceBackend
{
public:
    CountingBackend(std::shared_ptr<SourceLog> log, an::RawFrameDescriptor descriptor, an::ImageLayout layout)
        : m_log(std::move(log)), m_descriptor(descriptor), m_layout(layout)
    {
    }

    an::Result<an::SequenceReadResult> read(const an::EngineContext& engine, const an::ExecutionContext&,
                                            std::uint64_t index) override
    {
        m_log->reads.push_back(index);
        if (m_log->failAt && *m_log->failAt == index)
            return an::sequenceFailure(an::ErrorCode::ExternalFailure, an::SequenceFailure::DecoderFailure);
        auto buffer = an::SampleBuffer::createOwned(engine, m_layout, an::StorageInitialization::Uninitialized);
        if (!buffer.hasValue())
            return buffer.status();
        auto frame = an::RawFrame::create(m_descriptor, m_layout, std::move(*buffer));
        if (!frame.hasValue())
            return frame.status();
        std::ranges::fill(*frame->mutableBytes(), static_cast<std::byte>(index + 1u));
        return an::SequenceReadResult{an::SequenceFrame{std::move(*frame), {}, {index, {}, {}}}};
    }

    an::Result<an::SequencePosition> seek(const an::ExecutionContext&, const an::SequenceSeekRequest& request) override
    {
        ++m_log->seeks;
        return an::SequencePosition{request.index, {}, {}};
    }

    an::Status close() override
    {
        return an::Status::success();
    }

private:
    std::shared_ptr<SourceLog> m_log;
    an::RawFrameDescriptor m_descriptor;
    an::ImageLayout m_layout;
};

/// The first sample byte of a frame, or nothing for the empty frame a failed read leaves.
std::optional<std::byte> firstByte(const an::SequenceFrame& frame)
{
    const auto bytes = frame.frame.bytes();
    if (bytes.empty())
        return std::nullopt;
    return bytes.front();
}

class FrameCacheTest : public testing::Test
{
protected:
    void SetUp() override
    {
        engine = std::move(*an::EngineContext::create({.maxConcurrency = 2u}));
        execution = std::move(*an::ExecutionContext::create(engine, {.maxConcurrency = 1u}));
    }

    void TearDown() override
    {
        execution = {};
        EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
        EXPECT_TRUE(engine.shutdown().ok());
    }

    /// A counting source of frames of `bytes` bytes each.
    an::FrameSource countingSource(std::uint64_t frameCount, std::uint32_t bytes = 8u, bool seekable = true,
                                   bool counted = true)
    {
        const auto extent = *an::ImageExtent::create(bytes, 1u, 1u, engine.limits());
        const auto descriptor = *an::RawFrameDescriptor::create({extent,
                                                                 {1u, an::RawSampleClass::UnsignedInteger},
                                                                 an::ByteOrder::Native,
                                                                 an::ChannelSchema::Mono,
                                                                 an::ColorSpace::Unspecified,
                                                                 an::NumericDomain::RawSensorCode,
                                                                 {},
                                                                 an::CoordinateOrigin::TopLeft,
                                                                 1.0});
        const auto layout = *an::ImageLayout::create({extent, 1u, 1u, 1u, 1u}, engine.limits());
        const an::SequenceDescriptor sequence{.format = an::SequenceFormat::Dummy,
                                              .fixedFrame = descriptor,
                                              .frameCount =
                                                  counted ? std::optional<std::uint64_t>(frameCount) : std::nullopt,
                                              .seekByIndex = seekable};
        auto source =
            an::FrameSource::create(engine, sequence, std::make_unique<CountingBackend>(log, descriptor, layout));
        if (!source.hasValue())
        {
            ADD_FAILURE() << source.status().message();
            return {};
        }
        return std::move(*source);
    }

    an::Result<an::CachedFrameSource> cached(std::uint64_t frameCount, const an::FrameCacheOptions& options,
                                             std::uint32_t bytes = 8u)
    {
        return an::createCachedFrameSource(engine, execution, countingSource(frameCount, bytes), options);
    }

    /// The last index the source was asked to read, or nothing before its first read.
    std::optional<std::uint64_t> lastRead() const
    {
        if (log->reads.empty())
            return std::nullopt;
        return log->reads.back();
    }

    /// Reads the next frame and returns it, or fails the test and returns an empty one.
    an::SequenceFrame readNext(an::FrameSource& source)
    {
        auto read = source.read(engine, execution);
        if (!read.hasValue() || !read->frame)
        {
            ADD_FAILURE() << (read.hasValue() ? std::string("end of the sequence") : read.status().message());
            return {};
        }
        return std::move(*read->frame);
    }

    /// Seeks to frame `index`, reads it and returns it, or fails the test and returns an empty one.
    an::SequenceFrame readAt(an::FrameSource& source, std::uint64_t index)
    {
        auto positioned = source.seek(execution, {.index = index});
        if (!positioned.hasValue())
        {
            ADD_FAILURE() << positioned.status().message();
            return {};
        }
        return readNext(source);
    }

    std::shared_ptr<SourceLog> log = std::make_shared<SourceLog>();
    an::EngineContext engine;
    an::ExecutionContext execution;
};

TEST_F(FrameCacheTest, HeapExhaustionDuringConstructionReturnsAllocationFailure)
{
    if (!an::test::HeapFailure::kAvailable)
        GTEST_SKIP() << "requires the shared MSVC Debug CRT allocation hook";
    std::size_t points = 0u;
    for (; points != 3u; ++points)
    {
        auto source = countingSource(4u);
        bool refused = false;
        an::ErrorCode code = an::ErrorCode::Ok;
        {
            const an::test::HeapFailure failure(points);
            auto result = an::createCachedFrameSource(engine, execution, std::move(source), {});
            refused = failure.refused() != 0u;
            code = result.code();
        }
        EXPECT_TRUE(refused) << points;
        EXPECT_EQ(code, an::ErrorCode::AllocationFailed) << points;
    }
}

TEST_F(FrameCacheTest, ARevisitedFrameIsAnsweredFromTheCacheAndSharedRatherThanCopied)
{
    auto created = cached(6u, {.maxFrames = 3u});
    ASSERT_TRUE(created.hasValue()) << created.status().message();
    auto& [source, control] = *created;
    const an::SequenceFrame first = readAt(source, 1u);
    const an::SequenceFrame again = readAt(source, 1u);
    EXPECT_EQ(log->reads, (std::vector<std::uint64_t>{1u}));
    EXPECT_EQ(again.frame.bytes().data(), first.frame.bytes().data());
    EXPECT_EQ(firstByte(again), std::byte{2});
    EXPECT_EQ(again.position.index, 1u);
    EXPECT_EQ(again.frame.buffer().ownership(), an::SampleBufferOwnership::DecoderOwned);
    const an::FrameCacheCounters counters = control.counters();
    EXPECT_EQ(counters.hits, 1u);
    EXPECT_EQ(counters.misses, 1u);
    EXPECT_EQ(counters.frames, 1u);
    EXPECT_EQ(counters.bytes, 8u);
}

TEST_F(FrameCacheTest, TheLeastRecentlyUsedFrameIsTheFirstToGo)
{
    auto created = cached(8u, {.maxFrames = 2u});
    ASSERT_TRUE(created.hasValue()) << created.status().message();
    auto& [source, control] = *created;
    for (const std::uint64_t index : {0u, 1u, 0u, 2u, 1u, 0u})
        EXPECT_EQ(firstByte(readAt(source, index)), static_cast<std::byte>(index + 1u)) << index;
    // 0 and 1 kept; 0 used again; 2 evicts 1; 1 evicts 0; 0 is read again and evicts 2.
    EXPECT_EQ(log->reads, (std::vector<std::uint64_t>{0u, 1u, 2u, 1u, 0u}));
    const an::FrameCacheCounters counters = control.counters();
    EXPECT_EQ(counters.hits, 1u);
    EXPECT_EQ(counters.misses, 5u);
    EXPECT_EQ(counters.evictions, 3u);
    EXPECT_EQ(counters.frames, 2u);
}

TEST_F(FrameCacheTest, AByteBoundKeepsWhatFitsAndHandsOutWhatDoesNot)
{
    auto created = cached(8u, {.maxFrames = 10u, .maxBytes = 64u}, 32u);
    ASSERT_TRUE(created.hasValue()) << created.status().message();
    auto& [source, control] = *created;
    for (const std::uint64_t index : {0u, 1u, 2u})
        (void)readAt(source, index);
    EXPECT_EQ(control.counters().frames, 2u);
    EXPECT_EQ(control.counters().bytes, 64u);
    control.resize(10u, 16u);
    EXPECT_EQ(control.counters().frames, 0u);
    const an::SequenceFrame large = readAt(source, 3u);
    EXPECT_EQ(firstByte(large), std::byte{4});
    EXPECT_EQ(control.counters().frames, 0u) << "a frame larger than the byte bound is never kept";
    const an::FrameCacheOptions options = control.options();
    EXPECT_EQ(options.maxBytes, 16u);
    EXPECT_EQ(options.maxFrames, 10u);
}

TEST_F(FrameCacheTest, ClearingAndShrinkingDropFramesButNotTheOnesACallerHolds)
{
    auto created = cached(8u, {.maxFrames = 4u});
    ASSERT_TRUE(created.hasValue()) << created.status().message();
    auto& [source, control] = *created;
    const an::SequenceFrame held = readAt(source, 0u);
    (void)readAt(source, 1u);
    (void)readAt(source, 2u);
    control.resize(1u, 0u);
    EXPECT_EQ(control.counters().frames, 1u);
    EXPECT_EQ(control.counters().evictions, 2u);
    control.clear();
    EXPECT_EQ(control.counters().frames, 0u);
    EXPECT_EQ(control.counters().bytes, 0u);
    EXPECT_EQ(control.counters().evictions, 2u) << "clearing is not eviction";
    EXPECT_EQ(firstByte(held), std::byte{1});
    (void)readAt(source, 0u);
    EXPECT_EQ(log->reads, (std::vector<std::uint64_t>{0u, 1u, 2u, 0u}));
    control.resize(0u, 0u);
    (void)readAt(source, 0u);
    EXPECT_EQ(control.counters().frames, 0u) << "a cache bounded to no frame keeps none";
}

TEST_F(FrameCacheTest, AMissReadsAheadWhereTheSourceStandsAndStopsAtTheEndOrAKeptFrame)
{
    auto created = cached(10u, {.maxFrames = 10u, .readAhead = 2u});
    ASSERT_TRUE(created.hasValue()) << created.status().message();
    auto& [source, control] = *created;
    EXPECT_EQ(firstByte(readNext(source)), std::byte{1});
    EXPECT_EQ(log->reads, (std::vector<std::uint64_t>{0u, 1u, 2u}));
    EXPECT_EQ(firstByte(readNext(source)), std::byte{2});
    EXPECT_EQ(firstByte(readNext(source)), std::byte{3});
    EXPECT_EQ(firstByte(readNext(source)), std::byte{4});
    EXPECT_EQ(log->reads, (std::vector<std::uint64_t>{0u, 1u, 2u, 3u, 4u, 5u}));
    EXPECT_EQ(log->seeks, 0u) << "reading ahead continues where the source already stands";
    (void)readAt(source, 9u);
    EXPECT_EQ(lastRead(), 9u) << "nothing is read past the end";
    control.clear();
    (void)readAt(source, 5u);
    (void)readAt(source, 3u);
    ASSERT_GE(log->reads.size(), 5u);
    EXPECT_EQ(std::vector<std::uint64_t>(log->reads.end() - 5, log->reads.end()),
              (std::vector<std::uint64_t>{5u, 6u, 7u, 3u, 4u}))
        << "reading ahead stops at a frame the cache holds";
    EXPECT_EQ(control.counters().framesReadAhead, 7u);
    control.setReadAhead(0u);
    control.clear();
    (void)readAt(source, 0u);
    EXPECT_EQ(lastRead(), 0u);
    EXPECT_EQ(control.options().readAhead, 0u);
}

TEST_F(FrameCacheTest, ReadAheadFillsFreeCapacityWithoutEvictingOrDecodingFramesItCannotKeep)
{
    {
        auto created = cached(10u, {.maxFrames = 2u, .readAhead = 4u});
        ASSERT_TRUE(created.hasValue()) << created.status().message();
        auto& [source, control] = *created;
        EXPECT_EQ(firstByte(readNext(source)), std::byte{1});
        EXPECT_EQ(log->reads, (std::vector<std::uint64_t>{0u, 1u}));
        EXPECT_EQ(control.counters().frames, 2u);
        EXPECT_EQ(control.counters().framesReadAhead, 1u);
        EXPECT_EQ(firstByte(readNext(source)), std::byte{2});
        EXPECT_EQ(log->reads, (std::vector<std::uint64_t>{0u, 1u}));
    }

    log->reads.clear();
    auto created = cached(10u, {.maxFrames = 10u, .maxBytes = 4u, .readAhead = 4u});
    ASSERT_TRUE(created.hasValue()) << created.status().message();
    auto& [source, control] = *created;
    EXPECT_EQ(firstByte(readNext(source)), std::byte{1});
    EXPECT_EQ(log->reads, (std::vector<std::uint64_t>{0u}));
    EXPECT_EQ(control.counters().frames, 0u);
    EXPECT_EQ(control.counters().framesReadAhead, 0u);
}

TEST_F(FrameCacheTest, AFailedReadAheadFailsTheReadThatAskedForIt)
{
    log->failAt = 2u;
    auto created = cached(10u, {.maxFrames = 10u, .readAhead = 3u});
    ASSERT_TRUE(created.hasValue()) << created.status().message();
    auto& [source, control] = *created;
    auto read = source.read(engine, execution);
    ASSERT_FALSE(read.hasValue());
    EXPECT_EQ(source.state(), an::SequenceState::Failed);
    EXPECT_EQ(log->reads, (std::vector<std::uint64_t>{0u, 1u, 2u}));
    EXPECT_TRUE(source.close().ok());
}

TEST_F(FrameCacheTest, OnlyAnOpenIndexSeekableSourceOfStatedLengthIsCached)
{
    an::FrameSource closed;
    auto refused = an::createCachedFrameSource(engine, execution, std::move(closed), {});
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidState);

    auto unseekable = an::createCachedFrameSource(engine, execution, countingSource(4u, 8u, false), {});
    ASSERT_FALSE(unseekable.hasValue());
    EXPECT_EQ(unseekable.status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::sequenceFailureReason(unseekable.status()), an::SequenceFailure::UnsupportedSeek);

    auto unbounded = an::createCachedFrameSource(engine, execution, countingSource(4u, 8u, true, false), {});
    ASSERT_FALSE(unbounded.hasValue());
    EXPECT_EQ(an::sequenceFailureReason(unbounded.status()), an::SequenceFailure::InvalidDescriptor);

    auto other = an::EngineContext::create({.maxConcurrency = 1u});
    ASSERT_TRUE(other.hasValue());
    {
        auto otherExecution = an::ExecutionContext::create(*other, {.maxConcurrency = 1u});
        ASSERT_TRUE(otherExecution.hasValue());
        auto foreign = an::createCachedFrameSource(*other, *otherExecution, countingSource(4u), {});
        ASSERT_FALSE(foreign.hasValue());
        EXPECT_EQ(foreign.status().code(), an::ErrorCode::IncompatibleArguments);
    }
    EXPECT_FALSE(an::FrameCacheControl().valid());
    EXPECT_EQ(an::FrameCacheControl().counters().hits, 0u);
}

TEST_F(FrameCacheTest, TheControlMayBeUsedFromAnotherThreadWhileTheSourceIsRead)
{
    auto created = cached(64u, {.maxFrames = 8u, .readAhead = 1u});
    ASSERT_TRUE(created.hasValue()) << created.status().message();
    auto& [source, control] = *created;
    std::atomic<bool> reading{true};
    std::thread controller([&, control = control]() mutable {
        std::uint64_t turn = 0u;
        while (reading.load())
        {
            switch (turn++ % 4u)
            {
            case 0u:
                control.clear();
                break;
            case 1u:
                control.resize(1u + turn % 16u, turn % 3u == 0u ? 64u : 0u);
                break;
            case 2u:
                control.setReadAhead(static_cast<std::uint32_t>(turn % 3u));
                break;
            default:
                (void)control.counters();
                break;
            }
        }
    });
    std::mt19937_64 random(1234u);
    const std::uint64_t reads = 4000u;
    for (std::uint64_t i = 0u; i < reads; ++i)
    {
        const std::uint64_t index = random() % 64u;
        const an::SequenceFrame frame = readAt(source, index);
        ASSERT_EQ(firstByte(frame), static_cast<std::byte>(index + 1u));
    }
    reading.store(false);
    controller.join();
    const an::FrameCacheCounters counters = control.counters();
    EXPECT_EQ(counters.hits + counters.misses, reads);
}

} // namespace
