// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/IO/DumpCodec.h>
#include <AfterNightEngine/IO/DumpReader.h>
#include <AfterNightEngine/IO/DumpHistory.h>

#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/ImageCodec.h>
#include <AfterNightEngine/IO/OutputTransaction.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>
#include <AfterNightEngine/Runtime/TraceSink.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <new>
#include <stop_token>
#include <thread>
#include <atomic>
#include <future>
#include <vector>

namespace
{
TEST(DumpCodecContractTest, RegistersExplicitFullAndRegionAccess)
{
    auto codec = an::createDumpCodec();
    ASSERT_TRUE(codec.hasValue());
    EXPECT_EQ((*codec)->capabilities().format, an::CodecFormat::Dump);
    EXPECT_TRUE((*codec)->capabilities().regionDecode);
    const std::array adapters{*codec};
    EXPECT_TRUE(an::CodecRegistry::create(adapters).hasValue());
}
} // namespace

namespace
{
class DumpCodecTest : public testing::Test
{
protected:
    void SetUp() override
    {
        directory = std::filesystem::temp_directory_path() /
                    ("an-dump-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ASSERT_TRUE(std::filesystem::create_directory(directory));
        auto result = makeEngine();
        ASSERT_TRUE(result.hasValue());
        engine = std::move(*result);
    }
    void TearDown() override
    {
        std::error_code error;
        std::filesystem::remove_all(directory, error);
        EXPECT_FALSE(error) << error.message();
    }
    an::Result<an::EngineContext> makeEngine(std::uint64_t budget = 0u)
    {
        auto codec = an::createDumpCodec();
        if (!codec.hasValue())
            return codec.status();
        const std::array adapters{*codec};
        auto registry = an::CodecRegistry::create(adapters);
        if (!registry.hasValue())
            return registry.status();
        an::EngineContextOptions options;
        options.codecRegistry = *registry;
        // Stated rather than the host's: a read is asked for up to four workers, which an engine sized
        // to a two-core machine refuses.
        options.maxConcurrency = 4u;
        if (budget != 0u)
            options.limits.memoryBudgetBytes = budget;
        return an::EngineContext::create(options);
    }
    std::string path() const
    {
        const auto text = (directory / std::filesystem::path(u8"muestra-星.dump")).u8string();
        return {reinterpret_cast<const char*>(text.data()), text.size()};
    }
    std::filesystem::path nativePath() const
    {
        return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(path().c_str())));
    }
    an::RawFrame frame(an::RawSampleFormat format = {4u, an::RawSampleClass::FloatingPoint},
                       std::uint32_t channels = 1u, std::uint32_t samplesPerPixel = 1u, std::uint64_t alignment = 1u,
                       an::CfaDescriptor cfa = {}, an::ByteOrder order = an::ByteOrder::Native,
                       std::uint32_t width = 7u, std::uint32_t height = 5u)
    {
        const auto extent = *an::ImageExtent::create(width, height, channels, engine.limits());
        const auto descriptor = *an::RawFrameDescriptor::create(
            {extent, format, order, channels == 1u ? an::ChannelSchema::Mono : an::ChannelSchema::Named,
             an::ColorSpace::Unspecified, an::NumericDomain::RawSensorCode, cfa, an::CoordinateOrigin::BottomLeft,
             1.25});
        const auto layout = *an::ImageLayout::create(
            {extent, format.sizeBytes, samplesPerPixel, channels / samplesPerPixel, alignment}, engine.limits());
        auto buffer = an::SampleBuffer::createOwned(engine, layout, an::StorageInitialization::Zeroed);
        auto result = an::RawFrame::create(descriptor, layout, std::move(*buffer));
        auto bytes = *result->mutableBytes();
        for (std::uint32_t p = 0u; p < layout.planeCount(); ++p)
            for (std::uint32_t y = 0u; y < height; ++y)
                for (std::uint64_t x = 0u; x < std::uint64_t{width} * format.sizeBytes * samplesPerPixel; ++x)
                    bytes[p * layout.planeStrideBytes() + y * layout.rowStrideBytes() + x] =
                        static_cast<std::byte>((p * 73u + y * 17u + x) % 256u);
        return std::move(*result);
    }
    an::Result<an::OutputReport> save(const an::RawFrame& frame, const an::OutputIntent* intent = nullptr)
    {
        auto execution = an::ExecutionContext::create(engine, {});
        if (!execution.hasValue())
            return execution.status();
        const auto name = path();
        return an::writeImage(engine, *execution,
                              {.destinationPath = name, .format = an::CodecFormat::Dump, .intent = intent}, frame, {});
    }
    an::Result<an::CodecDecodeResult> load(const an::CodecRegion* region = nullptr)
    {
        auto execution = an::ExecutionContext::create(engine, {});
        if (!execution.hasValue())
            return execution.status();
        const auto name = path();
        return region ? an::decodeImageRegion(engine, *execution, {name, an::CodecFormat::Dump}, *region)
                      : an::decodeImage(engine, *execution, {name, an::CodecFormat::Dump});
    }
    std::vector<char> bytes() const
    {
        std::ifstream file(nativePath(), std::ios::binary);
        return {std::istreambuf_iterator<char>(file), {}};
    }
    void overwrite(const std::vector<char>& data)
    {
        std::ofstream file(nativePath(), std::ios::binary | std::ios::trunc);
        file.write(data.data(), data.size());
        ASSERT_TRUE(file.good());
    }
    std::filesystem::path directory;
    an::EngineContext engine;
};
TEST_F(DumpCodecTest, DiskHistoriesPreserveEverySampleUnderBothLayoutsAndReaderPolicies)
{
    for (unsigned packed : {1u, 3u})
    {
        const auto input = frame({2u, an::RawSampleClass::UnsignedInteger}, 3u, packed);
        ASSERT_TRUE(save(input));
        auto expectedRequest = input.descriptor()->toRequest();
        expectedRequest.byteOrder = an::nativeByteOrder();
        const auto expected = *an::RawFrameDescriptor::create(expectedRequest);
        auto execution = an::ExecutionContext::create(engine, {});
        const auto sourcePath = path();
        const std::array<std::string_view, 3u> paths{sourcePath, sourcePath, sourcePath};
        const an::CodecRegion region{1u, 1u, 5u, 3u};
        auto pool = an::DumpReaderPool::create(engine, 1u);
        for (const auto policy : {an::DumpHistoryReaderPolicy::Reopen, an::DumpHistoryReaderPolicy::Pooled})
            for (const auto layout : {an::DumpHistoryLayout::Automatic, an::DumpHistoryLayout::FrameMajor,
                                      an::DumpHistoryLayout::BlockedPixelMajor})
            {
                auto history =
                    an::DumpHistory::read(engine, *execution, paths, expected, region, {layout, policy, 7u, 4096u},
                                          policy == an::DumpHistoryReaderPolicy::Pooled ? &*pool : nullptr);
                ASSERT_TRUE(history) << history.status().message();
                EXPECT_EQ(history->frameCount(), 3u);
                EXPECT_EQ(history->samplesPerFrame(), 45u);
                EXPECT_EQ(history->byteCount(), 270u);
                for (std::uint64_t frameIndex = 0u; frameIndex < 3u; ++frameIndex)
                    for (std::uint64_t sample = 0u; sample < 45u; ++sample)
                    {
                        const auto channel = sample / 15u;
                        const auto y = static_cast<std::uint32_t>(sample % 15u / 5u) + region.y;
                        const auto x = sample % 5u + region.x;
                        const auto source =
                            input.row(static_cast<std::uint32_t>(channel / packed), y)
                                ->subspan(static_cast<std::size_t>((x * packed + channel % packed) * 2u), 2u);
                        const auto stored = history->sample(frameIndex, sample);
                        ASSERT_TRUE(stored);
                        EXPECT_EQ(std::memcmp(source.data(), stored->data(), 2u), 0);
                    }
                EXPECT_EQ(history->sample(3u, 0u).status().code(), an::ErrorCode::OutOfRange);
                EXPECT_EQ(history->sample(0u, 45u).status().code(), an::ErrorCode::OutOfRange);
            }
        EXPECT_EQ(pool->snapshot().handleOpens, 1u);
        auto refused = an::DumpHistory::read(engine, *execution, paths, expected, region, {.workingSetBytes = 359u});
        EXPECT_EQ(refused.status().code(), an::ErrorCode::MemoryBudgetExceeded);
    }
}

