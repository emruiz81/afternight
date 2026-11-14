// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/IO/XisfCodec.h>

#include <AfterNightEngine/Algorithms/Astrometry/WcsCards.h>
#include <AfterNightEngine/Core/Version.h>
#include <AfterNightEngine/IO/ArtifactIdentity.h>
#include <AfterNightEngine/IO/CodecError.h>
#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecMetadataPolicy.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/ImageCodec.h>
#include <AfterNightEngine/IO/OutputTransaction.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>
#include <AfterNightEngine/Runtime/ProgressSink.h>
#include <AfterNightEngine/Runtime/TraceSink.h>

#include <openxisf/error.h>
#include <openxisf/reader.h>
#include <openxisf/writer.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#include "CodecAllocationFault.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
/// One image of a hand-built container: its attributes, its child elements and its payload.
struct FixtureImage
{
    std::string attributes;
    std::string children;
    std::string payload;
};

/// One way a unit can store an image's pixel data, written by OpenXISF's own writer: among them forms this adapter
/// does not write, which a read is held to all the same.
struct BlockForm
{
    const char* name = "";
    openxisf::pixel_storage storage = openxisf::pixel_storage::planar;
    std::optional<openxisf::codec> codec{};
    bool shuffle = false;
    std::uint64_t subblockBytes = 0u;
    std::optional<openxisf::checksum_algorithm> checksum{};
};

/// The forms a region is read from: uncompressed in either storage model, Zstandard with byte shuffling in subblocks
/// of the size given and compressed as one subblock, and uncompressed with a checksum.
std::vector<BlockForm> blockForms(std::uint64_t subblockBytes = std::uint64_t{1u} << 20u)
{
    return {
        {.name = "planar"},
        {.name = "normal", .storage = openxisf::pixel_storage::normal},
        {.name = "zstd-sh-subblocks", .codec = openxisf::codec::zstd, .shuffle = true, .subblockBytes = subblockBytes},
        {.name = "zstd-sh-whole", .codec = openxisf::codec::zstd, .shuffle = true},
        {.name = "sha256", .checksum = openxisf::checksum_algorithm::sha256}};
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

/// Keeps the value of every counter an operation's trace reports under one name.
class RecordedCounter final : public an::TraceSink
{
public:
    explicit RecordedCounter(std::string name) : m_name(std::move(name)) {}

    void onTrace(const an::TraceEvent& event) noexcept override
    {
        if (event.kind == an::TraceEventKind::Counter && m_name == event.name)
        {
            const std::lock_guard lock(m_mutex);
            values.push_back(event.value);
        }
    }

    std::vector<double> values; ///< Each value reported, in the order delivered.

private:
    std::string m_name;
    std::mutex m_mutex;
};

class XisfCodecTest : public testing::Test
{
protected:
    void SetUp() override
    {
        directory = std::filesystem::temp_directory_path() /
                    ("an-xisf-suite-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
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
        auto adapter = an::createXisfCodec();
        if (!adapter.hasValue())
            return adapter.status();
        std::array adapters{*adapter};
        auto registry = an::CodecRegistry::create(adapters);
        if (!registry.hasValue())
            return registry.status();
        an::EngineContextOptions options;
        options.codecRegistry = *registry;
        // Stated rather than the host's: a read is asked for up to eight workers, which an engine sized to a smaller
        // machine refuses.
        options.maxConcurrency = 8u;
        if (budget != 0u)
            options.limits.memoryBudgetBytes = budget;
        return an::EngineContext::create(options);
    }
    std::string path(const char* name = "frame.xisf") const
    {
        const auto utf8 =
            (directory / std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(name)))).u8string();
        return {reinterpret_cast<const char*>(utf8.data()), utf8.size()};
    }
    static an::CodecCard card(an::CodecNamespace origin, std::string key, std::string value, std::string comment = {})
    {
        return *an::CodecCard::create(origin, std::move(key), std::move(value), std::move(comment));
    }
    static std::vector<std::byte> readBytes(const std::string& name)
    {
        std::ifstream input(std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(name.c_str()))),
                            std::ios::binary);
        const std::vector<char> chars((std::istreambuf_iterator<char>(input)), {});
        std::vector<std::byte> bytes(chars.size());
        if (!chars.empty())
            std::memcpy(bytes.data(), chars.data(), chars.size());
        return bytes;
    }
    /// The samples of a frame in its own arrangement, read through its own layout.
    template <typename T> static std::vector<T> values(const an::RawFrame& frame)
    {
        const auto layout = *frame.layout();
        std::vector<T> result;
        for (std::uint32_t p = 0u; p < layout.planeCount(); ++p)
            for (std::uint32_t y = 0u; y < layout.extent().height(); ++y)
                for (std::uint32_t x = 0u; x < layout.extent().width() * layout.samplesPerPixel(); ++x)
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
                       an::StorageArrangement arrangement = an::StorageArrangement::Planar, bool padded = false,
                       an::CoordinateOrigin origin = an::CoordinateOrigin::TopLeft,
                       an::ByteOrder order = an::ByteOrder::Native)
    {
        const auto extent = *an::ImageExtent::create(width, height, channels, engine.limits());
        const an::RawSampleFormat format{sizeof(T), std::is_floating_point_v<T> ? an::RawSampleClass::FloatingPoint
                                                    : std::is_signed_v<T>       ? an::RawSampleClass::SignedInteger
                                                                                : an::RawSampleClass::UnsignedInteger};
        const auto descriptor = *an::RawFrameDescriptor::create(
            {extent, format, order, channels == 1u ? an::ChannelSchema::Mono : an::ChannelSchema::Rgb,
             an::ColorSpace::Unspecified, an::NumericDomain::NormalizedLinear, cfa, origin, 1.0});
        const bool planar = arrangement == an::StorageArrangement::Planar;
        const auto layout = *an::ImageLayout::create(
            {extent, sizeof(T), planar ? 1u : channels, planar ? channels : 1u, padded ? 64u : 1u}, engine.limits());
        auto buffer = an::SampleBuffer::createOwned(engine, layout, an::StorageInitialization::Zeroed);
        auto raw = an::RawFrame::create(descriptor, layout, std::move(*buffer));
        auto bytes = *raw->mutableBytes();
        const auto samplesPerRow = static_cast<std::size_t>(width) * layout.samplesPerPixel();
        for (std::uint32_t p = 0u; p < layout.planeCount(); ++p)
            for (std::uint32_t y = 0u; y < height; ++y)
                for (std::size_t x = 0u; x < samplesPerRow; ++x)
                {
                    auto* target =
                        bytes.data() + p * layout.planeStrideBytes() + y * layout.rowStrideBytes() + x * sizeof(T);
                    std::memcpy(target, &data[(p * height + y) * samplesPerRow + x], sizeof(T));
                    if (order == an::ByteOrder::Big && std::endian::native == std::endian::little)
                        std::reverse(target, target + sizeof(T));
                }
        return std::move(*raw);
    }
    an::Result<an::OutputReport> save(const an::RawFrame& source, const an::OutputIntent* intent = nullptr,
                                      const an::ImageMetadata& metadata = {},
                                      const an::ArtifactIdentity* identity = nullptr, const char* name = "frame.xisf")
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
    an::Result<an::CodecDecodeResult> decode(const char* name = "frame.xisf")
    {
        auto execution = an::ExecutionContext::create(engine, {});
        if (!execution.hasValue())
            return execution.status();
        const auto source = path(name);
        auto result = an::decodeImage(engine, *execution, {source});
        lastError = std::string(result.status().message()) + " / " + std::string(result.status().rootCause().message());
        return result;
    }
    /// Writes a container this engine's encoder cannot produce, so that a refusal can be exercised.
    ///
    /// An attachment's position is an integer without leading zeros, as the format spells one, so the
    /// header's length depends on the positions it carries: they are recomputed from the length the
    /// last attempt had until the two agree, which takes a pass or two.
    void writeContainer(const std::vector<FixtureImage>& images, const char* name = "frame.xisf")
    {
        const auto headerAt = [&](std::uint64_t first) {
            std::string header;
            for (const auto& image : images)
            {
                header += "<Image " + image.attributes + " location=\"attachment:" + std::to_string(first) + ":" +
                          std::to_string(image.payload.size()) + "\">" + image.children + "</Image>";
                first += image.payload.size();
            }
            return "<xisf version=\"1.0\" xmlns=\"http://www.pixinsight.com/xisf\">" + header + "</xisf>";
        };
        std::string header = headerAt(0u);
        while (headerAt(16u + header.size()).size() != header.size())
            header = headerAt(16u + header.size());
        header = headerAt(16u + header.size());
        const auto length = static_cast<std::uint32_t>(header.size());
        std::string file = "XISF0100";
        file.resize(16u, '\0');
        std::memcpy(file.data() + 8u, &length, sizeof(length));
        file += header;
        for (const auto& image : images)
            file += image.payload;
        const auto target = path(name);
        std::ofstream out(
            std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(target.data()), target.size())),
            std::ios::binary | std::ios::trunc);
        out.write(file.data(), static_cast<std::streamsize>(file.size()));
    }
    template <typename T> std::string payloadOf(const std::vector<T>& data)
    {
        std::string bytes(data.size() * sizeof(T), '\0');
        std::memcpy(bytes.data(), data.data(), bytes.size());
        return bytes;
    }
    template <typename T> void roundTrip(const std::vector<T>& data, const char* sampleFormat)
    {
        auto source = frame(data, static_cast<std::uint32_t>(data.size()), 1u);
        ASSERT_TRUE(save(source).hasValue()) << lastError;
        auto decoded = decode();
        ASSERT_TRUE(decoded.hasValue()) << lastError;
        EXPECT_EQ(values<T>(decoded->frame), data);
        EXPECT_EQ(decoded->frame.descriptor()->sampleFormat(), source.descriptor()->sampleFormat());
        EXPECT_EQ(decoded->metadata.codec().sourceEncoding().sampleFormat, source.descriptor()->sampleFormat());
        EXPECT_FALSE(decoded->metadata.outputIntent().has_value());
        EXPECT_NE(readBytes(path()).size(), 0u);
        EXPECT_NE(std::string(reinterpret_cast<const char*>(readBytes(path()).data()), readBytes(path()).size())
                      .find(std::string("sampleFormat=\"") + sampleFormat + "\""),
                  std::string::npos);
    }
    /// Samples that compress as pixel data do - smooth along a row, different from one row and plane to the next -
    /// held a plane after another.
    template <typename T>
    static std::vector<T> gradient(std::uint32_t width, std::uint32_t height, std::uint32_t planes)
    {
        std::vector<T> data(static_cast<std::size_t>(width) * height * planes);
        for (std::size_t index = 0u; index < data.size(); ++index)
        {
            const std::size_t x = index % width;
            const std::size_t y = index / width % height;
            const std::size_t plane = index / (static_cast<std::size_t>(width) * height);
            if constexpr (std::is_floating_point_v<T>)
                data[index] = static_cast<T>(static_cast<double>(x % 509u) / 512.0 + static_cast<double>(y) / 65536.0 +
                                             static_cast<double>(plane));
            else
                data[index] = static_cast<T>((x * 7u + y * 3u + plane * 4099u) % 4096u);
        }
        return data;
    }
    /// The samples of a region of data held a plane after another, in the order a decode of a unit in the storage model
    /// stated holds them: plane after plane, or pixel after pixel.
    template <typename T>
    static std::vector<T> regionOf(const std::vector<T>& data, std::uint32_t width, std::uint32_t height,
                                   std::uint32_t planes, const an::CodecRegion& region, openxisf::pixel_storage storage)
    {
        const auto at = [&](std::uint32_t plane, std::uint32_t y, std::uint32_t x) {
            return data[(static_cast<std::size_t>(plane) * height + y) * width + x];
        };
        std::vector<T> samples;
        samples.reserve(static_cast<std::size_t>(region.width) * region.height * planes);
        if (storage == openxisf::pixel_storage::planar)
        {
            for (std::uint32_t plane = 0u; plane < planes; ++plane)
                for (std::uint32_t y = region.y; y < region.y + region.height; ++y)
                    for (std::uint32_t x = region.x; x < region.x + region.width; ++x)
                        samples.push_back(at(plane, y, x));
            return samples;
        }
        for (std::uint32_t y = region.y; y < region.y + region.height; ++y)
            for (std::uint32_t x = region.x; x < region.x + region.width; ++x)
                for (std::uint32_t plane = 0u; plane < planes; ++plane)
                    samples.push_back(at(plane, y, x));
        return samples;
    }
    /// Writes data held a plane after another as a unit of one image in the form stated, through OpenXISF's own
    /// writer, and holds the unit to that form: a codec that did not shrink the block would have stored it as it is.
    template <typename T>
    testing::AssertionResult writeLibraryUnit(const std::string& name, const std::vector<T>& data, std::uint32_t width,
                                              std::uint32_t height, std::uint32_t planes, const BlockForm& form)
    {
        try
        {
            openxisf::image_info image;
            image.geometry = {.dimensions = {width, height}, .channels = planes};
            image.sample_format = openxisf::sample_format_of<T>();
            image.color_space = planes == 3u ? openxisf::color_space::rgb : openxisf::color_space::gray;
            image.pixel_storage = form.storage;
            if constexpr (std::is_floating_point_v<T>)
                image.bounds = openxisf::bounds{};
            const auto stored = regionOf(data, width, height, planes, {0u, 0u, width, height}, form.storage);
            openxisf::write_options options;
            options.creator_application = "XisfCodecTest 1.0";
            options.codec = form.codec;
            options.byte_shuffle = form.shuffle;
            options.subblock_size = form.subblockBytes;
            options.checksum = form.checksum;
            openxisf::writer writer(std::move(options));
            (void)writer.add_image(std::move(image), std::as_bytes(std::span<const T>(stored)));
            writer.save(path(name.c_str()));
            const auto block = openxisf::reader(path(name.c_str())).block(0u);
            if (block.codec != form.codec || block.checksum != form.checksum || block.byte_shuffle != form.shuffle)
                return testing::AssertionFailure() << name << " was not stored in the form asked for";
            if (form.codec && (form.subblockBytes == 0u) != (block.subblock_count == 1u))
                return testing::AssertionFailure() << name << " holds " << block.subblock_count << " subblocks";
            return testing::AssertionSuccess();
        }
        catch (const openxisf::error& error)
        {
            return testing::AssertionFailure() << name << ": " << error.what();
        }
    }
    std::filesystem::path directory;
    an::EngineContext engine;
    std::string lastError;
};

TEST_F(XisfCodecTest, EverySupportedRepresentationKeepsItsExtremaAndAdjacentCodes)
{
    roundTrip<std::uint8_t>({0, 1, 127, 128, 255}, "UInt8");
    roundTrip<std::uint16_t>({0, 1, 32767, 32768, 65535}, "UInt16");
    roundTrip<std::uint32_t>({0u, 16777217u, 2147483648u, 4294967295u}, "UInt32");
    roundTrip<float>({-123.5f, 0.0f, 0.125f, 1.5f}, "Float32");
    roundTrip<double>({-1e100, 0.0, 1.0000000000000002, 1e100}, "Float64");
}

TEST_F(XisfCodecTest, UnsignedSixtyFourBitSamplesAreCarriedAsExactNonresidentCodes)
{
    const std::vector<std::uint64_t> input{0u, 1u, std::uint64_t{1} << 53u, (std::uint64_t{1} << 53u) + 1u,
                                           (std::numeric_limits<std::uint64_t>::max)()};
    auto source = frame(input, 5u, 1u);
    ASSERT_TRUE(save(source).hasValue()) << lastError;
    auto decoded = decode();
    ASSERT_TRUE(decoded.hasValue()) << lastError;
    EXPECT_FALSE(decoded->frame.descriptor()->residentSampleType().has_value());
    EXPECT_EQ(values<std::uint64_t>(decoded->frame), input);
}

TEST_F(XisfCodecTest, SignedAndComplexRepresentationsAreRefusedByNameRatherThanReinterpreted)
{
    auto signedFrame = frame<std::int16_t>({-32768, 0, 32767}, 3u, 1u);
    const auto refused = save(signedFrame);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().rootCause().code(), an::ErrorCode::Unsupported);
    EXPECT_FALSE(std::filesystem::exists(
        std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(path().data()), path().size()))));
    for (const auto* const format : {"Complex32", "Complex64"})
    {
        writeContainer({{std::string("geometry=\"2:1:1\" sampleFormat=\"") + format +
                             "\" colorSpace=\"Gray\" pixelStorage=\"Planar\"",
                         "", std::string(std::string_view(format) == "Complex32" ? 16u : 32u, '\0')}},
                       "complex.xisf");
        auto result = decode("complex.xisf");
        ASSERT_FALSE(result.hasValue()) << format;
        EXPECT_EQ(result.status().rootCause().code(), an::ErrorCode::Unsupported) << format;
        EXPECT_EQ(an::codecFailureReason(result.status().rootCause()), an::CodecFailure::UnsupportedRepresentation);
    }
}

TEST_F(XisfCodecTest, PlanarAndInterleavedStorageIsReportedAsTheFileDeclaresIt)
{
    std::vector<std::uint16_t> input(24u);
    for (std::size_t i = 0u; i < input.size(); ++i)
        input[i] = static_cast<std::uint16_t>(i * 771u);
    for (const auto arrangement : {an::StorageArrangement::Planar, an::StorageArrangement::Interleaved})
    {
        const bool planar = arrangement == an::StorageArrangement::Planar;
        const char* const name = planar ? "planar.xisf" : "normal.xisf";
        auto source = frame(input, 4u, 2u, 3u, {}, arrangement);
        ASSERT_TRUE(save(source, nullptr, {}, nullptr, name).hasValue()) << lastError;
        const auto bytes = readBytes(path(name));
        const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        // Planar is the format's default storage model, which the writer leaves unstated; the normal model is
        // stated.
        EXPECT_EQ(text.find("pixelStorage=\"Normal\"") != std::string::npos, !planar);
        auto decoded = decode(name);
        ASSERT_TRUE(decoded.hasValue()) << lastError;
        EXPECT_EQ(an::storageArrangementOf(*decoded->frame.layout()), arrangement);
        EXPECT_EQ(decoded->frame.layout()->planeCount(), planar ? 3u : 1u);
        EXPECT_EQ(decoded->frame.layout()->samplesPerPixel(), planar ? 1u : 3u);
        // Neither arrangement is turned into the other: the samples come back in the order the
        // file holds them, which is what makes the two files different rather than equivalent.
        EXPECT_EQ(values<std::uint16_t>(decoded->frame), input);
    }
    EXPECT_NE(readBytes(path("planar.xisf")), readBytes(path("normal.xisf")));
}

TEST_F(XisfCodecTest, PaddedRowsAndForeignByteOrderAreRepackedIntoTheContainerOrder)
{
    std::vector<std::uint16_t> input(45u);
    for (std::size_t i = 0u; i < input.size(); ++i)
        input[i] = static_cast<std::uint16_t>(i * 1409u);
    auto source = frame(input, 5u, 3u, 3u, {}, an::StorageArrangement::Planar, true, an::CoordinateOrigin::TopLeft,
                        an::ByteOrder::Big);
    ASSERT_TRUE(save(source).hasValue()) << lastError;
    auto decoded = decode();
    ASSERT_TRUE(decoded.hasValue()) << lastError;
    EXPECT_EQ(decoded->frame.descriptor()->byteOrder(), an::ByteOrder::Native);
    EXPECT_EQ(values<std::uint16_t>(decoded->frame), input);
    EXPECT_TRUE(decoded->frame.layout()->isContiguous());
}

TEST_F(XisfCodecTest, BottomUpFramesAreRefusedRatherThanWrittenUpsideDown)
{
    auto source = frame<std::uint16_t>({1, 2, 3, 4}, 2u, 2u, 1u, {}, an::StorageArrangement::Planar, false,
                                       an::CoordinateOrigin::BottomLeft);
    const auto refused = save(source);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().rootCause().code(), an::ErrorCode::Unsupported);
    EXPECT_EQ(std::distance(std::filesystem::directory_iterator(directory), std::filesystem::directory_iterator{}), 0);
}

