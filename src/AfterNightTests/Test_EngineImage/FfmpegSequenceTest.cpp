// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/IO/FfmpegSequence.h>

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
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

namespace
{
using an::test::VideoFixtureSpec;

/// Counts an operation's FFmpeg trace counters by name and requests a stop when a named one has
/// arrived a stated number of times: how a case stops an operation part way without racing a thread.
class FfmpegTrace final : public an::TraceSink
{
public:
    FfmpegTrace() = default;
    FfmpegTrace(std::stop_source& stop, std::string_view name, std::uint64_t after = 1u)
        : m_stop(&stop), m_name(name), m_after(after)
    {
    }

    void onTrace(const an::TraceEvent& event) noexcept override
    {
        if (std::string_view(event.category) != "ffmpeg")
            return;
        const std::string_view name(event.name);
        for (std::size_t index = 0u; index < kNames.size(); ++index)
            if (name == kNames[index])
            {
                m_events[index].fetch_add(1u);
                m_totals[index].fetch_add(static_cast<std::uint64_t>(event.value));
            }
        if (m_stop != nullptr && name == m_name && m_seen.fetch_add(1u) + 1u >= m_after)
            m_stop->request_stop();
    }

    [[nodiscard]] std::uint64_t events(std::string_view name) const
    {
        return m_events[slot(name)].load();
    }

    [[nodiscard]] std::uint64_t total(std::string_view name) const
    {
        return m_totals[slot(name)].load();
    }

private:
    static constexpr std::array<std::string_view, 11u> kNames{
        "packets-indexed", "input-bytes",  "packets-sent", "frames-decoded",  "frames-discarded", "repositions",
        "restarts",        "bytes-copied", "bytes-shared", "bytes-converted", "slice-tasks"};

    static std::size_t slot(std::string_view name)
    {
        for (std::size_t index = 0u; index < kNames.size(); ++index)
            if (name == kNames[index])
                return index;
        ADD_FAILURE() << "no ffmpeg trace counter is named " << name;
        return 0u;
    }

    std::stop_source* m_stop = nullptr;
    std::string_view m_name{};
    std::uint64_t m_after = 1u;
    std::atomic<std::uint64_t> m_seen{0u};
    std::array<std::atomic<std::uint64_t>, 11u> m_events{};
    std::array<std::atomic<std::uint64_t>, 11u> m_totals{};
};

bool chainCarries(const an::Status& status, an::SequenceFailure reason)
{
    for (an::Status current = status; current.failed(); current = current.cause())
        if (an::sequenceFailureReason(current) == reason)
            return true;
    return false;
}

bool explained(const an::Status& status)
{
    for (an::Status current = status; current.failed(); current = current.cause())
        if (an::sequenceFailureReason(current))
            return true;
    return status.code() == an::ErrorCode::AllocationFailed || status.code() == an::ErrorCode::MemoryBudgetExceeded;
}

std::string parameterIn(const an::Status& status)
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

std::vector<std::array<int, 4>> identityLevels(int frames)
{
    std::vector<std::array<int, 4>> levels;
    for (int index = 0; index < frames; ++index)
        levels.push_back({16 + 8 * index, 128, 128, 255});
    return levels;
}

/// The frame number a flat identity frame's mean luma names.
int identify(const an::RawFrame& frame)
{
    const auto layout = frame.layout();
    double sum = 0.0;
    std::uint64_t count = 0u;
    for (std::uint32_t y = 0u; y < layout->extent().height(); ++y)
        for (const std::byte sample : frame.row(0u, y)->first(layout->extent().width()))
        {
            sum += std::to_integer<int>(sample);
            ++count;
        }
    return static_cast<int>(std::lround((sum / static_cast<double>(count) - 16.0) / 8.0));
}

/// Checks every row of one delivered plane against the bytes one stored plane was written with.
void expectStoredRows(const an::RawFrame& frame, std::uint32_t deliveredPlane, int storedPlane, int index,
                      std::size_t rowBytes)
{
    const auto layout = frame.layout();
    ASSERT_TRUE(layout);
    for (std::uint32_t y = 0u; y < layout->extent().height(); ++y)
    {
        const auto row = frame.row(deliveredPlane, y);
        ASSERT_TRUE(row);
        ASSERT_GE(row->size(), rowBytes);
        for (std::size_t x = 0u; x < rowBytes; ++x)
            if (std::to_integer<int>((*row)[x]) !=
                an::test::storedByte(storedPlane, static_cast<int>(x), static_cast<int>(y), index))
            {
                ADD_FAILURE() << "frame " << index << " plane " << deliveredPlane << " row " << y << " byte " << x;
                return;
            }
    }
}

/// The R'G'B' a YCbCr triple names under a matrix and a range, each in zero to one.
std::array<double, 3> referenceRgb(double y, double u, double v, double kr, double kb, bool full, int bits)
{
    const double scale = static_cast<double>(1 << (bits - 8));
    const double top = static_cast<double>((1 << bits) - 1);
    const double luma = full ? y / top : (y - 16.0 * scale) / (219.0 * scale);
    const double cb = full ? (u - 128.0 * scale) / top : (u - 128.0 * scale) / (224.0 * scale);
    const double cr = full ? (v - 128.0 * scale) / top : (v - 128.0 * scale) / (224.0 * scale);
    const double r = luma + 2.0 * (1.0 - kr) * cr;
    const double b = luma + 2.0 * (1.0 - kb) * cb;
    const double g = (luma - kr * r - kb * b) / (1.0 - kr - kb);
    return {r, g, b};
}

/// Checks every pixel of an eight- or sixteen-bit RGB frame against a flat reference.
void expectFlatRgb(const an::RawFrame& frame, const std::array<double, 3>& reference, double tolerance)
{
    const auto description = frame.descriptor();
    const auto layout = frame.layout();
    const std::uint32_t bytes = description->sampleFormat().sizeBytes;
    const double top = bytes == 1u ? 255.0 : 65535.0;
    for (std::uint32_t y = 0u; y < layout->extent().height(); ++y)
    {
        const auto row = frame.row(0u, y);
        for (std::uint32_t x = 0u; x < layout->extent().width(); ++x)
            for (std::uint32_t channel = 0u; channel < 3u; ++channel)
            {
                const std::size_t offset = (static_cast<std::size_t>(x) * 3u + channel) * bytes;
                double sample = 0.0;
                if (bytes == 1u)
                    sample = std::to_integer<int>((*row)[offset]);
                else
                {
                    std::uint16_t wide = 0u;
                    std::memcpy(&wide, row->data() + offset, 2u);
                    sample = wide;
                }
                const double expected = std::round(top * std::clamp(reference[channel], 0.0, 1.0));
                if (std::abs(sample - expected) > tolerance)
                {
                    ADD_FAILURE() << "pixel " << x << "," << y << " channel " << channel << " is " << sample
                                  << ", expected " << expected;
                    return;
                }
            }
    }
}

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

/// Where one Matroska cue stores the position of the cluster it points at.
struct CuePosition
{
    std::size_t offset;
    std::size_t width;
    std::uint64_t value;
};

/// Reads one EBML variable-length number: an element identifier keeps its length marker and a size drops it.
std::optional<std::uint64_t> ebmlNumber(std::span<const char> bytes, std::size_t& at, bool keepMarker)
{
    if (at >= bytes.size())
        return std::nullopt;
    const auto first = static_cast<std::uint8_t>(bytes[at]);
    std::size_t length = 1u;
    while (length <= 8u && (first & (0x80u >> (length - 1u))) == 0u)
        ++length;
    if (length > 8u || at + length > bytes.size())
        return std::nullopt;
    std::uint64_t value = keepMarker ? first : first & ((0x80u >> (length - 1u)) - 1u);
    for (std::size_t next = 1u; next < length; ++next)
        value = (value << 8u) | static_cast<std::uint8_t>(bytes[at + next]);
    at += length;
    return value;
}

/// Every CueClusterPosition between two offsets in the order the cues are written, descending only into the
/// segment, its cues, their points and their track positions.
void findCueClusterPositions(std::span<const char> bytes, std::size_t begin, std::size_t end,
                             std::vector<CuePosition>& found)
{
    constexpr std::uint64_t kSegment = 0x18538067u;
    constexpr std::uint64_t kCues = 0x1C53BB6Bu;
    constexpr std::uint64_t kCuePoint = 0xBBu;
    constexpr std::uint64_t kCueTrackPositions = 0xB7u;
    constexpr std::uint64_t kCueClusterPosition = 0xF1u;
    std::size_t at = begin;
    while (at < end)
    {
        const auto id = ebmlNumber(bytes, at, true);
        const std::size_t sizeAt = at;
        const auto size = ebmlNumber(bytes, at, false);
        if (!id || !size)
            return;
        const std::size_t width = at - sizeAt;
        const bool unknown = *size == (std::uint64_t{1} << (7u * width)) - 1u;
        const std::size_t stop =
            unknown ? end : static_cast<std::size_t>((std::min)(static_cast<std::uint64_t>(end), at + *size));
        if (*id == kSegment || *id == kCues || *id == kCuePoint || *id == kCueTrackPositions)
        {
            findCueClusterPositions(bytes, at, stop, found);
        }
        else if (*id == kCueClusterPosition)
        {
            std::uint64_t value = 0u;
            for (std::size_t byte = at; byte < stop; ++byte)
                value = (value << 8u) | static_cast<std::uint8_t>(bytes[byte]);
            found.push_back({at, stop - at, value});
        }
        at = stop;
    }
}

class FfmpegSequenceTest : public testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        an::test::quietFfmpegLog();
    }

    void SetUp() override
    {
        directory = std::filesystem::temp_directory_path() /
                    ("an-ffmpeg-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
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
    }

    static std::string utf8(const std::filesystem::path& path)
    {
        const auto text = path.u8string();
        return {reinterpret_cast<const char*>(text.data()), text.size()};
    }

    std::string video(const std::filesystem::path& name, const VideoFixtureSpec& spec) const
    {
        const auto failure = an::test::writeVideo(directory / name, spec);
        EXPECT_TRUE(failure.empty()) << utf8(name) << ": " << failure;
        return utf8(directory / name);
    }

    std::string text(const std::filesystem::path& name, std::string_view contents) const
    {
        std::ofstream stream(directory / name, std::ios::binary | std::ios::trunc);
        stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        return utf8(directory / name);
    }

    std::vector<an::SequenceFrame> readAll(an::FrameSource& source, const an::ExecutionContext& reader)
    {
        std::vector<an::SequenceFrame> frames;
        for (;;)
        {
            auto read = source.read(engine, reader);
            if (!read.hasValue())
            {
                ADD_FAILURE() << chainText(read.status());
                break;
            }
            if (!read->frame)
                break;
            frames.push_back(std::move(*read->frame));
        }
        return frames;
    }

    std::vector<an::SequenceFrame> readFile(const std::string& path, const an::FfmpegSourceOptions& options = {})
    {
        auto source = an::createFfmpegSource(engine, execution, path, options);
        if (!source.hasValue())
        {
            ADD_FAILURE() << source.status().message();
            return {};
        }
        auto frames = readAll(*source, execution);
        EXPECT_TRUE(source->close().ok());
        return frames;
    }

    static std::vector<std::byte> bytesOf(const an::RawFrame& frame)
    {
        const auto bytes = frame.bytes();
        return {bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(frame.layout()->totalBytes())};
    }

    std::filesystem::path directory;
    an::EngineContext engine;
    an::ExecutionContext execution;
};