TEST_F(DumpCodecTest, PersistentReaderReusesHeaderAndCountsExactRegionTransfers)
{
    auto input = frame();
    ASSERT_TRUE(save(input));
    auto execution = an::ExecutionContext::create(engine, {});
    auto reader = an::DumpReader::open(engine, *execution, path());
    ASSERT_TRUE(reader) << reader.status().message();
    ASSERT_NE(reader->info(), nullptr);
    EXPECT_EQ(reader->info()->descriptor.extent().width(), 7u);
    const an::CodecRegion strip{0u, 1u, 7u, 2u};
    const an::CodecRegion rectangle{2u, 1u, 3u, 2u};
    for (const auto& region : {strip, rectangle})
    {
        auto decoded = reader->read(engine, *execution, &region);
        ASSERT_TRUE(decoded) << decoded.status().message();
        for (unsigned y = 0u; y < region.height; ++y)
        {
            auto expected = input.row(0u, y + region.y)->subspan(region.x * 4u, region.width * 4u);
            auto actual = *decoded->frame.row(0u, y);
            EXPECT_EQ(std::memcmp(expected.data(), actual.data(), actual.size()), 0);
        }
    }
    const auto counters = reader->snapshot();
    EXPECT_EQ(counters.handleOpens, 1u);
    EXPECT_EQ(counters.headerValidations, 1u);
    EXPECT_EQ(counters.positionedReads, 3u);
    EXPECT_EQ(counters.fullWidthStripReads, 1u);
    EXPECT_EQ(counters.rowReads, 2u);
    EXPECT_EQ(counters.payloadBytes, 80u);
    const an::CodecRegion outside{6u, 0u, 2u, 1u};
    EXPECT_EQ(reader->read(engine, *execution, &outside).status().code(), an::ErrorCode::OutOfRange);
}

