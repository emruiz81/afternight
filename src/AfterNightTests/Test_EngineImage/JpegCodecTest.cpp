// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/IO/JpegCodec.h>

#include <AfterNightEngine/Core/Version.h>
#include <AfterNightEngine/IO/ArtifactIdentity.h>
#include <AfterNightEngine/IO/CodecError.h>
#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/IO/CodecMetadataProjection.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/ImageCodec.h>
#include <AfterNightEngine/IO/OutputTransaction.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/ProgressSink.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace
{
/// One component of a hand-built header: what a scan says about it.
struct FixtureComponent
{
    unsigned char id = 1u;
    unsigned char sampling = 0x11u;
    unsigned char table = 0u;
};

class JpegCodecTest : public testing::Test
{
protected:
    void SetUp() override
    {
        directory = std::filesystem::temp_directory_path() /
                    ("an-jpeg-suite-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
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

    static an::Result<an::EngineContext> makeEngine(std::uint64_t budget = 0u)
    {
        auto adapter = an::createJpegCodec();
        if (!adapter.hasValue())
        {
            return adapter.status();
        }
        std::array adapters{*adapter};
        auto registry = an::CodecRegistry::create(adapters);
        if (!registry.hasValue())
        {
            return registry.status();
        }
        an::EngineContextOptions options;
        options.codecRegistry = *registry;
        if (budget != 0u)
        {
            options.limits.memoryBudgetBytes = budget;
        }
        return an::EngineContext::create(options);
    }

    std::string path(const char* name = "frame.jpg") const
    {
        const auto utf8 =
            (directory / std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(name)))).u8string();
        return {reinterpret_cast<const char*>(utf8.data()), utf8.size()};
    }

    static an::CodecCard card(std::string key, std::string value, std::string comment)
    {
        return *an::CodecCard::create(an::CodecNamespace::Jpeg, std::move(key), std::move(value), std::move(comment));
    }

    /// The samples of a frame in its own arrangement, read through its own layout.
    static std::vector<std::uint8_t> values(const an::RawFrame& frame)
    {
        const auto layout = *frame.layout();
        std::vector<std::uint8_t> result;
        for (std::uint32_t plane = 0u; plane < layout.planeCount(); ++plane)
        {
            for (std::uint32_t y = 0u; y < layout.extent().height(); ++y)
            {
                for (std::uint32_t x = 0u; x < layout.extent().width() * layout.samplesPerPixel(); ++x)
                {
                    result.push_back(static_cast<std::uint8_t>(
                        frame.bytes()[plane * layout.planeStrideBytes() + y * layout.rowStrideBytes() + x]));
                }
            }
        }
        return result;
    }

    /// A frame of eight-bit samples in the stated arrangement.
    an::RawFrame frame(const std::vector<std::uint8_t>& data, std::uint32_t width, std::uint32_t height,
                       std::uint32_t channels = 1u,
                       an::StorageArrangement arrangement = an::StorageArrangement::Interleaved, bool padded = false,
                       an::CoordinateOrigin origin = an::CoordinateOrigin::TopLeft, an::CfaDescriptor cfa = {})
    {
        const auto extent = *an::ImageExtent::create(width, height, channels, engine.limits());
        const auto descriptor =
            *an::RawFrameDescriptor::create({extent,
                                             {1u, an::RawSampleClass::UnsignedInteger},
                                             an::ByteOrder::Native,
                                             channels == 1u ? an::ChannelSchema::Mono : an::ChannelSchema::Rgb,
                                             an::ColorSpace::Unspecified,
                                             an::NumericDomain::EncodedNonlinear,
                                             cfa,
                                             origin,
                                             1.0});
        const bool planar = arrangement == an::StorageArrangement::Planar && channels > 1u;
        const auto layout = *an::ImageLayout::create(
            {extent, 1u, planar ? 1u : channels, planar ? channels : 1u, padded ? 64u : 1u}, engine.limits());
        auto buffer = an::SampleBuffer::createOwned(engine, layout, an::StorageInitialization::Zeroed);
        auto raw = an::RawFrame::create(descriptor, layout, std::move(*buffer));
        auto bytes = *raw->mutableBytes();
        const auto samplesPerRow = static_cast<std::size_t>(width) * layout.samplesPerPixel();
        for (std::uint32_t plane = 0u; plane < layout.planeCount(); ++plane)
        {
            for (std::uint32_t y = 0u; y < height; ++y)
            {
                for (std::size_t x = 0u; x < samplesPerRow; ++x)
                {
                    bytes[plane * layout.planeStrideBytes() + y * layout.rowStrideBytes() + x] =
                        static_cast<std::byte>(data[(plane * height + y) * samplesPerRow + x]);
                }
            }
        }
        return std::move(*raw);
    }

    /// A grey ramp, which survives a quality of a hundred within a code or two.
    static std::vector<std::uint8_t> ramp(std::uint32_t width, std::uint32_t height, std::uint32_t channels = 1u)
    {
        std::vector<std::uint8_t> data(static_cast<std::size_t>(width) * height * channels);
        for (std::size_t index = 0u; index < data.size(); ++index)
        {
            data[index] = static_cast<std::uint8_t>((index / channels) % 251u);
        }
        return data;
    }

    an::Result<an::OutputReport> save(const an::RawFrame& source, const an::OutputIntent* intent,
                                      const an::ImageMetadata& metadata = {},
                                      const an::ArtifactIdentity* identity = nullptr, const char* name = "frame.jpg")
    {
        auto execution = an::ExecutionContext::create(engine, {});
        if (!execution.hasValue())
        {
            return execution.status();
        }
        auto result =
            an::writeImage(engine, *execution, {.destinationPath = path(name), .intent = intent, .identity = identity},
                           source, metadata);
        lastError = std::string(result.status().message()) + " / " + std::string(result.status().rootCause().message());
        return result;
    }

    an::Result<an::CodecDecodeResult> decode(const char* name = "frame.jpg", bool metadata = true)
    {
        auto execution = an::ExecutionContext::create(engine, {});
        if (!execution.hasValue())
        {
            return execution.status();
        }
        auto result = an::decodeImage(engine, *execution, {.path = path(name), .readMetadata = metadata});
        lastError = std::string(result.status().message()) + " / " + std::string(result.status().rootCause().message());
        return result;
    }

    /// The intent a quality-stated write uses.
    static an::OutputIntent intent(std::optional<std::uint32_t> quality = std::nullopt,
                                   std::vector<an::CodecCard> cards = {})
    {
        return *an::OutputIntent::create({.sampleType = an::SampleType::UInt8,
                                          .compression = an::OutputCompression::Jpeg,
                                          .compressionLevel = quality,
                                          .cards = std::move(cards)});
    }

    /// Writes a container this engine's encoder cannot produce, so a refusal can be exercised.
    ///
    /// Only the markers a header read consults are written: the frame header states the geometry,
    /// the precision and the components, and the scan header ends the read.
    void writeHeader(const char* name, unsigned char precision, std::uint16_t width, std::uint16_t height,
                     const std::vector<FixtureComponent>& components)
    {
        std::string file("\xff\xd8", 2u);
        const auto length = static_cast<std::uint16_t>(8u + 3u * components.size());
        file += '\xff';
        file += '\xc0';
        file += static_cast<char>(length >> 8u);
        file += static_cast<char>(length & 0xffu);
        file += static_cast<char>(precision);
        file += static_cast<char>(height >> 8u);
        file += static_cast<char>(height & 0xffu);
        file += static_cast<char>(width >> 8u);
        file += static_cast<char>(width & 0xffu);
        file += static_cast<char>(components.size());
        for (const FixtureComponent& component : components)
        {
            file += static_cast<char>(component.id);
            file += static_cast<char>(component.sampling);
            file += static_cast<char>(component.table);
        }
        const auto scanLength = static_cast<std::uint16_t>(6u + 2u * components.size());
        file += '\xff';
        file += '\xda';
        file += static_cast<char>(scanLength >> 8u);
        file += static_cast<char>(scanLength & 0xffu);
        file += static_cast<char>(components.size());
        for (const FixtureComponent& component : components)
        {
            file += static_cast<char>(component.id);
            file += '\x00';
        }
        file += '\x00';
        file += '\x3f';
        file += '\x00';
        write(name, file);
    }

    void write(const char* name, std::string_view bytes) const
    {
        const auto target = path(name);
        std::ofstream out(
            std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(target.data()), target.size())),
            std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    std::uint64_t fileSize(const char* name) const
    {
        const auto target = path(name);
        std::error_code error;
        const auto size = std::filesystem::file_size(
            std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(target.data()), target.size())),
            error);
        return error ? 0u : static_cast<std::uint64_t>(size);
    }

    std::filesystem::path directory;
    an::EngineContext engine;
    std::string lastError;
};