VideoFixtureSpec raw(const char* pixelFormat, int frames = 3)
{
    return {
        .muxer = "nut", .encoder = "rawvideo", .pixelFormat = pixelFormat, .width = 16, .height = 12, .frames = frames};
}

VideoFixtureSpec ffv1(const char* pixelFormat, int width, int height, int frames, int gop)
{
    return {.muxer = "matroska",
            .encoder = "ffv1",
            .pixelFormat = pixelFormat,
            .width = width,
            .height = height,
            .frames = frames,
            .gop = gop,
            .encoderOptions = "level=3:slices=4"};
}

VideoFixtureSpec bframes(const char* muxer, const char* encoder, int frames)
{
    return {.muxer = muxer,
            .encoder = encoder,
            .pixelFormat = "yuv420p",
            .width = 64,
            .height = 48,
            .frames = frames,
            .gop = 6,
            .bframes = 2,
            .flatLevels = identityLevels(frames)};
}

TEST_F(FfmpegSequenceTest, AContainerOutsideTheAllowListIsRefused)
{
    const VideoFixtureSpec spec{.muxer = "flv", .encoder = "flv", .pixelFormat = "yuv420p"};
    ASSERT_TRUE(an::test::canWriteVideo(spec));
    const auto path = video("disallowed.mp4", spec);
    auto source =
        an::createFfmpegSource(engine, execution, path, {.sampleDelivery = an::FfmpegSampleDelivery::LumaPlane});
    EXPECT_FALSE(source.hasValue());
}

TEST_F(FfmpegSequenceTest, ADecoderOutsideTheAllowListIsRefusedInsideAnAllowedContainer)
{
    const VideoFixtureSpec spec{.muxer = "avi", .encoder = "flv", .pixelFormat = "yuv420p"};
    ASSERT_TRUE(an::test::canWriteVideo(spec));
    const auto path = video("disallowed.nut", spec);
    auto source =
        an::createFfmpegSource(engine, execution, path, {.sampleDelivery = an::FfmpegSampleDelivery::LumaPlane});
    EXPECT_FALSE(source.hasValue());
}

TEST_F(FfmpegSequenceTest, InspectionDescribesTheStreamAndTheSourceReadsItExactly)
{
    const auto path = video(std::filesystem::path(u8"vÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â­deo ÃƒÆ’Ã‚Â¢Ãƒâ€¹Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â .mkv"),
                            ffv1("gray16le", 32, 16, 5, 2));
    auto info = an::inspectFfmpegFile(engine, execution, path, {});
    ASSERT_TRUE(info.hasValue()) << info.status().message();
    EXPECT_EQ(info->format, an::SequenceFormat::Video);
    EXPECT_EQ(info->containerName, "matroska,webm");
    EXPECT_EQ(info->codecName, "ffv1");
    EXPECT_EQ(info->storedPixelFormat, "gray16le");
    EXPECT_EQ(info->streamIndex, 0u);
    EXPECT_EQ(info->storedBitsPerComponent, 16u);
    EXPECT_EQ(info->conversion, an::FfmpegConversion::None);
    EXPECT_EQ(info->frameCount, 5u);
    EXPECT_EQ(info->keyframeCount, 3u);
    EXPECT_EQ(info->hiddenPacketCount, 0u);
    EXPECT_FALSE(info->reordered);
    EXPECT_TRUE(info->slicesOnOperationThreads);
    EXPECT_EQ(info->timeBase.numerator, 1u);
    EXPECT_EQ(info->timeBase.denominator, 1000u);
    const an::RawFrameDescriptor& frame = info->frame;
    EXPECT_EQ(frame.extent().width(), 32u);
    EXPECT_EQ(frame.extent().height(), 16u);
    EXPECT_EQ(frame.extent().channelCount(), 1u);
    EXPECT_EQ(frame.sampleFormat().sizeBytes, 2u);
    EXPECT_EQ(frame.sampleFormat().sampleClass, an::RawSampleClass::UnsignedInteger);
    EXPECT_EQ(frame.byteOrder(), an::ByteOrder::Little);
    EXPECT_EQ(frame.channelSchema(), an::ChannelSchema::Mono);
    EXPECT_EQ(frame.colorSpace(), an::ColorSpace::Unspecified);
    EXPECT_EQ(frame.numericDomain(), an::NumericDomain::RawSensorCode);
    EXPECT_FALSE(frame.isMosaiced());
    EXPECT_EQ(frame.coordinateOrigin(), an::CoordinateOrigin::TopLeft);
    EXPECT_EQ(frame.pixelAspectRatio(), 1.0);

    auto source = an::createFfmpegSource(engine, execution, path, {});
    ASSERT_TRUE(source.hasValue()) << source.status().message();
    const an::SequenceDescriptor* descriptor = source->descriptor();
    ASSERT_NE(descriptor, nullptr);
    EXPECT_EQ(descriptor->format, an::SequenceFormat::Video);
    EXPECT_EQ(descriptor->fixedFrame, frame);
    EXPECT_EQ(descriptor->frameCount, 5u);
    EXPECT_EQ(descriptor->timeBase->denominator, 1000u);
    EXPECT_TRUE(descriptor->timestampsRequired);
    EXPECT_TRUE(descriptor->seekByIndex);
    EXPECT_TRUE(descriptor->seekByTime);
    for (int index = 0; index < 5; ++index)
    {
        auto read = source->read(engine, execution);
        ASSERT_TRUE(read.hasValue()) << read.status().message();
        ASSERT_TRUE(read->frame);
        const an::SequenceFrame& delivered = *read->frame;
        EXPECT_EQ(delivered.position.index, static_cast<std::uint64_t>(index));
        EXPECT_EQ(delivered.position.presentationTicks, 40 * index);
        EXPECT_EQ(delivered.frame.buffer().ownership(), an::SampleBufferOwnership::Owned);
        EXPECT_TRUE(delivered.frame.checkAdoptable().ok());
        expectStoredRows(delivered.frame, 0u, 0, index, 64u);
        EXPECT_FALSE(delivered.metadata.outputIntent());
    }
    auto end = source->read(engine, execution);
    ASSERT_TRUE(end.hasValue());
    EXPECT_FALSE(end->frame);
    EXPECT_TRUE(source->close().ok());
}

TEST_F(FfmpegSequenceTest, StoredByteOrderAndWidthAreStatedAndNeverSwapped)
{
    // No container FFmpeg writes rawvideo into carries a floating grey format - NUT tags one as rgb555le
    // and AVI as bgra - so floating samples have no fixture here.
    const std::array<std::tuple<const char*, std::uint32_t, an::ByteOrder, an::RawSampleClass>, 3u> cases{
        {{"gray16be", 2u, an::ByteOrder::Big, an::RawSampleClass::UnsignedInteger},
         {"gray16le", 2u, an::ByteOrder::Little, an::RawSampleClass::UnsignedInteger},
         {"gray", 1u, an::ByteOrder::Native, an::RawSampleClass::UnsignedInteger}}};
    for (const auto& [format, bytes, order, sampleClass] : cases)
    {
        SCOPED_TRACE(format);
        const auto path = video(std::string(format) + ".nut", raw(format));
        auto info = an::inspectFfmpegFile(engine, execution, path, {});
        ASSERT_TRUE(info.hasValue()) << info.status().message();
        EXPECT_EQ(info->frame.sampleFormat().sizeBytes, bytes);
        EXPECT_EQ(info->frame.sampleFormat().sampleClass, sampleClass);
        EXPECT_EQ(info->frame.byteOrder(), order);
        const auto frames = readFile(path);
        ASSERT_EQ(frames.size(), 3u);
        for (int index = 0; index < 3; ++index)
            expectStoredRows(frames[static_cast<std::size_t>(index)].frame, 0u, 0, index, 16u * bytes);
    }
}

TEST_F(FfmpegSequenceTest, MosaicStreamsCarryTheirFilterArray)
{
    const std::array<std::tuple<const char*, an::CfaPattern, std::uint32_t, an::ByteOrder>, 4u> cases{
        {{"bayer_rggb8", an::CfaPattern::Rggb, 1u, an::ByteOrder::Native},
         {"bayer_bggr16le", an::CfaPattern::Bggr, 2u, an::ByteOrder::Little},
         {"bayer_gbrg16be", an::CfaPattern::Gbrg, 2u, an::ByteOrder::Big},
         {"bayer_grbg8", an::CfaPattern::Grbg, 1u, an::ByteOrder::Native}}};
    for (const auto& [format, pattern, bytes, order] : cases)
    {
        SCOPED_TRACE(format);
        const auto path = video(std::string(format) + ".nut", raw(format));
        auto info = an::inspectFfmpegFile(engine, execution, path, {});
        ASSERT_TRUE(info.hasValue()) << info.status().message();
        EXPECT_EQ(info->frame.channelSchema(), an::ChannelSchema::Mono);
        EXPECT_EQ(info->frame.cfa(), *an::CfaDescriptor::create(pattern, 0u, 0u));
        EXPECT_EQ(info->frame.byteOrder(), order);
        EXPECT_EQ(info->conversion, an::FfmpegConversion::None);
        const auto frames = readFile(path);
        ASSERT_EQ(frames.size(), 3u);
        for (int index = 0; index < 3; ++index)
            expectStoredRows(frames[static_cast<std::size_t>(index)].frame, 0u, 0, index, 16u * bytes);
    }
}