TEST_F(DumpCodecTest, ReaderPoolBoundsHandlesReusesEvictsAndResetsDiagnostics)
{
    ASSERT_TRUE(save(frame()));
    const auto second = path() + ".second";
    std::filesystem::copy_file(nativePath(), std::filesystem::path(std::u8string(second.begin(), second.end())));
    auto execution = an::ExecutionContext::create(engine, {});
    auto pool = an::DumpReaderPool::create(engine, 1u);
    ASSERT_TRUE(pool);
    EXPECT_EQ(pool->maxOpenHandles(), 1u);
    for (unsigned i = 0u; i < 2u; ++i)
    {
        auto lease = pool->acquire(engine, *execution, path());
        ASSERT_TRUE(lease);
        ASSERT_NE(lease->reader(), nullptr);
        ASSERT_TRUE(lease->reader()->read(engine, *execution));
        EXPECT_EQ(pool->resetCounters().code(), an::ErrorCode::Busy);
    }
    {
        auto lease = pool->acquire(engine, *execution, second);
        ASSERT_TRUE(lease);
        ASSERT_TRUE(lease->reader()->read(engine, *execution));
    }
    auto counters = pool->snapshot();
    EXPECT_EQ(counters.handleOpens, 2u);
    EXPECT_EQ(counters.handleCloses, 1u);
    EXPECT_EQ(counters.poolHits, 1u);
    EXPECT_EQ(counters.poolMisses, 2u);
    EXPECT_EQ(counters.poolEvictions, 1u);
    EXPECT_EQ(counters.peakOpenHandles, 1u);
    EXPECT_EQ(counters.payloadBytes, 3u * 140u);
    ASSERT_TRUE(pool->resetCounters().ok());
    EXPECT_EQ(pool->snapshot().payloadBytes, 0u);
    EXPECT_EQ(pool->snapshot().currentOpenHandles, 1u);
    ASSERT_TRUE(pool->drain().ok());
    EXPECT_EQ(pool->snapshot().currentOpenHandles, 0u);
    EXPECT_EQ(pool->snapshot().handleCloses, 1u);
    EXPECT_EQ(pool->acquire(engine, *execution, path()).status().code(), an::ErrorCode::InvalidState);
}

TEST_F(DumpCodecTest, ReaderLeaseSurvivesPoolDestructionAndRetainsContextBudget)
{
    ASSERT_TRUE(save(frame()));
    auto execution = an::ExecutionContext::create(engine, {});
    const auto before = engine.memoryResource()->counters().liveBytes;
    an::DumpReaderLease lease;
    {
        auto pool = an::DumpReaderPool::create(engine, 1u);
        auto acquired = pool->acquire(engine, *execution, path());
        ASSERT_TRUE(acquired);
        lease = std::move(*acquired);
        EXPECT_GT(engine.memoryResource()->counters().liveBytes, before);
    }
    ASSERT_NE(lease.reader(), nullptr);
    ASSERT_TRUE(lease.reader()->read(engine, *execution));
    lease = {};
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, before);
}

TEST_F(DumpCodecTest, FullPoolWaitIsCancellableAndDrainWakesWaiters)
{
    ASSERT_TRUE(save(frame()));
    for (bool cancel : {false, true})
    {
        std::stop_source stop;
        auto execution = an::ExecutionContext::create(engine, {.stopToken = stop.get_token()});
        auto pool = an::DumpReaderPool::create(engine, 1u);
        auto first = pool->acquire(engine, *execution, path());
        ASSERT_TRUE(first);
        std::promise<void> started;
        auto waiting = std::async(std::launch::async, [&] {
            started.set_value();
            return pool->acquire(engine, *execution, path()).status();
        });
        started.get_future().wait();
        if (cancel)
            stop.request_stop();
        else
            ASSERT_TRUE(pool->drain().ok());
        EXPECT_EQ(waiting.get().code(), cancel ? an::ErrorCode::Cancelled : an::ErrorCode::InvalidState);
        EXPECT_LE(pool->snapshot().peakOpenHandles, 1u);
    }
}

TEST_F(DumpCodecTest, ReaderAndPoolRejectMalformedOrForeignInputsWithoutLeakingSlots)
{
    ASSERT_TRUE(save(frame()));
    auto execution = an::ExecutionContext::create(engine, {});
    auto pool = an::DumpReaderPool::create(engine, 1u);
    EXPECT_FALSE(pool->acquire(engine, *execution, "relative.dump"));
    EXPECT_FALSE(pool->acquire(engine, *execution, path() + ".missing"));
    EXPECT_EQ(pool->snapshot().currentOpenHandles, 0u);
    auto lease = pool->acquire(engine, *execution, path());
    ASSERT_TRUE(lease);
    auto foreign = makeEngine();
    EXPECT_EQ(lease->reader()->read(*foreign, *execution).status().code(), an::ErrorCode::IncompatibleArguments);
    lease = an::DumpReaderLease{};
    ASSERT_TRUE(pool->drain().ok());
    auto broken = bytes();
    broken.pop_back();
    overwrite(broken);
    EXPECT_FALSE(an::DumpReader::open(engine, *execution, path()));
}

