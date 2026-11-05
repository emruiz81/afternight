// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/ImageCodec.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <AfterNightEngine/Runtime/TaskScheduler.h>
#include <cstring>
#include <memory>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string>

namespace
{
an::RawFrameDescriptor description(std::uint32_t width = 7u, std::uint32_t height = 5u)
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

class MemoryCodec final : public an::ImageCodec
{
public:
    an::CodecCapabilities capabilities() const override
    {
        if (throwCapabilities)
            throw 42;
        return caps;
    }
    an::ImageLayout layoutFor(const an::RawFrameDescriptor& value) const
    {
        const auto bytes = value.sampleFormat().sizeBytes;
        const auto row = static_cast<std::uint64_t>(value.extent().width()) * bytes + padding;
        return *an::ImageLayout::createWithStrides({value.extent(), bytes, 1u, value.extent().channelCount(), 1u}, row,
                                                   row * value.extent().height(), an::defaultEngineLimits());
    }
    an::Result<an::CodecImageInfo> probe(const an::EngineContext&, const an::ExecutionContext&,
                                         const an::CodecReadRequest& request) const override
    {
        ++probes;
        if (trackPaths)
            lastPath = request.path;
        if (throwProbe)
            throw std::runtime_error("decoder failure");
        if (probeFailure && (*probeFailure != an::CodecFailure::PayloadUnreadable || request.validatePayload))
            return an::codecFailure(an::ErrorCode::InvalidData, *probeFailure, request.format, "MemoryCodec::probe");
        auto result = an::CodecImageInfo{source, layoutFor(source), imageCount, sourceMetadata};
        result.disposition = request.validatePayload ? probeDisposition : an::CodecProbeDisposition::DecodeRequired;
        if (badProbeLayout)
            result.decodedLayout = layoutFor(description(3u));
        return result;
    }
    an::Result<an::CodecDecodeResult> decode(const an::EngineContext& engine, const an::ExecutionContext& execution,
                                             const an::CodecReadRequest&) const override
    {
        ++decodes;
        if (throwDecode)
            throw std::runtime_error("decode exception");
        if (failDecode)
            return an::Status::failure(
                an::ErrorCode::InvalidData,
                {.message = "not parsed by a caller",
                 .domain = an::ErrorDomain::Io,
                 .context = {an::StatusContextField::ofNumber(an::ContextKey::LibraryCode, -91)}});
        if (nestedWork)
        {
            const auto status = an::parallelFor(execution, 101u, {2u, 1u}, [&](an::TaskRange range) {
                nestedCount.fetch_add(range.end - range.begin);
                return an::Status::success();
            });
            if (status.failed())
                return status;
        }
        return makeFrame(engine, wrongDescriptor ? description(6u) : source);
    }
    an::Result<an::CodecDecodeResult> decodeRegion(const an::EngineContext& engine, const an::ExecutionContext&,
                                                   const an::CodecReadRequest&,
                                                   const an::CodecRegion& region) const override
    {
        ++regions;
        if (throwRegion)
            throw 7;
        auto request = source.toRequest();
        request.extent =
            *an::ImageExtent::create(region.width, region.height, source.extent().channelCount(), engine.limits());
        if (request.cfa.isMosaiced())
        {
            const auto block = an::cfaPatternBlockSize(request.cfa.pattern());
            const auto x = (region.x % block + request.cfa.originX()) % block;
            const auto y = (region.y % block + request.cfa.originY()) % block;
            request.cfa = request.cfa.hasFilterTable()
                              ? *an::CfaDescriptor::create(request.cfa.filterTable().entries(), x, y)
                              : *an::CfaDescriptor::create(request.cfa.pattern(), x, y);
        }
        return makeFrame(engine, *an::RawFrameDescriptor::create(request));
    }
    an::Result<an::CodecEncodeReport> encode(const an::EngineContext&, const an::ExecutionContext&,
                                             const an::CodecEncodeRequest& request, const an::RawFrame& frame,
                                             const an::ImageMetadata&) const override
    {
        ++encodes;
        if (trackPaths)
            lastPath = request.pendingPath;
        sawIntent = request.intent != nullptr;
        if (throwEncode)
            throw std::runtime_error("encoder failure");
        return an::CodecEncodeReport{frame.bytes().size()};
    }
    an::Result<an::CodecDecodeResult> makeFrame(const an::EngineContext& engine,
                                                const an::RawFrameDescriptor& value) const
    {
        const auto layout = layoutFor(value);
        an::Result<an::SampleBuffer> buffer = an::Status::failure(an::ErrorCode::Internal);
        if (external != nullptr)
        {
            buffer = unsafeBorrow ? an::SampleBuffer::borrowConst(std::span<const std::byte>{*external})
                                  : an::SampleBuffer::external(an::SampleBufferOwnership::DecoderOwned,
                                                               external->data(), external->size(), external, false);
        }
        else
            buffer = an::SampleBuffer::createOwned(allocationEngine != nullptr ? *allocationEngine : engine, layout,
                                                   an::StorageInitialization::Zeroed);
        if (!buffer.hasValue())
            return buffer.status();
        auto frame = an::RawFrame::create(value, layout, std::move(*buffer));
        if (!frame.hasValue())
            return frame.status();
        if (cancelAfterDecode != nullptr)
            cancelAfterDecode->request_stop();
        return an::CodecDecodeResult{std::move(*frame), sourceMetadata};
    }
    an::CodecCapabilities caps{.format = an::CodecFormat::Fits,
                               .decodeSamples = an::codecSampleBit({2u, an::RawSampleClass::UnsignedInteger}),
                               .encodeSamples = an::codecSampleBit({2u, an::RawSampleClass::UnsignedInteger}),
                               .regionDecode = true};
    an::RawFrameDescriptor source = description();
    an::ImageMetadata sourceMetadata{};
    an::CodecProbeDisposition probeDisposition = an::CodecProbeDisposition::DecodeRequired;
    std::optional<an::CodecFailure> probeFailure;
    std::shared_ptr<std::vector<std::byte>> external;
    bool throwProbe = false, wrongDescriptor = false, throwCapabilities = false, throwDecode = false;
    bool throwEncode = false, throwRegion = false, failDecode = false, unsafeBorrow = false;
    bool badProbeLayout = false, trackPaths = true, nestedWork = false;
    std::uint64_t padding = 0u, imageCount = 1u;
    std::stop_source* cancelAfterDecode = nullptr;
    const an::EngineContext* allocationEngine = nullptr;
    mutable std::atomic<std::uint32_t> probes{0u}, decodes{0u}, regions{0u}, encodes{0u};
    mutable std::atomic<std::uint64_t> nestedCount{0u};
    mutable bool sawIntent = false;
    mutable std::string lastPath;
};

an::EngineContext makeEngine(const std::shared_ptr<MemoryCodec>& codec)
{
    const std::array<std::shared_ptr<const an::ImageCodec>, 1u> entries{codec};
    const auto registry = an::CodecRegistry::create(entries);
    EXPECT_TRUE(registry.hasValue());
    auto engine = an::EngineContext::create({.maxConcurrency = 2u, .codecRegistry = *registry});
    EXPECT_TRUE(engine.hasValue());
    return std::move(*engine);
}

TEST(CodecRegistryTest, RecognizesFormatsWithoutClaimingThatDecodersAreInstalled)
{
    EXPECT_STREQ(an::codecFormatName(an::CodecFormat::Unknown), "unknown");
    EXPECT_EQ(an::codecFormatFromPath("C:/images/M42.FiTs"), an::CodecFormat::Fits);
    EXPECT_EQ(an::codecFormatFromPath("/images/a.RAF"), an::CodecFormat::Raw);
    EXPECT_EQ(an::codecFormatFromPath("/a.fits/no_extension"), an::CodecFormat::Unknown);
    auto engine = an::EngineContext::create({.maxConcurrency = 1u});
    auto execution = an::ExecutionContext::create(*engine, {});
    const auto result = an::probeImage(*engine, *execution, {.path = "x.fits"});
    EXPECT_EQ(result.status().code(), an::ErrorCode::Unsupported);
    EXPECT_EQ(an::codecFailureReason(result.status()), an::CodecFailure::Unavailable);
}

TEST(CodecRegistryTest, RegistrationsAreImmutableOwnedAndDistinctBetweenContexts)
{
    auto codec = std::make_shared<MemoryCodec>();
    auto engine = makeEngine(codec);
    auto png = std::make_shared<MemoryCodec>();
    png->caps.format = an::CodecFormat::Png;
    auto other = makeEngine(png);
    EXPECT_TRUE(engine.codecRegistry()->capabilities(an::CodecFormat::Fits).has_value());
    EXPECT_FALSE(engine.codecRegistry()->capabilities(an::CodecFormat::Png).has_value());
    EXPECT_FALSE(other.codecRegistry()->capabilities(an::CodecFormat::Fits).has_value());
    EXPECT_TRUE(other.codecRegistry()->capabilities(an::CodecFormat::Png).has_value());
    codec->caps.regionDecode = false;
    EXPECT_TRUE(engine.codecRegistry()->capabilities(an::CodecFormat::Fits)->regionDecode);
    std::weak_ptr<const an::ImageCodec> lifetime = codec;
    codec.reset();
    EXPECT_FALSE(lifetime.expired());
    EXPECT_TRUE(engine.shutdown().ok());
    EXPECT_TRUE(lifetime.expired());
    EXPECT_TRUE(other.codecRegistry()->capabilities(an::CodecFormat::Png).has_value());
}

TEST(CodecRegistryTest, RejectsDuplicateAndMalformedRegistrations)
{
    auto codec = std::make_shared<MemoryCodec>();
    std::array<std::shared_ptr<const an::ImageCodec>, 2u> entries{codec, codec};
    EXPECT_EQ(an::codecFailureReason(an::CodecRegistry::create(entries).status()), an::CodecFailure::DuplicateFormat);
    entries[1].reset();
    EXPECT_EQ(an::CodecRegistry::create(entries).status().code(), an::ErrorCode::InvalidArgument);
}

TEST(CodecRegistryTest, DecodesOwnedFramesAndUsesExplicitRegionsWithoutFallback)
{
    auto codec = std::make_shared<MemoryCodec>();
    auto engine = makeEngine(codec);
    auto execution = an::ExecutionContext::create(engine, {});
    auto full = an::decodeImage(engine, *execution, {.path = "x.fits"});
    ASSERT_TRUE(full.hasValue()) << full.status().message();
    EXPECT_TRUE(full->frame.checkAdoptable().ok());
    auto region = an::decodeImageRegion(engine, *execution, {.path = "x.fits"}, {2u, 1u, 3u, 2u});
    ASSERT_TRUE(region.hasValue()) << region.status().message();
    EXPECT_EQ(region->frame.descriptor()->extent().width(), 3u);
    EXPECT_EQ(codec->decodes.load(), 1u);
    EXPECT_EQ(codec->regions.load(), 1u);
    EXPECT_EQ(an::decodeImageRegion(engine, *execution, {.path = "x.fits"}, {7u, 0u, 1u, 1u}).status().code(),
              an::ErrorCode::OutOfRange);
    EXPECT_EQ(codec->regions.load(), 1u);
}

TEST(CodecRegistryTest, TranslatesExceptionsAndRefusesDifferentReturnedDescriptors)
{
    auto codec = std::make_shared<MemoryCodec>();
    auto engine = makeEngine(codec);
    auto execution = an::ExecutionContext::create(engine, {});
    codec->throwProbe = true;
    EXPECT_EQ(an::probeImage(engine, *execution, {.path = "x.fits"}).status().code(), an::ErrorCode::ExternalFailure);
    codec->throwProbe = false;
    codec->wrongDescriptor = true;
    const auto result = an::decodeImage(engine, *execution, {.path = "x.fits"});
    EXPECT_EQ(an::codecFailureReason(result.status()), an::CodecFailure::DescriptorMismatch);
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
}

TEST(CodecRegistryTest, CancellationAfterDecodeDiscardsTheFrame)
{
    auto codec = std::make_shared<MemoryCodec>();
    auto engine = makeEngine(codec);
    std::stop_source stop;
    codec->cancelAfterDecode = &stop;
    an::ExecutionOptions options;
    options.stopToken = stop.get_token();
    auto execution = an::ExecutionContext::create(engine, options);
    const auto result = an::decodeImage(engine, *execution, {.path = "x.fits"});
    EXPECT_EQ(result.status().code(), an::ErrorCode::Cancelled);
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
}

TEST(CodecRegistryTest, EntireExtensionVocabularyIsCaseInsensitive)
{
    const std::array<const char*, 8u> names{"unknown", "FITS", "XISF", "RAW", "PNG", "JPEG", "TIFF", "Dump"};
    for (std::uint32_t i = 0u; i < names.size(); ++i)
        EXPECT_STREQ(an::codecFormatName(static_cast<an::CodecFormat>(i)), names[i]);
    for (std::uint32_t i = 1u; i <= an::kCodecFormatCount; ++i)
    {
        const auto format = static_cast<an::CodecFormat>(i);
        for (const auto extension : an::codecExtensions(format))
        {
            std::string path = "C:\\raw.folder\\\xc3\xa1" + std::string(extension);
            for (auto& value : path)
                if (value >= 'a' && value <= 'z')
                    value = static_cast<char>(value - ('a' - 'A'));
            EXPECT_EQ(an::codecFormatFromPath(path), format);
        }
        EXPECT_NE(std::string_view(an::codecFormatName(format)), "UnknownCodecFormat");
    }
    EXPECT_EQ(an::codecFormatFromPath("a.fits/"), an::CodecFormat::Unknown);
    EXPECT_EQ(an::codecFormatFromPath("a.fits\\file"), an::CodecFormat::Unknown);
    EXPECT_EQ(an::codecFormatFromPath(std::string_view("x.fits\0.png", 11u)), an::CodecFormat::Unknown);
    EXPECT_TRUE(an::codecExtensions(an::CodecFormat::Dump).empty());
    EXPECT_FALSE(an::isCodecFormat(static_cast<an::CodecFormat>(255u)));
}

TEST(CodecRegistryTest, CapabilityBitsCoverEveryRawRepresentationAndRejectUnknownBits)
{
    std::uint32_t bits = 0u;
    for (std::uint32_t cls = 0u; cls < 4u; ++cls)
        for (const auto size : {1u, 2u, 4u, 8u, 16u})
        {
            const auto bit = an::codecSampleBit({size, static_cast<an::RawSampleClass>(cls)});
            EXPECT_NE(bit, 0u);
            EXPECT_EQ(bits & bit, 0u);
            bits |= bit;
        }
    EXPECT_EQ(bits, (1u << 20u) - 1u);
    EXPECT_EQ(an::codecSampleBit({3u, an::RawSampleClass::UnsignedInteger}), 0u);
    auto codec = std::make_shared<MemoryCodec>();
    const auto valid = codec->caps;
    for (unsigned bad = 0u; bad < 6u; ++bad)
    {
        codec->caps = valid;
        switch (bad)
        {
        case 0u:
            codec->caps.format = an::CodecFormat::Unknown;
            break;
        case 1u:
            codec->caps.decodeSamples = 1u << 31u;
            break;
        case 2u:
            codec->caps.encodeLayouts = 8u;
            break;
        case 3u:
            codec->caps.maxChannels = 0u;
            break;
        case 4u:
            codec->caps.decodeSamples = 0u;
            break;
        case 5u:
            codec->caps.decodeLayouts = 0u;
            break;
        }
        const std::array<std::shared_ptr<const an::ImageCodec>, 1u> entries{codec};
        EXPECT_EQ(an::codecFailureReason(an::CodecRegistry::create(entries).status()),
                  an::CodecFailure::InvalidCapabilities);
    }
}

TEST(CodecRegistryTest, EmptyAndOversizedRegistriesAndThrowingCapabilityCallbacksAreTyped)
{
    const auto empty = an::CodecRegistry::create({});
    ASSERT_TRUE(empty.hasValue());
    EXPECT_EQ((*empty)->size(), 0u);
    EXPECT_FALSE((*empty)->capabilities(an::CodecFormat::Fits).has_value());
    std::array<std::shared_ptr<const an::ImageCodec>, 8u> tooMany{};
    const auto limited = an::CodecRegistry::create(tooMany);
    EXPECT_EQ(limited.status().code(), an::ErrorCode::LimitExceeded);
    EXPECT_EQ(limited.status().findContext(an::ContextKey::Limit)->count, an::kCodecFormatCount);
    auto codec = std::make_shared<MemoryCodec>();
    codec->throwCapabilities = true;
    const std::array<std::shared_ptr<const an::ImageCodec>, 1u> entries{codec};
    EXPECT_EQ(an::CodecRegistry::create(entries).status().code(), an::ErrorCode::ExternalFailure);
}

TEST(CodecRegistryTest, ExplicitSelectionAndUtf8PathsReachOnlyTheSelectedCodec)
{
    auto codec = std::make_shared<MemoryCodec>();
    auto engine = makeEngine(codec);
    auto execution = an::ExecutionContext::create(engine, {});
    const std::string path = "C:/\xc3\xa1/\xe6\x98\x9f.unknown";
    EXPECT_TRUE(an::probeImage(engine, *execution, {.path = path, .format = an::CodecFormat::Fits}).hasValue());
    EXPECT_EQ(codec->lastPath, path);
    const auto count = codec->probes.load();
    for (const auto& bad : {std::string{}, std::string("x\0.fits", 7u), std::string("\xc0\x80.fits"),
                            std::string("\xed\xa0\x80.fits"), std::string("\xf4\x90\x80\x80.fits")})
        EXPECT_EQ(an::probeImage(engine, *execution, {.path = bad}).status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::codecFailureReason(an::probeImage(engine, *execution, {.path = "a.png"}).status()),
              an::CodecFailure::Unavailable);
    EXPECT_EQ(an::codecFailureReason(an::probeImage(engine, *execution, {.path = "a.bin"}).status()),
              an::CodecFailure::UnknownFormat);
    EXPECT_EQ(codec->probes.load(), count);
}

TEST(CodecRegistryTest, RefusesUnsupportedRegionBeforeProbingAndNeverFallsBack)
{
    auto codec = std::make_shared<MemoryCodec>();
    codec->caps.regionDecode = false;
    auto engine = makeEngine(codec);
    auto execution = an::ExecutionContext::create(engine, {});
    EXPECT_EQ(an::codecFailureReason(
                  an::decodeImageRegion(engine, *execution, {.path = "x.fits"}, {0u, 0u, 1u, 1u}).status()),
              an::CodecFailure::UnsupportedOperation);
    EXPECT_EQ(codec->probes.load(), 0u);
    EXPECT_EQ(codec->decodes.load(), 0u);
}

TEST(CodecRegistryTest, ProbeOnlyAdapterDoesNotAdvertisePixels)
{
    auto codec = std::make_shared<MemoryCodec>();
    codec->caps.decodeSamples = codec->caps.encodeSamples = 0u;
    codec->caps.regionDecode = false;
    auto engine = makeEngine(codec);
    auto execution = an::ExecutionContext::create(engine, {});
    EXPECT_TRUE(an::probeImage(engine, *execution, {.path = "x.fits"}).hasValue());
    EXPECT_EQ(an::decodeImage(engine, *execution, {.path = "x.fits"}).status().code(), an::ErrorCode::Unsupported);
    EXPECT_EQ(codec->decodes.load(), 0u);
}

TEST(CodecRegistryTest, ProbeReadabilityRetainsAllFourOutcomesWithoutDecoding)
{
    auto codec = std::make_shared<MemoryCodec>();
    auto engine = makeEngine(codec);
    auto execution = an::ExecutionContext::create(engine, {});
    const auto header = an::probeImage(engine, *execution, {.path = "x.fits"});
    ASSERT_TRUE(header.hasValue());
    EXPECT_EQ(header->disposition, an::CodecProbeDisposition::DecodeRequired);
    codec->probeDisposition = an::CodecProbeDisposition::PayloadValidatedWithoutDecode;
    const auto validated = an::probeImage(engine, *execution, {.path = "x.fits"});
    ASSERT_TRUE(validated.hasValue());
    EXPECT_EQ(validated->disposition, an::CodecProbeDisposition::PayloadValidatedWithoutDecode);
    for (const auto reason : {an::CodecFailure::MetadataUnreadable, an::CodecFailure::PayloadUnreadable})
    {
        codec->probeFailure = reason;
        const auto failed = an::probeImage(engine, *execution, {.path = "x.fits"});
        EXPECT_EQ(failed.status().code(), an::ErrorCode::InvalidData);
        EXPECT_EQ(an::codecFailureReason(failed.status().rootCause()), reason);
    }
    // A malformed payload does not prevent separately retrieving its readable header.
    const auto metadata = an::probeImage(engine, *execution, {.path = "x.fits", .validatePayload = false});
    ASSERT_TRUE(metadata.hasValue());
    EXPECT_EQ(metadata->descriptor, header->descriptor);
    EXPECT_EQ(metadata->disposition, an::CodecProbeDisposition::DecodeRequired);
    // Internal decode preflight reads the header only; the decoder validates its own payload.
    EXPECT_TRUE(an::decodeImage(engine, *execution, {.path = "x.fits"}).hasValue());
    EXPECT_EQ(codec->decodes.load(), 1u);
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
    codec->decodes = 0u;
    codec->probeFailure.reset();
    codec->probeDisposition = static_cast<an::CodecProbeDisposition>(255u);
    EXPECT_EQ(an::codecFailureReason(an::probeImage(engine, *execution, {.path = "x.fits"}).status()),
              an::CodecFailure::DescriptorMismatch);
    EXPECT_EQ(codec->decodes.load(), 0u);
    EXPECT_EQ(engine.memoryResource()->counters().peakBytes, 70u);
}

TEST(CodecRegistryTest, EmptyAndOverflowingRegionsNeverReachTheDecoder)
{
    auto codec = std::make_shared<MemoryCodec>();
    auto engine = makeEngine(codec);
    auto execution = an::ExecutionContext::create(engine, {});
    for (const auto region : {an::CodecRegion{}, an::CodecRegion{0u, 0u, 7u, 0u},
                              an::CodecRegion{0xffffffffu, 0u, 2u, 1u}, an::CodecRegion{0u, 0xffffffffu, 1u, 2u}})
        EXPECT_EQ(an::decodeImageRegion(engine, *execution, {.path = "x.fits"}, region).status().code(),
                  an::ErrorCode::OutOfRange);
    EXPECT_EQ(codec->regions.load(), 0u);
}

TEST(CodecRegistryTest, ContextMismatchAndExpiredDeadlinesDoNotInvokeTheAdapter)
{
    auto codec = std::make_shared<MemoryCodec>();
    auto engine = makeEngine(codec);
    auto other = makeEngine(codec);
    auto execution = an::ExecutionContext::create(engine, {});
    EXPECT_EQ(an::probeImage(other, *execution, {.path = "x.fits"}).status().code(),
              an::ErrorCode::IncompatibleArguments);
    auto expired =
        an::ExecutionContext::create(other, {.deadline = std::chrono::steady_clock::now() - std::chrono::seconds(1)});
    EXPECT_EQ(an::probeImage(other, *expired, {.path = "x.fits"}).status().code(), an::ErrorCode::DeadlineExceeded);
    EXPECT_EQ(codec->probes.load(), 0u);
}

TEST(CodecRegistryTest, ProbeLimitsAndImageSelectionAreCheckedBeforePixelAllocation)
{
    auto codec = std::make_shared<MemoryCodec>();
    auto engine = makeEngine(codec);
    auto options = an::EngineContextOptions{.maxConcurrency = 1u};
    options.limits.maxImageWidth = 3u;
    const std::array<std::shared_ptr<const an::ImageCodec>, 1u> entries{codec};
    options.codecRegistry = *an::CodecRegistry::create(entries);
    auto narrow = an::EngineContext::create(options);
    auto limited = an::ExecutionContext::create(*narrow, {});
    EXPECT_EQ(an::decodeImage(*narrow, *limited, {.path = "x.fits"}).status().code(), an::ErrorCode::LimitExceeded);
    auto execution = an::ExecutionContext::create(engine, {});
    EXPECT_EQ(an::decodeImage(engine, *execution, {.path = "x.fits", .imageIndex = 1u}).status().code(),
              an::ErrorCode::OutOfRange);
    codec->imageCount = 0u;
    EXPECT_EQ(an::probeImage(engine, *execution, {.path = "x.fits"}).status().code(), an::ErrorCode::InvalidData);
    EXPECT_EQ(codec->decodes.load(), 0u);
    codec->imageCount = 1u;
    codec->badProbeLayout = true;
    EXPECT_EQ(an::codecFailureReason(an::probeImage(engine, *execution, {.path = "x.fits"}).status()),
              an::CodecFailure::DescriptorMismatch);
}

TEST(CodecRegistryTest, DecodedExternalLifetimeSurvivesTheProviderAndBareBorrowsAreRefused)
{
    auto codec = std::make_shared<MemoryCodec>();
    codec->external = std::make_shared<std::vector<std::byte>>(70u, std::byte{0x5a});
    auto engine = makeEngine(codec);
    auto execution = an::ExecutionContext::create(engine, {});
    codec->unsafeBorrow = true;
    EXPECT_EQ(an::codecFailureReason(an::decodeImage(engine, *execution, {.path = "x.fits"}).status()),
              an::CodecFailure::UnsafeLifetime);
    codec->unsafeBorrow = false;
    auto decoded = an::decodeImage(engine, *execution, {.path = "x.fits"});
    ASSERT_TRUE(decoded.hasValue());
    std::weak_ptr<std::vector<std::byte>> lifetime = codec->external;
    codec->external.reset();
    EXPECT_FALSE(lifetime.expired());
    EXPECT_EQ(decoded->frame.bytes().front(), std::byte{0x5a});
    decoded->frame.release();
    EXPECT_TRUE(lifetime.expired());
}

TEST(CodecRegistryTest, ForeignIntegerWidthEndianAndPaddingAreCarriedWithoutNarrowing)
{
    auto codec = std::make_shared<MemoryCodec>();
    auto request = codec->source.toRequest();
    request.sampleFormat = {8u, an::RawSampleClass::UnsignedInteger};
    request.byteOrder = an::ByteOrder::Big;
    codec->source = *an::RawFrameDescriptor::create(request);
    codec->padding = 16u;
    codec->caps.decodeSamples = codec->caps.encodeSamples = an::codecSampleBit(request.sampleFormat);
    auto engine = makeEngine(codec);
    auto execution = an::ExecutionContext::create(engine, {});
    auto decoded = an::decodeImage(engine, *execution, {.path = "x.fits"});
    ASSERT_TRUE(decoded.hasValue());
    EXPECT_FALSE(decoded->frame.descriptor()->residentSampleType().has_value());
    EXPECT_EQ(decoded->frame.descriptor()->byteOrder(), an::ByteOrder::Big);
    EXPECT_EQ(decoded->frame.layout()->rowStrideBytes(), 72u);
    const auto saved = an::encodeImage(engine, *execution, {.pendingPath = "x.antmp", .format = an::CodecFormat::Fits},
                                       decoded->frame, {});
    ASSERT_TRUE(saved.hasValue());
    EXPECT_EQ(saved->bytesWritten, decoded->frame.bytes().size());
    EXPECT_FALSE(codec->sawIntent);
}

TEST(CodecRegistryTest, DecoderCannotChargeSamplesToAnotherContextsBudget)
{
    auto codec = std::make_shared<MemoryCodec>();
    auto engine = makeEngine(codec);
    auto foreign = an::EngineContext::create({.maxConcurrency = 1u});
    auto execution = an::ExecutionContext::create(engine, {});
    codec->allocationEngine = &*foreign;
    const auto decoded = an::decodeImage(engine, *execution, {.path = "x.fits"});
    EXPECT_EQ(an::codecFailureReason(decoded.status()), an::CodecFailure::ForeignAllocation);
    EXPECT_EQ(foreign->memoryResource()->counters().liveBytes, 0u);
    EXPECT_EQ(engine.memoryResource()->counters().peakBytes, 0u);
}

TEST(CodecRegistryTest, RegionPreservesCoordinateConventionAndMovesCfaPhase)
{
    auto codec = std::make_shared<MemoryCodec>();
    auto request = codec->source.toRequest();
    request.cfa = *an::CfaDescriptor::create(an::CfaPattern::Rggb, 0u, 0u);
    request.coordinateOrigin = an::CoordinateOrigin::BottomLeft;
    codec->source = *an::RawFrameDescriptor::create(request);
    auto engine = makeEngine(codec);
    auto execution = an::ExecutionContext::create(engine, {});
    const auto region = an::decodeImageRegion(engine, *execution, {.path = "x.fits"}, {1u, 1u, 3u, 2u});
    ASSERT_TRUE(region.hasValue());
    const auto value = *region->frame.descriptor();
    EXPECT_EQ(value.coordinateOrigin(), an::CoordinateOrigin::BottomLeft);
    EXPECT_EQ(value.cfa().originX(), 1u);
    EXPECT_EQ(value.cfa().originY(), 1u);
    EXPECT_EQ(value.cfa().colorAt(0u, 0u), an::CfaColor::Blue);
}

TEST(CodecRegistryTest, EncoderReceivesExplicitIntentAndRefusesConversionBeforeCalling)
{
    auto codec = std::make_shared<MemoryCodec>();
    auto engine = makeEngine(codec);
    auto execution = an::ExecutionContext::create(engine, {});
    auto decoded = an::decodeImage(engine, *execution, {.path = "x.fits"});
    ASSERT_TRUE(decoded.hasValue());
    auto intent = *an::OutputIntent::create({.sampleType = an::SampleType::UInt16});
    const auto metadata = an::ImageMetadata::create({}, {}, {}, {}, {}, intent);
    EXPECT_TRUE(an::encodeImage(engine, *execution, {.pendingPath = "private.antmp", .format = an::CodecFormat::Fits},
                                decoded->frame, metadata)
                    .hasValue());
    EXPECT_FALSE(codec->sawIntent);
    EXPECT_TRUE(an::encodeImage(engine, *execution,
                                {.pendingPath = "private.antmp", .format = an::CodecFormat::Fits, .intent = &intent},
                                decoded->frame, {})
                    .hasValue());
    EXPECT_TRUE(codec->sawIntent);
    const auto count = codec->encodes.load();
    intent = *an::OutputIntent::create({.sampleType = an::SampleType::Float32});
    EXPECT_EQ(an::encodeImage(engine, *execution,
                              {.pendingPath = "private.antmp", .format = an::CodecFormat::Fits, .intent = &intent},
                              decoded->frame, {})
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(codec->encodes.load(), count);
}

TEST(CodecRegistryTest, SourceMetadataCannotAcquireAnOutputIntent)
{
    auto codec = std::make_shared<MemoryCodec>();
    codec->sourceMetadata = an::ImageMetadata::create(
        {}, {}, {}, {}, {}, *an::OutputIntent::create({.sampleType = an::SampleType::UInt16}));
    auto engine = makeEngine(codec);
    auto execution = an::ExecutionContext::create(engine, {});
    EXPECT_EQ(an::codecFailureReason(an::decodeImage(engine, *execution, {.path = "x.fits"}).status()),
              an::CodecFailure::InvalidMetadata);
    EXPECT_EQ(codec->decodes.load(), 0u);
}

TEST(CodecRegistryTest, EveryPixelCallbackTranslatesExceptionsAndRetainsTypedCauses)
{
    auto codec = std::make_shared<MemoryCodec>();
    auto engine = makeEngine(codec);
    auto execution = an::ExecutionContext::create(engine, {});
    codec->failDecode = true;
    const auto failed = an::decodeImage(engine, *execution, {.path = "x.fits"});
    EXPECT_EQ(failed.status().code(), an::ErrorCode::InvalidData);
    ASSERT_TRUE(failed.status().hasCause());
    EXPECT_EQ(failed.status().rootCause().findContext(an::ContextKey::LibraryCode)->number, -91);
    codec->failDecode = false;
    codec->throwDecode = true;
    EXPECT_EQ(an::decodeImage(engine, *execution, {.path = "x.fits"}).status().code(), an::ErrorCode::ExternalFailure);
    codec->throwDecode = false;
    codec->throwRegion = true;
    EXPECT_EQ(an::decodeImageRegion(engine, *execution, {.path = "x.fits"}, {0u, 0u, 1u, 1u}).status().code(),
              an::ErrorCode::ExternalFailure);
    auto decoded = an::decodeImage(engine, *execution, {.path = "x.fits"});
    ASSERT_TRUE(decoded.hasValue());
    codec->throwEncode = true;
    EXPECT_EQ(an::encodeImage(engine, *execution, {.pendingPath = "private.antmp", .format = an::CodecFormat::Fits},
                              decoded->frame, {})
                  .status()
                  .code(),
              an::ErrorCode::ExternalFailure);
}

TEST(CodecRegistryTest, BudgetFailureReleasesStorageAndNestedWorkUsesTheExecution)
{
    auto codec = std::make_shared<MemoryCodec>();
    const std::array<std::shared_ptr<const an::ImageCodec>, 1u> entries{codec};
    an::EngineContextOptions options{.maxConcurrency = 2u};
    options.codecRegistry = *an::CodecRegistry::create(entries);
    options.limits.memoryBudgetBytes = 69u;
    auto engine = an::EngineContext::create(options);
    auto execution = an::ExecutionContext::create(*engine, {});
    EXPECT_EQ(an::decodeImage(*engine, *execution, {.path = "x.fits"}).status().code(),
              an::ErrorCode::MemoryBudgetExceeded);
    EXPECT_EQ(engine->memoryResource()->counters().liveBytes, 0u);
    auto enough = makeEngine(codec);
    auto operation = an::ExecutionContext::create(enough, {.maxConcurrency = 1u});
    codec->nestedWork = true;
    EXPECT_TRUE(an::decodeImage(enough, *operation, {.path = "x.fits"}).hasValue());
    EXPECT_EQ(codec->nestedCount.load(), 101u);
}

TEST(CodecRegistryTest, SharedImmutableRegistryRunsIndependentOperationsConcurrently)
{
    auto codec = std::make_shared<MemoryCodec>();
    codec->trackPaths = false;
    auto engine = makeEngine(codec);
    auto first = an::ExecutionContext::create(engine, {.maxConcurrency = 1u});
    auto second = an::ExecutionContext::create(engine, {.maxConcurrency = 1u});
    std::atomic<unsigned> successes{0u};
    const auto work = [&](const an::ExecutionContext& execution) {
        for (unsigned i = 0u; i < 20u; ++i)
            if (an::decodeImage(engine, execution, {.path = "x.fits"}).hasValue())
                ++successes;
    };
    std::thread a([&] { work(*first); });
    std::thread b([&] { work(*second); });
    a.join();
    b.join();
    EXPECT_EQ(successes.load(), 40u);
    EXPECT_EQ(codec->decodes.load(), 40u);
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
    EXPECT_EQ(engine.shutdown().code(), an::ErrorCode::Busy);
}
} // namespace
