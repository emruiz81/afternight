// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/IO/PngCodec.h>

#include <AfterNightEngine/Core/Version.h>
#include <AfterNightEngine/IO/ArtifactIdentity.h>
#include <AfterNightEngine/IO/CodecError.h>
#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/ImageCodec.h>
#include <AfterNightEngine/IO/OutputTransaction.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stop_token>
#include <string>
#include <vector>

namespace
{

/// The check every chunk of this container carries.
///
/// @param bytes the chunk's type and payload.
/// @return the check value.
[[nodiscard]] std::uint32_t chunkCheck(std::string_view bytes)
{
    std::uint32_t value = 0xffffffffu;
    for (const char character : bytes)
    {
        value ^= static_cast<unsigned char>(character);
        for (int bit = 0; bit < 8; ++bit)
        {
            value = (value >> 1u) ^ (0xedb88320u & (0u - (value & 1u)));
        }
    }
    return value ^ 0xffffffffu;
}

/// The check a compressed stream of this container carries.
///
/// @param bytes the uncompressed data.
/// @return the check value.
[[nodiscard]] std::uint32_t streamCheck(std::string_view bytes)
{
    std::uint32_t low = 1u;
    std::uint32_t high = 0u;
    for (const char character : bytes)
    {
        low = (low + static_cast<unsigned char>(character)) % 65521u;
        high = (high + low) % 65521u;
    }
    return (high << 16u) | low;
}

/// Four bytes, most significant first, which is how this container states a number.
///
/// @param value the number.
/// @return its bytes.
[[nodiscard]] std::string beNumber(std::uint32_t value)
{
    std::string bytes;
    for (int shift = 24; shift >= 0; shift -= 8)
    {
        bytes += static_cast<char>((value >> shift) & 0xffu);
    }
    return bytes;
}

/// One complete chunk: its length, its type, its payload and its check.
///
/// @param type the chunk's four-character type.
/// @param payload what it holds.
/// @return the chunk.
[[nodiscard]] std::string chunk(std::string_view type, std::string_view payload)
{
    std::string body(type);
    body += payload;
    return beNumber(static_cast<std::uint32_t>(payload.size())) + body + beNumber(chunkCheck(body));
}

/// The data as a compressed stream that compresses nothing.
///
/// Stored blocks are a valid deflate stream, which is what lets a test build a container by hand
/// without a compressor.
///
/// @param data the bytes to carry.
/// @return the stream.
[[nodiscard]] std::string storedStream(std::string_view data)
{
    std::string stream("\x78\x01", 2u);
    std::size_t offset = 0u;
    do
    {
        const auto length = static_cast<std::uint16_t>((std::min)(std::size_t{65535u}, data.size() - offset));
        const bool last = offset + length >= data.size();
        stream += static_cast<char>(last ? 1 : 0);
        stream += static_cast<char>(length & 0xffu);
        stream += static_cast<char>(length >> 8u);
        stream += static_cast<char>((~length) & 0xffu);
        stream += static_cast<char>((~length) >> 8u);
        stream.append(data, offset, length);
        offset += length;
    } while (offset < data.size());
    const auto check = streamCheck(data);
    stream += beNumber(check);
    return stream;
}

class PngCodecTest : public testing::Test
{
protected:
    void SetUp() override
    {
        directory = std::filesystem::temp_directory_path() /
                    ("an-png-suite-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
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
        auto adapter = an::createPngCodec();
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

    std::string path(const char* name = "frame.png") const
    {
        const auto utf8 =
            (directory / std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(name)))).u8string();
        return {reinterpret_cast<const char*>(utf8.data()), utf8.size()};
    }

    static an::CodecCard card(std::string key, std::string value, std::string comment)
    {
        return *an::CodecCard::create(an::CodecNamespace::Png, std::move(key), std::move(value), std::move(comment));
    }