TEST_F(DumpCodecTest, RoundTripsEveryRawRepresentationAndArrangementWithoutPadding)
{
    for (unsigned kind = 0u; kind < 4u; ++kind)
        for (unsigned width : {1u, 2u, 4u, 8u, 16u})
            for (unsigned packed : {1u, 2u, 4u})
            {
                SCOPED_TRACE(testing::Message() << kind << '/' << width << '/' << packed);
                auto input =
                    frame({static_cast<std::uint8_t>(width), static_cast<an::RawSampleClass>(kind)}, 4u, packed, 64u);
                ASSERT_TRUE(save(input).hasValue());
                auto output = load();
                ASSERT_TRUE(output.hasValue()) << output.status().message();
                const auto layout = *output->frame.layout();
                EXPECT_EQ(layout.samplesPerPixel(), packed);
                EXPECT_EQ(layout.rowStrideBytes(), 7u * width * packed);
                EXPECT_EQ(bytes().size(), 128u + 7u * 5u * 4u * width);
                EXPECT_EQ(output->frame.descriptor()->pixelAspectRatio(), 1.25);
                EXPECT_EQ(output->frame.descriptor()->coordinateOrigin(), an::CoordinateOrigin::BottomLeft);
                for (unsigned p = 0; p < layout.planeCount(); ++p)
                    for (unsigned y = 0; y < 5; ++y)
                    {
                        const auto a = *input.row(p, y);
                        const auto b = *output->frame.row(p, y);
                        EXPECT_EQ(std::memcmp(a.data(), b.data(), b.size()), 0);
                    }
            }
}
TEST_F(DumpCodecTest, ReservedAndDirectOutputsMatchAcrossTransferAndPaddingBoundaries)
{
    const auto input =
        frame({4u, an::RawSampleClass::FloatingPoint}, 1u, 1u, 64u, {}, an::ByteOrder::Native, 1031u, 1025u);
    auto execution = an::ExecutionContext::create(engine, {});
    ASSERT_TRUE(execution.hasValue());
    const auto destination = path();
    const auto direct =
        an::encodeImage(engine, *execution, {.pendingPath = destination, .format = an::CodecFormat::Dump}, input, {});
    ASSERT_TRUE(direct.hasValue());
    const auto expected = bytes();
    EXPECT_EQ(expected.size(), 128u + 1031u * 1025u * 4u);
    const auto written = an::writeImage(engine, *execution,
                                        {.destinationPath = destination,
                                         .format = an::CodecFormat::Dump,
                                         .durability = an::OutputDurability::CloseOnly},
                                        input, {});
    ASSERT_TRUE(written.hasValue()) << written.status().message();
    EXPECT_TRUE(written->replacedExisting);
    EXPECT_EQ(written->bytesWritten, direct->bytesWritten);
    EXPECT_EQ(bytes(), expected);
    const auto decoded = an::decodeImage(engine, *execution, {.path = destination, .format = an::CodecFormat::Dump});
    ASSERT_TRUE(decoded.hasValue());
    for (std::uint32_t row = 0u; row < 1025u; ++row)
    {
        const auto before = *input.row(0u, row);
        const auto after = *decoded->frame.row(0u, row);
        EXPECT_EQ(std::memcmp(before.data(), after.data(), after.size()), 0);
    }
}

TEST_F(DumpCodecTest, ADirectOutputThatCannotBeCreatedIsDestinationUnavailable)
{
    const auto input = frame();
    auto execution = an::ExecutionContext::create(engine, {});
    ASSERT_TRUE(execution.hasValue());
    const auto missing = directory / "missing" / "frame.dump";
    const auto text = missing.u8string();
    const std::string destination(reinterpret_cast<const char*>(text.data()), text.size());
    const auto written =
        an::encodeImage(engine, *execution, {.pendingPath = destination, .format = an::CodecFormat::Dump}, input, {});
    ASSERT_FALSE(written.hasValue());
    EXPECT_EQ(written.status().code(), an::ErrorCode::ExternalFailure);
    EXPECT_EQ(an::codecFailureReason(written.status()), an::CodecFailure::CallbackFailure);
    EXPECT_EQ(an::codecFailureReason(written.status().rootCause()), an::CodecFailure::DestinationUnavailable);
}

