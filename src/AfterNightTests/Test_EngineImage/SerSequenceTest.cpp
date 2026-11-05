// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/IO/SerSequence.h>

#include <AfterNightEngine/IO/CodecError.h>
#include <AfterNightEngine/IO/SequenceOutput.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>
#include <AfterNightEngine/Runtime/TraceSink.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace
{
using Bytes = std::vector<std::byte>;

void putLe(Bytes& bytes, std::size_t offset, std::uint64_t value, std::size_t count)
{
    for (std::size_t i = 0u; i < count; ++i)
        bytes[offset + i] = static_cast<std::byte>((value >> (8u * i)) & 0xffu);
}

std::uint64_t getLe(std::span<const std::byte> bytes, std::size_t offset, std::size_t count)
{
    std::uint64_t value = 0u;
    for (std::size_t i = 0u; i < count; ++i)
        value |= std::uint64_t{std::to_integer<std::uint8_t>(bytes[offset + i])} << (8u * i);
    return value;
}

void putText(Bytes& bytes, std::size_t offset, std::string_view text)
{
    if (!text.empty())
        std::memcpy(bytes.data() + offset, text.data(), text.size());
}

std::string textAt(std::span<const std::byte> bytes, std::size_t offset, std::size_t count)
{
    return {reinterpret_cast<const char*>(bytes.data() + offset), count};
}

Bytes::const_iterator from(const Bytes& bytes, std::uint64_t offset)
{
    return bytes.begin() + static_cast<std::ptrdiff_t>(offset);
}

/// A container assembled field by field, so a case states exactly what a file says - including what
/// no conforming writer would write.
struct SerBytes
{
    std::string fileId = "LUCAM-RECORDER";
    std::int32_t luId = 0;
    std::int32_t colorId = 0;
    std::int32_t littleEndian = 0;
    std::int32_t width = 5;
    std::int32_t height = 4;
    std::int32_t depth = 16;
    std::uint32_t frameCount = 3u;
    std::string observer;
    std::string instrument;
    std::string telescope;
    std::uint64_t dateTime = 0u;
    std::uint64_t dateTimeUtc = 0u;
    Bytes payload;
    std::vector<std::uint64_t> trailer;
    Bytes tail;

    [[nodiscard]] std::size_t sampleBytes() const
    {
        return depth > 8 ? 2u : 1u;
    }

    [[nodiscard]] std::size_t channels() const
    {
        return colorId == 100 || colorId == 101 ? 3u : 1u;
    }

    [[nodiscard]] std::size_t frameBytes() const
    {
        return static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * channels() * sampleBytes();
    }

    [[nodiscard]] Bytes header() const
    {
        Bytes bytes(an::kSerHeaderBytes);
        putText(bytes, 0u, std::string_view(fileId).substr(0u, 14u));
        putLe(bytes, 14u, static_cast<std::uint32_t>(luId), 4u);
        putLe(bytes, 18u, static_cast<std::uint32_t>(colorId), 4u);
        putLe(bytes, 22u, static_cast<std::uint32_t>(littleEndian), 4u);
        putLe(bytes, 26u, static_cast<std::uint32_t>(width), 4u);
        putLe(bytes, 30u, static_cast<std::uint32_t>(height), 4u);
        putLe(bytes, 34u, static_cast<std::uint32_t>(depth), 4u);
        putLe(bytes, 38u, frameCount, 4u);
        putText(bytes, 42u, std::string_view(observer).substr(0u, 40u));
        putText(bytes, 82u, std::string_view(instrument).substr(0u, 40u));
        putText(bytes, 122u, std::string_view(telescope).substr(0u, 40u));
        putLe(bytes, 162u, dateTime, 8u);
        putLe(bytes, 170u, dateTimeUtc, 8u);
        return bytes;
    }

    [[nodiscard]] Bytes file() const
    {
        Bytes bytes = header();
        bytes.insert(bytes.end(), payload.begin(), payload.end());
        const std::size_t trailerStart = bytes.size();
        bytes.resize(trailerStart + 8u * trailer.size());
        for (std::size_t index = 0u; index < trailer.size(); ++index)
            putLe(bytes, trailerStart + 8u * index, trailer[index], 8u);
        bytes.insert(bytes.end(), tail.begin(), tail.end());
        return bytes;
    }
};

/// Bytes that differ from their neighbours, so an ordering or exchange mistake cannot cancel out.
Bytes pattern(std::size_t count, std::uint32_t seed)
{
    Bytes bytes(count);
    std::uint32_t state = seed * 2654435761u + 1u;
    for (auto& byte : bytes)
    {
        state = state * 1664525u + 1013904223u;
        byte = static_cast<std::byte>(state >> 24u);
    }
    return bytes;
}

SerBytes monoFile(std::uint32_t frames)
{
    SerBytes file;
    file.frameCount = frames;
    file.payload = pattern(file.frameBytes() * frames, 7u);
    return file;
}

/// A reproducible stream of mutations: the same seed regenerates exactly the same inputs.
class Mutator
{
public:
    explicit Mutator(std::uint64_t seed) noexcept : m_state(seed | 1u) {}

    std::uint64_t next() noexcept
    {
        m_state ^= m_state << 13u;
        m_state ^= m_state >> 7u;
        m_state ^= m_state << 17u;
        return m_state;
    }

    std::uint64_t below(std::uint64_t bound) noexcept
    {
        return bound == 0u ? 0u : next() % bound;
    }

private:
    std::uint64_t m_state;
};

/// Counts an operation's SER trace events by name and requests a stop when a named one arrives: how a
/// case stops an operation part way through a callback without racing a thread against it, and then
/// shows how much further the callback went before it noticed.
class StopOnTrace final : public an::TraceSink
{
public:
    // A null name counts without ever requesting the stop.
    StopOnTrace(std::stop_source& stop, const char* name) : m_stop(stop), m_name(name) {}

    void onTrace(const an::TraceEvent& event) noexcept override
    {
        if (std::string_view(event.category) != "ser")
            return;
        const std::string_view name(event.name);
        for (std::size_t index = 0u; index < kNames.size(); ++index)
            if (name == kNames[index])
                m_counts[index].fetch_add(1u);
        if (m_name != nullptr && name == m_name)
            m_stop.request_stop();
    }

    [[nodiscard]] std::uint64_t count(std::string_view name) const
    {
        for (std::size_t index = 0u; index < kNames.size(); ++index)
            if (name == kNames[index])
                return m_counts[index].load();
        ADD_FAILURE() << "no SER trace event is named " << name;
        return 0u;
    }

private:
    static constexpr std::array<std::string_view, 5u> kNames{"transfer-bytes", "trailer-bytes", "frame-bytes-read",
                                                             "frame-bytes-written", "trailer-bytes-written"};

    std::stop_source& m_stop;
    const char* m_name;
    std::array<std::atomic<std::uint64_t>, 5u> m_counts{};
};

bool chainCarries(const an::Status& status, an::SequenceFailure reason)
{
    for (an::Status current = status; current.failed(); current = current.cause())
        if (an::sequenceFailureReason(current) == reason)
            return true;
    return false;
}

std::string parameterOf(const an::Status& status)
{
    const auto* field = status.findContext(an::ContextKey::Parameter);
    return field != nullptr ? field->text : std::string{};
}

an::RawFrameDescriptor describe(std::uint32_t width, std::uint32_t height, std::uint32_t channels,
                                std::uint32_t sampleBytes, an::ByteOrder order,
                                an::ChannelSchema schema = an::ChannelSchema::Mono, an::CfaDescriptor cfa = {},
                                an::CoordinateOrigin origin = an::CoordinateOrigin::TopLeft, double aspect = 1.0,
                                an::RawSampleClass sampleClass = an::RawSampleClass::UnsignedInteger)
{
    return *an::RawFrameDescriptor::create(
        {.extent = *an::ImageExtent::create(width, height, channels, an::defaultEngineLimits()),
         .sampleFormat = {sampleBytes, sampleClass},
         .byteOrder = sampleBytes == 1u ? an::ByteOrder::Native : order,
         .channelSchema = schema,
         .colorSpace = an::ColorSpace::Unspecified,
         .numericDomain = an::NumericDomain::RawSensorCode,
         .cfa = cfa,
         .coordinateOrigin = origin,
         .pixelAspectRatio = aspect});
}

an::SequenceFrame borrowed(const an::RawFrameDescriptor& descriptor, const an::ImageLayout& layout, const Bytes& bytes,
                           std::uint64_t index, std::optional<std::int64_t> ticks = {}, an::ImageMetadata metadata = {})
{
    auto buffer = an::SampleBuffer::borrowConst(std::span<const std::byte>(bytes.data(), bytes.size()));
    EXPECT_TRUE(buffer.hasValue());
    auto raw = an::RawFrame::create(descriptor, layout, std::move(*buffer));
    EXPECT_TRUE(raw.hasValue()) << raw.status().message();
    return an::SequenceFrame{std::move(*raw), std::move(metadata), an::SequencePosition{index, ticks, std::nullopt}};
}

an::SequenceDescriptor serDescriptor(const an::RawFrameDescriptor& frame,
                                     std::optional<std::uint64_t> count = std::nullopt)
{
    return {.format = an::SequenceFormat::Ser, .fixedFrame = frame, .frameCount = count};
}

an::CfaColorEntries xTransEntries()
{
    constexpr std::array<std::string_view, 6u> kRows{"GGRGGB", "GGBGGR", "BRGRBG", "GGBGGR", "GGRGGB", "RBGBRG"};
    an::CfaColorEntries entries{};
    for (std::size_t row = 0u; row < 6u; ++row)
        for (std::size_t column = 0u; column < 6u; ++column)
            entries[row * 6u + column] = kRows[row][column] == 'R'   ? an::CfaColor::Red
                                         : kRows[row][column] == 'G' ? an::CfaColor::Green
                                                                     : an::CfaColor::Blue;
    return entries;
}

class SerSequenceTest : public testing::Test
{
protected:
    void SetUp() override
    {
        directory = std::filesystem::temp_directory_path() /
                    ("an-ser-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
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

    static void overwrite(std::string_view path, const Bytes& bytes)
    {
        std::ofstream stream(native(path), std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    std::string write(const std::filesystem::path& name, const Bytes& bytes) const
    {
        const auto path = pathOf(name);
        overwrite(path, bytes);
        return path;
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

    an::Result<an::SerContainer> inspect(const Bytes& bytes, const an::SerSourceOptions& options = {}) const
    {
        return an::inspectSerContainer(bytes, bytes.size(), options, engine.limits());
    }

    an::Result<an::SerContainer> inspect(const SerBytes& file, const an::SerSourceOptions& options = {}) const
    {
        return inspect(file.file(), options);
    }

    /// Writes frames through an output and a sink, commits, and returns what was published.
    Bytes writeContainer(const std::filesystem::path& name, const an::SequenceDescriptor& descriptor,
                         const an::SerSinkRequest& request, const std::vector<an::SequenceFrame>& frames)
    {
        const auto destination = pathOf(name);
        auto output = an::SequenceOutput::begin(
            engine, execution, {.destinationPath = destination, .durability = an::OutputDurability::CloseOnly});
        if (!output.hasValue())
        {
            ADD_FAILURE() << "begin: " << output.status().message();
            return {};
        }
        auto sink = an::createSerSink(engine, execution, *output, descriptor, request);
        if (!sink.hasValue())
        {
            ADD_FAILURE() << "createSerSink: " << sink.status().message();
            return {};
        }
        for (const auto& frame : frames)
        {
            auto appended = sink->append(engine, execution, frame);
            if (!appended.hasValue())
            {
                ADD_FAILURE() << "append: " << appended.status().message();
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
        return readAll(destination);
    }

    std::filesystem::path directory;
    an::EngineContext engine;
    an::ExecutionContext execution;
};

TEST_F(SerSequenceTest, ReadsLittleEndianMonoFramesBitExactlyInOrderAndSeeksBack)
{
    const auto file = monoFile(3u);
    const auto path = write(u8"星-mono.ser", file.file());
    auto source = an::createSerSource(engine, execution, path, {});
    ASSERT_TRUE(source.hasValue()) << source.status().message();
    const auto* descriptor = source->descriptor();
    ASSERT_NE(descriptor, nullptr);
    EXPECT_EQ(descriptor->format, an::SequenceFormat::Ser);
    EXPECT_EQ(descriptor->frameCount, 3u);
    EXPECT_TRUE(descriptor->seekByIndex);
    EXPECT_FALSE(descriptor->seekByTime);
    EXPECT_FALSE(descriptor->timeBase);
    EXPECT_FALSE(descriptor->nominalFrameRate);
    EXPECT_FALSE(descriptor->durationTicks);
    ASSERT_TRUE(descriptor->fixedFrame);
    const auto& frame = *descriptor->fixedFrame;
    EXPECT_EQ(frame.extent().width(), 5u);
    EXPECT_EQ(frame.extent().height(), 4u);
    EXPECT_EQ(frame.extent().channelCount(), 1u);
    EXPECT_EQ(frame.sampleFormat(), (an::RawSampleFormat{2u, an::RawSampleClass::UnsignedInteger}));
    EXPECT_EQ(frame.byteOrder(), an::ByteOrder::Little);
    EXPECT_EQ(frame.channelSchema(), an::ChannelSchema::Mono);
    EXPECT_EQ(frame.colorSpace(), an::ColorSpace::Unspecified);
    EXPECT_EQ(frame.numericDomain(), an::NumericDomain::RawSensorCode);
    EXPECT_FALSE(frame.isMosaiced());
    EXPECT_EQ(frame.coordinateOrigin(), an::CoordinateOrigin::TopLeft);
    EXPECT_EQ(frame.pixelAspectRatio(), 1.0);
    for (std::uint64_t index = 0u; index < 3u; ++index)
    {
        auto read = source->read(engine, execution);
        ASSERT_TRUE(read.hasValue()) << read.status().message();
        ASSERT_TRUE(read->frame);
        const auto& result = *read->frame;
        EXPECT_EQ(result.position.index, index);
        EXPECT_FALSE(result.position.presentationTicks);
        EXPECT_EQ(result.frame.buffer().ownership(), an::SampleBufferOwnership::Owned);
        EXPECT_EQ(result.frame.buffer().contextId(), engine.id());
        EXPECT_FALSE(result.metadata.outputIntent());
        ASSERT_EQ(result.frame.layout()->totalBytes(), file.frameBytes());
        EXPECT_EQ(result.frame.layout()->samplesPerPixel(), 1u);
        EXPECT_EQ(result.frame.layout()->planeCount(), 1u);
        EXPECT_EQ(result.frame.layout()->rowStrideBytes(), 10u);
        EXPECT_TRUE(std::equal(from(file.payload, index * file.frameBytes()),
                               from(file.payload, (index + 1u) * file.frameBytes()), result.frame.bytes().begin()));
    }
    auto end = source->read(engine, execution);
    ASSERT_TRUE(end.hasValue());
    EXPECT_FALSE(end->frame);
    EXPECT_EQ(source->state(), an::SequenceState::EndOfStream);
    auto sought = source->seek(execution, {.index = 1u});
    ASSERT_TRUE(sought.hasValue()) << sought.status().message();
    EXPECT_EQ(sought->index, 1u);
    auto again = source->read(engine, execution);
    ASSERT_TRUE(again.hasValue()) << again.status().message();
    ASSERT_TRUE(again->frame);
    EXPECT_EQ(again->frame->position.index, 1u);
    EXPECT_TRUE(std::equal(from(file.payload, file.frameBytes()), from(file.payload, 2u * file.frameBytes()),
                           again->frame->frame.bytes().begin()));
    EXPECT_TRUE(source->close().ok());
}

TEST_F(SerSequenceTest, DescribesTheStoredByteOrderUnderEitherConventionWithoutSwapping)
{
    struct Case
    {
        std::int32_t flag;
        an::SerEndianConvention convention;
        an::ByteOrder expected;
    };
    const std::array cases{Case{0, an::SerEndianConvention::Compatible, an::ByteOrder::Little},
                           Case{1, an::SerEndianConvention::Compatible, an::ByteOrder::Big},
                           Case{7, an::SerEndianConvention::Compatible, an::ByteOrder::Little},
                           Case{0, an::SerEndianConvention::Specification, an::ByteOrder::Big},
                           Case{1, an::SerEndianConvention::Specification, an::ByteOrder::Little},
                           Case{-1, an::SerEndianConvention::Specification, an::ByteOrder::Little}};
    for (const auto& entry : cases)
    {
        auto file = monoFile(1u);
        file.littleEndian = entry.flag;
        const auto path = write("order.ser", file.file());
        auto source = an::createSerSource(engine, execution, path, {.endianConvention = entry.convention});
        ASSERT_TRUE(source.hasValue()) << source.status().message();
        EXPECT_EQ(source->descriptor()->fixedFrame->byteOrder(), entry.expected) << entry.flag;
        auto read = source->read(engine, execution);
        ASSERT_TRUE(read.hasValue()) << read.status().message();
        EXPECT_EQ(read->frame->metadata.codec().sourceEncoding().byteOrder, entry.expected) << entry.flag;
        EXPECT_TRUE(std::equal(file.payload.begin(), file.payload.end(), read->frame->frame.bytes().begin()));
        const auto* card = read->frame->metadata.codec().find(an::CodecNamespace::Ser, "LittleEndian");
        ASSERT_NE(card, nullptr);
        EXPECT_EQ(card->value(), std::to_string(entry.flag));
    }
}

TEST_F(SerSequenceTest, EightBitSamplesCarryNoByteOrderAndEveryBayerIdNamesItsPattern)
{
    const std::array<std::pair<std::int32_t, an::CfaPattern>, 5u> cases{{{0, an::CfaPattern::None},
                                                                         {8, an::CfaPattern::Rggb},
                                                                         {9, an::CfaPattern::Grbg},
                                                                         {10, an::CfaPattern::Gbrg},
                                                                         {11, an::CfaPattern::Bggr}}};
    for (const auto& [colorId, expected] : cases)
    {
        SerBytes file;
        file.colorId = colorId;
        file.depth = 8;
        file.littleEndian = 1;
        file.frameCount = 2u;
        file.payload = pattern(file.frameBytes() * 2u, 3u);
        auto container = inspect(file);
        ASSERT_TRUE(container.hasValue()) << container.status().message();
        const auto& frame = container->frame;
        EXPECT_EQ(frame.sampleFormat(), (an::RawSampleFormat{1u, an::RawSampleClass::UnsignedInteger}));
        EXPECT_EQ(frame.byteOrder(), an::ByteOrder::Native);
        EXPECT_EQ(frame.channelSchema(), an::ChannelSchema::Mono);
        EXPECT_EQ(frame.cfa().pattern(), expected) << colorId;
        EXPECT_EQ(frame.cfa().originX(), 0u);
        EXPECT_EQ(frame.cfa().originY(), 0u);
        EXPECT_EQ(container->layout.totalBytes(), 20u);
        EXPECT_EQ(container->frameCount, 2u);
        EXPECT_FALSE(container->trailerPresent);
        EXPECT_FALSE(container->frameCountRecovered);
        EXPECT_FALSE(container->channelOrderExchanged);
        EXPECT_EQ(container->trailingBytes, 0u);
        EXPECT_EQ(container->header.colorId, colorId);
    }
}

TEST_F(SerSequenceTest, RgbIsInterleavedAndBgrIsDeliveredInRgbOrder)
{
    for (const std::int32_t colorId : {100, 101})
    {
        for (const std::int32_t depth : {8, 16})
        {
            SerBytes file;
            file.colorId = colorId;
            file.depth = depth;
            file.width = 3;
            file.height = 2;
            file.frameCount = 1u;
            file.payload = pattern(file.frameBytes(), 11u);
            const auto path = write("colour.ser", file.file());
            auto source = an::createSerSource(engine, execution, path, {});
            ASSERT_TRUE(source.hasValue()) << source.status().message();
            const auto& frame = *source->descriptor()->fixedFrame;
            EXPECT_EQ(frame.channelSchema(), an::ChannelSchema::Rgb);
            EXPECT_EQ(frame.extent().channelCount(), 3u);
            auto read = source->read(engine, execution);
            ASSERT_TRUE(read.hasValue()) << read.status().message();
            // By value: the layout accessor answers with an optional of its own, so a reference
            // here would bind into a temporary that dies at the end of this statement.
            const auto layout = *read->frame->frame.layout();
            EXPECT_EQ(layout.samplesPerPixel(), 3u);
            EXPECT_EQ(layout.planeCount(), 1u);
            const std::size_t sample = depth > 8 ? 2u : 1u;
            const auto bytes = read->frame->frame.bytes();
            for (std::size_t pixel = 0u; pixel < 6u; ++pixel)
                for (std::size_t channel = 0u; channel < 3u; ++channel)
                {
                    const std::size_t stored = colorId == 101 ? 2u - channel : channel;
                    for (std::size_t part = 0u; part < sample; ++part)
                        EXPECT_EQ(bytes[(pixel * 3u + channel) * sample + part],
                                  file.payload[(pixel * 3u + stored) * sample + part]);
                }
            auto info = an::inspectSerFile(engine, execution, path, {});
            ASSERT_TRUE(info.hasValue()) << info.status().message();
            EXPECT_EQ(info->container.channelOrderExchanged, colorId == 101);
        }
    }
}

TEST_F(SerSequenceTest, CmyMosaicsAreRefusedByNameUnlessDescribedAsMono)
{
    for (std::int32_t colorId = 16; colorId <= 19; ++colorId)
    {
        auto file = monoFile(1u);
        file.colorId = colorId;
        auto refused = inspect(file);
        ASSERT_FALSE(refused.hasValue());
        EXPECT_EQ(refused.status().code(), an::ErrorCode::Unsupported);
        EXPECT_EQ(an::sequenceFailureReason(refused.status()), an::SequenceFailure::UnsupportedEncoding);
        EXPECT_EQ(parameterOf(refused.status()), "ColorID");

        const auto path = write("cmy.ser", file.file());
        EXPECT_EQ(an::createSerSource(engine, execution, path, {}).status().code(), an::ErrorCode::Unsupported);
        auto source =
            an::createSerSource(engine, execution, path, {.cmyMosaicPolicy = an::SerCmyMosaicPolicy::DescribeAsMono});
        ASSERT_TRUE(source.hasValue()) << source.status().message();
        EXPECT_FALSE(source->descriptor()->fixedFrame->isMosaiced());
        EXPECT_EQ(source->descriptor()->fixedFrame->channelSchema(), an::ChannelSchema::Mono);
        auto read = source->read(engine, execution);
        ASSERT_TRUE(read.hasValue()) << read.status().message();
        const auto* card = read->frame->metadata.codec().find(an::CodecNamespace::Ser, "ColorID");
        ASSERT_NE(card, nullptr);
        EXPECT_EQ(card->value(), std::to_string(colorId));
    }
}

TEST_F(SerSequenceTest, AnOrderedTrailerBecomesTheTimelineAndTimeSeeksResolveBothWays)
{
    auto file = monoFile(4u);
    const std::uint64_t start = an::kSerUnixEpochTicks + 17000000000000000u;
    file.trailer = {start, start + 10u, start + 10u, start + 30u};
    file.dateTimeUtc = start - 5u;
    const auto path = write("timeline.ser", file.file());
    auto source = an::createSerSource(engine, execution, path, {});
    ASSERT_TRUE(source.hasValue()) << source.status().message();
    const auto* descriptor = source->descriptor();
    ASSERT_TRUE(descriptor->timeBase);
    EXPECT_EQ(descriptor->timeBase->numerator, 1u);
    EXPECT_EQ(descriptor->timeBase->denominator, 10000000u);
    EXPECT_TRUE(descriptor->timestampsRequired);
    EXPECT_TRUE(descriptor->seekByTime);
    EXPECT_FALSE(descriptor->durationTicks);
    const std::array<std::int64_t, 4u> ticks{0, 10, 10, 30};
    for (std::uint64_t index = 0u; index < 4u; ++index)
    {
        auto read = source->read(engine, execution);
        ASSERT_TRUE(read.hasValue()) << read.status().message();
        EXPECT_EQ(read->frame->position.presentationTicks, ticks[index]);
        EXPECT_FALSE(read->frame->position.durationTicks);
        EXPECT_EQ(read->frame->metadata.acquisition().exposureStart(), an::utcInstantFromSerTicks(file.trailer[index]));
    }

    struct Seek
    {
        an::SequenceSeekKind kind;
        std::int64_t ticks;
        std::uint64_t index;
    };
    const std::array seeks{
        Seek{an::SequenceSeekKind::TimeAtOrAfter, 10, 1u},    Seek{an::SequenceSeekKind::TimeAtOrBefore, 10, 2u},
        Seek{an::SequenceSeekKind::TimeAtOrAfter, 11, 3u},    Seek{an::SequenceSeekKind::TimeAtOrBefore, 29, 2u},
        Seek{an::SequenceSeekKind::TimeAtOrBefore, 1000, 3u}, Seek{an::SequenceSeekKind::TimeAtOrAfter, 0, 0u}};
    for (const auto& seek : seeks)
    {
        auto sought = source->seek(execution, {.kind = seek.kind, .ticks = seek.ticks});
        ASSERT_TRUE(sought.hasValue()) << seek.ticks << ": " << sought.status().message();
        EXPECT_EQ(sought->index, seek.index) << seek.ticks;
        EXPECT_EQ(sought->presentationTicks, ticks[seek.index]) << seek.ticks;
        auto read = source->read(engine, execution);
        ASSERT_TRUE(read.hasValue()) << read.status().message();
        EXPECT_EQ(read->frame->position.index, seek.index);
    }
    auto byIndex = source->seek(execution, {.index = 2u});
    ASSERT_TRUE(byIndex.hasValue()) << byIndex.status().message();
    EXPECT_EQ(byIndex->presentationTicks, 10);
    auto toEnd = source->seek(execution, {.index = 4u});
    ASSERT_TRUE(toEnd.hasValue()) << toEnd.status().message();
    EXPECT_FALSE(toEnd->presentationTicks);
    EXPECT_FALSE(source->read(engine, execution)->frame);

    auto beyond = source->seek(execution, {.kind = an::SequenceSeekKind::TimeAtOrAfter, .ticks = 31});
    ASSERT_FALSE(beyond.hasValue());
    EXPECT_TRUE(chainCarries(beyond.status(), an::SequenceFailure::InvalidPosition));
    EXPECT_EQ(source->state(), an::SequenceState::Failed);
}

TEST_F(SerSequenceTest, AnIncompleteOrDecreasingTrailerKeepsInstantsButStatesNoTimeline)
{
    const std::uint64_t start = an::kSerUnixEpochTicks + 17000000000000000u;
    for (const auto& trailer :
         {std::vector<std::uint64_t>{start, 0u, start + 2u}, std::vector<std::uint64_t>{start + 5u, start, start + 9u}})
    {
        auto file = monoFile(3u);
        file.trailer = trailer;
        const auto path = write("untimed.ser", file.file());
        auto info = an::inspectSerFile(engine, execution, path, {});
        ASSERT_TRUE(info.hasValue()) << info.status().message();
        EXPECT_TRUE(info->container.trailerPresent);
        EXPECT_FALSE(info->timestampsOrdered);
        EXPECT_FALSE(info->timeline);
        auto source = an::createSerSource(engine, execution, path, {});
        ASSERT_TRUE(source.hasValue()) << source.status().message();
        EXPECT_FALSE(source->descriptor()->timeBase);
        EXPECT_FALSE(source->descriptor()->seekByTime);
        EXPECT_FALSE(source->descriptor()->timestampsRequired);
        for (std::size_t index = 0u; index < 3u; ++index)
        {
            auto read = source->read(engine, execution);
            ASSERT_TRUE(read.hasValue()) << read.status().message();
            EXPECT_FALSE(read->frame->position.presentationTicks);
            EXPECT_EQ(read->frame->metadata.acquisition().exposureStart(), an::utcInstantFromSerTicks(trailer[index]));
        }
        EXPECT_EQ(source->seek(execution, {.kind = an::SequenceSeekKind::TimeAtOrAfter, .ticks = 0}).status().code(),
                  an::ErrorCode::Unsupported);
        const an::SerSourceOptions required{.timelinePolicy = an::SerTimelinePolicy::Require};
        auto refused = an::createSerSource(engine, execution, path, required);
        ASSERT_FALSE(refused.hasValue());
        EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidData);
        EXPECT_EQ(an::sequenceFailureReason(refused.status()), an::SequenceFailure::InvalidTimestamps);
        EXPECT_EQ(an::inspectSerFile(engine, execution, path, required).status().code(), an::ErrorCode::InvalidData);
    }

    auto ordered = monoFile(2u);
    ordered.trailer = {start, start + 1u};
    const auto path = write("omitted.ser", ordered.file());
    auto omitted = an::createSerSource(engine, execution, path, {.timelinePolicy = an::SerTimelinePolicy::Omit});
    ASSERT_TRUE(omitted.hasValue()) << omitted.status().message();
    EXPECT_FALSE(omitted->descriptor()->timeBase);
    auto read = omitted->read(engine, execution);
    ASSERT_TRUE(read.hasValue()) << read.status().message();
    EXPECT_EQ(read->frame->metadata.acquisition().exposureStart(), an::utcInstantFromSerTicks(start));
    EXPECT_FALSE(read->frame->position.presentationTicks);

    const auto bare = write("bare.ser", monoFile(2u).file());
    EXPECT_EQ(an::createSerSource(engine, execution, bare, {.timelinePolicy = an::SerTimelinePolicy::Require})
                  .status()
                  .code(),
              an::ErrorCode::InvalidData);
}

TEST_F(SerSequenceTest, ATrailerIsPresentOnlyWhenEveryFrameHasItsEntry)
{
    auto file = monoFile(3u);
    file.trailer = {1u, 2u, 3u};
    file.tail = pattern(5u, 1u);
    auto withTail = inspect(file);
    ASSERT_TRUE(withTail.hasValue()) << withTail.status().message();
    EXPECT_TRUE(withTail->trailerPresent);
    EXPECT_EQ(withTail->trailingBytes, 5u);
    EXPECT_EQ(withTail->fileBytes, an::kSerHeaderBytes + 3u * file.frameBytes() + 24u + 5u);

    file.trailer = {1u, 2u};
    file.tail.clear();
    auto partial = inspect(file);
    ASSERT_TRUE(partial.hasValue()) << partial.status().message();
    EXPECT_FALSE(partial->trailerPresent);
    EXPECT_EQ(partial->trailingBytes, 16u);

    file.trailer = {1u, 2u, 3u};
    auto info = an::inspectSerFile(engine, execution, write("early.ser", file.file()), {});
    ASSERT_TRUE(info.hasValue()) << info.status().message();
    EXPECT_EQ(info->timestampCount, 0u);
    EXPECT_FALSE(info->timestampsOrdered);
    EXPECT_FALSE(info->firstTimestamp);
}

TEST_F(SerSequenceTest, HeaderTextAndFactsBecomeAcquisitionFactsAndSerCards)
{
    auto file = monoFile(1u);
    file.depth = 12;
    file.luId = 42;
    file.observer = std::string("Ann  ") + '\0' + "junk";
    file.instrument = std::string(40u, 'x');
    file.telescope = "\xC9toile";
    file.fileId = "PIPP";
    file.dateTime = an::kSerUnixEpochTicks + 123u;
    file.dateTimeUtc = an::kSerUnixEpochTicks + 456u;
    const auto path = write("header.ser", file.file());
    auto info = an::inspectSerFile(engine, execution, path, {});
    ASSERT_TRUE(info.hasValue()) << info.status().message();
    const auto& header = info->container.header;
    EXPECT_EQ(header.fileId, "PIPP");
    EXPECT_EQ(header.luId, 42);
    EXPECT_EQ(header.pixelDepthPerPlane, 12);
    EXPECT_EQ(header.observer, "Ann");
    EXPECT_EQ(header.instrument, std::string(40u, 'x'));
    EXPECT_EQ(header.telescope, "\xC3\x89toile");
    EXPECT_EQ(header.dateTimeTicks, an::kSerUnixEpochTicks + 123u);
    EXPECT_EQ(header.dateTimeUtcTicks, an::kSerUnixEpochTicks + 456u);
    EXPECT_EQ(info->container.frame.sampleFormat().sizeBytes, 2u);

    auto source = an::createSerSource(engine, execution, path, {});
    ASSERT_TRUE(source.hasValue()) << source.status().message();
    auto read = source->read(engine, execution);
    ASSERT_TRUE(read.hasValue()) << read.status().message();
    const auto& metadata = read->frame->metadata;
    EXPECT_EQ(metadata.acquisition().observerName(), "Ann");
    EXPECT_EQ(metadata.acquisition().instrumentName(), std::string(40u, 'x'));
    EXPECT_EQ(metadata.acquisition().telescopeName(), "\xC3\x89toile");
    EXPECT_FALSE(metadata.acquisition().exposureStart());
    const auto& codec = metadata.codec();
    EXPECT_EQ(codec.sourceEncoding().sampleFormat, (an::RawSampleFormat{2u, an::RawSampleClass::UnsignedInteger}));
    EXPECT_EQ(codec.sourceEncoding().byteOrder, an::ByteOrder::Little);
    const std::array<std::pair<const char*, std::string>, 7u> cards{
        {{"FileID", "PIPP"},
         {"LuID", "42"},
         {"ColorID", "0"},
         {"LittleEndian", "0"},
         {"PixelDepthPerPlane", "12"},
         {"DateTime", std::to_string(an::kSerUnixEpochTicks + 123u)},
         {"DateTime_UTC", std::to_string(an::kSerUnixEpochTicks + 456u)}}};
    EXPECT_EQ(codec.cards().size(), cards.size());
    for (const auto& [keyword, value] : cards)
    {
        const auto* card = codec.find(an::CodecNamespace::Ser, keyword);
        ASSERT_NE(card, nullptr) << keyword;
        EXPECT_EQ(card->value(), value) << keyword;
    }
    EXPECT_EQ(metadata.semantics(), an::ImageSemantics{});
}

TEST_F(SerSequenceTest, FrameRateFromTimestampsIsTheIntervalCountOverTheTimestampSpan)
{
    auto file = monoFile(4u);
    const std::uint64_t start = an::kSerUnixEpochTicks + 16000000000000000u;
    file.trailer = {start, start + 10000000u, start + 20000000u, start + 30000000u};
    auto info = an::inspectSerFile(engine, execution, write("rate.ser", file.file()), {});
    ASSERT_TRUE(info.hasValue()) << info.status().message();
    EXPECT_TRUE(info->timestampsOrdered);
    EXPECT_TRUE(info->timeline);
    EXPECT_EQ(info->timestampCount, 4u);
    EXPECT_EQ(info->firstTimestamp, an::utcInstantFromSerTicks(start));
    EXPECT_EQ(info->lastTimestamp, an::utcInstantFromSerTicks(start + 30000000u));
    ASSERT_TRUE(info->timestampFrameRate);
    EXPECT_EQ(*info->timestampFrameRate, 1.0);

    file.trailer = {start + 20000000u, 0u, start, start + 5000000u};
    auto unordered = an::inspectSerFile(engine, execution, write("rate.ser", file.file()), {});
    ASSERT_TRUE(unordered.hasValue()) << unordered.status().message();
    EXPECT_EQ(unordered->timestampCount, 3u);
    EXPECT_FALSE(unordered->timestampsOrdered);
    EXPECT_EQ(unordered->firstTimestamp, an::utcInstantFromSerTicks(start + 20000000u));
    EXPECT_EQ(unordered->lastTimestamp, an::utcInstantFromSerTicks(start + 5000000u));
    ASSERT_TRUE(unordered->timestampFrameRate);
    EXPECT_DOUBLE_EQ(*unordered->timestampFrameRate, 2.0 * 10000.0 / (20000000.0 / 1000.0));

    file.trailer = {start, start, start, start};
    auto still = an::inspectSerFile(engine, execution, write("rate.ser", file.file()), {});
    ASSERT_TRUE(still.hasValue()) << still.status().message();
    EXPECT_TRUE(still->timestampsOrdered);
    EXPECT_FALSE(still->timestampFrameRate);

    file.trailer.clear();
    auto none = an::inspectSerFile(engine, execution, write("rate.ser", file.file()), {});
    ASSERT_TRUE(none.hasValue()) << none.status().message();
    EXPECT_FALSE(none->container.trailerPresent);
    EXPECT_EQ(none->timestampCount, 0u);
    EXPECT_FALSE(none->firstTimestamp);
    EXPECT_FALSE(none->timestampFrameRate);
}

TEST_F(SerSequenceTest, TruncatedPayloadIsRefusedWithBothByteCounts)
{
    auto file = monoFile(3u);
    file.payload.resize(file.frameBytes() * 2u + 7u);
    const auto bytes = file.file();
    auto refused = inspect(bytes);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidData);
    EXPECT_EQ(an::sequenceFailureReason(refused.status()), an::SequenceFailure::TruncatedContainer);
    EXPECT_EQ(parameterOf(refused.status()), "FrameCount");
    ASSERT_NE(refused.status().findContext(an::ContextKey::Requested), nullptr);
    ASSERT_NE(refused.status().findContext(an::ContextKey::Available), nullptr);
    EXPECT_EQ(refused.status().findContext(an::ContextKey::Requested)->count,
              an::kSerHeaderBytes + 3u * file.frameBytes());
    EXPECT_EQ(refused.status().findContext(an::ContextKey::Available)->count, bytes.size());

    const auto path = write("truncated.ser", bytes);
    auto source = an::createSerSource(engine, execution, path, {});
    ASSERT_FALSE(source.hasValue());
    EXPECT_EQ(an::sequenceFailureReason(source.status()), an::SequenceFailure::TruncatedContainer);
    ASSERT_NE(source.status().findContext(an::ContextKey::Path), nullptr);
    EXPECT_EQ(source.status().findContext(an::ContextKey::Path)->text, path);

    auto tiny = an::inspectSerContainer(file.header(), 100u, {}, engine.limits());
    ASSERT_FALSE(tiny.hasValue());
    EXPECT_EQ(an::sequenceFailureReason(tiny.status()), an::SequenceFailure::TruncatedContainer);
    EXPECT_EQ(tiny.status().findContext(an::ContextKey::Requested)->count, an::kSerHeaderBytes);
    EXPECT_EQ(tiny.status().findContext(an::ContextKey::Available)->count, 100u);
    const Bytes header = file.header();
    const auto headerOnly = write("tiny.ser", Bytes(header.begin(), header.begin() + 100));
    EXPECT_TRUE(chainCarries(an::createSerSource(engine, execution, headerOnly, {}).status(),
                             an::SequenceFailure::TruncatedContainer));

    const Bytes shortSpan(100u);
    auto span = an::inspectSerContainer(shortSpan, 1000u, {}, engine.limits());
    EXPECT_EQ(span.status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(parameterOf(span.status()), "header");
}

TEST_F(SerSequenceTest, CompleteFramesRecoversAnUnfinishedRecordingAndIgnoresItsPartialFrame)
{
    auto file = monoFile(3u);
    file.frameCount = 0u;
    file.payload.resize(file.frameBytes() * 2u + 7u);
    const auto zero = file.file();
    auto exact = inspect(zero);
    ASSERT_FALSE(exact.hasValue());
    EXPECT_EQ(exact.status().code(), an::ErrorCode::InvalidData);
    EXPECT_EQ(an::sequenceFailureReason(exact.status()), an::SequenceFailure::MalformedContainer);
    EXPECT_EQ(parameterOf(exact.status()), "FrameCount");

    const an::SerSourceOptions recover{.frameCountPolicy = an::SerFrameCountPolicy::CompleteFrames};
    auto recovered = inspect(zero, recover);
    ASSERT_TRUE(recovered.hasValue()) << recovered.status().message();
    EXPECT_EQ(recovered->frameCount, 2u);
    EXPECT_TRUE(recovered->frameCountRecovered);
    EXPECT_FALSE(recovered->trailerPresent);
    EXPECT_EQ(recovered->trailingBytes, 7u);
    EXPECT_EQ(recovered->header.frameCount, 0u);

    file.frameCount = 9u;
    auto shortened = inspect(file, recover);
    ASSERT_TRUE(shortened.hasValue()) << shortened.status().message();
    EXPECT_EQ(shortened->frameCount, 2u);
    EXPECT_TRUE(shortened->frameCountRecovered);

    file.frameCount = 2u;
    auto standing = inspect(file, recover);
    ASSERT_TRUE(standing.hasValue()) << standing.status().message();
    EXPECT_FALSE(standing->frameCountRecovered);
    EXPECT_EQ(standing->frameCount, 2u);
    EXPECT_EQ(standing->trailingBytes, 7u);

    const auto path = write("recovered.ser", zero);
    auto source = an::createSerSource(engine, execution, path, recover);
    ASSERT_TRUE(source.hasValue()) << source.status().message();
    EXPECT_EQ(source->descriptor()->frameCount, 2u);
    for (int frame = 0; frame < 2; ++frame)
    {
        auto read = source->read(engine, execution);
        ASSERT_TRUE(read.hasValue()) << read.status().message();
        EXPECT_TRUE(read->frame);
    }
    auto end = source->read(engine, execution);
    ASSERT_TRUE(end.hasValue());
    EXPECT_FALSE(end->frame);

    SerBytes empty;
    empty.frameCount = 0u;
    auto nothing = inspect(empty);
    ASSERT_TRUE(nothing.hasValue()) << nothing.status().message();
    EXPECT_EQ(nothing->frameCount, 0u);
    EXPECT_EQ(nothing->fileBytes, an::kSerHeaderBytes);
}

TEST_F(SerSequenceTest, MalformedFieldsAreRefusedByTheirOwnName)
{
    struct Case
    {
        const char* name;
        void (*mutate)(SerBytes&);
        const char* parameter;
    };
    const std::array cases{
        Case{"unknown colour", [](SerBytes& file) { file.colorId = 5; }, "ColorID"},
        Case{"negative colour", [](SerBytes& file) { file.colorId = -100; }, "ColorID"},
        Case{"zero depth", [](SerBytes& file) { file.depth = 0; }, "PixelDepthPerPlane"},
        Case{"too deep", [](SerBytes& file) { file.depth = 17; }, "PixelDepthPerPlane"},
        Case{"zero width", [](SerBytes& file) { file.width = 0; }, "ImageWidth"},
        Case{"negative width", [](SerBytes& file) { file.width = -5; }, "ImageWidth"},
        Case{"negative height", [](SerBytes& file) { file.height = -4; }, "ImageHeight"},
    };
    for (const auto& entry : cases)
    {
        auto file = monoFile(1u);
        entry.mutate(file);
        auto result = inspect(file);
        ASSERT_FALSE(result.hasValue()) << entry.name;
        EXPECT_EQ(result.status().code(), an::ErrorCode::InvalidData) << entry.name;
        EXPECT_EQ(an::sequenceFailureReason(result.status()), an::SequenceFailure::MalformedContainer) << entry.name;
        EXPECT_EQ(parameterOf(result.status()), entry.parameter) << entry.name;
    }

    const auto bytes = monoFile(1u).file();
    const auto refusedOption = [&](const an::SerSourceOptions& options) {
        auto result = inspect(bytes, options);
        EXPECT_EQ(result.status().code(), an::ErrorCode::InvalidArgument);
        return parameterOf(result.status());
    };
    EXPECT_EQ(refusedOption({.endianConvention = static_cast<an::SerEndianConvention>(2)}), "endianConvention");
    EXPECT_EQ(refusedOption({.frameCountPolicy = static_cast<an::SerFrameCountPolicy>(2)}), "frameCountPolicy");
    EXPECT_EQ(refusedOption({.timelinePolicy = static_cast<an::SerTimelinePolicy>(3)}), "timelinePolicy");
    EXPECT_EQ(refusedOption({.cmyMosaicPolicy = static_cast<an::SerCmyMosaicPolicy>(2)}), "cmyMosaicPolicy");
    EXPECT_EQ(refusedOption({.colorSpace = static_cast<an::ColorSpace>(99)}), "colorSpace");
    EXPECT_EQ(refusedOption({.numericDomain = static_cast<an::NumericDomain>(99)}), "numericDomain");

    auto odd = monoFile(1u);
    odd.fileId = "SOMETHING-ELSE";
    auto accepted = inspect(odd);
    ASSERT_TRUE(accepted.hasValue()) << accepted.status().message();
    EXPECT_EQ(accepted->header.fileId, "SOMETHING-ELSE");
}

TEST_F(SerSequenceTest, LimitsAndStatedMeaningAreCheckedBeforeAnyAllocation)
{
    const auto file = monoFile(1u);
    const auto bytes = file.file();
    an::EngineLimits narrow = engine.limits();
    narrow.maxImageWidth = 4u;
    auto wide = an::inspectSerContainer(bytes, bytes.size(), {}, narrow);
    ASSERT_FALSE(wide.hasValue());
    EXPECT_EQ(wide.status().code(), an::ErrorCode::LimitExceeded);
    EXPECT_EQ(an::sequenceFailureReason(wide.status()), an::SequenceFailure::DescriptorMismatch);
    narrow = engine.limits();
    narrow.maxImageBytes = 39u;
    EXPECT_EQ(an::inspectSerContainer(bytes, bytes.size(), {}, narrow).status().code(), an::ErrorCode::LimitExceeded);

    auto monoLinear = inspect(bytes, {.colorSpace = an::ColorSpace::LinearRgb});
    ASSERT_FALSE(monoLinear.hasValue());
    EXPECT_EQ(monoLinear.status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(an::sequenceFailureReason(monoLinear.status()), an::SequenceFailure::DescriptorMismatch);
    EXPECT_EQ(parameterOf(monoLinear.status()), "colorSpace");
    EXPECT_EQ(parameterOf(inspect(bytes, {.numericDomain = an::NumericDomain::PhysicalScaled}).status()),
              "numericDomain");
    SerBytes rgb;
    rgb.colorId = 100;
    rgb.frameCount = 1u;
    rgb.payload = pattern(rgb.frameBytes(), 1u);
    EXPECT_EQ(parameterOf(inspect(rgb, {.colorSpace = an::ColorSpace::LinearRgb}).status()), "numericDomain");
    auto display =
        inspect(rgb, {.colorSpace = an::ColorSpace::DisplayRgb, .numericDomain = an::NumericDomain::EncodedNonlinear});
    ASSERT_TRUE(display.hasValue()) << display.status().message();
    EXPECT_EQ(display->frame.colorSpace(), an::ColorSpace::DisplayRgb);
    EXPECT_EQ(display->frame.numericDomain(), an::NumericDomain::EncodedNonlinear);

    const auto path = write("limits.ser", bytes);
    auto options = an::EngineContextOptions{.maxConcurrency = 1u};
    options.limits.maxImageBytes = 39u;
    auto small = an::EngineContext::create(options);
    ASSERT_TRUE(small.hasValue());
    {
        auto smallExecution = an::ExecutionContext::create(*small, {});
        ASSERT_TRUE(smallExecution.hasValue());
        const auto before = small->memoryResource()->counters().totalAllocations;
        EXPECT_EQ(an::createSerSource(*small, *smallExecution, path, {}).status().code(), an::ErrorCode::LimitExceeded);
        EXPECT_EQ(small->memoryResource()->counters().totalAllocations, before);
    }
    EXPECT_TRUE(small->shutdown().ok());
}

TEST_F(SerSequenceTest, AReadThatMeetsAShortenedFileFailsTheSessionWithoutAPartialFrame)
{
    const auto file = monoFile(3u);
    const auto path = write("shrinking.ser", file.file());
    auto source = an::createSerSource(engine, execution, path, {});
    ASSERT_TRUE(source.hasValue()) << source.status().message();
    auto first = source->read(engine, execution);
    ASSERT_TRUE(first.hasValue()) << first.status().message();
    std::filesystem::resize_file(native(path), an::kSerHeaderBytes + file.frameBytes() + 5u);
    const auto live = engine.memoryResource()->counters().liveBytes;
    auto second = source->read(engine, execution);
    ASSERT_FALSE(second.hasValue());
    EXPECT_TRUE(chainCarries(second.status(), an::SequenceFailure::TruncatedContainer)) << second.status().message();
    EXPECT_EQ(source->state(), an::SequenceState::Failed);
    EXPECT_EQ(source->nextIndex(), 1u);
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, live);
    EXPECT_TRUE(
        std::equal(file.payload.begin(), from(file.payload, file.frameBytes()), first->frame->frame.bytes().begin()));
    EXPECT_EQ(source->read(engine, execution).status().code(), an::ErrorCode::InvalidState);
}

TEST_F(SerSequenceTest, MissingFilesAndMalformedPathsAreAnsweredWithTheirContext)
{
    const auto missing = pathOf("absent.ser");
    auto source = an::createSerSource(engine, execution, missing, {});
    ASSERT_FALSE(source.hasValue());
    EXPECT_EQ(source.status().code(), an::ErrorCode::ExternalFailure);
    EXPECT_EQ(an::sequenceFailureReason(source.status()), an::SequenceFailure::IoFailure);
    ASSERT_NE(source.status().findContext(an::ContextKey::Path), nullptr);
    EXPECT_EQ(source.status().findContext(an::ContextKey::Path)->text, missing);
    EXPECT_NE(source.status().findContext(an::ContextKey::LibraryCode), nullptr);
    EXPECT_EQ(an::inspectSerFile(engine, execution, missing, {}).status().code(), an::ErrorCode::ExternalFailure);

    for (const std::string& bad : {std::string{}, std::string("bad\0path", 8u), std::string("\xff\xfe.ser")})
    {
        auto refused = an::createSerSource(engine, execution, bad, {});
        EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidArgument);
        EXPECT_EQ(parameterOf(refused.status()), "path");
        EXPECT_EQ(an::inspectSerFile(engine, execution, bad, {}).status().code(), an::ErrorCode::InvalidArgument);
    }
    EXPECT_EQ(an::createSerSource(engine, execution, utf8(directory), {}).status().code(),
              an::ErrorCode::ExternalFailure);
    EXPECT_EQ(an::createSerSource(an::EngineContext{}, execution, missing, {}).status().code(),
              an::ErrorCode::NotInitialized);
}

TEST_F(SerSequenceTest, CancellationAndBudgetRefusalsPublishNoFrame)
{
    const auto file = monoFile(2u);
    const auto path = write("stopping.ser", file.file());
    {
        std::stop_source stop;
        auto stopped = an::ExecutionContext::create(engine, {.stopToken = stop.get_token()});
        ASSERT_TRUE(stopped.hasValue());
        stop.request_stop();
        EXPECT_EQ(an::createSerSource(engine, *stopped, path, {}).status().code(), an::ErrorCode::Cancelled);
        EXPECT_EQ(an::inspectSerFile(engine, *stopped, path, {}).status().code(), an::ErrorCode::Cancelled);
        auto source = an::createSerSource(engine, execution, path, {});
        ASSERT_TRUE(source.hasValue()) << source.status().message();
        EXPECT_EQ(source->read(engine, *stopped).status().code(), an::ErrorCode::Cancelled);
        EXPECT_EQ(source->state(), an::SequenceState::Open);
    }
    {
        std::stop_source stop;
        StopOnTrace trace(stop, "frame-bytes-read");
        auto during = an::ExecutionContext::create(engine, {.stopToken = stop.get_token(), .traceSink = &trace});
        ASSERT_TRUE(during.hasValue());
        auto source = an::createSerSource(engine, *during, path, {});
        ASSERT_TRUE(source.hasValue()) << source.status().message();
        const auto live = engine.memoryResource()->counters().liveBytes;
        auto read = source->read(engine, *during);
        EXPECT_EQ(read.status().code(), an::ErrorCode::Cancelled);
        EXPECT_EQ(source->state(), an::SequenceState::Failed);
        EXPECT_EQ(engine.memoryResource()->counters().liveBytes, live);
    }
    {
        auto options = an::EngineContextOptions{.maxConcurrency = 1u};
        options.limits.memoryBudgetBytes = file.frameBytes() - 1u;
        auto tight = an::EngineContext::create(options);
        ASSERT_TRUE(tight.hasValue());
        {
            auto tightExecution = an::ExecutionContext::create(*tight, {});
            ASSERT_TRUE(tightExecution.hasValue());
            auto source = an::createSerSource(*tight, *tightExecution, path, {});
            ASSERT_TRUE(source.hasValue()) << source.status().message();
            auto read = source->read(*tight, *tightExecution);
            ASSERT_FALSE(read.hasValue());
            EXPECT_EQ(read.status().code(), an::ErrorCode::MemoryBudgetExceeded);
            EXPECT_EQ(source->state(), an::SequenceState::Failed);
            EXPECT_EQ(tight->memoryResource()->counters().liveBytes, 0u);
        }
        EXPECT_TRUE(tight->shutdown().ok());
    }
}

TEST_F(SerSequenceTest, FramesOutliveTheirSourceAndIndependentSessionsReadConcurrently)
{
    const auto file = monoFile(6u);
    const auto path = write("shared.ser", file.file());
    std::optional<an::SequenceFrame> kept;
    {
        auto source = an::createSerSource(engine, execution, path, {});
        ASSERT_TRUE(source.hasValue()) << source.status().message();
        auto read = source->read(engine, execution);
        ASSERT_TRUE(read.hasValue()) << read.status().message();
        kept = std::move(read->frame);
        EXPECT_TRUE(source->close().ok());
        EXPECT_TRUE(source->close().ok());
    }
    ASSERT_TRUE(kept);
    EXPECT_GE(engine.memoryResource()->counters().liveBytes, file.frameBytes());
    EXPECT_TRUE(std::equal(file.payload.begin(), from(file.payload, file.frameBytes()), kept->frame.bytes().begin()));
    kept.reset();
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);

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
                auto source = an::createSerSource(*wide, *workerExecution, path, {});
                if (!source)
                    return;
                bool all = true;
                for (std::uint64_t step = 0u; step < 6u && all; ++step)
                {
                    const std::uint64_t target = (step + worker) % 6u;
                    all = static_cast<bool>(source->seek(*workerExecution, {.index = target}));
                    auto read = source->read(*wide, *workerExecution);
                    all = all && read && read->frame &&
                          std::equal(from(file.payload, target * file.frameBytes()),
                                     from(file.payload, (target + 1u) * file.frameBytes()),
                                     read->frame->frame.bytes().begin());
                }
                matched[worker] = all;
            });
        for (auto& thread : threads)
            thread.join();
    }
    EXPECT_EQ(std::count(matched.begin(), matched.end(), true), 4);
    EXPECT_TRUE(wide->shutdown().ok());
}

TEST(SerTicksTest, ConversionsRoundTripAndRefuseTheUnrepresentable)
{
    static constexpr std::optional<an::UtcInstant> kEpoch = an::utcInstantFromSerTicks(an::kSerUnixEpochTicks);
    ASSERT_TRUE(kEpoch);
    EXPECT_EQ(kEpoch->nanosecondsSinceEpoch, 0);
    EXPECT_FALSE(an::utcInstantFromSerTicks(0u));
    EXPECT_FALSE(an::utcInstantFromSerTicks(1u));
    EXPECT_EQ(an::utcInstantFromSerTicks(an::kSerUnixEpochTicks + 1u)->nanosecondsSinceEpoch, 100);
    EXPECT_EQ(an::utcInstantFromSerTicks(an::kSerUnixEpochTicks - 1u)->nanosecondsSinceEpoch, -100);
    const auto reach = static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)() / 100);
    EXPECT_TRUE(an::utcInstantFromSerTicks(an::kSerUnixEpochTicks + reach));
    EXPECT_FALSE(an::utcInstantFromSerTicks(an::kSerUnixEpochTicks + reach + 1u));
    EXPECT_TRUE(an::utcInstantFromSerTicks(an::kSerUnixEpochTicks - reach));
    EXPECT_FALSE(an::utcInstantFromSerTicks(an::kSerUnixEpochTicks - reach - 1u));
    EXPECT_FALSE(an::utcInstantFromSerTicks((std::numeric_limits<std::uint64_t>::max)()));

    static constexpr std::uint64_t kFloor = an::serTicksFromUtcInstant({-1});
    EXPECT_EQ(kFloor, an::kSerUnixEpochTicks - 1u);
    EXPECT_EQ(an::serTicksFromUtcInstant({99}), an::kSerUnixEpochTicks);
    EXPECT_EQ(an::serTicksFromUtcInstant({100}), an::kSerUnixEpochTicks + 1u);
    for (const std::int64_t nanoseconds :
         {(std::numeric_limits<std::int64_t>::min)() + 100, std::int64_t{-12345678901}, std::int64_t{0},
          std::int64_t{98765432199}, (std::numeric_limits<std::int64_t>::max)()})
    {
        const std::uint64_t ticks = an::serTicksFromUtcInstant({nanoseconds});
        EXPECT_NE(ticks, 0u);
        const auto back = an::utcInstantFromSerTicks(ticks);
        ASSERT_TRUE(back) << nanoseconds;
        const std::int64_t floored = nanoseconds / 100 * 100 - (nanoseconds % 100 < 0 ? 100 : 0);
        EXPECT_EQ(back->nanosecondsSinceEpoch, floored) << nanoseconds;
    }

    // At the bottom of the range, the eight instants below the earliest tick a count reads back from take
    // that tick; every other instant rounds down as usual.
    static constexpr std::uint64_t kBottom = an::serTicksFromUtcInstant({(std::numeric_limits<std::int64_t>::min)()});
    EXPECT_EQ(kBottom, an::kSerUnixEpochTicks - 92233720368547758u);
    const std::int64_t lowest = (std::numeric_limits<std::int64_t>::min)();
    for (std::int64_t offset = 0; offset < 256; ++offset)
    {
        const std::int64_t nanoseconds = lowest + offset;
        const auto back = an::utcInstantFromSerTicks(an::serTicksFromUtcInstant({nanoseconds}));
        ASSERT_TRUE(back) << offset;
        if (offset < 8)
            EXPECT_EQ(back->nanosecondsSinceEpoch, lowest + 8) << offset;
        else
            EXPECT_TRUE(back->nanosecondsSinceEpoch <= nanoseconds && nanoseconds - back->nanosecondsSinceEpoch < 100)
                << offset;
    }
}

