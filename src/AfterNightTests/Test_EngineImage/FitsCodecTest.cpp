// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/IO/FitsCodec.h>

#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/CodecError.h>
#include <AfterNightEngine/IO/OutputTransaction.h>
#include <AfterNightEngine/IO/ArtifactIdentity.h>
#include <AfterNightEngine/IO/ImageCodec.h>
#include <AfterNightEngine/Algorithms/Astrometry/WcsCards.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/ProgressSink.h>
#include <AfterNightEngine/Runtime/TraceSink.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <limits>
#include <thread>
#include <stop_token>

namespace
{
class FitsCodecTest : public testing::Test
{
protected:
    void SetUp() override
    {
        directory = std::filesystem::temp_directory_path() /
                    ("an-fits-suite-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(directory);
        auto created = makeEngine();
        ASSERT_TRUE(created.hasValue());
        engine = std::move(*created);
    }
    void TearDown() override
    {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }
    an::Result<an::EngineContext> makeEngine(an::FitsInteger64Policy policy = an::FitsInteger64Policy::CheckedFloat64,
                                             std::uint64_t budget = 0u)
    {
        auto adapter = an::createFitsCodec(policy);
        if (!adapter.hasValue())
            return adapter.status();
        std::array adapters{*adapter};
        auto registry = an::CodecRegistry::create(adapters);
        if (!registry.hasValue())
            return registry.status();
        an::EngineContextOptions options;
        options.codecRegistry = *registry;
        // Stated rather than the host's: a read is asked for up to eight workers, which an engine sized
        // to a smaller machine refuses.
        options.maxConcurrency = 8u;
        if (budget != 0u)
            options.limits.memoryBudgetBytes = budget;
        return an::EngineContext::create(options);
    }
    std::string path(const char* name = "frame.fits") const
    {
        const auto utf8 =
            (directory / std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(name)))).u8string();
        return {reinterpret_cast<const char*>(utf8.data()), utf8.size()};
    }
    static an::CodecCard card(std::string key, std::string value, std::string comment = {})
    {
        return *an::CodecCard::create(an::CodecNamespace::Fits, std::move(key), std::move(value), std::move(comment));
    }
    static std::vector<std::byte> readBytes(const std::string& name)
    {
        std::ifstream input(std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(name.c_str()))),
                            std::ios::binary);
        const std::vector<char> chars((std::istreambuf_iterator<char>(input)), {});
        std::vector<std::byte> bytes(chars.size());
        std::memcpy(bytes.data(), chars.data(), chars.size());
        return bytes;
    }
    template <typename T> static std::vector<T> values(const an::RawFrame& frame)
    {
        const auto layout = *frame.layout();
        std::vector<T> result;
        for (std::uint32_t p = 0u; p < layout.planeCount(); ++p)
            for (std::uint32_t y = 0u; y < layout.extent().height(); ++y)
                for (std::uint32_t x = 0u; x < layout.extent().width(); ++x)
                {
                    T value;
                    std::memcpy(&value,
                                frame.bytes().data() + p * layout.planeStrideBytes() + y * layout.rowStrideBytes() +
                                    x * sizeof(T),
                                sizeof(T));
                    result.push_back(value);
                }
        return result;
    }
    template <typename T>
    an::RawFrame frame(const std::vector<T>& data, std::uint32_t width, std::uint32_t height,
                       std::uint32_t channels = 1u, an::CfaDescriptor cfa = {},
                       an::CoordinateOrigin origin = an::CoordinateOrigin::TopLeft, bool padded = false,
                       an::ByteOrder order = an::ByteOrder::Native)
    {
        const auto extent = *an::ImageExtent::create(width, height, channels, engine.limits());
        const an::RawSampleFormat format{sizeof(T), std::is_floating_point_v<T> ? an::RawSampleClass::FloatingPoint
                                                    : std::is_signed_v<T>       ? an::RawSampleClass::SignedInteger
                                                                                : an::RawSampleClass::UnsignedInteger};
        const auto descriptor = *an::RawFrameDescriptor::create(
            {extent, format, order, channels == 1u ? an::ChannelSchema::Mono : an::ChannelSchema::Rgb,
             an::ColorSpace::Unspecified,
             std::is_floating_point_v<T> ? an::NumericDomain::PhysicalScaled : an::NumericDomain::RawSensorCode, cfa,
             origin, 1.0});
        const auto layout =
            *an::ImageLayout::create({extent, sizeof(T), 1u, channels, padded ? 64u : 1u}, engine.limits());
        auto buffer = an::SampleBuffer::createOwned(engine, layout, an::StorageInitialization::Zeroed);
        auto raw = an::RawFrame::create(descriptor, layout, std::move(*buffer));
        auto bytes = *raw->mutableBytes();
        for (std::uint32_t p = 0u; p < channels; ++p)
            for (std::uint32_t y = 0u; y < height; ++y)
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    auto* target =
                        bytes.data() + p * layout.planeStrideBytes() + y * layout.rowStrideBytes() + x * sizeof(T);
                    std::memcpy(target, &data[(p * height + y) * width + x], sizeof(T));
                    if (order == an::ByteOrder::Big && std::endian::native == std::endian::little)
                        std::reverse(target, target + sizeof(T));
                }
        return std::move(*raw);
    }
    an::Result<an::OutputReport> save(const an::RawFrame& source, const an::OutputIntent* intent = nullptr,
                                      const an::ImageMetadata& metadata = {},
                                      const an::ArtifactIdentity* identity = nullptr, const char* name = "frame.fits")
    {
        auto execution = an::ExecutionContext::create(engine, {});
        if (!execution.hasValue())
            return execution.status();
        const auto destination = path(name);
        auto result =
            an::writeImage(engine, *execution, {.destinationPath = destination, .intent = intent, .identity = identity},
                           source, metadata);
        lastError = std::string(result.status().message()) + " / " + std::string(result.status().rootCause().message());
        return result;
    }
    an::Result<an::CodecDecodeResult> decode(const char* name = "frame.fits")
    {
        auto execution = an::ExecutionContext::create(engine, {});
        if (!execution.hasValue())
            return execution.status();
        const auto source = path(name);
        return an::decodeImage(engine, *execution, {source});
    }
    template <typename T> void roundTrip(const std::vector<T>& data, int bitpix, const char* zero)
    {
        auto source = frame(data, static_cast<std::uint32_t>(data.size()), 1u);
        ASSERT_TRUE(save(source).hasValue()) << lastError;
        EXPECT_EQ(values<T>(source), data);
        auto decoded = decode();
        ASSERT_TRUE(decoded.hasValue());
        EXPECT_EQ(values<T>(decoded->frame), data);
        const auto* stored = decoded->metadata.codec().find(an::CodecNamespace::Fits, "BITPIX");
        ASSERT_NE(stored, nullptr);
        EXPECT_EQ(std::stoi(stored->value()), bitpix);
        const auto* offset = decoded->metadata.codec().find(an::CodecNamespace::Fits, "BZERO");
        if (zero != nullptr)
        {
            ASSERT_NE(offset, nullptr);
            EXPECT_DOUBLE_EQ(std::stod(offset->value()), std::stod(zero));
        }
        else
            EXPECT_EQ(offset, nullptr);
        EXPECT_FALSE(decoded->metadata.outputIntent().has_value());
    }
    std::filesystem::path directory;
    an::EngineContext engine;
    std::string lastError;
};

TEST_F(FitsCodecTest, AllResidentTypesKeepExtremaAndAdjacentCodes)
{
    roundTrip<std::uint8_t>({0, 1, 127, 128, 255}, 8, nullptr);
    roundTrip<std::int8_t>({-128, -1, 0, 1, 127}, 8, "-128");
    roundTrip<std::uint16_t>({0, 1, 32767, 32768, 65535}, 16, "32768");
    roundTrip<std::int16_t>({-32768, -1, 0, 1, 32767}, 16, nullptr);
    roundTrip<std::uint32_t>({0u, 16777217u, 2147483648u, 4294967295u}, 32, "2147483648");
    roundTrip<std::int32_t>({-2147483647 - 1, -16777217, 0, 2147483647}, 32, nullptr);
    roundTrip<float>({-123.5f, 0.0f, 0.125f, 1.5f}, -32, nullptr);
    roundTrip<double>({-1e100, 0.0, 1.0000000000000002, 1e100}, -64, nullptr);
}

TEST_F(FitsCodecTest, ExplicitScalingIsAppliedOnceAfterSignedStorageMapping)
{
    auto source = frame<std::uint16_t>({0, 1, 32768, 65535}, 4u, 1u);
    auto intent = an::OutputIntent::create({.sampleType = an::SampleType::UInt16,
                                            .scaling = an::OutputScaling::Explicit,
                                            .scale = 0.25,
                                            .zeroOffset = -12.0});
    ASSERT_TRUE(save(source, &*intent).hasValue()) << lastError;
    auto result = decode();
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result->frame.descriptor()->residentSampleType(), an::SampleType::Float64);
    EXPECT_EQ(values<double>(result->frame), (std::vector<double>{-12.0, -11.75, 8180.0, 16371.75}));
}