TEST_F(DumpCodecTest, HeaderHasFixedLittleEndianFieldsAndPreservesForeignSampleOrder)
{
    auto input = frame({8u, an::RawSampleClass::SignedInteger}, 1u, 1u, 1u, {}, an::ByteOrder::Big);
    ASSERT_TRUE(save(input).hasValue());
    auto data = bytes();
    EXPECT_EQ(std::string(data.data(), 8), "ANDUMP\r\n");
    EXPECT_EQ(static_cast<unsigned char>(data[8]), 1);
    EXPECT_EQ(static_cast<unsigned char>(data[12]), 128);
    EXPECT_EQ(static_cast<unsigned char>(data[16]), 7);
    EXPECT_EQ(static_cast<unsigned char>(data[20]), 5);
    EXPECT_EQ(static_cast<unsigned char>(data[28]), 8);
    auto output = load();
    ASSERT_TRUE(output.hasValue());
    EXPECT_EQ(output->frame.descriptor()->byteOrder(), an::ByteOrder::Big);
    EXPECT_EQ(std::memcmp(input.bytes().data(), output->frame.bytes().data(), input.bytes().size()), 0);
}
TEST_F(DumpCodecTest, RejectsEveryTruncationAndTrailingPayloadButPermitsHeaderOnlyProbe)
{
    auto input = frame();
    ASSERT_TRUE(save(input).hasValue());
    const auto original = bytes();
    for (std::size_t n = 0; n < original.size(); ++n)
    {
        overwrite({original.begin(), original.begin() + n});
        EXPECT_FALSE(load().hasValue()) << n;
    }
    auto extra = original;
    extra.push_back(0);
    overwrite(extra);
    EXPECT_FALSE(load().hasValue());
    overwrite({original.begin(), original.begin() + 128});
    auto execution = an::ExecutionContext::create(engine, {});
    const auto name = path();
    EXPECT_TRUE(
        an::probeImage(engine, *execution, {.path = name, .format = an::CodecFormat::Dump, .validatePayload = false})
            .hasValue());
    EXPECT_FALSE(an::probeImage(engine, *execution, {name, an::CodecFormat::Dump}).hasValue());
}
TEST_F(DumpCodecTest, RejectsCorruptFieldsBeforePixelAllocation)
{
    auto input = frame();
    ASSERT_TRUE(save(input).hasValue());
    const auto original = bytes();
    const auto before = engine.memoryResource()->counters().liveBytes;
    for (const std::size_t offset : {0u,  8u,  12u, 16u, 20u, 24u, 28u, 29u, 30u, 31u, 32u, 33u,  34u,
                                     35u, 36u, 37u, 38u, 47u, 48u, 52u, 56u, 64u, 72u, 80u, 116u, 127u})
    {
        auto bad = original;
        bad[offset] = static_cast<char>(255);
        overwrite(bad);
        EXPECT_FALSE(load().hasValue()) << offset;
        EXPECT_EQ(engine.memoryResource()->counters().liveBytes, before);
    }
}
TEST_F(DumpCodecTest, RegionsRetainEveryChannelAndRefuseOverflowAndEmptyBounds)
{
    for (unsigned packed : {1u, 2u, 4u})
    {
        auto input = frame({2u, an::RawSampleClass::UnsignedInteger}, 4u, packed);
        ASSERT_TRUE(save(input).hasValue());
        for (const auto area : {an::CodecRegion{1u, 2u, 3u, 2u}, an::CodecRegion{0u, 1u, 7u, 3u}})
        {
            auto result = load(&area);
            ASSERT_TRUE(result.hasValue());
            for (unsigned p = 0; p < 4u / packed; ++p)
                for (unsigned y = 0; y < area.height; ++y)
                {
                    const auto a = *input.row(p, y + area.y);
                    const auto b = *result->frame.row(p, y);
                    EXPECT_EQ(std::memcmp(a.data() + area.x * packed * 2u, b.data(), b.size()), 0);
                }
        }
    }
    for (const auto area : {an::CodecRegion{}, an::CodecRegion{7, 0, 1, 1}, an::CodecRegion{1, 0, 0xffffffffu, 1}})
        EXPECT_FALSE(load(&area).hasValue());
}
TEST_F(DumpCodecTest, PreservesMosaicPhaseAndShiftsItForRegions)
{
    constexpr an::CfaColorEntries table = {
        an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Red,   an::CfaColor::Green, an::CfaColor::Green,
        an::CfaColor::Blue,  an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Blue,  an::CfaColor::Green,
        an::CfaColor::Green, an::CfaColor::Red,   an::CfaColor::Blue,  an::CfaColor::Red,   an::CfaColor::Green,
        an::CfaColor::Red,   an::CfaColor::Blue,  an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Green,
        an::CfaColor::Blue,  an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Red,   an::CfaColor::Green,
        an::CfaColor::Green, an::CfaColor::Red,   an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Blue,
        an::CfaColor::Red,   an::CfaColor::Blue,  an::CfaColor::Green, an::CfaColor::Blue,  an::CfaColor::Red,
        an::CfaColor::Green};
    for (const auto cfa :
         {*an::CfaDescriptor::create(an::CfaPattern::Rggb, 1u, 0u), *an::CfaDescriptor::create(table, 5u, 4u)})
    {
        auto input = frame({2, an::RawSampleClass::UnsignedInteger}, 1u, 1u, 1u, cfa);
        ASSERT_TRUE(save(input).hasValue());
        auto full = load();
        ASSERT_TRUE(full.hasValue()) << full.status().message() << " / " << full.status().rootCause().message();
        EXPECT_EQ(full->frame.descriptor()->cfa(), cfa);
        const an::CodecRegion region{1, 2, 3, 2};
        auto cropped = load(&region);
        ASSERT_TRUE(cropped.hasValue());
        for (unsigned y = 0; y < 2; ++y)
            for (unsigned x = 0; x < 3; ++x)
                EXPECT_EQ(cropped->frame.descriptor()->cfa().colorAt(x, y), cfa.colorAt(x + 1u, y + 2u));
    }
}
TEST_F(DumpCodecTest, BudgetCancellationAndUnsupportedOutputPreserveDestination)
{
    auto input = frame();
    ASSERT_TRUE(save(input).hasValue());
    const auto original = bytes();
    auto small = makeEngine(64u);
    ASSERT_TRUE(small.hasValue());
    auto execution = an::ExecutionContext::create(*small, {});
    ASSERT_TRUE(execution.hasValue());
    const auto name = path();
    auto refused = an::decodeImage(*small, *execution, {name, an::CodecFormat::Dump});
    EXPECT_EQ(refused.status().code(), an::ErrorCode::MemoryBudgetExceeded);
    const an::CodecRegion region{0, 0, 2, 2};
    EXPECT_TRUE(an::decodeImageRegion(*small, *execution, {name, an::CodecFormat::Dump}, region).hasValue());
    {
        std::stop_source stop;
        auto cancelled = an::ExecutionContext::create(engine, {.stopToken = stop.get_token()});
        ASSERT_TRUE(cancelled.hasValue());
        stop.request_stop();
        EXPECT_EQ(an::decodeImage(engine, *cancelled, {name, an::CodecFormat::Dump}).status().code(),
                  an::ErrorCode::Cancelled);
        EXPECT_FALSE(
            an::writeImage(engine, *cancelled, {.destinationPath = name, .format = an::CodecFormat::Dump}, input, {})
                .hasValue());
    }
    auto intent = an::OutputIntent::create(
        {.sampleType = an::SampleType::Float32, .compression = an::OutputCompression::Deflate});
    ASSERT_TRUE(intent.hasValue());
    EXPECT_FALSE(save(input, &*intent).hasValue());
    EXPECT_EQ(bytes(), original);
    EXPECT_EQ(std::distance(std::filesystem::directory_iterator(directory), std::filesystem::directory_iterator{}), 1);
}
} // namespace

