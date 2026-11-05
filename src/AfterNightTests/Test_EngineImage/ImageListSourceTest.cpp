// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/IO/ImageListSource.h>
#include <AfterNightEngine/IO/DummySource.h>

#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/DumpCodec.h>
#include <AfterNightEngine/IO/OutputTransaction.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>
#include <AfterNightEngine/Runtime/TraceSink.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stop_token>

namespace
{
class ImageListSourceTest : public testing::Test
{
protected:
    void SetUp() override
    {
        directory = std::filesystem::temp_directory_path() /
                    ("an-list-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ASSERT_TRUE(std::filesystem::create_directory(directory));
        const std::array codecs{*an::createDumpCodec()};
        an::EngineContextOptions options;
        options.maxConcurrency = 4u;
        options.codecRegistry = *an::CodecRegistry::create(codecs);
        engine = std::move(*an::EngineContext::create(options));
        execution = std::move(*an::ExecutionContext::create(engine, {.maxConcurrency = 1u}));
        for (std::size_t i = 0; i < paths.size(); ++i)
        {
            const auto text = (directory / (std::filesystem::path(u8"星-") += std::to_string(i) + ".dump")).u8string();
            paths[i] = {reinterpret_cast<const char*>(text.data()), text.size()};
            requests[i] = {paths[i], an::CodecFormat::Dump};
            auto raw = frame(static_cast<std::byte>(i + 1u));
            ASSERT_TRUE(an::writeImage(engine, execution,
                                       {.destinationPath = paths[i], .format = an::CodecFormat::Dump}, raw, {})
                            .hasValue());
        }
    }
    void TearDown() override
    {
        execution = {};
        EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
        EXPECT_TRUE(engine.shutdown().ok());
        std::error_code error;
        const auto root = std::filesystem::weakly_canonical(directory, error);
        ASSERT_FALSE(error);
        ASSERT_EQ(root.parent_path(), std::filesystem::weakly_canonical(std::filesystem::temp_directory_path()));
        std::filesystem::remove_all(root, error);
        EXPECT_FALSE(error) << error.message();
        EXPECT_FALSE(std::filesystem::exists(root));
    }
    an::RawFrame frame(std::byte value, std::uint32_t width = 4u)
    {
        auto extent = *an::ImageExtent::create(width, 2u, 1u, engine.limits());
        auto descriptor = *an::RawFrameDescriptor::create({extent,
                                                           {2u, an::RawSampleClass::UnsignedInteger},
                                                           an::ByteOrder::Big,
                                                           an::ChannelSchema::Mono,
                                                           an::ColorSpace::Unspecified,
                                                           an::NumericDomain::RawSensorCode,
                                                           {},
                                                           an::CoordinateOrigin::BottomLeft,
                                                           1.25});
        auto layout = *an::ImageLayout::create({extent, 2u, 1u, 1u, 1u}, engine.limits());
        auto buffer = an::SampleBuffer::createOwned(engine, layout, an::StorageInitialization::Uninitialized);
        auto result = an::RawFrame::create(descriptor, layout, std::move(*buffer));
        std::ranges::fill(*result->mutableBytes(), value);
        return std::move(*result);
    }
    std::filesystem::path nativePath(std::size_t index) const
    {
        return std::filesystem::path(
            std::u8string_view(reinterpret_cast<const char8_t*>(paths[index].data()), paths[index].size()));
    }
    std::uint64_t setupBytes()
    {
        const auto before = engine.memoryResource()->counters().liveBytes;
        auto source = an::createImageListSource(engine, execution, requests, {});
        EXPECT_TRUE(source);
        return engine.memoryResource()->counters().liveBytes - before;
    }
    std::filesystem::path directory;
    an::EngineContext engine;
    an::ExecutionContext execution;
    std::array<std::string, 3u> paths;
    std::array<an::CodecReadRequest, 3u> requests;
};

TEST_F(ImageListSourceTest, LazyReadsUnicodeFilesInOrderAndSeeksFromEof)
{
    auto source = an::createImageListSource(engine, execution, requests, {});
    ASSERT_TRUE(source.hasValue());
    EXPECT_EQ(source->descriptor()->frameCount, 3u);
    EXPECT_FALSE(source->descriptor()->fixedFrame);
    for (std::uint64_t i = 0u; i < 3u; ++i)
    {
        auto read = source->read(engine, execution);
        ASSERT_TRUE(read.hasValue());
        ASSERT_TRUE(read->frame);
        EXPECT_EQ(read->frame->position.index, i);
        EXPECT_EQ(read->frame->frame.bytes().front(), static_cast<std::byte>(i + 1u));
        EXPECT_EQ(read->frame->frame.descriptor()->byteOrder(), an::ByteOrder::Big);
        EXPECT_FALSE(read->frame->metadata.outputIntent());
    }
    EXPECT_FALSE(source->read(engine, execution)->frame);
    ASSERT_TRUE(source->seek(execution, {.index = 1u}).hasValue());
    EXPECT_EQ(source->read(engine, execution)->frame->frame.bytes().front(), std::byte{2});
}

TEST_F(ImageListSourceTest, EagerRetainsImmutableFramesAfterFilesAndSourceAreGone)
{
    const auto administration = setupBytes();
    auto source = an::createImageListSource(engine, execution, requests, {an::ImageListLoadMode::Eager, 48u});
    ASSERT_TRUE(source.hasValue());
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, administration + 48u);
    for (std::size_t i = 0; i < paths.size(); ++i)
        ASSERT_TRUE(std::filesystem::remove(nativePath(i)));
    auto read = source->read(engine, execution);
    ASSERT_TRUE(read.hasValue());
    EXPECT_FALSE(read->frame->frame.mutableBytes());
    ASSERT_TRUE(source->close().ok());
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 16u);
    EXPECT_EQ(read->frame->frame.bytes().front(), std::byte{1});
}