/// What the documented order of checks says a header and a container size produce, worked out from the
/// raw fields without the inspection: a refusal's code, reason and field, or the accepted container's
/// counts and description. Whether an extent and layout fit the limits is the image model's judgement.
struct HeaderVerdict
{
    std::optional<an::ErrorCode> refusal;
    std::optional<an::SequenceFailure> reason;
    std::string parameter;
    std::uint64_t frameCount = 0u;
    std::uint64_t frameBytes = 0u;
    std::uint64_t trailingBytes = 0u;
    bool trailerPresent = false;
    bool frameCountRecovered = false;
    bool channelOrderExchanged = false;
    std::uint32_t sampleBytes = 0u;
    an::ByteOrder byteOrder = an::ByteOrder::Native;
    an::CfaPattern pattern = an::CfaPattern::None;
};

HeaderVerdict expectedVerdict(std::span<const std::byte> header, std::uint64_t fileBytes,
                              const an::SerSourceOptions& options, const an::EngineLimits& limits)
{
    HeaderVerdict verdict;
    const auto refuse = [&verdict](an::ErrorCode code, std::optional<an::SequenceFailure> reason,
                                   const char* parameter) {
        verdict.refusal = code;
        verdict.reason = reason;
        verdict.parameter = parameter;
        return verdict;
    };
    if (static_cast<unsigned>(options.endianConvention) > 1u)
        return refuse(an::ErrorCode::InvalidArgument, std::nullopt, "endianConvention");
    if (static_cast<unsigned>(options.frameCountPolicy) > 1u)
        return refuse(an::ErrorCode::InvalidArgument, std::nullopt, "frameCountPolicy");
    if (static_cast<unsigned>(options.timelinePolicy) > 2u)
        return refuse(an::ErrorCode::InvalidArgument, std::nullopt, "timelinePolicy");
    if (static_cast<unsigned>(options.cmyMosaicPolicy) > 1u)
        return refuse(an::ErrorCode::InvalidArgument, std::nullopt, "cmyMosaicPolicy");
    if (fileBytes < an::kSerHeaderBytes)
        return refuse(an::ErrorCode::InvalidData, an::SequenceFailure::TruncatedContainer, "");
    const auto field = [&header](std::size_t offset) { return static_cast<std::int32_t>(getLe(header, offset, 4u)); };
    const std::int32_t colorId = field(18u);
    const bool bayer = colorId >= 8 && colorId <= 11;
    const bool cmy = colorId >= 16 && colorId <= 19;
    const bool rgb = colorId == 100 || colorId == 101;
    if (colorId != 0 && !bayer && !cmy && !rgb)
        return refuse(an::ErrorCode::InvalidData, an::SequenceFailure::MalformedContainer, "ColorID");
    if (cmy && options.cmyMosaicPolicy == an::SerCmyMosaicPolicy::Refuse)
        return refuse(an::ErrorCode::Unsupported, an::SequenceFailure::UnsupportedEncoding, "ColorID");
    const std::int32_t depth = field(34u);
    if (depth < 1 || depth > 16)
        return refuse(an::ErrorCode::InvalidData, an::SequenceFailure::MalformedContainer, "PixelDepthPerPlane");
    const std::int32_t width = field(26u);
    if (width <= 0)
        return refuse(an::ErrorCode::InvalidData, an::SequenceFailure::MalformedContainer, "ImageWidth");
    const std::int32_t height = field(30u);
    if (height <= 0)
        return refuse(an::ErrorCode::InvalidData, an::SequenceFailure::MalformedContainer, "ImageHeight");
    const std::uint32_t channels = rgb ? 3u : 1u;
    verdict.sampleBytes = depth > 8 ? 2u : 1u;
    const auto extent = an::ImageExtent::create(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height),
                                                channels, limits);
    if (!extent || !an::ImageLayout::create({*extent, verdict.sampleBytes, channels, 1u, 1u}, limits))
        return refuse(an::ErrorCode::LimitExceeded, an::SequenceFailure::DescriptorMismatch, "extent");
    verdict.frameBytes = std::uint64_t{static_cast<std::uint32_t>(width)} * static_cast<std::uint32_t>(height) *
                         channels * verdict.sampleBytes;
    const std::uint64_t payload = fileBytes - an::kSerHeaderBytes;
    const std::uint64_t capacity = payload / verdict.frameBytes;
    const std::uint64_t stated = getLe(header, 38u, 4u);
    verdict.frameCount = stated;
    if (stated > capacity || (stated == 0u && capacity > 0u))
    {
        if (options.frameCountPolicy == an::SerFrameCountPolicy::Exact)
            return stated > capacity
                       ? refuse(an::ErrorCode::InvalidData, an::SequenceFailure::TruncatedContainer, "FrameCount")
                       : refuse(an::ErrorCode::InvalidData, an::SequenceFailure::MalformedContainer, "FrameCount");
        verdict.frameCount = capacity;
        verdict.frameCountRecovered = true;
    }
    const std::uint64_t remaining = payload - verdict.frameCount * verdict.frameBytes;
    verdict.trailerPresent =
        !verdict.frameCountRecovered && verdict.frameCount > 0u && remaining / 8u >= verdict.frameCount;
    verdict.trailingBytes = remaining - (verdict.trailerPresent ? 8u * verdict.frameCount : 0u);
    verdict.channelOrderExchanged = colorId == 101;
    const std::int32_t flag = field(22u);
    const bool big = options.endianConvention == an::SerEndianConvention::Compatible ? flag == 1 : flag == 0;
    if (verdict.sampleBytes == 2u)
        verdict.byteOrder = big ? an::ByteOrder::Big : an::ByteOrder::Little;
    constexpr std::array<an::CfaPattern, 4u> kBayer{an::CfaPattern::Rggb, an::CfaPattern::Grbg, an::CfaPattern::Gbrg,
                                                    an::CfaPattern::Bggr};
    if (bayer)
        verdict.pattern = kBayer[static_cast<std::size_t>(colorId - 8)];
    return verdict;
}

