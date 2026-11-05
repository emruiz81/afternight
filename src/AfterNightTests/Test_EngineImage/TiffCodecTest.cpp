// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/IO/TiffCodec.h>

#include <AfterNightEngine/Core/Version.h>
#include <AfterNightEngine/IO/ArtifactIdentity.h>
#include <AfterNightEngine/IO/CodecError.h>
#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecMetadataProjection.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/ImageCodec.h>
#include <AfterNightEngine/IO/OutputTransaction.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

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
#include <vector>

namespace
{

/// The tags a hand-built container states, in the order the format requires.
constexpr std::uint16_t kTagImageWidth = 256u;
constexpr std::uint16_t kTagImageLength = 257u;
constexpr std::uint16_t kTagBitsPerSample = 258u;
constexpr std::uint16_t kTagPhotometric = 262u;
constexpr std::uint16_t kTagStripOffsets = 273u;
constexpr std::uint16_t kTagSamplesPerPixel = 277u;
constexpr std::uint16_t kTagRowsPerStrip = 278u;
constexpr std::uint16_t kTagStripByteCounts = 279u;
constexpr std::uint16_t kTagTileWidth = 322u;
constexpr std::uint16_t kTagTileLength = 323u;
constexpr std::uint16_t kTagTileOffsets = 324u;
constexpr std::uint16_t kTagTileByteCounts = 325u;

/// The field types a hand-built container uses.
constexpr std::uint16_t kTypeShort = 3u;
constexpr std::uint16_t kTypeLong = 4u;

/// One entry of a hand-built directory.
struct FixtureEntry
{
    std::uint16_t tag = 0u;
    std::uint16_t type = kTypeShort;
    std::uint32_t count = 1u;
    std::uint32_t value = 0u;
    std::string blob{}; ///< Value bytes that do not fit an entry; the builder states where they are.
};

/// One image of a hand-built container.
struct FixtureImage
{
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::uint16_t bitsPerSample = 8u;
    std::uint16_t samplesPerPixel = 1u;
    std::uint16_t photometric = 1u;
    std::string payload; ///< The samples, in the order the entries below describe.
    std::vector<FixtureEntry> extra; ///< Anything else the image states.
    bool tiled = false; ///< Whether the payload is one tile rather than one strip.
    std::uint32_t tileWidth = 16u;
    std::uint32_t tileHeight = 16u;
};

/// Four bytes, least significant first, which is how these fixtures state a number.
[[nodiscard]] std::string leNumber(std::uint32_t value)
{
    std::string bytes;
    for (int shift = 0; shift < 32; shift += 8)
    {
        bytes += static_cast<char>((value >> shift) & 0xffu);
    }
    return bytes;
}

/// Two bytes, least significant first.
[[nodiscard]] std::string leShort(std::uint16_t value)
{
    std::string bytes;
    bytes += static_cast<char>(value & 0xffu);
    bytes += static_cast<char>(value >> 8u);
    return bytes;
}

/// A complete container, built by hand so that a representation this engine's encoder does not
/// produce can still be read.
///
/// Every value fits in an entry, so nothing is stored out of line and the payload can go first.
///
/// @param images the images to state, in order.
/// @return the container's bytes.
[[nodiscard]] std::string buildContainer(const std::vector<FixtureImage>& images)
{
    std::string file("II", 2u);
    file += leShort(42u);
    std::uint32_t offset = 8u;
    std::vector<std::uint32_t> payloads;
    for (const FixtureImage& image : images)
    {
        payloads.push_back(offset);
        offset += static_cast<std::uint32_t>(image.payload.size());
    }
    // Out-of-line values follow the payloads: an entry whose bytes do not fit the four an entry
    // holds states where they are instead, which is how a colour map reaches a reader.
    std::vector<std::vector<std::uint32_t>> blobOffsets;
    std::string blobs;
    for (const FixtureImage& image : images)
    {
        std::vector<std::uint32_t> perEntry;
        for (const FixtureEntry& entry : image.extra)
        {
            perEntry.push_back(entry.blob.empty() ? 0u : offset + static_cast<std::uint32_t>(blobs.size()));
            blobs += entry.blob;
        }
        blobOffsets.push_back(perEntry);
    }
    offset += static_cast<std::uint32_t>(blobs.size());
    std::vector<std::uint32_t> directories;
    for (const FixtureImage& image : images)
    {
        directories.push_back(offset);
        offset += static_cast<std::uint32_t>(2u + 12u * (image.extra.size() + 8u) + 4u);
    }
    file += leNumber(directories.front());
    for (const FixtureImage& image : images)
    {
        file += image.payload;
    }
    file += blobs;
    for (std::size_t index = 0u; index < images.size(); ++index)
    {
        const FixtureImage& image = images[index];
        std::vector<FixtureEntry> entries{
            {kTagImageWidth, kTypeLong, 1u, image.width},
            {kTagImageLength, kTypeLong, 1u, image.height},
            {kTagBitsPerSample, kTypeShort, 1u, image.bitsPerSample},
            {kTagPhotometric, kTypeShort, 1u, image.photometric},
            {kTagSamplesPerPixel, kTypeShort, 1u, image.samplesPerPixel},
        };
        if (image.tiled)
        {
            entries.push_back({kTagTileWidth, kTypeLong, 1u, image.tileWidth});
            entries.push_back({kTagTileLength, kTypeLong, 1u, image.tileHeight});
            entries.push_back({kTagTileOffsets, kTypeLong, 1u, payloads[index]});
            entries.push_back({kTagTileByteCounts, kTypeLong, 1u, static_cast<std::uint32_t>(image.payload.size())});
        }
        else
        {
            entries.push_back({kTagStripOffsets, kTypeLong, 1u, payloads[index]});
            entries.push_back({kTagRowsPerStrip, kTypeLong, 1u, image.height});
            entries.push_back({kTagStripByteCounts, kTypeLong, 1u, static_cast<std::uint32_t>(image.payload.size())});
            entries.push_back({0u, kTypeShort, 0u, 0u});
        }
        for (std::size_t entryIndex = 0u; entryIndex < image.extra.size(); ++entryIndex)
        {
            FixtureEntry entry = image.extra[entryIndex];
            if (!entry.blob.empty())
            {
                entry.value = blobOffsets[index][entryIndex];
            }
            entries.push_back(entry);
        }
        std::erase_if(entries, [](const FixtureEntry& entry) { return entry.tag == 0u; });
        std::sort(entries.begin(), entries.end(),
                  [](const FixtureEntry& left, const FixtureEntry& right) { return left.tag < right.tag; });
        file += leShort(static_cast<std::uint16_t>(entries.size()));
        for (const FixtureEntry& entry : entries)
        {
            file += leShort(entry.tag);
            file += leShort(entry.type);
            file += leNumber(entry.count);
            file += entry.type == kTypeShort
                        ? leShort(static_cast<std::uint16_t>(entry.value)) + std::string("\0\0", 2u)
                        : leNumber(entry.value);
        }
        file += leNumber(index + 1u < images.size() ? directories[index + 1u] : 0u);
    }
    return file;
}

class TiffCodecTest : public testing::Test
{
protected:
    void SetUp() override
    {
        directory = std::filesystem::temp_directory_path() /
                    ("an-tiff-suite-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
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
        auto adapter = an::createTiffCodec();
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

    std::string path(const char* name = "frame.tif") const
    {
        const auto utf8 =
            (directory / std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(name)))).u8string();
        return {reinterpret_cast<const char*>(utf8.data()), utf8.size()};
    }