TEST_F(FfmpegSequenceTest, PackedRgbIsDescribedAndBlueFirstOrderIsExchanged)
{
    struct Case
    {
        const char* format;
        an::ChannelSchema schema;
        std::uint32_t bytes;
        an::FfmpegConversion conversion;
        bool exchanged;
        an::ByteOrder order;
    };
    const std::array<Case, 5u> cases{
        {{"rgb24", an::ChannelSchema::Rgb, 1u, an::FfmpegConversion::None, false, an::ByteOrder::Native},
         {"bgr24", an::ChannelSchema::Rgb, 1u, an::FfmpegConversion::ChannelReorder, true, an::ByteOrder::Native},
         {"rgb48be", an::ChannelSchema::Rgb, 2u, an::FfmpegConversion::None, false, an::ByteOrder::Big},
         {"bgra", an::ChannelSchema::Rgba, 1u, an::FfmpegConversion::ChannelReorder, true, an::ByteOrder::Native},
         {"rgba64le", an::ChannelSchema::Rgba, 2u, an::FfmpegConversion::None, false, an::ByteOrder::Little}}};
    for (const Case& entry : cases)
    {
        SCOPED_TRACE(entry.format);
        const auto path = video(std::string(entry.format) + ".nut", raw(entry.format));
        auto info = an::inspectFfmpegFile(engine, execution, path, {});
        ASSERT_TRUE(info.hasValue()) << info.status().message();
        EXPECT_EQ(info->frame.channelSchema(), entry.schema);
        EXPECT_EQ(info->frame.byteOrder(), entry.order);
        EXPECT_EQ(info->conversion, entry.conversion);
        const std::uint32_t channels = entry.schema == an::ChannelSchema::Rgba ? 4u : 3u;
        const auto frames = readFile(path);
        ASSERT_EQ(frames.size(), 3u);
        for (int index = 0; index < 3; ++index)
        {
            const an::RawFrame& frame = frames[static_cast<std::size_t>(index)].frame;
            EXPECT_EQ(frame.layout()->samplesPerPixel(), channels);
            for (std::uint32_t y = 0u; y < 12u; ++y)
            {
                const auto row = frame.row(0u, y);
                for (std::uint32_t x = 0u; x < 16u; ++x)
                    for (std::uint32_t channel = 0u; channel < channels; ++channel)
                    {
                        const std::uint32_t stored = entry.exchanged && channel < 3u ? 2u - channel : channel;
                        for (std::uint32_t k = 0u; k < entry.bytes; ++k)
                        {
                            const std::size_t pixel = static_cast<std::size_t>(x) * channels * entry.bytes;
                            ASSERT_EQ(std::to_integer<int>((*row)[pixel + channel * entry.bytes + k]),
                                      an::test::storedByte(0, static_cast<int>(pixel + stored * entry.bytes + k),
                                                           static_cast<int>(y), index));
                        }
                    }
            }
        }
    }
}

TEST_F(FfmpegSequenceTest, PlanarRgbIsReorderedIntoRedGreenBluePlanes)
{
    for (const auto& [format, bytes] : {std::pair<const char*, std::uint32_t>{"gbrp", 1u}, {"gbrp16le", 2u}})
    {
        SCOPED_TRACE(format);
        const auto path = video(std::string(format) + ".nut", raw(format));
        auto info = an::inspectFfmpegFile(engine, execution, path, {});
        ASSERT_TRUE(info.hasValue()) << info.status().message();
        EXPECT_EQ(info->conversion, an::FfmpegConversion::ChannelReorder);
        EXPECT_EQ(info->frame.channelSchema(), an::ChannelSchema::Rgb);
        const auto frames = readFile(path);
        ASSERT_EQ(frames.size(), 3u);
        for (int index = 0; index < 3; ++index)
        {
            const an::RawFrame& frame = frames[static_cast<std::size_t>(index)].frame;
            EXPECT_EQ(frame.layout()->planeCount(), 3u);
            EXPECT_EQ(frame.layout()->samplesPerPixel(), 1u);
            // FFmpeg stores green, blue and red in planes zero, one and two.
            expectStoredRows(frame, 0u, 2, index, 16u * bytes);
            expectStoredRows(frame, 1u, 0, index, 16u * bytes);
            expectStoredRows(frame, 2u, 1, index, 16u * bytes);
        }
    }
}

TEST_F(FfmpegSequenceTest, StatedMeaningIsAppliedToStoredSamplesAndJudged)
{
    const auto rgb = video("rgb.nut", raw("rgb24"));
    const auto grey = video("grey.nut", raw("gray"));
    auto display = an::inspectFfmpegFile(
        engine, execution, rgb,
        {.colorSpace = an::ColorSpace::DisplayRgb, .numericDomain = an::NumericDomain::EncodedNonlinear});
    ASSERT_TRUE(display.hasValue()) << display.status().message();
    EXPECT_EQ(display->frame.colorSpace(), an::ColorSpace::DisplayRgb);
    EXPECT_EQ(display->frame.numericDomain(), an::NumericDomain::EncodedNonlinear);

    const auto refusedWith = [&](const std::string& path, const an::FfmpegSourceOptions& options,
                                 const char* parameter) {
        auto source = an::createFfmpegSource(engine, execution, path, options);
        ASSERT_FALSE(source.hasValue());
        EXPECT_EQ(source.status().code(), an::ErrorCode::IncompatibleArguments);
        EXPECT_EQ(an::sequenceFailureReason(source.status()), an::SequenceFailure::DescriptorMismatch);
        EXPECT_EQ(parameterIn(source.status()), parameter);
        EXPECT_EQ(an::inspectFfmpegFile(engine, execution, path, options).status().code(),
                  an::ErrorCode::IncompatibleArguments);
    };
    refusedWith(grey, {.colorSpace = an::ColorSpace::DisplayRgb}, "colorSpace");
    refusedWith(rgb, {.numericDomain = an::NumericDomain::PhysicalScaled}, "numericDomain");
    refusedWith(rgb, {.colorSpace = an::ColorSpace::LinearRgb}, "numericDomain");
}

TEST_F(FfmpegSequenceTest, PixelAspectRatioComesFromTheStream)
{
    auto anamorphic = raw("gray");
    anamorphic.sarNumerator = 3;
    anamorphic.sarDenominator = 2;
    auto wide = an::inspectFfmpegFile(engine, execution, video("wide.nut", anamorphic), {});
    ASSERT_TRUE(wide.hasValue()) << wide.status().message();
    EXPECT_DOUBLE_EQ(wide->frame.pixelAspectRatio(), 1.5);
    auto square = an::inspectFfmpegFile(engine, execution, video("square.nut", raw("gray")), {});
    ASSERT_TRUE(square.hasValue()) << square.status().message();
    EXPECT_DOUBLE_EQ(square->frame.pixelAspectRatio(), 1.0);
    EXPECT_FALSE(square->nominalFrameRate);
    EXPECT_FALSE(square->durationTicks);
}

TEST_F(FfmpegSequenceTest, YCbCrIsConvertedThroughTheStreamsMatrixAndRange)
{
    const std::vector<std::array<int, 4>> levels{{120, 100, 160, 255}, {90, 150, 110, 255}, {160, 128, 128, 255}};
    struct Case
    {
        int space;
        int range;
        double kr;
        double kb;
        bool full;
        std::uint8_t code;
        an::FfmpegColorRange converted;
    };
    for (const Case& entry : {Case{1, 2, 0.2126, 0.0722, true, 1u, an::FfmpegColorRange::Full},
                              Case{6, 1, 0.299, 0.114, false, 6u, an::FfmpegColorRange::Limited}})
    {
        SCOPED_TRACE(entry.space);
        auto spec = ffv1("yuv444p", 16, 12, 3, 1);
        spec.colorSpace = entry.space;
        spec.colorRange = entry.range;
        spec.flatLevels = levels;
        const auto path = video("ycbcr" + std::to_string(entry.space) + ".mkv", spec);
        auto info = an::inspectFfmpegFile(engine, execution, path, {});
        ASSERT_TRUE(info.hasValue()) << info.status().message();
        EXPECT_EQ(info->conversion, an::FfmpegConversion::ColorMatrix);
        EXPECT_EQ(info->matrixCoefficients, static_cast<std::uint8_t>(entry.space));
        EXPECT_EQ(info->conversionMatrix, entry.code);
        EXPECT_EQ(info->conversionRange, entry.converted);
        EXPECT_EQ(info->colorRange, entry.converted);
        EXPECT_EQ(info->frame.channelSchema(), an::ChannelSchema::Rgb);
        EXPECT_EQ(info->frame.sampleFormat().sizeBytes, 1u);
        EXPECT_EQ(info->frame.colorSpace(), an::ColorSpace::DisplayRgb);
        EXPECT_EQ(info->frame.numericDomain(), an::NumericDomain::EncodedNonlinear);
        const auto frames = readFile(path);
        ASSERT_EQ(frames.size(), 3u);
        for (std::size_t index = 0u; index < 3u; ++index)
        {
            EXPECT_TRUE(frames[index].frame.checkAdoptable().ok());
            expectFlatRgb(
                frames[index].frame,
                referenceRgb(levels[index][0], levels[index][1], levels[index][2], entry.kr, entry.kb, entry.full, 8),
                2.0);
        }
    }
}

TEST_F(FfmpegSequenceTest, WideYCbCrBecomesSixteenBitRgbInHostOrder)
{
    const std::vector<std::array<int, 4>> levels{{120, 100, 160, 255}, {90, 150, 110, 255}};
    auto spec = ffv1("yuv444p10le", 16, 12, 2, 1);
    spec.colorSpace = 1;
    spec.colorRange = 2;
    spec.flatLevels = levels;
    const auto path = video("wide.mkv", spec);
    auto info = an::inspectFfmpegFile(engine, execution, path, {});
    ASSERT_TRUE(info.hasValue()) << info.status().message();
    EXPECT_EQ(info->storedBitsPerComponent, 10u);
    EXPECT_EQ(info->frame.sampleFormat().sizeBytes, 2u);
    EXPECT_EQ(info->frame.byteOrder(), an::nativeByteOrder());
    const auto frames = readFile(path);
    ASSERT_EQ(frames.size(), 2u);
    for (std::size_t index = 0u; index < 2u; ++index)
        expectFlatRgb(frames[index].frame,
                      referenceRgb(levels[index][0] * 4.0, levels[index][1] * 4.0, levels[index][2] * 4.0, 0.2126,
                                   0.0722, true, 10),
                      400.0);
}

