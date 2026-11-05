// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/IO/ImageListSource.h>
#include <AfterNightEngine/IO/DummySource.h>

#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/ImageCodec.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>
#include <AfterNightEngine/Runtime/TraceSink.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <stdexcept>
#include <thread>

namespace
{
an::RawFrameDescriptor resourceDescriptor()
{
    return *an::RawFrameDescriptor::create({*an::ImageExtent::create(3u, 2u, 1u, an::defaultEngineLimits()),
                                            {2u, an::RawSampleClass::UnsignedInteger},
                                            an::ByteOrder::Big,
                                            an::ChannelSchema::Mono,
                                            an::ColorSpace::Unspecified,
                                            an::NumericDomain::RawSensorCode,
                                            {},
                                            an::CoordinateOrigin::TopLeft,
                                            1.0});
}

class SourceCodec final : public an::ImageCodec
{
public:
    an::CodecCapabilities capabilities() const override
    {
        return {.format = an::CodecFormat::Dump,
                .decodeSamples = an::codecSampleBit({2u, an::RawSampleClass::UnsignedInteger})};
    }
    an::Result<an::CodecImageInfo> probe(const an::EngineContext&, const an::ExecutionContext&,
                                         const an::CodecReadRequest&) const override
    {
        ++probes;
        return an::CodecImageInfo{descriptor, layout, 3u};
    }
    an::Result<an::CodecDecodeResult> decode(const an::EngineContext& engine, const an::ExecutionContext& execution,
                                             const an::CodecReadRequest& request) const override
    {
        ++decodes;
        imageIndex = request.imageIndex;
        metadataRequested = request.readMetadata;
        decoderOverride = request.rawDecoderPreference == an::RawDecoderPreference::NativeLibRaw;
        if (throwDecode)
            throw std::runtime_error("injected codec exception");
        auto allocationLayout = layout;
        if (extraCapacity)
            allocationLayout = *an::ImageLayout::create(
                {*an::ImageExtent::create(6u, 2u, 1u, engine.limits()), 2u, 1u, 1u, 1u}, engine.limits());
        an::Result<an::SampleBuffer> buffer =
            external ? an::SampleBuffer::external(an::SampleBufferOwnership::DecoderOwned, external->data(),
                                                  external->size(), external, false)
                     : an::SampleBuffer::createOwned(engine, allocationLayout, an::StorageInitialization::Zeroed);
        if (!buffer)
            return buffer.status();
        auto raw = an::RawFrame::create(descriptor, layout, std::move(*buffer));
        if (!raw)
            return raw.status();
        const auto nested = an::parallelFor(execution, 31u, {1u, 1u}, [&](an::TaskRange range) {
            items.fetch_add(range.end - range.begin);
            return an::Status::success();
        });
        if (nested.failed())
            return nested;
        return an::CodecDecodeResult{std::move(*raw), {}};
    }
    an::RawFrameDescriptor descriptor = resourceDescriptor();
    an::ImageLayout layout =
        *an::ImageLayout::createWithStrides({descriptor.extent(), 2u, 1u, 1u, 1u}, 8u, 16u, an::defaultEngineLimits());
    std::shared_ptr<std::array<std::byte, 16u>> external;
    bool throwDecode = false;
    bool extraCapacity = false;
    mutable std::atomic<unsigned> probes = 0u;
    mutable std::atomic<unsigned> decodes = 0u;
    mutable std::atomic<std::uint64_t> items = 0u;
    mutable std::atomic<std::uint32_t> imageIndex = 0u;
    mutable std::atomic<bool> metadataRequested = true;
    mutable std::atomic<bool> decoderOverride = false;
};

an::EngineContext resourceEngine(const std::shared_ptr<SourceCodec>& codec, std::uint64_t budget = 4096u)
{
    const std::array<std::shared_ptr<const an::ImageCodec>, 1u> adapters{codec};
    an::EngineContextOptions options;
    options.codecRegistry = *an::CodecRegistry::create(adapters);
    options.limits.memoryBudgetBytes = budget;
    return std::move(*an::EngineContext::create(options));
}

const std::array<an::CodecReadRequest, 3u> kEntries{{{"first.dump", an::CodecFormat::Dump},
                                                     {"second.dump", an::CodecFormat::Dump},
                                                     {"third.dump", an::CodecFormat::Dump}}};

std::uint64_t sourceSetupBytes(const std::shared_ptr<SourceCodec>& codec)
{
    auto engine = resourceEngine(codec);
    auto execution = an::ExecutionContext::create(engine, {});
    auto source = an::createImageListSource(engine, *execution, kEntries, {});
    EXPECT_TRUE(source);
    return engine.memoryResource()->counters().liveBytes;
}

TEST(ImageListResourceTest, EvictsBeforeAllocationSoOneFrameBudgetCanStream)
{
    auto codec = std::make_shared<SourceCodec>();
    const auto setupBytes = sourceSetupBytes(codec);
    auto engine = resourceEngine(codec, setupBytes + 16u);
    auto execution = an::ExecutionContext::create(engine, {});
    auto source = an::createImageListSource(engine, *execution, kEntries, {an::ImageListLoadMode::Cached, 16u});
    ASSERT_TRUE(source);
    for (std::size_t index = 0u; index < kEntries.size(); ++index)
        ASSERT_TRUE(source->read(engine, *execution).hasValue());
    EXPECT_EQ(codec->decodes, 3u);
    EXPECT_EQ(engine.memoryResource()->counters().peakBytes, setupBytes + 14u);
    EXPECT_EQ(engine.memoryResource()->counters().deniedAllocations, 0u);
    EXPECT_TRUE(source->close().ok());
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
}

TEST(ImageListResourceTest, CallerHeldEvictionRemainsChargedAndRefusesNextAllocation)
{
    auto codec = std::make_shared<SourceCodec>();
    auto engine = resourceEngine(codec, sourceSetupBytes(codec) + 16u);
    auto execution = an::ExecutionContext::create(engine, {});
    auto source = an::createImageListSource(engine, *execution, kEntries, {an::ImageListLoadMode::Cached, 16u});
    ASSERT_TRUE(source);
    auto retained = source->read(engine, *execution);
    ASSERT_TRUE(retained);
    EXPECT_EQ(source->read(engine, *execution).status().code(), an::ErrorCode::MemoryBudgetExceeded);
    EXPECT_EQ(source->nextIndex(), 1u);
    EXPECT_EQ(source->state(), an::SequenceState::Failed);
    EXPECT_TRUE(source->close().ok());
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 14u);
    retained->frame.reset();
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
}

