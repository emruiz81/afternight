// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the codec reads that name their own registry.
//
// A read through an explicit registry differs from the ordinary read in one thing only: which adapter it selects.
// Each case holds one half of that sentence. The adapter comes from the named registry and from nothing else - not
// the engine context's, not as a fallback - and everything else is the ordinary read's: the engine context's limits
// and memory, the execution's admission, arena and stop, the exception boundary, and a frame that does not depend on
// the registry it was read through. The adapter here is a memory-backed provider that counts its calls and states
// what it was handed, so a case can see which registry was reached without a file format in the way.

#include <AfterNightEngine/IO/CodecIO.h>

#include <AfterNightEngine/IO/CodecError.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/ImageCodec.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <utility>
#include <vector>

namespace
{

/// A seven by five plane of sixteen-bit sensor codes.
[[nodiscard]] an::RawFrameDescriptor description(std::uint32_t width = 7u, std::uint32_t height = 5u)
{
    return *an::RawFrameDescriptor::create(
        {.extent = *an::ImageExtent::create(width, height, 1u, an::defaultEngineLimits()),
         .sampleFormat = {2u, an::RawSampleClass::UnsignedInteger},
         .byteOrder = an::ByteOrder::Native,
         .channelSchema = an::ChannelSchema::Mono,
         .colorSpace = an::ColorSpace::Unspecified,
         .numericDomain = an::NumericDomain::RawSensorCode,
         .cfa = {},
         .coordinateOrigin = an::CoordinateOrigin::TopLeft,
         .pixelAspectRatio = 1.0});
}

/// The tight layout of a description.
[[nodiscard]] an::ImageLayout layoutOf(const an::RawFrameDescriptor& value)
{
    return *an::ImageLayout::create({.extent = value.extent(),
                                     .elementSizeBytes = value.sampleFormat().sizeBytes,
                                     .samplesPerPixel = 1u,
                                     .planeCount = value.extent().channelCount(),
                                     .rowAlignmentBytes = 1u},
                                    an::defaultEngineLimits());
}

/// What a provider throws from its pixel callbacks, when it throws.
enum class Throw : std::uint8_t
{
    Nothing,
    Exception,
    OutOfMemory
};

/// A memory-backed provider of one format that counts its calls and states what it was handed.
class CountingCodec final : public an::ImageCodec
{
public:
    explicit CountingCodec(an::CodecFormat format, bool regions = true) : m_format(format), m_regions(regions) {}

    ~CountingCodec() override
    {
        if (destroyed != nullptr)
        {
            destroyed->store(true);
        }
    }

    CountingCodec(const CountingCodec&) = delete;
    CountingCodec& operator=(const CountingCodec&) = delete;

    an::CodecCapabilities capabilities() const override
    {
        return {.format = m_format,
                .decodeSamples = an::codecSampleBit({2u, an::RawSampleClass::UnsignedInteger}),
                .encodeSamples = 0u,
                .regionDecode = m_regions};
    }

    an::Result<an::CodecImageInfo> probe(const an::EngineContext& engine, const an::ExecutionContext& execution,
                                         const an::CodecReadRequest&) const override
    {
        ++probes;
        sawEngine = &engine;
        sawExecution = &execution;
        if (throwing == Throw::Exception)
        {
            throw std::runtime_error("the provider's header reader failed");
        }
        return an::CodecImageInfo{source, layoutOf(source), 1u, {}};
    }

    an::Result<an::CodecDecodeResult> decode(const an::EngineContext& engine, const an::ExecutionContext& execution,
                                             const an::CodecReadRequest&) const override
    {
        ++decodes;
        sawEngine = &engine;
        sawExecution = &execution;
        return frameOf(engine, execution, source);
    }

    an::Result<an::CodecDecodeResult> decodeRegion(const an::EngineContext& engine,
                                                   const an::ExecutionContext& execution, const an::CodecReadRequest&,
                                                   const an::CodecRegion& region) const override
    {
        ++regionDecodes;
        sawEngine = &engine;
        sawExecution = &execution;
        auto request = source.toRequest();
        request.extent = *an::ImageExtent::create(region.width, region.height, 1u, engine.limits());
        return frameOf(engine, execution, *an::RawFrameDescriptor::create(request));
    }