TEST_F(FitsCodecTest, Integer64ChecksRawMagnitudeBeforeScalingAndOffersExplicitAlternatives)
{
    const std::int64_t large = (std::int64_t{1} << 53) + 1;
    auto source = frame<std::int64_t>({-large, 1, large}, 3u, 1u);
    ASSERT_TRUE(save(source).hasValue()) << lastError;
    EXPECT_EQ(decode().status().code(), an::ErrorCode::OutOfRange);
    for (const auto policy : {an::FitsInteger64Policy::PreserveRaw, an::FitsInteger64Policy::AllowLossyFloat64})
    {
        auto context = makeEngine(policy);
        auto execution = an::ExecutionContext::create(*context, {});
        const auto sourcePath = path();
        auto result = an::decodeImage(*context, *execution, {sourcePath});
        ASSERT_TRUE(result.hasValue());
        if (policy == an::FitsInteger64Policy::PreserveRaw)
        {
            EXPECT_FALSE(result->frame.descriptor()->residentSampleType());
            EXPECT_EQ(values<std::int64_t>(result->frame), (std::vector<std::int64_t>{-large, 1, large}));
        }
        else
        {
            EXPECT_EQ(values<double>(result->frame),
                      (std::vector<double>{static_cast<double>(-large), 1.0, static_cast<double>(large)}));
            ASSERT_EQ(result->metadata.provenance().entries().size(), 1u);
            EXPECT_NE(result->metadata.provenance().entries()[0].detail().find("acknowledged"), std::string::npos);
        }
    }
}

TEST_F(FitsCodecTest, Integer64ExactRangeIncludesBothEndpoints)
{
    constexpr std::int64_t exact = std::int64_t{1} << 53;
    auto source = frame<std::int64_t>({-exact, -1, 0, 1, exact}, 5u, 1u);
    ASSERT_TRUE(save(source).hasValue()) << lastError;
    auto result = decode();
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(values<double>(result->frame),
              (std::vector<double>{-static_cast<double>(exact), -1, 0, 1, static_cast<double>(exact)}));
}

TEST_F(FitsCodecTest, Unsigned64OutputRetainsEveryStoredBitAndItsOffset)
{
    const std::uint64_t midpoint = std::uint64_t{1} << 63;
    auto source =
        frame<std::uint64_t>({0u, midpoint, midpoint + 1u, (std::numeric_limits<std::uint64_t>::max)()}, 4u, 1u);
    ASSERT_TRUE(save(source).hasValue()) << lastError;
    auto context = makeEngine(an::FitsInteger64Policy::PreserveRaw);
    auto execution = an::ExecutionContext::create(*context, {});
    const auto sourcePath = path();
    auto raw = an::decodeImage(*context, *execution, {sourcePath});
    ASSERT_TRUE(raw.hasValue());
    EXPECT_EQ(values<std::int64_t>(raw->frame),
              (std::vector<std::int64_t>{(std::numeric_limits<std::int64_t>::min)(), 0, 1,
                                         (std::numeric_limits<std::int64_t>::max)()}));
    EXPECT_EQ(raw->metadata.codec().sourceEncoding().zeroOffset, static_cast<double>(midpoint));
    const auto copyPath = path("copy.fits");
    ASSERT_TRUE(
        an::writeImage(*context, *execution, {.destinationPath = copyPath}, raw->frame, raw->metadata).hasValue());
    auto copy = an::decodeImage(*context, *execution, {copyPath});
    ASSERT_TRUE(copy.hasValue());
    EXPECT_EQ(values<std::int64_t>(copy->frame), values<std::int64_t>(raw->frame));
    EXPECT_EQ(copy->metadata.codec().sourceEncoding(), raw->metadata.codec().sourceEncoding());
}

TEST_F(FitsCodecTest, PlanarRgbPaddingAndForeignByteOrderAreExplicit)
{
    std::vector<std::uint16_t> input(45u);
    for (std::size_t i = 0u; i < input.size(); ++i)
        input[i] = static_cast<std::uint16_t>(i * 123u);
    auto source = frame(input, 5u, 3u, 3u, {}, an::CoordinateOrigin::BottomLeft, true, an::ByteOrder::Big);
    ASSERT_TRUE(save(source).hasValue()) << lastError;
    auto result = decode();
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(values<std::uint16_t>(result->frame), input);
    EXPECT_EQ(result->frame.descriptor()->coordinateOrigin(), an::CoordinateOrigin::BottomLeft);
    EXPECT_EQ(result->frame.layout()->planeCount(), 3u);
}

TEST_F(FitsCodecTest, ADirectEncodeUnderNarrowerLimitsIsRefusedRatherThanWorkedAround)
{
    // The registry holds a frame to the writing context's limits before any codec sees it, but the codec is
    // published and a caller may ask it directly, from a context whose limits are narrower than the ones
    // the frame was described under. A frame the writer must byte-swap needs a one-row workspace that such
    // a context cannot describe, and that is a refusal - never a workspace built from a description that
    // failed.
    auto source = frame(std::vector<std::uint16_t>{1u, 2u, 3u, 4u, 5u}, 5u, 1u, 1u, {}, an::CoordinateOrigin::TopLeft,
                        false, an::ByteOrder::Big);
    an::EngineContextOptions options;
    options.limits.maxImageWidth = 4u;
    auto narrow = an::EngineContext::create(options);
    ASSERT_TRUE(narrow.hasValue());
    auto execution = an::ExecutionContext::create(*narrow, {});
    ASSERT_TRUE(execution.hasValue());
    auto adapter = an::createFitsCodec();
    ASSERT_TRUE(adapter.hasValue());
    const std::string pending = path("narrow.fits");
    const auto written =
        (*adapter)->encode(*narrow, *execution,
                           an::CodecEncodeRequest{.pendingPath = pending, .format = an::CodecFormat::Fits}, source, {});
    ASSERT_FALSE(written.hasValue());
    EXPECT_EQ(written.status().code(), an::ErrorCode::LimitExceeded);
}

TEST_F(FitsCodecTest, MisalignedRowsUseBudgetedWorkspaceAndPreserveCodes)
{
    const std::vector<std::uint32_t> input{0u, 1u, 16777217u, 4294967295u, 2u, 2147483648u};
    auto aligned = frame(input, 3u, 2u);
    const auto descriptor = *aligned.descriptor();
    const auto layout =
        an::ImageLayout::createWithStrides({descriptor.extent(), 4u, 1u, 1u, 1u}, 16u, 32u, engine.limits());
    ASSERT_TRUE(layout.has_value());
    std::vector<std::byte> bytes(33u, std::byte{0x55});
    std::memcpy(bytes.data() + 1u, input.data(), 12u);
    std::memcpy(bytes.data() + 17u, input.data() + 3u, 12u);
    const auto originalBytes = bytes;
    auto buffer = an::SampleBuffer::borrowConst(std::span<const std::byte>(bytes).subspan(1u));
    ASSERT_TRUE(buffer.hasValue());
    auto source = an::RawFrame::create(descriptor, *layout, std::move(*buffer));
    ASSERT_TRUE(source.hasValue());
    ASSERT_TRUE(save(*source).hasValue()) << lastError;
    EXPECT_EQ(bytes, originalBytes);
    auto decoded = decode();
    ASSERT_TRUE(decoded.hasValue());
    EXPECT_EQ(values<std::uint32_t>(decoded->frame), input);

    auto limited = makeEngine(an::FitsInteger64Policy::CheckedFloat64, 11u);
    ASSERT_TRUE(limited.hasValue());
    auto execution = an::ExecutionContext::create(*limited, {});
    ASSERT_TRUE(execution.hasValue());
    const auto destination = path("budget.fits");
    const auto refused = an::writeImage(*limited, *execution, {.destinationPath = destination}, *source, {});
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().rootCause().code(), an::ErrorCode::MemoryBudgetExceeded);
    EXPECT_EQ(limited->memoryResource()->counters().liveBytes, 0u);
    EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(
        std::u8string_view(reinterpret_cast<const char8_t*>(destination.data()), destination.size()))));
    for (const auto& entry : std::filesystem::directory_iterator(directory))
        EXPECT_EQ(entry.path().extension(), ".fits");
}