    static an::CodecCard card(std::string key, std::string value, std::string comment)
    {
        return *an::CodecCard::create(an::CodecNamespace::Tiff, std::move(key), std::move(value), std::move(comment));
    }

    void write(const char* name, std::string_view bytes) const
    {
        const auto target = path(name);
        std::ofstream out(
            std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(target.data()), target.size())),
            std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    /// The logical bytes of a frame, row by row, without whatever padding its layout carries.
    ///
    /// Bytes rather than samples, because this suite writes every width the container spells and a
    /// comparison that reads one width would miss half of the next.
    static std::vector<std::uint8_t> bytesOf(const an::RawFrame& frame)
    {
        const auto layout = *frame.layout();
        const auto rowBytes =
            static_cast<std::size_t>(layout.extent().width()) * layout.samplesPerPixel() * layout.elementSizeBytes();
        std::vector<std::uint8_t> result;
        for (std::uint32_t plane = 0u; plane < layout.planeCount(); ++plane)
        {
            for (std::uint32_t y = 0u; y < layout.extent().height(); ++y)
            {
                for (std::size_t x = 0u; x < rowBytes; ++x)
                {
                    result.push_back(static_cast<std::uint8_t>(
                        frame.bytes()[plane * layout.planeStrideBytes() + y * layout.rowStrideBytes() + x]));
                }
            }
        }
        return result;
    }

    /// A frame of the stated representation, filled from a byte pattern.
    an::RawFrame frame(const std::vector<std::byte>& data, std::uint32_t width, std::uint32_t height,
                       an::RawSampleFormat format, std::uint32_t channels = 1u,
                       an::StorageArrangement arrangement = an::StorageArrangement::Interleaved,
                       an::CoordinateOrigin origin = an::CoordinateOrigin::TopLeft, an::CfaDescriptor cfa = {},
                       an::ByteOrder order = an::ByteOrder::Native)
    {
        const auto extent = *an::ImageExtent::create(width, height, channels, engine.limits());
        const auto schema = channels == 1u   ? an::ChannelSchema::Mono
                            : channels == 3u ? an::ChannelSchema::Rgb
                            : channels == 4u ? an::ChannelSchema::Rgba
                                             : an::ChannelSchema::Named;
        const auto descriptor =
            *an::RawFrameDescriptor::create({extent, format, order, schema, an::ColorSpace::Unspecified,
                                             an::NumericDomain::NormalizedLinear, cfa, origin, 1.0});
        const bool planar = arrangement == an::StorageArrangement::Planar && channels > 1u;
        const auto layout = *an::ImageLayout::create(
            {extent, format.sizeBytes, planar ? 1u : channels, planar ? channels : 1u, 1u}, engine.limits());
        auto buffer = an::SampleBuffer::createOwned(engine, layout, an::StorageInitialization::Zeroed);
        auto raw = an::RawFrame::create(descriptor, layout, std::move(*buffer));
        auto bytes = *raw->mutableBytes();
        std::memcpy(bytes.data(), data.data(), (std::min)(bytes.size(), data.size()));
        return std::move(*raw);
    }

    /// A byte pattern of the stated length.
    static std::vector<std::byte> pattern(std::size_t size)
    {
        std::vector<std::byte> data(size);
        for (std::size_t index = 0u; index < size; ++index)
        {
            data[index] = static_cast<std::byte>((index * 37u + 11u) & 0xffu);
        }
        return data;
    }

    an::Result<an::OutputReport> save(const an::RawFrame& source, const an::OutputIntent* intent = nullptr,
                                      const an::ImageMetadata& metadata = {},
                                      const an::ArtifactIdentity* identity = nullptr, const char* name = "frame.tif")
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