    an::RawFrameDescriptor source = description();
    Throw throwing = Throw::Nothing;
    /// Stopped after the samples are made, so the facade's check after the adapter sees the stop.
    std::stop_source* stopAfterDecode = nullptr;
    /// Set when the provider is destroyed.
    std::shared_ptr<std::atomic<bool>> destroyed;
    mutable std::atomic<std::uint32_t> probes{0u};
    mutable std::atomic<std::uint32_t> decodes{0u};
    mutable std::atomic<std::uint32_t> regionDecodes{0u};
    /// How many items of nested parallel work the last decode ran.
    mutable std::atomic<std::uint64_t> nestedItems{0u};
    mutable const an::EngineContext* sawEngine = nullptr;
    mutable const an::ExecutionContext* sawExecution = nullptr;

private:
    an::Result<an::CodecDecodeResult> frameOf(const an::EngineContext& engine, const an::ExecutionContext& execution,
                                              const an::RawFrameDescriptor& value) const
    {
        if (throwing == Throw::Exception)
        {
            throw std::runtime_error("the provider's decoder failed");
        }
        if (throwing == Throw::OutOfMemory)
        {
            throw std::bad_alloc();
        }
        nestedItems.store(0u);
        const an::Status nested = an::parallelFor(execution, 64u, {1u, 1u}, [this](an::TaskRange range) {
            nestedItems.fetch_add(range.end - range.begin);
            return an::Status::success();
        });
        if (nested.failed())
        {
            return nested;
        }
        an::Result<an::SampleBuffer> buffer =
            an::SampleBuffer::createOwned(engine, layoutOf(value), an::StorageInitialization::Zeroed);
        if (!buffer.hasValue())
        {
            return buffer.status();
        }
        an::Result<an::RawFrame> frame = an::RawFrame::create(value, layoutOf(value), std::move(*buffer));
        if (!frame.hasValue())
        {
            return frame.status();
        }
        if (stopAfterDecode != nullptr)
        {
            stopAfterDecode->request_stop();
        }
        return an::CodecDecodeResult{std::move(*frame), {}};
    }

    an::CodecFormat m_format;
    bool m_regions;
};

/// A registry holding these providers, or the test fails.
[[nodiscard]] std::shared_ptr<const an::CodecRegistry>
registryOf(std::initializer_list<std::shared_ptr<const an::ImageCodec>> codecs)
{
    const std::vector<std::shared_ptr<const an::ImageCodec>> held(codecs);
    an::Result<std::shared_ptr<const an::CodecRegistry>> registry = an::CodecRegistry::create(held);
    EXPECT_TRUE(registry.hasValue()) << registry.status().message();
    return registry.hasValue() ? *registry : nullptr;
}

/// An engine context of two workers over this registry, which may be none, and this budget, which may be none.
[[nodiscard]] an::EngineContext engineOf(std::shared_ptr<const an::CodecRegistry> registry, std::uint64_t budget = 0u)
{
    an::EngineContextOptions options;
    options.maxConcurrency = 2u;
    options.codecRegistry = std::move(registry);
    if (budget != 0u)
    {
        options.limits.memoryBudgetBytes = budget;
    }
    an::Result<an::EngineContext> engine = an::EngineContext::create(options);
    EXPECT_TRUE(engine.hasValue()) << engine.status().message();
    return engine.hasValue() ? std::move(*engine) : an::EngineContext{};
}

/// An execution of an engine context with these options, or the test fails.
[[nodiscard]] an::ExecutionContext executionOf(const an::EngineContext& engine, an::ExecutionOptions options = {})
{
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(execution.hasValue()) << execution.status().message();
    return execution.hasValue() ? std::move(*execution) : an::ExecutionContext{};
}

[[nodiscard]] std::uint64_t liveBytes(const an::EngineContext& engine)
{
    return engine.memoryResource()->counters().liveBytes;
}

} // namespace