TEST_F(JpegCodecTest, ExifKeepsCameraFieldsWithoutDecodingXmpInEitherByteOrder)
{
    for (const bool little : {true, false})
    {
        const auto source = frame(ramp(8u, 8u), 8u, 8u);
        const auto written = intent(95u);
        ASSERT_TRUE(save(source, &written).hasValue()) << lastError;
        std::ifstream input(path(), std::ios::binary);
        std::string jpeg((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        input.close();
        const std::string make("AfterNight\0", 11u);
        const std::string xmp = "<x:xmpmeta xmlns:x='adobe:ns:meta/'><rdf:RDF "
                                "xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'/></x:xmpmeta>";
        std::string exif = little ? "II" : "MM";
        const auto number = [&](std::uint32_t value, unsigned width) {
            for (unsigned i = 0u; i < width; ++i)
                exif += static_cast<char>(value >> (8u * (little ? i : width - 1u - i)));
        };
        number(42u, 2u);
        number(8u, 4u);
        number(2u, 2u);
        number(271u, 2u);
        number(2u, 2u);
        number(static_cast<std::uint32_t>(make.size()), 4u);
        number(38u, 4u);
        number(700u, 2u);
        number(1u, 2u);
        number(static_cast<std::uint32_t>(xmp.size()), 4u);
        number(49u, 4u);
        number(0u, 4u);
        exif += make + xmp;
        const std::string payload = std::string("Exif\0\0", 6u) + exif;
        const auto length = payload.size() + 2u;
        std::string marker("\xff\xe1", 2u);
        marker += static_cast<char>(length >> 8u);
        marker += static_cast<char>(length);
        marker += payload;
        jpeg.insert(2u, marker);
        {
            std::ofstream output(path(), std::ios::binary | std::ios::trunc);
            output.write(jpeg.data(), static_cast<std::streamsize>(jpeg.size()));
        }
        auto decoded = decode();
        ASSERT_TRUE(decoded.hasValue()) << lastError;
        const auto& metadata = decoded->metadata.codec();
        const auto* camera = metadata.find(an::CodecNamespace::Jpeg, "Exif.Image.Make");
        ASSERT_NE(camera, nullptr);
        EXPECT_EQ(camera->value(), "AfterNight");
        EXPECT_EQ(metadata.find(an::CodecNamespace::Jpeg, "Exif.Image.XMLPacket"), nullptr);
    }
}

TEST_F(JpegCodecTest, GreyRampSurvivesAWriteAndReadAtTheHighestQuality)
{
    const auto data = ramp(64u, 32u);
    const auto source = frame(data, 64u, 32u);
    const auto written = intent(100u);
    ASSERT_TRUE(save(source, &written).hasValue()) << lastError;
    auto decoded = decode();
    ASSERT_TRUE(decoded.hasValue()) << lastError;
    const auto samples = values(decoded->frame);
    ASSERT_EQ(samples.size(), data.size());
    int worst = 0;
    for (std::size_t index = 0u; index < samples.size(); ++index)
    {
        worst = (std::max)(worst, std::abs(static_cast<int>(samples[index]) - static_cast<int>(data[index])));
    }
    EXPECT_LE(worst, 3) << "the transform is lossy, but not by more than a code or two at this quality";
    EXPECT_EQ(decoded->frame.descriptor()->channelSchema(), an::ChannelSchema::Mono);
    EXPECT_EQ(decoded->frame.descriptor()->numericDomain(), an::NumericDomain::EncodedNonlinear);
    EXPECT_EQ(decoded->frame.descriptor()->colorSpace(), an::ColorSpace::Unspecified);
}

TEST_F(JpegCodecTest, MultiBufferOutputReportsTheCompleteClosedFileSize)
{
    std::vector<std::uint8_t> input(512u * 256u);
    std::uint32_t random = 17u;
    for (auto& value : input)
    {
        random = random * 1664525u + 1013904223u;
        value = static_cast<std::uint8_t>(random >> 24u);
    }
    const auto source = frame(input, 512u, 256u);
    const auto intent = *an::OutputIntent::create(
        {.sampleType = an::SampleType::UInt8, .compression = an::OutputCompression::Jpeg, .compressionLevel = 100u});
    const auto report = save(source, &intent);
    ASSERT_TRUE(report.hasValue()) << lastError;
    EXPECT_GT(report->bytesWritten, 65536u);
    EXPECT_EQ(report->bytesWritten, std::filesystem::file_size(std::filesystem::path(path())));
    EXPECT_TRUE(decode().hasValue()) << lastError;
}

TEST_F(JpegCodecTest, PlanarAndInterleavedColourFramesProduceTheSameImage)
{
    const auto data = ramp(32u, 16u, 3u);
    std::vector<std::uint8_t> planar(data.size());
    for (std::size_t pixel = 0u; pixel < data.size() / 3u; ++pixel)
    {
        for (std::size_t channel = 0u; channel < 3u; ++channel)
        {
            planar[channel * (data.size() / 3u) + pixel] = data[pixel * 3u + channel];
        }
    }
    const auto written = intent(100u);
    ASSERT_TRUE(save(frame(data, 32u, 16u, 3u), &written, {}, nullptr, "interleaved.jpg").hasValue()) << lastError;
    ASSERT_TRUE(save(frame(planar, 32u, 16u, 3u, an::StorageArrangement::Planar), &written, {}, nullptr, "planar.jpg")
                    .hasValue())
        << lastError;
    auto first = decode("interleaved.jpg");
    auto second = decode("planar.jpg");
    ASSERT_TRUE(first.hasValue());
    ASSERT_TRUE(second.hasValue());
    EXPECT_EQ(values(first->frame), values(second->frame));
    EXPECT_EQ(first->frame.descriptor()->channelSchema(), an::ChannelSchema::Rgb);
    EXPECT_EQ(first->frame.layout()->samplesPerPixel(), 3u);
    EXPECT_EQ(first->frame.layout()->planeCount(), 1u);
}

TEST_F(JpegCodecTest, PaddedRowsAreWrittenWithoutTheirPadding)
{
    const auto data = ramp(24u, 8u);
    const auto written = intent(100u);
    ASSERT_TRUE(save(frame(data, 24u, 8u, 1u, an::StorageArrangement::Interleaved, true), &written).hasValue())
        << lastError;
    auto decoded = decode();
    ASSERT_TRUE(decoded.hasValue()) << lastError;
    EXPECT_EQ(values(decoded->frame).size(), data.size());
}

TEST_F(JpegCodecTest, OddHeightPaddedImagesDecodeEveryRowInGreyAndColour)
{
    const auto written = intent(100u);
    for (const auto channels : {1u, 3u})
        for (const auto height : {1u, 17u, 33u})
        {
            SCOPED_TRACE(channels);
            SCOPED_TRACE(height);
            const auto data = ramp(37u, height, channels);
            ASSERT_TRUE(save(frame(data, 37u, height, channels), &written).hasValue()) << lastError;
            auto packed = decode();
            ASSERT_TRUE(packed.hasValue()) << lastError;
            ASSERT_TRUE(save(frame(data, 37u, height, channels, an::StorageArrangement::Interleaved, true), &written)
                            .hasValue())
                << lastError;
            auto padded = decode();
            ASSERT_TRUE(padded.hasValue()) << lastError;
            EXPECT_EQ(values(padded->frame).size(), data.size());
            EXPECT_EQ(values(padded->frame), values(packed->frame));
        }
}

TEST_F(JpegCodecTest, CompleteGreyBlocksMatchTheScanlinePathAcrossQualitiesAndPadding)
{
    // Appending a row after a complete grey DCT block changes no preceding block. The odd
    // height uses the library's ordinary scanlines, independently checking the direct blocks.
    for (const auto width : {8u, 24u, 64u})
        for (const auto quality : {1u, 50u, 95u, 100u})
        {
            SCOPED_TRACE(width);
            SCOPED_TRACE(quality);
            constexpr std::uint32_t kHeight = 16u;
            std::vector<std::uint8_t> extended(width * (kHeight + 1u));
            std::uint32_t random = 17u;
            for (auto& sample : extended)
            {
                random = random * 1664525u + 1013904223u;
                sample = static_cast<std::uint8_t>(random >> 24u);
            }
            const auto written = intent(quality);
            ASSERT_TRUE(save(frame(extended, width, kHeight + 1u), &written).hasValue()) << lastError;
            auto reference = decode();
            ASSERT_TRUE(reference.hasValue()) << lastError;
            auto expected = values(reference->frame);
            expected.resize(width * kHeight);
            extended.resize(width * kHeight);
            for (const bool padded : {false, true})
            {
                ASSERT_TRUE(save(frame(extended, width, kHeight, 1u, an::StorageArrangement::Planar, padded), &written)
                                .hasValue())
                    << lastError;
                auto blocks = decode();
                ASSERT_TRUE(blocks.hasValue()) << lastError;
                EXPECT_EQ(values(blocks->frame), expected);
            }
        }
}

TEST_F(JpegCodecTest, DirectEncodingTruncatesAnOlderLongerPendingPayload)
{
    const auto pending = path("pending.jpg");
    {
        std::ofstream old(std::filesystem::path(pending), std::ios::binary);
        const std::string padding(131072u, 'x');
        old.write(padding.data(), static_cast<std::streamsize>(padding.size()));
    }
    const auto source = frame(ramp(37u, 33u), 37u, 33u);
    const auto written = intent(95u);
    auto execution = an::ExecutionContext::create(engine, {});
    ASSERT_TRUE(execution.hasValue());
    const auto encoded = an::encodeImage(
        engine, *execution, {.pendingPath = pending, .format = an::CodecFormat::Jpeg, .intent = &written}, source, {});
    ASSERT_TRUE(encoded.hasValue()) << encoded.status().message();
    EXPECT_EQ(encoded->bytesWritten, std::filesystem::file_size(std::filesystem::path(pending)));
    EXPECT_LT(encoded->bytesWritten, 131072u);
    const auto decoded = an::decodeImage(engine, *execution, {.path = pending});
    EXPECT_TRUE(decoded.hasValue()) << decoded.status().message();
}

TEST_F(JpegCodecTest, ReservedOutputsMatchDirectEncodingAndPublishOnlyAfterClose)
{
    class CountingSink final : public an::OutputByteSink
    {
    public:
        void observe(std::span<const std::byte> bytes) noexcept override
        {
            for (const auto byte : bytes)
                hash = (hash ^ static_cast<unsigned char>(byte)) * 1099511628211ull;
            count += bytes.size();
        }
        std::uint64_t hash = 14695981039346656037ull;
        std::uint64_t count = 0u;
    };
    const auto source = frame(ramp(513u, 259u, 3u), 513u, 259u, 3u);
    const auto written = intent(95u, {card("Comment", "transaction bytes", "comment marker")});
    auto execution = an::ExecutionContext::create(engine, {});
    ASSERT_TRUE(execution.hasValue());
    const auto direct = path("direct.jpg");
    ASSERT_TRUE(an::encodeImage(engine, *execution,
                                {.pendingPath = direct, .format = an::CodecFormat::Jpeg, .intent = &written}, source,
                                {})
                    .hasValue());
    std::ifstream directFile(std::filesystem::path(direct), std::ios::binary);
    const std::string expected{std::istreambuf_iterator<char>(directFile), {}};
    CountingSink reference;
    reference.observe(std::as_bytes(std::span(expected.data(), expected.size())));
    for (const auto durability : {an::OutputDurability::CloseOnly, an::OutputDurability::FlushBeforePublish})
    {
        CountingSink sink;
        const auto destination = path("transaction.jpg");
        auto transaction = an::OutputTransaction::begin(
            engine, *execution,
            {.destinationPath = destination, .intent = &written, .durability = durability, .byteSink = &sink});
        ASSERT_TRUE(transaction.hasValue());
        const auto conflicting =
            an::OutputTransaction::begin(engine, *execution, {.destinationPath = destination, .intent = &written});
        ASSERT_FALSE(conflicting.hasValue());
        EXPECT_EQ(conflicting.status().code(), an::ErrorCode::Busy);
        ASSERT_FALSE(transaction->encode(source, {}).failed());
        // All bytes are visible before commit; the reservation does not deny a reader access.
        std::ifstream pending(std::filesystem::path(std::string(transaction->pendingPath())), std::ios::binary);
        ASSERT_TRUE(pending.is_open());
        EXPECT_EQ((std::string{std::istreambuf_iterator<char>(pending), {}}), expected);
        pending.close();
        const auto published = transaction->commit();
        ASSERT_TRUE(published.hasValue()) << published.status().message();
        EXPECT_EQ(published->flushed, durability == an::OutputDurability::FlushBeforePublish);
        EXPECT_EQ(published->observedBytes, reference.count);
        EXPECT_EQ(sink.count, reference.count);
        EXPECT_EQ(sink.hash, reference.hash);
        std::ifstream result(std::filesystem::path(destination), std::ios::binary);
        ASSERT_TRUE(result.is_open());
        EXPECT_EQ((std::string{std::istreambuf_iterator<char>(result), {}}), expected);
    }
}

TEST_F(JpegCodecTest, CancellationDuringBatchedEncodingClosesAndRemovesTheReservation)
{
    class StopOnProgress final : public an::ProgressSink
    {
    public:
        void onProgress(const an::ProgressReport&) noexcept override
        {
            stop.request_stop();
        }
        std::stop_source stop;
    } sink;
    const auto source = frame(ramp(129u, 65u, 3u), 129u, 65u, 3u);
    const auto written = intent(95u);
    auto execution = an::ExecutionContext::create(engine, {.stopToken = sink.stop.get_token(), .progressSink = &sink});
    ASSERT_TRUE(execution.hasValue());
    const auto destination = path("cancel.jpg");
    auto transaction =
        an::OutputTransaction::begin(engine, *execution, {.destinationPath = destination, .intent = &written});
    ASSERT_TRUE(transaction.hasValue());
    const auto status = transaction->encode(source, {});
    EXPECT_EQ(status.code(), an::ErrorCode::Cancelled);
    EXPECT_FALSE(transaction->valid());
    EXPECT_TRUE(std::filesystem::is_empty(directory));
}

TEST_F(JpegCodecTest, TheStatedQualityDecidesTheFileAndTheDefaultIsNinetyFive)
{
    const auto data = ramp(64u, 64u);
    const auto source = frame(data, 64u, 64u);
    const auto low = intent(10u);
    const auto high = intent(100u);
    const auto stated = intent(95u);
    const auto unstated = intent();
    ASSERT_TRUE(save(source, &low, {}, nullptr, "low.jpg").hasValue()) << lastError;
    ASSERT_TRUE(save(source, &high, {}, nullptr, "high.jpg").hasValue()) << lastError;
    ASSERT_TRUE(save(source, &stated, {}, nullptr, "stated.jpg").hasValue()) << lastError;
    ASSERT_TRUE(save(source, &unstated, {}, nullptr, "unstated.jpg").hasValue()) << lastError;
    EXPECT_LT(fileSize("low.jpg"), fileSize("high.jpg"));
    EXPECT_EQ(fileSize("stated.jpg"), fileSize("unstated.jpg"))
        << "a write that states no quality writes the default quality, 95";
}

TEST_F(JpegCodecTest, EveryCompressionButThisContainersOwnIsRefusedBeforeTheFileIsOpened)
{
    const auto source = frame(ramp(8u, 8u), 8u, 8u);
    const auto plain = *an::OutputIntent::create({.sampleType = an::SampleType::UInt8});
    const auto result = save(source, &plain);
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(an::codecFailureReason(result.status().cause()), an::CodecFailure::UnsupportedRepresentation)
        << lastError;
    EXPECT_FALSE(std::filesystem::exists(
        std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(path().data()), path().size()))));
}

