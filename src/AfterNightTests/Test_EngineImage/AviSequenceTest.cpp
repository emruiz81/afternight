// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/IO/AviSequence.h>

#include <AfterNightEngine/IO/FfmpegSequence.h>
#include <AfterNightEngine/IO/SequenceOutput.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>
#include <AfterNightEngine/Runtime/TraceSink.h>

#include "VideoFixture.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace
{
using Bytes = std::vector<std::byte>;

std::uint64_t readLe(const Bytes& bytes, std::uint64_t offset, std::size_t count)
{
    std::uint64_t value = 0u;
    for (std::size_t i = 0u; i < count; ++i)
        value |= std::uint64_t{std::to_integer<std::uint8_t>(bytes[static_cast<std::size_t>(offset + i)])} << (8u * i);
    return value;
}

std::string fourccAt(const Bytes& bytes, std::uint64_t offset)
{
    return {reinterpret_cast<const char*>(bytes.data() + offset), 4u};
}

/// The byte a test frame holds at a sample: every coordinate and the frame number move it, so an ordering, an
/// exchange or an orientation mistake cannot cancel out, and frames 256 apart still differ.
std::uint8_t sampleValue(std::uint64_t frame, std::uint32_t x, std::uint32_t y, std::uint32_t channel)
{
    return static_cast<std::uint8_t>((x * 7u + y * 13u + frame * 29u + (frame >> 8u) * 101u + channel * 61u + 5u) &
                                     0xffu);
}

struct MasterEntry
{
    std::uint64_t offset = 0u;
    std::uint32_t size = 0u;
    std::uint32_t duration = 0u;
};

struct StandardIndex
{
    std::uint64_t chunk = 0u;
    std::uint32_t chunkBytes = 0u;
    std::vector<std::uint64_t> data;
    std::vector<std::uint32_t> sizes;
};

/// What an independent reading of a container finds. Every size, offset and count is checked against the bytes
/// around it rather than trusted, and the first inconsistency is kept.
struct ParsedAvi
{
    std::string problem;
    std::vector<std::uint64_t> riffBytes;
    std::uint32_t microsecondsPerFrame = 0u;
    std::uint32_t mainFlags = 0u;
    std::uint32_t firstRiffFrames = 0u;
    std::uint32_t streams = 0u;
    std::uint32_t mainWidth = 0u;
    std::uint32_t mainHeight = 0u;
    std::string streamType;
    std::string handler;
    std::uint32_t scale = 0u;
    std::uint32_t rate = 0u;
    std::uint32_t length = 0u;
    std::array<std::int16_t, 4u> frameRect{};
    std::int32_t bitmapWidth = 0;
    std::int32_t bitmapHeight = 0;
    std::uint32_t bitCount = 0u;
    std::uint32_t compression = 0u;
    std::uint32_t sizeImage = 0u;
    std::uint32_t colorsUsed = 0u;
    Bytes palette;
    std::uint32_t masterCapacity = 0u;
    std::vector<MasterEntry> masterEntries;
    std::uint32_t extendedTotalFrames = 0u;
    std::vector<std::uint64_t> frameData;
    std::vector<std::uint32_t> framesPerRiff;
    std::vector<StandardIndex> standardIndexes;
    std::vector<std::uint64_t> idx1Chunks;
};

class AviParser
{
public:
    explicit AviParser(const Bytes& file) : m_file(file) {}

    ParsedAvi parse()
    {
        std::uint64_t at = 0u;
        while (at < m_file.size() && m_out.problem.empty())
        {
            if (at + 12u > m_file.size() || fourccAt(m_file, at) != "RIFF")
            {
                note("a RIFF section header was expected at " + std::to_string(at));
                break;
            }
            const std::uint64_t end = at + 8u + readLe(m_file, at + 4u, 4u);
            if (end > m_file.size())
            {
                note("a RIFF section extends past the file");
                break;
            }
            const std::string form = fourccAt(m_file, at + 8u);
            if (form != (m_out.riffBytes.empty() ? "AVI " : "AVIX"))
            {
                note("unexpected RIFF form " + form);
                break;
            }
            m_out.riffBytes.push_back(end - at);
            m_out.framesPerRiff.push_back(0u);
            walk(at + 12u, end, [&](const std::string& id, std::uint64_t data, std::uint32_t size) {
                const bool first = m_out.riffBytes.size() == 1u;
                if (id == "LIST" && size >= 4u && fourccAt(m_file, data) == "hdrl" && first)
                    headerList(data + 4u, data + size);
                else if (id == "LIST" && size >= 4u && fourccAt(m_file, data) == "movi")
                    movieList(data, data + size);
                else if (id == "idx1" && first)
                    idx1Index(data, size);
                else
                    note("unexpected chunk " + id);
            });
            at = end;
        }
        return m_out;
    }

private:
    template <typename VisitT> void walk(std::uint64_t at, std::uint64_t end, VisitT visit)
    {
        while (at < end && m_out.problem.empty())
        {
            if (at + 8u > end)
            {
                note("a chunk header runs past its list");
                return;
            }
            const std::string id = fourccAt(m_file, at);
            const auto size = static_cast<std::uint32_t>(readLe(m_file, at + 4u, 4u));
            const std::uint64_t next = at + 8u + size + (size & 1u);
            if (next > end)
            {
                note(id + " runs past its list");
                return;
            }
            visit(id, at + 8u, size);
            at = next;
        }
        if (at != end && m_out.problem.empty())
            note("a list does not end where its size says");
    }

    void headerList(std::uint64_t at, std::uint64_t end)
    {
        walk(at, end, [&](const std::string& id, std::uint64_t data, std::uint32_t size) {
            if (id == "avih" && size == 56u)
            {
                m_out.microsecondsPerFrame = u32(data);
                m_out.mainFlags = u32(data + 12u);
                m_out.firstRiffFrames = u32(data + 16u);
                m_out.streams = u32(data + 24u);
                m_out.mainWidth = u32(data + 32u);
                m_out.mainHeight = u32(data + 36u);
            }
            else if (id == "LIST" && size >= 4u && fourccAt(m_file, data) == "strl")
                streamList(data + 4u, data + size);
            else if (id == "LIST" && size >= 4u && fourccAt(m_file, data) == "odml")
                walk(data + 4u, data + size,
                     [&](const std::string& inner, std::uint64_t innerData, std::uint32_t innerSize) {
                         if (inner == "dmlh" && innerSize == 248u)
                             m_out.extendedTotalFrames = u32(innerData);
                         else
                             note("unexpected chunk in odml: " + inner);
                     });
            else
                note("unexpected chunk in hdrl: " + id);
        });
    }

    void streamList(std::uint64_t at, std::uint64_t end)
    {
        walk(at, end, [&](const std::string& id, std::uint64_t data, std::uint32_t size) {
            if (id == "strh" && size == 56u)
            {
                m_out.streamType = fourccAt(m_file, data);
                m_out.handler = fourccAt(m_file, data + 4u);
                m_out.scale = u32(data + 20u);
                m_out.rate = u32(data + 24u);
                m_out.length = u32(data + 32u);
                for (std::size_t i = 0u; i < 4u; ++i)
                    m_out.frameRect[i] = static_cast<std::int16_t>(readLe(m_file, data + 48u + 2u * i, 2u));
            }
            else if (id == "strf" && size >= 40u)
            {
                m_out.bitmapWidth = static_cast<std::int32_t>(u32(data + 4u));
                m_out.bitmapHeight = static_cast<std::int32_t>(u32(data + 8u));
                m_out.bitCount = static_cast<std::uint32_t>(readLe(m_file, data + 14u, 2u));
                m_out.compression = u32(data + 16u);
                m_out.sizeImage = u32(data + 20u);
                m_out.colorsUsed = u32(data + 32u);
                m_out.palette.assign(m_file.begin() + static_cast<std::ptrdiff_t>(data + 40u),
                                     m_file.begin() + static_cast<std::ptrdiff_t>(data + size));
            }
            else if (id == "indx" && size >= 24u)
            {
                if (readLe(m_file, data, 2u) != 4u || m_file[static_cast<std::size_t>(data + 3u)] != std::byte{0} ||
                    fourccAt(m_file, data + 8u) != "00db" || (size - 24u) % 16u != 0u)
                    note("unexpected master index header");
                m_out.masterCapacity = (size - 24u) / 16u;
                const std::uint32_t inUse = u32(data + 4u);
                if (inUse > m_out.masterCapacity)
                    note("the master index uses more entries than it holds");
                for (std::uint32_t e = 0u; e < inUse && e < m_out.masterCapacity; ++e)
                    m_out.masterEntries.push_back({readLe(m_file, data + 24u + 16u * e, 8u), u32(data + 32u + 16u * e),
                                                   u32(data + 36u + 16u * e)});
            }
            else
                note("unexpected chunk in strl: " + id);
        });
    }

    void movieList(std::uint64_t movi, std::uint64_t end)
    {
        walk(movi + 4u, end, [&](const std::string& id, std::uint64_t data, std::uint32_t size) {
            if (id == "00db")
            {
                m_out.frameData.push_back(data);
                ++m_out.framesPerRiff.back();
                if (size != m_out.sizeImage)
                    note("a frame chunk's size differs from the bitmap's");
            }
            else if (id == "ix00" && size >= 24u)
            {
                StandardIndex index;
                index.chunk = data - 8u;
                index.chunkBytes = size + 8u;
                const std::uint32_t entries = u32(data + 4u);
                const std::uint64_t base = readLe(m_file, data + 12u, 8u);
                if (readLe(m_file, data, 2u) != 2u || m_file[static_cast<std::size_t>(data + 3u)] != std::byte{1} ||
                    fourccAt(m_file, data + 8u) != "00db" || base != movi || size != 24u + 8u * entries)
                    note("unexpected standard index header");
                for (std::uint32_t e = 0u; e < entries && m_out.problem.empty(); ++e)
                {
                    index.data.push_back(base + u32(data + 24u + 8u * e));
                    index.sizes.push_back(u32(data + 28u + 8u * e));
                }
                m_out.standardIndexes.push_back(std::move(index));
            }
            else
                note("unexpected chunk in movi: " + id);
        });
        if (m_out.riffBytes.size() == 1u)
            m_firstMovi = movi;
    }

    void idx1Index(std::uint64_t data, std::uint32_t size)
    {
        if (size % 16u != 0u)
            note("the idx1 index is not a whole number of entries");
        for (std::uint32_t e = 0u; e < size / 16u; ++e)
        {
            const std::uint64_t entry = data + 16u * e;
            if (fourccAt(m_file, entry) != "00db" || u32(entry + 4u) != 0x10u || u32(entry + 12u) != m_out.sizeImage)
                note("unexpected idx1 index entry");
            m_out.idx1Chunks.push_back(m_firstMovi + u32(entry + 8u));
        }
    }

    [[nodiscard]] std::uint32_t u32(std::uint64_t offset) const
    {
        return static_cast<std::uint32_t>(readLe(m_file, offset, 4u));
    }

    void note(std::string text)
    {
        if (m_out.problem.empty())
            m_out.problem = std::move(text);
    }

    const Bytes& m_file;
    ParsedAvi m_out;
    std::uint64_t m_firstMovi = 0u;
};

/// Every index names exactly the frames the movie lists hold, in order, and the counts agree with them.
std::string checkIndexes(const ParsedAvi& avi)
{
    if (!avi.problem.empty())
        return avi.problem;
    if (avi.masterEntries.size() != avi.standardIndexes.size())
        return "the master index and the standard indexes disagree in number";
    std::size_t frame = 0u;
    for (std::size_t s = 0u; s < avi.standardIndexes.size(); ++s)
    {
        const auto& index = avi.standardIndexes[s];
        const auto& entry = avi.masterEntries[s];
        if (entry.offset != index.chunk || entry.size != index.chunkBytes || entry.duration != index.data.size())
            return "master entry " + std::to_string(s) + " does not name its standard index";
        for (std::size_t i = 0u; i < index.data.size(); ++i, ++frame)
            if (frame >= avi.frameData.size() || index.data[i] != avi.frameData[frame] ||
                index.sizes[i] != avi.sizeImage)
                return "standard index entry " + std::to_string(i) + " of section " + std::to_string(s) +
                       " does not name its frame";
    }
    if (frame != avi.frameData.size())
        return "frames are missing from the standard indexes";
    if (avi.framesPerRiff.empty() || avi.idx1Chunks.size() != avi.framesPerRiff.front())
        return "the idx1 index does not cover exactly the first section";
    for (std::size_t i = 0u; i < avi.idx1Chunks.size(); ++i)
        if (avi.idx1Chunks[i] + 8u != avi.frameData[i])
            return "idx1 index entry " + std::to_string(i) + " does not name its frame";
    if (avi.firstRiffFrames != avi.framesPerRiff.front() || avi.extendedTotalFrames != avi.frameData.size() ||
        avi.length != avi.frameData.size())
        return "the frame counts disagree with the frames";
    return {};
}

std::string parameterOf(const an::Status& status)
{
    for (an::Status current = status; current.failed(); current = current.cause())
        if (const auto* field = current.findContext(an::ContextKey::Parameter))
            return field->text;
    return {};
}

/// Every level of a failure, with the parameter each names, for a message a person reads.
std::string chainText(const an::Status& status)
{
    std::string text;
    for (an::Status current = status; current.failed(); current = current.cause())
    {
        if (!text.empty())
            text += " <- ";
        text += current.message();
        if (const auto* field = current.findContext(an::ContextKey::Parameter))
            text += " [" + field->text + "]";
    }
    return text;
}

an::RawFrameDescriptor describe(std::uint32_t width, std::uint32_t height,
                                an::ChannelSchema schema = an::ChannelSchema::Mono,
                                an::CoordinateOrigin origin = an::CoordinateOrigin::TopLeft, an::CfaDescriptor cfa = {},
                                std::uint32_t sampleBytes = 1u,
                                an::RawSampleClass sampleClass = an::RawSampleClass::UnsignedInteger,
                                double aspect = 1.0)
{
    const std::uint32_t channels = schema == an::ChannelSchema::Rgba ? 4u : schema == an::ChannelSchema::Rgb ? 3u : 1u;
    return *an::RawFrameDescriptor::create(
        {.extent = *an::ImageExtent::create(width, height, channels, an::defaultEngineLimits()),
         .sampleFormat = {sampleBytes, sampleClass},
         .byteOrder = sampleBytes == 1u ? an::ByteOrder::Native : an::ByteOrder::Little,
         .channelSchema = schema,
         .colorSpace = an::ColorSpace::Unspecified,
         .numericDomain = an::NumericDomain::RawSensorCode,
         .cfa = cfa,
         .coordinateOrigin = origin,
         .pixelAspectRatio = aspect});
}

/// One frame's description, layout and bytes. Row and plane padding hold 0xEE, so a container that stores a
/// padding byte it was handed shows it.
struct TestFrame
{
    an::RawFrameDescriptor descriptor;
    an::ImageLayout layout;
    Bytes bytes;
    std::uint64_t index = 0u;
};

TestFrame makeFrame(const an::RawFrameDescriptor& descriptor, std::uint64_t index, bool planar = false,
                    std::uint32_t rowPadding = 0u)
{
    const an::ImageExtent& extent = descriptor.extent();
    const std::uint32_t channels = extent.channelCount();
    const std::uint32_t sampleBytes = descriptor.sampleFormat().sizeBytes;
    const std::uint32_t samplesPerPixel = planar ? 1u : channels;
    const std::uint32_t planes = planar ? channels : 1u;
    const std::uint64_t rowStride = std::uint64_t{extent.width()} * samplesPerPixel * sampleBytes + rowPadding;
    const std::uint64_t planeStride = rowStride * extent.height();
    const auto layout = an::ImageLayout::createWithStrides({extent, sampleBytes, samplesPerPixel, planes, 1u},
                                                           rowStride, planeStride, an::defaultEngineLimits());
    EXPECT_TRUE(layout.has_value());
    TestFrame frame{descriptor, *layout, Bytes(static_cast<std::size_t>(planeStride * planes), std::byte{0xEE}), index};
    if (sampleBytes != 1u)
        return frame;
    for (std::uint32_t c = 0u; c < channels; ++c)
        for (std::uint32_t y = 0u; y < extent.height(); ++y)
            for (std::uint32_t x = 0u; x < extent.width(); ++x)
            {
                const std::uint64_t at =
                    planar ? c * planeStride + y * rowStride + x : y * rowStride + std::uint64_t{x} * channels + c;
                frame.bytes[static_cast<std::size_t>(at)] = std::byte{sampleValue(index, x, y, c)};
            }
    return frame;
}

an::SequenceFrame borrowed(const TestFrame& frame, std::uint64_t position)
{
    auto buffer = an::SampleBuffer::borrowConst(std::span<const std::byte>(frame.bytes.data(), frame.bytes.size()));
    EXPECT_TRUE(buffer.hasValue());
    auto raw = an::RawFrame::create(frame.descriptor, frame.layout, std::move(*buffer));
    EXPECT_TRUE(raw.hasValue()) << raw.status().message();
    return an::SequenceFrame{std::move(*raw), {}, an::SequencePosition{position, std::nullopt, std::nullopt}};
}

an::SequenceDescriptor aviDescriptor(const an::RawFrameDescriptor& frame,
                                     std::optional<std::uint64_t> count = std::nullopt,
                                     an::SequenceRate rate = {25u, 1u})
{
    return {.format = an::SequenceFormat::Avi, .fixedFrame = frame, .frameCount = count, .nominalFrameRate = rate};
}

/// Checks every stored row of every frame against the frame it came from: bottom row first for a frame described
/// from its top, blue before green before red, and zero padding.
void expectStoredFrames(const ParsedAvi& avi, const Bytes& file, const std::vector<TestFrame>& frames)
{
    ASSERT_EQ(avi.frameData.size(), frames.size());
    for (std::size_t k = 0u; k < frames.size(); ++k)
    {
        const an::RawFrameDescriptor& d = frames[k].descriptor;
        const std::uint32_t width = d.extent().width();
        const std::uint32_t height = d.extent().height();
        const bool colour = d.channelSchema() == an::ChannelSchema::Rgb;
        const std::uint32_t bytesPerPixel = colour ? 3u : 1u;
        const std::uint64_t storedRow = (std::uint64_t{width} * bytesPerPixel + 3u) & ~std::uint64_t{3u};
        ASSERT_EQ(avi.sizeImage, storedRow * height);
        for (std::uint32_t stored = 0u; stored < height; ++stored)
        {
            const std::uint32_t y =
                d.coordinateOrigin() == an::CoordinateOrigin::BottomLeft ? stored : height - 1u - stored;
            const std::uint64_t row = avi.frameData[k] + stored * storedRow;
            for (std::uint32_t x = 0u; x < width; ++x)
                for (std::uint32_t b = 0u; b < bytesPerPixel; ++b)
                {
                    const std::uint8_t expected =
                        sampleValue(frames[k].index, x, y, colour ? bytesPerPixel - 1u - b : 0u);
                    const auto actual = std::to_integer<std::uint8_t>(
                        file[static_cast<std::size_t>(row + std::uint64_t{x} * bytesPerPixel + b)]);
                    ASSERT_EQ(actual, expected) << "frame " << k << " stored row " << stored << " x " << x;
                }
            for (std::uint64_t pad = std::uint64_t{width} * bytesPerPixel; pad < storedRow; ++pad)
                ASSERT_EQ(file[static_cast<std::size_t>(row + pad)], std::byte{0})
                    << "frame " << k << " row " << stored;
        }
    }
}

/// Counts an operation's AVI trace events and requests a stop at the first one with a stated name.
class AviTrace final : public an::TraceSink
{
public:
    AviTrace(std::stop_source& stop, std::string_view stopAt) : m_stop(stop), m_stopAt(stopAt) {}

    void onTrace(const an::TraceEvent& event) noexcept override
    {
        if (std::string_view(event.category) != "avi")
            return;
        const std::string_view name(event.name);
        if (name == "transfer-bytes")
            m_transfers.fetch_add(1u);
        if (name == "riff-sections")
            m_sections.fetch_add(1u);
        if (name == m_stopAt)
            m_stop.request_stop();
    }

    [[nodiscard]] std::uint64_t transfers() const
    {
        return m_transfers.load();
    }

    [[nodiscard]] std::uint64_t sections() const
    {
        return m_sections.load();
    }

private:
    std::stop_source& m_stop;
    std::string_view m_stopAt;
    std::atomic<std::uint64_t> m_transfers{0u};
    std::atomic<std::uint64_t> m_sections{0u};
};

/// A reproducible stream of choices: the same seed regenerates exactly the same cases.
class Mutator
{
public:
    explicit Mutator(std::uint64_t seed) noexcept : m_state(seed | 1u) {}

    std::uint64_t below(std::uint64_t bound) noexcept
    {
        m_state ^= m_state << 13u;
        m_state ^= m_state >> 7u;
        m_state ^= m_state << 17u;
        return bound == 0u ? 0u : m_state % bound;
    }

private:
    std::uint64_t m_state;
};

class AviSequenceTest : public testing::Test
{
protected:
    void SetUp() override
    {
        an::test::quietFfmpegLog();
        directory = std::filesystem::temp_directory_path() /
                    ("an-avi-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ASSERT_TRUE(std::filesystem::create_directory(directory));
        engine = std::move(*an::EngineContext::create({.maxConcurrency = 4u}));
        execution = std::move(*an::ExecutionContext::create(engine, {.maxConcurrency = 2u}));
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

    static std::string utf8(const std::filesystem::path& path)
    {
        const auto text = path.u8string();
        return {reinterpret_cast<const char*>(text.data()), text.size()};
    }

    static std::filesystem::path native(std::string_view path)
    {
        return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(path.data()), path.size()));
    }

    std::string pathOf(const std::filesystem::path& name) const
    {
        return utf8(directory / name);
    }

    static Bytes readAll(std::string_view path)
    {
        std::ifstream stream(native(path), std::ios::binary);
        const std::vector<char> text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        Bytes bytes(text.size());
        if (!text.empty())
            std::memcpy(bytes.data(), text.data(), text.size());
        return bytes;
    }

    static void overwrite(std::string_view path, const Bytes& bytes)
    {
        std::ofstream stream(native(path), std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    struct Published
    {
        Bytes file;
        std::string path;
    };

    /// Writes frames through an output and a sink, commits, and returns what was published.
    Published writeContainer(const std::filesystem::path& name, const an::SequenceDescriptor& descriptor,
                             const an::AviSinkRequest& request, const std::vector<TestFrame>& frames)
    {
        const auto destination = pathOf(name);
        auto output = an::SequenceOutput::begin(
            engine, execution, {.destinationPath = destination, .durability = an::OutputDurability::CloseOnly});
        if (!output.hasValue())
        {
            ADD_FAILURE() << "begin: " << output.status().message();
            return {};
        }
        auto sink = an::createAviSink(engine, execution, *output, descriptor, request);
        if (!sink.hasValue())
        {
            ADD_FAILURE() << "createAviSink: " << sink.status().message() << " [" << parameterOf(sink.status()) << "]";
            return {};
        }
        std::uint64_t previousBytes = 0u;
        for (std::size_t index = 0u; index < frames.size(); ++index)
        {
            auto appended = sink->append(engine, execution, borrowed(frames[index], index));
            if (!appended.hasValue())
            {
                ADD_FAILURE() << "append " << index << ": " << appended.status().message();
                return {};
            }
            EXPECT_GE(appended->bytesWritten, previousBytes);
            previousBytes = appended->bytesWritten;
        }
        auto finished = sink->finish(execution);
        if (!finished.hasValue())
        {
            ADD_FAILURE() << "finish: " << finished.status().message();
            return {};
        }
        auto committed = output->commit(execution, *finished);
        if (!committed.hasValue())
        {
            ADD_FAILURE() << "commit: " << committed.status().message();
            return {};
        }
        EXPECT_TRUE(sink->close().ok());
        Published published{readAll(destination), destination};
        EXPECT_EQ(published.file.size(), finished->bytesWritten);
        return published;
    }

    /// Reads a published container back through the FFmpeg source and checks every delivered frame against the
    /// frame it came from, the picture's top row first.
    void expectFfmpegReadsBack(const std::string& path, const std::vector<TestFrame>& frames)
    {
        auto source = an::createFfmpegSource(engine, execution, path, {});
        ASSERT_TRUE(source.hasValue()) << source.status().message();
        for (std::size_t k = 0u; k < frames.size(); ++k)
        {
            auto read = source->read(engine, execution);
            ASSERT_TRUE(read.hasValue()) << chainText(read.status());
            ASSERT_TRUE(read->frame.has_value()) << "frame " << k << " is missing";
            const auto layout = read->frame->frame.layout();
            ASSERT_TRUE(layout.has_value());
            ASSERT_EQ(layout->samplesPerPixel(), 3u);
            const an::RawFrameDescriptor& d = frames[k].descriptor;
            const std::uint32_t width = d.extent().width();
            const std::uint32_t height = d.extent().height();
            const bool colour = d.channelSchema() == an::ChannelSchema::Rgb;
            for (std::uint32_t row = 0u; row < height; ++row)
            {
                const std::uint32_t y =
                    d.coordinateOrigin() == an::CoordinateOrigin::BottomLeft ? height - 1u - row : row;
                const auto delivered = read->frame->frame.row(0u, row);
                ASSERT_TRUE(delivered.has_value());
                for (std::uint32_t x = 0u; x < width; ++x)
                    for (std::uint32_t c = 0u; c < 3u; ++c)
                        ASSERT_EQ(std::to_integer<std::uint8_t>((*delivered)[std::size_t{x} * 3u + c]),
                                  sampleValue(frames[k].index, x, y, colour ? c : 0u))
                            << "frame " << k << " displayed row " << row << " x " << x << " channel " << c;
            }
        }
        auto end = source->read(engine, execution);
        ASSERT_TRUE(end.hasValue()) << end.status().message();
        EXPECT_FALSE(end->frame.has_value());
        EXPECT_TRUE(source->close().ok());
    }

    std::filesystem::path directory;
    an::EngineContext engine;
    an::ExecutionContext execution;
};

TEST_F(AviSequenceTest, WritesAnUprightGreyContainerThatFfmpegReadsBackExactly)
{
    const auto frame = describe(30u, 20u);
    std::vector<TestFrame> frames;
    for (std::uint64_t index = 0u; index < 3u; ++index)
        frames.push_back(makeFrame(frame, index));
    const auto published = writeContainer("grey.avi", aviDescriptor(frame, 3u), {}, frames);
    const ParsedAvi avi = AviParser(published.file).parse();
    ASSERT_EQ(checkIndexes(avi), "");
    EXPECT_EQ(avi.riffBytes.size(), 1u);
    EXPECT_EQ(avi.streamType, "vids");
    EXPECT_EQ(avi.handler, "DIB ");
    EXPECT_EQ(avi.mainWidth, 30u);
    EXPECT_EQ(avi.mainHeight, 20u);
    EXPECT_EQ(avi.streams, 1u);
    EXPECT_EQ(avi.mainFlags & 0x10u, 0x10u);
    // A positive height is a bitmap stored from its bottom row, which every reader supports.
    EXPECT_EQ(avi.bitmapWidth, 30);
    EXPECT_EQ(avi.bitmapHeight, 20);
    EXPECT_EQ(avi.bitCount, 8u);
    EXPECT_EQ(avi.compression, 0u);
    EXPECT_EQ(avi.sizeImage, 32u * 20u);
    EXPECT_EQ(avi.colorsUsed, 256u);
    ASSERT_EQ(avi.palette.size(), 1024u);
    for (std::size_t entry = 0u; entry < 256u; ++entry)
    {
        EXPECT_EQ(std::to_integer<std::size_t>(avi.palette[4u * entry]), entry);
        EXPECT_EQ(std::to_integer<std::size_t>(avi.palette[4u * entry + 1u]), entry);
        EXPECT_EQ(std::to_integer<std::size_t>(avi.palette[4u * entry + 2u]), entry);
        EXPECT_EQ(avi.palette[4u * entry + 3u], std::byte{0});
    }
    EXPECT_EQ(avi.scale, 1u);
    EXPECT_EQ(avi.rate, 25u);
    EXPECT_EQ(avi.microsecondsPerFrame, 40000u);
    EXPECT_EQ(avi.frameRect[2], 30);
    EXPECT_EQ(avi.frameRect[3], 20);
    EXPECT_EQ(avi.masterEntries.size(), 1u);
    expectStoredFrames(avi, published.file, frames);

    auto info = an::inspectFfmpegFile(engine, execution, published.path, {});
    ASSERT_TRUE(info.hasValue()) << info.status().message();
    EXPECT_EQ(info->format, an::SequenceFormat::Avi);
    EXPECT_EQ(info->codecName, "rawvideo");
    EXPECT_EQ(info->frameCount, 3u);
    EXPECT_EQ(info->conversion, an::FfmpegConversion::PaletteExpansion);
    expectFfmpegReadsBack(published.path, frames);
}

TEST_F(AviSequenceTest, BottomLeftFramesAreStoredInTheOrderTheyArrive)
{
    // Sixteen grey samples a row need no padding, so an unpadded frame is written straight from its buffer and a
    // padded one row by row; both store the same bytes.
    for (const std::uint32_t rowPadding : {0u, 5u})
    {
        const auto frame = describe(16u, 6u, an::ChannelSchema::Mono, an::CoordinateOrigin::BottomLeft);
        std::vector<TestFrame> frames{makeFrame(frame, 11u, false, rowPadding),
                                      makeFrame(frame, 12u, false, rowPadding)};
        const auto published = writeContainer("bottom-up.avi", aviDescriptor(frame), {}, frames);
        const ParsedAvi avi = AviParser(published.file).parse();
        ASSERT_EQ(checkIndexes(avi), "");
        expectStoredFrames(avi, published.file, frames);
        expectFfmpegReadsBack(published.path, frames);
    }
}

TEST_F(AviSequenceTest, ColourIsStoredAsBlueGreenRedFromInterleavedAndPlanarRows)
{
    struct Arrangement
    {
        bool planar;
        std::uint32_t rowPadding;
        an::CoordinateOrigin origin;
    };
    for (const auto& arrangement :
         {Arrangement{false, 0u, an::CoordinateOrigin::TopLeft}, Arrangement{false, 3u, an::CoordinateOrigin::TopLeft},
          Arrangement{true, 0u, an::CoordinateOrigin::TopLeft}, Arrangement{true, 2u, an::CoordinateOrigin::BottomLeft},
          Arrangement{false, 1u, an::CoordinateOrigin::BottomLeft}})
    {
        // Thirteen pixels of three samples need one byte of padding each row.
        const auto frame = describe(13u, 5u, an::ChannelSchema::Rgb, arrangement.origin);
        std::vector<TestFrame> frames{makeFrame(frame, 3u, arrangement.planar, arrangement.rowPadding),
                                      makeFrame(frame, 4u, arrangement.planar, arrangement.rowPadding)};
        const auto published = writeContainer("colour.avi", aviDescriptor(frame), {}, frames);
        const ParsedAvi avi = AviParser(published.file).parse();
        ASSERT_EQ(checkIndexes(avi), "");
        EXPECT_EQ(avi.bitCount, 24u);
        EXPECT_EQ(avi.colorsUsed, 0u);
        EXPECT_TRUE(avi.palette.empty());
        expectStoredFrames(avi, published.file, frames);
        auto info = an::inspectFfmpegFile(engine, execution, published.path, {});
        ASSERT_TRUE(info.hasValue()) << info.status().message();
        EXPECT_EQ(info->conversion, an::FfmpegConversion::ChannelReorder);
        expectFfmpegReadsBack(published.path, frames);
    }
}

TEST_F(AviSequenceTest, ARateIsStoredAsItsExactFraction)
{
    const auto frame = describe(8u, 4u);
    const std::vector<TestFrame> frames{makeFrame(frame, 0u)};
    const auto published = writeContainer("ntsc.avi", aviDescriptor(frame, std::nullopt, {30000u, 1001u}), {}, frames);
    const ParsedAvi avi = AviParser(published.file).parse();
    ASSERT_EQ(checkIndexes(avi), "");
    EXPECT_EQ(avi.rate, 30000u);
    EXPECT_EQ(avi.scale, 1001u);
    EXPECT_EQ(avi.microsecondsPerFrame, 33367u);
    auto info = an::inspectFfmpegFile(engine, execution, published.path, {});
    ASSERT_TRUE(info.hasValue()) << info.status().message();
    EXPECT_EQ(std::uint64_t{info->timeBase.numerator} * 30000u, std::uint64_t{info->timeBase.denominator} * 1001u);
}

TEST_F(AviSequenceTest, SectionsCloseWhenFullAndEveryIndexNamesItsFrames)
{
    const auto frame = describe(64u, 64u);
    std::vector<TestFrame> frames;
    for (std::uint64_t index = 0u; index < 600u; ++index)
        frames.push_back(makeFrame(frame, index));
    const auto published =
        writeContainer("sections.avi", aviDescriptor(frame), {.riffBytes = an::kAviMinimumRiffBytes}, frames);
    const ParsedAvi avi = AviParser(published.file).parse();
    ASSERT_EQ(checkIndexes(avi), "");
    ASSERT_GE(avi.riffBytes.size(), 3u);
    for (const std::uint64_t bytes : avi.riffBytes)
        EXPECT_LE(bytes, an::kAviMinimumRiffBytes);
    EXPECT_EQ(avi.masterCapacity, an::kAviDefaultRiffCapacity);
    // With 4096-byte frames, a later section holds what fits beside its own opening and standard index.
    const std::uint64_t later = (an::kAviMinimumRiffBytes - 24u - 32u) / (8u + 4096u + 8u);
    for (std::size_t section = 1u; section + 1u < avi.framesPerRiff.size(); ++section)
        EXPECT_EQ(avi.framesPerRiff[section], later) << "section " << section;
    const std::uint64_t header = avi.frameData.front() - 8u;
    EXPECT_EQ(avi.framesPerRiff.front(), (an::kAviMinimumRiffBytes - header - 40u) / (8u + 4096u + 24u));
    EXPECT_EQ(avi.masterEntries.size(), avi.riffBytes.size());
    expectStoredFrames(avi, published.file, frames);
    expectFfmpegReadsBack(published.path, frames);
}

TEST_F(AviSequenceTest, AStatedFrameCountSizesTheMasterIndexAndIsCheckedFirst)
{
    const auto frame = describe(64u, 64u);
    std::vector<TestFrame> frames;
    for (std::uint64_t index = 0u; index < 600u; ++index)
        frames.push_back(makeFrame(frame, index));
    const auto published =
        writeContainer("counted.avi", aviDescriptor(frame, 600u), {.riffBytes = an::kAviMinimumRiffBytes}, frames);
    const ParsedAvi avi = AviParser(published.file).parse();
    ASSERT_EQ(checkIndexes(avi), "");
    EXPECT_EQ(avi.masterCapacity, avi.riffBytes.size());

    struct Refusal
    {
        std::uint64_t count;
        std::uint32_t capacity;
        an::ErrorCode code;
    };
    for (const auto& refusal :
         {Refusal{600u, 2u, an::ErrorCode::LimitExceeded},
          Refusal{std::uint64_t{(std::numeric_limits<std::uint32_t>::max)()} + 1u, 0u, an::ErrorCode::LimitExceeded},
          Refusal{254u * 70000u, 0u, an::ErrorCode::LimitExceeded}})
    {
        auto output = an::SequenceOutput::begin(engine, execution, {.destinationPath = pathOf("refused.avi")});
        ASSERT_TRUE(output.hasValue()) << output.status().message();
        const Bytes sentinel{std::byte{1}, std::byte{2}, std::byte{3}};
        overwrite(output->pendingPath(), sentinel);
        auto sink = an::createAviSink(engine, execution, *output, aviDescriptor(frame, refusal.count),
                                      {.riffBytes = an::kAviMinimumRiffBytes, .riffCapacity = refusal.capacity});
        ASSERT_FALSE(sink.hasValue());
        EXPECT_EQ(sink.status().code(), refusal.code);
        EXPECT_EQ(parameterOf(sink.status()), "frameCount");
        EXPECT_EQ(readAll(output->pendingPath()), sentinel);
        output->abort();
    }
}

TEST_F(AviSequenceTest, AnAppendPastTheReservedSectionsIsRefused)
{
    const auto frame = describe(64u, 64u);
    const auto destination = pathOf("full.avi");
    auto output = an::SequenceOutput::begin(engine, execution, {.destinationPath = destination});
    ASSERT_TRUE(output.hasValue()) << output.status().message();
    auto sink = an::createAviSink(engine, execution, *output, aviDescriptor(frame),
                                  {.riffBytes = an::kAviMinimumRiffBytes, .riffCapacity = 1u});
    ASSERT_TRUE(sink.hasValue()) << sink.status().message();
    const TestFrame samples = makeFrame(frame, 9u);
    std::uint64_t accepted = 0u;
    an::Status refused;
    for (std::uint64_t index = 0u; index < 1000u; ++index)
    {
        auto appended = sink->append(engine, execution, borrowed(samples, index));
        if (!appended.hasValue())
        {
            refused = appended.status();
            break;
        }
        ++accepted;
    }
    ASSERT_TRUE(refused.failed());
    EXPECT_EQ(refused.code(), an::ErrorCode::LimitExceeded);
    EXPECT_EQ(parameterOf(refused), "frameCount");
    // One section holds what fits beside the header, the standard index and the idx1 index.
    EXPECT_GT(accepted, 200u);
    EXPECT_LT(accepted, 260u);
    EXPECT_EQ(sink->state(), an::SequenceState::Failed);
    EXPECT_TRUE(sink->close().ok());
    EXPECT_EQ(std::filesystem::file_size(native(output->pendingPath())), 0u);
    output->abort();
    EXPECT_FALSE(std::filesystem::exists(native(destination)));
}

TEST_F(AviSequenceTest, RefusesWhatTheContainerCannotStoreBeforeWritingAByte)
{
    const auto mono = describe(16u, 8u);
    struct Case
    {
        const char* name;
        an::SequenceDescriptor descriptor;
        an::AviSinkRequest request;
        an::ErrorCode code;
        const char* parameter;
    };
    auto described = [](an::SequenceDescriptor descriptor, void (*change)(an::SequenceDescriptor&)) {
        change(descriptor);
        return descriptor;
    };
    const std::vector<Case> cases{
        {"not AVI",
         described(aviDescriptor(mono), [](auto& d) { d.format = an::SequenceFormat::Ser; }),
         {},
         an::ErrorCode::InvalidArgument,
         "format"},
        {"no fixed frame",
         described(aviDescriptor(mono), [](auto& d) { d.fixedFrame.reset(); }),
         {},
         an::ErrorCode::InvalidArgument,
         "fixedFrame"},
        {"seek",
         described(aviDescriptor(mono), [](auto& d) { d.seekByIndex = true; }),
         {},
         an::ErrorCode::InvalidArgument,
         "seekByIndex"},
        {"no rate",
         described(aviDescriptor(mono), [](auto& d) { d.nominalFrameRate.reset(); }),
         {},
         an::ErrorCode::InvalidArgument,
         "nominalFrameRate"},
        {"timestamps",
         described(aviDescriptor(mono),
                   [](auto& d) {
                       d.timeBase = an::SequenceRate{1u, 25u};
                       d.timestampsRequired = true;
                   }),
         {},
         an::ErrorCode::Unsupported,
         "timestampsRequired"},
        {"sixteen bits",
         aviDescriptor(describe(16u, 8u, an::ChannelSchema::Mono, an::CoordinateOrigin::TopLeft, {}, 2u)),
         {},
         an::ErrorCode::Unsupported,
         "sampleFormat"},
        {"float",
         aviDescriptor(describe(16u, 8u, an::ChannelSchema::Mono, an::CoordinateOrigin::TopLeft, {}, 4u,
                                an::RawSampleClass::FloatingPoint)),
         {},
         an::ErrorCode::Unsupported,
         "sampleFormat"},
        {"alpha",
         aviDescriptor(describe(16u, 8u, an::ChannelSchema::Rgba)),
         {},
         an::ErrorCode::Unsupported,
         "channelSchema"},
        {"mosaic",
         aviDescriptor(describe(16u, 8u, an::ChannelSchema::Mono, an::CoordinateOrigin::TopLeft,
                                *an::CfaDescriptor::create(an::CfaPattern::Rggb, 0u, 0u))),
         {},
         an::ErrorCode::Unsupported,
         "cfa"},
        {"pixel shape",
         aviDescriptor(describe(16u, 8u, an::ChannelSchema::Mono, an::CoordinateOrigin::TopLeft, {}, 1u,
                                an::RawSampleClass::UnsignedInteger, 2.0)),
         {},
         an::ErrorCode::Unsupported,
         "pixelAspectRatio"},
        {"too wide", aviDescriptor(describe(32768u, 2u)), {}, an::ErrorCode::Unsupported, "extent"},
        {"small sections",
         aviDescriptor(mono),
         {.riffBytes = an::kAviMinimumRiffBytes - 1u},
         an::ErrorCode::InvalidArgument,
         "riffBytes"},
        {"large sections",
         aviDescriptor(mono),
         {.riffBytes = an::kAviDefaultRiffBytes + 1u},
         an::ErrorCode::InvalidArgument,
         "riffBytes"},
        {"capacity",
         aviDescriptor(mono),
         {.riffCapacity = an::kAviMaximumRiffCapacity + 1u},
         an::ErrorCode::InvalidArgument,
         "riffCapacity"},
        {"policy",
         aviDescriptor(mono),
         {.mosaicFrames = static_cast<an::AviMosaicPolicy>(2)},
         an::ErrorCode::InvalidArgument,
         "mosaicFrames"},
        {"frame above a section",
         aviDescriptor(describe(2048u, 1024u, an::ChannelSchema::Rgb)),
         {.riffBytes = an::kAviMinimumRiffBytes},
         an::ErrorCode::LimitExceeded,
         "riffBytes"},
    };
    for (const auto& refusal : cases)
    {
        SCOPED_TRACE(refusal.name);
        auto output = an::SequenceOutput::begin(engine, execution, {.destinationPath = pathOf("refused.avi")});
        ASSERT_TRUE(output.hasValue()) << output.status().message();
        const Bytes sentinel{std::byte{9}, std::byte{8}, std::byte{7}, std::byte{6}};
        overwrite(output->pendingPath(), sentinel);
        const auto before = engine.memoryResource()->counters().totalAllocations;
        auto sink = an::createAviSink(engine, execution, *output, refusal.descriptor, refusal.request);
        ASSERT_FALSE(sink.hasValue());
        EXPECT_EQ(sink.status().code(), refusal.code) << sink.status().message();
        EXPECT_EQ(parameterOf(sink.status()), refusal.parameter);
        EXPECT_EQ(engine.memoryResource()->counters().totalAllocations, before);
        EXPECT_EQ(readAll(output->pendingPath()), sentinel);
        auto claim = output->claimWriter();
        EXPECT_TRUE(claim.hasValue());
        claim->release();
        output->abort();
    }

    auto output = an::SequenceOutput::begin(engine, execution, {.destinationPath = pathOf("contexts.avi")});
    ASSERT_TRUE(output.hasValue());
    auto other = an::EngineContext::create({.maxConcurrency = 1u});
    ASSERT_TRUE(other.hasValue());
    {
        auto otherExecution = an::ExecutionContext::create(*other, {});
        ASSERT_TRUE(otherExecution.hasValue());
        EXPECT_EQ(an::createAviSink(engine, *otherExecution, *output, aviDescriptor(mono), {}).status().code(),
                  an::ErrorCode::IncompatibleArguments);
        auto foreign = an::SequenceOutput::begin(*other, *otherExecution, {.destinationPath = pathOf("foreign.avi")});
        ASSERT_TRUE(foreign.hasValue());
        EXPECT_EQ(an::createAviSink(engine, execution, *foreign, aviDescriptor(mono), {}).status().code(),
                  an::ErrorCode::IncompatibleArguments);
        foreign->abort();
    }
    EXPECT_TRUE(other->shutdown().ok());
    std::stop_source stop;
    stop.request_stop();
    {
        auto stopped = an::ExecutionContext::create(engine, {.stopToken = stop.get_token(), .maxConcurrency = 1u});
        ASSERT_TRUE(stopped.hasValue());
        EXPECT_EQ(an::createAviSink(engine, *stopped, *output, aviDescriptor(mono), {}).status().code(),
                  an::ErrorCode::Cancelled);
    }
    auto claim = output->claimWriter();
    ASSERT_TRUE(claim.hasValue());
    EXPECT_EQ(an::createAviSink(engine, execution, *output, aviDescriptor(mono), {}).status().code(),
              an::ErrorCode::InvalidState);
    claim->release();
    output->abort();
    EXPECT_EQ(an::createAviSink(engine, execution, *output, aviDescriptor(mono), {}).status().code(),
              an::ErrorCode::InvalidState);
}

TEST_F(AviSequenceTest, MosaicFramesAreStoredAsGreyOnlyWhenAskedTo)
{
    const auto frame = describe(8u, 4u, an::ChannelSchema::Mono, an::CoordinateOrigin::TopLeft,
                                *an::CfaDescriptor::create(an::CfaPattern::Grbg, 1u, 0u));
    const std::vector<TestFrame> frames{makeFrame(frame, 5u), makeFrame(frame, 6u)};
    auto output = an::SequenceOutput::begin(engine, execution, {.destinationPath = pathOf("mosaic.avi")});
    ASSERT_TRUE(output.hasValue());
    EXPECT_EQ(parameterOf(an::createAviSink(engine, execution, *output, aviDescriptor(frame), {}).status()), "cfa");
    output->abort();
    const auto published = writeContainer("mosaic.avi", aviDescriptor(frame),
                                          {.mosaicFrames = an::AviMosaicPolicy::WriteSamplesAsMono}, frames);
    const ParsedAvi avi = AviParser(published.file).parse();
    ASSERT_EQ(checkIndexes(avi), "");
    EXPECT_EQ(avi.bitCount, 8u);
    expectStoredFrames(avi, published.file, frames);
    expectFfmpegReadsBack(published.path, frames);
}

TEST_F(AviSequenceTest, CancellationInsideAFrameFailsTheSinkAndLeavesNothing)
{
    // Twenty mebibytes a frame takes two transfers, so a stop requested after the first is noticed before the
    // second whether the rows are staged or written straight from the buffer.
    const auto topDown = describe(5000u, 4000u);
    TestFrame samples = makeFrame(topDown, 1u);
    for (const auto origin : {an::CoordinateOrigin::TopLeft, an::CoordinateOrigin::BottomLeft})
    {
        SCOPED_TRACE(static_cast<int>(origin));
        samples.descriptor = describe(5000u, 4000u, an::ChannelSchema::Mono, origin);
        auto output = an::SequenceOutput::begin(engine, execution, {.destinationPath = pathOf("cancelled.avi")});
        ASSERT_TRUE(output.hasValue());
        auto sink = an::createAviSink(engine, execution, *output, aviDescriptor(samples.descriptor), {});
        ASSERT_TRUE(sink.hasValue()) << sink.status().message();
        std::stop_source stop;
        AviTrace trace(stop, "transfer-bytes");
        {
            auto stopping = an::ExecutionContext::create(
                engine, {.stopToken = stop.get_token(), .traceSink = &trace, .maxConcurrency = 1u});
            ASSERT_TRUE(stopping.hasValue());
            auto appended = sink->append(engine, *stopping, borrowed(samples, 0u));
            ASSERT_FALSE(appended.hasValue());
            EXPECT_EQ(appended.status().code(), an::ErrorCode::Cancelled);
        }
        EXPECT_EQ(trace.transfers(), 1u);
        EXPECT_EQ(sink->state(), an::SequenceState::Failed);
        EXPECT_TRUE(sink->close().ok());
        EXPECT_EQ(std::filesystem::file_size(native(output->pendingPath())), 0u);
        output->abort();
    }
}

TEST_F(AviSequenceTest, IndependentSinksWriteConcurrently)
{
    auto wide = an::EngineContext::create({.maxConcurrency = 8u});
    ASSERT_TRUE(wide.hasValue());
    constexpr std::size_t kSinks = 4u;
    const auto frame = describe(24u, 16u);
    std::array<std::string, kSinks> failures;
    std::array<std::string, kSinks> paths;
    std::vector<std::thread> threads;
    for (std::size_t sinkIndex = 0u; sinkIndex < kSinks; ++sinkIndex)
    {
        paths[sinkIndex] = pathOf("concurrent-" + std::to_string(sinkIndex) + ".avi");
        threads.emplace_back([&, sinkIndex] {
            auto workerExecution = an::ExecutionContext::create(*wide, {.maxConcurrency = 1u});
            if (!workerExecution.hasValue())
            {
                failures[sinkIndex] = "execution";
                return;
            }
            auto output = an::SequenceOutput::begin(
                *wide, *workerExecution,
                {.destinationPath = paths[sinkIndex], .durability = an::OutputDurability::CloseOnly});
            if (!output.hasValue())
            {
                failures[sinkIndex] = "begin";
                return;
            }
            auto sink = an::createAviSink(*wide, *workerExecution, *output, aviDescriptor(frame), {});
            if (!sink.hasValue())
            {
                failures[sinkIndex] = std::string("create: ") + sink.status().message();
                return;
            }
            for (std::uint64_t index = 0u; index < 5u; ++index)
            {
                const TestFrame samples = makeFrame(frame, sinkIndex * 1000u + index);
                if (!sink->append(*wide, *workerExecution, borrowed(samples, index)).hasValue())
                {
                    failures[sinkIndex] = "append";
                    return;
                }
            }
            auto finished = sink->finish(*workerExecution);
            if (!finished.hasValue() || !output->commit(*workerExecution, *finished).hasValue())
                failures[sinkIndex] = "finish";
            (void)sink->close();
        });
    }
    for (auto& thread : threads)
        thread.join();
    for (std::size_t sinkIndex = 0u; sinkIndex < kSinks; ++sinkIndex)
    {
        ASSERT_EQ(failures[sinkIndex], "");
        std::vector<TestFrame> expected;
        for (std::uint64_t index = 0u; index < 5u; ++index)
            expected.push_back(makeFrame(frame, sinkIndex * 1000u + index));
        const Bytes file = readAll(paths[sinkIndex]);
        const ParsedAvi avi = AviParser(file).parse();
        ASSERT_EQ(checkIndexes(avi), "");
        expectStoredFrames(avi, file, expected);
    }
    EXPECT_TRUE(wide->shutdown().ok());
}

TEST_F(AviSequenceTest, UnicodeDestinationsArePublished)
{
    const auto frame = describe(12u, 6u, an::ChannelSchema::Rgb);
    const std::vector<TestFrame> frames{makeFrame(frame, 21u)};
    const auto published = writeContainer(std::filesystem::path(u8"звёзды-星.avi"), aviDescriptor(frame), {}, frames);
    ASSERT_TRUE(std::filesystem::exists(native(published.path)));
    const ParsedAvi avi = AviParser(published.file).parse();
    ASSERT_EQ(checkIndexes(avi), "");
    expectFfmpegReadsBack(published.path, frames);
}

TEST_F(AviSequenceTest, AnUnfinishedSinkDiscardsItsPayloadAndReturnsTheClaim)
{
    const auto frame = describe(20u, 10u);
    const auto destination = pathOf("unfinished.avi");
    auto output = an::SequenceOutput::begin(engine, execution, {.destinationPath = destination});
    ASSERT_TRUE(output.hasValue());
    {
        auto sink = an::createAviSink(engine, execution, *output, aviDescriptor(frame), {});
        ASSERT_TRUE(sink.hasValue()) << sink.status().message();
        const TestFrame samples = makeFrame(frame, 2u);
        ASSERT_TRUE(sink->append(engine, execution, borrowed(samples, 0u)).hasValue());
        ASSERT_TRUE(sink->append(engine, execution, borrowed(samples, 1u)).hasValue());
        EXPECT_GT(std::filesystem::file_size(native(output->pendingPath())), 0u);
        EXPECT_TRUE(sink->close().ok());
        EXPECT_EQ(std::filesystem::file_size(native(output->pendingPath())), 0u);
    }
    auto claim = output->claimWriter();
    ASSERT_TRUE(claim.hasValue()) << claim.status().message();
    claim->release();
    output->abort();
    EXPECT_FALSE(std::filesystem::exists(native(destination)));
}

TEST_F(AviSequenceTest, AWorkspaceTheBudgetCannotHoldIsRefusedBeforeThePayloadIsClaimed)
{
    // A three-mebibyte frame needs a workspace of that size; the header's few kilobytes would fit the budget.
    const auto frame = describe(1024u, 1024u, an::ChannelSchema::Rgb);
    auto options = an::EngineContextOptions{.maxConcurrency = 1u};
    options.limits.memoryBudgetBytes = 1024u * 1024u;
    auto tight = an::EngineContext::create(options);
    ASSERT_TRUE(tight.hasValue());
    {
        auto tightExecution = an::ExecutionContext::create(*tight, {});
        ASSERT_TRUE(tightExecution.hasValue());
        auto output = an::SequenceOutput::begin(*tight, *tightExecution, {.destinationPath = pathOf("budget.avi")});
        ASSERT_TRUE(output.hasValue()) << output.status().message();
        auto sink = an::createAviSink(*tight, *tightExecution, *output, aviDescriptor(frame), {});
        ASSERT_FALSE(sink.hasValue());
        EXPECT_EQ(sink.status().code(), an::ErrorCode::MemoryBudgetExceeded) << sink.status().message();
        EXPECT_EQ(tight->memoryResource()->counters().liveBytes, 0u);
        auto claim = output->claimWriter();
        EXPECT_TRUE(claim.hasValue());
        claim->release();
        output->abort();
    }
    EXPECT_TRUE(tight->shutdown().ok());
}

TEST_F(AviSequenceTest, AContainerWithNoFramesOpens)
{
    const auto frame = describe(10u, 10u);
    const auto published = writeContainer("empty.avi", aviDescriptor(frame, 0u), {}, {});
    const ParsedAvi avi = AviParser(published.file).parse();
    ASSERT_EQ(checkIndexes(avi), "");
    EXPECT_TRUE(avi.frameData.empty());
    EXPECT_TRUE(avi.masterEntries.empty());
    auto info = an::inspectFfmpegFile(engine, execution, published.path, {});
    ASSERT_TRUE(info.hasValue()) << info.status().message();
    EXPECT_EQ(info->frameCount, 0u);
}

TEST_F(AviSequenceTest, SeededGeometriesWriteContainersAParserAndFfmpegAgreeOn)
{
    Mutator random(20260913u);
    for (std::uint32_t caseIndex = 0u; caseIndex < 40u; ++caseIndex)
    {
        const bool colour = random.below(2u) == 1u;
        const bool planar = colour && random.below(2u) == 1u;
        const auto width = static_cast<std::uint32_t>(1u + random.below(40u));
        const auto height = static_cast<std::uint32_t>(1u + random.below(24u));
        const auto rowPadding = static_cast<std::uint32_t>(random.below(4u));
        const auto origin = random.below(2u) == 1u ? an::CoordinateOrigin::BottomLeft : an::CoordinateOrigin::TopLeft;
        const auto count = static_cast<std::uint32_t>(1u + random.below(4u));
        const an::SequenceRate rate{static_cast<std::uint32_t>(1u + random.below(120000u)),
                                    static_cast<std::uint32_t>(1u + random.below(1001u))};
        SCOPED_TRACE("case " + std::to_string(caseIndex) + ": " + std::to_string(width) + "x" + std::to_string(height) +
                     (colour ? (planar ? " planar RGB" : " RGB") : " grey") + " padding " + std::to_string(rowPadding));
        const auto frame = describe(width, height, colour ? an::ChannelSchema::Rgb : an::ChannelSchema::Mono, origin);
        std::vector<TestFrame> frames;
        for (std::uint32_t index = 0u; index < count; ++index)
            frames.push_back(makeFrame(frame, caseIndex * 16u + index, planar, rowPadding));
        const auto published = writeContainer("sweep.avi", aviDescriptor(frame, std::nullopt, rate), {}, frames);
        const ParsedAvi avi = AviParser(published.file).parse();
        ASSERT_EQ(checkIndexes(avi), "");
        EXPECT_EQ(avi.rate, rate.numerator);
        EXPECT_EQ(avi.scale, rate.denominator);
        expectStoredFrames(avi, published.file, frames);
        expectFfmpegReadsBack(published.path, frames);
    }
}
} // namespace