TEST_F(ImageListSourceTest, EagerBudgetRefusalIsAtomic)
{
    auto source = an::createImageListSource(engine, execution, requests, {an::ImageListLoadMode::Eager, 47u});
    ASSERT_FALSE(source.hasValue());
    EXPECT_EQ(source.status().code(), an::ErrorCode::MemoryBudgetExceeded);
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
}

TEST_F(ImageListSourceTest, CacheEvictsLeastRecentlyUsedAndPreservesReturnedLifetime)
{
    auto source = an::createImageListSource(engine, execution, requests, {an::ImageListLoadMode::Cached, 32u});
    ASSERT_TRUE(source.hasValue());
    auto held = source->read(engine, execution);
    ASSERT_TRUE(held.hasValue());
    ASSERT_TRUE(source->read(engine, execution).hasValue());
    ASSERT_TRUE(source->seek(execution, {.index = 0u}).hasValue());
    ASSERT_TRUE(source->read(engine, execution).hasValue());
    ASSERT_TRUE(source->seek(execution, {.index = 2u}).hasValue());
    ASSERT_TRUE(source->read(engine, execution).hasValue());
    ASSERT_TRUE(std::filesystem::remove(nativePath(0)));
    ASSERT_TRUE(source->seek(execution, {.index = 0u}).hasValue());
    ASSERT_TRUE(source->read(engine, execution).hasValue());
    ASSERT_TRUE(std::filesystem::remove(nativePath(1)));
    ASSERT_TRUE(source->seek(execution, {.index = 1u}).hasValue());
    auto missing = source->read(engine, execution);
    EXPECT_FALSE(missing.hasValue());
    EXPECT_EQ(source->state(), an::SequenceState::Failed);
    EXPECT_EQ(source->nextIndex(), 1u);
    ASSERT_TRUE(source->close().ok());
    EXPECT_EQ(held->frame->frame.bytes().front(), std::byte{1});
}

TEST_F(ImageListSourceTest, LazyDefersMissingFileFailureAndEagerReturnsItAtCreation)
{
    ASSERT_TRUE(std::filesystem::remove(nativePath(1)));
    auto eager = an::createImageListSource(engine, execution, requests, {an::ImageListLoadMode::Eager, 48u});
    EXPECT_FALSE(eager.hasValue());
    auto lazy = an::createImageListSource(engine, execution, requests, {});
    ASSERT_TRUE(lazy.hasValue());
    ASSERT_TRUE(lazy->read(engine, execution).hasValue());
    EXPECT_FALSE(lazy->read(engine, execution).hasValue());
    EXPECT_EQ(lazy->nextIndex(), 1u);
}