namespace
{
// A dump with the 40-byte header - a sample type, a channel schema and the geometry, and nothing else - is not this
// format's: the dump codec refuses it as unreadable, whatever representation it states, and reads nothing as a frame.
TEST_F(DumpCodecTest, AFortyByteHeaderDumpIsRefused)
{
    for (const auto format : {an::RawSampleFormat{1u, an::RawSampleClass::UnsignedInteger},
                              an::RawSampleFormat{1u, an::RawSampleClass::SignedInteger},
                              an::RawSampleFormat{2u, an::RawSampleClass::UnsignedInteger},
                              an::RawSampleFormat{2u, an::RawSampleClass::SignedInteger},
                              an::RawSampleFormat{4u, an::RawSampleClass::UnsignedInteger},
                              an::RawSampleFormat{4u, an::RawSampleClass::SignedInteger},
                              an::RawSampleFormat{4u, an::RawSampleClass::FloatingPoint},
                              an::RawSampleFormat{8u, an::RawSampleClass::FloatingPoint}})
    {
        auto input = frame(format);
        std::vector<char> header40(40u + input.bytes().size(), 0);
        const unsigned sampleCode = format.sampleClass == an::RawSampleClass::FloatingPoint
                                        ? (format.sizeBytes == 4u ? 7u : 8u)
                                        : (format.sizeBytes == 1u   ? 1u
                                           : format.sizeBytes == 2u ? 3u
                                                                    : 5u) +
                                              (format.sampleClass == an::RawSampleClass::UnsignedInteger ? 1u : 0u);
        header40[1] = static_cast<char>(sampleCode);
        header40[2] = 1;
        header40[8] = 7;
        header40[16] = 5;
        header40[24] = 1;
        for (unsigned i = 0; i < 8; ++i)
            header40[32u + i] = static_cast<char>(input.bytes().size() >> (8u * i));
        std::memcpy(header40.data() + 40u, input.bytes().data(), input.bytes().size());
        overwrite(header40);
        const auto refused = load();
        ASSERT_FALSE(refused.hasValue());
        EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidData) << refused.status().message();
    }
}
} // namespace