TEST_F(XisfCodecTest, DeflateRoundTripsLosslesslyAndTheTileCompressionsAreRefused)
{
    std::vector<std::uint16_t> input(4096u);
    for (std::size_t i = 0u; i < input.size(); ++i)
        input[i] = static_cast<std::uint16_t>(i % 97u);
    auto source = frame(input, 64u, 64u);
    auto plain = an::OutputIntent::create({.sampleType = an::SampleType::UInt16});
    ASSERT_TRUE(save(source, &*plain, {}, nullptr, "plain.xisf").hasValue()) << lastError;
    auto intent =
        an::OutputIntent::create({.sampleType = an::SampleType::UInt16, .compression = an::OutputCompression::Deflate});
    ASSERT_TRUE(save(source, &*intent, {}, nullptr, "deflate.xisf").hasValue()) << lastError;
    auto decoded = decode("deflate.xisf");
    ASSERT_TRUE(decoded.hasValue()) << lastError;
    EXPECT_EQ(values<std::uint16_t>(decoded->frame), input);
    EXPECT_LT(readBytes(path("deflate.xisf")).size(), readBytes(path("plain.xisf")).size());
    for (const auto compression :
         {an::OutputCompression::Rice, an::OutputCompression::Gzip, an::OutputCompression::Plio,
          an::OutputCompression::Lzw, an::OutputCompression::Jpeg})
    {
        auto refusedIntent =
            an::OutputIntent::create({.sampleType = an::SampleType::UInt16, .compression = compression});
        const auto refused = save(source, &*refusedIntent, {}, nullptr, "refused.xisf");
        ASSERT_FALSE(refused.hasValue()) << static_cast<int>(compression);
        EXPECT_EQ(refused.status().rootCause().code(), an::ErrorCode::Unsupported);
    }
    EXPECT_FALSE(std::filesystem::exists(
        std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(path("refused.xisf").data()), 0u))));
}

TEST_F(XisfCodecTest, EveryCodecRoundTripsLosslesslyInEveryFormTheIntentStates)
{
    // Every codec, every resident representation, shuffled and not, in subblocks that split the block and in one, at
    // the codec's default level and at both ends of its range: what is written reads back bit for bit, through this
    // adapter whole and by region and through OpenXISF's own reader, and the unit holds its pixel data in the form
    // the intent stated - shuffled by default when a sample has more than one byte, in 4 MiB subblocks by default.
    const auto check = [&]<typename T>(an::SampleType sampleType, T) {
        constexpr std::uint32_t kWidth = 96u;
        constexpr std::uint32_t kHeight = 64u;
        constexpr std::uint32_t kPlanes = 3u;
        // Blocks of one value, as a frame's smooth regions are: every codec shrinks them, shuffled or not.
        std::vector<T> data(static_cast<std::size_t>(kWidth) * kHeight * kPlanes);
        for (std::size_t index = 0u; index < data.size(); ++index)
            data[index] = static_cast<T>(
                (index % kWidth / 8u + 12u * (index / kWidth % kHeight / 8u) + 50u * (index / (kWidth * kHeight))) %
                200u);
        const auto source = frame(data, kWidth, kHeight, kPlanes);
        const std::uint64_t blockBytes = std::uint64_t{kWidth} * kHeight * kPlanes * sizeof(T);
        struct Form
        {
            std::optional<bool> shuffle;
            std::optional<std::uint64_t> subblock;
            std::optional<std::uint32_t> level;
        };
        // Each codec with the ends of its range of levels, and the place of the lower end on the library's levels
        // from 1 to 100: zlib's level 0 stores its data as they are, so its first compressing level is tested, which
        // is one ninth of the way along.
        for (const auto& [compression, codec, lowest, highest, lowestScale] :
             std::vector<std::tuple<an::OutputCompression, openxisf::codec, std::uint32_t, std::uint32_t, int>>{
                 {an::OutputCompression::Deflate, openxisf::codec::zlib, 1u, 9u, 12},
                 {an::OutputCompression::Lz4, openxisf::codec::lz4, 0u, 0u, 0},
                 {an::OutputCompression::Lz4Hc, openxisf::codec::lz4hc, 1u, 12u, 1},
                 {an::OutputCompression::Zstd, openxisf::codec::zstd, 1u, 22u, 1}})
        {
            std::vector<Form> forms{{},
                                    {true, {}, {}},
                                    {false, {}, {}},
                                    {{}, blockBytes / 3u, {}},
                                    {false, blockBytes / 5u, {}},
                                    {{}, blockBytes, {}}};
            if (highest != 0u)
            {
                forms.push_back({{}, {}, lowest});
                forms.push_back({{}, blockBytes / 4u, highest});
            }
            for (const Form& form : forms)
            {
                SCOPED_TRACE(testing::Message()
                             << an::outputCompressionName(compression) << " " << sizeof(T) << "-byte samples, shuffle "
                             << (form.shuffle ? (*form.shuffle ? "on" : "off") : "default") << ", subblock "
                             << form.subblock.value_or(0u) << ", level " << form.level.value_or(0u));
                auto intent = an::OutputIntent::create({.sampleType = sampleType,
                                                        .compression = compression,
                                                        .compressionLevel = form.level,
                                                        .byteShuffle = form.shuffle,
                                                        .compressionBlockBytes = form.subblock});
                ASSERT_TRUE(intent.hasValue()) << intent.status().message();
                ASSERT_TRUE(save(source, &*intent, {}, nullptr, "coded.xisf").hasValue()) << lastError;
                auto decoded = decode("coded.xisf");
                ASSERT_TRUE(decoded.hasValue()) << lastError;
                ASSERT_EQ(decoded->frame.bytes().size(), source.bytes().size());
                EXPECT_EQ(std::memcmp(decoded->frame.bytes().data(), source.bytes().data(), source.bytes().size()), 0);
                {
                    auto execution = an::ExecutionContext::create(engine, {});
                    const an::CodecRegion box{17u, 9u, 41u, 30u};
                    const auto coded = path("coded.xisf");
                    auto region = an::decodeImageRegion(engine, *execution, {coded}, box);
                    ASSERT_TRUE(region.hasValue()) << region.status().rootCause().message();
                    EXPECT_EQ(values<T>(region->frame),
                              regionOf(data, kWidth, kHeight, kPlanes, box, openxisf::pixel_storage::planar));
                }
                const openxisf::reader oracle(path("coded.xisf"));
                const auto block = oracle.block(0u);
                // A block the codec could not shrink is stored as it is; every one of these shrinks.
                EXPECT_EQ(block.codec, codec);
                // One byte has nothing to shuffle, so a block of one-byte samples states no shuffling whatever was
                // asked.
                EXPECT_EQ(block.byte_shuffle, form.shuffle.value_or(true) && sizeof(T) > 1u);
                const std::uint64_t subblock = form.subblock.value_or(std::uint64_t{4u} << 20u);
                EXPECT_EQ(block.subblock_count, (blockBytes + subblock - 1u) / subblock);
                EXPECT_EQ(block.checksum, std::nullopt);
                EXPECT_EQ(oracle.read_pixels<T>(0u), regionOf(data, kWidth, kHeight, kPlanes, {0u, 0u, kWidth, kHeight},
                                                              openxisf::pixel_storage::planar));
                if (form.level)
                {
                    // The library's scale spreads the codec's own levels over 1 to 100; the highest is its end.
                    const auto* stated = oracle.metadata().find("XISF:CompressionLevel");
                    ASSERT_NE(stated, nullptr);
                    EXPECT_EQ(stated->value.get<std::int32_t>(), *form.level == lowest ? lowestScale : 100);
                }
            }
        }
    };
    check(an::SampleType::UInt8, std::uint8_t{});
    check(an::SampleType::UInt16, std::uint16_t{});
    check(an::SampleType::UInt32, std::uint32_t{});
    check(an::SampleType::Float32, float{});
    check(an::SampleType::Float64, double{});
}

// Without a stated size a compressed block is cut into pieces of 4 MiB, which a region read decodes one at a time;
// OpenXISF given none would leave it whole, a region read then decoding all of it. A stated size cuts it into pieces
// of that size, and either way it reads back bit for bit.
TEST_F(XisfCodecTest, ACompressedFrameIsWrittenInSubblocksOf4MiBUnlessTheIntentStatesASize)
{
    constexpr std::uint32_t kWidth = 1100u;
    constexpr std::uint32_t kHeight = 1024u;
    std::vector<float> data(static_cast<std::size_t>(kWidth) * kHeight);
    for (std::size_t index = 0u; index < data.size(); ++index)
        data[index] = static_cast<float>(index % kWidth / 16u + 100u * (index / kWidth / 16u));
    const auto source = frame(data, kWidth, kHeight, 1u);
    const std::uint64_t blockBytes = std::uint64_t{kWidth} * kHeight * sizeof(float);
    ASSERT_GT(blockBytes, std::uint64_t{4u} << 20u);
    for (const std::optional<std::uint64_t> stated :
         {std::optional<std::uint64_t>{}, std::optional<std::uint64_t>{std::uint64_t{1u} << 20u}})
    {
        const std::uint64_t subblock = stated.value_or(std::uint64_t{4u} << 20u);
        SCOPED_TRACE(testing::Message() << "subblocks of " << subblock << " bytes");
        auto intent = an::OutputIntent::create({.sampleType = an::SampleType::Float32,
                                                .compression = an::OutputCompression::Zstd,
                                                .compressionBlockBytes = stated});
        ASSERT_TRUE(intent.hasValue()) << intent.status().message();
        ASSERT_TRUE(save(source, &*intent, {}, nullptr, "large.xisf").hasValue()) << lastError;
        const openxisf::reader oracle(path("large.xisf"));
        EXPECT_EQ(oracle.block(0u).codec, openxisf::codec::zstd);
        EXPECT_EQ(oracle.block(0u).subblock_count, (blockBytes + subblock - 1u) / subblock);
        auto decoded = decode("large.xisf");
        ASSERT_TRUE(decoded.hasValue()) << lastError;
        EXPECT_EQ(values<float>(decoded->frame), data);
    }
}

// OpenXISF spreads each codec's levels evenly over its own levels from 1 to 100, as the XISF specification asks of a
// compression level, and rounds one of its own to the nearest of the codec's (writer.h). Every level of every codec is
// written at a place on that scale the rule gives back as the same level, so the unit compresses at the level the
// intent stated and says so; zlib's level 0 compresses nothing, so its unit is written uncompressed.
TEST_F(XisfCodecTest, EveryLevelOfEveryCodecIsWrittenWhereTheLibraryReadsItBackAsThatLevel)
{
    std::vector<std::uint16_t> data(static_cast<std::size_t>(32u) * 16u);
    for (std::size_t index = 0u; index < data.size(); ++index)
        data[index] = static_cast<std::uint16_t>(index / 8u);
    const auto source = frame(data, 32u, 16u, 1u);
    for (const auto& [compression, lowest, highest] :
         std::vector<std::tuple<an::OutputCompression, int, int>>{{an::OutputCompression::Deflate, 0, 9},
                                                                  {an::OutputCompression::Lz4Hc, 1, 12},
                                                                  {an::OutputCompression::Zstd, 1, 22}})
    {
        int previous = 0;
        for (int level = lowest; level <= highest; ++level)
        {
            SCOPED_TRACE(testing::Message() << an::outputCompressionName(compression) << " level " << level);
            auto intent = an::OutputIntent::create({.sampleType = an::SampleType::UInt16,
                                                    .compression = compression,
                                                    .compressionLevel = static_cast<std::uint32_t>(level)});
            ASSERT_TRUE(intent.hasValue()) << intent.status().message();
            ASSERT_TRUE(save(source, &*intent, {}, nullptr, "level.xisf").hasValue()) << lastError;
            const openxisf::reader oracle(path("level.xisf"));
            const auto* stated = oracle.metadata().find("XISF:CompressionLevel");
            if (compression == an::OutputCompression::Deflate && level == 0)
            {
                EXPECT_EQ(stated, nullptr);
                EXPECT_EQ(oracle.block(0u).codec, std::nullopt);
                previous = 1;
                continue;
            }
            ASSERT_NE(stated, nullptr);
            const int scale = stated->value.get<std::int32_t>();
            EXPECT_EQ(lowest + ((scale - 1) * (highest - lowest) + 49) / 99, level) << "written at " << scale;
            EXPECT_GT(scale, previous);
            previous = scale;
            if (level == lowest)
                EXPECT_EQ(scale, 1);
            if (level == highest)
                EXPECT_EQ(scale, 100);
        }
    }
}

TEST_F(XisfCodecTest, EveryChecksumIsWrittenVerifiedOnReadAndRefusesAChangedByte)
{
    // Every algorithm the format defines, beside no compression and beside one: the unit states it, this adapter and
    // OpenXISF's reader verify it, and one byte changed in the block makes both refuse the pixels.
    constexpr std::uint32_t kWidth = 128u;
    constexpr std::uint32_t kHeight = 32u;
    const auto data = gradient<std::uint16_t>(kWidth, kHeight, 1u);
    const auto source = frame(data, kWidth, kHeight);
    for (const auto& [checksum, algorithm] : std::vector<std::pair<an::OutputChecksum, openxisf::checksum_algorithm>>{
             {an::OutputChecksum::Sha1, openxisf::checksum_algorithm::sha1},
             {an::OutputChecksum::Sha256, openxisf::checksum_algorithm::sha256},
             {an::OutputChecksum::Sha512, openxisf::checksum_algorithm::sha512},
             {an::OutputChecksum::Sha3Digest256, openxisf::checksum_algorithm::sha3_256},
             {an::OutputChecksum::Sha3Digest512, openxisf::checksum_algorithm::sha3_512}})
        for (const auto compression : {an::OutputCompression::None, an::OutputCompression::Zstd})
        {
            SCOPED_TRACE(testing::Message()
                         << an::outputChecksumName(checksum) << " beside " << an::outputCompressionName(compression));
            auto intent = an::OutputIntent::create(
                {.sampleType = an::SampleType::UInt16, .compression = compression, .checksum = checksum});
            ASSERT_TRUE(intent.hasValue()) << intent.status().message();
            ASSERT_TRUE(save(source, &*intent, {}, nullptr, "summed.xisf").hasValue()) << lastError;
            auto decoded = decode("summed.xisf");
            ASSERT_TRUE(decoded.hasValue()) << lastError;
            EXPECT_EQ(values<std::uint16_t>(decoded->frame), data);
            const openxisf::reader oracle(path("summed.xisf"));
            EXPECT_EQ(oracle.block(0u).checksum, algorithm);
            EXPECT_EQ(oracle.read_pixels<std::uint16_t>(0u), data);
            {
                // The block is the file's last bytes: its last byte is changed.
                std::fstream file(directory / "summed.xisf", std::ios::in | std::ios::out | std::ios::binary);
                file.seekg(-1, std::ios::end);
                const char last = static_cast<char>(file.get());
                file.seekp(-1, std::ios::end);
                file.put(static_cast<char>(last ^ 0x40));
            }
            const auto refused = decode("summed.xisf");
            ASSERT_FALSE(refused.hasValue());
            EXPECT_EQ(refused.status().rootCause().code(), an::ErrorCode::InvalidData);
            EXPECT_THROW((void)openxisf::reader(path("summed.xisf")).read_pixels<std::uint16_t>(0u),
                         openxisf::integrity_error);
        }
}

TEST_F(XisfCodecTest, DeflatedFloatPayloadsSurviveEveryByteOfTheirRepresentation)
{
    std::vector<double> input(1024u);
    for (std::size_t i = 0u; i < input.size(); ++i)
        input[i] = static_cast<double>(i) * 0.1234567890123456;
    input[0] = -0.0;
    input[1] = std::numeric_limits<double>::infinity();
    input[2] = std::bit_cast<double>(std::uint64_t{0x7ff8000012345678u});
    auto source = frame(input, 32u, 32u);
    auto intent = an::OutputIntent::create(
        {.sampleType = an::SampleType::Float64, .compression = an::OutputCompression::Deflate});
    ASSERT_TRUE(save(source, &*intent).hasValue()) << lastError;
    auto decoded = decode();
    ASSERT_TRUE(decoded.hasValue()) << lastError;
    ASSERT_EQ(decoded->frame.bytes().size(), source.bytes().size());
    EXPECT_EQ(std::memcmp(decoded->frame.bytes().data(), source.bytes().data(), source.bytes().size()), 0);
}

TEST_F(XisfCodecTest, ScalingAndForeignByteOrderIntentsAreRefusedBeforeAnythingIsWritten)
{
    auto source = frame<std::uint16_t>({1, 2, 3}, 3u, 1u);
    auto scaled = an::OutputIntent::create({.sampleType = an::SampleType::UInt16,
                                            .scaling = an::OutputScaling::Explicit,
                                            .scale = 2.0,
                                            .zeroOffset = 1.0});
    ASSERT_TRUE(scaled.hasValue());
    EXPECT_EQ(save(source, &*scaled).status().rootCause().code(), an::ErrorCode::Unsupported);
    auto swapped = an::OutputIntent::create({.sampleType = an::SampleType::UInt16, .byteOrder = an::ByteOrder::Big});
    ASSERT_TRUE(swapped.hasValue());
    auto bigFrame = frame<std::uint16_t>({1, 2, 3}, 3u, 1u, 1u, {}, an::StorageArrangement::Planar, false,
                                         an::CoordinateOrigin::TopLeft, an::ByteOrder::Big);
    EXPECT_EQ(save(bigFrame, &*swapped).status().rootCause().code(), an::ErrorCode::Unsupported);
    EXPECT_EQ(std::distance(std::filesystem::directory_iterator(directory), std::filesystem::directory_iterator{}), 0);
}