TEST(SerContainerFuzzTest, TwentyThousandMutatedHeadersAreAnsweredEitherWay)
{
    const an::EngineLimits limits = an::defaultEngineLimits();
    std::vector<SerBytes> seeds(5u);
    seeds[1].colorId = 9;
    seeds[1].depth = 8;
    seeds[2].colorId = 100;
    seeds[2].depth = 12;
    seeds[3].colorId = 101;
    seeds[3].depth = 8;
    seeds[3].littleEndian = 1;
    seeds[4].colorId = 17;
    constexpr std::array<std::size_t, 8u> kFieldOffsets{14u, 18u, 22u, 26u, 30u, 34u, 38u, 162u};
    constexpr std::array<std::uint32_t, 11u> kEdges{0u, 1u, 2u, 8u, 9u, 16u, 17u, 100u, 101u, 0x7fffffffu, 0xffffffffu};
    Mutator mutator(0x5e5e5e5e20260913ull);
    std::uint64_t accepted = 0u;
    std::uint64_t refused = 0u;
    for (std::uint32_t round = 0u; round < 20000u; ++round)
    {
        const auto& seed = seeds[round % seeds.size()];
        auto header = seed.header();
        const auto mutations = 1u + mutator.below(6u);
        for (std::uint64_t mutation = 0u; mutation < mutations; ++mutation)
        {
            switch (mutator.below(4u))
            {
            case 0u:
                header[mutator.below(header.size())] = static_cast<std::byte>(mutator.next());
                break;
            case 1u:
                putLe(header, kFieldOffsets[mutator.below(kFieldOffsets.size())], kEdges[mutator.below(kEdges.size())],
                      4u);
                break;
            case 2u:
                putLe(header, kFieldOffsets[mutator.below(kFieldOffsets.size())],
                      static_cast<std::uint32_t>(mutator.next()), 4u);
                break;
            default:
                header[mutator.below(header.size())] ^= static_cast<std::byte>(1u << mutator.below(8u));
                break;
            }
        }
        const std::uint64_t frameBytes = seed.frameBytes();
        std::uint64_t fileBytes = 0u;
        switch (mutator.below(6u))
        {
        case 0u:
            fileBytes = an::kSerHeaderBytes + frameBytes * seed.frameCount;
            break;
        case 1u:
            fileBytes = an::kSerHeaderBytes + frameBytes * seed.frameCount + 8u * seed.frameCount;
            break;
        case 2u:
            fileBytes = mutator.below(an::kSerHeaderBytes + 1u);
            break;
        case 3u:
            fileBytes = mutator.next();
            break;
        case 4u:
            fileBytes = (std::numeric_limits<std::uint64_t>::max)() - mutator.below(4096u);
            break;
        default:
            fileBytes = an::kSerHeaderBytes + mutator.below(frameBytes * 8u + 64u);
            break;
        }
        an::SerSourceOptions options;
        options.endianConvention = static_cast<an::SerEndianConvention>(mutator.below(2u));
        options.frameCountPolicy = static_cast<an::SerFrameCountPolicy>(mutator.below(2u));
        options.timelinePolicy = static_cast<an::SerTimelinePolicy>(mutator.below(3u));
        options.cmyMosaicPolicy = static_cast<an::SerCmyMosaicPolicy>(mutator.below(2u));
        if (mutator.below(64u) == 0u)
            options.timelinePolicy = static_cast<an::SerTimelinePolicy>(7);
        if (mutator.below(64u) == 0u)
            options.cmyMosaicPolicy = static_cast<an::SerCmyMosaicPolicy>(3);
        const auto result = an::inspectSerContainer(header, fileBytes, options, limits);
        const HeaderVerdict expected = expectedVerdict(header, fileBytes, options, limits);
        if (!result.hasValue())
        {
            ++refused;
            ASSERT_TRUE(expected.refusal) << round << ": refused " << result.status().message();
            ASSERT_EQ(result.status().code(), *expected.refusal) << round;
            ASSERT_EQ(an::sequenceFailureReason(result.status()), expected.reason) << round;
            ASSERT_EQ(parameterOf(result.status()), expected.parameter) << round;
            ASSERT_EQ(result.status().domain(), an::ErrorDomain::Io) << round;
            continue;
        }
        ++accepted;
        ASSERT_FALSE(expected.refusal) << round << ": accepted what " << expected.parameter << " refuses";
        const auto& container = *result;
        ASSERT_EQ(container.frameCount, expected.frameCount) << round;
        ASSERT_EQ(container.layout.totalBytes(), expected.frameBytes) << round;
        ASSERT_EQ(container.trailerPresent, expected.trailerPresent) << round;
        ASSERT_EQ(container.frameCountRecovered, expected.frameCountRecovered) << round;
        ASSERT_EQ(container.trailingBytes, expected.trailingBytes) << round;
        ASSERT_EQ(container.channelOrderExchanged, expected.channelOrderExchanged) << round;
        ASSERT_EQ(container.fileBytes, fileBytes) << round;
        ASSERT_EQ(container.frame.sampleFormat().sizeBytes, expected.sampleBytes) << round;
        ASSERT_EQ(container.frame.byteOrder(), expected.byteOrder) << round;
        ASSERT_EQ(container.frame.cfa().pattern(), expected.pattern) << round;
        ASSERT_EQ(container.frame.extent().width(), static_cast<std::uint32_t>(container.header.imageWidth)) << round;
        ASSERT_EQ(container.frame.extent().height(), static_cast<std::uint32_t>(container.header.imageHeight)) << round;
        ASSERT_EQ(container.header.frameCount, getLe(header, 38u, 4u)) << round;
    }
    std::cout << "[          ] SER headers accepted " << accepted << ", refused " << refused << std::endl;
    EXPECT_GT(accepted, 1000u);
    EXPECT_GT(refused, 1000u);
}