TEST_F(FfmpegSequenceTest, UnstatedMatrixAndRangeAreRefusedUntilAssumed)
{
    const std::vector<std::array<int, 4>> levels{{120, 100, 160, 255}};
    auto spec = ffv1("yuv444p", 16, 12, 1, 1);
    spec.flatLevels = levels;
    const auto path = video("unstated.mkv", spec);
    const auto refusal = [&](const an::FfmpegSourceOptions& options, const std::string& file, const char* parameter) {
        auto source = an::createFfmpegSource(engine, execution, file, options);
        ASSERT_FALSE(source.hasValue());
        EXPECT_EQ(source.status().code(), an::ErrorCode::Unsupported);
        EXPECT_EQ(an::sequenceFailureReason(source.status()), an::SequenceFailure::UnsupportedEncoding);
        EXPECT_EQ(parameterIn(source.status()), parameter);
        auto info = an::inspectFfmpegFile(engine, execution, file, options);
        EXPECT_EQ(parameterIn(info.status()), parameter);
    };
    refusal({}, path, "matrixCoefficients");
    refusal({.unspecifiedMatrix = an::FfmpegMatrixAssumption::Bt709}, path, "colorRange");

    const an::FfmpegSourceOptions assumed{.unspecifiedMatrix = an::FfmpegMatrixAssumption::Bt601,
                                          .unspecifiedRange = an::FfmpegRangeAssumption::Limited};
    auto info = an::inspectFfmpegFile(engine, execution, path, assumed);
    ASSERT_TRUE(info.hasValue()) << info.status().message();
    EXPECT_EQ(info->matrixCoefficients, 2u);
    EXPECT_EQ(info->colorRange, an::FfmpegColorRange::Unspecified);
    EXPECT_EQ(info->conversionMatrix, 6u);
    EXPECT_EQ(info->conversionRange, an::FfmpegColorRange::Limited);
    const auto frames = readFile(path, assumed);
    ASSERT_EQ(frames.size(), 1u);
    expectFlatRgb(frames[0].frame, referenceRgb(120.0, 100.0, 160.0, 0.299, 0.114, false, 8), 2.0);

    spec.colorSpace = 1;
    refusal({}, video("matrix-only.mkv", spec), "colorRange");
}

TEST_F(FfmpegSequenceTest, LumaPlaneDeliversStoredLumaAndNothingElse)
{
    const an::FfmpegSourceOptions luma{.sampleDelivery = an::FfmpegSampleDelivery::LumaPlane};
    for (const auto& [format, bytes, order] :
         {std::tuple<const char*, std::uint32_t, an::ByteOrder>{"yuv420p", 1u, an::ByteOrder::Native},
          {"yuv420p10le", 2u, an::ByteOrder::Little}})
    {
        SCOPED_TRACE(format);
        const auto path = video(std::string(format) + ".nut", raw(format));
        auto info = an::inspectFfmpegFile(engine, execution, path, luma);
        ASSERT_TRUE(info.hasValue()) << info.status().message();
        EXPECT_EQ(info->conversion, an::FfmpegConversion::LumaPlane);
        EXPECT_EQ(info->frame.channelSchema(), an::ChannelSchema::Mono);
        EXPECT_EQ(info->frame.byteOrder(), order);
        const auto frames = readFile(path, luma);
        ASSERT_EQ(frames.size(), 3u);
        for (int index = 0; index < 3; ++index)
            expectStoredRows(frames[static_cast<std::size_t>(index)].frame, 0u, 0, index, 16u * bytes);
    }
}

TEST_F(FfmpegSequenceTest, WhatNeedsArithmeticIsRefusedUnlessTheOptionsConvert)
{
    const auto ycbcr = video("ycbcr.nut", raw("yuv420p"));
    const auto paletted = video("paletted.nut", raw("pal8"));
    const auto shifted = video("shifted.nut", raw("p010le"));
    const auto refused = [&](const std::string& path, an::FfmpegSampleDelivery delivery) {
        const an::FfmpegSourceOptions options{.sampleDelivery = delivery};
        auto source = an::createFfmpegSource(engine, execution, path, options);
        ASSERT_FALSE(source.hasValue());
        EXPECT_EQ(source.status().code(), an::ErrorCode::Unsupported);
        EXPECT_EQ(an::sequenceFailureReason(source.status()), an::SequenceFailure::UnsupportedEncoding);
        EXPECT_EQ(parameterIn(source.status()), "pixelFormat");
        EXPECT_EQ(an::inspectFfmpegFile(engine, execution, path, options).status().code(), an::ErrorCode::Unsupported);
    };
    refused(ycbcr, an::FfmpegSampleDelivery::AsStoredOnly);
    refused(paletted, an::FfmpegSampleDelivery::AsStoredOnly);
    refused(paletted, an::FfmpegSampleDelivery::LumaPlane);
    refused(shifted, an::FfmpegSampleDelivery::LumaPlane);
    EXPECT_TRUE(an::inspectFfmpegFile(engine, execution, video("grey.nut", raw("gray16le")),
                                      {.sampleDelivery = an::FfmpegSampleDelivery::AsStoredOnly})
                    .hasValue());
}

TEST_F(FfmpegSequenceTest, PalettesAreLookedUpAndAnEmptyPaletteNeedsAPolicy)
{
    const auto expectPalette = [](const std::vector<an::SequenceFrame>& frames, bool grey, bool varying = false,
                                  const std::vector<int>& order = {0, 1, 2}) {
        ASSERT_EQ(frames.size(), order.size());
        for (std::size_t position = 0u; position < order.size(); ++position)
        {
            const int index = order[position];
            const an::RawFrame& frame = frames[position].frame;
            for (std::uint32_t y = 0u; y < 12u; ++y)
            {
                const auto row = frame.row(0u, y);
                for (std::uint32_t x = 0u; x < 16u; ++x)
                {
                    const std::uint8_t stored = an::test::paletteIndex(static_cast<int>(x), static_cast<int>(y), index);
                    const int entryIndex = varying ? (stored + 17 * index) % 256 : stored;
                    const std::uint32_t entry = grey ? (stored * 0x010101u) : an::test::paletteEntry(entryIndex);
                    ASSERT_EQ(std::to_integer<std::uint32_t>((*row)[3u * x]), (entry >> 16u) & 0xFFu);
                    ASSERT_EQ(std::to_integer<std::uint32_t>((*row)[3u * x + 1u]), (entry >> 8u) & 0xFFu);
                    ASSERT_EQ(std::to_integer<std::uint32_t>((*row)[3u * x + 2u]), entry & 0xFFu);
                }
            }
        }
    };
    const auto nut = video("paletted.nut", raw("pal8"));
    auto info = an::inspectFfmpegFile(engine, execution, nut, {});
    ASSERT_TRUE(info.hasValue()) << info.status().message();
    EXPECT_EQ(info->conversion, an::FfmpegConversion::PaletteExpansion);
    EXPECT_EQ(info->frame.channelSchema(), an::ChannelSchema::Rgb);
    {
        SCOPED_TRACE("NUT, palette in the packet");
        expectPalette(readFile(nut), false);
    }

    auto sided = raw("pal8");
    sided.muxer = "avi";
    sided.paletteSideData = true;
    sided.varyPalette = true;
    const auto avi = video("paletted.avi", sided);
    EXPECT_EQ(an::inspectFfmpegFile(engine, execution, avi, {})->format, an::SequenceFormat::Avi);
    {
        // AVI hands a stored palette over once, with the first packet read after it changes; the index
        // scan reads every packet first, and the frames after a seek still need theirs.
        SCOPED_TRACE("AVI, a palette per frame beside the packets, in order and after seeking back");
        auto source = an::createFfmpegSource(engine, execution, avi, {});
        ASSERT_TRUE(source.hasValue()) << source.status().message();
        auto frames = readAll(*source, execution);
        for (const std::uint64_t index : {2u, 0u, 1u})
        {
            ASSERT_TRUE(source->seek(execution, {.index = index}).hasValue());
            auto read = source->read(engine, execution);
            ASSERT_TRUE(read.hasValue()) << chainText(read.status());
            ASSERT_TRUE(read->frame);
            frames.push_back(std::move(*read->frame));
        }
        expectPalette(frames, false, true, {0, 1, 2, 2, 0, 1});
        EXPECT_TRUE(source->close().ok());
    }

    sided.varyPalette = false;
    sided.paletteSideData = false;
    {
        // FFmpeg's rawvideo encoder appends each frame's palette to its packet, and the AVI writer keeps it.
        SCOPED_TRACE("AVI, palette carried by the encoder's packets");
        expectPalette(readFile(video("encoder-palette.avi", sided)), false);
    }

    sided.zeroPalette = true;
    const auto empty = video("empty-palette.avi", sided);
    SCOPED_TRACE("AVI whose palette is empty");
    {
        auto source = an::createFfmpegSource(engine, execution, empty, {});
        ASSERT_TRUE(source.hasValue()) << source.status().message();
        auto read = source->read(engine, execution);
        ASSERT_FALSE(read.hasValue());
        EXPECT_TRUE(chainCarries(read.status(), an::SequenceFailure::UnsupportedEncoding)) << read.status().message();
        EXPECT_EQ(parameterIn(read.status()), "palette");
        EXPECT_EQ(source->state(), an::SequenceState::Failed);
    }
    expectPalette(readFile(empty, {.emptyPalette = an::FfmpegEmptyPalettePolicy::Grayscale}), true);
}

