// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The raw-dump exporter of the runner linked to this engine.
//
// What the exporter promises is held in three ways. Its cases run over this engine's frames: a dump survives the
// trip with its bytes, descriptor, cards and provenance, a replaced or missing payload or a malformed
// descriptor is reported rather than thrown, and a file nothing decodes leaves nothing behind. The frames a
// decoder hands back are written in the format's one arrangement whatever they arrived in - interleaved,
// bottom row first, padded, big-endian, a mosaic seen from another phase. And the checked-in inputs, one in
// every container and representation the engine reads, export to their recorded descriptor and payload digest,
// refuse where the record refuses, and differ in their cards only where the register says why; the runner's
// own command line answers with its documented exit codes.

#include "Digest.h"
#include "RawDump.h"
#include "RawDumpExport.h"
#include "RawDumpExportRecord.h"

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/IO/CodecRequests.h>
#include <AfterNightEngine/Image/CfaPattern.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/RawFrame.h>
#include <AfterNightEngine/Image/RawSampleFormat.h>
#include <AfterNightEngine/Image/SampleBuffer.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace
{

/// The bench layer's engine side, where the raw-dump exporter lives.
namespace bridge = an::bench::engine;

using an::bench::MetadataCard;
using an::bench::RawDump;
using an::bench::RawDumpCardDifference;
using an::bench::RawDumpExportCase;
using an::bench::RawDumpExportOutcome;
using an::bench::RawDumpExportRecord;
using an::bench::SampleType;
using an::bench::Sha256;

/// A scratch directory that removes itself, so a failing assertion cannot leave one behind.
class Scratch
{
public:
    explicit Scratch(const char* name)
    {
        static const std::string unique = std::to_string(
            static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
        m_path = std::filesystem::temp_directory_path() / (std::string("an-raw-dump-next-") + name + "-" + unique);
        std::filesystem::remove_all(m_path);
        std::filesystem::create_directories(m_path);
    }
    ~Scratch()
    {
        std::error_code ignored;
        std::filesystem::remove_all(m_path, ignored);
    }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const
    {
        return m_path;
    }

private:
    std::filesystem::path m_path;
};

/// How a hand-made frame arranges its samples.
struct FrameShape
{
    std::uint32_t width = 5u;
    std::uint32_t height = 3u;
    std::uint32_t channels = 1u;
    an::RawSampleFormat format{2u, an::RawSampleClass::UnsignedInteger};
    bool interleaved = false;
    an::CoordinateOrigin origin = an::CoordinateOrigin::TopLeft;
    an::ByteOrder byteOrder = an::ByteOrder::Native;
    an::CfaDescriptor cfa{};
    std::uint64_t rowAlignment = 1u;
};

/// A decoded frame over bytes the test owns, the way a decoder hands one back.
struct HandMadeFrame
{
    std::vector<std::byte> bytes;
    an::CodecDecodeResult decoded;
};

/// Fill a frame whose sample at (plane, x, y), in the frame's own row order and byte order, is `value(...)`'s
/// bytes. The stored row of image row y is y for a top-left origin and height - 1 - y for a bottom-left one.
template <typename SampleT, typename ValueFn>
HandMadeFrame makeFrame(const FrameShape& shape, ValueFn value, std::vector<an::CodecCard> cards = {})
{
    HandMadeFrame made;
    const an::EngineLimits limits = an::defaultEngineLimits();
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(shape.width, shape.height, shape.channels, limits);
    EXPECT_TRUE(extent.has_value());
    const an::ChannelSchema schema = shape.channels == 3u ? an::ChannelSchema::Rgb : an::ChannelSchema::Mono;
    const std::optional<an::RawFrameDescriptor> descriptor = an::RawFrameDescriptor::create(
        an::RawFrameDescriptorRequest{.extent = *extent,
                                      .sampleFormat = shape.format,
                                      .byteOrder = shape.byteOrder,
                                      .channelSchema = schema,
                                      .colorSpace = an::ColorSpace::Unspecified,
                                      .numericDomain = an::NumericDomain::NormalizedLinear,
                                      .cfa = shape.cfa,
                                      .coordinateOrigin = shape.origin,
                                      .pixelAspectRatio = 1.0});
    EXPECT_TRUE(descriptor.has_value());
    const std::optional<an::ImageLayout> layout =
        an::ImageLayout::create(an::ImageLayoutRequest{.extent = *extent,
                                                       .elementSizeBytes = shape.format.sizeBytes,
                                                       .samplesPerPixel = shape.interleaved ? shape.channels : 1u,
                                                       .planeCount = shape.interleaved ? 1u : shape.channels,
                                                       .rowAlignmentBytes = shape.rowAlignment},
                                limits);
    EXPECT_TRUE(layout.has_value());
    made.bytes.assign(static_cast<std::size_t>(layout->totalBytes()), std::byte{0xEE});
    const bool swap = shape.byteOrder != an::ByteOrder::Native && shape.byteOrder != an::nativeByteOrder();
    for (std::uint32_t channel = 0u; channel < shape.channels; ++channel)
    {
        for (std::uint32_t y = 0u; y < shape.height; ++y)
        {
            const std::uint32_t stored = shape.origin == an::CoordinateOrigin::BottomLeft ? shape.height - 1u - y : y;
            for (std::uint32_t x = 0u; x < shape.width; ++x)
            {
                const auto sample = static_cast<SampleT>(value(channel, x, y));
                std::array<std::byte, sizeof(SampleT)> raw{};
                std::memcpy(raw.data(), &sample, sizeof(SampleT));
                if (swap)
                {
                    std::reverse(raw.begin(), raw.end());
                }
                const std::uint32_t plane = shape.interleaved ? 0u : channel;
                const std::uint64_t column = shape.interleaved ? std::uint64_t{x} * shape.channels + channel : x;
                const std::uint64_t offset =
                    plane * layout->planeStrideBytes() + stored * layout->rowStrideBytes() + column * sizeof(SampleT);
                std::memcpy(made.bytes.data() + offset, raw.data(), sizeof(SampleT));
            }
        }
    }
    an::Result<an::SampleBuffer> buffer = an::SampleBuffer::borrowConst(made.bytes);
    EXPECT_TRUE(buffer.hasValue());
    an::Result<an::RawFrame> frame = an::RawFrame::create(*descriptor, *layout, std::move(*buffer));
    EXPECT_TRUE(frame.hasValue());
    made.decoded.frame = std::move(*frame);
    an::Result<an::CodecMetadata> codec = an::CodecMetadata::create(an::SourceEncoding{}, std::move(cards));
    EXPECT_TRUE(codec.hasValue());
    made.decoded.metadata = an::ImageMetadata::create({}, {}, {}, {}, std::move(*codec), std::nullopt);
    return made;
}

/// The payload a frame must export to: planes back to back, top row first, little-endian.
template <typename SampleT, typename ValueFn>
std::vector<std::uint8_t> expectedPayload(const FrameShape& shape, ValueFn value)
{
    std::vector<std::uint8_t> payload;
    for (std::uint32_t channel = 0u; channel < shape.channels; ++channel)
    {
        for (std::uint32_t y = 0u; y < shape.height; ++y)
        {
            for (std::uint32_t x = 0u; x < shape.width; ++x)
            {
                const auto sample = static_cast<SampleT>(value(channel, x, y));
                std::array<std::uint8_t, sizeof(SampleT)> raw{};
                std::memcpy(raw.data(), &sample, sizeof(SampleT));
                if (an::nativeByteOrder() == an::ByteOrder::Big)
                {
                    std::reverse(raw.begin(), raw.end());
                }
                payload.insert(payload.end(), raw.begin(), raw.end());
            }
        }
    }
    return payload;
}

std::uint32_t ramp(std::uint32_t channel, std::uint32_t x, std::uint32_t y)
{
    return 1000u * channel + 37u * y + x + 1u;
}

an::CodecCard card(const char* key, const char* value, const char* comment)
{
    an::Result<an::CodecCard> made = an::CodecCard::create(an::CodecNamespace::Fits, key, value, comment);
    EXPECT_TRUE(made.hasValue());
    return *made;
}

bool build(const HandMadeFrame& frame, RawDump& dump, std::vector<std::uint8_t>& payload, std::string& error)
{
    return bridge::buildRawDump(frame.decoded, "memory://synthetic", "SYNTHETIC", dump, payload, error);
}

std::filesystem::path expectationDirectory()
{
    std::string error;
    const std::optional<std::filesystem::path> found = an::bench::locateExpectationDirectory(error);
    EXPECT_TRUE(found.has_value()) << error;
    return found.value_or(std::filesystem::path());
}

/// The runner beside this suite, in this suite's configuration.
std::filesystem::path runnerBesideThisSuite()
{
#if defined(_WIN32)
    std::wstring buffer(32768u, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    buffer.resize(length);
    const std::filesystem::path self(buffer);
    const char* const extension = ".exe";
#else
    std::error_code ignored;
    const std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe", ignored);
    const char* const extension = "";
#endif
    const std::string stem = self.stem().string();
    const bool debug = stem.size() > 2u && stem.compare(stem.size() - 2u, 2u, "_d") == 0;
    return self.parent_path() / (std::string("AfterNightEngineBench") + (debug ? "_d" : "") + extension);
}

/// Run the runner with `arguments`, its output into `log`, and answer its exit code.
int runRunner(const std::vector<std::string>& arguments, const std::filesystem::path& log)
{
    std::string command = "\"" + runnerBesideThisSuite().string() + "\"";
    for (const std::string& argument : arguments)
    {
        command += " \"" + argument + "\"";
    }
    command += " > \"" + log.string() + "\" 2>&1";
#if defined(_WIN32)
    // The command processor strips the outer pair of quotes from a line that starts with one.
    return std::system(("\"" + command + "\"").c_str());
#else
    const int status = std::system(command.c_str());
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

} // namespace

// --- the exporter's cases, over this engine's frames ---------------------------------------------------------

TEST(RawDumpExportTest, ASyntheticFrameRoundTripsThroughADump)
{
    Scratch scratch("roundtrip");
    FrameShape shape;
    shape.format = an::RawSampleFormat{4u, an::RawSampleClass::FloatingPoint};
    shape.width = 64u;
    shape.height = 32u;
    const auto value = [](std::uint32_t, std::uint32_t x, std::uint32_t y) { return static_cast<float>(x * 0.25 + y); };
    const HandMadeFrame frame = makeFrame<float>(shape, value);

    RawDump dump;
    std::vector<std::uint8_t> payload;
    std::string error;
    ASSERT_TRUE(build(frame, dump, payload, error)) << error;

    const std::string descriptorPath = (scratch.path() / "frame.andesc.json").string();
    ASSERT_TRUE(an::bench::writeRawDump(descriptorPath, dump, payload, error)) << error;
    RawDump reread;
    std::vector<std::uint8_t> rereadPayload;
    ASSERT_TRUE(an::bench::readRawDump(descriptorPath, reread, rereadPayload, error)) << error;

    EXPECT_EQ(reread.descriptor, dump.descriptor);
    EXPECT_EQ(rereadPayload, payload);
    EXPECT_EQ(reread.payloadDigest, dump.payloadDigest);
    EXPECT_EQ(reread.payloadBytes, payload.size());
    EXPECT_EQ(reread.exporterVersion, bridge::kRawDumpExporterVersion);
    EXPECT_EQ(reread.sourcePath, "memory://synthetic");
    EXPECT_EQ(reread.descriptor.sampleType, SampleType::Float32);
    EXPECT_EQ(reread.descriptor.byteOrder, an::bench::ByteOrder::Little);
    // The bytes that came back are the frame's own samples, not merely bytes of the right length.
    EXPECT_EQ(rereadPayload, (expectedPayload<float>(shape, value)));
}

TEST(RawDumpExportTest, MetadataCardsSurviveTheTripInTheirOrder)
{
    Scratch scratch("cards");
    const HandMadeFrame frame = makeFrame<std::uint16_t>(FrameShape{}, ramp,
                                                         {card("OBJECT", "'M42'", "target"),
                                                          card("EXPTIME", "300.0", ""), card("FILTER", "'Ha'", ""),
                                                          card("HISTORY", "", "first"), card("HISTORY", "", "second")});

    RawDump dump;
    std::vector<std::uint8_t> payload;
    std::string error;
    ASSERT_TRUE(build(frame, dump, payload, error)) << error;
    const std::string descriptorPath = (scratch.path() / "cards.andesc.json").string();
    ASSERT_TRUE(an::bench::writeRawDump(descriptorPath, dump, payload, error)) << error;
    RawDump reread;
    std::vector<std::uint8_t> rereadPayload;
    ASSERT_TRUE(an::bench::readRawDump(descriptorPath, reread, rereadPayload, error)) << error;

    ASSERT_NE(reread.findCard("OBJECT"), nullptr);
    EXPECT_EQ(reread.findCard("OBJECT")->value, "'M42'");
    EXPECT_EQ(reread.findCard("OBJECT")->comment, "target");
    ASSERT_NE(reread.findCard("EXPTIME"), nullptr);
    EXPECT_EQ(reread.findCard("EXPTIME")->value, "300.0");
    ASSERT_EQ(reread.cards.size(), 5u);
    EXPECT_EQ(reread.cards[3].comment, "first");
    EXPECT_EQ(reread.cards[4].comment, "second");
    EXPECT_EQ(reread.cards, dump.cards) << "cards must keep their order as well as their values";
}

TEST(RawDumpExportTest, AReplacedPayloadIsRejected)
{
    Scratch scratch("tampered");
    RawDump dump;
    std::vector<std::uint8_t> payload;
    std::string error;
    ASSERT_TRUE(build(makeFrame<std::uint16_t>(FrameShape{}, ramp), dump, payload, error)) << error;
    const std::string descriptorPath = (scratch.path() / "frame.andesc.json").string();
    ASSERT_TRUE(an::bench::writeRawDump(descriptorPath, dump, payload, error)) << error;
    {
        std::fstream file(scratch.path() / dump.payloadFile, std::ios::binary | std::ios::in | std::ios::out);
        ASSERT_TRUE(file);
        const char replacement[4] = {'\x7f', '\x7f', '\x7f', '\x7f'};
        file.write(replacement, sizeof replacement);
    }
    RawDump reread;
    std::vector<std::uint8_t> rereadPayload;
    EXPECT_FALSE(an::bench::readRawDump(descriptorPath, reread, rereadPayload, error));
    EXPECT_NE(error.find("hashes to"), std::string::npos);
}

TEST(RawDumpExportTest, AMissingPayloadIsReported)
{
    Scratch scratch("orphan");
    RawDump dump;
    std::vector<std::uint8_t> payload;
    std::string error;
    ASSERT_TRUE(build(makeFrame<std::uint16_t>(FrameShape{}, ramp), dump, payload, error)) << error;
    const std::string descriptorPath = (scratch.path() / "frame.andesc.json").string();
    ASSERT_TRUE(an::bench::writeRawDump(descriptorPath, dump, payload, error)) << error;
    std::filesystem::remove(scratch.path() / dump.payloadFile);
    RawDump reread;
    std::vector<std::uint8_t> rereadPayload;
    EXPECT_FALSE(an::bench::readRawDump(descriptorPath, reread, rereadPayload, error));
    EXPECT_FALSE(error.empty());
}

TEST(RawDumpExportTest, WrongJsonFieldTypesAreReportedNotThrown)
{
    Scratch scratch("wrong-json-type");
    RawDump dump;
    std::vector<std::uint8_t> payload;
    std::string error;
    ASSERT_TRUE(build(makeFrame<std::uint16_t>(FrameShape{}, ramp), dump, payload, error)) << error;
    const std::filesystem::path descriptorPath = scratch.path() / "frame.andesc.json";
    ASSERT_TRUE(an::bench::writeRawDump(descriptorPath.string(), dump, payload, error)) << error;

    std::ifstream input(descriptorPath, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    input.close();
    const std::string original = "\"width\": 5";
    const auto at = text.find(original);
    ASSERT_NE(at, std::string::npos);
    text.replace(at, original.size(), "\"width\": \"five\"");
    std::ofstream(descriptorPath, std::ios::binary | std::ios::trunc) << text;

    RawDump reread;
    std::vector<std::uint8_t> rereadPayload;
    EXPECT_FALSE(an::bench::readRawDump(descriptorPath.string(), reread, rereadPayload, error));
    EXPECT_FALSE(error.empty());
}

TEST(RawDumpExportTest, AnUndecodableFileIsReportedNotThrownAndLeavesNothing)
{
    Scratch scratch("undecodable");
    const std::filesystem::path source = scratch.path() / "not-an-image.fit";
    std::ofstream(source, std::ios::binary) << "this is not a FITS file";

    std::string error;
    const std::filesystem::path out = scratch.path() / "out.json";
    EXPECT_FALSE(bridge::exportRawDump(source.string(), out.string(), error));
    EXPECT_NE(error.find("cannot decode"), std::string::npos) << error;
    EXPECT_FALSE(std::filesystem::exists(out));
    EXPECT_FALSE(std::filesystem::exists(scratch.path() / "out.anraw"));
}

TEST(RawDumpExportTest, AnAbsentSourceIsReported)
{
    Scratch scratch("absent");
    std::string error;
    EXPECT_FALSE(bridge::exportRawDump((scratch.path() / "missing.fit").string(),
                                       (scratch.path() / "out.json").string(), error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(std::filesystem::exists(scratch.path() / "out.json"));
}

// --- what a decoder hands back, written in the format's one arrangement -------------------------------------

TEST(RawDumpExportTest, AnInterleavedColourFrameIsWrittenPlanar)
{
    FrameShape shape;
    shape.channels = 3u;
    shape.interleaved = true;
    RawDump dump;
    std::vector<std::uint8_t> payload;
    std::string error;
    ASSERT_TRUE(build(makeFrame<std::uint16_t>(shape, ramp), dump, payload, error)) << error;
    EXPECT_EQ(dump.descriptor.channels, 3u);
    EXPECT_EQ(dump.descriptor.layout, an::bench::Layout::Planar);
    EXPECT_EQ(payload, (expectedPayload<std::uint16_t>(shape, ramp)));
}

TEST(RawDumpExportTest, APlanarColourFrameKeepsItsPlanesInOrder)
{
    FrameShape shape;
    shape.channels = 3u;
    RawDump dump;
    std::vector<std::uint8_t> payload;
    std::string error;
    ASSERT_TRUE(build(makeFrame<std::uint16_t>(shape, ramp), dump, payload, error)) << error;
    EXPECT_EQ(payload, (expectedPayload<std::uint16_t>(shape, ramp)));
}

TEST(RawDumpExportTest, ABottomUpFrameIsWrittenTopRowFirst)
{
    FrameShape shape;
    shape.origin = an::CoordinateOrigin::BottomLeft;
    shape.channels = 3u;
    shape.interleaved = true;
    RawDump dump;
    std::vector<std::uint8_t> payload;
    std::string error;
    ASSERT_TRUE(build(makeFrame<std::uint16_t>(shape, ramp), dump, payload, error)) << error;
    EXPECT_EQ(payload, (expectedPayload<std::uint16_t>(shape, ramp)));
}

TEST(RawDumpExportTest, APaddedFrameIsWrittenTight)
{
    FrameShape shape;
    shape.rowAlignment = 64u;
    shape.channels = 3u;
    RawDump dump;
    std::vector<std::uint8_t> payload;
    std::string error;
    ASSERT_TRUE(build(makeFrame<std::uint16_t>(shape, ramp), dump, payload, error)) << error;
    EXPECT_EQ(dump.descriptor.rowStrideBytes, 0u);
    EXPECT_EQ(dump.descriptor.planeStrideBytes, 0u);
    EXPECT_EQ(payload, (expectedPayload<std::uint16_t>(shape, ramp)));
}

TEST(RawDumpExportTest, AFrameInTheOtherByteOrderIsWrittenLittleEndian)
{
    FrameShape shape;
    shape.byteOrder = an::nativeByteOrder() == an::ByteOrder::Little ? an::ByteOrder::Big : an::ByteOrder::Little;
    shape.format = an::RawSampleFormat{4u, an::RawSampleClass::SignedInteger};
    const auto value = [](std::uint32_t channel, std::uint32_t x, std::uint32_t y) {
        return static_cast<std::int32_t>(ramp(channel, x, y)) * -70001;
    };
    RawDump dump;
    std::vector<std::uint8_t> payload;
    std::string error;
    ASSERT_TRUE(build(makeFrame<std::int32_t>(shape, value), dump, payload, error)) << error;
    EXPECT_EQ(dump.descriptor.sampleType, SampleType::Int32);
    EXPECT_EQ(dump.descriptor.byteOrder, an::bench::ByteOrder::Little);
    EXPECT_EQ(payload, (expectedPayload<std::int32_t>(shape, value)));
}

TEST(RawDumpExportTest, EveryRepresentationIsNamedAsTheSchemaNamesIt)
{
    const struct
    {
        an::RawSampleFormat format;
        SampleType expected;
    } formats[] = {
        {{1u, an::RawSampleClass::UnsignedInteger}, SampleType::UInt8},
        {{1u, an::RawSampleClass::SignedInteger}, SampleType::Int8},
        {{2u, an::RawSampleClass::UnsignedInteger}, SampleType::UInt16},
        {{2u, an::RawSampleClass::SignedInteger}, SampleType::Int16},
        {{4u, an::RawSampleClass::UnsignedInteger}, SampleType::UInt32},
        {{4u, an::RawSampleClass::SignedInteger}, SampleType::Int32},
        {{8u, an::RawSampleClass::UnsignedInteger}, SampleType::UInt64},
        {{8u, an::RawSampleClass::SignedInteger}, SampleType::Int64},
        {{4u, an::RawSampleClass::FloatingPoint}, SampleType::Float32},
        {{8u, an::RawSampleClass::FloatingPoint}, SampleType::Float64},
    };
    for (const auto& entry : formats)
    {
        FrameShape shape;
        shape.format = entry.format;
        RawDump dump;
        std::vector<std::uint8_t> payload;
        std::string error;
        HandMadeFrame frame;
        switch (entry.format.sizeBytes)
        {
        case 1u:
            frame = makeFrame<std::uint8_t>(shape, ramp);
            break;
        case 2u:
            frame = makeFrame<std::uint16_t>(shape, ramp);
            break;
        case 4u:
            frame = makeFrame<std::uint32_t>(shape, ramp);
            break;
        default:
            frame = makeFrame<std::uint64_t>(shape, ramp);
            break;
        }
        ASSERT_TRUE(build(frame, dump, payload, error)) << error;
        EXPECT_EQ(dump.descriptor.sampleType, entry.expected);
        EXPECT_EQ(payload.size(), std::size_t{5u * 3u} * entry.format.sizeBytes);
    }
}

TEST(RawDumpExportTest, ABayerMosaicIsNamedFromItsFirstPixel)
{
    const struct
    {
        an::CfaPattern pattern;
        std::uint32_t originX;
        std::uint32_t originY;
        const char* expected;
    } mosaics[] = {
        {an::CfaPattern::Rggb, 0u, 0u, "RGGB"}, {an::CfaPattern::Bggr, 0u, 0u, "BGGR"},
        {an::CfaPattern::Grbg, 0u, 0u, "GRBG"}, {an::CfaPattern::Gbrg, 0u, 0u, "GBRG"},
        {an::CfaPattern::Rggb, 1u, 0u, "GRBG"}, {an::CfaPattern::Rggb, 0u, 1u, "GBRG"},
        {an::CfaPattern::Rggb, 1u, 1u, "BGGR"},
    };
    for (const auto& mosaic : mosaics)
    {
        FrameShape shape;
        shape.width = 6u;
        shape.height = 4u;
        const std::optional<an::CfaDescriptor> cfa =
            an::CfaDescriptor::create(mosaic.pattern, mosaic.originX, mosaic.originY);
        ASSERT_TRUE(cfa.has_value());
        shape.cfa = *cfa;
        RawDump dump;
        std::vector<std::uint8_t> payload;
        std::string error;
        ASSERT_TRUE(build(makeFrame<std::uint16_t>(shape, ramp), dump, payload, error)) << error;
        EXPECT_EQ(dump.descriptor.cfaPattern, mosaic.expected)
            << an::cfaPatternName(mosaic.pattern) << " at " << mosaic.originX << "," << mosaic.originY;
    }
}

TEST(RawDumpExportTest, AnXTransMosaicIsRefusedByName)
{
    FrameShape shape;
    shape.width = 12u;
    shape.height = 12u;
    // One camera's six-by-six table, which the engine carries as a tabulated pattern.
    constexpr an::CfaColor g = an::CfaColor::Green;
    constexpr an::CfaColor r = an::CfaColor::Red;
    constexpr an::CfaColor b = an::CfaColor::Blue;
    constexpr an::CfaColorEntries table = {g, g, r, g, g, b, g, g, b, g, g, r, b, r, g, r, b, g,
                                           g, g, b, g, g, r, g, g, r, g, g, b, r, b, g, b, r, g};
    // At its origin the table's first block reads GGGG; two columns in and one row down it reads BGGR, which only
    // the pattern's own kind tells from a Bayer array.
    for (const auto& [originX, originY] : {std::pair{0u, 0u}, std::pair{2u, 1u}})
    {
        const std::optional<an::CfaDescriptor> cfa = an::CfaDescriptor::create(table, originX, originY);
        ASSERT_TRUE(cfa.has_value());
        shape.cfa = *cfa;
        RawDump dump;
        std::vector<std::uint8_t> payload;
        std::string error;
        EXPECT_FALSE(build(makeFrame<std::uint16_t>(shape, ramp), dump, payload, error)) << originX << "," << originY;
        EXPECT_NE(error.find("Bayer"), std::string::npos) << error;
    }
}

TEST(RawDumpExportTest, AContainerIsNamedByItsFormatsUpperCaseName)
{
    EXPECT_STREQ(bridge::rawDumpFormatName(an::CodecFormat::Fits), "FITS");
    EXPECT_STREQ(bridge::rawDumpFormatName(an::CodecFormat::Xisf), "XISF");
    EXPECT_STREQ(bridge::rawDumpFormatName(an::CodecFormat::Tiff), "TIFF");
    EXPECT_STREQ(bridge::rawDumpFormatName(an::CodecFormat::Png), "PNG");
    EXPECT_STREQ(bridge::rawDumpFormatName(an::CodecFormat::Jpeg), "JPEG");
    EXPECT_STREQ(bridge::rawDumpFormatName(an::CodecFormat::Raw), "RAW");
    EXPECT_STREQ(bridge::rawDumpFormatName(an::CodecFormat::Unknown), "unknown");
}

// --- the recorded exports of the checked-in inputs -----------------------------------------------------------

TEST(RawDumpExportTest, TheCheckedInInputsExportAsTheRecordStates)
{
    const std::filesystem::path expectations = expectationDirectory();
    ASSERT_FALSE(expectations.empty());
    std::string error;
    RawDumpExportRecord recorded;
    ASSERT_TRUE(an::bench::readRawDumpExportRecord(expectations / an::bench::kRawDumpExportRecordFile, recorded, error))
        << error;
    std::vector<RawDumpCardDifference> registered;
    ASSERT_TRUE(an::bench::readRawDumpCardDifferences(expectations / an::bench::kRawDumpExportDifferencesFile,
                                                      registered, error))
        << error;
    ASSERT_EQ(recorded.outcomes.size(), an::bench::rawDumpExportCases().size())
        << "the record and the case list disagree";

    Scratch scratch("recorded-exports");
    const std::filesystem::path inputs = expectations / an::bench::kRawDumpExportInputDirectory;
    std::vector<bool> used(registered.size(), false);
    for (const RawDumpExportCase& exportCase : an::bench::rawDumpExportCases())
    {
        const an::bench::RawDumpExportOutcome* const before = recorded.find(exportCase.name);
        ASSERT_NE(before, nullptr) << exportCase.name << " is not in the record";

        const std::filesystem::path descriptorPath = scratch.path() / (exportCase.name + ".andesc.json");
        RawDumpExportOutcome current;
        current.name = exportCase.name;
        if (bridge::exportRawDump((inputs / exportCase.file).string(), descriptorPath.string(), error))
        {
            RawDump dump;
            std::vector<std::uint8_t> payload;
            ASSERT_TRUE(an::bench::readRawDump(descriptorPath.string(), dump, payload, error))
                << exportCase.name << ": " << error;
            EXPECT_EQ(dump.exporterVersion, bridge::kRawDumpExporterVersion);
            EXPECT_EQ(dump.sourcePath, (inputs / exportCase.file).string());
            current = an::bench::exportedOutcome(exportCase.name, dump);
        }
        else
        {
            EXPECT_FALSE(std::filesystem::exists(descriptorPath)) << exportCase.name << " left a descriptor behind";
        }
        std::vector<bool> here;
        for (const std::string& problem : an::bench::compareRawDumpExports(*before, current, registered, here))
        {
            ADD_FAILURE() << problem;
        }
        for (std::size_t entry = 0u; entry < here.size(); ++entry)
        {
            used[entry] = used[entry] || here[entry];
        }
    }
    for (std::size_t entry = 0u; entry < registered.size(); ++entry)
    {
        EXPECT_TRUE(used[entry]) << "the register names a difference that no longer occurs: "
                                 << registered[entry].caseName << " " << registered[entry].key;
    }
}

// --- the runner's command line ---------------------------------------------------------------------------------

TEST(RawDumpExportTest, TheRunnerExportsWithItsCommandLineAndExitCodes)
{
    const std::filesystem::path runner = runnerBesideThisSuite();
    ASSERT_TRUE(std::filesystem::exists(runner)) << runner.string() << " is built beside this suite";
    const std::filesystem::path expectations = expectationDirectory();
    ASSERT_FALSE(expectations.empty());
    const std::filesystem::path input = expectations / an::bench::kRawDumpExportInputDirectory / "fits-uint16-grey.fit";
    Scratch scratch("command-line");
    const std::filesystem::path log = scratch.path() / "runner.log";

    const std::filesystem::path written = scratch.path() / "light.andesc.json";
    EXPECT_EQ(runRunner({"--export", input.string(), "--export-to", written.string()}, log), 0);
    RawDump dump;
    std::vector<std::uint8_t> payload;
    std::string error;
    ASSERT_TRUE(an::bench::readRawDump(written.string(), dump, payload, error)) << error;
    EXPECT_EQ(dump.payloadFile, "light.andesc.anraw");
    EXPECT_EQ(dump.sourceFormat, "FITS");

    const std::filesystem::path refused = scratch.path() / "refused.andesc.json";
    EXPECT_EQ(runRunner({"--export", (scratch.path() / "missing.fits").string(), "--export-to", refused.string()}, log),
              1);
    EXPECT_FALSE(std::filesystem::exists(refused));
    std::ifstream logFile(log, std::ios::binary);
    const std::string said((std::istreambuf_iterator<char>(logFile)), std::istreambuf_iterator<char>());
    EXPECT_NE(said.find("cannot decode"), std::string::npos) << said;
    logFile.close();

    EXPECT_EQ(runRunner({"--export", input.string()}, log), 3);
    EXPECT_EQ(runRunner({"--export", input.string(), "--export-to"}, log), 3);
    EXPECT_EQ(runRunner({"--export", input.string(), "--export-to", written.string(), "--threads", "2"}, log), 3);
}