TEST_F(SerSequenceTest, MutatedFilesAreReadCompletelyOrRefusedAlike)
{
    Mutator mutator(20260913u);
    const auto path = pathOf("fuzz.ser");
    const std::uint64_t start = an::kSerUnixEpochTicks + 15000000000000000u;
    std::uint64_t opened = 0u;
    std::uint64_t refused = 0u;
    for (std::uint32_t round = 0u; round < 400u; ++round)
    {
        SerBytes seed;
        seed.width = 3;
        seed.height = 2;
        seed.colorId = std::array{0, 8, 17, 100, 101}[round % 5u];
        seed.depth = round % 3u == 0u ? 8 : 16;
        seed.frameCount = 3u;
        seed.payload = pattern(seed.frameBytes() * 3u, round);
        if (mutator.below(2u) == 0u)
            seed.trailer = {start, start + mutator.below(3u), start + 5u};
        auto bytes = seed.file();
        const auto mutations = mutator.below(4u);
        for (std::uint64_t mutation = 0u; mutation < mutations; ++mutation)
            bytes[mutator.below(bytes.size())] = static_cast<std::byte>(mutator.next());
        if (mutator.below(3u) == 0u)
            bytes.resize(mutator.below(bytes.size() + 1u));
        overwrite(path, bytes);
        an::SerSourceOptions options;
        options.frameCountPolicy = static_cast<an::SerFrameCountPolicy>(mutator.below(2u));
        options.endianConvention = static_cast<an::SerEndianConvention>(mutator.below(2u));
        options.timelinePolicy = static_cast<an::SerTimelinePolicy>(mutator.below(3u));
        options.cmyMosaicPolicy = static_cast<an::SerCmyMosaicPolicy>(mutator.below(2u));
        const auto info = an::inspectSerFile(engine, execution, path, options);
        auto source = an::createSerSource(engine, execution, path, options);
        ASSERT_EQ(info.hasValue(), source.hasValue()) << round;
        if (!source.hasValue())
        {
            ++refused;
            EXPECT_EQ(info.status().code(), source.status().code()) << round;
            EXPECT_EQ(an::sequenceFailureReason(info.status()), an::sequenceFailureReason(source.status())) << round;
            EXPECT_EQ(parameterOf(info.status()), parameterOf(source.status())) << round;
            continue;
        }
        ++opened;
        const auto& container = info->container;
        const auto* descriptor = source->descriptor();
        ASSERT_EQ(descriptor->frameCount, container.frameCount) << round;
        ASSERT_EQ(*descriptor->fixedFrame, container.frame) << round;
        ASSERT_EQ(descriptor->timeBase.has_value(), info->timeline) << round;
        const std::uint64_t frameBytes = container.layout.totalBytes();
        const std::size_t sample = container.frame.sampleFormat().sizeBytes;
        const std::uint64_t trailerOffset = an::kSerHeaderBytes + container.frameCount * frameBytes;
        const auto tickAt = [&](std::uint64_t index) {
            return getLe(bytes, static_cast<std::size_t>(trailerOffset + 8u * index), 8u);
        };
        for (std::uint64_t index = 0u; index < container.frameCount; ++index)
        {
            auto read = source->read(engine, execution);
            ASSERT_TRUE(read.hasValue()) << round << ": " << read.status().message();
            ASSERT_TRUE(read->frame) << round;
            const std::uint64_t offset = an::kSerHeaderBytes + index * frameBytes;
            Bytes expected(from(bytes, offset), from(bytes, offset + frameBytes));
            if (container.channelOrderExchanged)
                for (std::size_t pixel = 0u; pixel + 3u * sample <= expected.size(); pixel += 3u * sample)
                    std::swap_ranges(expected.data() + pixel, expected.data() + pixel + sample,
                                     expected.data() + pixel + 2u * sample);
            ASSERT_TRUE(std::equal(expected.begin(), expected.end(), read->frame->frame.bytes().begin())) << round;
            const auto exposure = read->frame->metadata.acquisition().exposureStart();
            if (container.trailerPresent)
                ASSERT_EQ(exposure, an::utcInstantFromSerTicks(tickAt(index))) << round;
            else
                ASSERT_FALSE(exposure) << round;
            if (info->timeline)
                ASSERT_EQ(read->frame->position.presentationTicks,
                          static_cast<std::int64_t>(tickAt(index) - tickAt(0u)))
                    << round;
            else
                ASSERT_FALSE(read->frame->position.presentationTicks) << round;
        }
        auto end = source->read(engine, execution);
        ASSERT_TRUE(end.hasValue()) << round;
        EXPECT_FALSE(end->frame) << round;
    }
    std::cout << "[          ] SER files opened " << opened << ", refused " << refused << std::endl;
    EXPECT_GT(opened, 50u);
    EXPECT_GT(refused, 50u);
}