    an::Result<an::CodecDecodeResult> decode(const char* name = "frame.tif", std::uint32_t imageIndex = 0u)
    {
        auto execution = an::ExecutionContext::create(engine, {});
        if (!execution.hasValue())
        {
            return execution.status();
        }
        auto result = an::decodeImage(engine, *execution, {.path = path(name), .imageIndex = imageIndex});
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

    /// The strip height a written container states, read out of its first directory.
    ///
    /// @param name the written file.
    /// @return the stated rows per strip, or zero when the file does not state one.
    std::uint32_t rowsPerStrip(const char* name) const
    {
        const auto target = path(name);
        std::ifstream in(
            std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(target.data()), target.size())),
            std::ios::binary);
        in.seekg(0, std::ios::end);
        const auto size = static_cast<std::size_t>(in.tellg());
        in.seekg(0, std::ios::beg);
        std::vector<std::uint8_t> bytes(size);
        in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
        if (size < 8u || bytes[0] != 'I' || bytes[1] != 'I')
        {
            return 0u;
        }
        const auto shortAt = [&bytes](std::size_t at) {
            return static_cast<std::uint32_t>(bytes[at]) | (static_cast<std::uint32_t>(bytes[at + 1u]) << 8u);
        };
        const auto longAt = [&bytes](std::size_t at) {
            return static_cast<std::uint32_t>(bytes[at]) | (static_cast<std::uint32_t>(bytes[at + 1u]) << 8u) |
                   (static_cast<std::uint32_t>(bytes[at + 2u]) << 16u) |
                   (static_cast<std::uint32_t>(bytes[at + 3u]) << 24u);
        };
        const auto directoryAt = static_cast<std::size_t>(longAt(4u));
        if (directoryAt + 2u > size)
        {
            return 0u;
        }
        const auto count = shortAt(directoryAt);
        for (std::uint32_t index = 0u; index < count; ++index)
        {
            const auto entry = directoryAt + 2u + static_cast<std::size_t>(index) * 12u;
            if (entry + 12u > size)
            {
                return 0u;
            }
            if (shortAt(entry) == kTagRowsPerStrip)
            {
                return shortAt(entry + 2u) == kTypeShort ? shortAt(entry + 8u) : longAt(entry + 8u);
            }
        }
        return 0u;
    }

    std::filesystem::path directory;
    an::EngineContext engine;
    std::string lastError;
};

TEST_F(TiffCodecTest, XmpPacketsAreExcludedBeforeExifDecoding)
{
    const std::string xmp = "<x:xmpmeta xmlns:x='adobe:ns:meta/'><rdf:RDF "
                            "xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'/></x:xmpmeta>";
    write("xmp.tif",
          buildContainer({FixtureImage{.width = 2u,
                                       .height = 2u,
                                       .payload = std::string(4u, '\x2a'),
                                       .extra = {{700u, 1u, static_cast<std::uint32_t>(xmp.size()), 0u, xmp}}}}));
    auto decoded = decode("xmp.tif");
    ASSERT_TRUE(decoded.hasValue()) << decoded.status().message();
    EXPECT_EQ(decoded->metadata.codec().find(an::CodecNamespace::Tiff, "Exif.Image.XMLPacket"), nullptr);
}

TEST_F(TiffCodecTest, EveryRepresentationThisContainerSpellsRoundTripsExactly)
{
    const std::array<an::RawSampleFormat, 11u> formats{an::RawSampleFormat{1u, an::RawSampleClass::UnsignedInteger},
                                                       an::RawSampleFormat{2u, an::RawSampleClass::UnsignedInteger},
                                                       an::RawSampleFormat{4u, an::RawSampleClass::UnsignedInteger},
                                                       an::RawSampleFormat{8u, an::RawSampleClass::UnsignedInteger},
                                                       an::RawSampleFormat{1u, an::RawSampleClass::SignedInteger},
                                                       an::RawSampleFormat{2u, an::RawSampleClass::SignedInteger},
                                                       an::RawSampleFormat{4u, an::RawSampleClass::SignedInteger},
                                                       an::RawSampleFormat{8u, an::RawSampleClass::SignedInteger},
                                                       an::RawSampleFormat{2u, an::RawSampleClass::FloatingPoint},
                                                       an::RawSampleFormat{4u, an::RawSampleClass::FloatingPoint},
                                                       an::RawSampleFormat{8u, an::RawSampleClass::FloatingPoint}};
    for (const an::RawSampleFormat format : formats)
    {
        const auto data = pattern(static_cast<std::size_t>(8u) * 4u * format.sizeBytes);
        const std::string name = "type-" + std::to_string(format.sizeBytes) + "-" +
                                 std::to_string(static_cast<int>(format.sampleClass)) + ".tif";
        ASSERT_TRUE(save(frame(data, 8u, 4u, format), nullptr, {}, nullptr, name.c_str()).hasValue())
            << name << ": " << lastError;
        auto decoded = decode(name.c_str());
        ASSERT_TRUE(decoded.hasValue()) << name << ": " << lastError;
        EXPECT_EQ(decoded->frame.descriptor()->sampleFormat(), format);
        const auto samples = bytesOf(decoded->frame);
        ASSERT_EQ(samples.size(), data.size());
        for (std::size_t index = 0u; index < samples.size(); ++index)
        {
            ASSERT_EQ(samples[index], static_cast<std::uint8_t>(data[index])) << name << " at " << index;
        }
    }
}