TEST_F(JpegCodecTest, ASixteenBitFrameIsRefusedByName)
{
    const auto extent = *an::ImageExtent::create(4u, 4u, 1u, engine.limits());
    const auto descriptor = *an::RawFrameDescriptor::create({extent,
                                                             {2u, an::RawSampleClass::UnsignedInteger},
                                                             an::ByteOrder::Native,
                                                             an::ChannelSchema::Mono,
                                                             an::ColorSpace::Unspecified,
                                                             an::NumericDomain::EncodedNonlinear,
                                                             {},
                                                             an::CoordinateOrigin::TopLeft,
                                                             1.0});
    const auto layout = *an::ImageLayout::create({extent, 2u, 1u, 1u, 1u}, engine.limits());
    auto buffer = an::SampleBuffer::createOwned(engine, layout, an::StorageInitialization::Zeroed);
    auto source = an::RawFrame::create(descriptor, layout, std::move(*buffer));
    // The intent states the representation the frame actually has, so what refuses is this adapter
    // rather than the facade's check that the two agree.
    const auto written = *an::OutputIntent::create(
        {.sampleType = an::SampleType::UInt16, .compression = an::OutputCompression::Jpeg, .compressionLevel = 90u});
    const auto result = save(*source, &written);
    ASSERT_FALSE(result.hasValue());
    // The capability record states one representation, so the registry refuses this write before
    // the adapter is called at all - which is why the reason is on the refusal itself rather than
    // on the cause an adapter's own refusal travels in.
    EXPECT_EQ(an::codecFailureReason(result.status()), an::CodecFailure::UnsupportedRepresentation) << lastError;
}

