// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/IO/FfmpegSink.h>

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
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#if defined(__GLIBC__)
#include <malloc.h>
#endif
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <sys/mman.h>
#endif

namespace
{
using Bytes = std::vector<std::byte>;

/// A sample value as a function of its column, its row counted from the top of the picture, and its channel.
using PictureValue = std::function<int(std::uint32_t, std::uint32_t, std::uint32_t)>;

std::string parameterOf(const an::Status& status)
{
    for (an::Status current = status; current.failed(); current = current.cause())
        if (const auto* field = current.findContext(an::ContextKey::Parameter))
            return field->text;
    return {};
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

/// One frame's description, layout and bytes. Padding holds 0xEE, which no conversion should read.
struct VideoFrame
{
    an::RawFrameDescriptor descriptor;
    an::ImageLayout layout;
    Bytes bytes;
};

/// Builds a frame whose picture holds `value`, storing its rows from the top or from the bottom as the
/// description's origin says.
VideoFrame makeFrame(const an::RawFrameDescriptor& descriptor, const PictureValue& value, bool planar = false,
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
    VideoFrame frame{descriptor, *layout, Bytes(static_cast<std::size_t>(planeStride * planes), std::byte{0xEE})};
    if (sampleBytes != 1u)
        return frame;
    const bool bottomUp = descriptor.coordinateOrigin() == an::CoordinateOrigin::BottomLeft;
    for (std::uint32_t c = 0u; c < channels; ++c)
        for (std::uint32_t y = 0u; y < extent.height(); ++y)
            for (std::uint32_t x = 0u; x < extent.width(); ++x)
            {
                const std::uint32_t pictureRow = bottomUp ? extent.height() - 1u - y : y;
                const std::uint64_t at =
                    planar ? c * planeStride + y * rowStride + x : y * rowStride + std::uint64_t{x} * channels + c;
                frame.bytes[static_cast<std::size_t>(at)] = static_cast<std::byte>(value(x, pictureRow, c) & 0xff);
            }
    return frame;
}

VideoFrame flatFrame(const an::RawFrameDescriptor& descriptor, int level)
{
    return makeFrame(descriptor, [level](std::uint32_t, std::uint32_t, std::uint32_t) { return level; });
}

an::SequenceFrame borrowed(const VideoFrame& frame, std::uint64_t position)
{
    auto buffer = an::SampleBuffer::borrowConst(std::span<const std::byte>(frame.bytes.data(), frame.bytes.size()));
    EXPECT_TRUE(buffer.hasValue());
    auto raw = an::RawFrame::create(frame.descriptor, frame.layout, std::move(*buffer));
    EXPECT_TRUE(raw.hasValue()) << raw.status().message();
    return an::SequenceFrame{std::move(*raw), {}, an::SequencePosition{position, std::nullopt, std::nullopt}};
}

an::SequenceDescriptor videoDescriptor(const an::RawFrameDescriptor& frame, an::FfmpegVideoContainer container,
                                       an::SequenceRate rate = {25u, 1u})
{
    return {.format = container == an::FfmpegVideoContainer::Mp4 ? an::SequenceFormat::Mp4 : an::SequenceFormat::Video,
            .fixedFrame = frame,
            .nominalFrameRate = rate};
}

const char* extensionOf(an::FfmpegVideoContainer container)
{
    switch (container)
    {
    case an::FfmpegVideoContainer::Mp4:
        return ".mp4";
    case an::FfmpegVideoContainer::Matroska:
        return ".mkv";
    case an::FfmpegVideoContainer::WebM:
        return ".webm";
    }
    return ".bin";
}

/// A limited-range luma code for an eight-bit full-range value.
double limitedCode(double value)
{
    return 16.0 + 219.0 * value / 255.0;
}

/// The mean of one channel over a band of rows of a delivered frame, interleaved or planar.
double meanOf(const an::SequenceFrame& frame, std::uint32_t firstRow, std::uint32_t rows, std::uint32_t channel = 0u)
{
    const auto layout = frame.frame.layout();
    EXPECT_TRUE(layout.has_value());
    const std::uint32_t width = layout->extent().width();
    const bool planar = layout->planeCount() > 1u;
    const std::uint32_t samplesPerPixel = layout->samplesPerPixel();
    double sum = 0.0;
    for (std::uint32_t y = firstRow; y < firstRow + rows; ++y)
    {
        const auto row = frame.frame.row(planar ? channel : 0u, y);
        EXPECT_TRUE(row.has_value());
        for (std::uint32_t x = 0u; x < width; ++x)
            sum += std::to_integer<int>((*row)[planar ? x : std::size_t{x} * samplesPerPixel + channel]);
    }
    return sum / (static_cast<double>(width) * rows);
}

bool contains(const Bytes& bytes, std::string_view text)
{
    const auto* begin = reinterpret_cast<const char*>(bytes.data());
    return std::search(begin, begin + bytes.size(), text.begin(), text.end()) != begin + bytes.size();
}

/// Counts an operation's FFmpeg sink events and requests a stop when a named one has arrived a stated number of times.
class SinkTrace final : public an::TraceSink
{
public:
    SinkTrace(std::stop_source& stop, std::string_view name, std::uint64_t after)
        : m_stop(stop), m_name(name), m_after(after)
    {
    }

    void onTrace(const an::TraceEvent& event) noexcept override
    {
        if (std::string_view(event.category) != "ffmpeg" || std::string_view(event.name) != m_name)
            return;
        if (m_seen.fetch_add(1u) + 1u >= m_after)
            m_stop.request_stop();
    }

private:
    std::stop_source& m_stop;
    std::string_view m_name;
    std::uint64_t m_after;
    std::atomic<std::uint64_t> m_seen{0u};
};

class FfmpegSinkTest : public testing::Test
{
protected:
    void SetUp() override
    {
        an::test::quietFfmpegLog();
        directory =
            std::filesystem::temp_directory_path() /
            ("an-ffmpeg-sink-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
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

    /// Encodes frames through an output and a sink, commits, and returns what was published.
    Published writeVideo(const std::filesystem::path& name, const an::SequenceDescriptor& descriptor,
                         const an::FfmpegSinkRequest& request, const std::vector<VideoFrame>& frames)
    {
        const auto destination = pathOf(name);
        auto output = an::SequenceOutput::begin(
            engine, execution, {.destinationPath = destination, .durability = an::OutputDurability::CloseOnly});
        if (!output.hasValue())
        {
            ADD_FAILURE() << "begin: " << output.status().message();
            return {};
        }
        auto sink = an::createFfmpegSink(engine, execution, *output, descriptor, request);
        if (!sink.hasValue())
        {
            ADD_FAILURE() << "createFfmpegSink: " << sink.status().message() << " [" << parameterOf(sink.status())
                          << "]";
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

    /// Every frame a published video delivers under the options, in presentation order.
    std::vector<an::SequenceFrame> readFrames(const std::string& path, const an::FfmpegSourceOptions& options)
    {
        std::vector<an::SequenceFrame> frames;
        auto source = an::createFfmpegSource(engine, execution, path, options);
        EXPECT_TRUE(source.hasValue()) << source.status().message();
        if (!source.hasValue())
            return frames;
        for (;;)
        {
            auto read = source->read(engine, execution);
            EXPECT_TRUE(read.hasValue()) << read.status().message();
            if (!read.hasValue() || !read->frame)
                break;
            frames.push_back(std::move(*read->frame));
        }
        EXPECT_TRUE(source->close().ok());
        return frames;
    }

    /// Encodes frames as `writeVideo` does, stating which encoder opened.
    Published writeReportedVideo(const std::filesystem::path& name, const an::SequenceDescriptor& descriptor,
                                 const an::FfmpegSinkRequest& request, const std::vector<VideoFrame>& frames,
                                 an::FfmpegEncoderReport& report)
    {
        const auto destination = pathOf(name);
        auto output = an::SequenceOutput::begin(
            engine, execution, {.destinationPath = destination, .durability = an::OutputDurability::CloseOnly});
        if (!output.hasValue())
        {
            ADD_FAILURE() << "begin: " << output.status().message();
            return {};
        }
        auto sink = an::createFfmpegSink(engine, execution, *output, descriptor, request, report);
        if (!sink.hasValue())
        {
            ADD_FAILURE() << "createFfmpegSink: " << sink.status().message() << " [" << parameterOf(sink.status())
                          << "]";
            return {};
        }
        for (std::size_t index = 0u; index < frames.size(); ++index)
        {
            auto appended = sink->append(engine, execution, borrowed(frames[index], index));
            if (!appended.hasValue())
            {
                ADD_FAILURE() << "append " << index << ": " << appended.status().message();
                return {};
            }
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
        return Published{readAll(destination), destination};
    }

    std::filesystem::path directory;
    an::EngineContext engine;
    an::ExecutionContext execution;
};

constexpr an::FfmpegSourceOptions kLuma{.sampleDelivery = an::FfmpegSampleDelivery::LumaPlane};

TEST_F(FfmpegSinkTest, EncoderAvailabilityIsReported)
{
    EXPECT_EQ(an::isFfmpegEncoderAvailable(an::FfmpegVideoCodec::H264),
              an::test::canWriteVideo({.muxer = "mp4", .encoder = "libx264", .pixelFormat = "yuv420p"}));
    EXPECT_EQ(an::isFfmpegEncoderAvailable(an::FfmpegVideoCodec::H265),
              an::test::canWriteVideo({.muxer = "mp4", .encoder = "libx265", .pixelFormat = "yuv420p"}));
    EXPECT_EQ(an::isFfmpegEncoderAvailable(an::FfmpegVideoCodec::Vp9),
              an::test::canWriteVideo({.muxer = "webm", .encoder = "libvpx-vp9", .pixelFormat = "yuv420p"}));
    EXPECT_FALSE(an::test::canWriteVideo({.muxer = "mp4", .encoder = "libx264", .pixelFormat = "rgba64be"}));
    EXPECT_FALSE(an::isFfmpegEncoderAvailable(static_cast<an::FfmpegVideoCodec>(3)));
}

TEST_F(FfmpegSinkTest, EveryCodingAndContainerWritesFramesTheSourceReadsBackInOrder)
{
    struct Pairing
    {
        an::FfmpegVideoCodec codec;
        an::FfmpegVideoContainer container;
        const char* codecName;
    };
    constexpr std::array<Pairing, 7u> kPairings{
        {{an::FfmpegVideoCodec::H264, an::FfmpegVideoContainer::Mp4, "h264"},
         {an::FfmpegVideoCodec::H264, an::FfmpegVideoContainer::Matroska, "h264"},
         {an::FfmpegVideoCodec::H265, an::FfmpegVideoContainer::Mp4, "hevc"},
         {an::FfmpegVideoCodec::H265, an::FfmpegVideoContainer::Matroska, "hevc"},
         {an::FfmpegVideoCodec::Vp9, an::FfmpegVideoContainer::WebM, "vp9"},
         {an::FfmpegVideoCodec::Vp9, an::FfmpegVideoContainer::Mp4, "vp9"},
         {an::FfmpegVideoCodec::Vp9, an::FfmpegVideoContainer::Matroska, "vp9"}}};
    constexpr std::array<int, 5u> kLevels{30, 70, 110, 150, 190};
    const auto frame = describe(64u, 48u);
    std::vector<VideoFrame> frames;
    for (const int level : kLevels)
        frames.push_back(flatFrame(frame, level));
    std::size_t written = 0u;
    for (const auto& pairing : kPairings)
    {
        if (!an::isFfmpegEncoderAvailable(pairing.codec))
        {
            std::cout << "[ SKIPPED  ] this build has no encoder for " << pairing.codecName << '\n';
            continue;
        }
        SCOPED_TRACE(std::string(pairing.codecName) + extensionOf(pairing.container));
        const auto published = writeVideo(std::string("coding") + extensionOf(pairing.container),
                                          videoDescriptor(frame, pairing.container),
                                          {.codec = pairing.codec, .container = pairing.container}, frames);
        auto info = an::inspectFfmpegFile(engine, execution, published.path, kLuma);
        ASSERT_TRUE(info.hasValue()) << info.status().message();
        EXPECT_EQ(info->codecName, pairing.codecName);
        EXPECT_EQ(info->format, pairing.container == an::FfmpegVideoContainer::Mp4 ? an::SequenceFormat::Mp4
                                                                                   : an::SequenceFormat::Video);
        EXPECT_EQ(info->frameCount, kLevels.size());
        EXPECT_EQ(info->hiddenPacketCount, 0u);
        EXPECT_EQ(info->colorRange, an::FfmpegColorRange::Limited);
        EXPECT_EQ(info->matrixCoefficients, 2u);
        ASSERT_TRUE(info->nominalFrameRate.has_value());
        EXPECT_EQ(std::uint64_t{info->nominalFrameRate->numerator},
                  25u * std::uint64_t{info->nominalFrameRate->denominator});
        const auto decoded = readFrames(published.path, kLuma);
        ASSERT_EQ(decoded.size(), kLevels.size());
        for (std::size_t index = 0u; index < kLevels.size(); ++index)
            EXPECT_NEAR(meanOf(decoded[index], 0u, 48u), limitedCode(kLevels[index]), 2.0) << "frame " << index;
        ++written;
    }
    EXPECT_GT(written, 0u);
}

TEST_F(FfmpegSinkTest, ColourFramesAreConvertedThroughTheStatedMatrixAndRange)
{
    if (!an::isFfmpegEncoderAvailable(an::FfmpegVideoCodec::H264))
        GTEST_SKIP() << "this build has no libx264";
    const auto frame = describe(64u, 48u, an::ChannelSchema::Rgb);
    // Static, so the captureless lambda below may read it: subscripting a block-scope
    // `constexpr std::array` binds a reference to the object and therefore odr-uses it, which
    // a lambda with no capture-default cannot do.
    static constexpr std::array<int, 3u> kColour{180, 90, 40};
    const VideoFrame colour =
        makeFrame(frame, [](std::uint32_t, std::uint32_t, std::uint32_t c) { return kColour[c]; });
    struct Case
    {
        an::FfmpegEncodeMatrix matrix;
        an::FfmpegEncodeRange range;
        std::uint8_t code;
        double kr;
        double kb;
    };
    for (const auto& conversion :
         {Case{an::FfmpegEncodeMatrix::Bt709, an::FfmpegEncodeRange::Limited, 1u, 0.2126, 0.0722},
          Case{an::FfmpegEncodeMatrix::Bt709, an::FfmpegEncodeRange::Full, 1u, 0.2126, 0.0722},
          Case{an::FfmpegEncodeMatrix::Bt601, an::FfmpegEncodeRange::Limited, 6u, 0.299, 0.114},
          Case{an::FfmpegEncodeMatrix::Bt601, an::FfmpegEncodeRange::Full, 6u, 0.299, 0.114}})
    {
        const bool full = conversion.range == an::FfmpegEncodeRange::Full;
        SCOPED_TRACE(std::to_string(conversion.code) + (full ? " full" : " limited"));
        const auto published = writeVideo(
            "colour.mp4", videoDescriptor(frame, an::FfmpegVideoContainer::Mp4),
            {.quality = an::FfmpegEncodeQuality::Best, .matrix = conversion.matrix, .range = conversion.range},
            {colour, colour, colour});
        auto info = an::inspectFfmpegFile(engine, execution, published.path, {});
        ASSERT_TRUE(info.hasValue()) << info.status().message();
        EXPECT_EQ(info->matrixCoefficients, conversion.code);
        EXPECT_EQ(info->colorRange, full ? an::FfmpegColorRange::Full : an::FfmpegColorRange::Limited);
        EXPECT_EQ(info->conversion, an::FfmpegConversion::ColorMatrix);
        // The luma the stated matrix gives this colour: the two matrices differ by about five codes here.
        const double luma = conversion.kr * kColour[0] + (1.0 - conversion.kr - conversion.kb) * kColour[1] +
                            conversion.kb * kColour[2];
        const auto planes = readFrames(published.path, kLuma);
        ASSERT_EQ(planes.size(), 3u);
        EXPECT_NEAR(meanOf(planes[0], 0u, 48u), full ? luma : limitedCode(luma), 1.5);
        const auto rgb = readFrames(published.path, {});
        ASSERT_EQ(rgb.size(), 3u);
        for (std::uint32_t c = 0u; c < 3u; ++c)
            EXPECT_NEAR(meanOf(rgb[1], 0u, 48u, c), kColour[c], 3.0) << "channel " << c;
    }
}

TEST_F(FfmpegSinkTest, GreyFramesStateTheirRangeAndNoMatrix)
{
    if (!an::isFfmpegEncoderAvailable(an::FfmpegVideoCodec::H264))
        GTEST_SKIP() << "this build has no libx264";
    const auto frame = describe(64u, 48u);
    const auto published =
        writeVideo("grey-full.mp4", videoDescriptor(frame, an::FfmpegVideoContainer::Mp4),
                   {.range = an::FfmpegEncodeRange::Full}, {flatFrame(frame, 100), flatFrame(frame, 100)});
    auto info = an::inspectFfmpegFile(engine, execution, published.path, kLuma);
    ASSERT_TRUE(info.hasValue()) << info.status().message();
    EXPECT_EQ(info->colorRange, an::FfmpegColorRange::Full);
    EXPECT_EQ(info->matrixCoefficients, 2u);
    // No matrix was applied, so reading the stream as RGB needs one to be assumed.
    EXPECT_EQ(parameterOf(an::inspectFfmpegFile(engine, execution, published.path, {}).status()), "matrixCoefficients");
    const auto luma = readFrames(published.path, kLuma);
    ASSERT_EQ(luma.size(), 2u);
    EXPECT_NEAR(meanOf(luma[0], 0u, 48u), 100.0, 2.0);
    const auto rgb = readFrames(published.path, {.unspecifiedMatrix = an::FfmpegMatrixAssumption::Bt709});
    ASSERT_EQ(rgb.size(), 2u);
    for (std::uint32_t c = 0u; c < 3u; ++c)
        EXPECT_NEAR(meanOf(rgb[0], 0u, 48u, c), 100.0, 2.0);
}

TEST_F(FfmpegSinkTest, GreyLevelsBecomeTheirExactLumaCodes)
{
    // A limited-range grey frame of every level and a full-range grey frame that already holds each level's exact
    // limited-range code hand the encoder the same picture, so both decode to the same luma byte for byte - a
    // comparison no lossy coding can blur, which a rounding or offset in the mapping would fail.
    const auto frame = describe(256u, 16u);
    const VideoFrame levels = makeFrame(frame, [](std::uint32_t x, std::uint32_t, std::uint32_t) { return int(x); });
    const VideoFrame codes = makeFrame(frame, [](std::uint32_t x, std::uint32_t, std::uint32_t) {
        return static_cast<int>(std::lround(limitedCode(static_cast<double>(x))));
    });
    struct Pairing
    {
        an::FfmpegVideoCodec codec;
        an::FfmpegVideoContainer container;
        const char* name;
    };
    std::size_t compared = 0u;
    for (const auto& pairing : {Pairing{an::FfmpegVideoCodec::H264, an::FfmpegVideoContainer::Mp4, "h264"},
                                Pairing{an::FfmpegVideoCodec::Vp9, an::FfmpegVideoContainer::WebM, "vp9"}})
    {
        if (!an::isFfmpegEncoderAvailable(pairing.codec))
        {
            std::cout << "[ SKIPPED  ] this build has no encoder for " << pairing.name << '\n';
            continue;
        }
        SCOPED_TRACE(pairing.name);
        const auto descriptor = videoDescriptor(frame, pairing.container);
        const an::FfmpegSinkRequest limited{
            .codec = pairing.codec, .container = pairing.container, .quality = an::FfmpegEncodeQuality::Best};
        an::FfmpegSinkRequest full = limited;
        full.range = an::FfmpegEncodeRange::Full;
        const auto fromLevels = readFrames(
            writeVideo(std::string("levels") + extensionOf(pairing.container), descriptor, limited, {levels, levels})
                .path,
            kLuma);
        const auto fromCodes = readFrames(
            writeVideo(std::string("codes") + extensionOf(pairing.container), descriptor, full, {codes, codes}).path,
            kLuma);
        ASSERT_EQ(fromLevels.size(), 2u);
        ASSERT_EQ(fromCodes.size(), 2u);
        for (std::size_t index = 0u; index < 2u; ++index)
            for (std::uint32_t y = 0u; y < 16u; ++y)
            {
                const auto left = fromLevels[index].frame.row(0u, y);
                const auto right = fromCodes[index].frame.row(0u, y);
                ASSERT_TRUE(left.has_value() && right.has_value());
                EXPECT_TRUE(std::equal(left->begin(), left->end(), right->begin(), right->end()))
                    << "frame " << index << " row " << y;
            }
        ++compared;
    }
    EXPECT_GT(compared, 0u);
}

TEST_F(FfmpegSinkTest, FramesArePresentedUprightFromEitherOrigin)
{
    if (!an::isFfmpegEncoderAvailable(an::FfmpegVideoCodec::H264))
        GTEST_SKIP() << "this build has no libx264";
    const PictureValue split = [](std::uint32_t, std::uint32_t row, std::uint32_t c) {
        return row < 24u ? 200 - 40 * static_cast<int>(c) : 30 + 20 * static_cast<int>(c);
    };
    for (const auto origin : {an::CoordinateOrigin::TopLeft, an::CoordinateOrigin::BottomLeft})
    {
        SCOPED_TRACE(static_cast<int>(origin));
        const auto grey = describe(64u, 48u, an::ChannelSchema::Mono, origin);
        const auto greyVideo = writeVideo("upright-grey.mp4", videoDescriptor(grey, an::FfmpegVideoContainer::Mp4), {},
                                          {makeFrame(grey, split), makeFrame(grey, split)});
        const auto luma = readFrames(greyVideo.path, kLuma);
        ASSERT_EQ(luma.size(), 2u);
        EXPECT_NEAR(meanOf(luma[1], 2u, 18u), limitedCode(200.0), 4.0);
        EXPECT_NEAR(meanOf(luma[1], 28u, 18u), limitedCode(30.0), 4.0);

        const auto colour = describe(64u, 48u, an::ChannelSchema::Rgb, origin);
        const auto colourVideo =
            writeVideo("upright-colour.mp4", videoDescriptor(colour, an::FfmpegVideoContainer::Mp4),
                       {.quality = an::FfmpegEncodeQuality::Best}, {makeFrame(colour, split, true, 3u)});
        const auto rgb = readFrames(colourVideo.path, {});
        ASSERT_EQ(rgb.size(), 1u);
        for (std::uint32_t c = 0u; c < 3u; ++c)
        {
            EXPECT_NEAR(meanOf(rgb[0], 2u, 18u, c), 200.0 - 40.0 * c, 6.0) << "top channel " << c;
            EXPECT_NEAR(meanOf(rgb[0], 28u, 18u, c), 30.0 + 20.0 * c, 6.0) << "bottom channel " << c;
        }
    }
}

TEST_F(FfmpegSinkTest, PlanarPaddedAndBottomUpFramesEncodeLikeInterleavedOnes)
{
    if (!an::isFfmpegEncoderAvailable(an::FfmpegVideoCodec::H264))
        GTEST_SKIP() << "this build has no libx264";
    const PictureValue gradient = [](std::uint32_t x, std::uint32_t row, std::uint32_t c) {
        return static_cast<int>(x * 3u + row * 5u + c * 40u);
    };
    const auto topDown = describe(64u, 48u, an::ChannelSchema::Rgb);
    const auto bottomUp = describe(64u, 48u, an::ChannelSchema::Rgb, an::CoordinateOrigin::BottomLeft);
    const auto descriptor = videoDescriptor(topDown, an::FfmpegVideoContainer::Mp4);
    const auto reference = writeVideo("interleaved.mp4", descriptor, {}, {makeFrame(topDown, gradient)});
    ASSERT_FALSE(reference.file.empty());
    EXPECT_EQ(writeVideo("padded.mp4", descriptor, {}, {makeFrame(topDown, gradient, false, 3u)}).file, reference.file);
    EXPECT_EQ(writeVideo("planar.mp4", descriptor, {}, {makeFrame(topDown, gradient, true, 2u)}).file, reference.file);
    EXPECT_EQ(writeVideo("bottom-up.mp4", videoDescriptor(bottomUp, an::FfmpegVideoContainer::Mp4), {},
                         {makeFrame(bottomUp, gradient, true, 1u)})
                  .file,
              reference.file);
}

TEST_F(FfmpegSinkTest, AnMp4StreamKeepsItsLastFrame)
{
    const auto frame = describe(64u, 48u);
    std::vector<VideoFrame> frames;
    for (int index = 0; index < 13; ++index)
        frames.push_back(flatFrame(frame, 20 + 15 * index));
    std::size_t written = 0u;
    for (const auto codec : {an::FfmpegVideoCodec::H264, an::FfmpegVideoCodec::H265, an::FfmpegVideoCodec::Vp9})
    {
        if (!an::isFfmpegEncoderAvailable(codec))
            continue;
        SCOPED_TRACE(static_cast<int>(codec));
        const auto published = writeVideo("last-frame.mp4", videoDescriptor(frame, an::FfmpegVideoContainer::Mp4),
                                          {.codec = codec}, frames);
        auto info = an::inspectFfmpegFile(engine, execution, published.path, kLuma);
        ASSERT_TRUE(info.hasValue()) << info.status().message();
        EXPECT_EQ(info->frameCount, frames.size());
        EXPECT_EQ(info->hiddenPacketCount, 0u);
        const auto decoded = readFrames(published.path, kLuma);
        ASSERT_EQ(decoded.size(), frames.size());
        EXPECT_NEAR(meanOf(decoded.back(), 0u, 48u), limitedCode(20.0 + 15.0 * 12.0), 2.0);
        ++written;
    }
    EXPECT_GT(written, 0u);
}

TEST_F(FfmpegSinkTest, QualityLevelsFollowTheirTable)
{
    constexpr std::array<an::FfmpegEncodeQuality, 5u> kLevels{
        an::FfmpegEncodeQuality::Draft, an::FfmpegEncodeQuality::Standard, an::FfmpegEncodeQuality::Good,
        an::FfmpegEncodeQuality::High, an::FfmpegEncodeQuality::Best};
    const auto frame = describe(64u, 48u);
    const VideoFrame textured = makeFrame(frame, [](std::uint32_t x, std::uint32_t row, std::uint32_t) {
        return static_cast<int>((x * 37u + row * 101u + (x * row) % 23u * 11u) % 251u);
    });
    const std::vector<VideoFrame> frames{textured, textured, textured};
    // libx264 and libx265 write their settings into the stream, so the factor each level chose is readable there.
    constexpr std::array<int, 5u> kH264{28, 26, 24, 22, 20};
    constexpr std::array<int, 5u> kH265{32, 29, 26, 23, 20};
    for (std::size_t level = 0u; level < kLevels.size(); ++level)
    {
        if (an::isFfmpegEncoderAvailable(an::FfmpegVideoCodec::H264))
        {
            const auto h264 = writeVideo("quality.mp4", videoDescriptor(frame, an::FfmpegVideoContainer::Mp4),
                                         {.quality = kLevels[level]}, frames);
            EXPECT_TRUE(contains(h264.file, "crf=" + std::to_string(kH264[level]) + ".0")) << "H.264 level " << level;
        }
        if (an::isFfmpegEncoderAvailable(an::FfmpegVideoCodec::H265))
        {
            const auto h265 = writeVideo("quality.mp4", videoDescriptor(frame, an::FfmpegVideoContainer::Mp4),
                                         {.codec = an::FfmpegVideoCodec::H265, .quality = kLevels[level]}, frames);
            EXPECT_TRUE(contains(h265.file, "crf=" + std::to_string(kH265[level]) + ".0")) << "H.265 level " << level;
        }
    }
    if (!an::isFfmpegEncoderAvailable(an::FfmpegVideoCodec::Vp9))
        return;
    // VP9 does not say, so each level has to cost at least what the one below it cost.
    std::vector<std::size_t> sizes;
    for (const auto level : kLevels)
        sizes.push_back(
            writeVideo(
                "quality.webm", videoDescriptor(frame, an::FfmpegVideoContainer::WebM),
                {.codec = an::FfmpegVideoCodec::Vp9, .container = an::FfmpegVideoContainer::WebM, .quality = level},
                frames)
                .file.size());
    for (std::size_t level = 1u; level < sizes.size(); ++level)
        EXPECT_GE(sizes[level], sizes[level - 1u]) << "level " << level;
    EXPECT_LT(sizes.front(), sizes.back());
}

TEST_F(FfmpegSinkTest, TheSameRequestWritesTheSameBytes)
{
    const auto frame = describe(64u, 48u, an::ChannelSchema::Rgb);
    const VideoFrame picture = makeFrame(frame, [](std::uint32_t x, std::uint32_t row, std::uint32_t c) {
        return static_cast<int>((x * 11u + row * 7u + c * 50u) % 256u);
    });
    const std::vector<VideoFrame> frames{picture, picture, picture, picture};
    if (an::isFfmpegEncoderAvailable(an::FfmpegVideoCodec::H264))
    {
        const auto descriptor = videoDescriptor(frame, an::FfmpegVideoContainer::Matroska);
        const an::FfmpegSinkRequest request{.container = an::FfmpegVideoContainer::Matroska};
        const auto first = writeVideo("repeat.mkv", descriptor, request, frames);
        ASSERT_FALSE(first.file.empty());
        EXPECT_EQ(writeVideo("repeat.mkv", descriptor, request, frames).file, first.file);
    }
    if (an::isFfmpegEncoderAvailable(an::FfmpegVideoCodec::Vp9))
    {
        // libvpx-vp9 writes the same stream at every thread count, so the whole container repeats too.
        const auto descriptor = videoDescriptor(frame, an::FfmpegVideoContainer::WebM);
        const auto one = writeVideo(
            "threads.webm", descriptor,
            {.codec = an::FfmpegVideoCodec::Vp9, .container = an::FfmpegVideoContainer::WebM, .encoderThreads = 1u},
            frames);
        ASSERT_FALSE(one.file.empty());
        EXPECT_EQ(
            writeVideo(
                "threads.webm", descriptor,
                {.codec = an::FfmpegVideoCodec::Vp9, .container = an::FfmpegVideoContainer::WebM, .encoderThreads = 2u},
                frames)
                .file,
            one.file);
    }
}

TEST_F(FfmpegSinkTest, EncoderThreadsAreLeasedForTheSinksLife)
{
    if (!an::isFfmpegEncoderAvailable(an::FfmpegVideoCodec::Vp9))
        GTEST_SKIP() << "this build has no libvpx-vp9";
    auto wide = an::EngineContext::create({.maxConcurrency = 8u});
    ASSERT_TRUE(wide.hasValue());
    const auto frame = describe(64u, 48u);
    const auto descriptor = videoDescriptor(frame, an::FfmpegVideoContainer::WebM);
    const an::FfmpegSinkRequest threeThreads{
        .codec = an::FfmpegVideoCodec::Vp9, .container = an::FfmpegVideoContainer::WebM, .encoderThreads = 3u};
    {
        auto control = an::ExecutionContext::create(*wide, {.maxConcurrency = 1u});
        ASSERT_TRUE(control.hasValue());
        const std::uint32_t available = wide->availableConcurrency();
        ASSERT_EQ(available, 7u);
        auto output = an::SequenceOutput::begin(*wide, *control, {.destinationPath = pathOf("leased.webm")});
        ASSERT_TRUE(output.hasValue());
        auto sink = an::createFfmpegSink(*wide, *control, *output, descriptor, threeThreads);
        ASSERT_TRUE(sink.hasValue()) << sink.status().message();
        EXPECT_EQ(wide->availableConcurrency(), available - 3u);
        const VideoFrame picture = flatFrame(frame, 90);
        ASSERT_TRUE(sink->append(*wide, *control, borrowed(picture, 0u)).hasValue());
        ASSERT_TRUE(sink->append(*wide, *control, borrowed(picture, 1u)).hasValue());
        EXPECT_EQ(wide->availableConcurrency(), available - 3u);
        auto finished = sink->finish(*control);
        ASSERT_TRUE(finished.hasValue()) << finished.status().message();
        EXPECT_EQ(wide->availableConcurrency(), available);
        ASSERT_TRUE(output->commit(*control, *finished).hasValue());
        EXPECT_TRUE(sink->close().ok());

        auto tooMany = an::SequenceOutput::begin(*wide, *control, {.destinationPath = pathOf("too-many.webm")});
        ASSERT_TRUE(tooMany.hasValue());
        auto refused = an::createFfmpegSink(
            *wide, *control, *tooMany, descriptor,
            {.codec = an::FfmpegVideoCodec::Vp9, .container = an::FfmpegVideoContainer::WebM, .encoderThreads = 9u});
        EXPECT_EQ(refused.status().code(), an::ErrorCode::LimitExceeded);
        EXPECT_EQ(parameterOf(refused.status()), "encoderThreads");

        auto unfinished = an::createFfmpegSink(*wide, *control, *tooMany, descriptor, threeThreads);
        ASSERT_TRUE(unfinished.hasValue()) << unfinished.status().message();
        EXPECT_EQ(wide->availableConcurrency(), available - 3u);
        EXPECT_TRUE(unfinished->close().ok());
        EXPECT_EQ(wide->availableConcurrency(), available);
        tooMany->abort();
    }
    {
        auto busy = an::ExecutionContext::create(*wide, {.maxConcurrency = 6u});
        ASSERT_TRUE(busy.hasValue());
        ASSERT_EQ(wide->availableConcurrency(), 2u);
        auto output = an::SequenceOutput::begin(*wide, *busy, {.destinationPath = pathOf("busy.webm")});
        ASSERT_TRUE(output.hasValue());
        const Bytes sentinel{std::byte{4}, std::byte{5}};
        overwrite(output->pendingPath(), sentinel);
        auto refused = an::createFfmpegSink(*wide, *busy, *output, descriptor, threeThreads);
        EXPECT_EQ(refused.status().code(), an::ErrorCode::Busy);
        EXPECT_EQ(parameterOf(refused.status()), "encoderThreads");
        EXPECT_EQ(wide->availableConcurrency(), 2u);
        EXPECT_EQ(readAll(output->pendingPath()), sentinel);
        output->abort();
    }
    EXPECT_TRUE(wide->shutdown().ok());
}

TEST_F(FfmpegSinkTest, RefusesWhatAStreamCannotStoreBeforeWritingAByte)
{
    const auto frame = describe(64u, 48u);
    const auto mp4 = videoDescriptor(frame, an::FfmpegVideoContainer::Mp4);
    struct Case
    {
        const char* name;
        an::SequenceDescriptor descriptor;
        an::FfmpegSinkRequest request;
        an::ErrorCode code;
        const char* parameter;
    };
    auto changed = [](an::SequenceDescriptor descriptor, void (*change)(an::SequenceDescriptor&)) {
        change(descriptor);
        return descriptor;
    };
    const std::vector<Case> cases{
        {"MP4 described as AVI",
         changed(mp4, [](auto& d) { d.format = an::SequenceFormat::Avi; }),
         {},
         an::ErrorCode::InvalidArgument,
         "format"},
        {"Matroska described as MP4",
         mp4,
         {.container = an::FfmpegVideoContainer::Matroska},
         an::ErrorCode::InvalidArgument,
         "format"},
        {"no fixed frame",
         changed(mp4, [](auto& d) { d.fixedFrame.reset(); }),
         {},
         an::ErrorCode::InvalidArgument,
         "fixedFrame"},
        {"seek",
         changed(mp4, [](auto& d) { d.seekByIndex = true; }),
         {},
         an::ErrorCode::InvalidArgument,
         "seekByIndex"},
        {"no rate",
         changed(mp4, [](auto& d) { d.nominalFrameRate.reset(); }),
         {},
         an::ErrorCode::InvalidArgument,
         "nominalFrameRate"},
        {"timestamps",
         changed(mp4,
                 [](auto& d) {
                     d.timeBase = an::SequenceRate{1u, 25u};
                     d.timestampsRequired = true;
                 }),
         {},
         an::ErrorCode::Unsupported,
         "timestampsRequired"},
        {"H.264 in WebM",
         videoDescriptor(frame, an::FfmpegVideoContainer::WebM),
         {.container = an::FfmpegVideoContainer::WebM},
         an::ErrorCode::Unsupported,
         "codec"},
        {"H.265 in WebM",
         videoDescriptor(frame, an::FfmpegVideoContainer::WebM),
         {.codec = an::FfmpegVideoCodec::H265, .container = an::FfmpegVideoContainer::WebM},
         an::ErrorCode::Unsupported,
         "codec"},
        {"rate",
         videoDescriptor(frame, an::FfmpegVideoContainer::Mp4, {2147483648u, 1u}),
         {},
         an::ErrorCode::LimitExceeded,
         "nominalFrameRate"},
        {"odd width",
         videoDescriptor(describe(63u, 48u), an::FfmpegVideoContainer::Mp4),
         {},
         an::ErrorCode::Unsupported,
         "extent"},
        {"odd height",
         videoDescriptor(describe(64u, 47u), an::FfmpegVideoContainer::Mp4),
         {},
         an::ErrorCode::Unsupported,
         "extent"},
        {"sixteen bits",
         videoDescriptor(describe(64u, 48u, an::ChannelSchema::Mono, an::CoordinateOrigin::TopLeft, {}, 2u),
                         an::FfmpegVideoContainer::Mp4),
         {},
         an::ErrorCode::Unsupported,
         "sampleFormat"},
        {"alpha",
         videoDescriptor(describe(64u, 48u, an::ChannelSchema::Rgba), an::FfmpegVideoContainer::Mp4),
         {},
         an::ErrorCode::Unsupported,
         "channelSchema"},
        {"mosaic",
         videoDescriptor(describe(64u, 48u, an::ChannelSchema::Mono, an::CoordinateOrigin::TopLeft,
                                  *an::CfaDescriptor::create(an::CfaPattern::Bggr, 0u, 0u)),
                         an::FfmpegVideoContainer::Mp4),
         {},
         an::ErrorCode::Unsupported,
         "cfa"},
        {"pixel shape",
         videoDescriptor(describe(64u, 48u, an::ChannelSchema::Mono, an::CoordinateOrigin::TopLeft, {}, 1u,
                                  an::RawSampleClass::UnsignedInteger, 1.5),
                         an::FfmpegVideoContainer::Mp4),
         {},
         an::ErrorCode::Unsupported,
         "pixelAspectRatio"},
        {"codec", mp4, {.codec = static_cast<an::FfmpegVideoCodec>(3)}, an::ErrorCode::InvalidArgument, "codec"},
        {"container",
         mp4,
         {.container = static_cast<an::FfmpegVideoContainer>(3)},
         an::ErrorCode::InvalidArgument,
         "container"},
        {"quality",
         mp4,
         {.quality = static_cast<an::FfmpegEncodeQuality>(5)},
         an::ErrorCode::InvalidArgument,
         "quality"},
        {"matrix", mp4, {.matrix = static_cast<an::FfmpegEncodeMatrix>(2)}, an::ErrorCode::InvalidArgument, "matrix"},
        {"range", mp4, {.range = static_cast<an::FfmpegEncodeRange>(2)}, an::ErrorCode::InvalidArgument, "range"},
        {"policy",
         mp4,
         {.mosaicFrames = static_cast<an::FfmpegMosaicPolicy>(2)},
         an::ErrorCode::InvalidArgument,
         "mosaicFrames"},
        {"no threads", mp4, {.encoderThreads = 0u}, an::ErrorCode::InvalidArgument, "encoderThreads"},
        {"too many threads", mp4, {.encoderThreads = 99u}, an::ErrorCode::LimitExceeded, "encoderThreads"},
    };
    for (const auto& refusal : cases)
    {
        SCOPED_TRACE(refusal.name);
        auto output = an::SequenceOutput::begin(engine, execution, {.destinationPath = pathOf("refused.mp4")});
        ASSERT_TRUE(output.hasValue()) << output.status().message();
        const Bytes sentinel{std::byte{9}, std::byte{8}, std::byte{7}};
        overwrite(output->pendingPath(), sentinel);
        auto sink = an::createFfmpegSink(engine, execution, *output, refusal.descriptor, refusal.request);
        ASSERT_FALSE(sink.hasValue());
        EXPECT_EQ(sink.status().code(), refusal.code) << sink.status().message();
        EXPECT_EQ(parameterOf(sink.status()), refusal.parameter);
        EXPECT_EQ(readAll(output->pendingPath()), sentinel);
        auto claim = output->claimWriter();
        EXPECT_TRUE(claim.hasValue());
        claim->release();
        output->abort();
    }
}

TEST_F(FfmpegSinkTest, MosaicFramesAreEncodedAsGreyOnlyWhenAskedTo)
{
    if (!an::isFfmpegEncoderAvailable(an::FfmpegVideoCodec::H264))
        GTEST_SKIP() << "this build has no libx264";
    const auto frame = describe(64u, 48u, an::ChannelSchema::Mono, an::CoordinateOrigin::TopLeft,
                                *an::CfaDescriptor::create(an::CfaPattern::Rggb, 0u, 1u));
    const auto descriptor = videoDescriptor(frame, an::FfmpegVideoContainer::Mp4);
    auto output = an::SequenceOutput::begin(engine, execution, {.destinationPath = pathOf("mosaic.mp4")});
    ASSERT_TRUE(output.hasValue());
    EXPECT_EQ(parameterOf(an::createFfmpegSink(engine, execution, *output, descriptor, {}).status()), "cfa");
    output->abort();
    const auto published =
        writeVideo("mosaic.mp4", descriptor, {.mosaicFrames = an::FfmpegMosaicPolicy::WriteSamplesAsMono},
                   {flatFrame(frame, 120), flatFrame(frame, 120)});
    const auto luma = readFrames(published.path, kLuma);
    ASSERT_EQ(luma.size(), 2u);
    EXPECT_NEAR(meanOf(luma[0], 0u, 48u), limitedCode(120.0), 2.0);
}

TEST_F(FfmpegSinkTest, CancellationStopsTheSinkAndLeavesNothing)
{
    if (!an::isFfmpegEncoderAvailable(an::FfmpegVideoCodec::H264))
        GTEST_SKIP() << "this build has no libx264";
    const auto frame = describe(64u, 48u);
    const auto descriptor = videoDescriptor(frame, an::FfmpegVideoContainer::Mp4);
    const VideoFrame picture = flatFrame(frame, 60);

    // A stop before the sink exists changes nothing.
    {
        auto output = an::SequenceOutput::begin(engine, execution, {.destinationPath = pathOf("stopped.mp4")});
        ASSERT_TRUE(output.hasValue());
        const Bytes sentinel{std::byte{1}};
        overwrite(output->pendingPath(), sentinel);
        std::stop_source stop;
        stop.request_stop();
        auto stopped = an::ExecutionContext::create(engine, {.stopToken = stop.get_token(), .maxConcurrency = 1u});
        ASSERT_TRUE(stopped.hasValue());
        EXPECT_EQ(an::createFfmpegSink(engine, *stopped, *output, descriptor, {}).status().code(),
                  an::ErrorCode::Cancelled);
        EXPECT_EQ(readAll(output->pendingPath()), sentinel);
        auto claim = output->claimWriter();
        EXPECT_TRUE(claim.hasValue());
        claim->release();
        output->abort();
    }

    // A stop while frames are being appended fails the append that sees it, and the payload goes with the sink.
    {
        auto output = an::SequenceOutput::begin(engine, execution, {.destinationPath = pathOf("during.mp4")});
        ASSERT_TRUE(output.hasValue());
        auto sink = an::createFfmpegSink(engine, execution, *output, descriptor, {});
        ASSERT_TRUE(sink.hasValue()) << sink.status().message();
        std::stop_source stop;
        SinkTrace trace(stop, "frames-encoded", 2u);
        auto stopping = an::ExecutionContext::create(
            engine, {.stopToken = stop.get_token(), .traceSink = &trace, .maxConcurrency = 1u});
        ASSERT_TRUE(stopping.hasValue());
        std::optional<std::uint64_t> failedAt;
        for (std::uint64_t index = 0u; index < 5u && !failedAt; ++index)
        {
            auto appended = sink->append(engine, *stopping, borrowed(picture, index));
            if (!appended.hasValue())
            {
                EXPECT_EQ(appended.status().code(), an::ErrorCode::Cancelled);
                failedAt = index;
            }
        }
        ASSERT_TRUE(failedAt.has_value());
        EXPECT_EQ(*failedAt, 1u);
        EXPECT_EQ(sink->state(), an::SequenceState::Failed);
        EXPECT_FALSE(sink->finish(execution).hasValue());
        EXPECT_TRUE(sink->close().ok());
        EXPECT_EQ(std::filesystem::file_size(native(output->pendingPath())), 0u);
        output->abort();
    }

    // A finish that is stopped before it starts leaves the sink open, and a later finish completes it.
    {
        auto output = an::SequenceOutput::begin(engine, execution, {.destinationPath = pathOf("finish.mp4")});
        ASSERT_TRUE(output.hasValue());
        auto sink = an::createFfmpegSink(engine, execution, *output, descriptor, {});
        ASSERT_TRUE(sink.hasValue()) << sink.status().message();
        ASSERT_TRUE(sink->append(engine, execution, borrowed(picture, 0u)).hasValue());
        ASSERT_TRUE(sink->append(engine, execution, borrowed(picture, 1u)).hasValue());
        std::stop_source stop;
        stop.request_stop();
        {
            auto stopped = an::ExecutionContext::create(engine, {.stopToken = stop.get_token(), .maxConcurrency = 1u});
            ASSERT_TRUE(stopped.hasValue());
            EXPECT_EQ(sink->finish(*stopped).status().code(), an::ErrorCode::Cancelled);
        }
        EXPECT_EQ(sink->state(), an::SequenceState::Open);
        auto finished = sink->finish(execution);
        ASSERT_TRUE(finished.hasValue()) << finished.status().message();
        ASSERT_TRUE(output->commit(execution, *finished).hasValue());
        EXPECT_TRUE(sink->close().ok());
        EXPECT_EQ(readFrames(pathOf("finish.mp4"), kLuma).size(), 2u);
    }
}

TEST_F(FfmpegSinkTest, AStopWhileFfmpegWritesFailsTheCallThatSeesIt)
{
    if (!an::isFfmpegEncoderAvailable(an::FfmpegVideoCodec::H264))
        GTEST_SKIP() << "this build has no libx264";
    const auto frame = describe(64u, 48u);
    const auto descriptor = videoDescriptor(frame, an::FfmpegVideoContainer::Mp4);
    const VideoFrame picture = flatFrame(frame, 60);
    auto output = an::SequenceOutput::begin(engine, execution, {.destinationPath = pathOf("writing.mp4")});
    ASSERT_TRUE(output.hasValue());
    auto sink = an::createFfmpegSink(engine, execution, *output, descriptor, {});
    ASSERT_TRUE(sink.hasValue()) << sink.status().message();
    for (std::uint64_t index = 0u; index < 8u; ++index)
        ASSERT_TRUE(sink->append(engine, execution, borrowed(picture, index)).hasValue());

    // libx264 still holds the frames, so finishing drains their packets and then writes the container's index. A
    // stop that arrives with the first drained packet is not seen by any check between calls; FFmpeg's next write
    // into the payload has to see it, and the finish fails without publishing a partial container.
    std::stop_source stop;
    SinkTrace trace(stop, "packets-written", 1u);
    auto stopping = an::ExecutionContext::create(
        engine, {.stopToken = stop.get_token(), .traceSink = &trace, .maxConcurrency = 1u});
    ASSERT_TRUE(stopping.hasValue());
    auto finished = sink->finish(*stopping);
    ASSERT_FALSE(finished.hasValue());
    EXPECT_EQ(finished.status().code(), an::ErrorCode::Cancelled);
    EXPECT_EQ(sink->state(), an::SequenceState::Failed);
    EXPECT_TRUE(sink->close().ok());
    EXPECT_EQ(std::filesystem::file_size(native(output->pendingPath())), 0u);
    output->abort();
}

TEST_F(FfmpegSinkTest, AnUnfinishedSinkDiscardsItsPayloadAndReturnsTheClaim)
{
    if (!an::isFfmpegEncoderAvailable(an::FfmpegVideoCodec::H264))
        GTEST_SKIP() << "this build has no libx264";
    const auto frame = describe(64u, 48u);
    const auto destination = pathOf("unfinished.mp4");
    auto output = an::SequenceOutput::begin(engine, execution, {.destinationPath = destination});
    ASSERT_TRUE(output.hasValue());
    {
        auto sink =
            an::createFfmpegSink(engine, execution, *output, videoDescriptor(frame, an::FfmpegVideoContainer::Mp4), {});
        ASSERT_TRUE(sink.hasValue()) << sink.status().message();
        const VideoFrame picture = flatFrame(frame, 77);
        ASSERT_TRUE(sink->append(engine, execution, borrowed(picture, 0u)).hasValue());
        ASSERT_TRUE(sink->append(engine, execution, borrowed(picture, 1u)).hasValue());
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

TEST_F(FfmpegSinkTest, IndependentSinksEncodeConcurrently)
{
    if (!an::isFfmpegEncoderAvailable(an::FfmpegVideoCodec::H264))
        GTEST_SKIP() << "this build has no libx264";
    auto wide = an::EngineContext::create({.maxConcurrency = 8u});
    ASSERT_TRUE(wide.hasValue());
    constexpr std::size_t kSinks = 4u;
    const auto frame = describe(64u, 48u);
    std::array<std::string, kSinks> failures;
    std::array<std::string, kSinks> paths;
    std::vector<std::thread> threads;
    for (std::size_t sinkIndex = 0u; sinkIndex < kSinks; ++sinkIndex)
    {
        paths[sinkIndex] = pathOf("concurrent-" + std::to_string(sinkIndex) + ".mp4");
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
            auto sink = an::createFfmpegSink(*wide, *workerExecution, *output,
                                             videoDescriptor(frame, an::FfmpegVideoContainer::Mp4), {});
            if (!sink.hasValue())
            {
                failures[sinkIndex] = std::string("create: ") + sink.status().message();
                return;
            }
            for (std::uint64_t index = 0u; index < 3u; ++index)
            {
                const VideoFrame picture = flatFrame(frame, static_cast<int>(40u + 30u * sinkIndex + 10u * index));
                if (!sink->append(*wide, *workerExecution, borrowed(picture, index)).hasValue())
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
        const auto luma = readFrames(paths[sinkIndex], kLuma);
        ASSERT_EQ(luma.size(), 3u);
        for (std::size_t index = 0u; index < 3u; ++index)
            EXPECT_NEAR(meanOf(luma[index], 0u, 48u), limitedCode(40.0 + 30.0 * sinkIndex + 10.0 * index), 2.0);
    }
    EXPECT_TRUE(wide->shutdown().ok());
}

TEST_F(FfmpegSinkTest, UnicodeDestinationsArePublished)
{
    if (!an::isFfmpegEncoderAvailable(an::FfmpegVideoCodec::H264))
        GTEST_SKIP() << "this build has no libx264";
    const auto frame = describe(64u, 48u);
    const auto published =
        writeVideo(std::filesystem::path(u8"видео-映像.mp4"), videoDescriptor(frame, an::FfmpegVideoContainer::Mp4), {},
                   {flatFrame(frame, 150)});
    ASSERT_TRUE(std::filesystem::exists(native(published.path)));
    const auto luma = readFrames(published.path, kLuma);
    ASSERT_EQ(luma.size(), 1u);
    EXPECT_NEAR(meanOf(luma[0], 0u, 48u), limitedCode(150.0), 2.0);
}
} // namespace

namespace
{
/// A valid contiguous buffer for a layout whose padding consumes address space, not memory. A stride of gigabytes
/// spans more than a small machine holds, so nothing of the padding may be charged against it: Linux maps the whole
/// range without reserving swap, and Windows reserves it and commits only the pages the layout's rows occupy.
class PaddedVideoMemory
{
public:
    PaddedVideoMemory(const PaddedVideoMemory&) = delete;
    PaddedVideoMemory& operator=(const PaddedVideoMemory&) = delete;
    explicit PaddedVideoMemory(const an::ImageLayout& layout) : m_size(static_cast<std::size_t>(layout.totalBytes()))
    {
#ifdef _WIN32
        m_data = static_cast<std::byte*>(VirtualAlloc(nullptr, m_size, MEM_RESERVE, PAGE_READWRITE));
        if (m_data == nullptr)
            return;
        for (std::uint32_t plane = 0u; plane < layout.planeCount(); ++plane)
            for (std::uint32_t y = 0u; y < layout.extent().height(); ++y)
            {
                const auto offset = static_cast<std::size_t>(*layout.rowByteOffset(plane, y));
                if (VirtualAlloc(m_data + offset, static_cast<std::size_t>(layout.minimumRowBytes()), MEM_COMMIT,
                                 PAGE_READWRITE) == nullptr)
                {
                    VirtualFree(m_data, 0u, MEM_RELEASE);
                    m_data = nullptr;
                    return;
                }
            }
#else
        void* memory =
            mmap(nullptr, m_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        m_data = memory == MAP_FAILED ? nullptr : static_cast<std::byte*>(memory);
#endif
    }
    ~PaddedVideoMemory()
    {
        if (m_data == nullptr)
            return;
#ifdef _WIN32
        EXPECT_TRUE(VirtualFree(m_data, 0u, MEM_RELEASE));
#else
        EXPECT_EQ(munmap(m_data, m_size), 0);
#endif
    }
    std::byte* data() const
    {
        return m_data;
    }

private:
    /// The bytes the range spans, which `munmap` needs back; `VirtualFree` with `MEM_RELEASE` takes none.
    std::size_t m_size;
    std::byte* m_data = nullptr;
};

/// A coding, its container, its software encoder and every encoder a device preference tries, in order.
struct CodingCase
{
    an::FfmpegVideoCodec codec;
    an::FfmpegVideoContainer container;
    std::string software;
    std::vector<std::string> candidates;
};

std::vector<CodingCase> codingCases()
{
    return {{an::FfmpegVideoCodec::H264,
             an::FfmpegVideoContainer::Mp4,
             "libx264",
             {"h264_nvenc", "h264_qsv", "h264_amf", "h264_vaapi", "h264_videotoolbox", "libx264"}},
            {an::FfmpegVideoCodec::H265,
             an::FfmpegVideoContainer::Matroska,
             "libx265",
             {"hevc_nvenc", "hevc_qsv", "hevc_amf", "hevc_vaapi", "hevc_videotoolbox", "libx265"}},
            {an::FfmpegVideoCodec::Vp9, an::FfmpegVideoContainer::WebM, "libvpx-vp9", {"vp9_vaapi", "libvpx-vp9"}}};
}

std::vector<VideoFrame> gradientFrames(const an::RawFrameDescriptor& descriptor, int count)
{
    std::vector<VideoFrame> frames;
    for (int index = 0; index < count; ++index)
        frames.push_back(makeFrame(descriptor, [index](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
            return static_cast<int>(x * 3u + y * 5u + c * 40u) + index * 7;
        }));
    return frames;
}

TEST_F(FfmpegSinkTest, TheProcessorEncodesUnlessADeviceIsPreferredAndTheReportNamesTheEncoder)
{
    const an::RawFrameDescriptor frame = describe(64u, 48u, an::ChannelSchema::Rgb);
    const std::vector<VideoFrame> frames = gradientFrames(frame, 6);
    for (const CodingCase& coding : codingCases())
    {
        if (!an::isFfmpegEncoderAvailable(coding.codec))
            continue;
        an::FfmpegEncoderReport report;
        report.encoderName = "unchanged";
        const Published written =
            writeReportedVideo(std::string("processor-") + coding.software + extensionOf(coding.container),
                               videoDescriptor(frame, coding.container),
                               {.codec = coding.codec, .container = coding.container}, frames, report);
        ASSERT_FALSE(written.file.empty()) << coding.software;
        EXPECT_EQ(report.encoderName, coding.software);
        EXPECT_FALSE(report.device);
        EXPECT_TRUE(report.refused.empty());
    }

    an::FfmpegEncoderReport untouched;
    untouched.encoderName = "unchanged";
    auto output = an::SequenceOutput::begin(engine, execution, {.destinationPath = pathOf("refused.mp4")});
    ASSERT_TRUE(output.hasValue());
    auto refused =
        an::createFfmpegSink(engine, execution, *output, videoDescriptor(frame, an::FfmpegVideoContainer::Mp4),
                             {.encoderPreference = static_cast<an::FfmpegEncoderPreference>(9)}, untouched);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(parameterOf(refused.status()), "encoderPreference");
    EXPECT_EQ(untouched.encoderName, "unchanged") << "a refused sink leaves the report as it was";
}

TEST_F(FfmpegSinkTest, APreferredDeviceIsTriedInItsStatedOrderAndARefusalPassesToTheNext)
{
    const an::RawFrameDescriptor frame = describe(320u, 240u, an::ChannelSchema::Rgb);
    const std::vector<VideoFrame> frames = gradientFrames(frame, 6);
    std::string openedEncoders;
    std::string refusedEncoders;
    for (const CodingCase& coding : codingCases())
    {
        if (!an::isFfmpegEncoderAvailable(coding.codec))
            continue;
        const an::SequenceDescriptor descriptor = videoDescriptor(frame, coding.container);
        const Published processor =
            writeVideo(std::string("software-") + coding.software + extensionOf(coding.container), descriptor,
                       {.codec = coding.codec, .container = coding.container}, frames);
        an::FfmpegEncoderReport report;
        const Published preferred =
            writeReportedVideo(std::string("device-") + coding.software + extensionOf(coding.container), descriptor,
                               {.codec = coding.codec,
                                .container = coding.container,
                                .encoderPreference = an::FfmpegEncoderPreference::DeviceFirst},
                               frames, report);
        ASSERT_FALSE(preferred.file.empty()) << coding.software;
        const auto opened = std::find(coding.candidates.begin(), coding.candidates.end(), report.encoderName);
        ASSERT_NE(opened, coding.candidates.end()) << report.encoderName;
        EXPECT_EQ(report.device, report.encoderName != coding.software);
        // Every refusal names a candidate tried before the one that opened, in the order they were tried.
        auto next = coding.candidates.begin();
        for (const std::string& refusal : report.refused)
        {
            next = std::find(next, opened, refusal);
            ASSERT_NE(next, opened) << refusal;
            ++next;
        }
        if (!report.device)
            EXPECT_EQ(preferred.file, processor.file)
                << "the software encoder writes one stream whichever way it is reached";
        const std::vector<an::SequenceFrame> delivered = readFrames(preferred.path, kLuma);
        ASSERT_EQ(delivered.size(), frames.size()) << report.encoderName;
        for (const an::SequenceFrame& one : delivered)
        {
            EXPECT_EQ(one.frame.descriptor()->extent().width(), 320u);
            EXPECT_EQ(one.frame.descriptor()->extent().height(), 240u);
        }
        openedEncoders += report.encoderName + " ";
        for (const std::string& refusal : report.refused)
            refusedEncoders += refusal + " ";
    }
    RecordProperty("openedEncoders", openedEncoders);
    RecordProperty("refusedEncoders", refusedEncoders);
    if (const auto required = an::test::requiredVideoDevice("AFTERNIGHT_TEST_REQUIRE_ENCODE_DEVICE"))
    {
        EXPECT_NE((" " + openedEncoders).find(" " + *required + " "), std::string::npos)
            << "AFTERNIGHT_TEST_REQUIRE_ENCODE_DEVICE=" << *required
            << ", but the encoders that opened were: " << openedEncoders;
    }
}

// libx264's AVX-512 macroblock-tree pass once read its buffer past a row whose width in macroblocks is not a multiple
// of eight - 20 here - so what an encode wrote followed whatever the fresh memory it was given held. glibc fills every
// allocation with a stated byte under M_PERTURB, which makes that memory different from one encode to the next.
TEST_F(FfmpegSinkTest, TheSoftwareH264EncoderWritesOneStreamWhateverFreshMemoryHolds)
{
#if !defined(__GLIBC__)
    GTEST_SKIP() << "fresh memory is given stated contents through glibc's M_PERTURB";
#else
    if (!an::isFfmpegEncoderAvailable(an::FfmpegVideoCodec::H264))
        GTEST_SKIP() << "this build has no libx264";
    // A sanitizer's allocator stands in front of glibc's and takes no M_PERTURB, so fresh memory cannot be given
    // stated contents there.
    if (mallopt(M_PERTURB, 0) != 1)
        GTEST_SKIP() << "the allocator in front of glibc's takes no M_PERTURB";
    const an::RawFrameDescriptor frame = describe(320u, 240u, an::ChannelSchema::Rgb);
    const std::vector<VideoFrame> frames = gradientFrames(frame, 6);
    const an::SequenceDescriptor descriptor = videoDescriptor(frame, an::FfmpegVideoContainer::Mp4);
    struct PerturbationReset
    {
        ~PerturbationReset()
        {
            mallopt(M_PERTURB, 0);
        }
    } reset;
    Bytes first;
    for (const int fill : {0x00, 0x01, 0x80, 0xc8})
    {
        ASSERT_EQ(mallopt(M_PERTURB, fill), 1);
        const Published written = writeVideo("perturbed-" + std::to_string(fill) + ".mp4", descriptor,
                                             {.codec = an::FfmpegVideoCodec::H264}, frames);
        mallopt(M_PERTURB, 0);
        ASSERT_FALSE(written.file.empty()) << fill;
        if (first.empty())
            first = written.file;
        else
            EXPECT_EQ(written.file, first) << "the stream changed with the fill " << fill;
    }
#endif
}

} // namespace

TEST_F(FfmpegSinkTest, ColourStridesAreCheckedBeforeNarrowing)
{
    if (!an::isFfmpegEncoderAvailable(an::FfmpegVideoCodec::H264))
        GTEST_SKIP() << "this build has no libx264";
    // The positive wrap goes first: the unfixed implementation accepts it without crashing,
    // so the regression fails before it reaches strides whose narrowing becomes negative.
    constexpr std::array<std::uint64_t, 4u> kStrides{(std::uint64_t{1} << 32u) + 4096u, std::uint64_t{1} << 31u,
                                                     (std::uint64_t{1} << 32u) - 1u, (std::uint64_t{1} << 31u) - 1u};
    for (const auto stride : kStrides)
        for (const bool planar : {false, true})
            for (const auto origin : {an::CoordinateOrigin::TopLeft, an::CoordinateOrigin::BottomLeft})
            {
                SCOPED_TRACE(testing::Message() << stride << " planar " << planar << " origin " << int(origin));
                const auto descriptor = describe(32u, 2u, an::ChannelSchema::Rgb, origin);
                const auto layout = an::ImageLayout::createWithStrides(
                    {descriptor.extent(), 1u, planar ? 1u : 3u, planar ? 3u : 1u, 1u}, stride, 2u * stride,
                    engine.limits());
                ASSERT_TRUE(layout.has_value());
                PaddedVideoMemory memory(*layout);
                ASSERT_NE(memory.data(), nullptr);
                auto buffer =
                    an::SampleBuffer::borrowConst({memory.data(), static_cast<std::size_t>(layout->totalBytes())});
                ASSERT_TRUE(buffer.hasValue());
                auto raw = an::RawFrame::create(descriptor, *layout, std::move(*buffer));
                ASSERT_TRUE(raw.hasValue());
                auto output = an::SequenceOutput::begin(engine, execution, {.destinationPath = pathOf("stride.mp4")});
                ASSERT_TRUE(output.hasValue());
                auto sink = an::createFfmpegSink(engine, execution, *output,
                                                 videoDescriptor(descriptor, an::FfmpegVideoContainer::Mp4), {});
                ASSERT_TRUE(sink.hasValue()) << sink.status().message();
                const auto appended = sink->append(engine, execution, {std::move(*raw), {}, {}});
                if (stride > static_cast<std::uint64_t>((std::numeric_limits<int>::max)()))
                {
                    ASSERT_FALSE(appended.hasValue());
                    EXPECT_EQ(appended.status().code(), an::ErrorCode::Unsupported);
                    EXPECT_EQ(parameterOf(appended.status()), "rowStrideBytes");
                    EXPECT_EQ(an::sequenceFailureReason(appended.status().rootCause()),
                              an::SequenceFailure::UnsupportedEncoding);
                    EXPECT_EQ(sink->progress().framesWritten, 0u);
                }
                else
                    ASSERT_TRUE(appended.hasValue()) << appended.status().message();
                EXPECT_TRUE(sink->close().ok());
                output->abort();
                EXPECT_FALSE(std::filesystem::exists(native(output->pendingPath())));
            }
}

TEST_F(FfmpegSinkTest, ContainerTimestampResolutionIsCheckedBeforeClaimingThePayload)
{
    const auto frame = describe(64u, 64u);
    for (const auto container : {an::FfmpegVideoContainer::Matroska, an::FfmpegVideoContainer::WebM})
        for (const an::SequenceRate rate : {an::SequenceRate{2000u, 1u}, an::SequenceRate{1000001u, 1000u}})
        {
            const auto path = pathOf(std::string("rate") + extensionOf(container));
            auto output = an::SequenceOutput::begin(engine, execution, {.destinationPath = path});
            ASSERT_TRUE(output.hasValue());
            const Bytes sentinel{std::byte{17}, std::byte{29}};
            overwrite(output->pendingPath(), sentinel);
            const an::FfmpegSinkRequest request{.codec = container == an::FfmpegVideoContainer::WebM
                                                             ? an::FfmpegVideoCodec::Vp9
                                                             : an::FfmpegVideoCodec::H264,
                                                .container = container};
            const auto refused =
                an::createFfmpegSink(engine, execution, *output, videoDescriptor(frame, container, rate), request);
            ASSERT_FALSE(refused.hasValue());
            EXPECT_EQ(refused.status().code(), an::ErrorCode::Unsupported);
            EXPECT_EQ(parameterOf(refused.status()), "nominalFrameRate");
            EXPECT_EQ(readAll(output->pendingPath()), sentinel);
            auto claim = output->claimWriter();
            ASSERT_TRUE(claim.hasValue());
        }
}

TEST_F(FfmpegSinkTest, RepresentableHighRatesKeepEveryFrameAndDistinctPresentationTimes)
{
    if (!an::isFfmpegEncoderAvailable(an::FfmpegVideoCodec::H264))
        GTEST_SKIP() << "this build has no libx264";
    const auto frame = describe(64u, 64u);
    for (const auto container : {an::FfmpegVideoContainer::Matroska, an::FfmpegVideoContainer::Mp4})
    {
        const an::SequenceRate rate{container == an::FfmpegVideoContainer::Mp4 ? 2000u : 1000u, 1u};
        auto descriptor = videoDescriptor(frame, container, rate);
        descriptor.frameCount = 8u;
        const std::vector<VideoFrame> frames(8u, flatFrame(frame, 100));
        const auto published =
            writeVideo(std::string("high-rate") + extensionOf(container), descriptor, {.container = container}, frames);
        const auto decoded = readFrames(published.path, {.sampleDelivery = an::FfmpegSampleDelivery::LumaPlane});
        ASSERT_EQ(decoded.size(), frames.size());
        for (std::size_t index = 1u; index < decoded.size(); ++index)
        {
            ASSERT_TRUE(decoded[index].position.presentationTicks.has_value());
            EXPECT_GT(*decoded[index].position.presentationTicks, *decoded[index - 1u].position.presentationTicks);
        }
    }
}