TEST(ImageListResourceTest, ExternalPayloadIsCopiedIntoBudgetedCacheIncludingPadding)
{
    auto codec = std::make_shared<SourceCodec>();
    codec->external = std::make_shared<std::array<std::byte, 16u>>();
    codec->external->fill(std::byte{71});
    const auto setupBytes = sourceSetupBytes(codec);
    auto engine = resourceEngine(codec, setupBytes + 16u);
    auto execution = an::ExecutionContext::create(engine, {});
    auto source = an::createImageListSource(engine, *execution, kEntries, {an::ImageListLoadMode::Cached, 16u});
    ASSERT_TRUE(source);
    auto retained = source->read(engine, *execution);
    ASSERT_TRUE(retained);
    codec->external->fill(std::byte{0});
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, setupBytes + 14u);
    EXPECT_FALSE(retained->frame->frame.mutableBytes());
    EXPECT_TRUE(
        std::ranges::all_of(retained->frame->frame.bytes(), [](std::byte value) { return value == std::byte{71}; }));
    EXPECT_EQ(retained->frame->frame.layout()->rowStrideBytes(), 8u);
    EXPECT_TRUE(source->close().ok());
    EXPECT_EQ(retained->frame->frame.bytes().back(), std::byte{71});
}

TEST(ImageListResourceTest, LoadingPolicyControlsCodecCallsAndPreservesEverySelection)
{
    for (auto mode : {an::ImageListLoadMode::Lazy, an::ImageListLoadMode::Eager, an::ImageListLoadMode::Cached})
    {
        auto codec = std::make_shared<SourceCodec>();
        auto engine = resourceEngine(codec);
        auto execution = an::ExecutionContext::create(engine, {});
        auto entries = kEntries;
        entries[0].imageIndex = 2u;
        entries[0].readMetadata = false;
        entries[0].rawDecoderPreference = an::RawDecoderPreference::NativeLibRaw;
        auto source = an::createImageListSource(engine, *execution, entries,
                                                {mode, mode == an::ImageListLoadMode::Lazy ? 0u : 64u});
        ASSERT_TRUE(source);
        EXPECT_EQ(codec->decodes, mode == an::ImageListLoadMode::Eager ? 3u : 0u);
        EXPECT_EQ(codec->probes == 0u, mode != an::ImageListLoadMode::Eager);
        for (int repeat = 0; repeat < 2; ++repeat)
        {
            ASSERT_TRUE(source->seek(*execution, {}).hasValue());
            ASSERT_TRUE(source->read(engine, *execution).hasValue());
        }
        EXPECT_EQ(codec->decodes, mode == an::ImageListLoadMode::Eager    ? 3u
                                  : mode == an::ImageListLoadMode::Cached ? 1u
                                                                          : 2u);
        if (mode != an::ImageListLoadMode::Eager)
        {
            EXPECT_EQ(codec->imageIndex, 2u);
            EXPECT_FALSE(codec->metadataRequested);
            EXPECT_TRUE(codec->decoderOverride);
        }
    }
}