    void write(const char* name, std::string_view bytes) const
    {
        const auto target = path(name);
        std::ofstream out(
            std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(target.data()), target.size())),
            std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    /// A container built by hand, so that a representation this engine's encoder does not produce
    /// can still be read.
    ///
    /// @param name the file to write.
    /// @param width columns.
    /// @param height rows.
    /// @param bitDepth bits per sample.
    /// @param colorType the colour type, as the format numbers them.
    /// @param rows the unfiltered rows, already packed as the format packs them.
    /// @param extra chunks to state between the header and the payload.
    void writeContainer(const char* name, std::uint32_t width, std::uint32_t height, unsigned char bitDepth,
                        unsigned char colorType, const std::vector<std::string>& rows, std::string_view extra = {},
                        unsigned char interlace = 0u) const
    {
        std::string header = beNumber(width) + beNumber(height);
        header += static_cast<char>(bitDepth);
        header += static_cast<char>(colorType);
        header += '\0';
        header += '\0';
        header += static_cast<char>(interlace);
        std::string payload;
        for (const std::string& row : rows)
        {
            payload += '\0';
            payload += row;
        }
        std::string file("\x89PNG\r\n\x1a\n", 8u);
        file += chunk("IHDR", header);
        file += extra;
        file += chunk("IDAT", storedStream(payload));
        file += chunk("IEND", "");
        write(name, file);
    }

    /// The samples of a frame in its own arrangement, read through its own layout.
    template <typename T> static std::vector<T> values(const an::RawFrame& frame)
    {
        const auto layout = *frame.layout();
        std::vector<T> result;
        for (std::uint32_t plane = 0u; plane < layout.planeCount(); ++plane)
        {
            for (std::uint32_t y = 0u; y < layout.extent().height(); ++y)
            {
                for (std::uint32_t x = 0u; x < layout.extent().width() * layout.samplesPerPixel(); ++x)
                {
                    T value;
                    std::memcpy(&value,
                                frame.bytes().data() + plane * layout.planeStrideBytes() + y * layout.rowStrideBytes() +
                                    x * sizeof(T),
                                sizeof(T));
                    result.push_back(value);
                }
            }
        }
        return result;
    }