TEST_F(JpegCodecTest, AMosaicedOrBottomUpFrameIsRefusedRatherThanFlattenedOrFlipped)
{
    const auto data = ramp(8u, 8u);
    const auto written = intent(90u);
    const auto mosaic = *an::CfaDescriptor::create(an::CfaPattern::Rggb, 0u, 0u);
    const auto first =
        save(frame(data, 8u, 8u, 1u, an::StorageArrangement::Interleaved, false, an::CoordinateOrigin::TopLeft, mosaic),
             &written, {}, nullptr, "mosaic.jpg");
    ASSERT_FALSE(first.hasValue());
    EXPECT_EQ(an::codecFailureReason(first.status().cause()), an::CodecFailure::UnsupportedRepresentation);
    const auto second =
        save(frame(data, 8u, 8u, 1u, an::StorageArrangement::Interleaved, false, an::CoordinateOrigin::BottomLeft),
             &written, {}, nullptr, "bottom.jpg");
    ASSERT_FALSE(second.hasValue());
    EXPECT_EQ(an::codecFailureReason(second.status().cause()), an::CodecFailure::UnsupportedRepresentation);
}

TEST_F(JpegCodecTest, SeparatedInkContainersAndOtherPrecisionsAreRefusedByName)
{
    writeHeader("cmyk.jpg", 8u, 16u, 16u, {{1u}, {2u}, {3u}, {4u}});
    auto cmyk = decode("cmyk.jpg");
    ASSERT_FALSE(cmyk.hasValue());
    EXPECT_EQ(an::codecFailureReason(cmyk.status().cause()), an::CodecFailure::UnsupportedRepresentation) << lastError;
    writeHeader("deep.jpg", 12u, 16u, 16u, {{1u}});
    auto deep = decode("deep.jpg");
    ASSERT_FALSE(deep.hasValue()) << "twelve bits of precision is not a representation this adapter reads";
}