TEST_F(FitsCodecTest, RegionReadsOnlyRequestedRowsAcrossEveryPlane)
{
    std::vector<float> input(105u);
    for (std::size_t i = 0u; i < input.size(); ++i)
        input[i] = static_cast<float>(i);
    auto source = frame(input, 7u, 5u, 3u);
    ASSERT_TRUE(save(source).hasValue()) << lastError;
    auto execution = an::ExecutionContext::create(engine, {});
    const auto sourcePath = path();
    auto result = an::decodeImageRegion(engine, *execution, {sourcePath}, {2u, 1u, 3u, 2u});
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(values<float>(result->frame),
              (std::vector<float>{9, 10, 11, 16, 17, 18, 44, 45, 46, 51, 52, 53, 79, 80, 81, 86, 87, 88}));
    EXPECT_EQ(result->frame.bytes().size(), 18u * sizeof(float));
    EXPECT_EQ(an::decodeImageRegion(engine, *execution, {sourcePath}, {6u, 0u, 2u, 1u}).status().code(),
              an::ErrorCode::OutOfRange);
}

TEST_F(FitsCodecTest, CfaRegionMovesPhaseAndSaveWritesTheEffectiveBayerPattern)
{
    auto source = frame<std::uint16_t>(std::vector<std::uint16_t>(25u, 17u), 5u, 5u, 1u,
                                       *an::CfaDescriptor::create(an::CfaPattern::Rggb, 0u, 0u));
    ASSERT_TRUE(save(source).hasValue()) << lastError;
    auto execution = an::ExecutionContext::create(engine, {});
    const auto sourcePath = path();
    auto cropped = an::decodeImageRegion(engine, *execution, {sourcePath}, {1u, 1u, 3u, 3u});
    ASSERT_TRUE(cropped.hasValue());
    EXPECT_EQ(cropped->frame.descriptor()->cfa().colorAt(0u, 0u), an::CfaColor::Blue);
    *execution = an::ExecutionContext{};
    auto intent = an::OutputIntent::create({.sampleType = an::SampleType::UInt16});
    ASSERT_TRUE(save(cropped->frame, &*intent, cropped->metadata, nullptr, "crop.fits").hasValue()) << lastError;
    auto reread = decode("crop.fits");
    ASSERT_TRUE(reread.hasValue());
    EXPECT_EQ(reread->frame.descriptor()->cfa().colorAt(0u, 0u), an::CfaColor::Blue);
}

TEST_F(FitsCodecTest, AtomicSaveStampsItsOwnIdentityAndNeverInheritsTheSourceStamp)
{
    auto source = frame<float>({0.1f, 0.2f, 0.3f}, 3u, 1u);
    auto identity = an::ArtifactIdentity::create("artifact-1", "run-2");
    auto intent =
        an::OutputIntent::create({.sampleType = an::SampleType::Float32,
                                  .cards = {card("ANARTID", "stale"), card("BITPIX", "16"), card("bitpix", "16"),
                                            card("naxis1", "99"), card("BZERO", "32768"), card("OBJECT", "NGC 3372")}});
    ASSERT_TRUE(save(source, &*intent, {}, &*identity).hasValue()) << lastError;
    auto decoded = decode();
    ASSERT_TRUE(decoded.hasValue());
    auto readIdentity = an::artifactIdentityOf(decoded->metadata.codec(), an::CodecNamespace::Fits);
    ASSERT_TRUE(readIdentity.hasValue())
        << readIdentity.status().message()
        << "; artifact=" << decoded->metadata.codec().find(an::CodecNamespace::Fits, "ANARTID")->value()
        << "; run=" << decoded->metadata.codec().find(an::CodecNamespace::Fits, "ANRUNID")->value();
    ASSERT_TRUE(readIdentity->has_value());
    EXPECT_EQ(**readIdentity, *identity);
    auto cleanIntent = an::OutputIntent::create({.sampleType = an::SampleType::Float32});
    ASSERT_TRUE(save(decoded->frame, &*cleanIntent, decoded->metadata, nullptr, "derived.fits").hasValue())
        << lastError;
    auto derived = decode("derived.fits");
    ASSERT_TRUE(derived.hasValue());
    EXPECT_EQ(derived->metadata.codec().find(an::CodecNamespace::Fits, "ANARTID"), nullptr);
    EXPECT_NE(derived->metadata.codec().find(an::CodecNamespace::Fits, "OBJECT"), nullptr);
    for (const auto& entry : std::filesystem::directory_iterator(directory))
        EXPECT_NE(entry.path().extension(), ".antmp");
}

TEST_F(FitsCodecTest, AHeaderStatingAZeroExposureLeavesTheFrameReadable)
{
    // What a capture program writes for a bias frame. The typed acquisition record refuses a
    // zero-length exposure; the frame's pixels do not depend on it and must still decode, with the
    // fact left unstated and the card the file carried preserved as the file wrote it.
    auto source = frame<std::uint16_t>({100u, 200u, 300u, 400u}, 2u, 2u);
    const auto intent = an::OutputIntent::create(
        {.sampleType = an::SampleType::UInt16,
         .cards = {card("IMAGETYP", "Bias Frame"), card("EXPTIME", "0"), card("CCD-TEMP", "-400")}});
    ASSERT_TRUE(intent.hasValue());
    ASSERT_TRUE(save(source, &*intent).hasValue()) << lastError;
    auto decoded = decode();
    ASSERT_TRUE(decoded.hasValue()) << lastError;
    EXPECT_EQ(values<std::uint16_t>(decoded->frame), (std::vector<std::uint16_t>{100u, 200u, 300u, 400u}));
    EXPECT_FALSE(decoded->metadata.acquisition().exposureSeconds().has_value());
    EXPECT_FALSE(decoded->metadata.acquisition().sensorTemperatureCelsius().has_value());
    const auto* const exposure = decoded->metadata.codec().find(an::CodecNamespace::Fits, "EXPTIME");
    ASSERT_NE(exposure, nullptr);
    EXPECT_EQ(exposure->value(), "0");
}

TEST_F(FitsCodecTest, TruncatedPayloadLeavesHeaderReadableAndCannotPublishAPartialDecode)
{
    auto source = frame<float>(std::vector<float>(4096u, 0.5f), 64u, 64u);
    ASSERT_TRUE(save(source).hasValue()) << lastError;
    auto bytes = readBytes(path());
    std::filesystem::resize_file(directory / "frame.fits", bytes.size() - 2880u);
    auto execution = an::ExecutionContext::create(engine, {});
    const auto sourcePath = path();
    EXPECT_TRUE(an::probeImage(engine, *execution, {.path = sourcePath, .validatePayload = false}).hasValue());
    auto validated = an::probeImage(engine, *execution, {sourcePath});
    EXPECT_FALSE(validated.hasValue());
    *execution = an::ExecutionContext{};
    EXPECT_FALSE(decode().hasValue());
}

TEST_F(FitsCodecTest, UnsupportedCompressionLeavesExistingDestinationAndNoPendingFile)
{
    auto source = frame<float>({0.1f, 0.2f, 0.3f}, 3u, 1u);
    ASSERT_TRUE(save(source).hasValue()) << lastError;
    const auto before = readBytes(path());
    auto intent = an::OutputIntent::create(
        {.sampleType = an::SampleType::Float32, .compression = an::OutputCompression::Deflate});
    EXPECT_FALSE(save(source, &*intent).hasValue());
    EXPECT_EQ(readBytes(path()), before);
    EXPECT_EQ(std::distance(std::filesystem::directory_iterator(directory), std::filesystem::directory_iterator()), 1);
}

TEST_F(FitsCodecTest, CompressionRoundTripsRemainLossless)
{
    for (const auto compression :
         {an::OutputCompression::Rice, an::OutputCompression::Gzip, an::OutputCompression::Plio})
    {
        std::vector<std::uint16_t> input(105u);
        for (std::size_t i = 0; i < input.size(); ++i)
            input[i] = static_cast<std::uint16_t>(i * 31u);
        auto source = frame(input, 7u, 5u, 3u);
        auto intent = an::OutputIntent::create({.sampleType = an::SampleType::UInt16, .compression = compression});
        ASSERT_TRUE(save(source, &*intent).hasValue()) << static_cast<int>(compression);
        auto result = decode();
        ASSERT_TRUE(result.hasValue());
        EXPECT_EQ(values<std::uint16_t>(result->frame), input);
    }
    auto source = frame<double>({0.12345678901234567, -12.75, 1e42}, 3u, 1u);
    auto intent =
        an::OutputIntent::create({.sampleType = an::SampleType::Float64, .compression = an::OutputCompression::Gzip});
    ASSERT_TRUE(save(source, &*intent).hasValue()) << lastError;
    auto result = decode();
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(values<double>(result->frame), values<double>(source));
}