namespace
{
struct DumpTrace : an::TraceSink
{
    double reads = 0.0;
    double bytes = 0.0;
    double workers = 0.0;
    std::stop_source* stop = nullptr;
    void onTrace(const an::TraceEvent& event) noexcept override
    {
        if (std::string_view(event.name) == "payloadReadCalls")
            reads = event.value;
        if (std::string_view(event.name) == "payloadBytesRead")
            bytes = event.value;
        if (std::string_view(event.name) == "payloadReadWorkers")
            workers = event.value;
        if (stop)
            stop->request_stop();
    }
};

// Requests a stop when a block at least `threshold` bytes long is taken, which for a decode is the
// frame's own storage: everything after it is the payload's transfers.
class StoppingMemoryResource final : public an::MemoryResource
{
public:
    StoppingMemoryResource(std::stop_source& stop, std::uint64_t threshold) : m_stop(stop), m_threshold(threshold) {}
    an::Result<void*> allocate(std::uint64_t bytes, std::uint64_t alignment) override
    {
        if (bytes >= m_threshold)
            m_stop.request_stop();
        void* block =
            ::operator new(static_cast<std::size_t>(bytes), std::align_val_t{static_cast<std::size_t>(alignment)});
        const std::lock_guard held(m_guard);
        m_liveBytes += bytes;
        ++m_liveAllocations;
        return block;
    }
    void deallocate(void* pointer, std::uint64_t bytes, std::uint64_t alignment) noexcept override
    {
        ::operator delete(pointer, std::align_val_t{static_cast<std::size_t>(alignment)});
        const std::lock_guard held(m_guard);
        m_liveBytes -= bytes;
        --m_liveAllocations;
    }
    an::MemoryCounters counters() const noexcept override
    {
        const std::lock_guard held(m_guard);
        an::MemoryCounters counters;
        counters.liveBytes = m_liveBytes;
        counters.liveAllocations = m_liveAllocations;
        return counters;
    }

private:
    std::stop_source& m_stop;
    std::uint64_t m_threshold;
    mutable std::mutex m_guard;
    std::uint64_t m_liveBytes = 0u;
    std::uint64_t m_liveAllocations = 0u;
};

// A read of several 4 MiB transfers is made on as many of the operation's workers as it holds whole
// transfers, never more than the operation was admitted. Neither what it returns nor how many transfers
// it makes depends on how many workers made them: a mono frame of four strips, a full-width half of it
// in two, a narrow window of 1024 row reads that holds no whole transfer, three planes of one strip
// each, and three interleaved samples whose 12 KiB rows make four strips of unequal length.
TEST_F(DumpCodecTest, AReadRunsOneWorkerPerWholeTransferAndReturnsTheSameBytesAtEveryWidth)
{
    constexpr std::uint64_t kTransfer = 4u * 1024u * 1024u;
    struct Case
    {
        std::uint32_t side;
        std::uint32_t channels;
        std::uint32_t samplesPerPixel;
        std::vector<an::CodecRegion> areas;
    };
    const std::vector<Case> cases{
        {2048u, 1u, 1u, {{0u, 0u, 2048u, 2048u}, {0u, 512u, 2048u, 1024u}, {512u, 512u, 256u, 1024u}}},
        {1024u, 3u, 1u, {{0u, 0u, 1024u, 1024u}}},
        {1024u, 3u, 3u, {{0u, 0u, 1024u, 1024u}}}};
    for (const auto& item : cases)
    {
        auto input = frame({4u, an::RawSampleClass::FloatingPoint}, item.channels, item.samplesPerPixel, 1u, {},
                           an::ByteOrder::Native, item.side, item.side);
        ASSERT_TRUE(save(input).hasValue());
        const auto name = path();
        const std::uint32_t planes = item.channels / item.samplesPerPixel;
        for (const auto& area : item.areas)
        {
            const std::uint64_t rowBytes = std::uint64_t{area.width} * item.samplesPerPixel * 4u;
            const std::uint64_t payload = rowBytes * area.height * planes;
            const bool fullWidth = area.width == item.side;
            const std::uint64_t rowsPerRead = fullWidth ? (std::max)(std::uint64_t{1}, kTransfer / rowBytes) : 1u;
            const std::uint64_t transfers = planes * ((area.height + rowsPerRead - 1u) / rowsPerRead);
            for (const std::uint32_t width : {1u, 2u, 4u})
            {
                DumpTrace trace;
                auto execution = an::ExecutionContext::create(engine, {.traceSink = &trace, .maxConcurrency = width});
                ASSERT_TRUE(execution.hasValue());
                const bool whole = fullWidth && area.height == item.side;
                auto result = whole ? an::decodeImage(engine, *execution, {name, an::CodecFormat::Dump})
                                    : an::decodeImageRegion(engine, *execution, {name, an::CodecFormat::Dump}, area);
                ASSERT_TRUE(result.hasValue()) << result.status().message();
                for (std::uint32_t p = 0u; p < planes; ++p)
                    for (std::uint32_t y = 0u; y < area.height; ++y)
                    {
                        const auto expected =
                            input.row(p, y + area.y)
                                ->subspan(area.x * item.samplesPerPixel * 4u, static_cast<std::size_t>(rowBytes));
                        const auto actual = *result->frame.row(p, y);
                        ASSERT_EQ(actual.size(), expected.size());
                        ASSERT_EQ(std::memcmp(expected.data(), actual.data(), actual.size()), 0)
                            << "plane " << p << " row " << y << " width " << width;
                    }
                const std::uint64_t wholeTransfers = (std::max)(std::uint64_t{1}, payload / kTransfer);
                const std::uint64_t expectedWorkers =
                    (std::min)({std::uint64_t{execution->admittedConcurrency()}, wholeTransfers, transfers});
                EXPECT_EQ(trace.reads, static_cast<double>(transfers)) << width;
                EXPECT_EQ(trace.bytes, static_cast<double>(payload)) << width;
                EXPECT_EQ(trace.workers, static_cast<double>(expectedWorkers))
                    << "side " << item.side << " channels " << item.channels << " width " << width;
            }
        }
    }
}

// A stop that arrives once a parallel read has taken its frame's storage reaches every worker before
// it transfers: the read answers Cancelled and gives the storage back.
TEST_F(DumpCodecTest, AStopDuringAParallelReadReturnsNoFrameAndGivesItsStorageBack)
{
    ASSERT_TRUE(
        save(frame({4u, an::RawSampleClass::FloatingPoint}, 1u, 1u, 1u, {}, an::ByteOrder::Native, 2048u, 2048u))
            .hasValue());
    std::stop_source stop;
    StoppingMemoryResource memory(stop, 16u * 1024u * 1024u);
    auto codec = an::createDumpCodec();
    const std::array adapters{*codec};
    an::EngineContextOptions options;
    options.codecRegistry = *an::CodecRegistry::create(adapters);
    options.memoryResource = &memory;
    options.maxConcurrency = 4u;
    auto stopping = an::EngineContext::create(options);
    ASSERT_TRUE(stopping.hasValue());
    {
        DumpTrace trace;
        auto execution = an::ExecutionContext::create(*stopping, {.stopToken = stop.get_token(), .traceSink = &trace});
        ASSERT_TRUE(execution.hasValue());
        const auto before = memory.counters().liveBytes;
        const auto name = path();
        EXPECT_EQ(an::decodeImage(*stopping, *execution, {name, an::CodecFormat::Dump}).status().code(),
                  an::ErrorCode::Cancelled);
        EXPECT_TRUE(stop.stop_requested());
        EXPECT_EQ(trace.bytes, 0.0);
        EXPECT_EQ(memory.counters().liveBytes, before);
    }
}
TEST_F(DumpCodecTest, CoalescesStripsAndReturnsNoFrameWhenCancellationArrivesAtCompletion)
{
    auto input = frame({4, an::RawSampleClass::FloatingPoint}, 1, 1, 1, {}, an::ByteOrder::Native, 1024, 1024);
    ASSERT_TRUE(save(input).hasValue());
    const auto name = path();
    {
        DumpTrace trace;
        auto execution = an::ExecutionContext::create(engine, {.traceSink = &trace});
        ASSERT_TRUE(execution.hasValue());
        auto result = an::decodeImageRegion(engine, *execution, {name, an::CodecFormat::Dump}, {0, 128, 1024, 512});
        ASSERT_TRUE(result.hasValue());
        EXPECT_EQ(trace.reads, 1.0);
        EXPECT_EQ(trace.bytes, 2097152.0);
        result = an::decodeImageRegion(engine, *execution, {name, an::CodecFormat::Dump}, {1, 128, 10, 512});
        ASSERT_TRUE(result.hasValue());
        EXPECT_EQ(trace.reads, 512.0);
        EXPECT_EQ(trace.bytes, 20480.0);
    }
    const auto before = engine.memoryResource()->counters().liveBytes;
    std::stop_source stop;
    DumpTrace trace;
    trace.stop = &stop;
    auto execution = an::ExecutionContext::create(engine, {.stopToken = stop.get_token(), .traceSink = &trace});
    ASSERT_TRUE(execution.hasValue());
    auto result = an::decodeImage(engine, *execution, {name, an::CodecFormat::Dump});
    EXPECT_EQ(result.status().code(), an::ErrorCode::Cancelled);
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, before);
}
TEST_F(DumpCodecTest, ConcurrentReadersUseIndependentFilePositions)
{
    auto input = frame();
    ASSERT_TRUE(save(input).hasValue());
    const auto name = path();
    std::atomic<unsigned> failures{0};
    auto read = [&](unsigned row) {
        auto execution = an::ExecutionContext::create(engine, {.maxConcurrency = 1u});
        if (!execution.hasValue())
        {
            ++failures;
            return;
        }
        for (unsigned n = 0; n < 40; ++n)
        {
            auto result = an::decodeImageRegion(engine, *execution, {name, an::CodecFormat::Dump}, {0u, row, 7u, 1u});
            if (!result.hasValue() || std::memcmp(result->frame.bytes().data(), input.row(0u, row)->data(), 28u) != 0)
                ++failures;
        }
    };
    std::thread first(read, 1u);
    std::thread second(read, 3u);
    first.join();
    second.join();
    EXPECT_EQ(failures.load(), 0u);
}
TEST_F(DumpCodecTest, MutatedHeadersRemainBounded)
{
    auto input = frame();
    ASSERT_TRUE(save(input).hasValue());
    const auto original = bytes();
    std::uint32_t random = 2871u;
    const auto before = engine.memoryResource()->counters().liveBytes;
    for (unsigned n = 0; n < 512; ++n)
    {
        auto mutated = original;
        random = random * 1664525u + 1013904223u;
        const auto offset = random % 128u;
        random = random * 1664525u + 1013904223u;
        mutated[offset] ^= static_cast<char>(1u + (random % 255u));
        overwrite(mutated);
        {
            const auto result = load();
        }
        EXPECT_EQ(engine.memoryResource()->counters().liveBytes, before);
    }
}
} // namespace