    template <typename T>
    an::RawFrame frame(const std::vector<T>& data, std::uint32_t width, std::uint32_t height,
                       std::uint32_t channels = 1u,
                       an::StorageArrangement arrangement = an::StorageArrangement::Interleaved, bool padded = false,
                       an::CoordinateOrigin origin = an::CoordinateOrigin::TopLeft, an::CfaDescriptor cfa = {},
                       an::NumericDomain domain = an::NumericDomain::EncodedNonlinear)
    {
        const auto extent = *an::ImageExtent::create(width, height, channels, engine.limits());
        const auto schema = channels == 1u   ? an::ChannelSchema::Mono
                            : channels == 2u ? an::ChannelSchema::Named
                            : channels == 3u ? an::ChannelSchema::Rgb
                                             : an::ChannelSchema::Rgba;
        const auto space = channels >= 3u && domain == an::NumericDomain::NormalizedLinear
                               ? an::ColorSpace::LinearRgb
                               : an::ColorSpace::Unspecified;
        const auto descriptor = *an::RawFrameDescriptor::create({extent,
                                                                 {sizeof(T), an::RawSampleClass::UnsignedInteger},
                                                                 an::ByteOrder::Native,
                                                                 schema,
                                                                 space,
                                                                 domain,
                                                                 cfa,
                                                                 origin,
                                                                 1.0});
        const bool planar = arrangement == an::StorageArrangement::Planar && channels > 1u;
        const auto layout = *an::ImageLayout::create(
            {extent, sizeof(T), planar ? 1u : channels, planar ? channels : 1u, padded ? 64u : 1u}, engine.limits());
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
                    const T value = data[(plane * height + y) * samplesPerRow + x];
                    std::memcpy(bytes.data() + plane * layout.planeStrideBytes() + y * layout.rowStrideBytes() +
                                    x * sizeof(T),
                                &value, sizeof(T));
                }
            }
        }
        return std::move(*raw);
    }

    template <typename T> static std::vector<T> ramp(std::uint32_t width, std::uint32_t height, std::uint32_t channels)
    {
        std::vector<T> data(static_cast<std::size_t>(width) * height * channels);
        for (std::size_t index = 0u; index < data.size(); ++index)
        {
            data[index] = static_cast<T>(index * 7u + 3u);
        }
        return data;
    }

    an::Result<an::OutputReport> save(const an::RawFrame& source, const an::OutputIntent* intent = nullptr,
                                      const an::ImageMetadata& metadata = {},
                                      const an::ArtifactIdentity* identity = nullptr, const char* name = "frame.png")
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

    an::Result<an::CodecDecodeResult> decode(const char* name = "frame.png")
    {
        auto execution = an::ExecutionContext::create(engine, {});
        if (!execution.hasValue())
        {
            return execution.status();
        }
        auto result = an::decodeImage(engine, *execution, {.path = path(name)});
        lastError = std::string(result.status().message()) + " / " + std::string(result.status().rootCause().message());
        return result;
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

TEST_F(PngCodecTest, EveryChannelCountAndBothWidthsRoundTripExactly)
{
    for (const std::uint32_t channels : {1u, 2u, 3u, 4u})
    {
        const auto eight = ramp<std::uint8_t>(9u, 5u, channels);
        const std::string name = "eight-" + std::to_string(channels) + ".png";
        ASSERT_TRUE(save(frame(eight, 9u, 5u, channels), nullptr, {}, nullptr, name.c_str()).hasValue())
            << channels << ": " << lastError;
        auto decoded = decode(name.c_str());
        ASSERT_TRUE(decoded.hasValue()) << lastError;
        EXPECT_EQ(values<std::uint8_t>(decoded->frame), eight);
        const auto sixteen = ramp<std::uint16_t>(9u, 5u, channels);
        const std::string wide = "sixteen-" + std::to_string(channels) + ".png";
        ASSERT_TRUE(save(frame(sixteen, 9u, 5u, channels), nullptr, {}, nullptr, wide.c_str()).hasValue()) << lastError;
        auto wideDecoded = decode(wide.c_str());
        ASSERT_TRUE(wideDecoded.hasValue()) << lastError;
        EXPECT_EQ(values<std::uint16_t>(wideDecoded->frame), sixteen)
            << "sixteen-bit samples reach the caller in the host's order";
        EXPECT_EQ(wideDecoded->metadata.codec().sourceEncoding().byteOrder, an::ByteOrder::Big)
            << "and the container's own order is a source fact";
    }
}

TEST_F(PngCodecTest, APlanarFrameIsInterleavedIntoTheContainersOwnOrder)
{
    const auto interleaved = ramp<std::uint8_t>(8u, 4u, 3u);
    std::vector<std::uint8_t> planar(interleaved.size());
    for (std::size_t pixel = 0u; pixel < interleaved.size() / 3u; ++pixel)
    {
        for (std::size_t channel = 0u; channel < 3u; ++channel)
        {
            planar[channel * (interleaved.size() / 3u) + pixel] = interleaved[pixel * 3u + channel];
        }
    }
    ASSERT_TRUE(save(frame(planar, 8u, 4u, 3u, an::StorageArrangement::Planar)).hasValue()) << lastError;
    auto decoded = decode();
    ASSERT_TRUE(decoded.hasValue()) << lastError;
    EXPECT_EQ(values<std::uint8_t>(decoded->frame), interleaved);
}

TEST_F(PngCodecTest, ADeclaredColourSpaceDecidesWhatTheSamplesMean)
{
    const auto data = ramp<std::uint8_t>(4u, 4u, 3u);
    auto linear = an::ImageSemantics::create({.transferFunction = an::TransferFunction::Linear});
    ASSERT_TRUE(linear.hasValue());
    const auto linearMetadata = an::ImageMetadata::create({}, std::move(*linear), {}, {}, {}, std::nullopt);
    ASSERT_TRUE(save(frame(data, 4u, 4u, 3u, an::StorageArrangement::Interleaved, false, an::CoordinateOrigin::TopLeft,
                           {}, an::NumericDomain::NormalizedLinear),
                     nullptr, linearMetadata, nullptr, "linear.png")
                    .hasValue())
        << lastError;
    auto decodedLinear = decode("linear.png");
    ASSERT_TRUE(decodedLinear.hasValue()) << lastError;
    EXPECT_EQ(decodedLinear->frame.descriptor()->numericDomain(), an::NumericDomain::NormalizedLinear);
    EXPECT_EQ(decodedLinear->frame.descriptor()->colorSpace(), an::ColorSpace::LinearRgb);
    EXPECT_EQ(decodedLinear->metadata.semantics().transferFunction(), an::TransferFunction::Linear);

    auto standard = an::ImageSemantics::create({.transferFunction = an::TransferFunction::Srgb});
    ASSERT_TRUE(standard.hasValue());
    const auto standardMetadata = an::ImageMetadata::create({}, std::move(*standard), {}, {}, {}, std::nullopt);
    ASSERT_TRUE(save(frame(data, 4u, 4u, 3u), nullptr, standardMetadata, nullptr, "srgb.png").hasValue()) << lastError;
    auto decodedStandard = decode("srgb.png");
    ASSERT_TRUE(decodedStandard.hasValue()) << lastError;
    EXPECT_EQ(decodedStandard->frame.descriptor()->numericDomain(), an::NumericDomain::EncodedNonlinear);
    EXPECT_EQ(decodedStandard->frame.descriptor()->colorSpace(), an::ColorSpace::DisplayRgb);
    EXPECT_EQ(decodedStandard->metadata.semantics().transferFunction(), an::TransferFunction::Srgb);
}

TEST_F(PngCodecTest, AContainerThatDeclaresNoColourClaimsNone)
{
    writeContainer("plain.png", 2u, 2u, 8u, 0u, {std::string("\x01\x02", 2u), std::string("\x03\x04", 2u)});
    auto decoded = decode("plain.png");
    ASSERT_TRUE(decoded.hasValue()) << lastError;
    EXPECT_EQ(decoded->frame.descriptor()->numericDomain(), an::NumericDomain::EncodedNonlinear);
    EXPECT_EQ(decoded->frame.descriptor()->colorSpace(), an::ColorSpace::Unspecified);
    EXPECT_EQ(decoded->metadata.semantics().transferFunction(), an::TransferFunction::Unspecified);
    EXPECT_EQ(values<std::uint8_t>(decoded->frame), (std::vector<std::uint8_t>{1u, 2u, 3u, 4u}));
}

TEST_F(PngCodecTest, APaletteBecomesTheColoursItStandsFor)
{
    const std::string palette("\xff\x00\x00\x00\xff\x00\x00\x00\xff", 9u);
    writeContainer("palette.png", 3u, 1u, 8u, 3u, {std::string("\x00\x01\x02", 3u)}, chunk("PLTE", palette));
    auto decoded = decode("palette.png");
    ASSERT_TRUE(decoded.hasValue()) << lastError;
    EXPECT_EQ(decoded->frame.descriptor()->channelSchema(), an::ChannelSchema::Rgb);
    EXPECT_EQ(values<std::uint8_t>(decoded->frame),
              (std::vector<std::uint8_t>{255u, 0u, 0u, 0u, 255u, 0u, 0u, 0u, 255u}));
    const auto* const stated = decoded->metadata.codec().find(an::CodecNamespace::Png, "ColorType");
    ASSERT_NE(stated, nullptr);
    EXPECT_EQ(stated->value(), "Palette");
}

TEST_F(PngCodecTest, TransparencyKeysExpandAtEveryPermittedSampleWidth)
{
    for (const unsigned char depth : {1u, 2u, 4u, 8u, 16u})
        for (const unsigned char color : {0u, 2u})
        {
            if (color == 2u && depth < 8u)
                continue;
            SCOPED_TRACE(testing::Message() << "depth=" << unsigned(depth) << " color=" << unsigned(color));
            const unsigned channels = color == 0u ? 1u : 3u;
            std::string key;
            std::string row;
            for (unsigned c = 0u; c < channels; ++c)
            {
                key += '\0';
                key += '\x01';
            }
            if (depth < 8u)
                row += static_cast<char>(1u << (8u - depth));
            else
                for (unsigned pixel = 0u; pixel < 2u; ++pixel)
                    for (unsigned c = 0u; c < channels; ++c)
                    {
                        if (depth == 16u)
                            row += '\0';
                        row += static_cast<char>(pixel == 0u ? 1u : 0u);
                    }
            writeContainer("key.png", 2u, 1u, depth, color, {row}, chunk("tRNS", key));
            auto decoded = decode("key.png");
            ASSERT_TRUE(decoded.hasValue()) << decoded.status().message();
            const auto& frame = decoded->frame;
            ASSERT_EQ(frame.descriptor()->extent().channelCount(), channels + 1u);
            EXPECT_EQ(frame.descriptor()->channelSchema(),
                      color == 0u ? an::ChannelSchema::Named : an::ChannelSchema::Rgba);
            const auto bytes = frame.bytes();
            const auto sample = [&](unsigned index) {
                std::uint16_t value = 0u;
                std::memcpy(&value, bytes.data() + index * (depth == 16u ? 2u : 1u), depth == 16u ? 2u : 1u);
                return value;
            };
            EXPECT_EQ(sample(0u), depth < 8u ? 255u / ((1u << depth) - 1u) : 1u);
            EXPECT_EQ(sample(channels), 0u);
            EXPECT_EQ(sample(channels + 1u), 0u);
            EXPECT_EQ(sample(2u * channels + 1u), depth == 16u ? 65535u : 255u);
        }
}

TEST_F(PngCodecTest, ASubByteGreyBecomesTheSameFractionOfFullScale)
{
    // Two pixels per byte: the values 0 and 15 are the ends of a four-bit scale.
    writeContainer("four-bit.png", 2u, 1u, 4u, 0u, {std::string("\x0f", 1u)});
    auto decoded = decode("four-bit.png");
    ASSERT_TRUE(decoded.hasValue()) << lastError;
    EXPECT_EQ(values<std::uint8_t>(decoded->frame), (std::vector<std::uint8_t>{0u, 255u}));
    const auto* const depth = decoded->metadata.codec().find(an::CodecNamespace::Png, "BitDepth");
    ASSERT_NE(depth, nullptr);
    EXPECT_EQ(depth->value(), "4");
}

TEST_F(PngCodecTest, AGreyAndAlphaContainerIsCarriedAsNamedChannels)
{
    writeContainer("grey-alpha.png", 2u, 1u, 8u, 4u, {std::string("\x10\x20\x30\x40", 4u)});
    auto decoded = decode("grey-alpha.png");
    ASSERT_TRUE(decoded.hasValue()) << lastError;
    EXPECT_EQ(decoded->frame.descriptor()->extent().channelCount(), 2u);
    EXPECT_EQ(decoded->frame.descriptor()->channelSchema(), an::ChannelSchema::Named);
    EXPECT_EQ(values<std::uint8_t>(decoded->frame), (std::vector<std::uint8_t>{0x10u, 0x20u, 0x30u, 0x40u}));
}

TEST_F(PngCodecTest, AnInterlacedContainerIsReadThroughEveryPass)
{
    // Adam7 over a four-by-four grey image: each pass states the pixels it owns, in its own order.
    const std::vector<std::vector<std::uint8_t>> passes{
        {0u}, {2u}, {8u, 10u}, {1u, 3u}, {4u, 5u, 6u, 7u}, {9u, 11u, 13u, 15u}, {12u, 13u, 14u, 15u}};
    const std::vector<std::uint8_t> expected{0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u};
    std::vector<std::string> rows;
    const std::array<std::uint32_t, 7u> xOffset{0u, 4u, 0u, 2u, 0u, 1u, 0u};
    const std::array<std::uint32_t, 7u> yOffset{0u, 0u, 4u, 0u, 2u, 0u, 1u};
    const std::array<std::uint32_t, 7u> xStep{8u, 8u, 4u, 4u, 2u, 2u, 1u};
    const std::array<std::uint32_t, 7u> yStep{8u, 8u, 8u, 4u, 4u, 2u, 2u};
    for (std::size_t pass = 0u; pass < 7u; ++pass)
    {
        for (std::uint32_t y = yOffset[pass]; y < 4u; y += yStep[pass])
        {
            std::string row;
            for (std::uint32_t x = xOffset[pass]; x < 4u; x += xStep[pass])
            {
                row += static_cast<char>(expected[y * 4u + x]);
            }
            if (!row.empty())
            {
                rows.push_back(row);
            }
        }
    }
    static_cast<void>(passes);
    writeContainer("interlaced.png", 4u, 4u, 8u, 0u, rows, {}, 1u);
    auto decoded = decode("interlaced.png");
    ASSERT_TRUE(decoded.hasValue()) << lastError;
    EXPECT_EQ(values<std::uint8_t>(decoded->frame), expected);
}

TEST_F(PngCodecTest, TextEntriesAndAnIdentityTravelBothWays)
{
    auto identity = an::ArtifactIdentity::create("frame-0007", "run-0007");
    ASSERT_TRUE(identity.hasValue());
    const auto written = *an::OutputIntent::create(
        {.sampleType = an::SampleType::UInt8,
         .cards = {card("Title", "a stated title", "tEXt"), card("Comment", "\xc3\xa9t\xc3\xa9", "iTXt")}});
    ASSERT_TRUE(save(frame(ramp<std::uint8_t>(4u, 4u, 1u), 4u, 4u), &written, {}, &*identity).hasValue()) << lastError;
    auto decoded = decode();
    ASSERT_TRUE(decoded.hasValue()) << lastError;
    const auto& codec = decoded->metadata.codec();
    const auto* const title = codec.find(an::CodecNamespace::Png, "Title");
    ASSERT_NE(title, nullptr);
    EXPECT_EQ(title->value(), "a stated title");
    const auto* const comment = codec.find(an::CodecNamespace::Png, "Comment");
    ASSERT_NE(comment, nullptr);
    EXPECT_EQ(comment->value(), "\xc3\xa9t\xc3\xa9");
    const auto* const software = codec.find(an::CodecNamespace::Png, "Software");
    ASSERT_NE(software, nullptr);
    EXPECT_EQ(software->value(), an::kEngineVersionString);
    auto stamped = an::artifactIdentityOf(codec, an::CodecNamespace::Png);
    ASSERT_TRUE(stamped.hasValue()) << lastError;
    ASSERT_TRUE(stamped->has_value());
    EXPECT_EQ((*stamped)->artifactId(), "frame-0007");
    EXPECT_EQ((*stamped)->runId(), "run-0007");
}

TEST_F(PngCodecTest, ExifTravelsInTheContainersOwnChunk)
{
    const auto written = an::OutputIntent::create(
        {.sampleType = an::SampleType::UInt8,
         .cards = {*an::CodecCard::create(an::CodecNamespace::Png, "Exif.Image.Make", "AfterNight", "Ascii")}});
    ASSERT_TRUE(written.hasValue());
    ASSERT_TRUE(save(frame(ramp<std::uint8_t>(4u, 4u, 1u), 4u, 4u), &*written).hasValue()) << lastError;
    auto decoded = decode();
    ASSERT_TRUE(decoded.hasValue()) << lastError;
    const auto* const make = decoded->metadata.codec().find(an::CodecNamespace::Png, "Exif.Image.Make");
    ASSERT_NE(make, nullptr);
    EXPECT_EQ(make->value(), "AfterNight");
}

TEST_F(PngCodecTest, TheCompressionLevelDecidesTheFile)
{
    const auto data = ramp<std::uint8_t>(128u, 128u, 1u);
    const auto none =
        *an::OutputIntent::create({.sampleType = an::SampleType::UInt8, .compression = an::OutputCompression::None});
    const auto best = *an::OutputIntent::create(
        {.sampleType = an::SampleType::UInt8, .compression = an::OutputCompression::Deflate, .compressionLevel = 9u});
    ASSERT_TRUE(save(frame(data, 128u, 128u), &none, {}, nullptr, "stored.png").hasValue()) << lastError;
    ASSERT_TRUE(save(frame(data, 128u, 128u), &best, {}, nullptr, "packed.png").hasValue()) << lastError;
    EXPECT_GT(fileSize("stored.png"), fileSize("packed.png"));
    auto stored = decode("stored.png");
    ASSERT_TRUE(stored.hasValue()) << lastError;
    EXPECT_EQ(values<std::uint8_t>(stored->frame), data);
}

TEST_F(PngCodecTest, AnUnsupportedCompressionOrTheOtherByteOrderIsRefusedBeforeTheFileIsOpened)
{
    const auto lossy = *an::OutputIntent::create(
        {.sampleType = an::SampleType::UInt8, .compression = an::OutputCompression::Jpeg, .compressionLevel = 90u});
    const auto first = save(frame(ramp<std::uint8_t>(4u, 4u, 1u), 4u, 4u), &lossy, {}, nullptr, "lossy.png");
    ASSERT_FALSE(first.hasValue());
    EXPECT_EQ(an::codecFailureReason(first.status().cause()), an::CodecFailure::UnsupportedRepresentation);
    const auto little =
        *an::OutputIntent::create({.sampleType = an::SampleType::UInt16, .byteOrder = an::ByteOrder::Little});
    const auto second = save(frame(ramp<std::uint16_t>(4u, 4u, 1u), 4u, 4u), &little, {}, nullptr, "little.png");
    ASSERT_FALSE(second.hasValue());
    EXPECT_EQ(an::codecFailureReason(second.status().cause()), an::CodecFailure::UnsupportedRepresentation);
}

TEST_F(PngCodecTest, AMosaicedOrBottomUpFrameIsRefusedRatherThanFlattenedOrFlipped)
{
    const auto mosaic = *an::CfaDescriptor::create(an::CfaPattern::Rggb, 0u, 0u);
    const auto first = save(frame(ramp<std::uint8_t>(4u, 4u, 1u), 4u, 4u, 1u, an::StorageArrangement::Interleaved,
                                  false, an::CoordinateOrigin::TopLeft, mosaic),
                            nullptr, {}, nullptr, "mosaic.png");
    ASSERT_FALSE(first.hasValue());
    EXPECT_EQ(an::codecFailureReason(first.status().cause()), an::CodecFailure::UnsupportedRepresentation);
    const auto second = save(frame(ramp<std::uint8_t>(4u, 4u, 1u), 4u, 4u, 1u, an::StorageArrangement::Interleaved,
                                   false, an::CoordinateOrigin::BottomLeft),
                             nullptr, {}, nullptr, "bottom.png");
    ASSERT_FALSE(second.hasValue());
    EXPECT_EQ(an::codecFailureReason(second.status().cause()), an::CodecFailure::UnsupportedRepresentation);
}

TEST_F(PngCodecTest, ATruncatedPayloadAndAContainerThatIsNotOneAreBothRefused)
{
    ASSERT_TRUE(save(frame(ramp<std::uint8_t>(64u, 64u, 1u), 64u, 64u)).hasValue()) << lastError;
    const auto target = path();
    const std::filesystem::path file(
        std::u8string_view(reinterpret_cast<const char8_t*>(target.data()), target.size()));
    std::error_code error;
    std::filesystem::resize_file(file, std::filesystem::file_size(file) / 2u, error);
    ASSERT_FALSE(error);
    auto truncated = decode();
    ASSERT_FALSE(truncated.hasValue());
    EXPECT_EQ(an::codecFailureReason(truncated.status().cause()), an::CodecFailure::PayloadUnreadable) << lastError;

    write("broken.png", "not a container at all");
    auto broken = decode("broken.png");
    ASSERT_FALSE(broken.hasValue());
    EXPECT_EQ(an::codecFailureReason(broken.status().cause()), an::CodecFailure::MetadataUnreadable) << lastError;
}

TEST_F(PngCodecTest, ADecodeThatTheBudgetCannotHoldIsRefused)
{
    ASSERT_TRUE(save(frame(ramp<std::uint8_t>(64u, 64u, 1u), 64u, 64u)).hasValue()) << lastError;
    auto small = makeEngine(2048u);
    ASSERT_TRUE(small.hasValue());
    auto execution = an::ExecutionContext::create(*small, {});
    ASSERT_TRUE(execution.hasValue());
    const auto source = path();
    auto decoded = an::decodeImage(*small, *execution, {.path = source});
    ASSERT_FALSE(decoded.hasValue());
    EXPECT_EQ(decoded.status().code(), an::ErrorCode::MemoryBudgetExceeded);
}

TEST_F(PngCodecTest, CancellationEndsADecodeWithoutAFrame)
{
    ASSERT_TRUE(save(frame(ramp<std::uint8_t>(128u, 128u, 1u), 128u, 128u)).hasValue()) << lastError;
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

TEST_F(PngCodecTest, ALiteralUnicodePathIsWrittenAndReadBack)
{
    ASSERT_TRUE(save(frame(ramp<std::uint8_t>(4u, 4u, 1u), 4u, 4u), nullptr, {}, nullptr, "\xc3\xa9\xcf\x80-frame.png")
                    .hasValue())
        << lastError;
    EXPECT_TRUE(decode("\xc3\xa9\xcf\x80-frame.png").hasValue()) << lastError;
}

TEST_F(PngCodecTest, TheCapabilityRecordStatesWhatThisContainerHolds)
{
    const auto capabilities = engine.codecRegistry()->capabilities(an::CodecFormat::Png);
    ASSERT_TRUE(capabilities.has_value());
    EXPECT_EQ(capabilities->decodeSamples, an::codecSampleBit({1u, an::RawSampleClass::UnsignedInteger}) |
                                               an::codecSampleBit({2u, an::RawSampleClass::UnsignedInteger}));
    EXPECT_EQ(capabilities->maxChannels, 4u);
    EXPECT_FALSE(capabilities->regionDecode);
}

} // namespace

TEST_F(PngCodecTest, ConfiguredDimensionsAreLimitsRatherThanCorruptMetadata)
{
    writeContainer("wide.png", 8u, 1u, 8u, 0u, {std::string(8u, 'x')});
    const auto codec = an::createPngCodec();
    ASSERT_TRUE(codec.hasValue());
    const std::array codecs{*codec};
    const auto registry = an::CodecRegistry::create(codecs);
    ASSERT_TRUE(registry.hasValue());
    an::EngineContextOptions options;
    options.codecRegistry = *registry;
    options.limits.maxImageWidth = 4u;
    const auto engine = an::EngineContext::create(options);
    ASSERT_TRUE(engine.hasValue());
    const auto execution = an::ExecutionContext::create(*engine, {});
    ASSERT_TRUE(execution.hasValue());
    const auto result = an::decodeImage(*engine, *execution, {.path = path("wide.png")});
    EXPECT_EQ(result.code(), an::ErrorCode::LimitExceeded);
    EXPECT_FALSE(an::codecFailureReason(result.status().rootCause()).has_value());
    ASSERT_NE(result.status().rootCause().findContext(an::ContextKey::Requested), nullptr);
    EXPECT_EQ(result.status().rootCause().findContext(an::ContextKey::Requested)->count, 8u);
}