TEST_F(FfmpegSequenceTest, OtherRgbPackingsAreRepacked)
{
    const auto path = video("rgb0.nut", raw("rgb0"));
    auto info = an::inspectFfmpegFile(engine, execution, path, {});
    ASSERT_TRUE(info.hasValue()) << info.status().message();
    EXPECT_EQ(info->conversion, an::FfmpegConversion::Repack);
    const auto frames = readFile(path);
    ASSERT_EQ(frames.size(), 3u);
    for (int index = 0; index < 3; ++index)
        for (std::uint32_t y = 0u; y < 12u; ++y)
        {
            const auto row = frames[static_cast<std::size_t>(index)].frame.row(0u, y);
            for (std::uint32_t x = 0u; x < 16u; ++x)
                for (std::uint32_t channel = 0u; channel < 3u; ++channel)
                    ASSERT_EQ(std::to_integer<int>((*row)[3u * x + channel]),
                              an::test::storedByte(0, static_cast<int>(4u * x + channel), static_cast<int>(y), index));
        }
}

TEST_F(FfmpegSequenceTest, BFramesAreDeliveredInPresentationOrder)
{
    const an::FfmpegSourceOptions luma{.sampleDelivery = an::FfmpegSampleDelivery::LumaPlane};
    const auto path = video("bframes.mkv", bframes("matroska", "mpeg4", 18));
    auto info = an::inspectFfmpegFile(engine, execution, path, luma);
    ASSERT_TRUE(info.hasValue()) << info.status().message();
    EXPECT_TRUE(info->reordered);
    EXPECT_FALSE(info->slicesOnOperationThreads);
    EXPECT_EQ(info->frameCount, 18u);
    const auto frames = readFile(path, luma);
    ASSERT_EQ(frames.size(), 18u);
    for (std::size_t index = 0u; index < frames.size(); ++index)
    {
        EXPECT_EQ(identify(frames[index].frame), static_cast<int>(index));
        if (index > 0u)
            EXPECT_GT(*frames[index].position.presentationTicks, *frames[index - 1u].position.presentationTicks);
    }
}

TEST_F(FfmpegSequenceTest, HiddenPacketsAreDecodedButNeverDelivered)
{
    const an::FfmpegSourceOptions luma{.sampleDelivery = an::FfmpegSampleDelivery::LumaPlane};
    const auto path = video("bframes.mp4", bframes("mp4", "mpeg4", 18));
    auto info = an::inspectFfmpegFile(engine, execution, path, luma);
    ASSERT_TRUE(info.hasValue()) << info.status().message();
    EXPECT_EQ(info->format, an::SequenceFormat::Mp4);
    // FFmpeg's MP4 writer trims a B-frame stream's last frame with an edit list, and its reader marks
    // that packet as decoded but not shown.
    EXPECT_GE(info->hiddenPacketCount, 1u);
    EXPECT_EQ(info->frameCount + info->hiddenPacketCount, 18u);
    EXPECT_TRUE(info->nominalFrameRate);
    ASSERT_TRUE(info->durationTicks);
    const auto frames = readFile(path, luma);
    ASSERT_EQ(frames.size(), info->frameCount);
    for (std::size_t index = 0u; index < frames.size(); ++index)
    {
        EXPECT_EQ(identify(frames[index].frame), static_cast<int>(index));
        ASSERT_TRUE(frames[index].position.durationTicks);
    }
    EXPECT_EQ(*info->durationTicks, *frames.back().position.presentationTicks + *frames.back().position.durationTicks);
}

TEST_F(FfmpegSequenceTest, SeeksReproduceSequentialDecodingExactly)
{
    struct Case
    {
        const char* name;
        VideoFixtureSpec spec;
        an::FfmpegSourceOptions options;
    };
    const an::FfmpegSourceOptions luma{.sampleDelivery = an::FfmpegSampleDelivery::LumaPlane};
    std::vector<Case> cases{{"bframes.mkv", bframes("matroska", "mpeg4", 20), luma},
                            {"bframes.mp4", bframes("mp4", "mpeg4", 20), luma},
                            {"ffv1.mkv", ffv1("gray16le", 16, 12, 10, 3), {}}};
    auto x264 = bframes("mp4", "libx264", 24);
    x264.gop = 8;
    x264.encoderOptions = "preset=veryfast:bf=2";
    if (an::test::canWriteVideo(x264))
        cases.push_back({"x264.mp4", x264, luma});
    else
        std::cout << "libx264 is not in this FFmpeg build; its seek case did not run" << std::endl;
    for (const Case& entry : cases)
    {
        SCOPED_TRACE(entry.name);
        const auto path = video(entry.name, entry.spec);
        std::vector<std::vector<std::byte>> reference;
        for (const auto& frame : readFile(path, entry.options))
            reference.push_back(bytesOf(frame.frame));
        ASSERT_GT(reference.size(), 5u);
        FfmpegTrace trace;
        auto traced = an::ExecutionContext::create(engine, {.traceSink = &trace});
        ASSERT_TRUE(traced.hasValue());
        auto source = an::createFfmpegSource(engine, *traced, path, entry.options);
        ASSERT_TRUE(source.hasValue()) << source.status().message();
        Mutator random(0x5EEDu + reference.size());
        for (int step = 0; step < 40; ++step)
        {
            const std::uint64_t target = random.below(reference.size());
            const auto sought = source->seek(*traced, {.index = target});
            ASSERT_TRUE(sought.hasValue()) << sought.status().message();
            const std::uint64_t reads = 1u + random.below(3u);
            for (std::uint64_t k = 0u; k < reads && target + k < reference.size(); ++k)
            {
                auto read = source->read(engine, *traced);
                ASSERT_TRUE(read.hasValue()) << "step " << step << ": " << read.status().message();
                ASSERT_TRUE(read->frame);
                ASSERT_EQ(read->frame->position.index, target + k);
                ASSERT_EQ(bytesOf(read->frame->frame), reference[target + k]) << "step " << step;
            }
        }
        EXPECT_GT(trace.events("repositions"), 1u);
        EXPECT_TRUE(source->close().ok());
    }
}

TEST_F(FfmpegSequenceTest, TimeSeeksChooseTheStatedSide)
{
    const auto path = video("time.mkv", ffv1("gray16le", 16, 12, 5, 1));
    auto source = an::createFfmpegSource(engine, execution, path, {});
    ASSERT_TRUE(source.hasValue()) << source.status().message();
    const auto landsOn = [&](an::SequenceSeekKind kind, std::int64_t ticks, std::uint64_t index) {
        auto sought = source->seek(execution, {.kind = kind, .ticks = ticks});
        ASSERT_TRUE(sought.hasValue()) << sought.status().message();
        EXPECT_EQ(sought->index, index);
        EXPECT_EQ(sought->presentationTicks, static_cast<std::int64_t>(40u * index));
        auto read = source->read(engine, execution);
        ASSERT_TRUE(read.hasValue() && read->frame) << read.status().message();
        EXPECT_EQ(read->frame->position.index, index);
        expectStoredRows(read->frame->frame, 0u, 0, static_cast<int>(index), 32u);
    };
    landsOn(an::SequenceSeekKind::TimeAtOrAfter, 50, 2u);
    landsOn(an::SequenceSeekKind::TimeAtOrBefore, 50, 1u);
    landsOn(an::SequenceSeekKind::TimeAtOrAfter, 80, 2u);
    landsOn(an::SequenceSeekKind::TimeAtOrBefore, 80, 2u);
    landsOn(an::SequenceSeekKind::TimeAtOrBefore, 0, 0u);
    auto past = source->seek(execution, {.kind = an::SequenceSeekKind::TimeAtOrAfter, .ticks = 161});
    ASSERT_FALSE(past.hasValue());
    EXPECT_TRUE(chainCarries(past.status(), an::SequenceFailure::InvalidPosition)) << past.status().message();
}

TEST_F(FfmpegSequenceTest, ASeekThatLandsPastItsKeyframeRestartsFromTheStart)
{
    // Every uncompressed frame is a keyframe larger than the Matroska writer's cluster threshold, so each frame has a
    // cluster and a cue of its own. Pointing one frame's cue at the next frame's cluster makes the demuxer's seek to
    // that frame land on the frame after it, and the demuxer reports success either way.
    const an::FfmpegSourceOptions luma{.sampleDelivery = an::FfmpegSampleDelivery::LumaPlane};
    const VideoFixtureSpec spec{
        .muxer = "matroska", .encoder = "rawvideo", .pixelFormat = "yuv420p", .width = 128, .height = 96, .frames = 6};
    const auto honest = video("honest.mkv", spec);
    std::vector<std::vector<std::byte>> reference;
    for (const auto& frame : readFile(honest, luma))
        reference.push_back(bytesOf(frame.frame));
    ASSERT_EQ(reference.size(), 6u);

    std::vector<char> bytes;
    {
        std::ifstream stream(directory / "honest.mkv", std::ios::binary);
        bytes.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    }
    std::vector<CuePosition> cues;
    findCueClusterPositions(bytes, 0u, bytes.size(), cues);
    ASSERT_EQ(cues.size(), 6u);
    // A cue whose field can hold the next cluster's position, and not the last two: from the last cue the demuxer
    // searches onward by itself.
    std::optional<std::uint64_t> misplaced;
    for (std::size_t cue = 1u; cue + 2u < cues.size() && !misplaced; ++cue)
        if (cues[cue].width == cues[cue + 1u].width && cues[cue].value < cues[cue + 1u].value)
            misplaced = cue;
    ASSERT_TRUE(misplaced);
    const CuePosition& written = cues[static_cast<std::size_t>(*misplaced)];
    const std::uint64_t later = cues[static_cast<std::size_t>(*misplaced) + 1u].value;
    for (std::size_t byte = 0u; byte < written.width; ++byte)
        bytes[written.offset + byte] = static_cast<char>((later >> (8u * (written.width - 1u - byte))) & 0xffu);
    const auto path = text("misplaced.mkv", std::string_view(bytes.data(), bytes.size()));

    FfmpegTrace trace;
    auto traced = an::ExecutionContext::create(engine, {.traceSink = &trace});
    ASSERT_TRUE(traced.hasValue());
    auto source = an::createFfmpegSource(engine, *traced, path, luma);
    ASSERT_TRUE(source.hasValue()) << source.status().message();
    const auto readFrom = [&](std::uint64_t target, std::uint64_t reads) {
        ASSERT_TRUE(source->seek(*traced, {.index = target}).hasValue());
        for (std::uint64_t k = 0u; k < reads; ++k)
        {
            auto read = source->read(engine, *traced);
            ASSERT_TRUE(read.hasValue() && read->frame) << chainText(read.status());
            ASSERT_EQ(read->frame->position.index, target + k);
            EXPECT_EQ(bytesOf(read->frame->frame), reference[target + k]) << "frame " << target + k;
        }
    };
    readFrom(*misplaced, 2u);
    EXPECT_EQ(trace.events("restarts"), 1u);
    readFrom(0u, 1u);
    readFrom(*misplaced + 2u, 1u);
    EXPECT_EQ(trace.events("restarts"), 1u);
    readFrom(*misplaced, 1u);
    EXPECT_EQ(trace.events("restarts"), 2u);
    EXPECT_TRUE(source->close().ok());
}