namespace
{
TEST_F(DumpCodecTest, RefusesUnpersistedProcessingMeaningAndExpiredDeadlines)
{
    auto input = frame();
    ASSERT_TRUE(save(input).hasValue());
    const auto original = bytes();
    const auto name = path();
    auto validity = an::ValidityDescriptor::create({.rule = an::ValidityRule::ZeroIsNoData});
    ASSERT_TRUE(validity.has_value());
    auto semantics = an::ImageSemantics::create({.validity = *validity});
    ASSERT_TRUE(semantics.hasValue());
    const auto metadata = an::ImageMetadata::create({}, *semantics, {}, {}, {}, {});
    {
        auto execution = an::ExecutionContext::create(engine, {});
        ASSERT_TRUE(execution.hasValue());
        EXPECT_FALSE(an::writeImage(engine, *execution, {.destinationPath = name, .format = an::CodecFormat::Dump},
                                    input, metadata)
                         .hasValue());
        EXPECT_EQ(bytes(), original);
    }
    auto expired =
        an::ExecutionContext::create(engine, {.deadline = std::chrono::steady_clock::now() - std::chrono::seconds(1)});
    ASSERT_TRUE(expired.hasValue());
    EXPECT_EQ(an::decodeImage(engine, *expired, {name, an::CodecFormat::Dump}).status().code(),
              an::ErrorCode::DeadlineExceeded);
}
} // namespace

namespace
{
TEST_F(DumpCodecTest, PreservesEveryAdmissibleChannelBasisAndNumericMeaning)
{
    for (unsigned schema = 0u; an::isChannelSchema(static_cast<an::ChannelSchema>(schema)); ++schema)
    {
        const auto channelSchema = static_cast<an::ChannelSchema>(schema);
        const auto channels = an::channelCountOf(channelSchema).value_or(4u);
        auto input = frame({4u, an::RawSampleClass::FloatingPoint}, channels);
        unsigned accepted = 0u;
        for (unsigned color = 0u; an::isColorSpace(static_cast<an::ColorSpace>(color)); ++color)
            for (unsigned domain = 0u; an::isNumericDomain(static_cast<an::NumericDomain>(domain)); ++domain)
            {
                auto request = input.descriptor()->toRequest();
                request.channelSchema = channelSchema;
                request.colorSpace = static_cast<an::ColorSpace>(color);
                request.numericDomain = static_cast<an::NumericDomain>(domain);
                request.byteOrder = an::ByteOrder::Little;
                const auto descriptor = an::RawFrameDescriptor::create(request);
                if (!descriptor)
                    continue;
                ++accepted;
                auto buffer = an::SampleBuffer::borrowConst(input.bytes());
                ASSERT_TRUE(buffer.hasValue());
                auto described = an::RawFrame::create(*descriptor, *input.layout(), std::move(*buffer));
                ASSERT_TRUE(described.hasValue());
                ASSERT_TRUE(save(*described).hasValue());
                auto decoded = load();
                ASSERT_TRUE(decoded.hasValue());
                EXPECT_EQ(*decoded->frame.descriptor(), *descriptor);
            }
        EXPECT_GT(accepted, 0u) << schema;
    }
}
} // namespace