TEST_F(XisfCodecTest, FitsKeywordsAndXisfPropertiesTravelInTheirOwnVocabularies)
{
    auto source = frame<float>({0.25f}, 1u, 1u);
    auto intent = an::OutputIntent::create(
        {.sampleType = an::SampleType::Float32,
         .cards = {card(an::CodecNamespace::Fits, "OBJECT", "'NGC 3372'", "target"),
                   card(an::CodecNamespace::Fits, "EXPTIME", "120.5", "seconds"),
                   card(an::CodecNamespace::Fits, "HISTORY", "calibrated"),
                   card(an::CodecNamespace::Fits, "HISTORY", "registered"),
                   card(an::CodecNamespace::Fits, "HISTORY", "", "continued"),
                   card(an::CodecNamespace::Xisf, "Observer:Name", "an observer"),
                   card(an::CodecNamespace::Xisf, "Observation:Object:Name", "Carina", "target"),
                   card(an::CodecNamespace::Tiff, "Exif.Image.Make", "ignored"),
                   card(an::CodecNamespace::Fits, "NAXIS1", "99")}});
    ASSERT_TRUE(save(source, &*intent).hasValue()) << lastError;
    auto decoded = decode();
    ASSERT_TRUE(decoded.hasValue()) << lastError;
    const auto& codec = decoded->metadata.codec();
    ASSERT_NE(codec.find(an::CodecNamespace::Fits, "OBJECT"), nullptr);
    EXPECT_EQ(codec.find(an::CodecNamespace::Fits, "OBJECT")->value(), "'NGC 3372'");
    EXPECT_EQ(codec.find(an::CodecNamespace::Fits, "OBJECT")->comment(), "target");
    ASSERT_NE(codec.find(an::CodecNamespace::Xisf, "Observer:Name"), nullptr);
    EXPECT_EQ(codec.find(an::CodecNamespace::Xisf, "Observer:Name")->value(), "an observer");
    // A property states its type before its own comment, and a card that stated none is written as a String.
    EXPECT_EQ(codec.find(an::CodecNamespace::Xisf, "Observer:Name")->comment(), "String");
    EXPECT_EQ(codec.find(an::CodecNamespace::Xisf, "Observation:Object:Name")->comment(), "String; target");
    // A history is a sequence of records, and one whose text is the continuation comment is a record of its own: a
    // FITS card never continues another.
    EXPECT_EQ(std::count_if(codec.cards().begin(), codec.cards().end(),
                            [](const an::CodecCard& entry) { return entry.keyword() == "HISTORY"; }),
              3);
    // A card of another format's vocabulary never reaches this container, and a FITS structural
    // keyword describes the file a frame arrived in rather than the one being written.
    EXPECT_EQ(codec.find(an::CodecNamespace::Tiff, "Exif.Image.Make"), nullptr);
    EXPECT_EQ(codec.find(an::CodecNamespace::Fits, "NAXIS1"), nullptr);
    EXPECT_EQ(decoded->metadata.acquisition().exposureSeconds(), 120.5);
    // A String XML cannot spell as character data - a control character, a line break - travels in a data block
    // and is read back as it was. A comment, an attribute that no block can hold instead, is refused rather than
    // written into a file that could not be read back.
    const std::string controlled("a\x01name\nwritten\ton two lines");
    auto control = an::OutputIntent::create({.sampleType = an::SampleType::Float32,
                                             .cards = {card(an::CodecNamespace::Xisf, "Observer:Name", controlled)}});
    ASSERT_TRUE(control.hasValue());
    ASSERT_TRUE(save(source, &*control).hasValue()) << lastError;
    auto controlledBack = decode();
    ASSERT_TRUE(controlledBack.hasValue()) << lastError;
    ASSERT_NE(controlledBack->metadata.codec().find(an::CodecNamespace::Xisf, "Observer:Name"), nullptr);
    EXPECT_EQ(controlledBack->metadata.codec().find(an::CodecNamespace::Xisf, "Observer:Name")->value(), controlled);
    const auto before = readBytes(path());
    auto commented = an::OutputIntent::create(
        {.sampleType = an::SampleType::Float32,
         .cards = {
             card(an::CodecNamespace::Xisf, "Observer:Name", "an observer", std::string("a\x01comment")),
         }});
    ASSERT_TRUE(commented.hasValue());
    EXPECT_EQ(save(source, &*commented).status().rootCause().code(), an::ErrorCode::InvalidArgument);
    auto nul =
        an::OutputIntent::create({.sampleType = an::SampleType::Float32,
                                  .cards = {card(an::CodecNamespace::Xisf, "Observer:Name", std::string("a\0b", 3u))}});
    ASSERT_TRUE(nul.hasValue());
    EXPECT_EQ(save(source, &*nul).status().rootCause().code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(readBytes(path()), before);
    EXPECT_EQ(std::distance(std::filesystem::directory_iterator(directory), std::filesystem::directory_iterator{}), 1);
}

TEST_F(XisfCodecTest, TheFilesOwnStructuralDeclarationsSurviveEveryRoundTrip)
{
    auto source = frame<float>({0.5f, 0.25f, 0.125f}, 3u, 1u);
    // Every fact the file states as an attribute or element of the image rather than as a property.
    const std::vector<std::pair<const char*, const char*>> declared{
        {"ImageType", "MasterFlat"},
        {"Bounds", "0:65535"},
        {"Offset", "0.125"},
        {"Orientation", "90;flip"},
        {"Id", "frame_7"},
        {"Uuid", "0f8fad5b-d9cb-469f-a165-70867728950e"},
        {"DisplayFunction", "0.25,0.3,0.35,0.5;0.01,0.02,0.03,0;0.9,0.95,1,1;0,0,0,0;1,1,1,1;stretch"},
        {"Resolution", "120;100;cm"}};
    std::vector<an::CodecCard> cards;
    for (const auto& [keyword, value] : declared)
        cards.push_back(card(an::CodecNamespace::Xisf, keyword, value));
    auto intent = an::OutputIntent::create({.sampleType = an::SampleType::Float32, .cards = cards});
    ASSERT_TRUE(save(source, &*intent).hasValue()) << lastError;
    auto decoded = decode();
    ASSERT_TRUE(decoded.hasValue()) << lastError;
    const auto& codec = decoded->metadata.codec();
    for (const auto& [keyword, value] : declared)
    {
        ASSERT_NE(codec.find(an::CodecNamespace::Xisf, keyword), nullptr) << keyword;
        EXPECT_EQ(codec.find(an::CodecNamespace::Xisf, keyword)->value(), value) << keyword;
    }
    // Samples outside the normalized range are carried rather than rescaled, and the description
    // says so instead of claiming a fraction of full scale the samples do not hold.
    EXPECT_EQ(decoded->frame.descriptor()->numericDomain(), an::NumericDomain::PhysicalScaled);
    ASSERT_NE(codec.find(an::CodecNamespace::Xisf, "ColorSpace"), nullptr);
    EXPECT_EQ(codec.find(an::CodecNamespace::Xisf, "ColorSpace")->value(), "Gray");
    // The raw encoding carries no cards, and the structural facts still survive it - but for the image's UUID,
    // which a derived file never inherits, as it inherits no artifact identity.
    ASSERT_TRUE(save(decoded->frame, nullptr, decoded->metadata, nullptr, "raw.xisf").hasValue()) << lastError;
    auto again = decode("raw.xisf");
    ASSERT_TRUE(again.hasValue()) << lastError;
    for (const auto& [keyword, value] : declared)
    {
        const auto* const kept = again->metadata.codec().find(an::CodecNamespace::Xisf, keyword);
        if (std::string_view(keyword) == "Uuid")
        {
            EXPECT_EQ(kept, nullptr);
            continue;
        }
        ASSERT_NE(kept, nullptr) << keyword;
        EXPECT_EQ(kept->value(), value) << keyword;
    }
    EXPECT_EQ(again->metadata.codec().find(an::CodecNamespace::Fits, "OBJECT"), nullptr);
    EXPECT_EQ(values<float>(again->frame), values<float>(decoded->frame));
    // The unit's own metadata names the writer, and is never carried from a source into another file.
    const auto* const creator = again->metadata.codec().find(an::CodecNamespace::Xisf, "XISF:CreatorApplication");
    ASSERT_NE(creator, nullptr);
    EXPECT_TRUE(creator->value().starts_with("AfterNightEngine ")) << creator->value();
}

TEST_F(XisfCodecTest, AWrittenUnitNamesItsWriterOnlyAsTheUnitsCreator)
{
    // The unit's creator is the specification's way to name the application that wrote it, name and version alike;
    // no property of the image restates it.
    const auto header = [&](const char* name) {
        const auto bytes = readBytes(path(name));
        std::uint32_t length = 0u;
        std::memcpy(&length, bytes.data() + 8u, sizeof(length));
        return std::string(reinterpret_cast<const char*>(bytes.data()) + 16u, length);
    };
    auto source = frame<std::uint16_t>({1, 2, 3}, 3u, 1u);
    auto intent = an::OutputIntent::create({.sampleType = an::SampleType::UInt16});
    ASSERT_TRUE(intent.hasValue());
    ASSERT_TRUE(save(source, &*intent).hasValue()) << lastError;
    EXPECT_EQ(header("frame.xisf").find("Instrument:Software"), std::string::npos);
    auto decoded = decode();
    ASSERT_TRUE(decoded.hasValue()) << lastError;
    const auto* const creator = decoded->metadata.codec().find(an::CodecNamespace::Xisf, "XISF:CreatorApplication");
    ASSERT_NE(creator, nullptr);
    EXPECT_EQ(creator->value(), std::string("AfterNightEngine ") + an::kEngineVersionString);
    // A unit an earlier version wrote carries the engine's version as Instrument:Software:Name, which is not a
    // property the specification defines: it is not carried into the unit written from it, nor written when an
    // intent states it.
    {
        openxisf::image_info image;
        image.geometry = {.dimensions = {3u, 1u}, .channels = 1u};
        image.sample_format = openxisf::sample_format::uint16;
        image.properties.set(
            {.id = "Instrument:Software:Name", .value = openxisf::property_value(std::string("1.5.0"))});
        const std::array<std::uint16_t, 3u> samples{1u, 2u, 3u};
        openxisf::write_options options;
        options.creator_application = "AfterNightEngine 1.5.0";
        openxisf::writer writer(std::move(options));
        (void)writer.add_image(std::move(image), std::as_bytes(std::span<const std::uint16_t>(samples)));
        writer.save(path("earlier.xisf"));
    }
    auto earlier = decode("earlier.xisf");
    ASSERT_TRUE(earlier.hasValue()) << lastError;
    ASSERT_NE(earlier->metadata.codec().find(an::CodecNamespace::Xisf, "Instrument:Software:Name"), nullptr);
    ASSERT_TRUE(save(earlier->frame, &*intent, earlier->metadata, nullptr, "again.xisf").hasValue()) << lastError;
    EXPECT_EQ(header("again.xisf").find("Instrument:Software"), std::string::npos);
    auto stating = an::OutputIntent::create(
        {.sampleType = an::SampleType::UInt16,
         .cards = {card(an::CodecNamespace::Xisf, "Instrument:Software:Name", "1.5.0", "String")}});
    ASSERT_TRUE(stating.hasValue());
    ASSERT_TRUE(save(source, &*stating, {}, nullptr, "stated.xisf").hasValue()) << lastError;
    EXPECT_EQ(header("stated.xisf").find("Instrument:Software"), std::string::npos);
}

TEST_F(XisfCodecTest, AUnitWrittenWithABlankPrologOpensAtItsRootElementAndReadsBackAsWritten)
{
    const auto header = [&](const char* name) {
        const auto bytes = readBytes(path(name));
        std::uint32_t length = 0u;
        std::memcpy(&length, bytes.data() + 8u, sizeof(length));
        return std::string(reinterpret_cast<const char*>(bytes.data()) + 16u, length);
    };
    const auto data = gradient<float>(64u, 48u, 1u);
    auto source = frame<float>(data, 64u, 48u);
    // One write under an execution of its own, which ends before the next operation of the case begins.
    const auto write = [&](const std::string& destination, const an::OutputIntent& intent,
                           an::OutputHeaderProlog prolog) -> an::Result<an::OutputReport> {
        auto execution = an::ExecutionContext::create(engine, {});
        if (!execution.hasValue())
            return execution.status();
        return an::writeImage(engine, *execution,
                              {.destinationPath = destination, .intent = &intent, .headerProlog = prolog}, source, {});
    };
    for (const an::OutputCompression compression : {an::OutputCompression::None, an::OutputCompression::Lz4})
    {
        SCOPED_TRACE(an::outputCompressionName(compression));
        auto intent = an::OutputIntent::create(
            {.sampleType = an::SampleType::Float32,
             .compression = compression,
             .cards = {card(an::CodecNamespace::Fits, "OBJECT", "'M42'"),
                       card(an::CodecNamespace::Xisf, "Observation:Note", "less < than > it seems", "String")}});
        ASSERT_TRUE(intent.hasValue());
        const std::string specifiedPath = path("specified.xisf");
        const std::string blankPath = path("blank.xisf");
        // As the specification writes a header: the XML declaration first, then a comment naming its writer.
        ASSERT_TRUE(write(specifiedPath, *intent, an::OutputHeaderProlog::Specified).hasValue());
        const std::string specified = header("specified.xisf");
        EXPECT_TRUE(specified.starts_with(R"(<?xml version="1.0" encoding="UTF-8"?>)"));
        EXPECT_NE(specified.find("<!--"), std::string::npos);
        // With a blank prolog the header holds spaces up to its root element and nothing has moved.
        const auto written = write(blankPath, *intent, an::OutputHeaderProlog::Blank);
        ASSERT_TRUE(written.hasValue()) << written.status().message() << " / "
                                        << written.status().rootCause().message();
        const std::string blank = header("blank.xisf");
        const auto root = blank.find('<');
        ASSERT_NE(root, std::string::npos);
        EXPECT_GT(root, 0u);
        EXPECT_EQ(blank.find_first_not_of(' '), root);
        EXPECT_TRUE(blank.substr(root).starts_with("<xisf "));
        EXPECT_EQ(blank.find("<?"), std::string::npos);
        EXPECT_EQ(blank.find("<!--"), std::string::npos);
        EXPECT_EQ(specified.find("<xisf "), root);
        EXPECT_EQ(readBytes(blankPath).size(), readBytes(specifiedPath).size());
        EXPECT_EQ(written->bytesWritten, readBytes(blankPath).size());
        // The unit reads as the one written as specified does: its samples, its keyword and its property, whose
        // own text keeps its angle brackets.
        auto decoded = decode("blank.xisf");
        ASSERT_TRUE(decoded.hasValue()) << lastError;
        EXPECT_EQ(values<float>(decoded->frame), data);
        const auto* const object = decoded->metadata.codec().find(an::CodecNamespace::Fits, "OBJECT");
        ASSERT_NE(object, nullptr);
        EXPECT_EQ(object->value(), "'M42'");
        const auto* const note = decoded->metadata.codec().find(an::CodecNamespace::Xisf, "Observation:Note");
        ASSERT_NE(note, nullptr);
        EXPECT_EQ(note->value(), "less < than > it seems");
    }
}

TEST_F(XisfCodecTest, ABlankPrologIsRefusedForAReservedImageAndLeavesNothing)
{
    const auto data = gradient<float>(64u, 48u, 1u);
    auto source = frame<float>(data, 64u, 48u);
    auto intent = an::OutputIntent::create({.sampleType = an::SampleType::Float32});
    ASSERT_TRUE(intent.hasValue());
    auto execution = an::ExecutionContext::create(engine, {});
    ASSERT_TRUE(execution.hasValue());
    const std::string destination = path("reserved.xisf");
    auto transaction = an::OutputTransaction::begin(
        engine, *execution,
        {.destinationPath = destination, .intent = &*intent, .headerProlog = an::OutputHeaderProlog::Blank});
    ASSERT_TRUE(transaction.hasValue()) << transaction.status().message();
    // A reserved image's header is written again when its rows are complete, as the specification writes one.
    const an::Status reserved = transaction->reserve(*source.descriptor(), an::StorageArrangement::Planar, {});
    EXPECT_EQ(reserved.code(), an::ErrorCode::Unsupported);
    EXPECT_FALSE(transaction->valid());
    EXPECT_TRUE(std::filesystem::is_empty(directory));
}

TEST_F(XisfCodecTest, CardsTheContainerCannotSpellAreRefusedBeforeAnythingIsWritten)
{
    auto source = frame<std::uint16_t>({1, 2, 3}, 3u, 1u);
    // A FITS keyword XISF's FITSKeyword element cannot hold - a name of the HIERARCH convention, text outside
    // printable ASCII - and a property name that is not an XISF identifier.
    for (const auto& refused : {card(an::CodecNamespace::Fits, "LONGNAMED9", "1"),
                                card(an::CodecNamespace::Fits, "OBSERVER", "'J\xc3\xb6rg'"),
                                card(an::CodecNamespace::Xisf, "Observer Name", "an observer")})
    {
        auto intent = an::OutputIntent::create({.sampleType = an::SampleType::UInt16, .cards = {refused}});
        ASSERT_TRUE(intent.hasValue());
        const auto result = save(source, &*intent);
        ASSERT_FALSE(result.hasValue()) << refused.keyword();
        EXPECT_EQ(result.status().rootCause().code(), an::ErrorCode::InvalidArgument) << refused.keyword();
    }
    EXPECT_EQ(std::distance(std::filesystem::directory_iterator(directory), std::filesystem::directory_iterator{}), 0);
    // A lower-case FITS keyword is written as FITS spells it, and a commentary record's text as its comment.
    auto intent = an::OutputIntent::create({.sampleType = an::SampleType::UInt16,
                                            .cards = {card(an::CodecNamespace::Fits, "object", "'M42'"),
                                                      card(an::CodecNamespace::Fits, "COMMENT", "made here")}});
    ASSERT_TRUE(save(source, &*intent).hasValue()) << lastError;
    auto decoded = decode();
    ASSERT_TRUE(decoded.hasValue()) << lastError;
    ASSERT_NE(decoded->metadata.codec().find(an::CodecNamespace::Fits, "OBJECT"), nullptr);
    EXPECT_EQ(decoded->metadata.codec().find(an::CodecNamespace::Fits, "OBJECT")->value(), "'M42'");
    ASSERT_NE(decoded->metadata.codec().find(an::CodecNamespace::Fits, "COMMENT"), nullptr);
    EXPECT_EQ(decoded->metadata.codec().find(an::CodecNamespace::Fits, "COMMENT")->comment(), "made here");
}

TEST_F(XisfCodecTest, AFilterArrayTravelsAsTheEffectivePatternAndAnUnnamedOneAsItsDeclaration)
{
    auto mosaic = frame<std::uint16_t>(std::vector<std::uint16_t>(16u, 7u), 4u, 4u, 1u,
                                       *an::CfaDescriptor::create(an::CfaPattern::Rggb, 1u, 0u));
    ASSERT_TRUE(save(mosaic).hasValue()) << lastError;
    auto decoded = decode();
    ASSERT_TRUE(decoded.hasValue()) << lastError;
    ASSERT_TRUE(decoded->frame.descriptor()->cfa().isMosaiced());
    EXPECT_EQ(decoded->frame.descriptor()->cfa().colorAt(0u, 0u), an::CfaColor::Green);
    EXPECT_EQ(decoded->frame.descriptor()->cfa().colorAt(1u, 0u), an::CfaColor::Red);
    ASSERT_NE(decoded->metadata.codec().find(an::CodecNamespace::Xisf, "ColorFilterArray"), nullptr);
    EXPECT_EQ(decoded->metadata.codec().find(an::CodecNamespace::Xisf, "ColorFilterArray")->value(), "2x2:GRBG");
    // A four-colour array is not a mosaic this engine names. It is preserved as the file's own
    // declaration, with the name the file gives it, and is not turned into a typed filter array a
    // demosaic would then be run over.
    writeContainer({{"geometry=\"4:4:1\" sampleFormat=\"UInt16\" colorSpace=\"Gray\" pixelStorage=\"Planar\"",
                     "<ColorFilterArray pattern=\"CYYM\" width=\"2\" height=\"2\" name=\"four colour\"/>",
                     std::string(32u, '\0')}},
                   "cmy.xisf");
    auto foreign = decode("cmy.xisf");
    ASSERT_TRUE(foreign.hasValue()) << lastError;
    EXPECT_FALSE(foreign->frame.descriptor()->cfa().isMosaiced());
    ASSERT_NE(foreign->metadata.codec().find(an::CodecNamespace::Xisf, "ColorFilterArray"), nullptr);
    EXPECT_EQ(foreign->metadata.codec().find(an::CodecNamespace::Xisf, "ColorFilterArray")->value(),
              "2x2:CYYM;four colour");
    ASSERT_TRUE(save(foreign->frame, nullptr, foreign->metadata, nullptr, "cmy-copy.xisf").hasValue()) << lastError;
    auto copied = decode("cmy-copy.xisf");
    ASSERT_TRUE(copied.hasValue()) << lastError;
    EXPECT_EQ(copied->metadata.codec().find(an::CodecNamespace::Xisf, "ColorFilterArray")->value(),
              "2x2:CYYM;four colour");
}

TEST_F(XisfCodecTest, ColourSpacesAndChannelSchemasAgreeInBothDirections)
{
    const std::vector<float> input(12u, 0.5f);
    auto rgb = frame(input, 2u, 2u, 3u);
    ASSERT_TRUE(save(rgb, nullptr, {}, nullptr, "rgb.xisf").hasValue()) << lastError;
    auto decodedRgb = decode("rgb.xisf");
    ASSERT_TRUE(decodedRgb.hasValue()) << lastError;
    EXPECT_EQ(decodedRgb->frame.descriptor()->channelSchema(), an::ChannelSchema::Rgb);
    EXPECT_EQ(decodedRgb->frame.descriptor()->colorSpace(), an::ColorSpace::Unspecified);
    writeContainer({{"geometry=\"2:2:3\" sampleFormat=\"Float32\" colorSpace=\"CIELab\" pixelStorage=\"Planar\"", "",
                     std::string(48u, '\0')}},
                   "lab.xisf");
    auto lab = decode("lab.xisf");
    ASSERT_TRUE(lab.hasValue()) << lastError;
    EXPECT_EQ(lab->frame.descriptor()->channelSchema(), an::ChannelSchema::Lab);
    EXPECT_EQ(lab->frame.descriptor()->colorSpace(), an::ColorSpace::CieLab);
    EXPECT_EQ(lab->frame.descriptor()->numericDomain(), an::NumericDomain::EncodedNonlinear);
    ASSERT_TRUE(save(lab->frame, nullptr, lab->metadata, nullptr, "lab-copy.xisf").hasValue()) << lastError;
    const auto bytes = readBytes(path("lab-copy.xisf"));
    EXPECT_NE(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()).find("colorSpace=\"CIELab\""),
              std::string::npos);
    // Four channels of grey is not a colour space this vocabulary names, so the schema says the
    // meaning is carried elsewhere and the file's own declaration decides what is written back.
    writeContainer({{"geometry=\"2:2:2\" sampleFormat=\"UInt8\" colorSpace=\"Gray\" pixelStorage=\"Planar\"", "",
                     std::string(8u, '\0')}},
                   "grayalpha.xisf");
    auto grayAlpha = decode("grayalpha.xisf");
    ASSERT_TRUE(grayAlpha.hasValue()) << lastError;
    EXPECT_EQ(grayAlpha->frame.descriptor()->channelSchema(), an::ChannelSchema::Named);
    ASSERT_TRUE(save(grayAlpha->frame, nullptr, grayAlpha->metadata, nullptr, "grayalpha-copy.xisf").hasValue())
        << lastError;
    const auto copy = readBytes(path("grayalpha-copy.xisf"));
    EXPECT_NE(std::string(reinterpret_cast<const char*>(copy.data()), copy.size()).find("colorSpace=\"Gray\""),
              std::string::npos);
}