TEST_F(FfmpegSequenceTest, ReachableFramesAreDecodedForwardAndOthersReposition)
{
    const auto path = video("forward.mkv", ffv1("gray16le", 16, 12, 12, 4));
    FfmpegTrace trace;
    auto traced = an::ExecutionContext::create(engine, {.traceSink = &trace});
    ASSERT_TRUE(traced.hasValue());
    auto source = an::createFfmpegSource(engine, *traced, path, {});
    ASSERT_TRUE(source.hasValue()) << source.status().message();
    const auto readAt = [&](std::optional<std::uint64_t> seekTo, std::uint64_t repositions, std::uint64_t discarded) {
        if (seekTo)
            ASSERT_TRUE(source->seek(*traced, {.index = *seekTo}).hasValue());
        auto read = source->read(engine, *traced);
        ASSERT_TRUE(read.hasValue() && read->frame) << read.status().message();
        expectStoredRows(read->frame->frame, 0u, 0, static_cast<int>(read->frame->position.index), 32u);
        EXPECT_EQ(trace.events("repositions"), repositions);
        EXPECT_EQ(trace.events("frames-discarded"), discarded);
    };
    readAt(std::nullopt, 1u, 0u);
    readAt(std::nullopt, 1u, 0u);
    readAt(3u, 1u, 1u);
    readAt(9u, 2u, 2u);
    readAt(1u, 3u, 3u);
    EXPECT_EQ(trace.total("bytes-copied"), 5u * 16u * 12u * 2u);
    EXPECT_EQ(trace.events("frames-decoded"), 8u);
    EXPECT_EQ(trace.events("restarts"), 0u);
    EXPECT_EQ(trace.total("bytes-shared"), 0u);
}

TEST_F(FfmpegSequenceTest, DecoderOwnedFramesKeepTheDecodersBufferAliveAfterClose)
{
    const auto path = video("shared.nut", raw("gray16le", 4));
    FfmpegTrace trace;
    auto traced = an::ExecutionContext::create(engine, {.traceSink = &trace});
    ASSERT_TRUE(traced.hasValue());
    std::vector<an::SequenceFrame> frames;
    {
        auto source =
            an::createFfmpegSource(engine, *traced, path, {.frameMemory = an::FfmpegFrameMemory::DecoderOwned});
        ASSERT_TRUE(source.hasValue()) << source.status().message();
        frames = readAll(*source, *traced);
        EXPECT_TRUE(source->close().ok());
    }
    ASSERT_EQ(frames.size(), 4u);
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
    EXPECT_EQ(trace.total("bytes-shared"), 4u * 16u * 12u * 2u);
    EXPECT_EQ(trace.total("bytes-copied"), 0u);
    for (int index = 0; index < 4; ++index)
    {
        const an::RawFrame& frame = frames[static_cast<std::size_t>(index)].frame;
        EXPECT_EQ(frame.buffer().ownership(), an::SampleBufferOwnership::DecoderOwned);
        EXPECT_FALSE(frame.buffer().adoptable());
        EXPECT_EQ(frame.checkAdoptable().code(), an::ErrorCode::InvalidState);
        expectStoredRows(frame, 0u, 0, index, 32u);
    }
}

TEST_F(FfmpegSequenceTest, DecoderOwnedCopiesWhatCannotBeDescribedInPlace)
{
    for (const char* format : {"gbrp", "bgr24"})
    {
        SCOPED_TRACE(format);
        const auto path = video(std::string(format) + ".nut", raw(format));
        FfmpegTrace trace;
        auto traced = an::ExecutionContext::create(engine, {.traceSink = &trace});
        ASSERT_TRUE(traced.hasValue());
        auto source =
            an::createFfmpegSource(engine, *traced, path, {.frameMemory = an::FfmpegFrameMemory::DecoderOwned});
        ASSERT_TRUE(source.hasValue()) << source.status().message();
        auto frames = readAll(*source, *traced);
        ASSERT_EQ(frames.size(), 3u);
        for (const auto& frame : frames)
            EXPECT_EQ(frame.frame.buffer().ownership(), an::SampleBufferOwnership::Owned);
        EXPECT_EQ(trace.total("bytes-copied"), 3u * 16u * 12u * 3u);
        EXPECT_EQ(trace.total("bytes-shared"), 0u);
        EXPECT_TRUE(source->close().ok());
    }
}

TEST_F(FfmpegSequenceTest, EngineOwnedFramesAreCountedAndOutliveTheSource)
{
    const auto path = video("owned.mkv", ffv1("gray16le", 16, 12, 3, 1));
    std::vector<an::SequenceFrame> frames = readFile(path);
    ASSERT_EQ(frames.size(), 3u);
    EXPECT_GE(engine.memoryResource()->counters().liveBytes, 3u * 16u * 12u * 2u);
    for (int index = 0; index < 3; ++index)
        expectStoredRows(frames[static_cast<std::size_t>(index)].frame, 0u, 0, index, 32u);
    frames.clear();
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
}

TEST_F(FfmpegSequenceTest, BudgetRefusalsPublishNothing)
{
    const auto path = video("budget.mkv", ffv1("gray16le", 64, 48, 2, 1));
    {
        auto options = an::EngineContextOptions{.maxConcurrency = 1u};
        options.limits.memoryBudgetBytes = 64u * 48u * 2u - 1u;
        auto tight = an::EngineContext::create(options);
        ASSERT_TRUE(tight.hasValue());
        {
            auto tightExecution = an::ExecutionContext::create(*tight, {});
            ASSERT_TRUE(tightExecution.hasValue());
            auto source = an::createFfmpegSource(*tight, *tightExecution, path, {});
            ASSERT_TRUE(source.hasValue()) << source.status().message();
            auto read = source->read(*tight, *tightExecution);
            ASSERT_FALSE(read.hasValue());
            EXPECT_EQ(read.status().code(), an::ErrorCode::MemoryBudgetExceeded);
            EXPECT_EQ(source->state(), an::SequenceState::Failed);
        }
        EXPECT_EQ(tight->memoryResource()->counters().liveBytes, 0u);
        EXPECT_TRUE(tight->shutdown().ok());
    }
    {
        auto options = an::EngineContextOptions{.maxConcurrency = 1u};
        options.limits.memoryBudgetBytes = 100u;
        auto tiny = an::EngineContext::create(options);
        ASSERT_TRUE(tiny.hasValue());
        {
            auto tinyExecution = an::ExecutionContext::create(*tiny, {});
            ASSERT_TRUE(tinyExecution.hasValue());
            EXPECT_EQ(an::createFfmpegSource(*tiny, *tinyExecution, path, {}).status().code(),
                      an::ErrorCode::MemoryBudgetExceeded);
        }
        EXPECT_EQ(tiny->memoryResource()->counters().liveBytes, 0u);
        EXPECT_TRUE(tiny->shutdown().ok());
    }
}

TEST_F(FfmpegSequenceTest, CancellationStopsOpeningIndexingAndReading)
{
    const auto path = video("stopping.mkv", ffv1("gray16le", 16, 12, 5, 1));
    {
        std::stop_source stop;
        auto stopped = an::ExecutionContext::create(engine, {.stopToken = stop.get_token()});
        ASSERT_TRUE(stopped.hasValue());
        stop.request_stop();
        EXPECT_EQ(an::createFfmpegSource(engine, *stopped, path, {}).status().code(), an::ErrorCode::Cancelled);
        EXPECT_EQ(an::inspectFfmpegFile(engine, *stopped, path, {}).status().code(), an::ErrorCode::Cancelled);
    }
    {
        std::stop_source stop;
        FfmpegTrace trace(stop, "input-bytes");
        auto during = an::ExecutionContext::create(engine, {.stopToken = stop.get_token(), .traceSink = &trace});
        ASSERT_TRUE(during.hasValue());
        EXPECT_EQ(an::createFfmpegSource(engine, *during, path, {}).status().code(), an::ErrorCode::Cancelled);
        EXPECT_EQ(trace.events("packets-indexed"), 0u);
    }
    {
        std::stop_source stop;
        FfmpegTrace trace(stop, "packets-indexed", 2u);
        auto during = an::ExecutionContext::create(engine, {.stopToken = stop.get_token(), .traceSink = &trace});
        ASSERT_TRUE(during.hasValue());
        EXPECT_EQ(an::createFfmpegSource(engine, *during, path, {}).status().code(), an::ErrorCode::Cancelled);
        EXPECT_EQ(trace.events("packets-indexed"), 2u);
    }
    {
        auto source = an::createFfmpegSource(engine, execution, path, {});
        ASSERT_TRUE(source.hasValue()) << source.status().message();
        std::stop_source stop;
        FfmpegTrace trace(stop, "packets-sent");
        auto during = an::ExecutionContext::create(engine, {.stopToken = stop.get_token(), .traceSink = &trace});
        ASSERT_TRUE(during.hasValue());
        EXPECT_EQ(source->read(engine, *during).status().code(), an::ErrorCode::Cancelled);
        EXPECT_EQ(source->state(), an::SequenceState::Failed);
    }
}