TEST_F(JpegCodecTest, AContainerThatIsNotOneIsRefusedBeforeAnythingIsAllocated)
{
    write("broken.jpg", "this is not a container at all, whatever its name says");
    auto decoded = decode("broken.jpg");
    ASSERT_FALSE(decoded.hasValue());
    EXPECT_EQ(an::codecFailureReason(decoded.status().cause()), an::CodecFailure::MetadataUnreadable) << lastError;
}

TEST_F(JpegCodecTest, ATruncatedPayloadIsRefusedRatherThanCompletedWithInventedRows)
{
    const auto data = ramp(64u, 64u);
    const auto written = intent(95u);
    ASSERT_TRUE(save(frame(data, 64u, 64u), &written).hasValue()) << lastError;
    const auto target = path();
    const std::filesystem::path file(
        std::u8string_view(reinterpret_cast<const char8_t*>(target.data()), target.size()));
    const auto size = std::filesystem::file_size(file);
    std::error_code error;
    std::filesystem::resize_file(file, size / 2u, error);
    ASSERT_FALSE(error);
    auto decoded = decode();
    ASSERT_FALSE(decoded.hasValue());
    EXPECT_EQ(an::codecFailureReason(decoded.status().cause()), an::CodecFailure::PayloadUnreadable) << lastError;
}