TEST_F(XisfCodecTest, AtomicSaveStampsItsOwnIdentityAndNeverInheritsTheSourceStamp)
{
    auto source = frame<float>({0.1f, 0.2f, 0.3f}, 3u, 1u);
    auto identity = an::ArtifactIdentity::create("artifact-1", "run-2");
    ASSERT_TRUE(identity.hasValue());
    auto intent =
        an::OutputIntent::create({.sampleType = an::SampleType::Float32,
                                  .cards = {card(an::CodecNamespace::Xisf, "ANARTID", "stale"),
                                            card(an::CodecNamespace::Xisf, "Observation:Object:Name", "Carina")}});
    ASSERT_TRUE(save(source, &*intent, {}, &*identity).hasValue()) << lastError;
    auto decoded = decode();
    ASSERT_TRUE(decoded.hasValue()) << lastError;
    auto read = an::artifactIdentityOf(decoded->metadata.codec(), an::CodecNamespace::Xisf);
    ASSERT_TRUE(read.hasValue()) << read.status().message();
    ASSERT_TRUE(read->has_value());
    EXPECT_EQ(**read, *identity);
    auto clean = an::OutputIntent::create({.sampleType = an::SampleType::Float32});
    ASSERT_TRUE(save(decoded->frame, &*clean, decoded->metadata, nullptr, "derived.xisf").hasValue()) << lastError;
    auto derived = decode("derived.xisf");
    ASSERT_TRUE(derived.hasValue()) << lastError;
    EXPECT_EQ(derived->metadata.codec().find(an::CodecNamespace::Xisf, "ANARTID"), nullptr);
    EXPECT_NE(derived->metadata.codec().find(an::CodecNamespace::Xisf, "Observation:Object:Name"), nullptr);
    for (const auto& entry : std::filesystem::directory_iterator(directory))
        EXPECT_EQ(entry.path().extension(), ".xisf");
}

TEST_F(XisfCodecTest, WcsCardsTravelThroughTheSharedAstrometricBridge)
{
    auto source = frame<float>(std::vector<float>(63u, 0.5f), 9u, 7u);
    auto intent = an::OutputIntent::create(
        {.sampleType = an::SampleType::Float32,
         .cards = {card(an::CodecNamespace::Fits, "CTYPE1", "'RA---TAN'"),
                   card(an::CodecNamespace::Fits, "CTYPE2", "'DEC--TAN'"),
                   card(an::CodecNamespace::Fits, "CRPIX1", "5"), card(an::CodecNamespace::Fits, "CRPIX2", "4"),
                   card(an::CodecNamespace::Fits, "CRVAL1", "161.25"),
                   card(an::CodecNamespace::Fits, "CRVAL2", "-59.5"),
                   card(an::CodecNamespace::Fits, "CD1_1", "-0.0003"), card(an::CodecNamespace::Fits, "CD1_2", "0"),
                   card(an::CodecNamespace::Fits, "CD2_1", "0"), card(an::CodecNamespace::Fits, "CD2_2", "0.0003")}});
    ASSERT_TRUE(save(source, &*intent).hasValue()) << lastError;
    auto decoded = decode();
    ASSERT_TRUE(decoded.hasValue()) << lastError;
    ASSERT_EQ(an::checkWcsCards(decoded->metadata.codec().cards()), an::WcsCardError::None);
    const auto parsed = an::parseWcsCards(decoded->metadata.codec().cards(), 9u, 7u);
    ASSERT_TRUE(parsed.hasValue());
    EXPECT_EQ(decoded->metadata.astrometry(), *parsed->toAstrometricSolution());
}

TEST_F(XisfCodecTest, ATruncatedOrShortPayloadIsRefusedRatherThanReadPastItsEnd)
{
    auto source = frame<std::uint16_t>(std::vector<std::uint16_t>(1024u, 9u), 32u, 32u);
    ASSERT_TRUE(save(source).hasValue()) << lastError;
    const auto complete = readBytes(path());
    std::filesystem::resize_file(directory / "frame.xisf", complete.size() - 512u);
    auto truncated = decode();
    ASSERT_FALSE(truncated.hasValue());
    EXPECT_EQ(truncated.status().rootCause().code(), an::ErrorCode::InvalidData);
    // A header may also claim more samples than its own data block holds. The two are compared
    // before a byte is copied, so the shortfall is a refusal rather than a read past the block.
    writeContainer({{"geometry=\"8:8:1\" sampleFormat=\"UInt16\" colorSpace=\"Gray\" pixelStorage=\"Planar\"", "",
                     std::string(16u, '\0')}},
                   "short.xisf");
    auto shortBlock = decode("short.xisf");
    ASSERT_FALSE(shortBlock.hasValue());
    EXPECT_EQ(shortBlock.status().rootCause().code(), an::ErrorCode::InvalidData);
    // The registry names the adapter as the failing callback; the adapter's own reason is the
    // cause it wrapped, and it distinguishes an unreadable payload from an unreadable header.
    EXPECT_EQ(an::codecFailureReason(shortBlock.status().cause()), an::CodecFailure::MetadataUnreadable);
    EXPECT_EQ(an::codecFailureReason(truncated.status().cause()), an::CodecFailure::PayloadUnreadable);
}

TEST_F(XisfCodecTest, AContainerThatIsNotOneIsRefusedBeforeAnythingIsAllocated)
{
    const auto target = path("garbage.xisf");
    {
        std::ofstream out(
            std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(target.data()), target.size())),
            std::ios::binary);
        const std::string junk(4096u, 'Z');
        out.write(junk.data(), static_cast<std::streamsize>(junk.size()));
    }
    EXPECT_FALSE(decode("garbage.xisf").hasValue());
    auto execution = an::ExecutionContext::create(engine, {});
    ASSERT_TRUE(execution.hasValue());
    const auto missing = path("absent.xisf");
    EXPECT_FALSE(an::probeImage(engine, *execution, {missing}).hasValue());
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
}

TEST_F(XisfCodecTest, ImageSelectionCountsEveryImageTheContainerHolds)
{
    const std::vector<std::uint8_t> first{1u, 2u, 3u, 4u};
    const std::vector<std::uint8_t> second{9u, 8u};
    writeContainer({{"geometry=\"2:2:1\" sampleFormat=\"UInt8\" colorSpace=\"Gray\" pixelStorage=\"Planar\"", "",
                     payloadOf(first)},
                    {"geometry=\"2:1:1\" sampleFormat=\"UInt8\" colorSpace=\"Gray\" pixelStorage=\"Planar\"", "",
                     payloadOf(second)}},
                   "pair.xisf");
    auto execution = an::ExecutionContext::create(engine, {});
    ASSERT_TRUE(execution.hasValue());
    const auto source = path("pair.xisf");
    auto info = an::probeImage(engine, *execution, {source});
    ASSERT_TRUE(info.hasValue());
    EXPECT_EQ(info->imageCount, 2u);
    EXPECT_EQ(info->disposition, an::CodecProbeDisposition::DecodeRequired);
    auto selected = an::decodeImage(engine, *execution, {.path = source, .imageIndex = 1u});
    ASSERT_TRUE(selected.hasValue());
    EXPECT_EQ(values<std::uint8_t>(selected->frame), second);
    EXPECT_EQ(an::decodeImage(engine, *execution, {.path = source, .imageIndex = 2u}).status().code(),
              an::ErrorCode::OutOfRange);
}

TEST_F(XisfCodecTest, RegionReadsOnlyRequestedRowsAcrossEveryPlane)
{
    // A region of a unit in every form its pixel data can take - uncompressed in either storage model, compressed in
    // subblocks and as one, checksummed - decodes to the samples of the region alone, every plane's, in the file's own
    // arrangement: a box inside the image, one at its far corner, one pixel, a band of whole rows and one column.
    constexpr std::uint32_t kWidth = 640u;
    constexpr std::uint32_t kHeight = 480u;
    constexpr std::uint32_t kPlanes = 3u;
    const auto data = gradient<std::uint16_t>(kWidth, kHeight, kPlanes);
    const std::vector<an::CodecRegion> regions{{13u, 17u, 101u, 59u},
                                               {600u, 400u, 40u, 80u},
                                               {639u, 479u, 1u, 1u},
                                               {0u, 211u, kWidth, 37u},
                                               {320u, 0u, 1u, kHeight}};
    for (const BlockForm& form : blockForms())
    {
        SCOPED_TRACE(form.name);
        const std::string name = std::string(form.name) + ".xisf";
        ASSERT_TRUE(writeLibraryUnit(name, data, kWidth, kHeight, kPlanes, form));
        auto execution = an::ExecutionContext::create(engine, {});
        ASSERT_TRUE(execution.hasValue());
        const auto file = path(name.c_str());
        for (const an::CodecRegion& region : regions)
        {
            auto decoded = an::decodeImageRegion(engine, *execution, {file}, region);
            ASSERT_TRUE(decoded.hasValue()) << decoded.status().rootCause().message();
            EXPECT_EQ(values<std::uint16_t>(decoded->frame),
                      regionOf(data, kWidth, kHeight, kPlanes, region, form.storage))
                << region.x << "," << region.y << " " << region.width << "x" << region.height;
            EXPECT_EQ(decoded->frame.bytes().size(),
                      static_cast<std::size_t>(region.width) * region.height * kPlanes * sizeof(std::uint16_t));
            EXPECT_EQ(an::storageArrangementOf(*decoded->frame.layout()),
                      form.storage == openxisf::pixel_storage::planar ? an::StorageArrangement::Planar
                                                                      : an::StorageArrangement::Interleaved);
        }
        EXPECT_EQ(an::decodeImageRegion(engine, *execution, {file}, {kWidth - 1u, 0u, 2u, 1u}).status().code(),
                  an::ErrorCode::OutOfRange);
    }
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
}

TEST_F(XisfCodecTest, CfaRegionMovesPhaseAndSaveWritesTheEffectivePattern)
{
    // A region's filter array starts where the region does, and a save of the region writes the pattern it then has.
    auto source = frame<std::uint16_t>(std::vector<std::uint16_t>(25u, 17u), 5u, 5u, 1u,
                                       *an::CfaDescriptor::create(an::CfaPattern::Rggb, 0u, 0u));
    ASSERT_TRUE(save(source).hasValue()) << lastError;
    auto execution = an::ExecutionContext::create(engine, {});
    ASSERT_TRUE(execution.hasValue());
    const auto sourcePath = path();
    auto cropped = an::decodeImageRegion(engine, *execution, {sourcePath}, {1u, 1u, 3u, 3u});
    ASSERT_TRUE(cropped.hasValue()) << cropped.status().rootCause().message();
    EXPECT_EQ(cropped->frame.descriptor()->cfa().colorAt(0u, 0u), an::CfaColor::Blue);
    // A six-by-six table moves the same way, by the region's offset within its block.
    constexpr an::CfaColorEntries kTable = {
        an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Red,   an::CfaColor::Green, an::CfaColor::Green,
        an::CfaColor::Blue,  an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Blue,  an::CfaColor::Green,
        an::CfaColor::Green, an::CfaColor::Red,   an::CfaColor::Blue,  an::CfaColor::Red,   an::CfaColor::Green,
        an::CfaColor::Red,   an::CfaColor::Blue,  an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Green,
        an::CfaColor::Blue,  an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Red,   an::CfaColor::Green,
        an::CfaColor::Green, an::CfaColor::Red,   an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Blue,
        an::CfaColor::Red,   an::CfaColor::Blue,  an::CfaColor::Green, an::CfaColor::Blue,  an::CfaColor::Red,
        an::CfaColor::Green};
    *execution = an::ExecutionContext{};
    const auto tableCfa = *an::CfaDescriptor::create(kTable, 0u, 0u);
    auto tableFrame = frame<std::uint16_t>(std::vector<std::uint16_t>(144u, 9u), 12u, 12u, 1u, tableCfa);
    ASSERT_TRUE(save(tableFrame, nullptr, {}, nullptr, "table.xisf").hasValue()) << lastError;
    execution = an::ExecutionContext::create(engine, {});
    ASSERT_TRUE(execution.hasValue());
    const auto tablePath = path("table.xisf");
    auto tableRegion = an::decodeImageRegion(engine, *execution, {tablePath}, {7u, 4u, 5u, 6u});
    ASSERT_TRUE(tableRegion.hasValue()) << tableRegion.status().rootCause().message();
    for (std::uint32_t y = 0u; y < 6u; ++y)
        for (std::uint32_t x = 0u; x < 5u; ++x)
            EXPECT_EQ(tableRegion->frame.descriptor()->cfa().colorAt(x, y), tableCfa.colorAt(x + 7u, y + 4u))
                << x << "," << y;
    *execution = an::ExecutionContext{};
    auto intent = an::OutputIntent::create({.sampleType = an::SampleType::UInt16});
    ASSERT_TRUE(save(cropped->frame, &*intent, cropped->metadata, nullptr, "crop.xisf").hasValue()) << lastError;
    auto reread = decode("crop.xisf");
    ASSERT_TRUE(reread.hasValue()) << lastError;
    EXPECT_EQ(reread->frame.descriptor()->cfa().colorAt(0u, 0u), an::CfaColor::Blue);
    ASSERT_NE(reread->metadata.codec().find(an::CodecNamespace::Xisf, "ColorFilterArray"), nullptr);
    EXPECT_EQ(reread->metadata.codec().find(an::CodecNamespace::Xisf, "ColorFilterArray")->value(), "2x2:BGGR");
}

TEST_F(XisfCodecTest, ARegionCarriesItsOwnSolutionAndNothingOfTheWholeImages)
{
    // With metadata, a region's solution is the whole image's moved to the region's origin, and the world-coordinate
    // cards say so; without them, nothing of the solution is read.
    auto source = frame<float>(std::vector<float>(63u, 0.5f), 9u, 7u);
    auto intent = an::OutputIntent::create(
        {.sampleType = an::SampleType::Float32,
         .cards = {card(an::CodecNamespace::Fits, "CTYPE1", "'RA---TAN'"),
                   card(an::CodecNamespace::Fits, "CTYPE2", "'DEC--TAN'"),
                   card(an::CodecNamespace::Fits, "CRPIX1", "5"), card(an::CodecNamespace::Fits, "CRPIX2", "4"),
                   card(an::CodecNamespace::Fits, "CRVAL1", "161.25"),
                   card(an::CodecNamespace::Fits, "CRVAL2", "-59.5"),
                   card(an::CodecNamespace::Fits, "CD1_1", "-0.0003"), card(an::CodecNamespace::Fits, "CD1_2", "0"),
                   card(an::CodecNamespace::Fits, "CD2_1", "0"), card(an::CodecNamespace::Fits, "CD2_2", "0.0003")}});
    ASSERT_TRUE(save(source, &*intent).hasValue()) << lastError;
    auto execution = an::ExecutionContext::create(engine, {});
    ASSERT_TRUE(execution.hasValue());
    const auto file = path();
    auto whole = an::decodeImage(engine, *execution, {file});
    ASSERT_TRUE(whole.hasValue());
    const an::CodecRegion box{2u, 1u, 5u, 4u};
    auto region = an::decodeImageRegion(engine, *execution, {file}, box);
    ASSERT_TRUE(region.hasValue()) << region.status().rootCause().message();
    const auto parsed = an::parseWcsCards(whole->metadata.codec().cards(), 9u, 7u);
    ASSERT_TRUE(parsed.hasValue());
    const auto expected = an::cropWcsSolution(*parsed, box.x, box.y, box.width, box.height);
    ASSERT_TRUE(expected.hasValue());
    EXPECT_EQ(region->metadata.astrometry(), *expected->toAstrometricSolution());
    ASSERT_EQ(an::checkWcsCards(region->metadata.codec().cards()), an::WcsCardError::None);
    const auto regionCards = an::parseWcsCards(region->metadata.codec().cards(), box.width, box.height);
    ASSERT_TRUE(regionCards.hasValue());
    EXPECT_EQ(*regionCards->toAstrometricSolution(), *expected->toAstrometricSolution());
    auto pixels = an::decodeImageRegion(engine, *execution, {.path = file, .readMetadata = false}, box);
    ASSERT_TRUE(pixels.hasValue());
    EXPECT_FALSE(pixels->metadata.astrometry().present());
}