TEST_F(FitsCodecTest, EveryCompressionIsEitherWrittenAsRequestedOrRefusedForEveryResidentType)
{
    const auto check = [&]<typename T>() {
        auto source = frame<T>({T{0}, T{1}}, 2u, 1u);
        for (unsigned code = 0u; an::isOutputCompression(static_cast<an::OutputCompression>(code)); ++code)
        {
            const auto compression = static_cast<an::OutputCompression>(code);
            SCOPED_TRACE(testing::Message() << "sample=" << unsigned(*source.descriptor()->residentSampleType())
                                            << " compression=" << code);
            const auto intent = an::OutputIntent::create(
                {.sampleType = *source.descriptor()->residentSampleType(), .compression = compression});
            ASSERT_TRUE(intent.hasValue());
            ASSERT_TRUE(save(source).hasValue()) << lastError;
            const auto before = readBytes(path());
            const auto report = save(source, &*intent);
            const bool supported =
                compression == an::OutputCompression::None || compression == an::OutputCompression::Gzip ||
                (!std::is_floating_point_v<T> &&
                 (compression == an::OutputCompression::Rice ||
                  (compression == an::OutputCompression::Plio && !std::is_same_v<T, std::uint32_t>)));
            ASSERT_EQ(report.hasValue(), supported) << lastError;
            if (!supported)
            {
                EXPECT_EQ(report.status().code(), an::ErrorCode::Unsupported);
                EXPECT_EQ(readBytes(path()), before);
                continue;
            }
            auto result = decode();
            ASSERT_TRUE(result.hasValue()) << lastError;
            EXPECT_EQ(values<T>(result->frame), values<T>(source));
            const auto encoded = readBytes(path());
            const std::string header(reinterpret_cast<const char*>(encoded.data()), encoded.size());
            const auto* spelling = compression == an::OutputCompression::Rice   ? "RICE_1"
                                   : compression == an::OutputCompression::Plio ? "PLIO_1"
                                                                                : "GZIP_1";
            if (compression == an::OutputCompression::None)
                EXPECT_EQ(header.find("ZCMPTYPE"), std::string::npos);
            else
                EXPECT_NE(header.find(spelling), std::string::npos);
        }
    };
    check.operator()<std::uint8_t>();
    check.operator()<std::int8_t>();
    check.operator()<std::uint16_t>();
    check.operator()<std::int16_t>();
    check.operator()<std::uint32_t>();
    check.operator()<std::int32_t>();
    check.operator()<float>();
    check.operator()<double>();
}

TEST_F(FitsCodecTest, PlioTilesHaveSpaceForTheirHeaderAndWorstCaseCodes)
{
    const auto intent =
        an::OutputIntent::create({.sampleType = an::SampleType::Int32, .compression = an::OutputCompression::Plio});
    ASSERT_TRUE(intent.hasValue());
    for (const auto width : {1u, 2u, 2048u})
    {
        std::vector<std::int32_t> input(width);
        for (std::size_t i = 0u; i < input.size(); ++i)
            input[i] = i % 2u == 0u ? 1 : 16777215;
        auto source = frame<std::int32_t>(input, width, 1u);
        ASSERT_TRUE(save(source, &*intent).hasValue()) << lastError;
        auto result = decode();
        ASSERT_TRUE(result.hasValue()) << lastError;
        EXPECT_EQ(values<std::int32_t>(result->frame), input);
    }
}

TEST_F(FitsCodecTest, GzipKeepsFloatingNonfiniteSamplesWithoutNullSubstitution)
{
    const auto check = [&]<typename T>() {
        const auto source = frame<T>({T{0}, -T{0}, std::numeric_limits<T>::infinity(),
                                      -std::numeric_limits<T>::infinity(), std::numeric_limits<T>::quiet_NaN()},
                                     5u, 1u);
        const auto intent = an::OutputIntent::create(
            {.sampleType = *source.descriptor()->residentSampleType(), .compression = an::OutputCompression::Gzip});
        ASSERT_TRUE(save(source, &*intent).hasValue()) << lastError;
        auto result = decode();
        ASSERT_TRUE(result.hasValue()) << lastError;
        EXPECT_EQ(std::memcmp(result->frame.bytes().data(), source.bytes().data(), source.bytes().size()), 0);
    };
    check.operator()<float>();
    check.operator()<double>();
}

TEST_F(FitsCodecTest, LiteralUnicodePathsDoNotInvokeTheExtendedFilenameParser)
{
    auto source = frame<std::uint8_t>({1, 2, 3}, 3u, 1u);
    constexpr const char* name = "\xc3\xa1-\xe6\x98\x9f[1].fits";
    ASSERT_TRUE(save(source, nullptr, {}, nullptr, name).hasValue()) << lastError;
    auto result = decode(name);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(values<std::uint8_t>(result->frame), (std::vector<std::uint8_t>{1, 2, 3}));
}

TEST_F(FitsCodecTest, WcsSipCardsTravelThroughTheSharedBridgeAndCropTransformsTheSolution)
{
    auto source = frame<float>(std::vector<float>(63u, 0.5f), 9u, 7u);
    const std::vector<an::CodecCard> cards{card("CTYPE1", "RA---TAN-SIP"),
                                           card("CTYPE2", "DEC--TAN-SIP"),
                                           card("CRPIX1", "5"),
                                           card("CRPIX2", "4"),
                                           card("CRVAL1", "161.25"),
                                           card("CRVAL2", "-59.5"),
                                           card("CD1_1", "-0.0003"),
                                           card("CD1_2", "0"),
                                           card("CD2_1", "0"),
                                           card("CD2_2", "0.0003"),
                                           card("A_ORDER", "2"),
                                           card("B_ORDER", "2"),
                                           card("A_2_0", "0.00001"),
                                           card("B_0_2", "-0.00001"),
                                           card("EXPTIME", "120.5"),
                                           card("HISTORY", "calibrated bias and dark"),
                                           card("HISTORY", "registered with SIP")};
    auto intent = an::OutputIntent::create({.sampleType = an::SampleType::Float32, .cards = cards});
    ASSERT_TRUE(save(source, &*intent).hasValue()) << lastError;
    auto result = decode();
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(an::checkWcsCards(result->metadata.codec().cards()), an::WcsCardError::None);
    const auto original = an::parseWcsCards(result->metadata.codec().cards(), 9u, 7u);
    ASSERT_TRUE(original.hasValue());
    EXPECT_EQ(result->metadata.astrometry(), *original->toAstrometricSolution());
    auto execution = an::ExecutionContext::create(engine, {});
    const auto sourcePath = path();
    auto cropped = an::decodeImageRegion(engine, *execution, {sourcePath}, {2u, 1u, 5u, 4u});
    ASSERT_TRUE(cropped.hasValue());
    const auto expected = an::cropWcsSolution(*original, 2u, 1u, 5u, 4u);
    const auto parsed = an::parseWcsCards(cropped->metadata.codec().cards(), 5u, 4u);
    ASSERT_TRUE(parsed.hasValue());
    EXPECT_EQ(*parsed, *expected);
    EXPECT_EQ(std::count_if(result->metadata.codec().cards().begin(), result->metadata.codec().cards().end(),
                            [](const auto& entry) { return entry.keyword() == "HISTORY"; }),
              2);
}

TEST_F(FitsCodecTest, NonFiniteFloatingPayloadIsPreservedWithoutInventingValidity)
{
    const auto nan = std::bit_cast<float>(std::uint32_t{0x7fc01234u});
    auto source = frame<float>(
        {nan, std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), -0.0f}, 4u, 1u);
    ASSERT_TRUE(save(source).hasValue()) << lastError;
    auto result = decode();
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result->frame.bytes().size(), source.bytes().size());
    EXPECT_EQ(std::memcmp(result->frame.bytes().data(), source.bytes().data(), source.bytes().size()), 0);
    EXPECT_EQ(result->metadata.semantics(), an::ImageSemantics{});
}

TEST_F(FitsCodecTest, IndependentImageExtensionsAreSelectedByImageIndex)
{
    auto source = frame<std::uint8_t>({1, 2, 3}, 3u, 1u);
    ASSERT_TRUE(save(source).hasValue()) << lastError;
    auto bytes = readBytes(path());
    std::string extension;
    for (const std::string text :
         {"XTENSION= 'IMAGE   '", "BITPIX  =                    8", "NAXIS   =                    2",
          "NAXIS1  =                    2", "NAXIS2  =                    1", "PCOUNT  =                    0",
          "GCOUNT  =                    1", "OBJECT  = 'second image'", "END"})
        extension += text + std::string(80u - text.size(), ' ');
    extension.resize(2880u, ' ');
    std::string payload(2880u, '\0');
    payload[0] = 42;
    payload[1] = 43;
    {
        std::ofstream out(directory / "frame.fits", std::ios::binary | std::ios::app);
        out.write(extension.data(), extension.size());
        out.write(payload.data(), payload.size());
    }
    auto execution = an::ExecutionContext::create(engine, {});
    const auto sourcePath = path();
    auto info = an::probeImage(engine, *execution, {sourcePath});
    ASSERT_TRUE(info.hasValue());
    EXPECT_EQ(info->imageCount, 2u);
    auto second = an::decodeImage(engine, *execution, {.path = sourcePath, .imageIndex = 1u});
    ASSERT_TRUE(second.hasValue());
    EXPECT_EQ(values<std::uint8_t>(second->frame), (std::vector<std::uint8_t>{42, 43}));
    EXPECT_EQ(an::decodeImage(engine, *execution, {.path = sourcePath, .imageIndex = 2u}).status().code(),
              an::ErrorCode::OutOfRange);
}