TEST(ExplicitRegistryReadTest, TheNamedRegistrysAdapterServesTheReadAndTheContextsServesTheOthers)
{
    const auto own = std::make_shared<CountingCodec>(an::CodecFormat::Fits);
    const auto named = std::make_shared<CountingCodec>(an::CodecFormat::Fits);
    named->source = description(9u, 3u);
    const an::EngineContext engine = engineOf(registryOf({own}));
    const std::shared_ptr<const an::CodecRegistry> registry = registryOf({named});
    const an::ExecutionContext execution = executionOf(engine);

    const an::Result<an::CodecImageInfo> probed = an::probeImage(engine, execution, *registry, {.path = "m42.fits"});
    ASSERT_TRUE(probed.hasValue()) << probed.status().message();
    EXPECT_EQ(probed->descriptor, description(9u, 3u));
    const an::Result<an::CodecDecodeResult> decoded =
        an::decodeImage(engine, execution, *registry, {.path = "m42.fits"});
    ASSERT_TRUE(decoded.hasValue()) << decoded.status().message();
    EXPECT_EQ(decoded->frame.descriptor(), description(9u, 3u));
    const an::Result<an::CodecDecodeResult> region =
        an::decodeImageRegion(engine, execution, *registry, {.path = "m42.fits"}, {1u, 1u, 4u, 2u});
    ASSERT_TRUE(region.hasValue()) << region.status().message();
    EXPECT_EQ(region->frame.descriptor()->extent().width(), 4u);
    EXPECT_EQ(own->probes.load() + own->decodes.load() + own->regionDecodes.load(), 0u);
    EXPECT_EQ(named->decodes.load(), 1u);
    EXPECT_EQ(named->regionDecodes.load(), 1u);

    // The calls that name no registry still read through the engine context's, which nothing above changed.
    const an::Result<an::CodecDecodeResult> ordinary = an::decodeImage(engine, execution, {.path = "m42.fits"});
    ASSERT_TRUE(ordinary.hasValue()) << ordinary.status().message();
    EXPECT_EQ(ordinary->frame.descriptor(), description());
    EXPECT_EQ(own->decodes.load(), 1u);
    EXPECT_EQ(named->decodes.load(), 1u);
}

TEST(ExplicitRegistryReadTest, AnEngineContextWithNoCodecsReadsThroughTheRegistryItIsGiven)
{
    const auto named = std::make_shared<CountingCodec>(an::CodecFormat::Raw);
    const an::EngineContext engine = engineOf(nullptr);
    const std::shared_ptr<const an::CodecRegistry> registry = registryOf({named});
    const an::ExecutionContext execution = executionOf(engine);

    EXPECT_TRUE(an::probeImage(engine, execution, *registry, {.path = "frame.dng"}).hasValue());
    EXPECT_TRUE(an::decodeImage(engine, execution, *registry, {.path = "frame.dng"}).hasValue());
    EXPECT_TRUE(
        an::decodeImageRegion(engine, execution, *registry, {.path = "frame.dng"}, {0u, 0u, 2u, 2u}).hasValue());
    const an::Result<an::CodecDecodeResult> ordinary = an::decodeImage(engine, execution, {.path = "frame.dng"});
    EXPECT_EQ(ordinary.status().code(), an::ErrorCode::Unsupported);
    EXPECT_EQ(an::codecFailureReason(ordinary.status()), an::CodecFailure::Unavailable);
}

TEST(ExplicitRegistryReadTest, AFormatTheRegistryLacksIsRefusedWithNoFallbackToTheContexts)
{
    const auto own = std::make_shared<CountingCodec>(an::CodecFormat::Fits);
    const auto png = std::make_shared<CountingCodec>(an::CodecFormat::Png);
    const an::EngineContext engine = engineOf(registryOf({own}));
    const std::shared_ptr<const an::CodecRegistry> registry = registryOf({png});
    const an::ExecutionContext execution = executionOf(engine);

    const an::Result<an::CodecImageInfo> probed = an::probeImage(engine, execution, *registry, {.path = "m42.fits"});
    const an::Result<an::CodecDecodeResult> decoded =
        an::decodeImage(engine, execution, *registry, {.path = "m42.fits"});
    const an::Result<an::CodecDecodeResult> region =
        an::decodeImageRegion(engine, execution, *registry, {.path = "m42.fits"}, {0u, 0u, 1u, 1u});
    for (const an::Status& status : {probed.status(), decoded.status(), region.status()})
    {
        EXPECT_EQ(status.code(), an::ErrorCode::Unsupported);
        EXPECT_EQ(an::codecFailureReason(status), an::CodecFailure::Unavailable);
    }
    EXPECT_EQ(own->probes.load() + own->decodes.load() + own->regionDecodes.load(), 0u);
    EXPECT_EQ(png->probes.load() + png->decodes.load() + png->regionDecodes.load(), 0u);

    // An empty registry has no adapter for anything, and the engine context's is still not consulted.
    const an::Result<std::shared_ptr<const an::CodecRegistry>> empty = an::CodecRegistry::create({});
    ASSERT_TRUE(empty.hasValue());
    EXPECT_EQ(an::codecFailureReason(an::decodeImage(engine, execution, **empty, {.path = "m42.fits"}).status()),
              an::CodecFailure::Unavailable);
    EXPECT_EQ(own->decodes.load(), 0u);
}