TEST_F(TiffCodecTest, TheArrangementTheFrameHasIsTheArrangementTheContainerDeclares)
{
    const an::RawSampleFormat format{2u, an::RawSampleClass::UnsignedInteger};
    const auto data = pattern(static_cast<std::size_t>(6u) * 4u * 3u * format.sizeBytes);
    ASSERT_TRUE(save(frame(data, 6u, 4u, format, 3u, an::StorageArrangement::Interleaved), nullptr, {}, nullptr,
                     "contiguous.tif")
                    .hasValue())
        << lastError;
    ASSERT_TRUE(
        save(frame(data, 6u, 4u, format, 3u, an::StorageArrangement::Planar), nullptr, {}, nullptr, "separate.tif")
            .hasValue())
        << lastError;
    auto contiguous = decode("contiguous.tif");
    auto separate = decode("separate.tif");
    ASSERT_TRUE(contiguous.hasValue()) << lastError;
    ASSERT_TRUE(separate.hasValue()) << lastError;
    EXPECT_EQ(contiguous->frame.layout()->planeCount(), 1u);
    EXPECT_EQ(contiguous->frame.layout()->samplesPerPixel(), 3u);
    EXPECT_EQ(separate->frame.layout()->planeCount(), 3u);
    EXPECT_EQ(separate->frame.layout()->samplesPerPixel(), 1u);
    // Each file holds the bytes it was handed, in its own order: the two are not the same byte
    // sequence, which is the whole point of reporting the arrangement the container declares.
    std::vector<std::uint8_t> expected(data.size());
    for (std::size_t index = 0u; index < data.size(); ++index)
    {
        expected[index] = static_cast<std::uint8_t>(data[index]);
    }
    EXPECT_EQ(bytesOf(contiguous->frame), expected);
    EXPECT_EQ(bytesOf(separate->frame), expected);
}

TEST_F(TiffCodecTest, ABottomUpFrameIsDescribedRatherThanFlipped)
{
    const an::RawSampleFormat format{1u, an::RawSampleClass::UnsignedInteger};
    const auto data = pattern(16u);
    ASSERT_TRUE(
        save(frame(data, 4u, 4u, format, 1u, an::StorageArrangement::Interleaved, an::CoordinateOrigin::BottomLeft),
             nullptr, {}, nullptr, "bottom.tif")
            .hasValue())
        << lastError;
    auto decoded = decode("bottom.tif");
    ASSERT_TRUE(decoded.hasValue()) << lastError;
    EXPECT_EQ(decoded->frame.descriptor()->coordinateOrigin(), an::CoordinateOrigin::BottomLeft);
    const auto samples = bytesOf(decoded->frame);
    for (std::size_t index = 0u; index < samples.size(); ++index)
    {
        ASSERT_EQ(samples[index], static_cast<std::uint8_t>(data[index]));
    }
}

TEST_F(TiffCodecTest, AMosaicTravelsAsATypedFilterArray)
{
    const an::RawSampleFormat format{2u, an::RawSampleClass::UnsignedInteger};
    const auto mosaic = *an::CfaDescriptor::create(an::CfaPattern::Grbg, 0u, 0u);
    const auto data = pattern(static_cast<std::size_t>(8u) * 8u * format.sizeBytes);
    ASSERT_TRUE(save(frame(data, 8u, 8u, format, 1u, an::StorageArrangement::Interleaved, an::CoordinateOrigin::TopLeft,
                           mosaic),
                     nullptr, {}, nullptr, "mosaic.tif")
                    .hasValue())
        << lastError;
    auto decoded = decode("mosaic.tif");
    ASSERT_TRUE(decoded.hasValue()) << lastError;
    ASSERT_TRUE(decoded->frame.descriptor()->cfa().isMosaiced());
    EXPECT_EQ(decoded->frame.descriptor()->cfa().pattern(), an::CfaPattern::Grbg);
}

TEST_F(TiffCodecTest, EveryCompressionThisContainerStatesRoundTripsAndTheLossyOneIsForEightBits)
{
    const an::RawSampleFormat eight{1u, an::RawSampleClass::UnsignedInteger};
    const auto data = pattern(64u * 64u);
    const std::array<an::OutputCompression, 3u> lossless{an::OutputCompression::None, an::OutputCompression::Lzw,
                                                         an::OutputCompression::Deflate};
    std::uint64_t plain = 0u;
    for (const an::OutputCompression compression : lossless)
    {
        const auto written = *an::OutputIntent::create(
            {.sampleType = an::SampleType::UInt8,
             .compression = compression,
             .compressionLevel =
                 compression == an::OutputCompression::Deflate ? std::optional<std::uint32_t>{9u} : std::nullopt});
        const std::string name = std::string("compressed-") + an::outputCompressionName(compression) + ".tif";
        ASSERT_TRUE(save(frame(data, 64u, 64u, eight), &written, {}, nullptr, name.c_str()).hasValue())
            << name << ": " << lastError;
        auto decoded = decode(name.c_str());
        ASSERT_TRUE(decoded.hasValue()) << name << ": " << lastError;
        const auto samples = bytesOf(decoded->frame);
        for (std::size_t index = 0u; index < samples.size(); ++index)
        {
            ASSERT_EQ(samples[index], static_cast<std::uint8_t>(data[index])) << name;
        }
        if (compression == an::OutputCompression::None)
        {
            plain = fileSize(name.c_str());
        }
    }
    EXPECT_GT(plain, 0u);

    const auto lossy = *an::OutputIntent::create(
        {.sampleType = an::SampleType::UInt8, .compression = an::OutputCompression::Jpeg, .compressionLevel = 90u});
    ASSERT_TRUE(save(frame(data, 64u, 64u, eight), &lossy, {}, nullptr, "lossy.tif").hasValue()) << lastError;
    EXPECT_TRUE(decode("lossy.tif").hasValue()) << lastError;

    const an::RawSampleFormat sixteen{2u, an::RawSampleClass::UnsignedInteger};
    // Sixteen-bit samples with the lossy transform: the intent and the frame agree about the
    // representation, so the refusal is this adapter's own rather than the facade's.
    const auto wide = *an::OutputIntent::create(
        {.sampleType = an::SampleType::UInt16, .compression = an::OutputCompression::Jpeg, .compressionLevel = 90u});
    const auto refused = save(frame(pattern(64u * 64u * 2u), 64u, 64u, sixteen), &wide, {}, nullptr, "wide.tif");
    ASSERT_FALSE(refused.hasValue()) << "the lossy transform is defined for eight-bit samples";
    EXPECT_EQ(an::codecFailureReason(refused.status().cause()), an::CodecFailure::UnsupportedRepresentation)
        << lastError;
}