TEST_F(ImageListSourceTest, DummyZerosPaddingAndRetainsIndependentFramesThroughClose)
{
    auto raw = frame(std::byte{99});
    const auto descriptor = *raw.descriptor();
    const auto layout =
        *an::ImageLayout::createWithStrides({descriptor.extent(), 2u, 1u, 1u, 1u}, 16u, 32u, engine.limits());
    auto source = an::createDummySource(engine, execution, {descriptor, layout, 2u});
    ASSERT_TRUE(source.hasValue());
    auto first = source->read(engine, execution);
    ASSERT_TRUE(first.hasValue());
    EXPECT_TRUE(std::ranges::all_of(first->frame->frame.bytes(), [](std::byte b) { return b == std::byte{}; }));
    std::ranges::fill(*first->frame->frame.mutableBytes(), std::byte{42});
    ASSERT_TRUE(source->seek(execution, {}).hasValue());
    auto second = source->read(engine, execution);
    ASSERT_TRUE(second.hasValue());
    EXPECT_TRUE(std::ranges::all_of(second->frame->frame.bytes(), [](std::byte b) { return b == std::byte{}; }));
    ASSERT_TRUE(source->close().ok());
    EXPECT_EQ(first->frame->frame.bytes().front(), std::byte{42});
}
TEST_F(ImageListSourceTest, EmptySourcesAndMaximumDummyCountHaveCheckedEof)
{
    for (auto mode : {an::ImageListLoadMode::Lazy, an::ImageListLoadMode::Eager, an::ImageListLoadMode::Cached})
    {
        auto source =
            an::createImageListSource(engine, execution, {}, {mode, mode == an::ImageListLoadMode::Lazy ? 0u : 1u});
        ASSERT_TRUE(source);
        EXPECT_FALSE(source->read(engine, execution)->frame);
        EXPECT_TRUE(source->seek(execution, {}).hasValue());
        EXPECT_FALSE(source->seek(execution, {.index = 1u}).hasValue());
    }
    auto raw = frame(std::byte{});
    for (auto count : {std::uint64_t{0u}, (std::numeric_limits<std::uint64_t>::max)()})
    {
        auto source = an::createDummySource(engine, execution, {*raw.descriptor(), *raw.layout(), count});
        ASSERT_TRUE(source);
        EXPECT_TRUE(source->seek(execution, {.index = count}).hasValue());
        EXPECT_FALSE(source->read(engine, execution)->frame);
        if (count != 0u)
        {
            ASSERT_TRUE(source->seek(execution, {.index = count - 1u}).hasValue());
            EXPECT_EQ(source->read(engine, execution)->frame->position.index, count - 1u);
            EXPECT_FALSE(source->read(engine, execution)->frame);
        }
    }
}

TEST_F(ImageListSourceTest, RejectsIncoherentPoliciesAndMalformedPathsBeforeIo)
{
    for (const auto& options : {an::ImageListSourceOptions{an::ImageListLoadMode::Lazy, 1u},
                                an::ImageListSourceOptions{an::ImageListLoadMode::Eager, 0u},
                                an::ImageListSourceOptions{an::ImageListLoadMode::Cached, 0u},
                                an::ImageListSourceOptions{static_cast<an::ImageListLoadMode>(255u), 1u}})
        EXPECT_EQ(an::createImageListSource(engine, execution, requests, options).status().code(),
                  an::ErrorCode::InvalidArgument);
    for (const auto& path :
         {std::string{}, std::string("bad\0.dump", 9u), std::string("\xff.dump"), std::string(131073u, 'a')})
    {
        const std::array entries{an::CodecReadRequest{path}};
        EXPECT_EQ(an::createImageListSource(engine, execution, entries, {}).status().code(),
                  an::ErrorCode::InvalidArgument);
    }
    auto invalid = requests;
    invalid[0].format = static_cast<an::CodecFormat>(255u);
    EXPECT_EQ(an::createImageListSource(engine, execution, invalid, {}).status().code(),
              an::ErrorCode::InvalidArgument);
}