TEST(ExplicitRegistryReadTest, AnUnsupportedRegionIsRefusedBeforeTheAdapterIsAsked)
{
    const auto whole = std::make_shared<CountingCodec>(an::CodecFormat::Fits, false);
    const an::EngineContext engine = engineOf(nullptr);
    const std::shared_ptr<const an::CodecRegistry> registry = registryOf({whole});
    const an::ExecutionContext execution = executionOf(engine);

    const an::Result<an::CodecDecodeResult> region =
        an::decodeImageRegion(engine, execution, *registry, {.path = "m42.fits"}, {0u, 0u, 2u, 2u});
    EXPECT_EQ(region.status().code(), an::ErrorCode::Unsupported);
    EXPECT_EQ(an::codecFailureReason(region.status()), an::CodecFailure::UnsupportedOperation);
    EXPECT_EQ(whole->probes.load() + whole->decodes.load() + whole->regionDecodes.load(), 0u);
    const an::Result<an::CodecDecodeResult> outside =
        an::decodeImageRegion(engine, execution, *registryOf({std::make_shared<CountingCodec>(an::CodecFormat::Fits)}),
                              {.path = "m42.fits"}, {6u, 0u, 2u, 1u});
    EXPECT_EQ(outside.status().code(), an::ErrorCode::OutOfRange);
}

TEST(ExplicitRegistryReadTest, TheSamplesAreChargedToTheEngineContextAndItsLimitsBind)
{
    const auto named = std::make_shared<CountingCodec>(an::CodecFormat::Fits);
    const std::shared_ptr<const an::CodecRegistry> registry = registryOf({named});
    {
        const an::EngineContext engine = engineOf(nullptr);
        const an::ExecutionContext execution = executionOf(engine);
        std::optional<an::CodecDecodeResult> held;
        {
            an::Result<an::CodecDecodeResult> decoded =
                an::decodeImage(engine, execution, *registry, {.path = "a.fits"});
            ASSERT_TRUE(decoded.hasValue()) << decoded.status().message();
            held.emplace(std::move(*decoded));
        }
        EXPECT_EQ(held->frame.buffer().contextId(), engine.id());
        EXPECT_GE(liveBytes(engine), held->frame.bytes().size());
        held.reset();
        EXPECT_EQ(liveBytes(engine), 0u);
    }
    {
        // The budget is the engine context's: a registry brings none of its own.
        named->source = description(512u, 512u);
        const an::EngineContext engine = engineOf(nullptr, 4096u);
        const an::ExecutionContext execution = executionOf(engine);
        const an::Result<an::CodecDecodeResult> decoded =
            an::decodeImage(engine, execution, *registry, {.path = "a.fits"});
        EXPECT_EQ(decoded.status().code(), an::ErrorCode::MemoryBudgetExceeded);
        EXPECT_EQ(liveBytes(engine), 0u);
    }
    {
        // So are the limits, checked on the header before any sample is allocated.
        an::EngineContextOptions options;
        options.limits.maxImageWidth = 100u;
        an::Result<an::EngineContext> engine = an::EngineContext::create(options);
        ASSERT_TRUE(engine.hasValue());
        const an::ExecutionContext execution = executionOf(*engine);
        const std::uint32_t decodes = named->decodes.load();
        EXPECT_EQ(an::probeImage(*engine, execution, *registry, {.path = "a.fits"}).status().code(),
                  an::ErrorCode::LimitExceeded);
        EXPECT_EQ(an::decodeImage(*engine, execution, *registry, {.path = "a.fits"}).status().code(),
                  an::ErrorCode::LimitExceeded);
        EXPECT_EQ(named->decodes.load(), decodes);
    }
}

TEST(ExplicitRegistryReadTest, TheAdapterRunsUnderTheCallersExecutionAndAdmitsNothingOfItsOwn)
{
    const auto named = std::make_shared<CountingCodec>(an::CodecFormat::Fits);
    const an::EngineContext engine = engineOf(nullptr);
    const std::shared_ptr<const an::CodecRegistry> registry = registryOf({named});
    const an::ExecutionContext execution = executionOf(engine);
    ASSERT_EQ(execution.admittedConcurrency(), 2u);

    // The whole budget is admitted to this execution, so a read that admitted work of its own would be refused.
    EXPECT_EQ(an::ExecutionContext::create(engine, {}).status().code(), an::ErrorCode::Busy);
    const an::Result<an::CodecDecodeResult> decoded = an::decodeImage(engine, execution, *registry, {.path = "a.fits"});
    ASSERT_TRUE(decoded.hasValue()) << decoded.status().message();
    EXPECT_EQ(named->sawEngine, &engine);
    EXPECT_EQ(named->sawExecution, &execution);
    EXPECT_EQ(named->nestedItems.load(), 64u);
}