TEST_F(SerSequenceTest, WritesAContainerThatReadsBackBitExact)
{
    const auto frame = describe(5u, 4u, 1u, 2u, an::ByteOrder::Little);
    const auto layout = *an::ImageLayout::create({frame.extent(), 2u, 1u, 1u, 1u}, engine.limits());
    const std::vector<Bytes> samples{pattern(40u, 1u), pattern(40u, 2u), pattern(40u, 3u)};
    std::vector<an::SequenceFrame> frames;
    for (std::uint64_t index = 0u; index < 3u; ++index)
        frames.push_back(borrowed(frame, layout, samples[index], index));
    const auto bytes = writeContainer("written.ser", serDescriptor(frame),
                                      {.observer = "Ann",
                                       .instrument = "Cam",
                                       .telescope = "Scope",
                                       .dateTimeTicks = an::kSerUnixEpochTicks + 11u,
                                       .dateTimeUtcTicks = an::kSerUnixEpochTicks + 22u},
                                      frames);
    ASSERT_EQ(bytes.size(), an::kSerHeaderBytes + 120u);
    EXPECT_EQ(textAt(bytes, 0u, 14u), "LUCAM-RECORDER");
    EXPECT_EQ(getLe(bytes, 14u, 4u), 0u);
    EXPECT_EQ(getLe(bytes, 18u, 4u), 0u);
    EXPECT_EQ(getLe(bytes, 22u, 4u), 0u);
    EXPECT_EQ(getLe(bytes, 26u, 4u), 5u);
    EXPECT_EQ(getLe(bytes, 30u, 4u), 4u);
    EXPECT_EQ(getLe(bytes, 34u, 4u), 16u);
    EXPECT_EQ(getLe(bytes, 38u, 4u), 3u);
    EXPECT_EQ(textAt(bytes, 42u, 3u), "Ann");
    EXPECT_EQ(std::to_integer<int>(bytes[45u]), 0);
    EXPECT_EQ(textAt(bytes, 82u, 3u), "Cam");
    EXPECT_EQ(textAt(bytes, 122u, 5u), "Scope");
    EXPECT_EQ(getLe(bytes, 162u, 8u), an::kSerUnixEpochTicks + 11u);
    EXPECT_EQ(getLe(bytes, 170u, 8u), an::kSerUnixEpochTicks + 22u);
    for (std::uint64_t index = 0u; index < 3u; ++index)
        EXPECT_TRUE(
            std::equal(samples[index].begin(), samples[index].end(), from(bytes, an::kSerHeaderBytes + 40u * index)));

    auto source = an::createSerSource(engine, execution, pathOf("written.ser"), {});
    ASSERT_TRUE(source.hasValue()) << source.status().message();
    EXPECT_EQ(*source->descriptor()->fixedFrame, frame);
    for (std::uint64_t index = 0u; index < 3u; ++index)
    {
        auto read = source->read(engine, execution);
        ASSERT_TRUE(read.hasValue()) << read.status().message();
        EXPECT_TRUE(std::equal(samples[index].begin(), samples[index].end(), read->frame->frame.bytes().begin()));
        EXPECT_EQ(read->frame->metadata.acquisition().observerName(), "Ann");
    }
}

TEST_F(SerSequenceTest, WritesMosaicsColourAndBigEndianUnderEitherConvention)
{
    struct Mosaic
    {
        an::CfaPattern pattern;
        std::uint32_t originX;
        std::uint32_t originY;
        std::uint64_t colorId;
    };
    const std::array mosaics{Mosaic{an::CfaPattern::Rggb, 0u, 0u, 8u},  Mosaic{an::CfaPattern::Rggb, 1u, 0u, 9u},
                             Mosaic{an::CfaPattern::Rggb, 0u, 1u, 10u}, Mosaic{an::CfaPattern::Rggb, 1u, 1u, 11u},
                             Mosaic{an::CfaPattern::Bggr, 0u, 0u, 11u}, Mosaic{an::CfaPattern::Gbrg, 1u, 0u, 11u},
                             Mosaic{an::CfaPattern::Grbg, 0u, 0u, 9u}};
    for (const auto& mosaic : mosaics)
    {
        const auto frame = describe(4u, 2u, 1u, 1u, an::ByteOrder::Native, an::ChannelSchema::Mono,
                                    *an::CfaDescriptor::create(mosaic.pattern, mosaic.originX, mosaic.originY));
        const auto layout = *an::ImageLayout::create({frame.extent(), 1u, 1u, 1u, 1u}, engine.limits());
        const Bytes samples = pattern(8u, static_cast<std::uint32_t>(mosaic.colorId));
        std::vector<an::SequenceFrame> frames;
        frames.push_back(borrowed(frame, layout, samples, 0u));
        const auto bytes = writeContainer("mosaic.ser", serDescriptor(frame),
                                          {.endianConvention = an::SerEndianConvention::Specification}, frames);
        ASSERT_EQ(bytes.size(), an::kSerHeaderBytes + 8u);
        EXPECT_EQ(getLe(bytes, 18u, 4u), mosaic.colorId);
        EXPECT_EQ(getLe(bytes, 22u, 4u), 1u);
        EXPECT_EQ(getLe(bytes, 34u, 4u), 8u);
        auto container = inspect(bytes, {.endianConvention = an::SerEndianConvention::Specification});
        ASSERT_TRUE(container.hasValue()) << container.status().message();
        for (std::uint32_t y = 0u; y < 2u; ++y)
            for (std::uint32_t x = 0u; x < 4u; ++x)
                EXPECT_EQ(container->frame.cfa().colorAt(x, y), frame.cfa().colorAt(x, y));
    }

    const auto rgb = describe(3u, 2u, 3u, 2u, an::ByteOrder::Big, an::ChannelSchema::Rgb);
    const auto interleaved = *an::ImageLayout::create({rgb.extent(), 2u, 3u, 1u, 1u}, engine.limits());
    const Bytes samples = pattern(36u, 77u);
    for (const auto convention : {an::SerEndianConvention::Compatible, an::SerEndianConvention::Specification})
    {
        std::vector<an::SequenceFrame> frames;
        frames.push_back(borrowed(rgb, interleaved, samples, 0u));
        const auto bytes = writeContainer("rgb.ser", serDescriptor(rgb), {.endianConvention = convention}, frames);
        ASSERT_EQ(bytes.size(), an::kSerHeaderBytes + 36u);
        EXPECT_EQ(getLe(bytes, 18u, 4u), 100u);
        EXPECT_EQ(getLe(bytes, 22u, 4u), convention == an::SerEndianConvention::Compatible ? 1u : 0u);
        EXPECT_TRUE(std::equal(samples.begin(), samples.end(), from(bytes, an::kSerHeaderBytes)));
        auto same = inspect(bytes, {.endianConvention = convention});
        ASSERT_TRUE(same.hasValue()) << same.status().message();
        EXPECT_EQ(same->frame, rgb);
        const auto other = convention == an::SerEndianConvention::Compatible ? an::SerEndianConvention::Specification
                                                                             : an::SerEndianConvention::Compatible;
        auto misread = inspect(bytes, {.endianConvention = other});
        ASSERT_TRUE(misread.hasValue()) << misread.status().message();
        EXPECT_EQ(misread->frame.byteOrder(), an::ByteOrder::Little);
    }

    const auto hostOrder = describe(2u, 1u, 1u, 2u, an::ByteOrder::Native);
    const auto hostLayout = *an::ImageLayout::create({hostOrder.extent(), 2u, 1u, 1u, 1u}, engine.limits());
    const Bytes hostSamples = pattern(4u, 5u);
    std::vector<an::SequenceFrame> hostFrames;
    hostFrames.push_back(borrowed(hostOrder, hostLayout, hostSamples, 0u));
    const auto hostBytes = writeContainer("host.ser", serDescriptor(hostOrder), {}, hostFrames);
    ASSERT_EQ(hostBytes.size(), an::kSerHeaderBytes + 4u);
    EXPECT_EQ(getLe(hostBytes, 22u, 4u), an::nativeByteOrder() == an::ByteOrder::Big ? 1u : 0u);
    auto host = inspect(hostBytes);
    ASSERT_TRUE(host.hasValue()) << host.status().message();
    EXPECT_EQ(host->frame.byteOrder(), an::nativeByteOrder());
}

TEST_F(SerSequenceTest, InterleavesPlanarRgbAndDropsRowPadding)
{
    const auto frame = describe(3u, 2u, 3u, 1u, an::ByteOrder::Native, an::ChannelSchema::Rgb);
    const auto expected = [](std::size_t channel, std::size_t y, std::size_t x) {
        return static_cast<std::byte>(channel * 16u + y * 4u + x);
    };
    const auto planar = *an::ImageLayout::createWithStrides({frame.extent(), 1u, 1u, 3u, 1u}, 4u, 8u, engine.limits());
    Bytes planes(planar.totalBytes(), std::byte{0xEE});
    for (std::size_t channel = 0u; channel < 3u; ++channel)
        for (std::size_t y = 0u; y < 2u; ++y)
            for (std::size_t x = 0u; x < 3u; ++x)
                planes[channel * 8u + y * 4u + x] = expected(channel, y, x);
    std::vector<an::SequenceFrame> planarFrames;
    planarFrames.push_back(borrowed(frame, planar, planes, 0u));
    const auto fromPlanes = writeContainer("planar.ser", serDescriptor(frame), {}, planarFrames);
    ASSERT_EQ(fromPlanes.size(), an::kSerHeaderBytes + 18u);
    for (std::size_t y = 0u; y < 2u; ++y)
        for (std::size_t x = 0u; x < 3u; ++x)
            for (std::size_t channel = 0u; channel < 3u; ++channel)
                EXPECT_EQ(fromPlanes[an::kSerHeaderBytes + (y * 3u + x) * 3u + channel], expected(channel, y, x));

    const auto padded =
        *an::ImageLayout::createWithStrides({frame.extent(), 1u, 3u, 1u, 1u}, 16u, 32u, engine.limits());
    Bytes rows(padded.totalBytes(), std::byte{0xEE});
    for (std::size_t y = 0u; y < 2u; ++y)
        for (std::size_t x = 0u; x < 3u; ++x)
            for (std::size_t channel = 0u; channel < 3u; ++channel)
                rows[y * 16u + x * 3u + channel] = expected(channel, y, x);
    std::vector<an::SequenceFrame> paddedFrames;
    paddedFrames.push_back(borrowed(frame, padded, rows, 0u));
    const auto fromRows = writeContainer("padded.ser", serDescriptor(frame), {}, paddedFrames);
    EXPECT_EQ(fromRows, fromPlanes);
}