TEST_F(JpegCodecTest, ExifTravelsBothWaysAndTheEngineThatWroteTheFileSaysSo)
{
    const auto data = ramp(16u, 16u);
    const auto written = intent(
        95u, {card("Exif.Photo.ExposureTime", "1/250", "Rational"), card("Exif.Image.Make", "AfterNight", "Ascii")});
    ASSERT_TRUE(save(frame(data, 16u, 16u), &written).hasValue()) << lastError;
    auto decoded = decode();
    ASSERT_TRUE(decoded.hasValue()) << lastError;
    const auto& codec = decoded->metadata.codec();
    const auto* const exposure = codec.find(an::CodecNamespace::Jpeg, "Exif.Photo.ExposureTime");
    ASSERT_NE(exposure, nullptr);
    EXPECT_EQ(exposure->value(), "1/250");
    const auto* const make = codec.find(an::CodecNamespace::Jpeg, "Exif.Image.Make");
    ASSERT_NE(make, nullptr);
    EXPECT_EQ(make->value(), "AfterNight");
    const auto* const software = codec.find(an::CodecNamespace::Jpeg, "Exif.Image.Software");
    ASSERT_NE(software, nullptr);
    EXPECT_EQ(software->value(), an::kEngineVersionString);
}

TEST_F(JpegCodecTest, AcquisitionFactsAreStatedInTheContainersOwnVocabulary)
{
    auto acquisition = an::AcquisitionMetadata::create({.instrumentName = "Test Camera", .exposureSeconds = 2.5});
    ASSERT_TRUE(acquisition.hasValue());
    const auto metadata = an::ImageMetadata::create(std::move(*acquisition), {}, {}, {}, {}, std::nullopt);
    const auto written = intent(95u);
    ASSERT_TRUE(save(frame(ramp(16u, 16u), 16u, 16u), &written, metadata).hasValue()) << lastError;
    auto decoded = decode();
    ASSERT_TRUE(decoded.hasValue()) << lastError;
    const auto projection = an::projectAcquisition(decoded->metadata.codec());
    EXPECT_NEAR(projection.acquisition.exposureSeconds.value_or(0.0), 2.5, 1e-9);
    EXPECT_EQ(projection.acquisition.instrumentName, "Test Camera");
}