TEST(ImageListResourceTest, CodecExceptionsFailAtomicallyAndReleasePreloadedFrames)
{
    auto codec = std::make_shared<SourceCodec>();
    codec->throwDecode = true;
    auto engine = resourceEngine(codec);
    auto execution = an::ExecutionContext::create(engine, {});
    auto eager = an::createImageListSource(engine, *execution, kEntries, {an::ImageListLoadMode::Eager, 64u});
    EXPECT_EQ(eager.status().code(), an::ErrorCode::ExternalFailure);
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
    auto lazy = an::createImageListSource(engine, *execution, kEntries, {});
    ASSERT_TRUE(lazy);
    EXPECT_EQ(lazy->read(engine, *execution).status().code(), an::ErrorCode::ExternalFailure);
    EXPECT_EQ(lazy->state(), an::SequenceState::Failed);
}

TEST(ImageListResourceTest, ContextMismatchIsRefusedAndMovingTheOwnerKeepsIdentity)
{
    auto codec = std::make_shared<SourceCodec>();
    auto engine = resourceEngine(codec);
    auto foreign = resourceEngine(codec);
    auto execution = an::ExecutionContext::create(engine, {});
    EXPECT_EQ(an::createImageListSource(foreign, *execution, kEntries, {}).status().code(),
              an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(an::createDummySource(foreign, *execution, {codec->descriptor, codec->layout, 1u}).status().code(),
              an::ErrorCode::IncompatibleArguments);
    auto source = an::createImageListSource(engine, *execution, kEntries, {});
    ASSERT_TRUE(source);
    auto moved = std::move(engine);
    EXPECT_TRUE(source->read(moved, *execution).hasValue());
}

TEST(ImageListResourceTest, IndependentSourcesRunNestedWorkConcurrently)
{
    // The budget has to hold both sources at once, which is the whole point: an eager source of
    // these entries holds 574 bytes while it is alive, and a budget that fits one of them makes this
    // case pass only in the runs where the two threads did not actually overlap. Budget pressure is
    // EvictsBeforeAllocationSoOneFrameBudgetCanStream's subject, not this one's.
    auto codec = std::make_shared<SourceCodec>();
    auto engine = resourceEngine(codec, 4096u);
    std::atomic<unsigned> failures = 0u;
    std::atomic<unsigned> ready = 0u;
    auto run = [&] {
        auto execution = an::ExecutionContext::create(engine, {.maxConcurrency = 1u});
        if (!execution)
        {
            ++failures;
            return;
        }
        auto source = an::createImageListSource(engine, *execution, kEntries, {an::ImageListLoadMode::Eager, 48u});
        if (!source)
        {
            ++failures;
            return;
        }
        // Neither thread reads until both sources exist, so the two really are alive at once rather
        // than one after the other. Bounded, so a thread whose partner already failed reports instead
        // of waiting for it.
        ready.fetch_add(1u, std::memory_order_acq_rel);
        for (int spin = 0; spin < 200000 && ready.load(std::memory_order_acquire) < 2u; ++spin)
        {
            std::this_thread::yield();
        }
        for (std::uint64_t index = 0u; index < 3u; ++index)
        {
            auto read = source->read(engine, *execution);
            if (!read || read->frame->position.index != index)
                ++failures;
        }
    };
    std::thread first(run), second(run);
    first.join();
    second.join();
    EXPECT_EQ(failures, 0u);
    EXPECT_EQ(codec->items, 6u * 31u);
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
}

TEST(ImageListResourceTest, DummyDescribesResidentAndNonResidentFormatsWithoutConversion)
{
    auto codec = std::make_shared<SourceCodec>();
    auto engine = resourceEngine(codec, 256u);
    auto execution = an::ExecutionContext::create(engine, {});
    for (auto sampleClass :
         {an::RawSampleClass::UnsignedInteger, an::RawSampleClass::SignedInteger, an::RawSampleClass::FloatingPoint})
        for (std::uint8_t size : {1u, 2u, 4u, 8u})
        {
            if (sampleClass == an::RawSampleClass::FloatingPoint && size < 4u)
                continue;
            auto request = codec->descriptor.toRequest();
            request.sampleFormat = {size, sampleClass};
            request.byteOrder = size == 1u ? an::ByteOrder::Native : an::ByteOrder::Big;
            auto descriptor = an::RawFrameDescriptor::create(request);
            ASSERT_TRUE(descriptor);
            auto layout = an::ImageLayout::create({request.extent, size, 1u, 1u, 16u}, engine.limits());
            ASSERT_TRUE(layout);
            auto source = an::createDummySource(engine, *execution, {*descriptor, *layout, 1u});
            ASSERT_TRUE(source);
            auto read = source->read(engine, *execution);
            ASSERT_TRUE(read);
            EXPECT_EQ(read->frame->frame.descriptor(), *descriptor);
            EXPECT_TRUE(
                std::ranges::all_of(read->frame->frame.bytes(), [](std::byte value) { return value == std::byte{}; }));
        }
}

TEST(ImageListResourceTest, CacheBudgetUsesActualCapacityBeyondTheProbedGeometry)
{
    auto codec = std::make_shared<SourceCodec>();
    codec->extraCapacity = true;
    auto engine = resourceEngine(codec);
    auto execution = an::ExecutionContext::create(engine, {});
    const std::span entries(kEntries.data(), 1u);
    auto eager = an::createImageListSource(engine, *execution, entries, {an::ImageListLoadMode::Eager, 20u});
    EXPECT_EQ(eager.status().code(), an::ErrorCode::MemoryBudgetExceeded);
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
    auto cache = an::createImageListSource(engine, *execution, entries, {an::ImageListLoadMode::Cached, 20u});
    ASSERT_TRUE(cache);
    const auto setupBytes = engine.memoryResource()->counters().liveBytes;
    {
        auto read = cache->read(engine, *execution);
        ASSERT_TRUE(read);
        EXPECT_EQ(read->frame->frame.buffer().ownership(), an::SampleBufferOwnership::Owned);
        EXPECT_EQ(read->frame->frame.buffer().byteCount(), 24u);
    }
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, setupBytes);
}