TEST_F(SerSequenceTest, RefusesWhatTheContainerCannotStoreBeforeWritingAByte)
{
    const auto destination = pathOf("refused.ser");
    auto output = an::SequenceOutput::begin(engine, execution, {.destinationPath = destination});
    ASSERT_TRUE(output.hasValue()) << output.status().message();
    const std::string pending(output->pendingPath());
    // A payload that already holds bytes shows whether a refusal wrote, or discarded, anything at all.
    const Bytes sentinel = pattern(4096u, 99u);
    overwrite(pending, sentinel);
    const auto mono16 = describe(4u, 2u, 1u, 2u, an::ByteOrder::Little);
    const auto mono8 = describe(4u, 2u, 1u, 1u, an::ByteOrder::Native);
    const auto stated = [](const an::RawFrameDescriptor& frame) { return serDescriptor(frame); };
    constexpr std::string_view kFortyOne = "abcdefghijklmnopqrstuvwxyzabcdefghijklmno";

    struct Case
    {
        const char* name;
        an::SequenceDescriptor descriptor;
        an::SerSinkRequest request;
        an::ErrorCode code;
        const char* parameter;
    };
    an::SequenceDescriptor timed = stated(mono16);
    timed.timeBase = an::SequenceRate{1u, 1000u};
    timed.timestampsRequired = true;
    an::SequenceDescriptor untimed = stated(mono16);
    untimed.timeBase = an::SequenceRate{1u, 10000000u};
    const std::vector<Case> cases{
        {"another format",
         {.format = an::SequenceFormat::Avi, .fixedFrame = mono16},
         {},
         an::ErrorCode::InvalidArgument,
         "format"},
        {"no fixed frame", {.format = an::SequenceFormat::Ser}, {}, an::ErrorCode::InvalidArgument, "fixedFrame"},
        {"seekable",
         {.format = an::SequenceFormat::Ser, .fixedFrame = mono16, .seekByIndex = true},
         {},
         an::ErrorCode::InvalidArgument,
         "seekByIndex"},
        {"floating samples",
         stated(describe(4u, 2u, 1u, 4u, an::ByteOrder::Little, an::ChannelSchema::Mono, {},
                         an::CoordinateOrigin::TopLeft, 1.0, an::RawSampleClass::FloatingPoint)),
         {},
         an::ErrorCode::Unsupported,
         "sampleFormat"},
        {"four-byte samples",
         stated(describe(4u, 2u, 1u, 4u, an::ByteOrder::Little)),
         {},
         an::ErrorCode::Unsupported,
         "sampleFormat"},
        {"signed samples",
         stated(describe(4u, 2u, 1u, 2u, an::ByteOrder::Little, an::ChannelSchema::Mono, {},
                         an::CoordinateOrigin::TopLeft, 1.0, an::RawSampleClass::SignedInteger)),
         {},
         an::ErrorCode::Unsupported,
         "sampleFormat"},
        {"six-by-six mosaic",
         stated(describe(6u, 6u, 1u, 1u, an::ByteOrder::Native, an::ChannelSchema::Mono,
                         *an::CfaDescriptor::create(xTransEntries(), 0u, 0u))),
         {},
         an::ErrorCode::Unsupported,
         "cfa"},
        {"bottom-left origin",
         stated(describe(4u, 2u, 1u, 1u, an::ByteOrder::Native, an::ChannelSchema::Mono, {},
                         an::CoordinateOrigin::BottomLeft)),
         {},
         an::ErrorCode::Unsupported,
         "coordinateOrigin"},
        {"tall pixels",
         stated(describe(4u, 2u, 1u, 1u, an::ByteOrder::Native, an::ChannelSchema::Mono, {},
                         an::CoordinateOrigin::TopLeft, 2.0)),
         {},
         an::ErrorCode::Unsupported,
         "pixelAspectRatio"},
        {"alpha",
         stated(describe(4u, 2u, 4u, 1u, an::ByteOrder::Native, an::ChannelSchema::Rgba)),
         {},
         an::ErrorCode::Unsupported,
         "channelSchema"},
        {"named channels",
         stated(describe(4u, 2u, 2u, 1u, an::ByteOrder::Native, an::ChannelSchema::Named)),
         {},
         an::ErrorCode::Unsupported,
         "channelSchema"},
        {"shallow depth for two bytes",
         stated(mono16),
         {.pixelDepthPerPlane = 8u},
         an::ErrorCode::InvalidArgument,
         "pixelDepthPerPlane"},
        {"deep depth for one byte",
         stated(mono8),
         {.pixelDepthPerPlane = 9u},
         an::ErrorCode::InvalidArgument,
         "pixelDepthPerPlane"},
        {"deeper than sixteen",
         stated(mono16),
         {.pixelDepthPerPlane = 17u},
         an::ErrorCode::InvalidArgument,
         "pixelDepthPerPlane"},
        {"long identifier", stated(mono16), {.fileId = "LUCAM-RECORDER!"}, an::ErrorCode::InvalidArgument, "fileId"},
        {"long observer", stated(mono16), {.observer = kFortyOne}, an::ErrorCode::InvalidArgument, "observer"},
        {"invalid instrument", stated(mono16), {.instrument = "\xff"}, an::ErrorCode::InvalidArgument, "instrument"},
        {"telescope with a NUL",
         stated(mono16),
         {.telescope = std::string_view("a\0b", 3u)},
         an::ErrorCode::InvalidArgument,
         "telescope"},
        {"date beyond the stored range",
         stated(mono16),
         {.dateTimeTicks = (std::numeric_limits<std::uint64_t>::max)()},
         an::ErrorCode::InvalidArgument,
         "dateTimeTicks"},
        {"UTC date beyond the stored range",
         stated(mono16),
         {.dateTimeUtcTicks = std::uint64_t{1} << 63u},
         an::ErrorCode::InvalidArgument,
         "dateTimeUtcTicks"},
        {"unknown convention",
         stated(mono16),
         {.endianConvention = static_cast<an::SerEndianConvention>(5)},
         an::ErrorCode::InvalidArgument,
         "endianConvention"},
        {"unknown timestamp source",
         stated(mono16),
         {.timestamps = static_cast<an::SerTimestampSource>(9)},
         an::ErrorCode::InvalidArgument,
         "timestamps"},
        {"origin without ticks",
         stated(mono16),
         {.timelineOriginTicks = 5u},
         an::ErrorCode::InvalidArgument,
         "timelineOriginTicks"},
        {"ticks without an origin",
         timed,
         {.timestamps = an::SerTimestampSource::PresentationTicks},
         an::ErrorCode::InvalidArgument,
         "timelineOriginTicks"},
        {"ticks in another time base",
         timed,
         {.timestamps = an::SerTimestampSource::PresentationTicks, .timelineOriginTicks = 5u},
         an::ErrorCode::IncompatibleArguments,
         "timeBase"},
        {"ticks that are not required",
         untimed,
         {.timestamps = an::SerTimestampSource::PresentationTicks, .timelineOriginTicks = 5u},
         an::ErrorCode::IncompatibleArguments,
         "timestampsRequired"},
        {"too many frames",
         serDescriptor(mono16, std::uint64_t{1} << 31u),
         {},
         an::ErrorCode::LimitExceeded,
         "frameCount"},
    };
    for (const auto& entry : cases)
    {
        auto sink = an::createSerSink(engine, execution, *output, entry.descriptor, entry.request);
        ASSERT_FALSE(sink.hasValue()) << entry.name;
        EXPECT_EQ(sink.status().code(), entry.code) << entry.name << ": " << sink.status().message();
        EXPECT_EQ(parameterOf(sink.status()), entry.parameter) << entry.name;
        EXPECT_TRUE(output->valid()) << entry.name;
        EXPECT_EQ(readAll(pending), sentinel) << entry.name;
    }
    auto unsupported = an::createSerSink(engine, execution, *output, cases[3].descriptor, {});
    EXPECT_EQ(an::sequenceFailureReason(unsupported.status()), an::SequenceFailure::UnsupportedEncoding);

    an::SequenceOutput empty;
    EXPECT_EQ(an::createSerSink(engine, execution, empty, stated(mono16), {}).status().code(),
              an::ErrorCode::InvalidState);

    // Frames beyond the engine's limits are refused before the payload is claimed, rather than by the
    // session after a header has been written.
    auto limitedOptions = an::EngineContextOptions{.maxConcurrency = 1u};
    limitedOptions.limits.maxImageBytes = 15u;
    auto limited = an::EngineContext::create(limitedOptions);
    ASSERT_TRUE(limited.hasValue());
    {
        auto limitedExecution = an::ExecutionContext::create(*limited, {});
        ASSERT_TRUE(limitedExecution.hasValue());
        auto small = an::SequenceOutput::begin(*limited, *limitedExecution, {.destinationPath = pathOf("limited.ser")});
        ASSERT_TRUE(small.hasValue()) << small.status().message();
        overwrite(small->pendingPath(), sentinel);
        auto beyond = an::createSerSink(*limited, *limitedExecution, *small, stated(mono16), {});
        ASSERT_FALSE(beyond.hasValue());
        EXPECT_EQ(beyond.status().code(), an::ErrorCode::LimitExceeded) << beyond.status().message();
        EXPECT_TRUE(chainCarries(beyond.status(), an::SequenceFailure::DescriptorMismatch));
        EXPECT_EQ(readAll(small->pendingPath()), sentinel);
    }
    EXPECT_TRUE(limited->shutdown().ok());

    auto other = an::EngineContext::create({.maxConcurrency = 1u});
    ASSERT_TRUE(other.hasValue());
    {
        auto otherExecution = an::ExecutionContext::create(*other, {});
        ASSERT_TRUE(otherExecution.hasValue());
        auto foreign = an::SequenceOutput::begin(*other, *otherExecution, {.destinationPath = pathOf("foreign.ser")});
        ASSERT_TRUE(foreign.hasValue()) << foreign.status().message();
        EXPECT_EQ(an::createSerSink(engine, execution, *foreign, stated(mono16), {}).status().code(),
                  an::ErrorCode::IncompatibleArguments);
        EXPECT_EQ(an::createSerSink(*other, execution, *foreign, stated(mono16), {}).status().code(),
                  an::ErrorCode::IncompatibleArguments);
    }
    EXPECT_TRUE(other->shutdown().ok());

    // Once a request is accepted, the payload's old bytes are discarded ahead of the header.
    const auto layout = *an::ImageLayout::create({mono16.extent(), 2u, 1u, 1u, 1u}, engine.limits());
    const Bytes samples = pattern(16u, 98u);
    {
        auto sink = an::createSerSink(engine, execution, *output, stated(mono16), {});
        ASSERT_TRUE(sink.hasValue()) << sink.status().message();
        ASSERT_TRUE(sink->append(engine, execution, borrowed(mono16, layout, samples, 0u)).hasValue());
        auto finished = sink->finish(execution);
        ASSERT_TRUE(finished.hasValue()) << finished.status().message();
        auto committed = output->commit(execution, *finished);
        ASSERT_TRUE(committed.hasValue()) << committed.status().message();
    }
    const auto bytes = readAll(destination);
    ASSERT_EQ(bytes.size(), an::kSerHeaderBytes + 16u);
    EXPECT_TRUE(std::equal(samples.begin(), samples.end(), from(bytes, an::kSerHeaderBytes)));
}

TEST_F(SerSequenceTest, WritesTimestampsFromPresentationTicksOrExposureStarts)
{
    const auto frame = describe(2u, 2u, 1u, 1u, an::ByteOrder::Native);
    const auto layout = *an::ImageLayout::create({frame.extent(), 1u, 1u, 1u, 1u}, engine.limits());
    const Bytes samples = pattern(4u, 9u);
    const std::uint64_t origin = an::kSerUnixEpochTicks + 16500000000000000u;
    {
        auto descriptor = serDescriptor(frame, 3u);
        descriptor.timeBase = an::SequenceRate{1u, 10000000u};
        descriptor.timestampsRequired = true;
        std::vector<an::SequenceFrame> frames;
        for (std::uint64_t index = 0u; index < 3u; ++index)
            frames.push_back(borrowed(frame, layout, samples, index, static_cast<std::int64_t>(index * 400000u)));
        const auto bytes = writeContainer(
            "ticks.ser", descriptor,
            {.timestamps = an::SerTimestampSource::PresentationTicks, .timelineOriginTicks = origin}, frames);
        ASSERT_EQ(bytes.size(), an::kSerHeaderBytes + 12u + 24u);
        for (std::uint64_t index = 0u; index < 3u; ++index)
            EXPECT_EQ(getLe(bytes, an::kSerHeaderBytes + 12u + 8u * index, 8u), origin + index * 400000u);
        auto info = an::inspectSerFile(engine, execution, pathOf("ticks.ser"), {});
        ASSERT_TRUE(info.hasValue()) << info.status().message();
        EXPECT_TRUE(info->timeline);
        EXPECT_EQ(info->firstTimestamp, an::utcInstantFromSerTicks(origin));
    }
    {
        const an::UtcInstant first{1700000000123456700};
        const an::UtcInstant third{1700000000223456789};
        std::vector<an::SequenceFrame> frames;
        frames.push_back(borrowed(frame, layout, samples, 0u, std::nullopt,
                                  an::ImageMetadata::create(*an::AcquisitionMetadata::create({.exposureStart = first}),
                                                            {}, {}, {}, {}, std::nullopt)));
        frames.push_back(borrowed(frame, layout, samples, 1u));
        frames.push_back(borrowed(frame, layout, samples, 2u, std::nullopt,
                                  an::ImageMetadata::create(*an::AcquisitionMetadata::create({.exposureStart = third}),
                                                            {}, {}, {}, {}, std::nullopt)));
        const auto bytes = writeContainer("exposures.ser", serDescriptor(frame),
                                          {.timestamps = an::SerTimestampSource::ExposureStart}, frames);
        ASSERT_EQ(bytes.size(), an::kSerHeaderBytes + 12u + 24u);
        EXPECT_EQ(getLe(bytes, an::kSerHeaderBytes + 12u, 8u), an::serTicksFromUtcInstant(first));
        EXPECT_EQ(getLe(bytes, an::kSerHeaderBytes + 20u, 8u), 0u);
        EXPECT_EQ(getLe(bytes, an::kSerHeaderBytes + 28u, 8u), an::serTicksFromUtcInstant(third));
        auto source = an::createSerSource(engine, execution, pathOf("exposures.ser"), {});
        ASSERT_TRUE(source.hasValue()) << source.status().message();
        EXPECT_FALSE(source->descriptor()->timeBase);
        const std::array<std::optional<an::UtcInstant>, 3u> expected{first, std::nullopt,
                                                                     an::UtcInstant{1700000000223456700}};
        for (const auto& instant : expected)
        {
            auto read = source->read(engine, execution);
            ASSERT_TRUE(read.hasValue()) << read.status().message();
            EXPECT_EQ(read->frame->metadata.acquisition().exposureStart(), instant);
        }
    }
}

TEST_F(SerSequenceTest, ATimelineWrittenAtAFrameRateReadsBackAtThatRate)
{
    const auto frame = describe(4u, 3u, 1u, 2u, an::ByteOrder::Little);
    const auto layout = *an::ImageLayout::create({frame.extent(), 2u, 1u, 1u, 1u}, engine.limits());
    const Bytes samples = pattern(24u, 4u);
    auto descriptor = serDescriptor(frame);
    descriptor.timeBase = an::SequenceRate{1u, 10000000u};
    descriptor.timestampsRequired = true;
    constexpr std::int64_t kInterval = 800000;
    std::vector<an::SequenceFrame> frames;
    for (std::uint64_t index = 0u; index < 3u; ++index)
        frames.push_back(borrowed(frame, layout, samples, index, static_cast<std::int64_t>(index) * kInterval));
    const auto bytes = writeContainer(
        "rate.ser", descriptor,
        {.timestamps = an::SerTimestampSource::PresentationTicks, .timelineOriginTicks = an::kSerUnixEpochTicks},
        frames);
    ASSERT_FALSE(bytes.empty());
    auto info = an::inspectSerFile(engine, execution, pathOf("rate.ser"), {});
    ASSERT_TRUE(info.hasValue()) << info.status().message();
    ASSERT_TRUE(info->timestampFrameRate);
    EXPECT_NEAR(*info->timestampFrameRate, 12.5, 0.001);
}

TEST_F(SerSequenceTest, InheritsHeaderDatesThroughAnExplicitInspection)
{
    auto parentFile = monoFile(1u);
    parentFile.dateTime = an::kSerUnixEpochTicks + 1234u;
    parentFile.dateTimeUtc = an::kSerUnixEpochTicks + 5678u;
    auto parent = an::inspectSerFile(engine, execution, write("parent.ser", parentFile.file()), {});
    ASSERT_TRUE(parent.hasValue()) << parent.status().message();
    std::vector<an::SequenceFrame> frames;
    frames.push_back(borrowed(parent->container.frame, parent->container.layout, parentFile.payload, 0u));
    const auto bytes = writeContainer("child.ser", serDescriptor(parent->container.frame),
                                      {.dateTimeTicks = parent->container.header.dateTimeTicks,
                                       .dateTimeUtcTicks = parent->container.header.dateTimeUtcTicks},
                                      frames);
    EXPECT_EQ(bytes, parentFile.file());
}