TEST_F(FitsCodecTest, OneImmutableAdapterSupportsConcurrentContextsAndIndependentHandles)
{
    auto source = frame<float>(std::vector<float>(4096u, 0.125f), 64u, 64u);
    ASSERT_TRUE(save(source).hasValue()) << lastError;
    auto adapter = an::createFitsCodec();
    ASSERT_TRUE(adapter.hasValue());
    const std::array adapters{*adapter};
    auto registry = an::CodecRegistry::create(adapters);
    ASSERT_TRUE(registry.hasValue());
    an::EngineContextOptions options;
    options.codecRegistry = *registry;
    auto shared = an::EngineContext::create(options);
    auto other = an::EngineContext::create(options);
    ASSERT_TRUE(shared.hasValue());
    ASSERT_TRUE(other.hasValue());
    const auto sourcePath = path();
    std::array<bool, 2> ok{};
    std::thread first([&] {
        auto execution = an::ExecutionContext::create(*shared, {});
        auto result = an::decodeImage(*shared, *execution, {sourcePath});
        ok[0] = result.hasValue() && values<float>(result->frame) == values<float>(source);
    });
    std::thread second([&] {
        auto execution = an::ExecutionContext::create(*other, {});
        auto result = an::decodeImage(*other, *execution, {sourcePath});
        ok[1] = result.hasValue() && values<float>(result->frame) == values<float>(source);
    });
    first.join();
    second.join();
    EXPECT_TRUE(ok[0]);
    EXPECT_TRUE(ok[1]);
}

TEST_F(FitsCodecTest, CancellationAndMemoryRefusalReleaseUnpublishedSamples)
{
    auto source = frame<float>(std::vector<float>(4096u, 1.0f), 64u, 64u);
    ASSERT_TRUE(save(source).hasValue()) << lastError;
    auto constrained = makeEngine(an::FitsInteger64Policy::CheckedFloat64, 64u);
    auto execution = an::ExecutionContext::create(*constrained, {});
    const auto sourcePath = path();
    auto refused = an::decodeImage(*constrained, *execution, {sourcePath});
    EXPECT_EQ(refused.status().code(), an::ErrorCode::MemoryBudgetExceeded);
    EXPECT_EQ(constrained->memoryResource()->counters().liveBytes, 0u);
    std::stop_source stop;
    stop.request_stop();
    an::ExecutionOptions options;
    options.stopToken = stop.get_token();
    auto cancelled = an::ExecutionContext::create(engine, options);
    EXPECT_EQ(an::decodeImage(engine, *cancelled, {sourcePath}).status().code(), an::ErrorCode::Cancelled);
}

TEST_F(FitsCodecTest, BoundedRegionQueryDistinguishesCompressedTileWorkspace)
{
    auto source = frame<std::uint16_t>(std::vector<std::uint16_t>(100u, 8u), 10u, 10u);
    ASSERT_TRUE(save(source).hasValue()) << lastError;
    const auto sourcePath = path();
    {
        auto execution = an::ExecutionContext::create(engine, {});
        auto bounded = an::supportsBoundedFitsRegionDecode(engine, *execution, sourcePath);
        ASSERT_TRUE(bounded.hasValue());
        EXPECT_TRUE(*bounded);
    }
    auto intent =
        an::OutputIntent::create({.sampleType = an::SampleType::UInt16, .compression = an::OutputCompression::Rice});
    ASSERT_TRUE(save(source, &*intent).hasValue()) << lastError;
    auto execution = an::ExecutionContext::create(engine, {});
    auto bounded = an::supportsBoundedFitsRegionDecode(engine, *execution, sourcePath);
    ASSERT_TRUE(bounded.hasValue());
    EXPECT_FALSE(*bounded);
    auto region = an::decodeImageRegion(engine, *execution, {sourcePath}, {1u, 2u, 3u, 4u});
    ASSERT_TRUE(region.hasValue());
    EXPECT_EQ(values<std::uint16_t>(region->frame), std::vector<std::uint16_t>(12u, 8u));
}

TEST_F(FitsCodecTest, LongStringsAndQuotedNumericStringsSurviveRepeatedSaves)
{
    auto source = frame<float>({0.125f}, 1u, 1u);
    const std::string longText = "observer's " + std::string(700u, 'x');
    auto intent = an::OutputIntent::create(
        {.sampleType = an::SampleType::Float32,
         .cards = {card("LONGTEXT", longText), card("TEXTNUM", "'12345'"), card("FRACTION", "1/2"),
                   card("LONG.HIERARCHICAL.KEY", "'" + std::string(60u, 'z') + "'")}});
    ASSERT_TRUE(save(source, &*intent).hasValue()) << lastError;
    auto first = decode();
    ASSERT_TRUE(first.hasValue());
    const auto* original = first->metadata.codec().find(an::CodecNamespace::Fits, "LONGTEXT");
    ASSERT_NE(original, nullptr);
    EXPECT_NE(original->value().find(std::string(700u, 'x')), std::string::npos);
    auto preserve = an::OutputIntent::create({.sampleType = an::SampleType::Float32});
    ASSERT_TRUE(save(first->frame, &*preserve, first->metadata, nullptr, "second.fits").hasValue()) << lastError;
    auto second = decode("second.fits");
    ASSERT_TRUE(second.hasValue());
    EXPECT_EQ(second->metadata.codec().find(an::CodecNamespace::Fits, "LONGTEXT")->value(), original->value());
    EXPECT_EQ(second->metadata.codec().find(an::CodecNamespace::Fits, "TEXTNUM")->value(), "'12345'");
    EXPECT_EQ(second->metadata.codec().find(an::CodecNamespace::Fits, "FRACTION")->value(), "'1/2'");
    EXPECT_EQ(second->metadata.codec().find(an::CodecNamespace::Fits, "LONG.HIERARCHICAL.KEY")->value(),
              "'" + std::string(60u, 'z') + "'");
}

TEST_F(FitsCodecTest, ExpandedHistoryReservesExplicitScalingCardsBeforeThePayload)
{
    auto source = frame<std::uint16_t>({0u, 1u, 32768u, 65535u}, 4u, 1u);
    std::vector<an::CodecCard> cards;
    for (unsigned index = 0u; index < 15u; ++index)
        cards.push_back(card("HISTORY", std::to_string(index) + ":" + std::string(217u, 'x')));
    auto intent = an::OutputIntent::create({.sampleType = an::SampleType::UInt16,
                                            .scaling = an::OutputScaling::Explicit,
                                            .scale = 0.25,
                                            .zeroOffset = -12.0,
                                            .cards = std::move(cards)});

    ASSERT_TRUE(save(source, &*intent).hasValue()) << lastError;
    auto result = decode();
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(values<double>(result->frame), (std::vector<double>{-12.0, -11.75, 8180.0, 16371.75}));
    EXPECT_NE(result->metadata.codec().find(an::CodecNamespace::Fits, "BSCALE"), nullptr);
    EXPECT_NE(result->metadata.codec().find(an::CodecNamespace::Fits, "BZERO"), nullptr);
}