TEST_F(JpegCodecTest, ACommentMarkerTravelsBothWays)
{
    const auto written = intent(95u, {card("Comment", "written by a test", "comment marker")});
    ASSERT_TRUE(save(frame(ramp(16u, 16u), 16u, 16u), &written).hasValue()) << lastError;
    auto decoded = decode();
    ASSERT_TRUE(decoded.hasValue()) << lastError;
    const auto* const comment = decoded->metadata.codec().find(an::CodecNamespace::Jpeg, "Comment");
    ASSERT_NE(comment, nullptr);
    EXPECT_EQ(comment->value(), "written by a test");
}

TEST_F(JpegCodecTest, MetadataMarkerCountIsBoundedBeforeLibjpegRetainsIt)
{
    const auto written = intent(95u);
    ASSERT_TRUE(save(frame(ramp(8u, 8u), 8u, 8u), &written).hasValue()) << lastError;
    std::ifstream input(std::filesystem::path(path()), std::ios::binary);
    const std::string original{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    ASSERT_GE(original.size(), 2u);
    std::string markers;
    markers.reserve((an::kMaxCodecCards + 1u) * 4u);
    for (std::size_t index = 0u; index <= an::kMaxCodecCards; ++index)
        markers.append("\xff\xfe\x00\x02", 4u);
    std::string attacked = original;
    attacked.insert(2u, markers);
    write("markers.jpg", attacked);

    EXPECT_TRUE(decode("markers.jpg", false).hasValue()) << lastError;
    const auto refused = decode("markers.jpg", true);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::LimitExceeded);
    EXPECT_EQ(an::codecFailureReason(refused.status().cause()), an::CodecFailure::MetadataUnreadable);
}

TEST_F(JpegCodecTest, ACardOfAnotherVocabularyIsRefusedRatherThanDroppedSilently)
{
    const auto written = intent(95u, {card("BitDepth", "8", "not a vocabulary this container speaks")});
    const auto result = save(frame(ramp(8u, 8u), 8u, 8u), &written);
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(an::codecFailureReason(result.status().cause()), an::CodecFailure::UnsupportedRepresentation)
        << lastError;
}

TEST_F(JpegCodecTest, AnArtifactIdentityIsRefusedBecauseNothingCouldReadItBack)
{
    auto identity = an::ArtifactIdentity::create("frame-0001", "run-0001");
    ASSERT_TRUE(identity.hasValue());
    const auto written = intent(95u);
    const auto result = save(frame(ramp(8u, 8u), 8u, 8u), &written, {}, &*identity);
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(an::codecFailureReason(result.status().cause()), an::CodecFailure::UnsupportedRepresentation)
        << lastError;
    EXPECT_FALSE(std::filesystem::exists(
        std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(path().data()), path().size()))));
}