TEST_F(SerSequenceTest, AnUnfinishedSinkLeavesNothingBehindAndNothingIsPublished)
{
    const auto destination = pathOf("unfinished.ser");
    auto output = an::SequenceOutput::begin(engine, execution, {.destinationPath = destination});
    ASSERT_TRUE(output.hasValue()) << output.status().message();
    const std::string pending(output->pendingPath());
    const auto frame = describe(5u, 4u, 1u, 2u, an::ByteOrder::Little);
    const auto layout = *an::ImageLayout::create({frame.extent(), 2u, 1u, 1u, 1u}, engine.limits());
    const Bytes samples = pattern(40u, 5u);
    an::SequenceWriteResult progress;
    {
        auto sink = an::createSerSink(engine, execution, *output, serDescriptor(frame), {});
        ASSERT_TRUE(sink.hasValue()) << sink.status().message();
        auto appended = sink->append(engine, execution, borrowed(frame, layout, samples, 0u));
        ASSERT_TRUE(appended.hasValue()) << appended.status().message();
        EXPECT_EQ(appended->framesWritten, 1u);
        EXPECT_EQ(appended->bytesWritten, an::kSerHeaderBytes + 40u);
        progress = *appended;
        EXPECT_TRUE(sink->close().ok());
        EXPECT_TRUE(sink->close().ok());
    }
    EXPECT_EQ(std::filesystem::file_size(native(pending)), 0u);
    auto refused = output->commit(execution, progress);
    EXPECT_EQ(an::codecFailureReason(refused.status()), an::CodecFailure::ShortWrite);
    EXPECT_FALSE(std::filesystem::exists(native(destination)));
    EXPECT_FALSE(std::filesystem::exists(native(pending)));

    auto retry = an::SequenceOutput::begin(engine, execution, {.destinationPath = destination});
    ASSERT_TRUE(retry.hasValue()) << retry.status().message();
    {
        auto sink = an::createSerSink(engine, execution, *retry, serDescriptor(frame), {});
        ASSERT_TRUE(sink.hasValue()) << sink.status().message();
        ASSERT_TRUE(sink->append(engine, execution, borrowed(frame, layout, samples, 0u)).hasValue());
    }
    EXPECT_EQ(std::filesystem::file_size(native(retry->pendingPath())), 0u);
    retry->abort();
    EXPECT_TRUE(std::filesystem::is_empty(directory));
}

TEST_F(SerSequenceTest, AKnownFrameCountIsWrittenUpfrontAndHeld)
{
    const auto destination = pathOf("counted.ser");
    auto output = an::SequenceOutput::begin(
        engine, execution, {.destinationPath = destination, .durability = an::OutputDurability::CloseOnly});
    ASSERT_TRUE(output.hasValue()) << output.status().message();
    const auto frame = describe(2u, 2u, 1u, 1u, an::ByteOrder::Native);
    const auto layout = *an::ImageLayout::create({frame.extent(), 1u, 1u, 1u, 1u}, engine.limits());
    const Bytes samples = pattern(4u, 6u);
    auto sink = an::createSerSink(engine, execution, *output, serDescriptor(frame, 2u), {});
    ASSERT_TRUE(sink.hasValue()) << sink.status().message();
    // The count is in the header from the start, not only once the final count is written.
    const auto early = readAll(output->pendingPath());
    ASSERT_EQ(early.size(), an::kSerHeaderBytes);
    EXPECT_EQ(getLe(early, 38u, 4u), 2u);
    ASSERT_TRUE(sink->append(engine, execution, borrowed(frame, layout, samples, 0u)).hasValue());
    EXPECT_EQ(sink->finish(execution).status().code(), an::ErrorCode::InvalidState);
    EXPECT_EQ(sink->state(), an::SequenceState::Open);
    ASSERT_TRUE(sink->append(engine, execution, borrowed(frame, layout, samples, 1u)).hasValue());
    EXPECT_EQ(sink->append(engine, execution, borrowed(frame, layout, samples, 2u)).status().code(),
              an::ErrorCode::OutOfRange);
    EXPECT_EQ(sink->state(), an::SequenceState::Open);
    auto finished = sink->finish(execution);
    ASSERT_TRUE(finished.hasValue()) << finished.status().message();
    EXPECT_EQ(finished->framesWritten, 2u);
    EXPECT_EQ(finished->bytesWritten, an::kSerHeaderBytes + 8u);
    auto committed = output->commit(execution, *finished);
    ASSERT_TRUE(committed.hasValue()) << committed.status().message();
    const auto bytes = readAll(destination);
    ASSERT_EQ(bytes.size(), an::kSerHeaderBytes + 8u);
    EXPECT_EQ(getLe(bytes, 38u, 4u), 2u);
    auto repeated = sink->finish(execution);
    ASSERT_TRUE(repeated.hasValue());
    EXPECT_EQ(repeated->bytesWritten, an::kSerHeaderBytes + 8u);
}

TEST_F(SerSequenceTest, CancellationDuringAppendFailsTheSinkAndLeavesNothing)
{
    const auto destination = pathOf("cancelled.ser");
    auto output = an::SequenceOutput::begin(engine, execution, {.destinationPath = destination});
    ASSERT_TRUE(output.hasValue()) << output.status().message();
    const auto frame = describe(5u, 4u, 1u, 2u, an::ByteOrder::Little);
    const auto layout = *an::ImageLayout::create({frame.extent(), 2u, 1u, 1u, 1u}, engine.limits());
    const Bytes samples = pattern(40u, 8u);
    std::stop_source stop;
    StopOnTrace trace(stop, "frame-bytes-written");
    auto stopping = an::ExecutionContext::create(engine, {.stopToken = stop.get_token(), .traceSink = &trace});
    ASSERT_TRUE(stopping.hasValue());
    auto sink = an::createSerSink(engine, *stopping, *output, serDescriptor(frame), {});
    ASSERT_TRUE(sink.hasValue()) << sink.status().message();
    auto appended = sink->append(engine, *stopping, borrowed(frame, layout, samples, 0u));
    EXPECT_EQ(appended.status().code(), an::ErrorCode::Cancelled);
    EXPECT_EQ(sink->state(), an::SequenceState::Failed);
    EXPECT_EQ(sink->progress().framesWritten, 0u);
    EXPECT_TRUE(sink->close().ok());
    EXPECT_EQ(std::filesystem::file_size(native(output->pendingPath())), 0u);
    output->abort();

    auto late = an::SequenceOutput::begin(engine, execution, {.destinationPath = destination});
    ASSERT_TRUE(late.hasValue()) << late.status().message();
    EXPECT_EQ(an::createSerSink(engine, *stopping, *late, serDescriptor(frame), {}).status().code(),
              an::ErrorCode::Cancelled);
    EXPECT_EQ(std::filesystem::file_size(native(late->pendingPath())), 0u);
}

TEST_F(SerSequenceTest, AnOutputWaitsForItsOpenSinkAndOnlyOneSinkWritesAPayload)
{
    const auto destination = pathOf("claimed.ser");
    auto output = an::SequenceOutput::begin(
        engine, execution, {.destinationPath = destination, .durability = an::OutputDurability::CloseOnly});
    ASSERT_TRUE(output.hasValue()) << output.status().message();
    const std::string pending(output->pendingPath());
    const auto frame = describe(5u, 4u, 1u, 2u, an::ByteOrder::Little);
    const auto layout = *an::ImageLayout::create({frame.extent(), 2u, 1u, 1u, 1u}, engine.limits());
    const Bytes samples = pattern(40u, 12u);
    auto sink = an::createSerSink(engine, execution, *output, serDescriptor(frame), {});
    ASSERT_TRUE(sink.hasValue()) << sink.status().message();
    auto appended = sink->append(engine, execution, borrowed(frame, layout, samples, 0u));
    ASSERT_TRUE(appended.hasValue()) << appended.status().message();

    // An append's report matches the payload byte for byte and is still no finished container, so the
    // output refuses it for as long as the sink may write, and keeps the payload.
    auto early = output->commit(execution, *appended);
    ASSERT_FALSE(early.hasValue());
    EXPECT_EQ(early.status().code(), an::ErrorCode::InvalidState);
    EXPECT_TRUE(output->valid());
    EXPECT_FALSE(std::filesystem::exists(native(destination)));

    // A second sink would discard what the first is writing.
    auto second = an::createSerSink(engine, execution, *output, serDescriptor(frame), {});
    ASSERT_FALSE(second.hasValue());
    EXPECT_EQ(second.status().code(), an::ErrorCode::InvalidState);
    EXPECT_EQ(readAll(pending).size(), an::kSerHeaderBytes + 40u);

    ASSERT_TRUE(sink->append(engine, execution, borrowed(frame, layout, samples, 1u)).hasValue());
    auto finished = sink->finish(execution);
    ASSERT_TRUE(finished.hasValue()) << finished.status().message();
    EXPECT_EQ(an::createSerSink(engine, execution, *output, serDescriptor(frame), {}).status().code(),
              an::ErrorCode::InvalidState);
    auto committed = output->commit(execution, *finished);
    ASSERT_TRUE(committed.hasValue()) << committed.status().message();
    EXPECT_TRUE(sink->close().ok());
    const auto bytes = readAll(destination);
    ASSERT_EQ(bytes.size(), an::kSerHeaderBytes + 80u);
    EXPECT_EQ(getLe(bytes, 38u, 4u), 2u);
    EXPECT_TRUE(std::equal(samples.begin(), samples.end(), from(bytes, an::kSerHeaderBytes + 40u)));
}

TEST_F(SerSequenceTest, AnOutputEndedUnderAnOpenSinkLeavesRemovingThePayloadToIt)
{
    const auto frame = describe(5u, 4u, 1u, 2u, an::ByteOrder::Little);
    const auto layout = *an::ImageLayout::create({frame.extent(), 2u, 1u, 1u, 1u}, engine.limits());
    const Bytes samples = pattern(40u, 13u);

    // Closed unfinished, a sink gives the payload back, and another sink may then write it.
    {
        const auto destination = pathOf("retried.ser");
        auto output = an::SequenceOutput::begin(
            engine, execution, {.destinationPath = destination, .durability = an::OutputDurability::CloseOnly});
        ASSERT_TRUE(output.hasValue()) << output.status().message();
        {
            auto first = an::createSerSink(engine, execution, *output, serDescriptor(frame), {});
            ASSERT_TRUE(first.hasValue()) << first.status().message();
            ASSERT_TRUE(first->append(engine, execution, borrowed(frame, layout, samples, 0u)).hasValue());
            EXPECT_TRUE(first->close().ok());
        }
        auto retry = an::createSerSink(engine, execution, *output, serDescriptor(frame), {});
        ASSERT_TRUE(retry.hasValue()) << retry.status().message();
        ASSERT_TRUE(retry->append(engine, execution, borrowed(frame, layout, samples, 0u)).hasValue());
        auto finished = retry->finish(execution);
        ASSERT_TRUE(finished.hasValue()) << finished.status().message();
        ASSERT_TRUE(output->commit(execution, *finished).hasValue());
        EXPECT_EQ(readAll(destination).size(), an::kSerHeaderBytes + 40u);
        EXPECT_TRUE(std::filesystem::remove(native(destination)));
    }

    // Aborted while its sink writes: the sink goes on into a payload nothing will publish, and removes it
    // when it closes.
    {
        auto output = an::SequenceOutput::begin(engine, execution, {.destinationPath = pathOf("aborted.ser")});
        ASSERT_TRUE(output.hasValue()) << output.status().message();
        const std::string pending(output->pendingPath());
        auto sink = an::createSerSink(engine, execution, *output, serDescriptor(frame), {});
        ASSERT_TRUE(sink.hasValue()) << sink.status().message();
        ASSERT_TRUE(sink->append(engine, execution, borrowed(frame, layout, samples, 0u)).hasValue());
        output->abort();
        EXPECT_FALSE(output->valid());
        EXPECT_TRUE(std::filesystem::exists(native(pending)));
        ASSERT_TRUE(sink->append(engine, execution, borrowed(frame, layout, samples, 1u)).hasValue());
        EXPECT_TRUE(sink->close().ok());
        EXPECT_FALSE(std::filesystem::exists(native(pending)));
    }

    // Destroyed while its sink writes, and the sink then finishes: the container is complete and still
    // never published.
    {
        an::FrameSink sink;
        std::string pending;
        {
            auto output = an::SequenceOutput::begin(engine, execution, {.destinationPath = pathOf("destroyed.ser")});
            ASSERT_TRUE(output.hasValue()) << output.status().message();
            pending = output->pendingPath();
            auto created = an::createSerSink(engine, execution, *output, serDescriptor(frame), {});
            ASSERT_TRUE(created.hasValue()) << created.status().message();
            sink = std::move(*created);
            ASSERT_TRUE(sink.append(engine, execution, borrowed(frame, layout, samples, 0u)).hasValue());
        }
        EXPECT_TRUE(std::filesystem::exists(native(pending)));
        auto finished = sink.finish(execution);
        ASSERT_TRUE(finished.hasValue()) << finished.status().message();
        EXPECT_FALSE(std::filesystem::exists(native(pending)));
        EXPECT_TRUE(sink.close().ok());
    }

    // Destroyed while its sink writes, and the sink is then destroyed unfinished without a close.
    {
        an::FrameSink sink;
        std::string pending;
        {
            auto output = an::SequenceOutput::begin(engine, execution, {.destinationPath = pathOf("dropped.ser")});
            ASSERT_TRUE(output.hasValue()) << output.status().message();
            pending = output->pendingPath();
            auto created = an::createSerSink(engine, execution, *output, serDescriptor(frame), {});
            ASSERT_TRUE(created.hasValue()) << created.status().message();
            sink = std::move(*created);
            ASSERT_TRUE(sink.append(engine, execution, borrowed(frame, layout, samples, 0u)).hasValue());
        }
        EXPECT_TRUE(std::filesystem::exists(native(pending)));
        sink = an::FrameSink{};
        EXPECT_FALSE(std::filesystem::exists(native(pending)));
    }
    EXPECT_TRUE(std::filesystem::is_empty(directory));
}

TEST_F(SerSequenceTest, ASinkReopensOnlyTheRegularFileItsOutputReserved)
{
    const auto frame = describe(5u, 4u, 1u, 2u, an::ByteOrder::Little);
    const Bytes kept = pattern(64u, 14u);

    // A second name for the payload would let the sink discard a file it never reserved.
    {
        auto output = an::SequenceOutput::begin(engine, execution, {.destinationPath = pathOf("linked.ser")});
        ASSERT_TRUE(output.hasValue()) << output.status().message();
        const std::string pending(output->pendingPath());
        overwrite(pending, kept);
        const auto other = directory / "other-name.bin";
        std::error_code linked;
        std::filesystem::create_hard_link(native(pending), other, linked);
        ASSERT_FALSE(linked) << linked.message();
        auto refused = an::createSerSink(engine, execution, *output, serDescriptor(frame), {});
        ASSERT_FALSE(refused.hasValue());
        EXPECT_EQ(refused.status().code(), an::ErrorCode::ExternalFailure);
        EXPECT_EQ(an::sequenceFailureReason(refused.status()), an::SequenceFailure::IoFailure);
        EXPECT_EQ(readAll(utf8(other)), kept);
        ASSERT_TRUE(std::filesystem::remove(other));
        // With its second name gone it is the reserved file again, and the refusal gave the claim back.
        auto accepted = an::createSerSink(engine, execution, *output, serDescriptor(frame), {});
        ASSERT_TRUE(accepted.hasValue()) << accepted.status().message();
        EXPECT_EQ(readAll(pending).size(), an::kSerHeaderBytes);
        EXPECT_TRUE(accepted->close().ok());
    }

    // A missing payload is not created again, and a directory in its place is not written into.
    {
        auto output = an::SequenceOutput::begin(engine, execution, {.destinationPath = pathOf("replaced.ser")});
        ASSERT_TRUE(output.hasValue()) << output.status().message();
        const std::string pending(output->pendingPath());
        ASSERT_TRUE(std::filesystem::remove(native(pending)));
        auto missing = an::createSerSink(engine, execution, *output, serDescriptor(frame), {});
        EXPECT_EQ(missing.status().code(), an::ErrorCode::ExternalFailure);
        EXPECT_FALSE(std::filesystem::exists(native(pending)));
        ASSERT_TRUE(std::filesystem::create_directory(native(pending)));
        auto replaced = an::createSerSink(engine, execution, *output, serDescriptor(frame), {});
        EXPECT_EQ(replaced.status().code(), an::ErrorCode::ExternalFailure);
        EXPECT_TRUE(std::filesystem::is_directory(native(pending)));
        ASSERT_TRUE(std::filesystem::remove(native(pending)));
    }

    // A symbolic link is opened as itself and refused, where this host lets a test create one.
    {
        auto output = an::SequenceOutput::begin(engine, execution, {.destinationPath = pathOf("symlinked.ser")});
        ASSERT_TRUE(output.hasValue()) << output.status().message();
        const std::string pending(output->pendingPath());
        const auto target = directory / "target.bin";
        overwrite(utf8(target), kept);
        ASSERT_TRUE(std::filesystem::remove(native(pending)));
        std::error_code linked;
        std::filesystem::create_symlink(target, native(pending), linked);
        if (linked)
        {
            std::cout << "[          ] symbolic links are unavailable to this process: " << linked.message()
                      << std::endl;
        }
        else
        {
            auto refused = an::createSerSink(engine, execution, *output, serDescriptor(frame), {});
            EXPECT_EQ(refused.status().code(), an::ErrorCode::ExternalFailure);
            EXPECT_EQ(readAll(utf8(target)), kept);
            EXPECT_TRUE(std::filesystem::remove(native(pending)));
        }
        ASSERT_TRUE(std::filesystem::remove(target));
    }
    EXPECT_TRUE(std::filesystem::is_empty(directory));
}