TEST_F(ImageListSourceTest, SnapshotsRequestPathsAndKeepsDuplicateEntries)
{
    std::string name = paths[1];
    std::array entries{an::CodecReadRequest{name, an::CodecFormat::Dump},
                       an::CodecReadRequest{name, an::CodecFormat::Dump}};
    auto source = an::createImageListSource(engine, execution, entries, {});
    ASSERT_TRUE(source);
    name.assign("not-the-original.dump");
    entries = {};
    for (std::uint64_t i = 0u; i < 2u; ++i)
    {
        auto read = source->read(engine, execution);
        ASSERT_TRUE(read);
        EXPECT_EQ(read->frame->frame.bytes().front(), std::byte{2});
        EXPECT_EQ(read->frame->position.index, i);
    }
}

TEST_F(ImageListSourceTest, OversizedCacheFramesBypassWithoutRetainingPayload)
{
    auto source = an::createImageListSource(engine, execution, requests, {an::ImageListLoadMode::Cached, 15u});
    ASSERT_TRUE(source);
    const auto administration = engine.memoryResource()->counters().liveBytes;
    {
        auto read = source->read(engine, execution);
        ASSERT_TRUE(read);
        EXPECT_EQ(read->frame->frame.buffer().ownership(), an::SampleBufferOwnership::Owned);
    }
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, administration);
    ASSERT_TRUE(std::filesystem::remove(nativePath(0u)));
    ASSERT_TRUE(source->seek(execution, {}).hasValue());
    EXPECT_FALSE(source->read(engine, execution).hasValue());
}

TEST_F(ImageListSourceTest, AnOversizedBypassDoesNotEvictFramesAlreadyRetained)
{
    auto oversized = frame(std::byte{9}, 9u);
    ASSERT_TRUE(an::writeImage(engine, execution, {.destinationPath = paths[2], .format = an::CodecFormat::Dump},
                               oversized, {}));
    auto source = an::createImageListSource(engine, execution, requests, {an::ImageListLoadMode::Cached, 32u});
    ASSERT_TRUE(source);
    EXPECT_EQ(source->read(engine, execution)->frame->frame.bytes().front(), std::byte{1});
    EXPECT_EQ(source->read(engine, execution)->frame->frame.bytes().front(), std::byte{2});
    EXPECT_EQ(source->read(engine, execution)->frame->frame.bytes().front(), std::byte{9});

    ASSERT_TRUE(std::filesystem::remove(nativePath(0u)));
    ASSERT_TRUE(std::filesystem::remove(nativePath(1u)));
    ASSERT_TRUE(source->seek(execution, {.index = 0u}));
    EXPECT_EQ(source->read(engine, execution)->frame->frame.bytes().front(), std::byte{1});
    ASSERT_TRUE(source->seek(execution, {.index = 1u}));
    EXPECT_EQ(source->read(engine, execution)->frame->frame.bytes().front(), std::byte{2});
}

TEST_F(ImageListSourceTest, LazyRereadsChangedFilesWhileCacheHitsRetainSnapshot)
{
    auto lazy = an::createImageListSource(engine, execution, requests, {});
    auto cached = an::createImageListSource(engine, execution, requests, {an::ImageListLoadMode::Cached, 16u});
    ASSERT_TRUE(lazy);
    ASSERT_TRUE(cached);
    EXPECT_EQ(lazy->read(engine, execution)->frame->frame.bytes().front(), std::byte{1});
    EXPECT_EQ(cached->read(engine, execution)->frame->frame.bytes().front(), std::byte{1});
    auto replacement = frame(std::byte{9});
    ASSERT_TRUE(an::writeImage(engine, execution, {.destinationPath = paths[0], .format = an::CodecFormat::Dump},
                               replacement, {}));
    ASSERT_TRUE(lazy->seek(execution, {}).hasValue());
    ASSERT_TRUE(cached->seek(execution, {}).hasValue());
    EXPECT_EQ(lazy->read(engine, execution)->frame->frame.bytes().front(), std::byte{9});
    EXPECT_EQ(cached->read(engine, execution)->frame->frame.bytes().front(), std::byte{1});
}