TEST(ExplicitRegistryReadTest, AStopIsObservedBeforeTheAdapterRunsAndAfterItReturns)
{
    const auto named = std::make_shared<CountingCodec>(an::CodecFormat::Fits);
    const an::EngineContext engine = engineOf(nullptr);
    const std::shared_ptr<const an::CodecRegistry> registry = registryOf({named});
    {
        std::stop_source stop;
        an::ExecutionOptions options;
        options.stopToken = stop.get_token();
        const an::ExecutionContext execution = executionOf(engine, options);
        stop.request_stop();
        EXPECT_EQ(an::probeImage(engine, execution, *registry, {.path = "a.fits"}).status().code(),
                  an::ErrorCode::Cancelled);
        EXPECT_EQ(an::decodeImage(engine, execution, *registry, {.path = "a.fits"}).status().code(),
                  an::ErrorCode::Cancelled);
        EXPECT_EQ(
            an::decodeImageRegion(engine, execution, *registry, {.path = "a.fits"}, {0u, 0u, 1u, 1u}).status().code(),
            an::ErrorCode::Cancelled);
        EXPECT_EQ(named->probes.load() + named->decodes.load() + named->regionDecodes.load(), 0u);
    }
    {
        // A stop that arrives while the adapter runs discards the frame it made; nothing stays charged.
        std::stop_source stop;
        named->stopAfterDecode = &stop;
        an::ExecutionOptions options;
        options.stopToken = stop.get_token();
        const an::ExecutionContext execution = executionOf(engine, options);
        EXPECT_EQ(an::decodeImage(engine, execution, *registry, {.path = "a.fits"}).status().code(),
                  an::ErrorCode::Cancelled);
        EXPECT_EQ(named->decodes.load(), 1u);
        EXPECT_EQ(liveBytes(engine), 0u);
        named->stopAfterDecode = nullptr;
    }
}

TEST(ExplicitRegistryReadTest, WhatTheAdapterThrowsIsReturnedAtTheBoundary)
{
    const auto named = std::make_shared<CountingCodec>(an::CodecFormat::Fits);
    const an::EngineContext engine = engineOf(nullptr);
    const std::shared_ptr<const an::CodecRegistry> registry = registryOf({named});
    const an::ExecutionContext execution = executionOf(engine);

    named->throwing = Throw::Exception;
    const an::Result<an::CodecImageInfo> probed = an::probeImage(engine, execution, *registry, {.path = "a.fits"});
    EXPECT_EQ(probed.status().code(), an::ErrorCode::ExternalFailure);
    EXPECT_EQ(an::codecFailureReason(probed.status()), an::CodecFailure::CallbackFailure);
    const an::Result<an::CodecDecodeResult> decoded = an::decodeImage(engine, execution, *registry, {.path = "a.fits"});
    EXPECT_EQ(decoded.status().code(), an::ErrorCode::ExternalFailure);
    EXPECT_EQ(an::codecFailureReason(decoded.status()), an::CodecFailure::CallbackFailure);

    named->throwing = Throw::OutOfMemory;
    const an::Result<an::CodecDecodeResult> exhausted =
        an::decodeImageRegion(engine, execution, *registry, {.path = "a.fits"}, {0u, 0u, 3u, 3u});
    EXPECT_EQ(exhausted.status().code(), an::ErrorCode::AllocationFailed);
    EXPECT_EQ(an::codecFailureReason(exhausted.status()), an::CodecFailure::CallbackFailure);
    EXPECT_EQ(liveBytes(engine), 0u);
}