TEST_F(TiffCodecTest, EncodingNeverMutatesTheSourceRows)
{
    auto codec = an::createTiffCodec();
    ASSERT_TRUE(codec.hasValue());
    auto execution = an::ExecutionContext::create(engine, {});
    ASSERT_TRUE(execution.hasValue());

    // LibTIFF byte-swaps the caller's scanline in place when the file order differs. Exercise the
    // adapter directly because the registry normally requires this conversion to have been prepared
    // already; the codec contract still makes its source immutable for every call it receives.
    const an::ByteOrder foreign =
        an::nativeByteOrder() == an::ByteOrder::Little ? an::ByteOrder::Big : an::ByteOrder::Little;
    constexpr std::array wideTypes{an::SampleType::UInt16, an::SampleType::UInt32, an::SampleType::Float64};
    for (const an::SampleType type : wideTypes)
    {
        const an::RawSampleFormat format = an::rawSampleFormatOf(type);
        an::RawFrame source = frame(pattern(static_cast<std::size_t>(16u) * format.sizeBytes), 4u, 4u, format);
        const std::vector<std::uint8_t> before = bytesOf(source);
        auto intent = an::OutputIntent::create({.sampleType = type, .byteOrder = foreign});
        ASSERT_TRUE(intent.hasValue());
        const std::string name = "foreign-" + std::to_string(format.sizeBytes) + ".tif";
        const std::string target = path(name.c_str());
        const auto encoded =
            (*codec)->encode(engine, *execution,
                             {.pendingPath = target, .format = an::CodecFormat::Tiff, .intent = &*intent}, source, {});
        ASSERT_TRUE(encoded.hasValue()) << an::sampleTypeName(type) << ": " << encoded.status().message();
        EXPECT_EQ(bytesOf(source), before) << an::sampleTypeName(type);
    }

    // LibTIFF releases before 4.7.1 also horizontal-difference the source row in place. Both
    // affected lossless encodings must therefore use the same staging path even for native order.
    for (const an::OutputCompression compression : {an::OutputCompression::Lzw, an::OutputCompression::Deflate})
    {
        const an::RawSampleFormat format = an::rawSampleFormatOf(an::SampleType::UInt16);
        an::RawFrame source = frame(pattern(64u * 8u * format.sizeBytes), 64u, 8u, format);
        const std::vector<std::uint8_t> before = bytesOf(source);
        auto intent = an::OutputIntent::create({.sampleType = an::SampleType::UInt16, .compression = compression});
        ASSERT_TRUE(intent.hasValue());
        const std::string name = std::string("immutable-") + an::outputCompressionName(compression) + ".tif";
        const std::string target = path(name.c_str());
        const auto encoded =
            (*codec)->encode(engine, *execution,
                             {.pendingPath = target, .format = an::CodecFormat::Tiff, .intent = &*intent}, source, {});
        ASSERT_TRUE(encoded.hasValue()) << name << ": " << encoded.status().message();
        EXPECT_EQ(bytesOf(source), before) << name;
    }
}

TEST_F(TiffCodecTest, TheByteOrderTheIntentStatesIsTheOrderTheFileHolds)
{
    const an::RawSampleFormat format{2u, an::RawSampleClass::UnsignedInteger};
    const auto data = pattern(32u);
    const auto big = *an::OutputIntent::create({.sampleType = an::SampleType::UInt16, .byteOrder = an::ByteOrder::Big});
    // The intent states the order the samples are already prepared in, which the facade requires to
    // match the frame, and the container then holds them in that order.
    ASSERT_TRUE(save(frame(data, 4u, 4u, format, 1u, an::StorageArrangement::Interleaved, an::CoordinateOrigin::TopLeft,
                           {}, an::ByteOrder::Big),
                     &big, {}, nullptr, "big.tif")
                    .hasValue())
        << lastError;
    std::ifstream check(std::filesystem::path(std::u8string_view(
                            reinterpret_cast<const char8_t*>(path("big.tif").data()), path("big.tif").size())),
                        std::ios::binary);
    char order[2]{};
    check.read(order, 2);
    EXPECT_EQ(std::string(order, 2u), "MM");
    auto decoded = decode("big.tif");
    ASSERT_TRUE(decoded.hasValue()) << lastError;
    EXPECT_EQ(decoded->metadata.codec().sourceEncoding().byteOrder, an::ByteOrder::Big);
    const auto samples = bytesOf(decoded->frame);
    ASSERT_EQ(samples.size(), data.size());
    for (std::size_t index = 0u; index + 1u < samples.size(); index += 2u)
    {
        // The frame was prepared big-endian and the caller is handed the host's order, so each
        // sample's two bytes come back the other way round from the way they went in.
        ASSERT_EQ(samples[index], static_cast<std::uint8_t>(data[index + 1u]));
        ASSERT_EQ(samples[index + 1u], static_cast<std::uint8_t>(data[index]));
    }
}