TEST_F(ImageListSourceTest, VariableFramesAreExplicitAndFixedMismatchNamesItsIndex)
{
    auto replacement = frame(std::byte{8}, 7u);
    ASSERT_TRUE(an::writeImage(engine, execution, {.destinationPath = paths[1], .format = an::CodecFormat::Dump},
                               replacement, {}));
    auto first = frame(std::byte{});
    for (auto mode : {an::ImageListLoadMode::Lazy, an::ImageListLoadMode::Eager, an::ImageListLoadMode::Cached})
    {
        const std::uint64_t budget = mode == an::ImageListLoadMode::Lazy ? 0u : 128u;
        auto variable = an::createImageListSource(engine, execution, requests, {mode, budget});
        ASSERT_TRUE(variable);
        ASSERT_TRUE(variable->seek(execution, {.index = 1u}).hasValue());
        EXPECT_EQ(variable->read(engine, execution)->frame->frame.descriptor()->extent().width(), 7u);
        auto fixed = an::createImageListSource(engine, execution, requests, {mode, budget, *first.descriptor()});
        if (mode == an::ImageListLoadMode::Eager)
            EXPECT_FALSE(fixed.hasValue());
        else
        {
            ASSERT_TRUE(fixed);
            ASSERT_TRUE(fixed->seek(execution, {.index = 1u}).hasValue());
            auto failed = fixed->read(engine, execution);
            EXPECT_EQ(failed.status().code(), an::ErrorCode::IncompatibleArguments);
            EXPECT_EQ(fixed->state(), an::SequenceState::Failed);
        }
    }
}

an::Status entryCause(an::Status current)
{
    while (current.failed())
    {
        const auto* field = current.findContext(an::ContextKey::Parameter);
        if (field && field->text == "frameIndex")
            return current;
        current = current.cause();
    }
    return {};
}

TEST_F(ImageListSourceTest, TruncatedAndMissingFilesRetainTypedCausePathAndIndex)
{
    for (bool truncate : {false, true})
    {
        if (truncate)
            std::filesystem::resize_file(nativePath(2u), 3u);
        else
            ASSERT_TRUE(std::filesystem::remove(nativePath(1u)));
        auto source = an::createImageListSource(engine, execution, requests, {});
        ASSERT_TRUE(source);
        const auto index = truncate ? 2u : 1u;
        ASSERT_TRUE(source->seek(execution, {.index = index}).hasValue());
        auto read = source->read(engine, execution);
        ASSERT_FALSE(read);
        auto cause = entryCause(read.status());
        ASSERT_TRUE(cause.failed());
        ASSERT_NE(cause.findContext(an::ContextKey::Requested), nullptr);
        EXPECT_EQ(cause.findContext(an::ContextKey::Requested)->count, index);
        EXPECT_EQ(cause.findContext(an::ContextKey::Path)->text, paths[index]);
        EXPECT_TRUE(cause.cause().failed());
        EXPECT_EQ(source->nextIndex(), index);
    }
}

class StopOnCacheEvent final : public an::TraceSink
{
public:
    std::stop_source stop;
    std::string event = "cache-bytes";
    void onTrace(const an::TraceEvent& value) noexcept override
    {
        if (std::string_view(value.category) == "image-list" && std::string_view(value.name) == event &&
            (event != "cache-bytes" || value.value > 0.0))
            stop.request_stop();
    }
};

TEST_F(ImageListSourceTest, CancellationDuringPreloadReleasesEveryRetainedFrame)
{
    StopOnCacheEvent sink;
    auto cancelled = an::ExecutionContext::create(
        engine, {.stopToken = sink.stop.get_token(), .traceSink = &sink, .maxConcurrency = 1u});
    ASSERT_TRUE(cancelled);
    auto source = an::createImageListSource(engine, *cancelled, requests, {an::ImageListLoadMode::Eager, 48u});
    EXPECT_EQ(source.status().code(), an::ErrorCode::Cancelled);
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
}