TEST(ExplicitRegistryReadTest, AnExecutionOfAnotherEngineContextIsRefused)
{
    const auto named = std::make_shared<CountingCodec>(an::CodecFormat::Fits);
    const an::EngineContext engine = engineOf(nullptr);
    const an::EngineContext other = engineOf(nullptr);
    const std::shared_ptr<const an::CodecRegistry> registry = registryOf({named});
    const an::ExecutionContext execution = executionOf(other);

    EXPECT_EQ(an::probeImage(engine, execution, *registry, {.path = "a.fits"}).status().code(),
              an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(an::decodeImage(engine, execution, *registry, {.path = "a.fits"}).status().code(),
              an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(an::decodeImage(an::EngineContext{}, execution, *registry, {.path = "a.fits"}).status().code(),
              an::ErrorCode::NotInitialized);
    EXPECT_EQ(named->probes.load() + named->decodes.load(), 0u);
}

TEST(ExplicitRegistryReadTest, TheRegistryIsBorrowedForTheCallAndTheFrameOutlivesIt)
{
    const an::EngineContext engine = engineOf(nullptr);
    const an::ExecutionContext execution = executionOf(engine);
    const auto destroyed = std::make_shared<std::atomic<bool>>(false);
    std::optional<an::CodecDecodeResult> kept;
    {
        auto named = std::make_shared<CountingCodec>(an::CodecFormat::Fits);
        named->destroyed = destroyed;
        std::shared_ptr<const an::CodecRegistry> registry = registryOf({std::move(named)});
        an::Result<an::CodecDecodeResult> decoded = an::decodeImage(engine, execution, *registry, {.path = "a.fits"});
        ASSERT_TRUE(decoded.hasValue()) << decoded.status().message();
        kept.emplace(std::move(*decoded));
        registry.reset();
    }
    // The read kept no reference to the registry or its adapter, and the frame it returned needs neither.
    EXPECT_TRUE(destroyed->load());
    ASSERT_TRUE(kept->frame.valid());
    EXPECT_EQ(kept->frame.buffer().contextId(), engine.id());
    EXPECT_GE(liveBytes(engine), kept->frame.bytes().size());
    kept.reset();
    EXPECT_EQ(liveBytes(engine), 0u);
}

// A row reader opened through a named registry reads its header and every band through that registry's adapter - a
// band as a region, for an adapter that decodes no rows into caller memory - and through nothing else, in an engine
// context that holds another adapter of the format or none; a registry without the format refuses the reader, as it
// refuses the reads above, and the engine context's adapter is not asked.
TEST(ExplicitRegistryReadTest, ARowReaderOpenedThroughANamedRegistryReadsEveryBandThroughIt)
{
    const auto own = std::make_shared<CountingCodec>(an::CodecFormat::Fits);
    const auto named = std::make_shared<CountingCodec>(an::CodecFormat::Fits);
    named->source = description(9u, 3u);
    const std::shared_ptr<const an::CodecRegistry> registry = registryOf({named});
    for (const bool contextHoldsOne : {true, false})
    {
        const an::EngineContext engine = engineOf(contextHoldsOne ? registryOf({own}) : nullptr);
        const an::ExecutionContext execution = executionOf(engine);
        named->probes.store(0u);
        named->regionDecodes.store(0u);

        an::Result<an::ImageRowReader> reader =
            an::ImageRowReader::open(engine, execution, *registry, {.path = "m42.fits"});
        ASSERT_TRUE(reader.hasValue()) << reader.status().message();
        ASSERT_NE(reader->info(), nullptr);
        EXPECT_EQ(reader->info()->descriptor, description(9u, 3u));
        std::vector<std::byte> band(9u * 2u * 2u);
        for (const std::uint32_t firstRow : {0u, 1u})
        {
            const an::Result<an::ImageLayout> layout = reader->readRows(execution, firstRow, 2u, band);
            ASSERT_TRUE(layout.hasValue()) << layout.status().message();
            EXPECT_EQ(layout->extent().width(), 9u);
            EXPECT_EQ(layout->extent().height(), 2u);
        }
        // The header at the open, and again with each band: an adapter that decodes no rows into caller memory is read
        // a band at a time through the facade, which reads the header it decodes the region under.
        EXPECT_EQ(named->probes.load(), 3u);
        EXPECT_EQ(named->regionDecodes.load(), 2u);

        const std::shared_ptr<const an::CodecRegistry> png =
            registryOf({std::make_shared<CountingCodec>(an::CodecFormat::Png)});
        const an::Result<an::ImageRowReader> refused =
            an::ImageRowReader::open(engine, execution, *png, {.path = "m42.fits"});
        EXPECT_EQ(refused.status().code(), an::ErrorCode::Unsupported);
        EXPECT_EQ(an::codecFailureReason(refused.status()), an::CodecFailure::Unavailable);
    }
    EXPECT_EQ(own->probes.load() + own->decodes.load() + own->regionDecodes.load(), 0u);
}