TEST_F(TiffCodecTest, ExifTravelsBothWaysAndTheEngineThatWroteTheFileSaysSo)
{
    const an::RawSampleFormat format{1u, an::RawSampleClass::UnsignedInteger};
    const auto written = *an::OutputIntent::create({.sampleType = an::SampleType::UInt8,
                                                    .cards = {card("Exif.Image.Make", "AfterNight", "Ascii"),
                                                              card("Exif.Photo.ExposureTime", "1/500", "Rational")}});
    ASSERT_TRUE(save(frame(pattern(64u), 8u, 8u, format), &written).hasValue()) << lastError;
    auto decoded = decode();
    ASSERT_TRUE(decoded.hasValue()) << lastError;
    const auto& codec = decoded->metadata.codec();
    const auto* const make = codec.find(an::CodecNamespace::Tiff, "Exif.Image.Make");
    ASSERT_NE(make, nullptr) << lastError;
    EXPECT_EQ(make->value(), "AfterNight");
    const auto* const exposure = codec.find(an::CodecNamespace::Tiff, "Exif.Photo.ExposureTime");
    ASSERT_NE(exposure, nullptr);
    EXPECT_EQ(exposure->value(), "1/500");
    const auto* const software = codec.find(an::CodecNamespace::Tiff, "Exif.Image.Software");
    ASSERT_NE(software, nullptr);
    EXPECT_EQ(software->value(), an::kEngineVersionString);
}

TEST_F(TiffCodecTest, AnExifExposureOfZeroLeavesTheFrameReadable)
{
    // The Exif spelling of the same case: a descriptive fact the typed record refuses never costs the
    // caller the pixels.
    const an::RawSampleFormat format{1u, an::RawSampleClass::UnsignedInteger};
    const auto written = *an::OutputIntent::create(
        {.sampleType = an::SampleType::UInt8, .cards = {card("Exif.Photo.ExposureTime", "0", "Rational")}});
    ASSERT_TRUE(save(frame(pattern(64u), 8u, 8u, format), &written).hasValue()) << lastError;
    auto decoded = decode();
    ASSERT_TRUE(decoded.hasValue()) << lastError;
    EXPECT_FALSE(decoded->metadata.acquisition().exposureSeconds().has_value());
    EXPECT_NE(decoded->metadata.codec().find(an::CodecNamespace::Tiff, "Exif.Photo.ExposureTime"), nullptr);
}

TEST_F(TiffCodecTest, AnOrientationASourceStatedSurvivesAWriteThatMovedNoPixel)
{
    const an::RawSampleFormat format{1u, an::RawSampleClass::UnsignedInteger};
    auto codec = an::CodecMetadata::create({}, {card("Exif.Image.Orientation", "2", "Short")});
    ASSERT_TRUE(codec.hasValue());
    const auto metadata = an::ImageMetadata::create({}, {}, {}, {}, std::move(*codec), std::nullopt);
    const auto written = *an::OutputIntent::create({.sampleType = an::SampleType::UInt8});
    ASSERT_TRUE(save(frame(pattern(16u), 4u, 4u, format), &written, metadata).hasValue()) << lastError;
    auto decoded = decode();
    ASSERT_TRUE(decoded.hasValue()) << lastError;
    EXPECT_EQ(decoded->frame.descriptor()->coordinateOrigin(), an::CoordinateOrigin::TopLeft);
    const auto* const orientation = decoded->metadata.codec().find(an::CodecNamespace::Tiff, "Exif.Image.Orientation");
    ASSERT_NE(orientation, nullptr);
    EXPECT_EQ(orientation->value(), "2") << "the samples were not moved, so the fact the source stated still holds";
}

TEST_F(TiffCodecTest, AnArtifactIdentityAndAForeignCardAreBothRefusedByName)
{
    const an::RawSampleFormat format{1u, an::RawSampleClass::UnsignedInteger};
    auto identity = an::ArtifactIdentity::create("frame-0002", "");
    ASSERT_TRUE(identity.hasValue());
    const auto written = *an::OutputIntent::create({.sampleType = an::SampleType::UInt8});
    const auto stamped = save(frame(pattern(16u), 4u, 4u, format), &written, {}, &*identity, "stamped.tif");
    ASSERT_FALSE(stamped.hasValue());
    EXPECT_EQ(an::codecFailureReason(stamped.status().cause()), an::CodecFailure::UnsupportedRepresentation)
        << lastError;
    const auto foreign =
        *an::OutputIntent::create({.sampleType = an::SampleType::UInt8, .cards = {card("Title", "x", "text")}});
    const auto refused = save(frame(pattern(16u), 4u, 4u, format), &foreign, {}, nullptr, "foreign.tif");
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(an::codecFailureReason(refused.status().cause()), an::CodecFailure::UnsupportedRepresentation);
}

TEST_F(TiffCodecTest, APaletteOrAWhiteIsZeroGreyIsRefusedRatherThanReadAsGreyLevels)
{
    // A colour map, because a reader that finds none treats the image as plain grey and this case
    // is about the container that really does state a palette.
    std::string colorMap(3u * 256u * 2u, '\0');
    for (std::size_t entry = 0u; entry < 256u; ++entry)
    {
        colorMap[entry * 2u + 1u] = static_cast<char>(entry);
    }
    write(
        "palette.tif",
        buildContainer({FixtureImage{
            .width = 2u,
            .height = 2u,
            .photometric = 3u,
            .payload = std::string("\x00\x01\x02\x03", 4u),
            .extra = {FixtureEntry{.tag = 320u, .type = kTypeShort, .count = 768u, .value = 0u, .blob = colorMap}}}}));
    auto palette = decode("palette.tif");
    ASSERT_FALSE(palette.hasValue()) << "decoded as "
                                     << an::channelSchemaName(palette->frame.descriptor()->channelSchema()) << " with "
                                     << palette->frame.descriptor()->extent().channelCount() << " channel(s)";
    EXPECT_EQ(an::codecFailureReason(palette.status().cause()), an::CodecFailure::UnsupportedRepresentation)
        << lastError;

    write("inverted.tif",
          buildContainer({FixtureImage{
              .width = 2u, .height = 2u, .photometric = 0u, .payload = std::string("\x00\x01\x02\x03", 4u)}}));
    auto inverted = decode("inverted.tif");
    ASSERT_FALSE(inverted.hasValue());
    EXPECT_EQ(an::codecFailureReason(inverted.status().cause()), an::CodecFailure::UnsupportedRepresentation);
}