TEST_F(ImageListSourceTest, LateStopOnCacheHitFailsWithoutAdvancingPublicCursor)
{
    auto source = an::createImageListSource(engine, execution, requests, {an::ImageListLoadMode::Cached, 16u});
    ASSERT_TRUE(source);
    ASSERT_TRUE(source->read(engine, execution).hasValue());
    ASSERT_TRUE(source->seek(execution, {}).hasValue());
    StopOnCacheEvent sink;
    sink.event = "cache-hit";
    auto cancelled = an::ExecutionContext::create(
        engine, {.stopToken = sink.stop.get_token(), .traceSink = &sink, .maxConcurrency = 1u});
    ASSERT_TRUE(cancelled);
    EXPECT_EQ(source->read(engine, *cancelled).status().code(), an::ErrorCode::Cancelled);
    EXPECT_EQ(source->state(), an::SequenceState::Failed);
    EXPECT_EQ(source->nextIndex(), 0u);
}

TEST_F(ImageListSourceTest, PreCancelledAndExpiredFactoriesAllocateNoPixels)
{
    auto raw = frame(std::byte{});
    std::stop_source stop;
    stop.request_stop();
    auto cancelled = an::ExecutionContext::create(engine, {.stopToken = stop.get_token(), .maxConcurrency = 1u});
    auto expired = an::ExecutionContext::create(
        engine, {.deadline = std::chrono::steady_clock::now() - std::chrono::seconds(1), .maxConcurrency = 1u});
    ASSERT_TRUE(cancelled);
    ASSERT_TRUE(expired);
    const auto before = engine.memoryResource()->counters().totalBytes;
    EXPECT_EQ(an::createImageListSource(engine, *cancelled, requests, {}).status().code(), an::ErrorCode::Cancelled);
    EXPECT_EQ(an::createDummySource(engine, *cancelled, {*raw.descriptor(), *raw.layout(), 1u}).status().code(),
              an::ErrorCode::Cancelled);
    EXPECT_EQ(an::createImageListSource(engine, *expired, requests, {}).status().code(),
              an::ErrorCode::DeadlineExceeded);
    EXPECT_EQ(an::createDummySource(engine, *expired, {*raw.descriptor(), *raw.layout(), 1u}).status().code(),
              an::ErrorCode::DeadlineExceeded);
    EXPECT_EQ(engine.memoryResource()->counters().totalBytes, before);
}

TEST_F(ImageListSourceTest, DummyRefusesMismatchedLayoutsBeforeAllocation)
{
    auto small = frame(std::byte{});
    auto large = frame(std::byte{}, 7u);
    const auto before = engine.memoryResource()->counters().totalBytes;
    EXPECT_EQ(an::createDummySource(engine, execution, {*small.descriptor(), *large.layout(), 1u}).status().code(),
              an::ErrorCode::IncompatibleArguments);
    const auto layout = *an::ImageLayout::create({small.descriptor()->extent(), 1u, 1u, 1u, 1u}, engine.limits());
    EXPECT_EQ(an::createDummySource(engine, execution, {*small.descriptor(), layout, 1u}).status().code(),
              an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(engine.memoryResource()->counters().totalBytes, before);
}

TEST_F(ImageListSourceTest, RelativeSelectionsStayAnchoredToFactoryDirectory)
{
    struct RestoreDirectory
    {
        std::filesystem::path original = std::filesystem::current_path();
        ~RestoreDirectory()
        {
            std::error_code error;
            std::filesystem::current_path(original, error);
            EXPECT_FALSE(error);
        }
    } restore;
    std::filesystem::current_path(directory);
    const auto text = nativePath(0u).filename().u8string();
    const std::string relative(reinterpret_cast<const char*>(text.data()), text.size());
    const std::array entries{an::CodecReadRequest{relative, an::CodecFormat::Dump}};
    auto source = an::createImageListSource(engine, execution, entries, {});
    ASSERT_TRUE(source);
    std::filesystem::current_path(restore.original);
    auto read = source->read(engine, execution);
    ASSERT_TRUE(read);
    EXPECT_EQ(read->frame->frame.bytes().front(), std::byte{1});
}
} // namespace