TEST_F(SerSequenceTest, LargeFramesCrossEveryTransferBoundaryBitExactly)
{
    // Frames several transfers long in the three arrangements a sink stores: packed rows are written as
    // they stand, while planar colour and padded rows are packed through the scratch block, which fills
    // and flushes part way through every frame. Each container is read back through the source's own
    // transfers.
    const auto mono = describe(4096u, 2560u, 1u, 2u, an::ByteOrder::Little);
    const auto rgb = describe(2400u, 1400u, 3u, 2u, an::ByteOrder::Big, an::ChannelSchema::Rgb);
    struct Case
    {
        const char* name;
        an::RawFrameDescriptor frame;
        an::ImageLayout layout;
    };
    const std::array cases{
        Case{"packed.ser", mono, *an::ImageLayout::create({mono.extent(), 2u, 1u, 1u, 1u}, engine.limits())},
        Case{
            "planar.ser", rgb,
            *an::ImageLayout::createWithStrides({rgb.extent(), 2u, 1u, 3u, 1u}, 4806u, 4806u * 1400u, engine.limits())},
        Case{"padded.ser", mono,
             *an::ImageLayout::createWithStrides({mono.extent(), 2u, 1u, 1u, 1u}, 8196u, 8196u * 2560u,
                                                 engine.limits())}};
    for (const auto& entry : cases)
    {
        const auto& extent = entry.frame.extent();
        const std::size_t rowBytes = std::size_t{extent.width()} * extent.channelCount() * 2u;
        const std::size_t frameBytes = rowBytes * extent.height();
        const std::array<Bytes, 2u> inputs{pattern(entry.layout.totalBytes(), 41u),
                                           pattern(entry.layout.totalBytes(), 42u)};
        std::vector<Bytes> stored;
        std::vector<an::SequenceFrame> frames;
        for (std::size_t index = 0u; index < inputs.size(); ++index)
        {
            const Bytes& input = inputs[index];
            Bytes packed(frameBytes);
            for (std::size_t y = 0u; y < extent.height(); ++y)
            {
                std::byte* const target = packed.data() + y * rowBytes;
                if (entry.layout.planeCount() == 1u)
                {
                    std::memcpy(target, input.data() + y * entry.layout.rowStrideBytes(), rowBytes);
                    continue;
                }
                for (std::size_t x = 0u; x < extent.width(); ++x)
                    for (std::size_t channel = 0u; channel < 3u; ++channel)
                        std::memcpy(target + (x * 3u + channel) * 2u,
                                    input.data() + channel * entry.layout.planeStrideBytes() +
                                        y * entry.layout.rowStrideBytes() + x * 2u,
                                    2u);
            }
            stored.push_back(std::move(packed));
            frames.push_back(borrowed(entry.frame, entry.layout, input, index));
        }
        const auto bytes = writeContainer(entry.name, serDescriptor(entry.frame), {}, frames);
        ASSERT_EQ(bytes.size(), an::kSerHeaderBytes + 2u * frameBytes) << entry.name;
        auto source = an::createSerSource(engine, execution, pathOf(entry.name), {});
        ASSERT_TRUE(source.hasValue()) << source.status().message();
        EXPECT_EQ(*source->descriptor()->fixedFrame, entry.frame) << entry.name;
        for (std::size_t index = 0u; index < stored.size(); ++index)
        {
            EXPECT_EQ(
                std::memcmp(stored[index].data(), bytes.data() + an::kSerHeaderBytes + index * frameBytes, frameBytes),
                0)
                << entry.name << " frame " << index;
            auto read = source->read(engine, execution);
            ASSERT_TRUE(read.hasValue()) << read.status().message();
            EXPECT_EQ(std::memcmp(stored[index].data(), read->frame->frame.bytes().data(), frameBytes), 0)
                << entry.name << " frame " << index;
        }
        EXPECT_TRUE(source->close().ok());
    }
}

TEST_F(SerSequenceTest, AReadStopsBetweenTransfersAndBeforeTheColourExchange)
{
    // A frame of two transfers, stopped as the first arrives: the read's own checkpoint refuses the
    // second, so neither the rest of the frame nor its completion is reported.
    SerBytes large;
    large.width = 4096;
    large.height = 2560;
    large.frameCount = 1u;
    large.payload = pattern(large.frameBytes(), 51u);
    // A colour frame of one transfer, stopped as it arrives: only the exchange's own checkpoint stands
    // between that stop and a completed frame.
    SerBytes bgr;
    bgr.colorId = 101;
    bgr.depth = 8;
    bgr.width = 64;
    bgr.height = 16;
    bgr.frameCount = 1u;
    bgr.payload = pattern(bgr.frameBytes(), 52u);
    const auto stopsPartWay = [&](const char* name, const SerBytes& file) {
        const auto path = write(name, file.file());
        std::stop_source stop;
        StopOnTrace trace(stop, "transfer-bytes");
        auto stopping = an::ExecutionContext::create(engine, {.stopToken = stop.get_token(), .traceSink = &trace});
        ASSERT_TRUE(stopping.hasValue());
        auto source = an::createSerSource(engine, *stopping, path, {});
        ASSERT_TRUE(source.hasValue()) << source.status().message();
        const auto live = engine.memoryResource()->counters().liveBytes;
        auto read = source->read(engine, *stopping);
        EXPECT_EQ(read.status().code(), an::ErrorCode::Cancelled) << name;
        EXPECT_EQ(trace.count("transfer-bytes"), 1u) << name;
        EXPECT_EQ(trace.count("frame-bytes-read"), 0u) << name;
        EXPECT_EQ(source->state(), an::SequenceState::Failed) << name;
        EXPECT_EQ(engine.memoryResource()->counters().liveBytes, live) << name;
    };
    stopsPartWay("large.ser", large);
    stopsPartWay("bgr.ser", bgr);

    // Unstopped, the large frame arrives whole in two transfers.
    std::stop_source idle;
    StopOnTrace counted(idle, nullptr);
    auto counting = an::ExecutionContext::create(engine, {.traceSink = &counted});
    ASSERT_TRUE(counting.hasValue());
    auto source = an::createSerSource(engine, *counting, pathOf("large.ser"), {});
    ASSERT_TRUE(source.hasValue()) << source.status().message();
    auto read = source->read(engine, *counting);
    ASSERT_TRUE(read.hasValue()) << read.status().message();
    EXPECT_EQ(counted.count("transfer-bytes"), 2u);
    EXPECT_EQ(counted.count("frame-bytes-read"), 1u);
    EXPECT_EQ(std::memcmp(read->frame->frame.bytes().data(), large.payload.data(), large.payload.size()), 0);
}

TEST_F(SerSequenceTest, ATrailerReadStopsBetweenTransfersAndReleasesItsBlock)
{
    // One-byte frames under a trailer longer than one transfer holds.
    constexpr std::uint32_t kFrames = 16u * 1024u * 1024u / 8u + 3u;
    SerBytes file;
    file.width = 1;
    file.height = 1;
    file.depth = 8;
    file.frameCount = kFrames;
    file.payload = pattern(kFrames, 61u);
    const std::uint64_t start = an::kSerUnixEpochTicks + 15500000000000000u;
    file.trailer.resize(kFrames);
    for (std::uint32_t index = 0u; index < kFrames; ++index)
        file.trailer[index] = start + index;
    const auto path = write("long-trailer.ser", file.file());
    {
        std::stop_source stop;
        StopOnTrace trace(stop, "trailer-bytes");
        auto stopping = an::ExecutionContext::create(engine, {.stopToken = stop.get_token(), .traceSink = &trace});
        ASSERT_TRUE(stopping.hasValue());
        const auto live = engine.memoryResource()->counters().liveBytes;
        EXPECT_EQ(an::createSerSource(engine, *stopping, path, {}).status().code(), an::ErrorCode::Cancelled);
        EXPECT_EQ(trace.count("trailer-bytes"), 1u);
        EXPECT_EQ(engine.memoryResource()->counters().liveBytes, live);
    }
    std::stop_source idle;
    StopOnTrace counted(idle, nullptr);
    auto counting = an::ExecutionContext::create(engine, {.traceSink = &counted});
    ASSERT_TRUE(counting.hasValue());
    auto info = an::inspectSerFile(engine, *counting, path, {});
    ASSERT_TRUE(info.hasValue()) << info.status().message();
    EXPECT_EQ(counted.count("trailer-bytes"), 2u);
    EXPECT_TRUE(info->timeline);
    EXPECT_EQ(info->timestampCount, kFrames);
    EXPECT_EQ(info->lastTimestamp, an::utcInstantFromSerTicks(start + kFrames - 1u));
}

TEST_F(SerSequenceTest, AnAppendStopsBetweenTransfersAndItsSinkDiscardsThePartialFrame)
{
    // A frame of two transfers, packed and with padded rows, stopped as its first transfer is written:
    // the sink's own checkpoint refuses the second, so the payload holds the header and one transfer
    // until the failed sink closes.
    const auto frame = describe(4096u, 2560u, 1u, 2u, an::ByteOrder::Little);
    const std::array layouts{
        *an::ImageLayout::create({frame.extent(), 2u, 1u, 1u, 1u}, engine.limits()),
        *an::ImageLayout::createWithStrides({frame.extent(), 2u, 1u, 1u, 1u}, 8196u, 8196u * 2560u, engine.limits())};
    for (const auto& layout : layouts)
    {
        const Bytes samples = pattern(layout.totalBytes(), 71u);
        auto output = an::SequenceOutput::begin(engine, execution, {.destinationPath = pathOf("stopped.ser")});
        ASSERT_TRUE(output.hasValue()) << output.status().message();
        const std::string pending(output->pendingPath());
        std::stop_source stop;
        StopOnTrace trace(stop, "transfer-bytes");
        auto stopping = an::ExecutionContext::create(engine, {.stopToken = stop.get_token(), .traceSink = &trace});
        ASSERT_TRUE(stopping.hasValue());
        auto sink = an::createSerSink(engine, *stopping, *output, serDescriptor(frame), {});
        ASSERT_TRUE(sink.hasValue()) << sink.status().message();
        auto appended = sink->append(engine, *stopping, borrowed(frame, layout, samples, 0u));
        EXPECT_EQ(appended.status().code(), an::ErrorCode::Cancelled);
        EXPECT_EQ(trace.count("transfer-bytes"), 1u);
        EXPECT_EQ(trace.count("frame-bytes-written"), 0u);
        EXPECT_EQ(readAll(pending).size(), an::kSerHeaderBytes + 16u * 1024u * 1024u);
        EXPECT_EQ(sink->state(), an::SequenceState::Failed);
        EXPECT_TRUE(sink->close().ok());
        EXPECT_EQ(std::filesystem::file_size(native(pending)), 0u);
    }
}

#ifndef _WIN32
TEST_F(SerSequenceTest, ANamedPipeIsRefusedWithoutWaitingForTheOtherEnd)
{
    // Opening a pipe with nothing at its other end would wait where no cancellation reaches, so a source
    // and a sink's reopen both refuse one at once.
    const auto path = pathOf("pipe.ser");
    ASSERT_EQ(::mkfifo(native(path).c_str(), 0600), 0);
    auto source = an::createSerSource(engine, execution, path, {});
    ASSERT_FALSE(source.hasValue());
    EXPECT_EQ(source.status().code(), an::ErrorCode::ExternalFailure);
    EXPECT_EQ(an::sequenceFailureReason(source.status()), an::SequenceFailure::IoFailure);
    EXPECT_EQ(an::inspectSerFile(engine, execution, path, {}).status().code(), an::ErrorCode::ExternalFailure);
    EXPECT_TRUE(std::filesystem::remove(native(path)));

    auto output = an::SequenceOutput::begin(engine, execution, {.destinationPath = pathOf("piped.ser")});
    ASSERT_TRUE(output.hasValue()) << output.status().message();
    const std::string pending(output->pendingPath());
    ASSERT_TRUE(std::filesystem::remove(native(pending)));
    ASSERT_EQ(::mkfifo(native(pending).c_str(), 0600), 0);
    const auto frame = describe(5u, 4u, 1u, 2u, an::ByteOrder::Little);
    auto sink = an::createSerSink(engine, execution, *output, serDescriptor(frame), {});
    ASSERT_FALSE(sink.hasValue());
    EXPECT_EQ(sink.status().code(), an::ErrorCode::ExternalFailure);
    EXPECT_TRUE(std::filesystem::remove(native(pending)));
}
#endif
} // namespace

TEST_F(SerSequenceTest, TrailerFinalizationStopsBeforeTheRemainingTimestampsAndHeader)
{
    for (const std::uint64_t frameCount : {1u, 8193u})
    {
        const auto destination = pathOf("trailer-finish-" + std::to_string(frameCount) + ".ser");
        const auto descriptor = describe(1u, 1u, 1u, 1u, an::ByteOrder::Native);
        const auto layout = *an::ImageLayout::create({descriptor.extent(), 1u, 1u, 1u, 1u}, engine.limits());
        constexpr std::uint64_t kOrigin = an::kSerUnixEpochTicks + 123456789u;
        auto sequence = serDescriptor(descriptor);
        sequence.timeBase = an::SequenceRate{1u, 10000000u};
        sequence.timestampsRequired = true;
        const Bytes samples{std::byte{37}};
        auto frame = borrowed(descriptor, layout, samples, 0u);
        // Test both a stop during the trailer and the complete multi-chunk trailer.
        for (const bool cancel : {true, false})
        {
            auto output = an::SequenceOutput::begin(engine, execution, {.destinationPath = destination});
            ASSERT_TRUE(output.hasValue());
            const std::string pending(output->pendingPath());
            auto sink = an::createSerSink(
                engine, execution, *output, sequence,
                {.timestamps = an::SerTimestampSource::PresentationTicks, .timelineOriginTicks = kOrigin});
            ASSERT_TRUE(sink.hasValue()) << sink.status().message();
            for (std::uint64_t index = 0u; index < frameCount; ++index)
            {
                frame.position.index = index;
                frame.position.presentationTicks = static_cast<std::int64_t>(index);
                ASSERT_TRUE(sink->append(engine, execution, frame).hasValue());
            }
            std::stop_source stop;
            StopOnTrace trace(stop, cancel ? "trailer-bytes-written" : nullptr);
            auto finishing = an::ExecutionContext::create(engine, {.stopToken = stop.get_token(), .traceSink = &trace});
            ASSERT_TRUE(finishing.hasValue());
            const auto finished = sink->finish(*finishing);
            if (cancel)
            {
                ASSERT_FALSE(finished.hasValue());
                EXPECT_EQ(finished.status().code(), an::ErrorCode::Cancelled);
                EXPECT_EQ(trace.count("trailer-bytes-written"), 1u);
                const auto bytes = readAll(pending);
                EXPECT_EQ(bytes.size(),
                          an::kSerHeaderBytes + frameCount + (std::min)(frameCount, std::uint64_t{8192}) * 8u);
                EXPECT_EQ(getLe(bytes, 38u, 4u), 0u);
                EXPECT_EQ(sink->state(), an::SequenceState::Failed);
                EXPECT_FALSE(std::filesystem::exists(native(output->destinationPath())));
            }
            else
            {
                ASSERT_TRUE(finished.hasValue()) << finished.status().message();
                EXPECT_GE(trace.count("trailer-bytes-written"), frameCount > 8192u ? 2u : 1u);
                ASSERT_TRUE(output->commit(execution, *finished).hasValue());
                auto info = an::inspectSerFile(engine, execution, destination, {});
                ASSERT_TRUE(info.hasValue()) << info.status().message();
                EXPECT_EQ(info->timestampCount, frameCount);
                const auto bytes = readAll(output->destinationPath());
                EXPECT_EQ(bytes.size(), an::kSerHeaderBytes + frameCount * 9u);
                EXPECT_EQ(getLe(bytes, 38u, 4u), frameCount);
                for (const auto index : {0u, 8191u, 8192u})
                    if (index < frameCount)
                        EXPECT_EQ(getLe(bytes, an::kSerHeaderBytes + frameCount + 8u * index, 8u), kOrigin + index);
            }
            EXPECT_TRUE(sink->close().ok());
            output->abort();
            EXPECT_FALSE(std::filesystem::exists(native(pending)));
            EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
        }
    }
}