TEST_F(TiffCodecTest, AssociatedAlphaIsRefusedAndStraightAlphaKeepsItsRenderedMeaning)
{
    const std::string pixels("\x40\x20\x10\x80\x20\x10\x08\xff", 8u);
    for (const std::uint16_t association : {1u, 2u})
    {
        write("alpha.tif", buildContainer({FixtureImage{.width = 2u,
                                                        .height = 1u,
                                                        .samplesPerPixel = 4u,
                                                        .photometric = 2u,
                                                        .payload = pixels,
                                                        .extra = {{338u, kTypeShort, 1u, association}}}}));
        auto decoded = decode("alpha.tif");
        if (association == 1u)
        {
            EXPECT_EQ(an::codecFailureReason(decoded.status().cause()), an::CodecFailure::UnsupportedRepresentation);
            auto execution = an::ExecutionContext::create(engine, {});
            EXPECT_FALSE(
                an::decodeImage(engine, *execution, {.path = path("alpha.tif"), .readMetadata = false}).hasValue());
            continue;
        }
        ASSERT_TRUE(decoded.hasValue()) << decoded.status().message();
        ASSERT_EQ(decoded->frame.descriptor()->channelSchema(), an::ChannelSchema::Rgba);
        ASSERT_TRUE(save(decoded->frame).hasValue());
        auto repeated = decode();
        ASSERT_TRUE(repeated.hasValue());
        ASSERT_EQ(repeated->frame.bytes().size(), pixels.size());
        const auto bytes = repeated->frame.bytes();
        for (unsigned c = 0u; c < 3u; ++c)
            EXPECT_DOUBLE_EQ(std::to_integer<unsigned>(bytes[c]) * std::to_integer<unsigned>(bytes[3]) / 255.0,
                             static_cast<unsigned char>(pixels[c]) * 128.0 / 255.0);
    }
}

TEST_F(TiffCodecTest, AContainerOfSeveralImagesSelectsByIndex)
{
    write("pages.tif",
          buildContainer({FixtureImage{.width = 2u, .height = 1u, .payload = std::string("\x01\x02", 2u)},
                          FixtureImage{.width = 2u, .height = 1u, .payload = std::string("\x03\x04", 2u)}}));
    auto first = decode("pages.tif", 0u);
    ASSERT_TRUE(first.hasValue()) << lastError;
    EXPECT_EQ(bytesOf(first->frame), (std::vector<std::uint8_t>{1u, 2u}));
    auto second = decode("pages.tif", 1u);
    ASSERT_TRUE(second.hasValue()) << lastError;
    EXPECT_EQ(bytesOf(second->frame), (std::vector<std::uint8_t>{3u, 4u}));
    auto execution = an::ExecutionContext::create(engine, {});
    ASSERT_TRUE(execution.hasValue());
    const auto source = path("pages.tif");
    auto probed = an::probeImage(engine, *execution, {.path = source});
    ASSERT_TRUE(probed.hasValue());
    EXPECT_EQ(probed->imageCount, 2u);
}

TEST_F(TiffCodecTest, ATiledPayloadIsReadAndClippedToTheImage)
{
    std::string tile(16u * 16u, '\0');
    for (std::uint32_t y = 0u; y < 16u; ++y)
    {
        for (std::uint32_t x = 0u; x < 16u; ++x)
        {
            tile[y * 16u + x] = static_cast<char>(y * 16u + x);
        }
    }
    write("tiled.tif", buildContainer({FixtureImage{.width = 12u, .height = 10u, .payload = tile, .tiled = true}}));
    auto decoded = decode("tiled.tif");
    ASSERT_TRUE(decoded.hasValue()) << lastError;
    const auto samples = bytesOf(decoded->frame);
    ASSERT_EQ(samples.size(), 12u * 10u);
    for (std::uint32_t y = 0u; y < 10u; ++y)
    {
        for (std::uint32_t x = 0u; x < 12u; ++x)
        {
            ASSERT_EQ(samples[y * 12u + x], static_cast<std::uint8_t>(y * 16u + x)) << x << "," << y;
        }
    }
}

TEST_F(TiffCodecTest, ATruncatedPayloadAndAContainerThatIsNotOneAreBothRefused)
{
    const an::RawSampleFormat format{1u, an::RawSampleClass::UnsignedInteger};
    ASSERT_TRUE(save(frame(pattern(64u * 64u), 64u, 64u, format)).hasValue()) << lastError;
    const auto target = path();
    const std::filesystem::path file(
        std::u8string_view(reinterpret_cast<const char8_t*>(target.data()), target.size()));
    std::error_code error;
    std::filesystem::resize_file(file, 200u, error);
    ASSERT_FALSE(error);
    auto truncated = decode();
    EXPECT_FALSE(truncated.hasValue()) << "a container whose payload is gone is not readable";

    write("broken.tif", "not a container at all");
    auto broken = decode("broken.tif");
    ASSERT_FALSE(broken.hasValue());
    EXPECT_EQ(an::codecFailureReason(broken.status().cause()), an::CodecFailure::MetadataUnreadable) << lastError;
}

TEST_F(TiffCodecTest, ADecodeThatTheBudgetCannotHoldIsRefused)
{
    const an::RawSampleFormat format{1u, an::RawSampleClass::UnsignedInteger};
    ASSERT_TRUE(save(frame(pattern(64u * 64u), 64u, 64u, format)).hasValue()) << lastError;
    auto small = makeEngine(2048u);
    ASSERT_TRUE(small.hasValue());
    auto execution = an::ExecutionContext::create(*small, {});
    ASSERT_TRUE(execution.hasValue());
    const auto source = path();
    auto decoded = an::decodeImage(*small, *execution, {.path = source});
    ASSERT_FALSE(decoded.hasValue());
    EXPECT_EQ(decoded.status().code(), an::ErrorCode::MemoryBudgetExceeded);
}