TEST_F(XisfCodecTest, ARowReaderDecodesBandAfterBandIntoCallerMemoryAsARegionDecodeDoes)
{
    // Opened once, a row reader decodes band after band into memory the caller holds, each band the samples a region
    // decode of the same rows returns, in any order and in every form a unit stores its pixel data in - a compressed
    // one in subblocks of 128 KiB, so that a band starts and ends inside them - and refuses what that decode refuses
    // without touching anything else.
    constexpr std::uint32_t kWidth = 613u;
    constexpr std::uint32_t kHeight = 409u;
    constexpr std::uint32_t kPlanes = 3u;
    const auto data = gradient<std::uint16_t>(kWidth, kHeight, kPlanes);
    for (const BlockForm& form : blockForms(std::uint64_t{128u} << 10u))
    {
        SCOPED_TRACE(form.name);
        const std::string name = std::string(form.name) + ".xisf";
        ASSERT_TRUE(writeLibraryUnit(name, data, kWidth, kHeight, kPlanes, form));
        auto execution = an::ExecutionContext::create(engine, {});
        ASSERT_TRUE(execution.hasValue());
        const auto sourcePath = path(name.c_str());
        auto opened = an::ImageRowReader::open(engine, *execution, {sourcePath});
        ASSERT_TRUE(opened.hasValue()) << opened.status().message();
        an::ImageRowReader reader = std::move(*opened);
        ASSERT_TRUE(reader.valid());
        ASSERT_NE(reader.info(), nullptr);
        EXPECT_EQ(reader.info()->descriptor.extent().height(), kHeight);
        const bool planar = form.storage == openxisf::pixel_storage::planar;
        for (const auto& [first, rows] : std::vector<std::pair<std::uint32_t, std::uint32_t>>{
                 {300u, 50u}, {0u, 64u}, {64u, 64u}, {408u, 1u}, {205u, 204u}, {0u, kHeight}})
        {
            const auto layout = reader.rowsLayout(rows);
            ASSERT_TRUE(layout.hasValue());
            EXPECT_EQ(layout->planeCount(), planar ? kPlanes : 1u);
            EXPECT_EQ(layout->rowStrideBytes(), kWidth * sizeof(std::uint16_t) * (planar ? 1u : kPlanes));
            std::vector<std::byte> band(static_cast<std::size_t>(layout->totalBytes()) + 5u, std::byte{0xA5});
            const auto allocationsBefore = engine.memoryResource()->counters().totalAllocations;
            const auto read = reader.readRows(*execution, first, rows, band);
            ASSERT_TRUE(read.hasValue()) << read.status().rootCause().message();
            EXPECT_EQ(read->totalBytes(), layout->totalBytes());
            EXPECT_EQ(engine.memoryResource()->counters().totalAllocations, allocationsBefore)
                << "the band is decoded into the caller's memory, not into a frame of the engine's and copied";
            const auto region = an::decodeImageRegion(engine, *execution, {sourcePath}, {0u, first, kWidth, rows});
            ASSERT_TRUE(region.hasValue());
            EXPECT_EQ(
                std::memcmp(band.data(), region->frame.bytes().data(), static_cast<std::size_t>(layout->totalBytes())),
                0)
                << "rows " << first << " to " << first + rows - 1u;
            const auto expected = regionOf(data, kWidth, kHeight, kPlanes, {0u, first, kWidth, rows}, form.storage);
            EXPECT_EQ(std::memcmp(band.data(), expected.data(), expected.size() * sizeof(std::uint16_t)), 0);
            EXPECT_EQ(band.back(), std::byte{0xA5}) << "nothing past the band is written";
        }
    }

    auto execution = an::ExecutionContext::create(engine, {});
    ASSERT_TRUE(execution.hasValue());
    auto opened = an::ImageRowReader::open(engine, *execution, {path("planar.xisf")});
    ASSERT_TRUE(opened.hasValue());
    an::ImageRowReader reader = std::move(*opened);
    std::vector<std::byte> band(static_cast<std::size_t>(reader.rowsLayout(2u)->totalBytes()));
    EXPECT_EQ(reader.readRows(*execution, kHeight - 1u, 2u, band).status().code(), an::ErrorCode::OutOfRange);
    EXPECT_EQ(reader.readRows(*execution, 0u, 0u, band).status().code(), an::ErrorCode::OutOfRange);
    EXPECT_EQ(reader.readRows(*execution, kHeight, 1u, band).status().code(), an::ErrorCode::OutOfRange);
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
    EXPECT_FALSE(an::ImageRowReader::open(engine, *execution, {path("absent.xisf")}).hasValue());

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

TEST_F(XisfCodecTest, ARowReaderRefusesTheRowsAShortPayloadNoLongerHolds)
{
    // A unit cut short before it is opened has its block checked against the file by the open, so the image is still
    // described and every band of it is refused with the block's own reason.
    constexpr std::uint32_t kWidth = 1024u;
    constexpr std::uint32_t kHeight = 512u;
    const auto data = gradient<std::uint16_t>(kWidth, kHeight, 1u);
    ASSERT_TRUE(writeLibraryUnit("cut.xisf", data, kWidth, kHeight, 1u, BlockForm{.name = "cut"}));
    std::filesystem::resize_file(directory / "cut.xisf", std::filesystem::file_size(directory / "cut.xisf") - 2048u);
    {
        auto execution = an::ExecutionContext::create(engine, {});
        ASSERT_TRUE(execution.hasValue());
        auto cut = an::ImageRowReader::open(engine, *execution, {path("cut.xisf")});
        ASSERT_TRUE(cut.hasValue()) << cut.status().rootCause().message();
        std::vector<std::byte> rows(2u * kWidth * sizeof(std::uint16_t));
        for (const std::uint32_t first : {0u, kHeight - 2u})
        {
            const auto refused = cut->readRows(*execution, first, 2u, rows);
            ASSERT_FALSE(refused.hasValue()) << first;
            EXPECT_EQ(refused.status().rootCause().code(), an::ErrorCode::InvalidData) << first;
            EXPECT_EQ(an::codecFailureReason(refused.status().cause()), an::CodecFailure::PayloadUnreadable) << first;
        }
    }
    // A unit cut short once it is open is read for the rows the file still holds and refused for those it does not, in
    // either storage model and in subblocks.
    for (const BlockForm& form : blockForms(std::uint64_t{64u} << 10u))
    {
        if (form.checksum || (form.codec && form.subblockBytes == 0u))
            continue;
        SCOPED_TRACE(form.name);
        const std::string name = std::string(form.name) + ".xisf";
        ASSERT_TRUE(writeLibraryUnit(name, data, kWidth, kHeight, 1u, form));
        auto execution = an::ExecutionContext::create(engine, {});
        ASSERT_TRUE(execution.hasValue());
        auto shortened = an::ImageRowReader::open(engine, *execution, {path(name.c_str())});
        ASSERT_TRUE(shortened.hasValue()) << shortened.status().message();
        const auto size = std::filesystem::file_size(directory / name);
        std::filesystem::resize_file(directory / name, size - size / 4u);
        std::vector<std::byte> rows(16u * kWidth * sizeof(std::uint16_t));
        ASSERT_TRUE(shortened->readRows(*execution, 0u, 16u, rows).hasValue());
        EXPECT_EQ(std::memcmp(rows.data(), data.data(), rows.size()), 0);
        const auto refused = shortened->readRows(*execution, kHeight - 16u, 16u, rows);
        ASSERT_FALSE(refused.hasValue());
        EXPECT_EQ(refused.status().rootCause().code(), an::ErrorCode::InvalidData);
    }
}

TEST_F(XisfCodecTest, ALargePayloadIsReadOnTheOperationsWorkersAsOneWorkerReadsIt)
{
    // A payload past the shared minimum, read by an operation granted eight workers, is read strip by strip on them
    // through the one reader when its block can be read in parts - uncompressed, in subblocks small beside its planes,
    // a checksum it does not verify - and in one call otherwise. Its samples are the ones one worker reads, whole and
    // as a band of whole rows that starts and ends inside a strip; its progress only moves on and reaches every row,
    // nothing discarded; a stop asked for at the first report ends it; and a payload cut short is refused as one worker
    // refuses it.
    const auto readWith = [&](const an::EngineContext& context, std::uint32_t workers, an::ProgressSink* progress,
                              an::TraceSink* trace, std::stop_token stop, const std::string& file,
                              const an::CodecRegion* region = nullptr) {
        an::ExecutionOptions options;
        options.maxConcurrency = workers;
        options.progressSink = progress;
        options.traceSink = trace;
        options.stopToken = stop;
        auto execution = an::ExecutionContext::create(context, options);
        EXPECT_TRUE(execution.hasValue());
        auto decoded = region != nullptr ? an::decodeImageRegion(context, *execution, {file}, *region)
                                         : an::decodeImage(context, *execution, {file});
        return std::make_pair(std::move(decoded), execution->operationCounters());
    };
    constexpr std::uint32_t kWidth = 2048u;
    constexpr std::uint32_t kHeight = 1400u;
    constexpr std::uint32_t kPlanes = 3u;
    const auto data = gradient<float>(kWidth, kHeight, kPlanes);
    ASSERT_GE(data.size() * sizeof(float), std::size_t{32u} << 20u);
    auto unverified = [] {
        auto adapter = an::createXisfCodec(an::XisfCodecOptions{.verifyChecksums = false});
        std::array adapters{*adapter};
        an::EngineContextOptions options;
        options.codecRegistry = *an::CodecRegistry::create(adapters);
        options.maxConcurrency = 8u;
        return an::EngineContext::create(options);
    }();
    ASSERT_TRUE(unverified.hasValue());
    struct Case
    {
        BlockForm form;
        const an::EngineContext* context;
        bool strips;
    };
    // Subblocks of 256 KiB put eight of them in each of a shuffled plane's four stretches; a block in subblocks of
    // 1 MiB has too few for its planes to be cut, and is read in one call as one subblock is.
    const auto forms = blockForms(std::uint64_t{256u} << 10u);
    auto large = forms[2];
    large.name = "zstd-sh-1MiB";
    large.subblockBytes = std::uint64_t{1u} << 20u;
    for (const Case& read : {Case{forms[0], &engine, true}, Case{forms[1], &engine, true},
                             Case{forms[2], &engine, true}, Case{large, &engine, false}, Case{forms[3], &engine, false},
                             Case{forms[4], &engine, false}, Case{forms[4], &*unverified, true}})
    {
        SCOPED_TRACE(std::string(read.form.name) + (read.context == &engine ? "" : ", checksum not verified"));
        const std::string name = std::string(read.form.name) + ".xisf";
        const auto file = path(name.c_str());
        ASSERT_TRUE(writeLibraryUnit(name, data, kWidth, kHeight, kPlanes, read.form));
        const std::uint64_t planes = read.form.storage == openxisf::pixel_storage::planar ? kPlanes : 1u;
        const auto expected = regionOf(data, kWidth, kHeight, kPlanes, {0u, 0u, kWidth, kHeight}, read.form.storage);

        auto [alone, aloneCounters] = readWith(*read.context, 1u, nullptr, nullptr, {}, file);
        ASSERT_TRUE(alone.hasValue()) << alone.status().rootCause().message();
        RecordedProgress progress;
        RecordedCounter strips("strips");
        auto [shared, sharedCounters] = readWith(*read.context, 8u, &progress, &strips, {}, file);
        ASSERT_TRUE(shared.hasValue()) << shared.status().rootCause().message();
        EXPECT_EQ(values<float>(shared->frame), values<float>(alone->frame));
        EXPECT_EQ(values<float>(shared->frame), expected);
        EXPECT_EQ(strips.values.size(), read.strips ? 1u : 0u);
        if (read.strips && !strips.values.empty())
            EXPECT_GE(strips.values.front(), 2.0);
        ASSERT_GT(progress.completed.size(), 1u);
        EXPECT_TRUE(std::is_sorted(progress.completed.begin(), progress.completed.end()));
        EXPECT_EQ(progress.completed.back(), static_cast<std::uint64_t>(kHeight) * planes);
        EXPECT_EQ(progress.totals.back(), static_cast<std::uint64_t>(kHeight) * planes);
        EXPECT_EQ(sharedCounters.discardedProgressReports, 0u);

        const an::CodecRegion band{0u, 3u, kWidth, kHeight - 5u};
        auto [bandAlone, bandAloneCounters] = readWith(*read.context, 1u, nullptr, nullptr, {}, file, &band);
        RecordedCounter bandStrips("strips");
        auto [bandShared, bandSharedCounters] = readWith(*read.context, 8u, nullptr, &bandStrips, {}, file, &band);
        ASSERT_TRUE(bandAlone.hasValue()) << bandAlone.status().rootCause().message();
        ASSERT_TRUE(bandShared.hasValue()) << bandShared.status().rootCause().message();
        EXPECT_EQ(values<float>(bandShared->frame), values<float>(bandAlone->frame));
        EXPECT_EQ(values<float>(bandShared->frame), regionOf(data, kWidth, kHeight, kPlanes, band, read.form.storage));
        EXPECT_EQ(bandStrips.values.size(), read.strips ? 1u : 0u);

        std::stop_source stop;
        RecordedProgress stopping(&stop);
        auto [stopped, stoppedCounters] = readWith(*read.context, 8u, &stopping, nullptr, stop.get_token(), file);
        ASSERT_FALSE(stopped.hasValue());
        EXPECT_EQ(stopped.status().code(), an::ErrorCode::Cancelled);

        std::filesystem::resize_file(directory / name, std::filesystem::file_size(directory / name) - 4096u);
        auto [shortAlone, shortAloneCounters] = readWith(*read.context, 1u, nullptr, nullptr, {}, file);
        auto [shortShared, shortSharedCounters] = readWith(*read.context, 8u, nullptr, nullptr, {}, file);
        ASSERT_FALSE(shortAlone.hasValue());
        ASSERT_FALSE(shortShared.hasValue());
        EXPECT_EQ(shortShared.status().code(), shortAlone.status().code());
        EXPECT_EQ(shortShared.status().code(), an::ErrorCode::InvalidData);
    }
}

TEST_F(XisfCodecTest, ARowReaderStatesTheRowsTheSmallestPieceItDecodesWholeSpans)
{
    // A band of a compressed block decodes the subblocks that hold its bytes, again for every band that reaches them,
    // so a reader states how many rows one subblock spans: of one stretch of the block, which with shuffling holds one
    // byte of every sample - a row of 512 UInt16 samples is 512 bytes there and 1024 unshuffled, and 1536 when three
    // channels share each pixel. An uncompressed block is read run by run; a block compressed whole, or whose checksum
    // is verified, is decoded or verified whole by every band.
    constexpr std::uint32_t kWidth = 512u;
    constexpr std::uint32_t kHeight = 512u;
    constexpr std::uint64_t kSubblock = std::uint64_t{64u} << 10u;
    struct Case
    {
        BlockForm form;
        std::uint32_t planes;
        std::uint32_t rows;
    };
    const std::vector<Case> cases{
        {{.name = "planar"}, 1u, 1u},
        {{.name = "normal", .storage = openxisf::pixel_storage::normal}, 3u, 1u},
        {{.name = "zstd-sh", .codec = openxisf::codec::zstd, .shuffle = true, .subblockBytes = kSubblock}, 1u, 128u},
        {{.name = "zstd", .codec = openxisf::codec::zstd, .subblockBytes = kSubblock}, 1u, 64u},
        {{.name = "zstd-sh-planes", .codec = openxisf::codec::zstd, .shuffle = true, .subblockBytes = kSubblock},
         3u,
         128u},
        {{.name = "zstd-sh-normal",
          .storage = openxisf::pixel_storage::normal,
          .codec = openxisf::codec::zstd,
          .shuffle = true,
          .subblockBytes = kSubblock},
         3u,
         43u},
        {{.name = "zstd-sh-whole", .codec = openxisf::codec::zstd, .shuffle = true}, 1u, kHeight},
        {{.name = "sha256", .checksum = openxisf::checksum_algorithm::sha256}, 1u, kHeight}};
    for (const Case& test : cases)
    {
        SCOPED_TRACE(test.form.name);
        const auto data = gradient<std::uint16_t>(kWidth, kHeight, test.planes);
        const std::string name = std::string(test.form.name) + ".xisf";
        ASSERT_TRUE(writeLibraryUnit(name, data, kWidth, kHeight, test.planes, test.form));
        auto execution = an::ExecutionContext::create(engine, {});
        ASSERT_TRUE(execution.hasValue());
        auto reader = an::ImageRowReader::open(engine, *execution, {path(name.c_str())});
        ASSERT_TRUE(reader.hasValue()) << reader.status().message();
        EXPECT_EQ(reader->decodeGranularityRows(), test.rows);
    }
    // A checksum the adapter was made not to verify leaves the block to be read run by run.
    auto unverifiedEngine = [] {
        auto adapter = an::createXisfCodec(an::XisfCodecOptions{.verifyChecksums = false});
        std::array adapters{*adapter};
        an::EngineContextOptions options;
        options.codecRegistry = *an::CodecRegistry::create(adapters);
        return an::EngineContext::create(options);
    }();
    ASSERT_TRUE(unverifiedEngine.hasValue());
    auto execution = an::ExecutionContext::create(*unverifiedEngine, {});
    ASSERT_TRUE(execution.hasValue());
    auto unverified = an::ImageRowReader::open(*unverifiedEngine, *execution, {path("sha256.xisf")});
    ASSERT_TRUE(unverified.hasValue()) << unverified.status().message();
    EXPECT_EQ(unverified->decodeGranularityRows(), 1u);
    EXPECT_EQ(an::ImageRowReader{}.decodeGranularityRows(), 0u);
}

TEST_F(XisfCodecTest, AChecksumTheCallerChoseNotToVerifyIsNotVerified)
{
    // A unit whose data no longer match their checksum is refused by the adapter that verifies, whole, by region and
    // by band, and read as it stands by the one created not to, which reads the changed sample back.
    constexpr std::uint32_t kWidth = 256u;
    constexpr std::uint32_t kHeight = 128u;
    const auto data = gradient<std::uint16_t>(kWidth, kHeight, 1u);
    ASSERT_TRUE(writeLibraryUnit("sha256.xisf", data, kWidth, kHeight, 1u, blockForms()[4]));
    // The block is the file's last bytes: its last sample, the image's last, is changed.
    {
        std::fstream file(directory / "sha256.xisf", std::ios::in | std::ios::out | std::ios::binary);
        file.seekp(-1, std::ios::end);
        file.put('\x5a');
    }
    auto unverifiedEngine = [] {
        auto adapter = an::createXisfCodec(an::XisfCodecOptions{.verifyChecksums = false});
        std::array adapters{*adapter};
        an::EngineContextOptions options;
        options.codecRegistry = *an::CodecRegistry::create(adapters);
        return an::EngineContext::create(options);
    }();
    ASSERT_TRUE(unverifiedEngine.hasValue());
    const auto file = path("sha256.xisf");
    const an::CodecRegion lastRows{0u, kHeight - 2u, kWidth, 2u};
    {
        auto execution = an::ExecutionContext::create(engine, {});
        ASSERT_TRUE(execution.hasValue());
        const auto whole = an::decodeImage(engine, *execution, {file});
        ASSERT_FALSE(whole.hasValue());
        EXPECT_EQ(whole.status().rootCause().code(), an::ErrorCode::InvalidData);
        EXPECT_FALSE(an::decodeImageRegion(engine, *execution, {file}, lastRows).hasValue());
        auto reader = an::ImageRowReader::open(engine, *execution, {file});
        ASSERT_TRUE(reader.hasValue());
        std::vector<std::byte> band(static_cast<std::size_t>(kWidth) * 2u * sizeof(std::uint16_t));
        EXPECT_EQ(reader->readRows(*execution, 0u, 2u, band).status().code(), an::ErrorCode::InvalidData)
            << "the rows a checksum covers are refused with the block, the unchanged ones among them";
    }
    auto execution = an::ExecutionContext::create(*unverifiedEngine, {});
    ASSERT_TRUE(execution.hasValue());
    auto whole = an::decodeImage(*unverifiedEngine, *execution, {file});
    ASSERT_TRUE(whole.hasValue()) << whole.status().rootCause().message();
    auto changed = data;
    std::byte last[sizeof(std::uint16_t)];
    std::memcpy(last, &changed.back(), sizeof(last));
    last[sizeof(last) - 1u] = std::byte{0x5a};
    std::memcpy(&changed.back(), last, sizeof(last));
    EXPECT_EQ(values<std::uint16_t>(whole->frame), changed);
    auto region = an::decodeImageRegion(*unverifiedEngine, *execution, {file}, lastRows);
    ASSERT_TRUE(region.hasValue()) << region.status().rootCause().message();
    EXPECT_EQ(values<std::uint16_t>(region->frame),
              regionOf(changed, kWidth, kHeight, 1u, lastRows, openxisf::pixel_storage::planar));
}

TEST_F(XisfCodecTest, BoundedRegionQueryDistinguishesBlocksReadInPartsFromBlocksReadWhole)
{
    // A block read in parts - uncompressed, or compressed in subblocks - bounds what a region read of it takes from the
    // library; one compressed as one subblock, one whose checksum is verified and one held in the header do not, and a
    // region of each still reads correctly.
    constexpr std::uint32_t kWidth = 512u;
    constexpr std::uint32_t kHeight = 512u;
    const auto data = gradient<std::uint16_t>(kWidth, kHeight, 1u);
    const an::CodecRegion box{100u, 200u, 50u, 30u};
    for (const BlockForm& form : blockForms(std::uint64_t{64u} << 10u))
    {
        SCOPED_TRACE(form.name);
        const std::string name = std::string(form.name) + ".xisf";
        ASSERT_TRUE(writeLibraryUnit(name, data, kWidth, kHeight, 1u, form));
        auto execution = an::ExecutionContext::create(engine, {});
        ASSERT_TRUE(execution.hasValue());
        const auto file = path(name.c_str());
        const bool inParts = !form.checksum && (!form.codec || form.subblockBytes != 0u);
        auto bounded = an::supportsBoundedXisfRegionDecode(engine, *execution, file);
        ASSERT_TRUE(bounded.hasValue()) << bounded.status().message();
        EXPECT_EQ(*bounded, inParts);
        // A checksum the caller chose not to verify leaves the block to be read in parts.
        auto unverified = an::supportsBoundedXisfRegionDecode(engine, *execution, file, {.verifyChecksums = false});
        ASSERT_TRUE(unverified.hasValue());
        EXPECT_EQ(*unverified, inParts || form.checksum.has_value());
        auto region = an::decodeImageRegion(engine, *execution, {file}, box);
        ASSERT_TRUE(region.hasValue()) << region.status().rootCause().message();
        EXPECT_EQ(values<std::uint16_t>(region->frame), regionOf(data, kWidth, kHeight, 1u, box, form.storage));
    }
    // A block written in the header is held by the reader from the open on, whatever a region needs of it.
    writeContainer({{"geometry=\"4:2:1\" sampleFormat=\"UInt16\" colorSpace=\"Gray\"", "", std::string(16u, '\0')}},
                   "attached.xisf");
    {
        auto execution = an::ExecutionContext::create(engine, {});
        auto attached = an::supportsBoundedXisfRegionDecode(engine, *execution, path("attached.xisf"));
        ASSERT_TRUE(attached.hasValue()) << attached.status().message();
        EXPECT_TRUE(*attached);
    }
    std::string header = "<xisf version=\"1.0\" xmlns=\"http://www.pixinsight.com/xisf\"><Image geometry=\"4:2:1\" "
                         "sampleFormat=\"UInt16\" colorSpace=\"Gray\" location=\"inline:base64\">" +
                         std::string("AAAAAAAAAAAAAAAAAAAAAA==") + "</Image></xisf>";
    std::string unit = "XISF0100";
    unit.resize(16u, '\0');
    const auto length = static_cast<std::uint32_t>(header.size());
    std::memcpy(unit.data() + 8u, &length, sizeof(length));
    unit += header;
    {
        std::ofstream out(directory / "inline.xisf", std::ios::binary | std::ios::trunc);
        out.write(unit.data(), static_cast<std::streamsize>(unit.size()));
    }
    auto execution = an::ExecutionContext::create(engine, {});
    ASSERT_TRUE(execution.hasValue());
    auto inlined = an::supportsBoundedXisfRegionDecode(engine, *execution, path("inline.xisf"));
    ASSERT_TRUE(inlined.hasValue()) << inlined.status().message();
    EXPECT_FALSE(*inlined);
    auto pixels = an::decodeImageRegion(engine, *execution, {path("inline.xisf")}, {1u, 1u, 2u, 1u});
    ASSERT_TRUE(pixels.hasValue()) << pixels.status().rootCause().message();
    EXPECT_EQ(values<std::uint16_t>(pixels->frame), (std::vector<std::uint16_t>{0u, 0u}));
    EXPECT_EQ(an::supportsBoundedXisfRegionDecode(engine, *execution, {}).status().code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_FALSE(an::supportsBoundedXisfRegionDecode(engine, *execution, path("absent.xisf")).hasValue());
    EXPECT_EQ(an::supportsBoundedXisfRegionDecode(engine, an::ExecutionContext{}, path("inline.xisf")).status().code(),
              an::ErrorCode::NotInitialized);
}

TEST_F(XisfCodecTest, ARegionReadTakesASubblockAtATimeFromTheLibrary)
{
    // What a region read takes from the library besides the frame follows the block's form: nothing for a band of
    // whole rows of an uncompressed block, at most the staging buffer for any other region of one, a subblock and its
    // stored bytes for a block in subblocks, whatever the region, and the block's stored bytes for one verified whole;
    // a whole decode of a shuffled block takes a buffer of the block's size besides. The frame is past the size a read
    // shares between workers, and its subblocks small enough beside its planes to cut them, so the plan of a read on
    // eight workers is the strips'.
    constexpr std::uint32_t kWidth = 2048u;
    constexpr std::uint32_t kHeight = 1400u;
    constexpr std::uint64_t kSubblock = std::uint64_t{256u} << 10u;
    constexpr std::uint64_t kFrameBytes = std::uint64_t{kWidth} * kHeight * 3u * sizeof(float);
    const auto data = gradient<float>(kWidth, kHeight, 3u);
    const an::CodecRegion band{0u, 500u, kWidth, 100u};
    const an::CodecRegion box{700u, 300u, 90u, 400u};
    for (const BlockForm& form : blockForms(kSubblock))
    {
        SCOPED_TRACE(form.name);
        const std::string name = std::string(form.name) + ".xisf";
        ASSERT_TRUE(writeLibraryUnit(name, data, kWidth, kHeight, 3u, form));
        an::ExecutionOptions one;
        one.maxConcurrency = 1u;
        auto execution = an::ExecutionContext::create(engine, one);
        ASSERT_TRUE(execution.hasValue());
        const auto file = path(name.c_str());
        auto whole = an::xisfDecodeLibraryBytes(engine, *execution, file, 0u, {});
        auto bandBytes = an::xisfRegionDecodeLibraryBytes(engine, *execution, file, band);
        auto boxBytes = an::xisfRegionDecodeLibraryBytes(engine, *execution, file, box);
        ASSERT_TRUE(whole.hasValue()) << whole.status().message();
        ASSERT_TRUE(bandBytes.hasValue()) << bandBytes.status().message();
        ASSERT_TRUE(boxBytes.hasValue()) << boxBytes.status().message();
        if (form.checksum)
        {
            // Read whole into the frame, it is verified there; a region reads it all first.
            EXPECT_EQ(*whole, 0u);
            EXPECT_EQ(*bandBytes, kFrameBytes);
            EXPECT_EQ(*boxBytes, kFrameBytes);
            auto unverified =
                an::xisfRegionDecodeLibraryBytes(engine, *execution, file, band, 0u, {.verifyChecksums = false});
            ASSERT_TRUE(unverified.hasValue());
            EXPECT_EQ(*unverified, 0u);
        }
        else if (!form.codec)
        {
            EXPECT_EQ(*whole, 0u);
            EXPECT_EQ(*bandBytes, 0u);
            EXPECT_GT(*boxBytes, 0u);
            EXPECT_LE(*boxBytes, std::uint64_t{4u} << 20u);
        }
        else if (form.subblockBytes != 0u)
        {
            EXPECT_GT(*whole, kFrameBytes) << "the stored bytes and the buffer a shuffled block is unshuffled from";
            EXPECT_LE(*bandBytes, 2u * kSubblock);
            EXPECT_EQ(*boxBytes, *bandBytes);
        }
        else
        {
            // Compressed as one subblock, a region decodes the whole block.
            EXPECT_GT(*bandBytes, kFrameBytes);
            EXPECT_EQ(*boxBytes, *bandBytes);
        }
        // Read on eight workers, a block read in parts is read strip by strip, each worker a subblock at a time.
        *execution = an::ExecutionContext{};
        an::ExecutionOptions eight;
        eight.maxConcurrency = 8u;
        auto shared = an::ExecutionContext::create(engine, eight);
        ASSERT_TRUE(shared.hasValue());
        auto sharedWhole = an::xisfDecodeLibraryBytes(engine, *shared, file, 0u, {});
        ASSERT_TRUE(sharedWhole.hasValue());
        if (form.codec && form.subblockBytes != 0u)
        {
            EXPECT_LT(*sharedWhole, *whole);
            EXPECT_LE(*sharedWhole, 8u * 2u * kSubblock);
        }
        else
            EXPECT_EQ(*sharedWhole, *whole);
        EXPECT_EQ(an::xisfRegionDecodeLibraryBytes(engine, *shared, file, {kWidth, 0u, 1u, 1u}).status().code(),
                  an::ErrorCode::OutOfRange);
    }
}

#if defined(_MSC_VER) && defined(_DEBUG)
TEST_F(XisfCodecTest, WhatARegionReadAllocatesStaysWithinTheBoundTheLibraryQueryAnswers)
{
    // The most the library and its codecs hold at once while one worker reads a region - every allocation counted
    // until it is released - is within the region query's answer, besides a codec's decoder state, which that answer
    // leaves out; a whole decode of the block in subblocks holds many times more.
    constexpr std::uint32_t kWidth = 1600u;
    constexpr std::uint32_t kHeight = 1200u;
    constexpr std::uint64_t kDecoderState = std::uint64_t{512u} << 10u;
    const auto data = gradient<float>(kWidth, kHeight, 3u);
    for (const BlockForm& form : blockForms())
    {
        SCOPED_TRACE(form.name);
        const std::string name = std::string(form.name) + ".xisf";
        ASSERT_TRUE(writeLibraryUnit(name, data, kWidth, kHeight, 3u, form));
        an::ExecutionOptions one;
        one.maxConcurrency = 1u;
        auto execution = an::ExecutionContext::create(engine, one);
        ASSERT_TRUE(execution.hasValue());
        const auto file = path(name.c_str());
        for (const an::CodecRegion& region :
             {an::CodecRegion{0u, 500u, kWidth, 100u}, an::CodecRegion{700u, 300u, 90u, 400u}})
        {
            auto bound = an::xisfRegionDecodeLibraryBytes(engine, *execution, file, region);
            ASSERT_TRUE(bound.hasValue());
            std::uint64_t peak = 0u;
            {
                codec_test::PeakAllocation measured;
                auto decoded = an::decodeImageRegion(engine, *execution, {.path = file, .readMetadata = false}, region);
                ASSERT_TRUE(decoded.hasValue()) << decoded.status().rootCause().message();
                // The frame is the engine's, counted by its budget, and seen here allocated: what is left is the
                // library's.
                const std::uint64_t frameBytes = decoded->frame.bytes().size();
                ASSERT_GE(measured.peakBytes, frameBytes) << "the read ran where this count cannot see it";
                peak = measured.peakBytes - frameBytes;
            }
            EXPECT_LE(peak, *bound + kDecoderState) << region.x << "," << region.y;
        }
    }
}
#endif

TEST_F(XisfCodecTest, TheLibraryWorkspaceOfADecodeIsABoundAMemoryPlannerCanUse)
{
    std::vector<float> samples(300u);
    for (std::size_t i = 0u; i < samples.size(); ++i)
        samples[i] = static_cast<float>(i % 7u) * 0.125f;
    {
        auto source = frame<float>(samples, 10u, 10u, 3u);
        ASSERT_TRUE(save(source).hasValue()) << lastError;
        auto deflate = an::OutputIntent::create({.sampleType = an::SampleType::Float32,
                                                 .compression = an::OutputCompression::Deflate,
                                                 .byteShuffle = false});
        ASSERT_TRUE(save(source, &*deflate, {}, nullptr, "deflate.xisf").hasValue()) << lastError;
    }
    auto execution = an::ExecutionContext::create(engine, {});
    ASSERT_TRUE(execution.hasValue());
    const auto file = path();
    // An uncompressed block is read straight into the frame the engine allocates, so nothing else is needed.
    auto plain = an::xisfDecodeLibraryBytes(engine, *execution, file);
    ASSERT_TRUE(plain.hasValue()) << plain.status().message();
    EXPECT_EQ(*plain, 0u);
    // A compressed one needs its stored bytes, fewer than the frame's for samples this repetitive.
    auto compressed = an::xisfDecodeLibraryBytes(engine, *execution, path("deflate.xisf"));
    ASSERT_TRUE(compressed.hasValue()) << compressed.status().message();
    EXPECT_GT(*compressed, 0u);
    EXPECT_LT(*compressed, samples.size() * sizeof(float));
    // Describing a file allocates no samples at all: the figure comes from the header.
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
    EXPECT_EQ(an::xisfDecodeLibraryBytes(engine, *execution, file, 1u).status().code(), an::ErrorCode::OutOfRange);
    EXPECT_EQ(an::xisfDecodeLibraryBytes(engine, *execution, {}).status().code(), an::ErrorCode::InvalidArgument);
}

TEST_F(XisfCodecTest, APixelsOnlyReadLoadsNoAncillaryData)
{
    // A thumbnail of 1024 x 1024 RGB UInt16 samples, six MiB attached after a two-pixel image, is the one large
    // block of the file: a read that loaded it would read six MiB the samples asked for do not need.
    constexpr std::uint64_t kThumbnailBytes = 1024u * 1024u * 3u * 2u;
    // The image's four bytes follow the header and the thumbnail follows them; the header's length depends on the
    // positions it states, so it is recomputed until the two agree.
    const auto headerOf = [&](std::uint64_t length) {
        return "<?xml version=\"1.0\" encoding=\"UTF-8\"?><xisf version=\"1.0\" "
               "xmlns=\"http://www.pixinsight.com/xisf\"><Image geometry=\"2:1:1\" sampleFormat=\"UInt16\" "
               "colorSpace=\"Gray\" location=\"attachment:" +
               std::to_string(16u + length) +
               ":4\"><Thumbnail geometry=\"1024:1024:3\" sampleFormat=\"UInt16\" colorSpace=\"RGB\" "
               "location=\"attachment:" +
               std::to_string(16u + length + 4u) + ":" + std::to_string(kThumbnailBytes) +
               "\"/></Image><Metadata><Property id=\"XISF:CreatorApplication\" type=\"String\" value=\"test\"/>"
               "</Metadata></xisf>";
    };
    std::string header = headerOf(0u);
    while (headerOf(header.size()).size() != header.size())
        header = headerOf(header.size());
    header = headerOf(header.size());
    std::string file = "XISF0100";
    file.resize(16u, '\0');
    const auto stated = static_cast<std::uint32_t>(header.size());
    std::memcpy(file.data() + 8u, &stated, sizeof(stated));
    file += header;
    file += std::string("\x01\x00\x02\x00", 4u);
    file += std::string(static_cast<std::size_t>(kThumbnailBytes), '\x7f');
    {
        std::ofstream out(directory / "thumbnail.xisf", std::ios::binary | std::ios::trunc);
        out.write(file.data(), static_cast<std::streamsize>(file.size()));
    }
    const auto bytesRead = []() -> std::uint64_t {
#if defined(_WIN32)
        IO_COUNTERS counters{};
        return GetProcessIoCounters(GetCurrentProcess(), &counters) != 0 ? counters.ReadTransferCount : 0u;
#else
        std::ifstream io("/proc/self/io");
        for (std::string line; std::getline(io, line);)
            if (line.starts_with("rchar:"))
                return std::stoull(line.substr(6u));
        return 0u;
#endif
    };
    auto execution = an::ExecutionContext::create(engine, {});
    ASSERT_TRUE(execution.hasValue());
    const auto source = path("thumbnail.xisf");
    const auto beforePixels = bytesRead();
    auto pixels = an::decodeImage(engine, *execution, {.path = source, .readMetadata = false});
    const auto pixelsRead = bytesRead() - beforePixels;
    ASSERT_TRUE(pixels.hasValue()) << pixels.status().rootCause().message();
    EXPECT_EQ(values<std::uint16_t>(pixels->frame), (std::vector<std::uint16_t>{1u, 2u}));
    const auto beforeMetadata = bytesRead();
    auto described = an::decodeImage(engine, *execution, {.path = source});
    const auto metadataRead = bytesRead() - beforeMetadata;
    ASSERT_TRUE(described.hasValue()) << described.status().rootCause().message();
    // The read with metadata loads the thumbnail's pixels with the unit, which is how the count is known to see
    // the file's reads at all; the read without metadata opens the header alone.
    EXPECT_GE(metadataRead, kThumbnailBytes);
    EXPECT_LT(pixelsRead, kThumbnailBytes / 8u);
}

TEST_F(XisfCodecTest, DisproportionateAttachmentsAndDecompressionDeclarationsAreRefused)
{
    for (const auto& compression : {std::string{}, std::string{" compression=\"zlib:1048576\""},
                                    std::string{" compression=\"zlib+sh:1048576:2\""}})
    {
        const auto payload = compression.empty() ? std::string(1048576u, '\0') : std::string(4u, '\0');
        writeContainer({{"geometry=\"2:1:1\" sampleFormat=\"UInt16\" colorSpace=\"Gray\"" + compression, {}, payload}},
                       "oversized.xisf");
        auto execution = an::ExecutionContext::create(engine, {});
        const auto name = path("oversized.xisf");
        EXPECT_FALSE(an::xisfDecodeLibraryBytes(engine, *execution, name).hasValue());
        EXPECT_FALSE(an::decodeImage(engine, *execution, {.path = name, .readMetadata = false}).hasValue());
        EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
    }
    writeContainer({{"geometry=\"2:1:1\" sampleFormat=\"UInt16\" colorSpace=\"Gray\"", {}, std::string(4u, '\0')}},
                   "valid.xisf");
    EXPECT_TRUE(decode("valid.xisf").hasValue()) << lastError;
}

TEST_F(XisfCodecTest, HeaderLengthIsBoundedBeforeTheLibraryAllocatesXmlStorage)
{
    const auto name = path();
    std::string header = "XISF0100";
    header.resize(16u, '\0');
    const std::uint32_t length = 0xffffffffu;
    std::memcpy(header.data() + 8u, &length, sizeof(length));
    {
        std::ofstream out(directory / "frame.xisf", std::ios::binary);
        out.write(header.data(), static_cast<std::streamsize>(header.size()));
    }
    EXPECT_FALSE(decode().hasValue());
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
}

TEST_F(XisfCodecTest, EmptyDimensionsAndShortDecompressionCannotProduceAnImage)
{
    for (const auto* geometry : {"0:1:1", "1:0:1", "1:1:0"})
    {
        writeContainer({{std::string("geometry=\"") + geometry + "\" sampleFormat=\"UInt16\" colorSpace=\"Gray\"", "",
                         std::string(4u, '\0')}});
        const auto result = decode();
        // The library leaves out an image whose geometry describes no pixels, before any payload is sized, and
        // the unit then holds no image to describe.
        EXPECT_EQ(result.status().rootCause().code(), an::ErrorCode::InvalidData);
    }
    // Independent zlib stream for two bytes; the image and compression declaration both promise four.
    writeContainer({{"geometry=\"2:1:1\" sampleFormat=\"UInt16\" colorSpace=\"Gray\" compression=\"zlib:4\"", "",
                     std::string("\x78\x9c\x63\x64\x00\x00\x00\x04\x00\x02", 10u)}});
    EXPECT_FALSE(decode().hasValue());
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
}

#if defined(_MSC_VER) && defined(_DEBUG)
TEST_F(XisfCodecTest, WorkspaceQueryAllocationFailuresReturnAStatus)
{
    writeContainer({{"geometry=\"2:1:1\" sampleFormat=\"UInt16\" colorSpace=\"Gray\"", "", std::string(4u, '\0')}});
    auto execution = an::ExecutionContext::create(engine, {});
    const auto source = path();
    bool reachedUnfaultedQuery = false;
    // Every allocation the query makes is refused in turn - the library's open, its XML parser and its model of the
    // header among them - until one run makes none fail.
    for (std::size_t ordinal = 1u; ordinal <= 4096u; ++ordinal)
    {
        bool refused = false;
        const auto result = [&] {
            codec_test::AllocationFault fault(ordinal);
            auto queried = an::xisfDecodeLibraryBytes(engine, *execution, source);
            refused = fault.refused;
            return queried;
        }();
        if (refused)
        {
            // A refusal anywhere in the library or its XML parser is an allocation failure, or an opaque one.
            EXPECT_FALSE(result.hasValue()) << ordinal;
            EXPECT_TRUE(result.code() == an::ErrorCode::AllocationFailed ||
                        result.code() == an::ErrorCode::ExternalFailure)
                << ordinal;
        }
        else
        {
            EXPECT_TRUE(result.hasValue()) << ordinal;
            reachedUnfaultedQuery = true;
            break;
        }
    }
    EXPECT_TRUE(reachedUnfaultedQuery);
}
#endif

TEST_F(XisfCodecTest, CancellationAndMemoryRefusalReleaseUnpublishedSamples)
{
    {
        auto source = frame<float>(std::vector<float>(4096u, 1.0f), 64u, 64u);
        ASSERT_TRUE(save(source).hasValue()) << lastError;
    }
    auto constrained = makeEngine(64u);
    ASSERT_TRUE(constrained.hasValue());
    auto execution = an::ExecutionContext::create(*constrained, {});
    ASSERT_TRUE(execution.hasValue());
    const auto file = path();
    auto refused = an::decodeImage(*constrained, *execution, {file});
    EXPECT_EQ(refused.status().code(), an::ErrorCode::MemoryBudgetExceeded);
    EXPECT_EQ(constrained->memoryResource()->counters().liveBytes, 0u);
    std::stop_source stop;
    stop.request_stop();
    an::ExecutionOptions options;
    options.stopToken = stop.get_token();
    auto cancelled = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(cancelled.hasValue());
    EXPECT_EQ(an::decodeImage(engine, *cancelled, {file}).status().code(), an::ErrorCode::Cancelled);
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
}

TEST_F(XisfCodecTest, LiteralUnicodePathsReachTheFileTheyName)
{
    auto source = frame<std::uint8_t>({1, 2, 3}, 3u, 1u);
    constexpr const char* name = "\xc3\xa1-\xe6\x98\x9f[1].xisf";
    ASSERT_TRUE(save(source, nullptr, {}, nullptr, name).hasValue()) << lastError;
    auto decoded = decode(name);
    ASSERT_TRUE(decoded.hasValue()) << lastError;
    EXPECT_EQ(values<std::uint8_t>(decoded->frame), (std::vector<std::uint8_t>{1, 2, 3}));
}

TEST_F(XisfCodecTest, OneImmutableAdapterSupportsConcurrentContextsAndIndependentHandles)
{
    auto source = frame<float>(std::vector<float>(4096u, 0.125f), 64u, 64u);
    ASSERT_TRUE(save(source).hasValue()) << lastError;
    auto adapter = an::createXisfCodec();
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
    const auto file = path();
    const auto expected = values<float>(source);
    std::array<bool, 2> ok{};
    std::thread first([&] {
        auto execution = an::ExecutionContext::create(*shared, {});
        auto result = an::decodeImage(*shared, *execution, {file});
        ok[0] = result.hasValue() && values<float>(result->frame) == expected;
    });
    std::thread second([&] {
        auto execution = an::ExecutionContext::create(*other, {});
        auto result = an::decodeImage(*other, *execution, {file});
        ok[1] = result.hasValue() && values<float>(result->frame) == expected;
    });
    first.join();
    second.join();
    EXPECT_TRUE(ok[0]);
    EXPECT_TRUE(ok[1]);
}

TEST_F(XisfCodecTest, EveryPropertyTypeIsWrittenAsItsTypeAndReadBackExactly)
{
    // A card states its property's type in its comment, so a unit read and written again states each property as
    // its writer did. The text read back is the reader's rendering of the value the file holds, so text that comes
    // back as it went means the value went through as itself: an integer at its width, every number exact, a
    // matrix at its dimensions. A Float128 element is carried as its nearest double.
    struct Typed
    {
        const char* keyword;
        const char* text;
        const char* comment;
    };
    const std::vector<Typed> typed = {
        {"Test:Boolean", "false", "Boolean"},
        {"Test:Int8", "-128", "Int8"},
        {"Test:UInt8", "255", "UInt8"},
        {"Test:Int16", "-32768", "Int16"},
        {"Test:UInt16", "65535", "UInt16"},
        {"Test:Int32", "-2147483648", "Int32"},
        {"Test:UInt32", "4294967295", "UInt32"},
        {"Test:Int64", "-9223372036854775808", "Int64"},
        {"Test:UInt64", "18446744073709551615", "UInt64"},
        {"Test:Int128", "-170141183460469231731687303715884105728", "Int128"},
        {"Test:UInt128", "340282366920938463463374607431768211455", "UInt128"},
        {"Test:Float32", "0.1", "Float32"},
        {"Test:Float64", "-1.7976931348623157e+308", "Float64"},
        {"Test:Float128", "1.25", "Float128"},
        {"Test:Complex32", "(1.5,-2)", "Complex32"},
        {"Test:Complex64", "(0.1,1e-300)", "Complex64"},
        {"Test:Complex128", "(1.5,-2)", "Complex128"},
        {"Test:String", "a,b;c", "String; with a comment of its own"},
        {"Test:TimePoint", "2024-02-29T23:59:60.000000001Z", "TimePoint"},
        {"Test:I8Vector", "-128,-1,0,127", "I8Vector"},
        {"Test:UI8Vector", "0,255", "UI8Vector"},
        {"Test:I16Vector", "-32768,32767", "I16Vector"},
        {"Test:UI16Vector", "0,65535", "UI16Vector"},
        {"Test:I32Vector", "-2147483648,2147483647", "I32Vector"},
        {"Test:UI32Vector", "0,4294967295", "UI32Vector"},
        {"Test:I64Vector", "-9223372036854775808,9223372036854775807", "I64Vector"},
        {"Test:UI64Vector", "0,18446744073709551615", "UI64Vector"},
        {"Test:I128Vector", "-1,0,170141183460469231731687303715884105727", "I128Vector"},
        {"Test:UI128Vector", "0,18446744073709551616", "UI128Vector"},
        {"Test:F32Vector", "-3.4028235e+38,1e-45,0.1", "F32Vector"},
        {"Test:F64Vector", "5e-324,-0,0.1", "F64Vector"},
        {"Test:F128Vector", "0.5,-0,1e-310,inf", "F128Vector"},
        {"Test:C32Vector", "(1,2),(-3,4)", "C32Vector"},
        {"Test:C64Vector", "(1,2),(-3,4)", "C64Vector"},
        {"Test:C128Vector", "(1,2),(-3,0.5)", "C128Vector"},
        {"Test:EmptyVector", "", "I32Vector"},
        {"Test:I8Matrix", "1,2,3;4,5,6", "I8Matrix 2x3"},
        {"Test:UI8Matrix", "1;2", "UI8Matrix 2x1"},
        {"Test:I16Matrix", "-1,1", "I16Matrix 1x2"},
        {"Test:UI16Matrix", "1,2;3,4", "UI16Matrix 2x2"},
        {"Test:I32Matrix", "7", "I32Matrix 1x1"},
        {"Test:UI32Matrix", "1,2;3,4", "UI32Matrix 2x2"},
        {"Test:I64Matrix", "1,2;3,4", "I64Matrix 2x2"},
        {"Test:UI64Matrix", "1,2;3,4", "UI64Matrix 2x2"},
        {"Test:I128Matrix", "1,-2;3,4", "I128Matrix 2x2"},
        {"Test:UI128Matrix", "1,2;3,4", "UI128Matrix 2x2"},
        {"Test:F32Matrix", "0.5,1;1.5,2", "F32Matrix 2x2"},
        {"Test:F64Matrix", "0,1,2,3;10,11,12,13;20,21,22,23", "F64Matrix 3x4"},
        {"Test:F128Matrix", "0.5,1;1.5,2", "F128Matrix 2x2"},
        {"Test:C32Matrix", "(1,2);(3,4)", "C32Matrix 2x1"},
        {"Test:C64Matrix", "(1,2),(3,4)", "C64Matrix 1x2"},
        {"Test:C128Matrix", "(1,2),(3,4);(5,6),(7,8)", "C128Matrix 2x2"},
        {"Test:EmptyMatrix", "", "F64Matrix 0x0"},
    };
    std::vector<an::CodecCard> cards;
    for (const Typed& property : typed)
        cards.push_back(card(an::CodecNamespace::Xisf, property.keyword, property.text, property.comment));
    auto intent = an::OutputIntent::create({.sampleType = an::SampleType::UInt16, .cards = cards});
    ASSERT_TRUE(intent.hasValue()) << intent.status().message();
    ASSERT_TRUE(save(frame<std::uint16_t>({1, 2}, 2u, 1u), &*intent).hasValue()) << lastError;
    auto decoded = decode();
    ASSERT_TRUE(decoded.hasValue()) << lastError;
    for (const Typed& property : typed)
    {
        const auto* const read = decoded->metadata.codec().find(an::CodecNamespace::Xisf, property.keyword);
        ASSERT_NE(read, nullptr) << property.keyword;
        EXPECT_EQ(read->value(), property.text) << property.keyword;
        EXPECT_EQ(read->comment(), property.comment) << property.keyword;
    }
}

TEST_F(XisfCodecTest, ACardThatIsNotAValueOfItsTypeIsWrittenAsAString)
{
    // Text that is not a value of the type its comment names, and a comment that names no type, are written as a
    // String of the text rather than refused or approximated; a comment of the card's own stays with it.
    struct Stated
    {
        const char* keyword;
        const char* text;
        const char* comment;
        const char* readComment;
    };
    const std::vector<Stated> stated = {
        {"Test:NotAVector", "1,2,x", "F64Vector", "String"},
        {"Test:TrailingSeparator", "1,2,", "I32Vector", "String"},
        {"Test:TooWide", "200", "Int8", "String"},
        {"Test:ShortMatrix", "1,2;3", "F64Matrix 2x2", "String"},
        {"Test:RowsMisplaced", "1;2,3,4", "F64Matrix 2x2", "String"},
        {"Test:NoDimensions", "1,2", "F64Matrix", "String; F64Matrix"},
        {"Test:NoSuchDay", "2026-02-30T00:00:00Z", "TimePoint", "String"},
        {"Test:NotATime", "yesterday", "TimePoint; when", "String; when"},
        {"Test:Free", "x", "engine notes", "String; engine notes"},
        {"Test:Kept", "5", "Int32; five of them", "Int32; five of them"},
    };
    std::vector<an::CodecCard> cards;
    for (const Stated& property : stated)
        cards.push_back(card(an::CodecNamespace::Xisf, property.keyword, property.text, property.comment));
    auto intent = an::OutputIntent::create({.sampleType = an::SampleType::UInt16, .cards = cards});
    ASSERT_TRUE(intent.hasValue());
    ASSERT_TRUE(save(frame<std::uint16_t>({1}, 1u, 1u), &*intent).hasValue()) << lastError;
    auto decoded = decode();
    ASSERT_TRUE(decoded.hasValue()) << lastError;
    for (const Stated& property : stated)
    {
        const auto* const read = decoded->metadata.codec().find(an::CodecNamespace::Xisf, property.keyword);
        ASSERT_NE(read, nullptr) << property.keyword;
        EXPECT_EQ(read->value(), property.text) << property.keyword;
        EXPECT_EQ(read->comment(), property.readComment) << property.keyword;
    }
}

TEST_F(XisfCodecTest, APropertyLongerThanACardIsWrittenWholeAndReadInCardsAgain)
{
    // A processing history of tens of kilobytes, and a text of two-byte characters that a card boundary falls in the
    // middle of, each stated as the shared continuation rule states them: the first card with the type, each further
    // card continuing it.
    std::string history;
    for (int step = 0; history.size() < 25000u; ++step)
        history += "step " + std::to_string(step) + " calibrated, ";
    std::string accents;
    for (int letter = 0; letter < 5001; ++letter)
        accents += "\xC3\xA9";
    accents.insert(0u, "x");
    const auto continued = [](const char* keyword, const std::string& text) {
        std::vector<an::CodecCard> cards;
        for (std::size_t at = 0u; at < text.size();)
        {
            std::size_t length = (std::min)(an::kMaxCodecCardTextBytes, text.size() - at);
            while (at + length < text.size() && (static_cast<unsigned char>(text[at + length]) & 0xC0u) == 0x80u)
                --length;
            cards.push_back(
                card(an::CodecNamespace::Xisf, keyword, text.substr(at, length), at == 0u ? "String" : "continued"));
            at += length;
        }
        return cards;
    };
    auto cards = continued("PixInsight:ProcessingHistory", history);
    const auto accented = continued("Test:Accents", accents);
    cards.insert(cards.end(), accented.begin(), accented.end());
    auto intent = an::OutputIntent::create({.sampleType = an::SampleType::UInt16, .cards = cards});
    ASSERT_TRUE(intent.hasValue());
    ASSERT_TRUE(save(frame<std::uint16_t>({1}, 1u, 1u), &*intent).hasValue()) << lastError;
    // The file holds each as one property.
    const auto bytes = readBytes(path());
    const std::string file(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    EXPECT_NE(file.find(history), std::string::npos);
    EXPECT_NE(file.find(accents), std::string::npos);
    auto decoded = decode();
    ASSERT_TRUE(decoded.hasValue()) << lastError;
    for (const auto& [keyword, text] :
         {std::pair{"PixInsight:ProcessingHistory", &history}, {"Test:Accents", &accents}})
    {
        SCOPED_TRACE(keyword);
        std::string joined;
        std::size_t pieces = 0u;
        for (const an::CodecCard& read : decoded->metadata.codec().cards())
        {
            if (read.origin() != an::CodecNamespace::Xisf || read.keyword() != keyword)
                continue;
            EXPECT_EQ(read.comment(), pieces == 0u ? "String" : "continued");
            EXPECT_LE(read.value().size(), an::kMaxCodecCardTextBytes);
            // A card never starts in the middle of a character.
            EXPECT_FALSE(!read.value().empty() && (static_cast<unsigned char>(read.value()[0]) & 0xC0u) == 0x80u);
            joined += read.value();
            ++pieces;
        }
        EXPECT_EQ(joined, *text);
        EXPECT_GT(pieces, 1u);
    }
}

TEST_F(XisfCodecTest, TranslatedWorldCoordinatesAreLeftToTheSolutionTheyCameFrom)
{
    // A linear solution in XISF's AstrometricSolution namespace, as PixInsight states one.
    const std::vector<an::CodecCard> solution = {
        card(an::CodecNamespace::Xisf, "AstrometricSolution:ProjectionSystem", "Gnomonic", "String"),
        card(an::CodecNamespace::Xisf, "AstrometricSolution:ReferenceCelestialCoordinates", "83.82208,-5.39111",
             "F64Vector"),
        card(an::CodecNamespace::Xisf, "AstrometricSolution:ReferenceImageCoordinates", "1,0.5", "F64Vector"),
        card(an::CodecNamespace::Xisf, "AstrometricSolution:LinearTransformationMatrix", "-0.01,0.002;-0.002,-0.01",
             "F64Matrix 2x2"),
        card(an::CodecNamespace::Xisf, "AstrometricSolution:CelestialPoleNativeCoordinates", "180,90", "F64Vector"),
        card(an::CodecNamespace::Xisf, "AstrometricSolution:Version", "1.0", "String")};
    auto stated = an::OutputIntent::create({.sampleType = an::SampleType::UInt16, .cards = solution});
    ASSERT_TRUE(stated.hasValue());
    const auto source = frame<std::uint16_t>({1, 2}, 2u, 1u);
    ASSERT_TRUE(save(source, &*stated).hasValue()) << lastError;
    const auto header = [&](const char* name) {
        const auto bytes = readBytes(path(name));
        std::uint32_t length = 0u;
        std::memcpy(&length, bytes.data() + 8u, sizeof(length));
        return std::string(reinterpret_cast<const char*>(bytes.data()) + 16u, length);
    };
    EXPECT_EQ(header("frame.xisf").find("CRVAL1"), std::string::npos);
    auto solved = decode();
    ASSERT_TRUE(solved.hasValue()) << lastError;
    ASSERT_TRUE(solved->metadata.astrometry().present());
    EXPECT_EQ(solved->metadata.astrometry().hasDistortion(), false);
    const auto* const crval = solved->metadata.codec().find(an::CodecNamespace::Fits, "CRVAL1");
    ASSERT_NE(crval, nullptr);
    EXPECT_EQ(crval->comment(), "XISF AstrometricSolution");
    // Written again with its cards, the unit states its solution as it did, once: in its properties.
    auto preserving = an::OutputIntent::create({.sampleType = an::SampleType::UInt16});
    ASSERT_TRUE(preserving.hasValue());
    ASSERT_TRUE(save(solved->frame, &*preserving, solved->metadata, nullptr, "again.xisf").hasValue()) << lastError;
    EXPECT_EQ(header("again.xisf").find("CRVAL1"), std::string::npos);
    EXPECT_NE(header("again.xisf").find("AstrometricSolution:LinearTransformationMatrix"), std::string::npos);
    auto again = decode("again.xisf");
    ASSERT_TRUE(again.hasValue()) << lastError;
    ASSERT_TRUE(again->metadata.astrometry().present());
    EXPECT_EQ(*again->metadata.astrometry().referencePoint(), *solved->metadata.astrometry().referencePoint());
    // The translated cards on their own, the properties they came from gone, are the image's solution and are
    // written as keywords.
    std::vector<an::CodecCard> keywords;
    for (const an::CodecCard& entry : solved->metadata.codec().cards())
        if (entry.origin() == an::CodecNamespace::Fits && an::isWcsKeyword(entry.keyword()))
            keywords.push_back(entry);
    ASSERT_FALSE(keywords.empty());
    auto alone = an::OutputIntent::create(
        {.sampleType = an::SampleType::UInt16, .preserveSourceCards = false, .cards = keywords});
    ASSERT_TRUE(alone.hasValue()) << alone.status().message();
    ASSERT_TRUE(save(source, &*alone, {}, nullptr, "keywords.xisf").hasValue()) << lastError;
    EXPECT_NE(header("keywords.xisf").find("CRVAL1"), std::string::npos);
    auto fromKeywords = decode("keywords.xisf");
    ASSERT_TRUE(fromKeywords.hasValue()) << lastError;
    ASSERT_TRUE(fromKeywords->metadata.astrometry().present());
    EXPECT_EQ(fromKeywords->metadata.codec().find(an::CodecNamespace::Xisf, "AstrometricSolution:Version"), nullptr);
}

TEST_F(XisfCodecTest, MetadataTheImagesCardsCannotHoldIsRefusedByNameAndThePixelsStillRead)
{
    const auto image = [&](const std::string& children) {
        return FixtureImage{"geometry=\"2:1:1\" sampleFormat=\"UInt16\" colorSpace=\"Gray\"", children,
                            payloadOf<std::uint16_t>({1, 2})};
    };
    const auto refusedBy = [&](const char* name) -> std::string {
        {
            auto execution = an::ExecutionContext::create(engine, {});
            if (!execution.hasValue())
                return "no execution";
            auto pixels = an::decodeImage(engine, *execution, {.path = path(name), .readMetadata = false});
            if (!pixels.hasValue())
                return "the pixels were refused: " + std::string(pixels.status().rootCause().message());
        }
        auto withMetadata = decode(name);
        if (withMetadata.hasValue())
            return "nothing refused";
        if (withMetadata.status().rootCause().code() != an::ErrorCode::LimitExceeded)
            return "refused for " + std::string(withMetadata.status().rootCause().message());
        const auto* const named = withMetadata.status().rootCause().findContext(an::ContextKey::Parameter);
        return named == nullptr ? "refused naming nothing" : named->text;
    };
    // A vector of zeros whose text could not fit every card an image has, refused before its text is built.
    constexpr std::size_t kElements = 16'800'000u;
    std::string encoded;
    encoded.reserve(kElements / 3u * 4u);
    for (std::size_t at = 0u; at < kElements; at += 3u)
        encoded += "AAAA";
    writeContainer({image("<Property id=\"Test:Huge\" type=\"UI8Vector\" length=\"" + std::to_string(kElements) +
                          "\" location=\"inline:base64\">" + encoded + "</Property>")},
                   "vector.xisf");
    EXPECT_EQ(refusedBy("vector.xisf"), "Test:Huge");
    // A text that the cards left after six thousand properties cannot hold, refused when they run out.
    std::string properties;
    for (int index = 0; index < 6000; ++index)
        properties += "<Property id=\"Test:P" + std::to_string(index) + "\" type=\"Int32\" value=\"1\"/>";
    properties += "<Property id=\"Test:Long\" type=\"String\">" + std::string(9u << 20u, 'a') + "</Property>";
    writeContainer({image(properties)}, "cards.xisf");
    EXPECT_EQ(refusedBy("cards.xisf"), "Test:Long");
}

TEST_F(XisfCodecTest, CapabilitiesDescribeExactlyWhatThisAdapterDoes)
{
    auto adapter = an::createXisfCodec();
    ASSERT_TRUE(adapter.hasValue());
    const auto capabilities = (*adapter)->capabilities();
    EXPECT_TRUE(an::validCodecCapabilities(capabilities));
    EXPECT_EQ(capabilities.format, an::CodecFormat::Xisf);
    EXPECT_TRUE(capabilities.regionDecode);
    // An adapter that leaves checksums unverified describes the same capabilities: the option changes what a read
    // verifies, not what it can read.
    auto unverified = an::createXisfCodec(an::XisfCodecOptions{.verifyChecksums = false});
    ASSERT_TRUE(unverified.hasValue());
    const auto same = (*unverified)->capabilities();
    EXPECT_EQ(same.format, capabilities.format);
    EXPECT_EQ(same.decodeSamples, capabilities.decodeSamples);
    EXPECT_EQ(same.encodeSamples, capabilities.encodeSamples);
    EXPECT_EQ(same.decodeLayouts, capabilities.decodeLayouts);
    EXPECT_EQ(same.encodeLayouts, capabilities.encodeLayouts);
    EXPECT_EQ(same.maxChannels, capabilities.maxChannels);
    EXPECT_EQ(same.regionDecode, capabilities.regionDecode);
    EXPECT_EQ(capabilities.decodeLayouts, an::codecLayoutBit(an::StorageArrangement::Planar) |
                                              an::codecLayoutBit(an::StorageArrangement::Interleaved));
    EXPECT_EQ(capabilities.decodeSamples, capabilities.encodeSamples);
    for (const auto format : {an::RawSampleFormat{1u, an::RawSampleClass::UnsignedInteger},
                              an::RawSampleFormat{2u, an::RawSampleClass::UnsignedInteger},
                              an::RawSampleFormat{4u, an::RawSampleClass::UnsignedInteger},
                              an::RawSampleFormat{8u, an::RawSampleClass::UnsignedInteger},
                              an::RawSampleFormat{4u, an::RawSampleClass::FloatingPoint},
                              an::RawSampleFormat{8u, an::RawSampleClass::FloatingPoint}})
        EXPECT_NE(capabilities.decodeSamples & an::codecSampleBit(format), 0u);
    for (const auto format :
         {an::RawSampleFormat{1u, an::RawSampleClass::SignedInteger},
          an::RawSampleFormat{2u, an::RawSampleClass::SignedInteger},
          an::RawSampleFormat{4u, an::RawSampleClass::SignedInteger},
          an::RawSampleFormat{8u, an::RawSampleClass::SignedInteger},
          an::RawSampleFormat{8u, an::RawSampleClass::Opaque}, an::RawSampleFormat{16u, an::RawSampleClass::Opaque}})
        EXPECT_EQ(capabilities.decodeSamples & an::codecSampleBit(format), 0u);
}

/// A band of a frame's rows, described where they lie in the frame: the frame's strides over the band's rows, so the
/// band's planes are as far apart as the frame's.
struct FrameBand
{
    an::ImageLayout layout;
    std::span<const std::byte> samples;
};

FrameBand bandOf(const an::EngineContext& engine, const an::RawFrame& source, std::uint32_t firstRow,
                 std::uint32_t rows)
{
    const auto whole = *source.layout();
    const auto extent =
        *an::ImageExtent::create(whole.extent().width(), rows, whole.extent().channelCount(), engine.limits());
    const auto layout =
        an::ImageLayout::createWithStrides({.extent = extent,
                                            .elementSizeBytes = whole.elementSizeBytes(),
                                            .samplesPerPixel = whole.samplesPerPixel(),
                                            .planeCount = whole.planeCount(),
                                            .rowAlignmentBytes = 1u},
                                           whole.rowStrideBytes(), whole.planeStrideBytes(), engine.limits());
    EXPECT_TRUE(layout.has_value());
    return {*layout, source.bytes().subspan(static_cast<std::size_t>(firstRow * whole.rowStrideBytes()))};
}

/// Bands of one to seven rows over a frame's height, in order.
std::vector<std::pair<std::uint32_t, std::uint32_t>> bandsOver(std::uint32_t height)
{
    std::vector<std::pair<std::uint32_t, std::uint32_t>> bands;
    for (std::uint32_t row = 0u; row < height;)
    {
        const std::uint32_t rows = (std::min)(height - row, 1u + row % 7u);
        bands.emplace_back(row, rows);
        row += rows;
    }
    return bands;
}

TEST_F(XisfCodecTest, AReservedImageIsFilledBandByBandFromSeveralThreadsAndPublishedWhole)
{
    // The unit is written before its samples exist, its rows then written in place by four threads in an order of
    // their own; the destination holds nothing until the commit, and then the image, its cards and its identity.
    constexpr std::uint32_t kWidth = 97u;
    constexpr std::uint32_t kHeight = 61u;
    const auto data = gradient<float>(kWidth, kHeight, 3u);
    const auto source = frame(data, kWidth, kHeight, 3u);
    const auto identity = an::ArtifactIdentity::create("master-1", "run-7");
    ASSERT_TRUE(identity.hasValue());
    const auto intent = an::OutputIntent::create(
        {.sampleType = an::SampleType::Float32, .cards = {card(an::CodecNamespace::Fits, "OBJECT", "Carina")}});
    ASSERT_TRUE(intent.hasValue());
    EXPECT_TRUE(an::supportsReservedImageOutput(engine, an::CodecFormat::Xisf));
    EXPECT_FALSE(an::supportsReservedImageOutput(engine, an::CodecFormat::Fits));
    EXPECT_FALSE(an::supportsReservedImageOutput(engine, an::CodecFormat::Unknown));
    const auto destination = path("reserved.xisf");
    {
        auto execution = an::ExecutionContext::create(engine, {});
        ASSERT_TRUE(execution.hasValue());
        auto transaction = an::OutputTransaction::begin(
            engine, *execution, {.destinationPath = destination, .intent = &*intent, .identity = &*identity});
        ASSERT_TRUE(transaction.hasValue()) << transaction.status().message();
        const an::Status reserved = transaction->reserve(*source.descriptor(), an::StorageArrangement::Planar, {});
        ASSERT_TRUE(reserved.ok()) << reserved.message() << " / " << reserved.rootCause().message();
        EXPECT_TRUE(transaction->reserved());
        EXPECT_FALSE(transaction->encoded());
        auto bands = bandsOver(kHeight);
        std::reverse(bands.begin(), bands.end());
        std::atomic<std::size_t> next{0u};
        std::mutex failuresMutex;
        std::vector<std::string> failures;
        std::vector<std::thread> writers;
        for (int worker = 0; worker < 4; ++worker)
            writers.emplace_back([&] {
                for (std::size_t index = next.fetch_add(1u); index < bands.size(); index = next.fetch_add(1u))
                {
                    const auto band = bandOf(engine, source, bands[index].first, bands[index].second);
                    if (const an::Status written =
                            transaction->writeRows(bands[index].first, band.layout, band.samples);
                        written.failed())
                    {
                        const std::lock_guard lock(failuresMutex);
                        failures.emplace_back(written.message());
                    }
                }
            });
        for (auto& writer : writers)
            writer.join();
        EXPECT_TRUE(failures.empty()) << failures.front();
        EXPECT_FALSE(std::filesystem::exists(directory / "reserved.xisf"));
        EXPECT_TRUE(std::filesystem::exists(std::filesystem::path(std::u8string_view(
            reinterpret_cast<const char8_t*>(transaction->pendingPath().data()), transaction->pendingPath().size()))));
        const auto report = transaction->commit();
        ASSERT_TRUE(report.hasValue()) << report.status().message() << " / " << report.status().rootCause().message();
        EXPECT_EQ(report->bytesWritten, std::filesystem::file_size(directory / "reserved.xisf"));
        EXPECT_FALSE(transaction->reserved());
    }
    auto decoded = decode("reserved.xisf");
    ASSERT_TRUE(decoded.hasValue()) << lastError;
    EXPECT_EQ(values<float>(decoded->frame), data);
    EXPECT_EQ(an::storageArrangementOf(*decoded->frame.layout()), an::StorageArrangement::Planar);
    const auto* object = decoded->metadata.codec().find(an::CodecNamespace::Fits, "OBJECT");
    ASSERT_NE(object, nullptr);
    EXPECT_EQ(object->value(), "Carina");
    const auto stamped = an::artifactIdentityOf(decoded->metadata.codec(), an::CodecNamespace::Xisf);
    ASSERT_TRUE(stamped.hasValue());
    ASSERT_TRUE(stamped->has_value());
    EXPECT_EQ(**stamped, *identity);
    // The block a reservation writes is uncompressed, which is what its rows were written into.
    const openxisf::reader oracle(path("reserved.xisf"));
    EXPECT_FALSE(oracle.block(0u).codec.has_value());
    EXPECT_EQ(oracle.read_pixels<float>(0u), data);
    for (const auto& entry : std::filesystem::directory_iterator(directory))
        EXPECT_EQ(entry.path().filename(), "reserved.xisf");
}

TEST_F(XisfCodecTest, AReservedImageIsWrittenInEveryStorageAndRepresentationItCanHold)
{
    // Planar and interleaved, one channel and three, rows padded in memory and not, every width of sample, and a
    // checksum the commit computes over the rows written, which this adapter and OpenXISF's reader then verify.
    struct Form
    {
        const char* name;
        std::uint32_t channels;
        an::StorageArrangement arrangement;
        bool padded;
        an::OutputChecksum checksum;
    };
    const std::vector<Form> forms{
        {"mono", 1u, an::StorageArrangement::Planar, false, an::OutputChecksum::None},
        {"interleaved", 3u, an::StorageArrangement::Interleaved, false, an::OutputChecksum::None},
        {"planar-padded", 3u, an::StorageArrangement::Planar, true, an::OutputChecksum::None},
        {"interleaved-padded-sha256", 3u, an::StorageArrangement::Interleaved, true, an::OutputChecksum::Sha256},
        {"planar-sha512", 3u, an::StorageArrangement::Planar, false, an::OutputChecksum::Sha512}};
    constexpr std::uint32_t kWidth = 41u;
    constexpr std::uint32_t kHeight = 23u;
    const auto check = [&]<typename T>(const Form& form, an::SampleType type) {
        SCOPED_TRACE(testing::Message() << form.name << " " << an::sampleTypeName(type));
        const auto data = gradient<T>(kWidth, kHeight, form.channels);
        // The gradient is held a plane after another; an interleaved frame takes it pixel by pixel.
        const auto stored = form.arrangement == an::StorageArrangement::Planar
                                ? data
                                : regionOf(data, kWidth, kHeight, form.channels, {0u, 0u, kWidth, kHeight},
                                           openxisf::pixel_storage::normal);
        const auto source = frame(stored, kWidth, kHeight, form.channels, {}, form.arrangement, form.padded);
        const auto intent = an::OutputIntent::create({.sampleType = type, .checksum = form.checksum});
        ASSERT_TRUE(intent.hasValue()) << intent.status().message();
        {
            auto execution = an::ExecutionContext::create(engine, {});
            ASSERT_TRUE(execution.hasValue());
            auto transaction = an::OutputTransaction::begin(engine, *execution,
                                                            {.destinationPath = path("form.xisf"), .intent = &*intent});
            ASSERT_TRUE(transaction.hasValue());
            const an::Status reserved = transaction->reserve(*source.descriptor(), form.arrangement, {});
            ASSERT_TRUE(reserved.ok()) << reserved.message() << " / " << reserved.rootCause().message();
            for (const auto& [first, rows] : bandsOver(kHeight))
            {
                const auto band = bandOf(engine, source, first, rows);
                const an::Status written = transaction->writeRows(first, band.layout, band.samples);
                ASSERT_TRUE(written.ok()) << written.message() << " / " << written.rootCause().message();
            }
            const auto report = transaction->commit();
            ASSERT_TRUE(report.hasValue())
                << report.status().message() << " / " << report.status().rootCause().message();
        }
        auto decoded = decode("form.xisf");
        ASSERT_TRUE(decoded.hasValue()) << lastError;
        EXPECT_EQ(values<T>(decoded->frame), stored);
        EXPECT_EQ(an::storageArrangementOf(*decoded->frame.layout()),
                  form.channels == 1u ? an::StorageArrangement::Planar : form.arrangement);
        const openxisf::reader oracle(path("form.xisf"));
        const auto block = oracle.block(0u);
        EXPECT_FALSE(block.codec.has_value());
        EXPECT_EQ(block.checksum.has_value(), form.checksum != an::OutputChecksum::None);
        EXPECT_EQ(oracle.read_pixels<T>(0u), stored);
    };
    for (const Form& form : forms)
    {
        check.template operator()<std::uint8_t>(form, an::SampleType::UInt8);
        check.template operator()<std::uint16_t>(form, an::SampleType::UInt16);
        check.template operator()<std::uint32_t>(form, an::SampleType::UInt32);
        check.template operator()<float>(form, an::SampleType::Float32);
        check.template operator()<double>(form, an::SampleType::Float64);
    }
}

TEST_F(XisfCodecTest, AReservedImageIsPublishedOnlyWhenEveryRowHasBeenWrittenOnce)
{
    constexpr std::uint32_t kWidth = 32u;
    constexpr std::uint32_t kHeight = 20u;
    const auto data = gradient<float>(kWidth, kHeight, 1u);
    const auto source = frame(data, kWidth, kHeight);
    const auto other = frame(std::vector<float>(data.size(), 7.0f), kWidth, kHeight);
    const auto intent = an::OutputIntent::create({.sampleType = an::SampleType::Float32});
    ASSERT_TRUE(intent.hasValue());
    const auto destination = path("whole.xisf");
    {
        // A row never written: the commit refuses, naming it, and nothing is left beside the destination.
        auto execution = an::ExecutionContext::create(engine, {});
        ASSERT_TRUE(execution.hasValue());
        auto transaction =
            an::OutputTransaction::begin(engine, *execution, {.destinationPath = destination, .intent = &*intent});
        ASSERT_TRUE(transaction.hasValue());
        ASSERT_TRUE(transaction->reserve(*source.descriptor(), an::StorageArrangement::Planar, {}).ok());
        const auto top = bandOf(engine, source, 0u, 12u);
        ASSERT_TRUE(transaction->writeRows(0u, top.layout, top.samples).ok());
        const auto bottom = bandOf(engine, source, 13u, kHeight - 13u);
        ASSERT_TRUE(transaction->writeRows(13u, bottom.layout, bottom.samples).ok());
        const auto refused = transaction->commit();
        ASSERT_FALSE(refused.hasValue());
        EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidState);
        const auto* missing = refused.status().findContext(an::ContextKey::Requested);
        ASSERT_NE(missing, nullptr);
        EXPECT_EQ(missing->count, 12u);
        EXPECT_FALSE(transaction->valid());
    }
    EXPECT_TRUE(std::filesystem::is_empty(directory));
    {
        // A band over a row written already is refused, naming that row, and writes nothing: the rest of the image
        // is then written and published with the first band's samples.
        auto execution = an::ExecutionContext::create(engine, {});
        ASSERT_TRUE(execution.hasValue());
        auto transaction =
            an::OutputTransaction::begin(engine, *execution, {.destinationPath = destination, .intent = &*intent});
        ASSERT_TRUE(transaction.hasValue());
        ASSERT_TRUE(transaction->reserve(*source.descriptor(), an::StorageArrangement::Planar, {}).ok());
        const auto top = bandOf(engine, source, 0u, 10u);
        ASSERT_TRUE(transaction->writeRows(0u, top.layout, top.samples).ok());
        const auto overlapping = bandOf(engine, other, 5u, 10u);
        const an::Status twice = transaction->writeRows(5u, overlapping.layout, overlapping.samples);
        EXPECT_EQ(twice.code(), an::ErrorCode::InvalidArgument);
        const auto* first = twice.findContext(an::ContextKey::Requested);
        ASSERT_NE(first, nullptr);
        EXPECT_EQ(first->count, 5u);
        const auto rest = bandOf(engine, source, 10u, kHeight - 10u);
        ASSERT_TRUE(transaction->writeRows(10u, rest.layout, rest.samples).ok());
        ASSERT_TRUE(transaction->commit().hasValue());
    }
    auto decoded = decode("whole.xisf");
    ASSERT_TRUE(decoded.hasValue()) << lastError;
    EXPECT_EQ(values<float>(decoded->frame), data);
}

TEST_F(XisfCodecTest, AReservationRefusesWhatCannotBeFilledInPlaceBeforeAnythingIsWritten)
{
    constexpr std::uint32_t kWidth = 16u;
    constexpr std::uint32_t kHeight = 8u;
    const auto data = gradient<float>(kWidth, kHeight, 3u);
    const auto source = frame(data, kWidth, kHeight, 3u);
    const auto destination = path("refused.xisf");
    const auto reserveWith = [&](const an::OutputIntent* intent, const an::RawFrameDescriptor& descriptor,
                                 an::StorageArrangement arrangement) {
        auto execution = an::ExecutionContext::create(engine, {});
        EXPECT_TRUE(execution.hasValue());
        auto transaction =
            an::OutputTransaction::begin(engine, *execution, {.destinationPath = destination, .intent = intent});
        EXPECT_TRUE(transaction.hasValue());
        const an::Status reserved = transaction->reserve(descriptor, arrangement, {});
        EXPECT_FALSE(transaction->valid());
        EXPECT_TRUE(std::filesystem::is_empty(directory));
        return reserved;
    };
    // A compressed block cannot be filled where it lies.
    const auto zstd =
        an::OutputIntent::create({.sampleType = an::SampleType::Float32, .compression = an::OutputCompression::Zstd});
    ASSERT_TRUE(zstd.hasValue());
    const an::Status compressed = reserveWith(&*zstd, *source.descriptor(), an::StorageArrangement::Planar);
    EXPECT_EQ(compressed.code(), an::ErrorCode::Unsupported);
    EXPECT_EQ(an::codecFailureReason(compressed.rootCause()), an::CodecFailure::UnsupportedRepresentation);
    // An intent of another representation than the image's.
    const auto narrower = an::OutputIntent::create({.sampleType = an::SampleType::UInt16});
    ASSERT_TRUE(narrower.hasValue());
    EXPECT_EQ(reserveWith(&*narrower, *source.descriptor(), an::StorageArrangement::Planar).code(),
              an::ErrorCode::IncompatibleArguments);
    // A bottom-up image, which the container's top row cannot hold, and samples in the foreign byte order.
    const auto plain = an::OutputIntent::create({.sampleType = an::SampleType::Float32});
    ASSERT_TRUE(plain.hasValue());
    const auto bottomUp =
        frame(data, kWidth, kHeight, 3u, {}, an::StorageArrangement::Planar, false, an::CoordinateOrigin::BottomLeft);
    EXPECT_EQ(reserveWith(&*plain, *bottomUp.descriptor(), an::StorageArrangement::Planar).code(),
              an::ErrorCode::Unsupported);
    const auto foreign =
        frame(data, kWidth, kHeight, 3u, {}, an::StorageArrangement::Planar, false, an::CoordinateOrigin::TopLeft,
              std::endian::native == std::endian::little ? an::ByteOrder::Big : an::ByteOrder::Little);
    EXPECT_EQ(reserveWith(nullptr, *foreign.descriptor(), an::StorageArrangement::Planar).code(),
              an::ErrorCode::Unsupported);
    EXPECT_EQ(reserveWith(&*plain, *source.descriptor(), an::StorageArrangement::SemiPlanar).code(),
              an::ErrorCode::Unsupported);

    // On a reserved transaction: no encode, no second reservation, and each band held to the image.
    {
        auto execution = an::ExecutionContext::create(engine, {});
        ASSERT_TRUE(execution.hasValue());
        auto transaction =
            an::OutputTransaction::begin(engine, *execution, {.destinationPath = destination, .intent = &*plain});
        ASSERT_TRUE(transaction.hasValue());
        const auto whole = bandOf(engine, source, 0u, kHeight);
        EXPECT_EQ(transaction->writeRows(0u, whole.layout, whole.samples).code(), an::ErrorCode::InvalidState);
        ASSERT_TRUE(transaction->reserve(*source.descriptor(), an::StorageArrangement::Planar, {}).ok());
        EXPECT_EQ(transaction->reserve(*source.descriptor(), an::StorageArrangement::Planar, {}).code(),
                  an::ErrorCode::InvalidState);
        EXPECT_EQ(transaction->encode(source, {}).code(), an::ErrorCode::InvalidState);
        EXPECT_TRUE(transaction->reserved());
        const auto band = bandOf(engine, source, 2u, 4u);
        EXPECT_EQ(transaction->writeRows(kHeight - 2u, band.layout, band.samples).code(), an::ErrorCode::OutOfRange);
        EXPECT_EQ(transaction->writeRows(0u, band.layout, band.samples.first(band.layout.totalBytes() - 1u)).code(),
                  an::ErrorCode::IncompatibleArguments);
        const auto interleaved =
            frame(regionOf(data, kWidth, kHeight, 3u, {0u, 0u, kWidth, kHeight}, openxisf::pixel_storage::normal),
                  kWidth, kHeight, 3u, {}, an::StorageArrangement::Interleaved);
        const auto pixels = bandOf(engine, interleaved, 0u, 4u);
        EXPECT_EQ(transaction->writeRows(0u, pixels.layout, pixels.samples).code(),
                  an::ErrorCode::IncompatibleArguments);
        const auto narrow = frame(gradient<float>(kWidth - 1u, kHeight, 3u), kWidth - 1u, kHeight, 3u);
        const auto narrowBand = bandOf(engine, narrow, 0u, 4u);
        EXPECT_EQ(transaction->writeRows(0u, narrowBand.layout, narrowBand.samples).code(),
                  an::ErrorCode::IncompatibleArguments);
        // Nothing of the refused bands was claimed: the image is still written whole.
        ASSERT_TRUE(transaction->writeRows(0u, whole.layout, whole.samples).ok());
        ASSERT_TRUE(transaction->commit().hasValue());
    }
    auto decoded = decode("refused.xisf");
    ASSERT_TRUE(decoded.hasValue()) << lastError;
    EXPECT_EQ(values<float>(decoded->frame), data);
}

TEST_F(XisfCodecTest, AStopDuringAReservedImageRemovesItsPayloadAndPublishesNothing)
{
    constexpr std::uint32_t kWidth = 24u;
    constexpr std::uint32_t kHeight = 12u;
    const auto data = gradient<std::uint16_t>(kWidth, kHeight, 1u);
    const auto source = frame(data, kWidth, kHeight);
    const auto intent = an::OutputIntent::create({.sampleType = an::SampleType::UInt16});
    ASSERT_TRUE(intent.hasValue());
    std::stop_source stop;
    an::ExecutionOptions options;
    options.stopToken = stop.get_token();
    auto execution = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(execution.hasValue());
    auto transaction =
        an::OutputTransaction::begin(engine, *execution, {.destinationPath = path("stopped.xisf"), .intent = &*intent});
    ASSERT_TRUE(transaction.hasValue());
    ASSERT_TRUE(transaction->reserve(*source.descriptor(), an::StorageArrangement::Planar, {}).ok());
    const auto top = bandOf(engine, source, 0u, kHeight / 2u);
    ASSERT_TRUE(transaction->writeRows(0u, top.layout, top.samples).ok());
    stop.request_stop();
    const auto bottom = bandOf(engine, source, kHeight / 2u, kHeight / 2u);
    EXPECT_EQ(transaction->writeRows(kHeight / 2u, bottom.layout, bottom.samples).code(), an::ErrorCode::Cancelled);
    EXPECT_EQ(transaction->commit().status().code(), an::ErrorCode::Cancelled);
    EXPECT_FALSE(transaction->valid());
    EXPECT_TRUE(std::filesystem::is_empty(directory));
}
} // namespace