TEST_F(FitsCodecTest, RepeatedStringKeywordsKeepTheirOwnValuesAndComments)
{
    auto source = frame<float>({0.125f}, 1u, 1u);
    ASSERT_TRUE(save(source).hasValue()) << lastError;
    auto bytes = readBytes(path());
    std::size_t end = bytes.size();
    for (std::size_t offset = 0u; offset + 80u <= bytes.size(); offset += 80u)
        if (bytes[offset] == std::byte{'E'} && bytes[offset + 1u] == std::byte{'N'} &&
            bytes[offset + 2u] == std::byte{'D'})
        {
            end = offset;
            break;
        }
    ASSERT_LT(end, bytes.size());
    ASSERT_LE(end + 240u, bytes.size());
    const auto putCard = [&bytes](std::size_t offset, std::string text) {
        text.resize(80u, ' ');
        std::memcpy(bytes.data() + offset, text.data(), text.size());
    };
    putCard(end, "FILTER  = 'Ha'                 / first filter");
    putCard(end + 80u, "FILTER  = 'OIII'               / second filter");
    putCard(end + 160u, "END");
    std::ofstream output(std::filesystem::path(path()), std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    output.close();

    auto decoded = decode();
    ASSERT_TRUE(decoded.hasValue()) << lastError;
    std::vector<const an::CodecCard*> filters;
    for (const an::CodecCard& current : decoded->metadata.codec().cards())
        if (current.keyword() == "FILTER")
            filters.push_back(&current);
    ASSERT_EQ(filters.size(), 2u);
    EXPECT_EQ(filters[0]->value(), "'Ha'");
    EXPECT_EQ(filters[0]->comment(), "first filter");
    EXPECT_EQ(filters[1]->value(), "'OIII'");
    EXPECT_EQ(filters[1]->comment(), "second filter");
}

TEST_F(FitsCodecTest, SmallWritesKeepIdenticalBytesWhenTheBudgetRequiresDiskStreaming)
{
    std::vector<float> data(257u * 259u);
    for (std::size_t i = 0u; i < data.size(); ++i)
        data[i] = static_cast<float>(i % 65521u) / 65521.0f;
    const auto source = frame(data, 257u, 259u);
    const auto intent = an::OutputIntent::create(
        {.sampleType = an::SampleType::Float32,
         .cards = {card("LONGTEXT", "observer's " + std::string(2049u, 'x')), card("TEXTNUM", "'12345'")}});
    ASSERT_TRUE(intent.hasValue());
    ASSERT_TRUE(save(source, &*intent).hasValue()) << lastError;
    auto bounded = makeEngine(an::FitsInteger64Policy::CheckedFloat64, 1024u);
    ASSERT_TRUE(bounded.hasValue());
    auto execution = an::ExecutionContext::create(*bounded, {});
    ASSERT_TRUE(execution.hasValue());
    const auto destination = path("streamed.fits");
    const auto saved =
        an::writeImage(*bounded, *execution, {.destinationPath = destination, .intent = &*intent}, source, {});
    ASSERT_TRUE(saved.hasValue()) << saved.status().message();
    EXPECT_EQ(readBytes(path()), readBytes(destination));
    EXPECT_EQ(bounded->memoryResource()->counters().liveBytes, 0u);
    EXPECT_EQ(bounded->memoryResource()->counters().peakBytes, 0u);
    const auto decoded = decode("streamed.fits");
    ASSERT_TRUE(decoded.hasValue());
    EXPECT_EQ(values<float>(decoded->frame), data);
}

TEST_F(FitsCodecTest, SmallUncompressedWritesAreStagedInContextMemoryWhenTheBudgetAllows)
{
    std::vector<float> data(257u * 259u);
    for (std::size_t i = 0u; i < data.size(); ++i)
        data[i] = static_cast<float>(i % 65521u) / 65521.0f;
    const auto source = frame(data, 257u, 259u);
    const auto intent = an::OutputIntent::create({.sampleType = an::SampleType::Float32});
    ASSERT_TRUE(intent.hasValue());
    auto staged = makeEngine();
    ASSERT_TRUE(staged.hasValue());
    auto execution = an::ExecutionContext::create(*staged, {});
    ASSERT_TRUE(execution.hasValue());
    const auto destination = path("staged.fits");
    const auto saved =
        an::writeImage(*staged, *execution, {.destinationPath = destination, .intent = &*intent}, source, {});
    ASSERT_TRUE(saved.hasValue()) << saved.status().message();
    // Every host stages the whole file in context memory and hands the disk one completed write, so the
    // high-water mark holds at least the serialized file and nothing stays live once the write returns.
    EXPECT_GE(staged->memoryResource()->counters().peakBytes, saved->bytesWritten);
    EXPECT_EQ(staged->memoryResource()->counters().liveBytes, 0u);
    const auto decoded = decode("staged.fits");
    ASSERT_TRUE(decoded.hasValue());
    EXPECT_EQ(values<float>(decoded->frame), data);
}

TEST_F(FitsCodecTest, UnrepresentableScalingCannotReplaceAnExistingDestination)
{
    auto source = frame<std::uint16_t>({0, 1, 65535}, 3u, 1u);
    ASSERT_TRUE(save(source).hasValue()) << lastError;
    const auto before = readBytes(path());
    auto intent = an::OutputIntent::create({.sampleType = an::SampleType::UInt16,
                                            .scaling = an::OutputScaling::Explicit,
                                            .scale = (std::numeric_limits<double>::max)(),
                                            .zeroOffset = 0.0});
    ASSERT_TRUE(intent.hasValue());
    EXPECT_EQ(save(source, &*intent).status().code(), an::ErrorCode::OutOfRange);
    EXPECT_EQ(readBytes(path()), before);
    EXPECT_EQ(std::distance(std::filesystem::directory_iterator(directory), std::filesystem::directory_iterator{}), 1);
}

TEST_F(FitsCodecTest, RawInteger64RoundTripRetainsScalingAndChecksBeforeApplyingIt)
{
    const std::int64_t large = (std::int64_t{1} << 53) + 1;
    auto source = frame<std::int64_t>({large, -large}, 2u, 1u);
    auto codec = an::CodecMetadata::create(
        {an::RawSampleFormat{8u, an::RawSampleClass::SignedInteger}, an::ByteOrder::Big, 0.00001, 17.0}, {});
    auto metadata = an::ImageMetadata::create({}, {}, {}, {}, *codec, std::nullopt);
    ASSERT_TRUE(save(source, nullptr, metadata).hasValue()) << lastError;
    EXPECT_EQ(decode().status().code(), an::ErrorCode::OutOfRange);
    auto context = makeEngine(an::FitsInteger64Policy::PreserveRaw);
    auto execution = an::ExecutionContext::create(*context, {});
    const auto sourcePath = path();
    auto raw = an::decodeImage(*context, *execution, {sourcePath});
    ASSERT_TRUE(raw.hasValue());
    EXPECT_EQ(values<std::int64_t>(raw->frame), (std::vector<std::int64_t>{large, -large}));
    EXPECT_EQ(raw->metadata.codec().sourceEncoding().scale, 0.00001);
    EXPECT_EQ(raw->metadata.codec().sourceEncoding().zeroOffset, 17.0);
    const auto copyPath = path("copy.fits");
    auto copied = an::writeImage(*context, *execution, {.destinationPath = copyPath}, raw->frame, raw->metadata);
    ASSERT_TRUE(copied.hasValue());
    auto restored = an::decodeImage(*context, *execution, {copyPath});
    ASSERT_TRUE(restored.hasValue());
    EXPECT_EQ(values<std::int64_t>(restored->frame), values<std::int64_t>(raw->frame));
    EXPECT_EQ(restored->metadata.codec().sourceEncoding(), raw->metadata.codec().sourceEncoding());
    auto lossy = makeEngine(an::FitsInteger64Policy::AllowLossyFloat64);
    auto lossyExecution = an::ExecutionContext::create(*lossy, {});
    auto pixelsOnly = an::decodeImage(*lossy, *lossyExecution, {.path = sourcePath, .readMetadata = false});
    ASSERT_TRUE(pixelsOnly.hasValue());
    EXPECT_EQ(pixelsOnly->metadata.codec().sourceEncoding(), raw->metadata.codec().sourceEncoding());
    EXPECT_TRUE(pixelsOnly->metadata.codec().cards().empty());
    ASSERT_EQ(pixelsOnly->metadata.provenance().entries().size(), 1u);
}

TEST_F(FitsCodecTest, WholeRowsAreReadFromThePayloadExactlyInEveryRepresentationAndPlane)
{
    // Whole rows of an uncompressed image at unit scale are read straight from the payload and
    // converted in place. Every representation, its extremes and a floating payload's specials come
    // back bit for bit as written - whole, and as a band of rows in each of three planes.
    const auto bits = [](const auto& values) {
        std::vector<std::byte> raw(values.size() * sizeof(values[0]));
        std::memcpy(raw.data(), values.data(), raw.size());
        return raw;
    };
    const auto check = [&]<typename T>(const std::vector<T>& palette) {
        constexpr std::uint32_t kWidth = 13u;
        constexpr std::uint32_t kHeight = 7u;
        constexpr std::uint32_t kPlanes = 3u;
        std::vector<T> data(static_cast<std::size_t>(kWidth) * kHeight * kPlanes);
        for (std::size_t index = 0u; index < data.size(); ++index)
            data[index] = palette[(index * 7u + index / 5u) % palette.size()];
        const auto source = frame(data, kWidth, kHeight, kPlanes);
        ASSERT_TRUE(save(source).hasValue()) << lastError;
        const auto whole = decode();
        ASSERT_TRUE(whole.hasValue()) << whole.status().message();
        EXPECT_EQ(bits(values<T>(whole->frame)), bits(data)) << sizeof(T) << "-byte samples";
        auto execution = an::ExecutionContext::create(engine, {});
        const auto sourcePath = path();
        const auto band = an::decodeImageRegion(engine, *execution, {sourcePath}, {0u, 2u, kWidth, 3u});
        ASSERT_TRUE(band.hasValue()) << band.status().message();
        std::vector<T> expected;
        for (std::uint32_t plane = 0u; plane < kPlanes; ++plane)
            for (std::uint32_t y = 2u; y < 5u; ++y)
                for (std::uint32_t x = 0u; x < kWidth; ++x)
                    expected.push_back(data[(static_cast<std::size_t>(plane) * kHeight + y) * kWidth + x]);
        EXPECT_EQ(bits(values<T>(band->frame)), bits(expected)) << sizeof(T) << "-byte samples, rows 2 to 4";
    };
    check(std::vector<std::uint8_t>{0, 1, 127, 128, 254, 255});
    check(std::vector<std::int8_t>{-128, -127, -1, 0, 1, 127});
    check(std::vector<std::uint16_t>{0, 1, 255, 256, 32767, 32768, 65534, 65535});
    check(std::vector<std::int16_t>{-32768, -32767, -256, -1, 0, 1, 256, 32767});
    check(std::vector<std::uint32_t>{0u, 1u, 16777217u, 2147483647u, 2147483648u, 4294967295u});
    check(std::vector<std::int32_t>{-2147483647 - 1, -16777217, -1, 0, 1, 2147483647});
    check(std::vector<float>{std::bit_cast<float>(std::uint32_t{0x7fc01234u}), std::numeric_limits<float>::infinity(),
                             -std::numeric_limits<float>::infinity(), -0.0f, 0.0f,
                             std::numeric_limits<float>::denorm_min(), -123.5f, (std::numeric_limits<float>::max)()});
    check(std::vector<double>{std::bit_cast<double>(std::uint64_t{0x7ff8000000001234u}), -0.0, 1.0000000000000002,
                              std::numeric_limits<double>::denorm_min(), -1e300, (std::numeric_limits<double>::max)()});
}

TEST_F(FitsCodecTest, AFrameLargerThanOneStripIsReadStripByStripAndAShortPayloadIsRefused)
{
    // A strip is at most four mebibytes; a frame of more is read in several, each converted where it
    // lands, and a payload that ends inside a strip the decode reaches is refused rather than read
    // short, while rows before it still decode.
    constexpr std::uint32_t kWidth = 1024u;
    constexpr std::uint32_t kHeight = 2100u;
    std::vector<std::uint16_t> data(static_cast<std::size_t>(kWidth) * kHeight);
    for (std::size_t index = 0u; index < data.size(); ++index)
        data[index] = static_cast<std::uint16_t>((index * 2654435761u) >> 16u);
    const auto source = frame(data, kWidth, kHeight);
    ASSERT_TRUE(save(source).hasValue()) << lastError;
    const auto whole = decode();
    ASSERT_TRUE(whole.hasValue()) << whole.status().message();
    EXPECT_EQ(values<std::uint16_t>(whole->frame), data);
    auto execution = an::ExecutionContext::create(engine, {});
    const auto sourcePath = path();
    const auto late = an::decodeImageRegion(engine, *execution, {sourcePath}, {0u, 1000u, kWidth, 1100u});
    ASSERT_TRUE(late.hasValue()) << late.status().message();
    EXPECT_EQ(values<std::uint16_t>(late->frame),
              std::vector<std::uint16_t>(data.begin() + 1000u * kWidth, data.end()));

    std::filesystem::resize_file(directory / "frame.fits",
                                 std::filesystem::file_size(directory / "frame.fits") - 2880u * 4u);
    const auto early = an::decodeImageRegion(engine, *execution, {sourcePath}, {0u, 0u, kWidth, 16u});
    ASSERT_TRUE(early.hasValue()) << early.status().message();
    EXPECT_EQ(values<std::uint16_t>(early->frame),
              std::vector<std::uint16_t>(data.begin(), data.begin() + 16u * kWidth));
    const auto cut = an::decodeImageRegion(engine, *execution, {sourcePath}, {0u, kHeight - 16u, kWidth, 16u});
    ASSERT_FALSE(cut.hasValue());
    EXPECT_EQ(cut.status().code(), an::ErrorCode::InvalidData);
    EXPECT_FALSE(decode().hasValue());
}

/// Keeps every progress report an operation delivers, and asks for its stop at the first one when told to.
class RecordedProgress final : public an::ProgressSink
{
public:
    explicit RecordedProgress(std::stop_source* stopAtFirst = nullptr) : m_stopAtFirst(stopAtFirst) {}

    void onProgress(const an::ProgressReport& report) noexcept override
    {
        completed.push_back(report.completed);
        totals.push_back(report.total);
        if (m_stopAtFirst != nullptr)
            m_stopAtFirst->request_stop();
    }

    std::vector<std::uint64_t> completed; ///< Each report's completed count, in the order delivered.
    std::vector<std::uint64_t> totals; ///< Each report's total.

private:
    std::stop_source* m_stopAtFirst;
};

TEST_F(FitsCodecTest, ALargePayloadIsReadOnTheOperationsWorkersAsOneWorkerReadsIt)
{
    // A payload past the shared minimum, read by an operation granted eight workers, is read strip by strip on them,
    // each through a handle of its own. Its samples are the ones one worker reads, in every plane and at two widths,
    // and so are those of a full-width band that starts and ends inside a strip; its progress only moves on and
    // reaches every row, nothing discarded; a stop asked for at the first report ends it; and a payload cut short is
    // refused as one worker refuses it.
    const auto readWith = [&](std::uint32_t workers, an::ProgressSink* sink, std::stop_token stop,
                              const an::CodecRegion* region = nullptr) {
        an::ExecutionOptions options;
        options.maxConcurrency = workers;
        options.progressSink = sink;
        options.stopToken = stop;
        auto execution = an::ExecutionContext::create(engine, options);
        EXPECT_TRUE(execution.hasValue());
        const auto source = path();
        auto decoded = region != nullptr ? an::decodeImageRegion(engine, *execution, {source}, *region)
                                         : an::decodeImage(engine, *execution, {source});
        return std::make_pair(std::move(decoded), execution->operationCounters());
    };
    const auto check = [&]<typename T>(std::uint32_t width, std::uint32_t height, std::uint32_t planes, T bias) {
        std::vector<T> data(static_cast<std::size_t>(width) * height * planes);
        for (std::size_t index = 0u; index < data.size(); ++index)
            data[index] = static_cast<T>(static_cast<T>((index * 2654435761u) >> 13u) + bias);
        ASSERT_GE(data.size() * sizeof(T), std::size_t{32u} << 20u);
        const auto source = frame(data, width, height, planes);
        ASSERT_TRUE(save(source).hasValue()) << lastError;

        auto [alone, aloneCounters] = readWith(1u, nullptr, {});
        ASSERT_TRUE(alone.hasValue()) << alone.status().message();
        RecordedProgress progress;
        auto [shared, sharedCounters] = readWith(8u, &progress, {});
        ASSERT_TRUE(shared.hasValue()) << shared.status().message();
        EXPECT_EQ(values<T>(shared->frame), values<T>(alone->frame));
        EXPECT_EQ(values<T>(shared->frame), data);
        ASSERT_GT(progress.completed.size(), 1u);
        EXPECT_TRUE(std::is_sorted(progress.completed.begin(), progress.completed.end()));
        EXPECT_EQ(progress.completed.back(), static_cast<std::uint64_t>(height) * planes);
        EXPECT_EQ(progress.totals.back(), static_cast<std::uint64_t>(height) * planes);
        EXPECT_EQ(sharedCounters.discardedProgressReports, 0u);

        const an::CodecRegion band{0u, 3u, width, height - 5u};
        ASSERT_GE(static_cast<std::size_t>(band.width) * band.height * planes * sizeof(T), std::size_t{32u} << 20u);
        auto [bandAlone, bandAloneCounters] = readWith(1u, nullptr, {}, &band);
        auto [bandShared, bandSharedCounters] = readWith(8u, nullptr, {}, &band);
        ASSERT_TRUE(bandAlone.hasValue()) << bandAlone.status().message();
        ASSERT_TRUE(bandShared.hasValue()) << bandShared.status().message();
        std::vector<T> bandRows;
        for (std::uint32_t plane = 0u; plane < planes; ++plane)
        {
            const auto first = data.begin() + static_cast<std::ptrdiff_t>((plane * height + band.y) * width);
            bandRows.insert(bandRows.end(), first, first + static_cast<std::ptrdiff_t>(band.height * width));
        }
        EXPECT_EQ(values<T>(bandShared->frame), values<T>(bandAlone->frame));
        EXPECT_EQ(values<T>(bandShared->frame), bandRows);

        std::stop_source stop;
        RecordedProgress stopping(&stop);
        auto [stopped, stoppedCounters] = readWith(8u, &stopping, stop.get_token());
        ASSERT_FALSE(stopped.hasValue());
        EXPECT_EQ(stopped.status().code(), an::ErrorCode::Cancelled);

        std::filesystem::resize_file(directory / "frame.fits",
                                     std::filesystem::file_size(directory / "frame.fits") - 2880u * 4u);
        auto [shortAlone, shortAloneCounters] = readWith(1u, nullptr, {});
        auto [shortShared, shortSharedCounters] = readWith(8u, nullptr, {});
        ASSERT_FALSE(shortAlone.hasValue());
        ASSERT_FALSE(shortShared.hasValue());
        EXPECT_EQ(shortShared.status().code(), shortAlone.status().code());
        EXPECT_EQ(shortShared.status().code(), an::ErrorCode::InvalidData);
    };
    check(2048u, 1400u, 3u, 0.25f);
    check(4096u, 4200u, 1u, std::uint16_t{7u});
}

TEST_F(FitsCodecTest, ARowReaderDecodesBandAfterBandIntoCallerMemoryAsARegionDecodeDoes)
{
    // Opened once, a row reader decodes band after band into memory the caller holds, each band the
    // samples a region decode of the same rows returns, in any order, and refuses what that decode
    // refuses without touching anything else.
    constexpr std::uint32_t kWidth = 13u;
    constexpr std::uint32_t kHeight = 9u;
    std::vector<std::uint16_t> data(static_cast<std::size_t>(kWidth) * kHeight * 3u);
    for (std::size_t index = 0u; index < data.size(); ++index)
        data[index] = static_cast<std::uint16_t>(index * 977u + 3u);
    const auto source = frame(data, kWidth, kHeight, 3u);
    ASSERT_TRUE(save(source).hasValue()) << lastError;
    auto execution = an::ExecutionContext::create(engine, {});
    ASSERT_TRUE(execution.hasValue());
    const auto sourcePath = path();
    auto opened = an::ImageRowReader::open(engine, *execution, {sourcePath});
    ASSERT_TRUE(opened.hasValue()) << opened.status().message();
    an::ImageRowReader reader = std::move(*opened);
    ASSERT_TRUE(reader.valid());
    ASSERT_NE(reader.info(), nullptr);
    EXPECT_EQ(reader.info()->descriptor.extent().height(), kHeight);
    // An uncompressed payload is read row by row as it is stored, so no band decodes more than its own rows.
    EXPECT_EQ(reader.decodeGranularityRows(), 1u);

    for (const auto& [first, rows] :
         std::vector<std::pair<std::uint32_t, std::uint32_t>>{{6u, 3u}, {0u, 3u}, {3u, 3u}, {8u, 1u}, {0u, kHeight}})
    {
        const auto layout = reader.rowsLayout(rows);
        ASSERT_TRUE(layout.hasValue());
        EXPECT_EQ(layout->planeCount(), 3u);
        EXPECT_EQ(layout->rowStrideBytes(), kWidth * sizeof(std::uint16_t));
        std::vector<std::byte> band(static_cast<std::size_t>(layout->totalBytes()) + 5u, std::byte{0xA5});
        const auto read = reader.readRows(*execution, first, rows, band);
        ASSERT_TRUE(read.hasValue()) << read.status().message();
        EXPECT_EQ(read->totalBytes(), layout->totalBytes());
        const auto region = an::decodeImageRegion(engine, *execution, {sourcePath}, {0u, first, kWidth, rows});
        ASSERT_TRUE(region.hasValue());
        EXPECT_EQ(
            std::memcmp(band.data(), region->frame.bytes().data(), static_cast<std::size_t>(layout->totalBytes())), 0)
            << "rows " << first << " to " << first + rows - 1u;
        EXPECT_EQ(band.back(), std::byte{0xA5}) << "nothing past the band is written";
    }

    std::vector<std::byte> band(static_cast<std::size_t>(reader.rowsLayout(2u)->totalBytes()));
    EXPECT_EQ(reader.readRows(*execution, 8u, 2u, band).status().code(), an::ErrorCode::OutOfRange);
    EXPECT_EQ(reader.readRows(*execution, 0u, 0u, band).status().code(), an::ErrorCode::OutOfRange);
    EXPECT_EQ(reader.readRows(*execution, 9u, 1u, band).status().code(), an::ErrorCode::OutOfRange);
    EXPECT_EQ(reader.rowsLayout(kHeight + 1u).status().code(), an::ErrorCode::OutOfRange);
    EXPECT_EQ(reader.readRows(*execution, 0u, 3u, band).status().code(), an::ErrorCode::IncompatibleArguments)
        << "a destination shorter than the band";
    auto other = makeEngine();
    ASSERT_TRUE(other.hasValue());
    auto foreign = an::ExecutionContext::create(*other, {});
    EXPECT_EQ(reader.readRows(*foreign, 0u, 2u, band).status().code(), an::ErrorCode::IncompatibleArguments);
    an::ImageRowReader moved = std::move(reader);
    EXPECT_TRUE(moved.valid());
    EXPECT_FALSE(reader.valid());
    EXPECT_EQ(reader.info(), nullptr);
    EXPECT_EQ(reader.readRows(*execution, 0u, 2u, band).status().code(), an::ErrorCode::NotInitialized);
    EXPECT_EQ(reader.rowsLayout(2u).status().code(), an::ErrorCode::NotInitialized);

    EXPECT_FALSE(an::ImageRowReader::open(engine, *execution, {path("absent.fits")}).hasValue());

    // A stopped operation reads nothing. The engine grants one execution its whole width at a time.
    *execution = an::ExecutionContext{};
    std::stop_source stop;
    stop.request_stop();
    an::ExecutionOptions stopping;
    stopping.stopToken = stop.get_token();
    auto stopped = an::ExecutionContext::create(engine, stopping);
    ASSERT_TRUE(stopped.hasValue());
    EXPECT_EQ(moved.readRows(*stopped, 0u, 2u, band).status().code(), an::ErrorCode::Cancelled);
}

TEST_F(FitsCodecTest, ARowReaderRefusesTheRowsAShortPayloadNoLongerHolds)
{
    // A payload that ends early is refused for the rows it no longer holds and read for the rest.
    std::vector<std::uint16_t> tall(1024u * 8u);
    for (std::size_t index = 0u; index < tall.size(); ++index)
        tall[index] = static_cast<std::uint16_t>(index);
    const auto source = frame(tall, 1024u, 8u);
    ASSERT_TRUE(save(source).hasValue()) << lastError;
    std::filesystem::resize_file(directory / "frame.fits",
                                 std::filesystem::file_size(directory / "frame.fits") - 2880u);
    auto execution = an::ExecutionContext::create(engine, {});
    ASSERT_TRUE(execution.hasValue());
    auto shortened = an::ImageRowReader::open(engine, *execution, {path()});
    ASSERT_TRUE(shortened.hasValue()) << shortened.status().message();
    std::vector<std::byte> rows(2u * 1024u * sizeof(std::uint16_t));
    ASSERT_TRUE(shortened->readRows(*execution, 0u, 2u, rows).hasValue());
    EXPECT_EQ(std::memcmp(rows.data(), tall.data(), rows.size()), 0);
    EXPECT_EQ(shortened->readRows(*execution, 6u, 2u, rows).status().code(), an::ErrorCode::InvalidData);
}

TEST_F(FitsCodecTest, DecodesIndependentUnsignedOffsetFixtureWithoutNormalization)
{
    const auto path =
        std::filesystem::temp_directory_path() /
        ("an-fits-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".fits");
    struct Cleanup
    {
        std::filesystem::path path;
        ~Cleanup()
        {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }
    } cleanup{path};
    std::string header;
    for (const std::string card :
         {"SIMPLE  =                    T", "BITPIX  =                   16", "NAXIS   =                    2",
          "NAXIS1  =                    3", "NAXIS2  =                    1", "BZERO   =                32768",
          "BSCALE  =                    1", "END"})
        header += card + std::string(80u - card.size(), ' ');
    header.resize(2880u, ' ');
    std::string payload(2880u, '\0');
    payload[0] = static_cast<char>(0x80); // stored -32768 is logical zero.
    payload[4] = static_cast<char>(0x7f);
    payload[5] = static_cast<char>(0xff);
    {
        std::ofstream out(path, std::ios::binary);
        out.write(header.data(), header.size());
        out.write(payload.data(), payload.size());
    }

    auto adapter = an::createFitsCodec();
    ASSERT_TRUE(adapter.hasValue());
    const std::array adapters{*adapter};
    auto registry = an::CodecRegistry::create(adapters);
    ASSERT_TRUE(registry.hasValue());
    an::EngineContextOptions options;
    options.codecRegistry = *registry;
    auto engine = an::EngineContext::create(options);
    ASSERT_TRUE(engine.hasValue());
    auto execution = an::ExecutionContext::create(*engine, {});
    ASSERT_TRUE(execution.hasValue());
    auto decoded = an::decodeImage(*engine, *execution, {path.string()});
    ASSERT_TRUE(decoded.hasValue());
    EXPECT_EQ(decoded->frame.descriptor()->residentSampleType(), an::SampleType::UInt16);
    std::array<std::uint16_t, 3> values{};
    std::memcpy(values.data(), decoded->frame.bytes().data(), sizeof(values));
    EXPECT_EQ(values[0], 0u);
    EXPECT_EQ(values[1], 32768u);
    EXPECT_EQ(values[2], 65535u);
}
} // namespace