TEST_F(JpegCodecTest, ARegionRequestIsRefusedWithoutDecodingAnything)
{
    const auto written = intent(95u);
    ASSERT_TRUE(save(frame(ramp(16u, 16u), 16u, 16u), &written).hasValue()) << lastError;
    auto execution = an::ExecutionContext::create(engine, {});
    ASSERT_TRUE(execution.hasValue());
    const auto source = path();
    auto region =
        an::decodeImageRegion(engine, *execution, {.path = source}, {.x = 0u, .y = 0u, .width = 4u, .height = 4u});
    ASSERT_FALSE(region.hasValue());
    EXPECT_EQ(an::codecFailureReason(region.status()), an::CodecFailure::UnsupportedOperation);
}

TEST_F(JpegCodecTest, TheSecondImageOfAContainerThatHasOneIsRefused)
{
    const auto written = intent(95u);
    ASSERT_TRUE(save(frame(ramp(16u, 16u), 16u, 16u), &written).hasValue()) << lastError;
    auto execution = an::ExecutionContext::create(engine, {});
    ASSERT_TRUE(execution.hasValue());
    const auto source = path();
    auto second = an::probeImage(engine, *execution, {.path = source, .imageIndex = 1u});
    ASSERT_FALSE(second.hasValue());
    EXPECT_EQ(second.status().code(), an::ErrorCode::OutOfRange);
    auto first = an::probeImage(engine, *execution, {.path = source});
    ASSERT_TRUE(first.hasValue());
    EXPECT_EQ(first->imageCount, 1u);
    EXPECT_EQ(first->disposition, an::CodecProbeDisposition::DecodeRequired);
}

TEST_F(JpegCodecTest, ADecodeThatTheBudgetCannotHoldIsRefusedBeforeTheRowsAreRead)
{
    const auto written = intent(95u);
    ASSERT_TRUE(save(frame(ramp(64u, 64u), 64u, 64u), &written).hasValue()) << lastError;
    auto small = makeEngine(2048u);
    ASSERT_TRUE(small.hasValue());
    auto execution = an::ExecutionContext::create(*small, {});
    ASSERT_TRUE(execution.hasValue());
    const auto source = path();
    auto decoded = an::decodeImage(*small, *execution, {.path = source});
    ASSERT_FALSE(decoded.hasValue());
    EXPECT_EQ(decoded.status().code(), an::ErrorCode::MemoryBudgetExceeded);
}

TEST_F(JpegCodecTest, CancellationEndsADecodeWithoutAFrame)
{
    const auto written = intent(95u);
    ASSERT_TRUE(save(frame(ramp(256u, 256u), 256u, 256u), &written).hasValue()) << lastError;
    std::stop_source stop;
    an::ExecutionOptions options;
    options.stopToken = stop.get_token();
    auto execution = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(execution.hasValue());
    stop.request_stop();
    const auto source = path();
    auto decoded = an::decodeImage(engine, *execution, {.path = source});
    ASSERT_FALSE(decoded.hasValue());
    EXPECT_EQ(decoded.status().code(), an::ErrorCode::Cancelled);
}

TEST_F(JpegCodecTest, ProgressCancellationDiscardsAPartiallyDecodedImage)
{
    class StopOnProgress final : public an::ProgressSink
    {
    public:
        std::stop_source stop;
        bool partial = false;
        void onProgress(const an::ProgressReport& report) noexcept override
        {
            partial = report.completed > 0u && report.completed < report.total;
            stop.request_stop();
        }
    } sink;
    const auto written = intent(95u);
    ASSERT_TRUE(save(frame(ramp(129u, 65u, 3u), 129u, 65u, 3u), &written).hasValue()) << lastError;
    auto execution = an::ExecutionContext::create(engine, {.stopToken = sink.stop.get_token(), .progressSink = &sink});
    ASSERT_TRUE(execution.hasValue());
    const auto source = path();
    const auto decoded = an::decodeImage(engine, *execution, {.path = source});
    EXPECT_TRUE(sink.partial);
    EXPECT_FALSE(decoded.hasValue());
    EXPECT_EQ(decoded.status().code(), an::ErrorCode::Cancelled);
}

TEST_F(JpegCodecTest, ALiteralUnicodePathIsWrittenAndReadBack)
{
    const auto written = intent(95u);
    ASSERT_TRUE(save(frame(ramp(16u, 16u), 16u, 16u), &written, {}, nullptr, "\xc3\xa9\xcf\x80-frame.jpg").hasValue())
        << lastError;
    auto decoded = decode("\xc3\xa9\xcf\x80-frame.jpg");
    EXPECT_TRUE(decoded.hasValue()) << lastError;
}

TEST_F(JpegCodecTest, TheCapabilityRecordStatesWhatThisContainerHolds)
{
    const auto capabilities = engine.codecRegistry()->capabilities(an::CodecFormat::Jpeg);
    ASSERT_TRUE(capabilities.has_value());
    EXPECT_EQ(capabilities->decodeSamples, an::codecSampleBit({1u, an::RawSampleClass::UnsignedInteger}));
    EXPECT_EQ(capabilities->encodeSamples, capabilities->decodeSamples);
    EXPECT_EQ(capabilities->maxChannels, 3u);
    EXPECT_FALSE(capabilities->regionDecode);
}

} // namespace