TEST_F(FfmpegSequenceTest, SlicesRunOnTheReadingOperationsThreads)
{
    const auto path = video("slices.mkv", ffv1("gray16le", 64, 48, 3, 1));
    for (const std::uint32_t width : {4u, 1u})
    {
        SCOPED_TRACE(width);
        FfmpegTrace trace;
        auto traced = an::ExecutionContext::create(engine, {.traceSink = &trace, .maxConcurrency = width});
        ASSERT_TRUE(traced.hasValue());
        auto source = an::createFfmpegSource(engine, *traced, path, {});
        ASSERT_TRUE(source.hasValue()) << source.status().message();
        const auto frames = readAll(*source, *traced);
        ASSERT_EQ(frames.size(), 3u);
        for (int index = 0; index < 3; ++index)
            expectStoredRows(frames[static_cast<std::size_t>(index)].frame, 0u, 0, index, 128u);
        EXPECT_EQ(trace.events("slice-tasks"), 3u);
        EXPECT_EQ(trace.total("slice-tasks"), 12u);
        EXPECT_TRUE(source->close().ok());
    }
    auto serial = an::inspectFfmpegFile(engine, execution, video("serial.mkv", bframes("matroska", "mpeg4", 6)),
                                        {.sampleDelivery = an::FfmpegSampleDelivery::LumaPlane});
    ASSERT_TRUE(serial.hasValue()) << serial.status().message();
    EXPECT_FALSE(serial->slicesOnOperationThreads);
}

TEST_F(FfmpegSequenceTest, MissingFilesAndMalformedPathsAreRefusedWithTheirContext)
{
    const auto missing = utf8(directory / "absent.mkv");
    auto source = an::createFfmpegSource(engine, execution, missing, {});
    ASSERT_FALSE(source.hasValue());
    EXPECT_EQ(source.status().code(), an::ErrorCode::ExternalFailure);
    EXPECT_EQ(an::sequenceFailureReason(source.status()), an::SequenceFailure::IoFailure);
    ASSERT_NE(source.status().findContext(an::ContextKey::Path), nullptr);
    EXPECT_EQ(source.status().findContext(an::ContextKey::Path)->text, missing);
    EXPECT_NE(source.status().findContext(an::ContextKey::LibraryCode), nullptr);
    EXPECT_EQ(an::inspectFfmpegFile(engine, execution, missing, {}).status().code(), an::ErrorCode::ExternalFailure);
    for (const std::string& bad : {std::string{}, std::string("bad\0path", 8u), std::string("\xff\xfe.mkv")})
    {
        auto refused = an::createFfmpegSource(engine, execution, bad, {});
        EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidArgument);
        EXPECT_EQ(parameterIn(refused.status()), "path");
    }
    EXPECT_EQ(an::createFfmpegSource(engine, execution, utf8(directory), {}).status().code(),
              an::ErrorCode::ExternalFailure);
    EXPECT_EQ(an::createFfmpegSource(an::EngineContext{}, execution, missing, {}).status().code(),
              an::ErrorCode::NotInitialized);
}

TEST_F(FfmpegSequenceTest, UnrecognisedAndReferringContainersAreRefused)
{
    std::string prose;
    for (int line = 0; line < 64; ++line)
        prose += "this is not a video, only prose that no demuxer recognises as one\n";
    for (const std::string& path : {text("prose.mkv", prose), text("empty.mkv", {})})
    {
        auto source = an::createFfmpegSource(engine, execution, path, {});
        ASSERT_FALSE(source.hasValue());
        EXPECT_TRUE(explained(source.status())) << source.status().message();
        EXPECT_EQ(an::inspectFfmpegFile(engine, execution, path, {}).status().code(), source.status().code());
    }
    static_cast<void>(video("clip.nut", raw("gray")));
    const auto list = text("list.ffconcat", "ffconcat version 1.0\nfile 'clip.nut'\n");
    auto referring = an::createFfmpegSource(engine, execution, list, {});
    ASSERT_FALSE(referring.hasValue());
    EXPECT_EQ(referring.status().code(), an::ErrorCode::InvalidData) << referring.status().message();
    EXPECT_EQ(an::sequenceFailureReason(referring.status()), an::SequenceFailure::MalformedContainer);
    EXPECT_TRUE(parameterIn(referring.status()).empty());
}