TEST_F(TiffCodecTest, CancellationEndsADecodeWithoutAFrame)
{
    const an::RawSampleFormat format{1u, an::RawSampleClass::UnsignedInteger};
    ASSERT_TRUE(save(frame(pattern(128u * 128u), 128u, 128u, format)).hasValue()) << lastError;
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

TEST_F(TiffCodecTest, ALiteralUnicodePathIsWrittenAndReadBack)
{
    const an::RawSampleFormat format{1u, an::RawSampleClass::UnsignedInteger};
    ASSERT_TRUE(
        save(frame(pattern(16u), 4u, 4u, format), nullptr, {}, nullptr, "\xc3\xa9\xcf\x80-frame.tif").hasValue())
        << lastError;
    EXPECT_TRUE(decode("\xc3\xa9\xcf\x80-frame.tif").hasValue()) << lastError;
}

TEST_F(TiffCodecTest, AWrittenContainerCarriesStripsOfAStatedHeightRatherThanOnePerRow)
{
    const an::RawSampleFormat format{2u, an::RawSampleClass::UnsignedInteger};
    // A row of this frame is eight kilobytes exactly, which is the whole strip the imaging library
    // aims at by default - so a container that inherited that default would state one row per strip.
    // The stated quarter-megabyte target states thirty-two of them instead.
    constexpr std::uint32_t wide = 4096u;
    constexpr std::uint32_t tall = 64u;
    ASSERT_TRUE(save(frame(pattern(static_cast<std::size_t>(wide) * tall * 2u), wide, tall, format), nullptr, {},
                     nullptr, "strips.tif")
                    .hasValue())
        << lastError;
    EXPECT_EQ(rowsPerStrip("strips.tif"), 32u);
    ASSERT_TRUE(decode("strips.tif").hasValue()) << lastError;

    // An image with fewer rows than one strip would hold is one strip, never a strip taller than the
    // image it divides.
    ASSERT_TRUE(save(frame(pattern(64u * 8u * 2u), 64u, 8u, format), nullptr, {}, nullptr, "small.tif").hasValue())
        << lastError;
    EXPECT_EQ(rowsPerStrip("small.tif"), 8u);
}

TEST_F(TiffCodecTest, ACompressedStripWhoseLengthTheFileDoesNotStateIsReadToTheFilesEnd)
{
    // A reader that meets a compressed strip with no stated length takes it to run to the end of the
    // file, so this read is the one that asks the container how long it is.
    const an::RawSampleFormat format{2u, an::RawSampleClass::UnsignedInteger};
    const auto deflated = *an::OutputIntent::create(
        {.sampleType = an::SampleType::UInt16, .compression = an::OutputCompression::Deflate});
    const auto source = frame(pattern(64u * 48u * 2u), 64u, 48u, format);
    ASSERT_TRUE(save(source, &deflated, {}, nullptr, "stated.tif").hasValue()) << lastError;

    const auto target = path("stated.tif");
    std::ifstream in(
        std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(target.data()), target.size())),
        std::ios::binary);
    std::string bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    in.close();
    ASSERT_GE(bytes.size(), 8u);
    ASSERT_EQ(bytes.substr(0u, 2u), "II");
    const auto shortAt = [&bytes](std::size_t at) {
        return static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[at])) |
               (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[at + 1u])) << 8u);
    };
    const auto directory = static_cast<std::size_t>(shortAt(4u) | (shortAt(6u) << 16u));
    ASSERT_LE(directory + 2u, bytes.size());
    const auto count = shortAt(directory);
    const auto end = directory + 2u + static_cast<std::size_t>(count) * 12u + 4u;
    ASSERT_LE(end, bytes.size());
    std::size_t removed = 0u;
    for (std::uint32_t index = 0u; index < count; ++index)
    {
        const auto entry = directory + 2u + static_cast<std::size_t>(index) * 12u;
        if (shortAt(entry) == kTagStripByteCounts)
        {
            ASSERT_EQ(shortAt(entry + 4u) | (shortAt(entry + 6u) << 16u), 1u) << "one strip, stated in its entry";
            // The later entries and the next directory's offset move up over it.
            bytes.erase(entry, 12u);
            bytes.insert(end - 12u, 12u, '\0');
            removed = entry;
            break;
        }
    }
    ASSERT_NE(removed, 0u) << "the written container states its strip lengths";
    bytes[directory] = static_cast<char>((count - 1u) & 0xffu);
    bytes[directory + 1u] = static_cast<char>((count - 1u) >> 8u);
    write("lengthless.tif", bytes);

    auto decoded = decode("lengthless.tif");
    ASSERT_TRUE(decoded.hasValue()) << lastError;
    EXPECT_EQ(bytesOf(decoded->frame), bytesOf(source));
}

TEST_F(TiffCodecTest, TheCapabilityRecordStatesWhatThisContainerHolds)
{
    const auto capabilities = engine.codecRegistry()->capabilities(an::CodecFormat::Tiff);
    ASSERT_TRUE(capabilities.has_value());
    EXPECT_NE(capabilities->decodeSamples & an::codecSampleBit({8u, an::RawSampleClass::SignedInteger}), 0u);
    EXPECT_NE(capabilities->decodeSamples & an::codecSampleBit({2u, an::RawSampleClass::FloatingPoint}), 0u);
    EXPECT_EQ(capabilities->encodeSamples, capabilities->decodeSamples);
    EXPECT_FALSE(capabilities->regionDecode);
}

} // namespace