TEST(ImageListResourceTest, DummyBudgetFailureIsTerminalAndPublishesNoFrame)
{
    auto codec = std::make_shared<SourceCodec>();
    auto engine = resourceEngine(codec, 13u);
    auto execution = an::ExecutionContext::create(engine, {});
    auto source = an::createDummySource(engine, *execution, {codec->descriptor, codec->layout, 1u});
    ASSERT_TRUE(source);
    auto read = source->read(engine, *execution);
    EXPECT_EQ(read.status().code(), an::ErrorCode::MemoryBudgetExceeded);
    EXPECT_EQ(source->state(), an::SequenceState::Failed);
    EXPECT_EQ(source->nextIndex(), 0u);
    EXPECT_TRUE(source->close().ok());
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
}

TEST(ImageListResourceTest, SelectionStorageCannotEscapeTheEngineBudget)
{
    auto codec = std::make_shared<SourceCodec>();
    auto engine = resourceEngine(codec, 1u);
    auto execution = an::ExecutionContext::create(engine, {});
    auto source = an::createImageListSource(engine, *execution, kEntries, {});
    EXPECT_EQ(source.status().code(), an::ErrorCode::MemoryBudgetExceeded);
    EXPECT_EQ(codec->probes, 0u);
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
}

TEST(ImageListResourceTest, FullScaleBlackRetainsSignedMinimumInBothByteOrdersAndZerosPadding)
{
    auto codec = std::make_shared<SourceCodec>();
    auto engine = resourceEngine(codec);
    auto execution = an::ExecutionContext::create(engine, {});
    for (const auto size : std::array<std::uint8_t, 4u>{1u, 2u, 4u, 8u})
        for (auto order : {an::ByteOrder::Little, an::ByteOrder::Big})
        {
            auto request = codec->descriptor.toRequest();
            request.sampleFormat = {size, an::RawSampleClass::SignedInteger};
            request.byteOrder = size == 1u ? an::ByteOrder::Native : order;
            auto descriptor = an::RawFrameDescriptor::create(request);
            ASSERT_TRUE(descriptor);
            const auto stride = std::uint64_t{size} * 4u;
            auto layout = an::ImageLayout::createWithStrides({request.extent, size, 1u, 1u, 1u}, stride, stride * 2u,
                                                             engine.limits());
            ASSERT_TRUE(layout);
            auto source = an::createDummySource(engine, *execution,
                                                {*descriptor, *layout, 1u, an::DummyFillMode::FullScaleBlack});
            ASSERT_TRUE(source);
            auto read = source->read(engine, *execution);
            ASSERT_TRUE(read);
            const auto bytes = read->frame->frame.bytes();
            for (std::size_t offset = 0u; offset < bytes.size(); ++offset)
            {
                const auto x = offset % stride;
                const auto expected = x < 3u * size && x % size == (order == an::ByteOrder::Big ? 0u : size - 1u)
                                          ? std::byte{0x80}
                                          : std::byte{};
                EXPECT_EQ(bytes[offset], expected);
            }
        }
}

TEST(ImageListResourceTest, RejectsUnknownDummyPatternBeforeAllocating)
{
    auto codec = std::make_shared<SourceCodec>();
    auto engine = resourceEngine(codec);
    auto execution = an::ExecutionContext::create(engine, {});
    auto source = an::createDummySource(engine, *execution,
                                        {codec->descriptor, codec->layout, 1u, static_cast<an::DummyFillMode>(255u)});
    EXPECT_EQ(source.status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(engine.memoryResource()->counters().totalBytes, 0u);
}
} // namespace