TEST_F(FfmpegSequenceTest, StreamSelectionIsExplicit)
{
    auto spec = raw("gray16le");
    spec.extraStreams = 1;
    const auto path = video("streams.nut", spec);
    auto first = an::inspectFfmpegFile(engine, execution, path, {});
    ASSERT_TRUE(first.hasValue()) << first.status().message();
    EXPECT_EQ(first->streamIndex, 0u);
    EXPECT_EQ(first->storedPixelFormat, "gray16le");
    const auto second = readFile(path, {.streamIndex = 1u});
    ASSERT_EQ(second.size(), 3u);
    for (const auto& frame : second)
    {
        EXPECT_EQ(frame.frame.descriptor()->sampleFormat().sizeBytes, 1u);
        const auto row = frame.frame.row(0u, 5u);
        EXPECT_TRUE(std::all_of(row->begin(), row->begin() + 16, [](std::byte b) { return b == std::byte{200}; }));
    }
    auto absent = an::createFfmpegSource(engine, execution, path, {.streamIndex = 2u});
    ASSERT_FALSE(absent.hasValue());
    EXPECT_EQ(absent.status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(an::sequenceFailureReason(absent.status()), an::SequenceFailure::DescriptorMismatch);
    EXPECT_EQ(parameterIn(absent.status()), "streamIndex");
}

TEST_F(FfmpegSequenceTest, OptionsOutsideTheirEnumerationsAreRefusedByName)
{
    const auto path = video("options.nut", raw("gray"));
    const auto refused = [&](const an::FfmpegSourceOptions& options, const char* parameter) {
        auto source = an::createFfmpegSource(engine, execution, path, options);
        EXPECT_EQ(source.status().code(), an::ErrorCode::InvalidArgument);
        EXPECT_EQ(parameterIn(source.status()), parameter);
        EXPECT_EQ(parameterIn(an::inspectFfmpegFile(engine, execution, path, options).status()), parameter);
    };
    refused({.frameMemory = static_cast<an::FfmpegFrameMemory>(9)}, "frameMemory");
    refused({.sampleDelivery = static_cast<an::FfmpegSampleDelivery>(9)}, "sampleDelivery");
    refused({.unspecifiedMatrix = static_cast<an::FfmpegMatrixAssumption>(9)}, "unspecifiedMatrix");
    refused({.unspecifiedRange = static_cast<an::FfmpegRangeAssumption>(9)}, "unspecifiedRange");
    refused({.emptyPalette = static_cast<an::FfmpegEmptyPalettePolicy>(9)}, "emptyPalette");
    refused({.colorSpace = static_cast<an::ColorSpace>(99)}, "colorSpace");
    refused({.numericDomain = static_cast<an::NumericDomain>(99)}, "numericDomain");
}

TEST_F(FfmpegSequenceTest, FramesCarryTheStreamsFactsAndNoOutputIntent)
{
    const auto frames = readFile(video("facts.mkv", ffv1("gray16le", 16, 12, 1, 1)));
    ASSERT_EQ(frames.size(), 1u);
    const an::CodecMetadata& codec = frames[0].metadata.codec();
    const auto card = [&](std::string_view keyword) {
        const auto* found = codec.find(an::CodecNamespace::Ffmpeg, keyword);
        return found != nullptr ? found->value() : std::string("<absent>");
    };
    EXPECT_EQ(card("container"), "matroska,webm");
    EXPECT_EQ(card("codec"), "ffv1");
    EXPECT_EQ(card("pixelFormat"), "gray16le");
    EXPECT_EQ(card("streamIndex"), "0");
    EXPECT_EQ(card("bitsPerComponent"), "16");
    EXPECT_EQ(card("colorRange"), "Unspecified");
    EXPECT_EQ(card("matrixCoefficients"), "2");
    EXPECT_EQ(card("conversion"), "None");
    ASSERT_TRUE(codec.sourceEncoding().sampleFormat);
    EXPECT_EQ(codec.sourceEncoding().sampleFormat->sizeBytes, 2u);
    EXPECT_EQ(codec.sourceEncoding().byteOrder, an::ByteOrder::Little);
    EXPECT_FALSE(frames[0].metadata.outputIntent());
}

TEST_F(FfmpegSequenceTest, IndependentSessionsReadOneFileConcurrently)
{
    const auto path = video("concurrent.mkv", ffv1("gray16le", 16, 12, 6, 2));
    std::vector<std::vector<std::byte>> reference;
    for (const auto& frame : readFile(path))
        reference.push_back(bytesOf(frame.frame));
    ASSERT_EQ(reference.size(), 6u);
    auto wide = an::EngineContext::create({.maxConcurrency = 8u});
    ASSERT_TRUE(wide.hasValue());
    std::array<bool, 4u> matched{};
    {
        std::vector<std::thread> threads;
        for (std::size_t worker = 0u; worker < matched.size(); ++worker)
            threads.emplace_back([&, worker] {
                auto workerExecution = an::ExecutionContext::create(*wide, {.maxConcurrency = 1u});
                if (!workerExecution)
                    return;
                auto source = an::createFfmpegSource(*wide, *workerExecution, path, {});
                if (!source)
                    return;
                bool all = true;
                for (std::uint64_t step = 0u; step < 6u && all; ++step)
                {
                    const std::uint64_t target = (5u * step + worker) % 6u;
                    all = static_cast<bool>(source->seek(*workerExecution, {.index = target}));
                    auto read = source->read(*wide, *workerExecution);
                    all = all && read && read->frame && bytesOf(read->frame->frame) == reference[target];
                }
                matched[worker] = all && source->close().ok();
            });
        for (auto& thread : threads)
            thread.join();
    }
    EXPECT_EQ(std::count(matched.begin(), matched.end(), true), 4);
    EXPECT_TRUE(wide->shutdown().ok());
}

TEST_F(FfmpegSequenceTest, AnEmptyStreamIsAnEmptySource)
{
    // AVI states its stream in its header; NUT and Matroska refuse a stream that holds no packet.
    auto empty = raw("gray", 0);
    empty.muxer = "avi";
    const auto path = video("empty.avi", empty);
    auto source = an::createFfmpegSource(engine, execution, path, {});
    ASSERT_TRUE(source.hasValue()) << source.status().message();
    EXPECT_EQ(source->descriptor()->frameCount, 0u);
    auto read = source->read(engine, execution);
    ASSERT_TRUE(read.hasValue()) << read.status().message();
    EXPECT_FALSE(read->frame);
    EXPECT_TRUE(source->close().ok());
}

TEST_F(FfmpegSequenceTest, MutatedAndTruncatedContainersAreReadOrRefusedWithAReason)
{
    const std::array<std::pair<const char*, VideoFixtureSpec>, 2u> bases{
        {{"mkv", ffv1("gray16le", 16, 12, 4, 2)}, {"nut", raw("gray16le", 4)}}};
    std::array<std::vector<char>, 2u> originals;
    for (std::size_t base = 0u; base < bases.size(); ++base)
    {
        const auto path = video(std::string("base.") + bases[base].first, bases[base].second);
        std::ifstream stream(directory / (std::string("base.") + bases[base].first), std::ios::binary);
        originals[base].assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
        ASSERT_FALSE(originals[base].empty()) << path;
    }
    Mutator random(20260913u);
    std::uint64_t opened = 0u;
    std::uint64_t refused = 0u;
    std::uint64_t readFailures = 0u;
    for (int round = 0; round < 160; ++round)
    {
        const std::size_t base = static_cast<std::size_t>(round) % bases.size();
        std::vector<char> bytes = originals[base];
        if (round % 4 == 3)
            bytes.resize(static_cast<std::size_t>(random.below(bytes.size())));
        else
            for (std::uint64_t change = 0u, changes = 1u + random.below(4u); change < changes; ++change)
                bytes[static_cast<std::size_t>(random.below(bytes.size()))] = static_cast<char>(random.below(256u));
        const auto path =
            text(std::string("mutated.") + bases[base].first, std::string_view(bytes.data(), bytes.size()));
        auto info = an::inspectFfmpegFile(engine, execution, path, {});
        auto source = an::createFfmpegSource(engine, execution, path, {});
        ASSERT_EQ(info.hasValue(), source.hasValue()) << "round " << round;
        if (!source.hasValue())
        {
            ++refused;
            EXPECT_TRUE(explained(source.status())) << "round " << round << ": " << source.status().message();
            EXPECT_EQ(info.status().code(), source.status().code()) << "round " << round;
            continue;
        }
        ++opened;
        for (;;)
        {
            auto read = source->read(engine, execution);
            if (!read.hasValue())
            {
                ++readFailures;
                EXPECT_TRUE(explained(read.status())) << "round " << round << ": " << read.status().message();
                break;
            }
            if (!read->frame)
                break;
        }
        static_cast<void>(source->close());
    }
    std::cout << "mutated containers: " << opened << " opened (" << readFailures << " failed while reading), "
              << refused << " refused" << std::endl;
    EXPECT_EQ(opened + refused, 160u);
}

TEST_F(FfmpegSequenceTest, TheProcessorDecodesUnlessADeviceIsNamedAndEachFrameSaysWhatDecodedIt)
{
    const VideoFixtureSpec spec{.encoder = "libx264",
                                .pixelFormat = "yuv420p",
                                .width = 64,
                                .height = 48,
                                .frames = 6,
                                .gop = 3,
                                .colorRange = 1,
                                .colorSpace = 1,
                                .colorPrimaries = 1,
                                .colorTransfer = 1};
    if (!an::test::canWriteVideo(spec))
        GTEST_SKIP() << "this build's FFmpeg cannot write the H.264 fixture";
    const std::string path = video("processor.mkv", spec);
    auto info = an::inspectFfmpegFile(engine, execution, path, {});
    ASSERT_TRUE(info.hasValue()) << info.status().message();
    EXPECT_EQ(info->decodeDevice, an::FfmpegDecodeDevice::Processor);
    const auto frames = readFile(path);
    ASSERT_EQ(frames.size(), 6u);
    for (const an::SequenceFrame& frame : frames)
    {
        const an::CodecCard* card = frame.metadata.codec().find(an::CodecNamespace::Ffmpeg, "decodeDevice");
        ASSERT_NE(card, nullptr);
        EXPECT_EQ(card->value(), "processor");
    }
}

TEST_F(FfmpegSequenceTest, AFrameADeviceDecodesIsTheFrameTheProcessorDelivers)
{
    struct Clip
    {
        const char* name;
        VideoFixtureSpec spec;
    };
    const std::array clips{
        Clip{"h264.mkv",
             {.encoder = "libx264",
              .pixelFormat = "yuv420p",
              .width = 320,
              .height = 240,
              .frames = 8,
              .gop = 4,
              .bframes = 2,
              .colorRange = 1,
              .colorSpace = 1,
              .colorPrimaries = 1,
              .colorTransfer = 1}},
        Clip{"hevc10.mkv",
             {.encoder = "libx265",
              .pixelFormat = "yuv420p10le",
              .width = 320,
              .height = 240,
              .frames = 8,
              .gop = 4,
              .colorRange = 1,
              .colorSpace = 1,
              .colorPrimaries = 1,
              .colorTransfer = 1}},
        Clip{"vp9.mkv",
             {.encoder = "libvpx-vp9",
              .pixelFormat = "yuv420p",
              .width = 320,
              .height = 240,
              .frames = 8,
              .gop = 4,
              .colorRange = 1,
              .colorSpace = 1,
              .colorPrimaries = 1,
              .colorTransfer = 1}},
    };
    const std::array devices{an::FfmpegDecodeDevice::Automatic,    an::FfmpegDecodeDevice::Cuda,
                             an::FfmpegDecodeDevice::D3d11va,      an::FfmpegDecodeDevice::Vaapi,
                             an::FfmpegDecodeDevice::VideoToolbox, an::FfmpegDecodeDevice::Vulkan};
    std::uint64_t compared = 0u;
    std::string decodedOn;
    for (const Clip& clip : clips)
    {
        if (!an::test::canWriteVideo(clip.spec))
            continue;
        const std::string path = video(clip.name, clip.spec);
        const auto processorColour = readFile(path);
        const auto processorLuma = readFile(path, {.sampleDelivery = an::FfmpegSampleDelivery::LumaPlane});
        ASSERT_FALSE(processorColour.empty()) << clip.name;
        for (const an::FfmpegDecodeDevice device : devices)
        {
            auto info = an::inspectFfmpegFile(engine, execution, path, {.decodeDevice = device});
            ASSERT_TRUE(info.hasValue()) << clip.name << ": " << info.status().message();
            ASSERT_NE(info->decodeDevice, an::FfmpegDecodeDevice::Automatic);
            if (device != an::FfmpegDecodeDevice::Automatic)
                EXPECT_TRUE(info->decodeDevice == an::FfmpegDecodeDevice::Processor || info->decodeDevice == device)
                    << clip.name;
            if (info->decodeDevice == an::FfmpegDecodeDevice::Processor)
                continue;
            const auto colour = readFile(path, {.decodeDevice = device});
            const auto luma =
                readFile(path, {.sampleDelivery = an::FfmpegSampleDelivery::LumaPlane, .decodeDevice = device});
            ASSERT_EQ(colour.size(), processorColour.size()) << clip.name;
            ASSERT_EQ(luma.size(), processorLuma.size()) << clip.name;
            for (std::size_t index = 0u; index < colour.size(); ++index)
            {
                EXPECT_EQ(bytesOf(colour[index].frame), bytesOf(processorColour[index].frame))
                    << clip.name << " frame " << index;
                EXPECT_EQ(bytesOf(luma[index].frame), bytesOf(processorLuma[index].frame))
                    << clip.name << " frame " << index;
                const an::CodecCard* card =
                    colour[index].metadata.codec().find(an::CodecNamespace::Ffmpeg, "decodeDevice");
                ASSERT_NE(card, nullptr);
                EXPECT_EQ(card->value(), an::ffmpegDecodeDeviceName(info->decodeDevice));
            }
            compared += colour.size();
            decodedOn += std::string(clip.name) + ":" + an::ffmpegDecodeDeviceName(info->decodeDevice) + " ";
        }
    }
    RecordProperty("deviceFramesCompared", static_cast<int>(compared));
    RecordProperty("decodedOn", decodedOn);
    if (const auto required = an::test::requiredVideoDevice("AFTERNIGHT_TEST_REQUIRE_DECODE_DEVICE"))
    {
        EXPECT_NE(decodedOn.find(":" + *required + " "), std::string::npos)
            << "AFTERNIGHT_TEST_REQUIRE_DECODE_DEVICE=" << *required << ", but the clips decoded on: " << decodedOn;
    }
    else if (compared == 0u)
    {
        GTEST_SKIP() << "no decode device on this host decoded a clip, so there was nothing to compare";
    }
}

TEST_F(FfmpegSequenceTest, DecodeDevicesAreNamedInLowerCaseAndAnUnknownOneIsRefused)
{
    EXPECT_STREQ(an::ffmpegDecodeDeviceName(an::FfmpegDecodeDevice::Processor), "processor");
    EXPECT_STREQ(an::ffmpegDecodeDeviceName(an::FfmpegDecodeDevice::Automatic), "auto");
    EXPECT_STREQ(an::ffmpegDecodeDeviceName(an::FfmpegDecodeDevice::Cuda), "cuda");
    EXPECT_STREQ(an::ffmpegDecodeDeviceName(an::FfmpegDecodeDevice::D3d11va), "d3d11va");
    EXPECT_STREQ(an::ffmpegDecodeDeviceName(an::FfmpegDecodeDevice::Vaapi), "vaapi");
    EXPECT_STREQ(an::ffmpegDecodeDeviceName(an::FfmpegDecodeDevice::VideoToolbox), "videotoolbox");
    EXPECT_STREQ(an::ffmpegDecodeDeviceName(an::FfmpegDecodeDevice::Vulkan), "vulkan");
    EXPECT_STREQ(an::ffmpegDecodeDeviceName(static_cast<an::FfmpegDecodeDevice>(99)), "unknown");
    const VideoFixtureSpec spec{};
    if (!an::test::canWriteVideo(spec))
        GTEST_SKIP() << "this build's FFmpeg cannot write the FFV1 fixture";
    const std::string path = video("unknown-device.mkv", spec);
    auto refused =
        an::inspectFfmpegFile(engine, execution, path, {.decodeDevice = static_cast<an::FfmpegDecodeDevice>(99)});
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidArgument);
    const an::StatusContextField* const field = refused.status().findContext(an::ContextKey::Parameter);
    ASSERT_NE(field, nullptr);
    EXPECT_EQ(field->text, "decodeDevice");
}

} // namespace
