// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the image snapshot container.
//
// A snapshot is what an undo history restores from, so the test that matters most is the round trip: every one of
// the eight resident sample types, in planar and interleaved layouts with and without row padding, holding the
// values a careless copy loses - negative zero, a NaN with a payload, the infinities, subnormals and both
// extremes - comes back bit for bit; and every metadata object, a world coordinate solution with distortion among
// the cards, and a validity mask come back equal. The rest pins the format as a format: the header's fields where
// the format places them, each section's checksum an XXH64 an independent implementation here agrees
// with, a version this build does not read refused before anything else, and a file with any one byte damaged,
// cut short at any length or grown by one byte refused. A write is whole or nothing, and a read is charged to the
// context and refused by its budget like any other allocation.

#include <AfterNightEngine/IO/ImageSnapshot.h>

#include <AfterNightEngine/Algorithms/Astrometry/WcsCards.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/IO/ArtifactIdentity.h>
#include <AfterNightEngine/IO/CodecError.h>
#include <AfterNightEngine/IO/OutputTransaction.h>
#include <AfterNightEngine/IO/PendingOutput.h>
#include <AfterNightEngine/Image/AcquisitionMetadata.h>
#include <AfterNightEngine/Image/AstrometricSolution.h>
#include <AfterNightEngine/Image/CfaPattern.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/OutputIntent.h>
#include <AfterNightEngine/Image/Provenance.h>
#include <AfterNightEngine/Image/RawFrame.h>
#include <AfterNightEngine/Image/SampleBuffer.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include <gtest/gtest.h>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

#include <array>
#include <bit>
#include <chrono>
#include <cmath>
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
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{

// ---- The format's fixed header.

constexpr std::size_t kHeaderBytes = 256u;
constexpr std::size_t kVersionOffset = 8u;
constexpr std::size_t kFileBytesOffset = 16u;
constexpr std::size_t kMetadataOffsetOffset = 24u;
constexpr std::size_t kMetadataBytesOffset = 32u;
constexpr std::size_t kSampleOffsetOffset = 40u;
constexpr std::size_t kSampleBytesOffset = 48u;
constexpr std::size_t kValidityOffsetOffset = 56u;
constexpr std::size_t kValidityBytesOffset = 64u;
constexpr std::size_t kValidityPlanesOffset = 164u;
constexpr std::size_t kMetadataChecksumOffset = 168u;
constexpr std::size_t kSampleChecksumOffset = 176u;
constexpr std::size_t kValidityChecksumOffset = 184u;
constexpr std::size_t kHeaderChecksumOffset = 248u;

// ---- An independent XXH64, written from the algorithm's published description.

constexpr std::uint64_t kPrime1 = 0x9E3779B185EBCA87ull;
constexpr std::uint64_t kPrime2 = 0xC2B2AE3D27D4EB4Full;
constexpr std::uint64_t kPrime3 = 0x165667B19E3779F9ull;
constexpr std::uint64_t kPrime4 = 0x85EBCA77C2B2AE63ull;
constexpr std::uint64_t kPrime5 = 0x27D4EB2F165667C5ull;

[[nodiscard]] std::uint64_t load64(const unsigned char* bytes)
{
    std::uint64_t value = 0u;
    for (std::size_t i = 0u; i < 8u; ++i)
        value |= std::uint64_t{bytes[i]} << (8u * i);
    return value;
}

[[nodiscard]] std::uint64_t load32(const unsigned char* bytes)
{
    std::uint64_t value = 0u;
    for (std::size_t i = 0u; i < 4u; ++i)
        value |= std::uint64_t{bytes[i]} << (8u * i);
    return value;
}

[[nodiscard]] std::uint64_t round64(std::uint64_t accumulator, std::uint64_t input)
{
    return std::rotl(accumulator + input * kPrime2, 31) * kPrime1;
}

[[nodiscard]] std::uint64_t merge64(std::uint64_t accumulator, std::uint64_t lane)
{
    return (accumulator ^ round64(0u, lane)) * kPrime1 + kPrime4;
}

[[nodiscard]] std::uint64_t referenceXxh64(std::span<const unsigned char> data)
{
    const unsigned char* p = data.data();
    const unsigned char* const end = p + data.size();
    std::uint64_t hash = 0u;
    if (data.size() >= 32u)
    {
        std::uint64_t v1 = kPrime1 + kPrime2;
        std::uint64_t v2 = kPrime2;
        std::uint64_t v3 = 0u;
        std::uint64_t v4 = 0u - kPrime1;
        for (; end - p >= 32; p += 32)
        {
            v1 = round64(v1, load64(p));
            v2 = round64(v2, load64(p + 8));
            v3 = round64(v3, load64(p + 16));
            v4 = round64(v4, load64(p + 24));
        }
        hash = std::rotl(v1, 1) + std::rotl(v2, 7) + std::rotl(v3, 12) + std::rotl(v4, 18);
        hash = merge64(hash, v1);
        hash = merge64(hash, v2);
        hash = merge64(hash, v3);
        hash = merge64(hash, v4);
    }
    else
    {
        hash = kPrime5;
    }
    hash += static_cast<std::uint64_t>(data.size());
    for (; end - p >= 8; p += 8)
        hash = std::rotl(hash ^ round64(0u, load64(p)), 27) * kPrime1 + kPrime4;
    if (end - p >= 4)
    {
        hash = std::rotl(hash ^ (load32(p) * kPrime1), 23) * kPrime2 + kPrime3;
        p += 4;
    }
    for (; p < end; ++p)
        hash = std::rotl(hash ^ (std::uint64_t{*p} * kPrime5), 11) * kPrime1;
    hash ^= hash >> 33;
    hash *= kPrime2;
    hash ^= hash >> 29;
    hash *= kPrime3;
    hash ^= hash >> 32;
    return hash;
}

[[nodiscard]] std::uint64_t referenceXxh64(std::string_view text)
{
    return referenceXxh64(
        std::span<const unsigned char>(reinterpret_cast<const unsigned char*>(text.data()), text.size()));
}

/// The sample section's checksum: the XXH64 of each 4 MiB block's XXH64, eight little-endian bytes each, in order.
[[nodiscard]] std::uint64_t referenceBlockwise(std::span<const unsigned char> section)
{
    constexpr std::size_t kBlock = 4u * 1024u * 1024u;
    std::vector<unsigned char> digests;
    for (std::size_t offset = 0u; offset < section.size(); offset += kBlock)
    {
        const std::uint64_t digest =
            referenceXxh64(section.subspan(offset, (std::min)(kBlock, section.size() - offset)));
        for (std::size_t i = 0u; i < 8u; ++i)
            digests.push_back(static_cast<unsigned char>(digest >> (8u * i)));
    }
    return referenceXxh64(digests);
}

// ---- Building what a snapshot holds.

/// A card, built or the test fails.
[[nodiscard]] an::CodecCard card(an::CodecNamespace origin, std::string keyword, std::string value,
                                 std::string comment = {})
{
    an::Result<an::CodecCard> made =
        an::CodecCard::create(origin, std::move(keyword), std::move(value), std::move(comment));
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : *an::CodecCard::create(an::CodecNamespace::Fits, "X", "", "");
}

/// Metadata in which every object states something other than its default.
[[nodiscard]] an::ImageMetadata fullMetadata(an::ValidityDescriptor validity)
{
    an::RawCameraMetadata camera;
    camera.blackLevel = 2048u;
    camera.channelBlack[0] = 1u;
    camera.channelBlack[3] = 4u;
    camera.whiteLevel = 16383u;
    camera.blackPatternRows = 2u;
    camera.blackPatternColumns = 2u;
    camera.blackPattern = {10u, 11u, 12u, 13u};
    camera.cameraMultipliers[0] = 2.1;
    camera.cameraMultipliers[1] = 1.0;
    camera.cameraMultipliers[2] = 1.6;
    camera.daylightMultipliers[0] = 2.0;
    camera.daylightMultipliers[1] = 1.0;
    camera.daylightMultipliers[2] = 1.5;
    camera.cameraToRgb[0][0] = 1.7;
    camera.cameraToRgb[1][1] = 1.3;
    camera.cameraToRgb[2][2] = 1.9;
    camera.cameraToXyz[0][0] = 0.4;
    camera.cameraToXyz[3][2] = 0.1;
    camera.isoSpeed = 800.0;
    camera.focalLengthMillimeters = 250.0;
    camera.aperture = 4.9;
    camera.orientation = 6;
    camera.exifColorSpace = 1;
    camera.asShotWhiteBalanceApplied = true;
    const an::AcquisitionMetadata acquisition =
        *an::AcquisitionMetadata::create({.instrumentName = "Camera \"Q\"",
                                          .telescopeName = "Refractor",
                                          .observerName = "O'Brien",
                                          .objectName = "NGC 7000",
                                          .filterName = "Ha 3nm",
                                          .exposureSeconds = 300.25,
                                          .sensorTemperatureCelsius = -10.5,
                                          .gainElectronsPerAdu = 0.21,
                                          .binning = an::PixelBinning{2u, 1u},
                                          .exposureStart = an::UtcInstant{-1234567890123456789},
                                          .rawCamera = camera});
    const an::ImageSemantics semantics = *an::ImageSemantics::create(
        {.transferFunction = an::TransferFunction::PureGamma,
         .gammaExponent = 2.4,
         .processingState = {.biasSubtracted = true, .flatFielded = true, .registered = true, .colorCalibrated = true},
         .validity = validity});
    const an::AstrometricSolution solution = *an::AstrometricSolution::create(
        {.referencePoint = {.rightAscensionDegrees = 312.75, .declinationDegrees = 44.33},
         .referencePixelX = 3.5,
         .referencePixelY = 2.25,
         .pixelScaleArcsecPerPixel = 1.87,
         .orientationDegrees = 180.5,
         .mirrored = true,
         .hasDistortion = true});
    an::Provenance provenance = *an::Provenance::create(an::ProvenanceSourceKind::Derived, "stack of 50");
    provenance = *provenance.appended(*an::ProvenanceEntry::create("decodeImage", "light-001.fits"));
    provenance = *provenance.appended(*an::ProvenanceEntry::create("composeBlend", std::string(300u, 'x')));
    std::vector<an::CodecCard> cards = {
        card(an::CodecNamespace::Fits, "RADESYS", "'ICRS'"),
        card(an::CodecNamespace::Fits, "CTYPE1", "'RA---TAN-SIP'"),
        card(an::CodecNamespace::Fits, "CTYPE2", "'DEC--TAN-SIP'"),
        card(an::CodecNamespace::Fits, "CRVAL1", "312.75"),
        card(an::CodecNamespace::Fits, "CRVAL2", "44.33"),
        card(an::CodecNamespace::Fits, "CRPIX1", "4.0"),
        card(an::CodecNamespace::Fits, "CRPIX2", "2.75"),
        card(an::CodecNamespace::Fits, "CD1_1", "-5.19E-04"),
        card(an::CodecNamespace::Fits, "CD1_2", "1.2E-06"),
        card(an::CodecNamespace::Fits, "CD2_1", "1.1E-06"),
        card(an::CodecNamespace::Fits, "CD2_2", "5.19E-04"),
        card(an::CodecNamespace::Fits, "A_ORDER", "2"),
        card(an::CodecNamespace::Fits, "A_2_0", "8.9146E-07"),
        card(an::CodecNamespace::Fits, "B_ORDER", "2"),
        card(an::CodecNamespace::Fits, "B_0_2", "-1.3437E-06"),
        card(an::CodecNamespace::Fits, "HISTORY", "", "a commentary card with no value"),
        card(an::CodecNamespace::Xisf, "Instrument:Filter:Name", "Ha", "an XISF property"),
        card(an::CodecNamespace::CameraRaw, "Exif.Photo.ExposureTime", "300/1"),
        card(an::CodecNamespace::Unspecified, "binary\x01key", std::string("\0\xff", 2u), "bytes, not text"),
    };
    const an::CodecMetadata codec =
        *an::CodecMetadata::create({.sampleFormat = an::RawSampleFormat{2u, an::RawSampleClass::SignedInteger},
                                    .byteOrder = an::ByteOrder::Big,
                                    .scale = 1.0,
                                    .zeroOffset = 32768.0},
                                   std::move(cards));
    const an::OutputIntent intent =
        *an::OutputIntent::create({.sampleType = an::SampleType::UInt16,
                                   .byteOrder = an::ByteOrder::Big,
                                   .scaling = an::OutputScaling::Explicit,
                                   .scale = 65535.0,
                                   .zeroOffset = 0.5,
                                   .rounding = an::OutputRounding::Floor,
                                   .compression = an::OutputCompression::Deflate,
                                   .compressionLevel = 7u,
                                   .byteShuffle = false,
                                   .compressionBlockBytes = std::uint64_t{3u} << 30u,
                                   .checksum = an::OutputChecksum::Sha3Digest512,
                                   .preserveSourceCards = false,
                                   .cards = {card(an::CodecNamespace::Fits, "ORIGIN", "'AfterNight'")}});
    return an::ImageMetadata::create(acquisition, semantics, solution, std::move(provenance), codec, intent);
}

/// The identity every snapshot of this test is written for.
[[nodiscard]] const an::ArtifactIdentity& testIdentity()
{
    static const an::ArtifactIdentity identity = *an::ArtifactIdentity::create("history-0042", "session-7");
    return identity;
}

/// The values a careless copy loses, for one sample type.
template <class SampleT> [[nodiscard]] std::vector<SampleT> awkwardValues()
{
    using Limits = std::numeric_limits<SampleT>;
    if constexpr (std::is_floating_point_v<SampleT>)
    {
        SampleT quiet{};
        SampleT signaling{};
        if constexpr (sizeof(SampleT) == 4u)
        {
            quiet = std::bit_cast<SampleT>(std::uint32_t{0x7FC12345u});
            signaling = std::bit_cast<SampleT>(std::uint32_t{0xFF812345u});
        }
        else
        {
            quiet = std::bit_cast<SampleT>(std::uint64_t{0x7FF8000000ABCDEFull});
            signaling = std::bit_cast<SampleT>(std::uint64_t{0xFFF0000000ABCDEFull});
        }
        return {SampleT{0},
                -SampleT{0},
                quiet,
                signaling,
                Limits::infinity(),
                -Limits::infinity(),
                Limits::denorm_min(),
                -Limits::denorm_min(),
                Limits::lowest(),
                (Limits::max)(),
                (Limits::min)(),
                SampleT{1} / SampleT{3}};
    }
    else
    {
        return {Limits::lowest(),
                (Limits::max)(),
                SampleT{0},
                SampleT{1},
                static_cast<SampleT>(Limits::lowest() + 1),
                static_cast<SampleT>((Limits::max)() - 1)};
    }
}

/// The bytes of every row of every plane of a frame, padding excluded, in order.
[[nodiscard]] std::vector<std::byte> packedBytes(const an::RawFrame& frame)
{
    const an::ImageLayout layout = *frame.layout();
    const std::uint64_t rowBytes =
        std::uint64_t{layout.extent().width()} * layout.samplesPerPixel() * layout.elementSizeBytes();
    std::vector<std::byte> packed;
    for (std::uint32_t plane = 0u; plane < layout.planeCount(); ++plane)
        for (std::uint32_t y = 0u; y < layout.extent().height(); ++y)
        {
            const std::byte* row =
                frame.bytes().data() + plane * layout.planeStrideBytes() + y * layout.rowStrideBytes();
            packed.insert(packed.end(), row, row + rowBytes);
        }
    return packed;
}

/// Every bit of a mask, in order.
[[nodiscard]] std::vector<bool> bitsOf(const an::ValidityMask& mask)
{
    const an::ValidityMaskView<const std::uint8_t> view = *mask.view();
    std::vector<bool> bits;
    for (std::uint32_t plane = 0u; plane < view.planeCount(); ++plane)
        for (std::uint32_t y = 0u; y < view.height(); ++y)
            for (std::uint32_t x = 0u; x < view.width(); ++x)
                bits.push_back(*view.test(plane, x, y));
    return bits;
}

[[nodiscard]] std::uint64_t field(const std::string& bytes, std::size_t offset, std::size_t count)
{
    std::uint64_t value = 0u;
    for (std::size_t i = 0u; i < count; ++i)
        value |= std::uint64_t{static_cast<unsigned char>(bytes[offset + i])} << (8u * i);
    return value;
}

void putField(std::string& bytes, std::size_t offset, std::size_t count, std::uint64_t value)
{
    for (std::size_t i = 0u; i < count; ++i)
        bytes[offset + i] = static_cast<char>(value >> (8u * i));
}

// The version 1 file of a version 2 snapshot whose output intent states no form of its payload and no card.
//
// Such an intent ends the metadata section: the three bytes version 2 added - no shuffling, no block size, no checksum
// - then whether source cards are preserved and the empty card count. Version 1 is the same section without the three,
// so they go, every offset and size after them moves back, and the section's and the header's checksums are taken
// again.
[[nodiscard]] std::string asVersionOne(std::string bytes)
{
    const std::uint64_t metadataBytes = field(bytes, kMetadataBytesOffset, 8u);
    const auto end = static_cast<std::size_t>(field(bytes, kMetadataOffsetOffset, 8u) + metadataBytes);
    EXPECT_EQ(bytes.substr(end - 8u, 3u), std::string(3u, '\0')) << "the intent states a form of its payload";
    bytes.erase(end - 8u, 3u);
    putField(bytes, kVersionOffset, 4u, 1u);
    putField(bytes, kFileBytesOffset, 8u, bytes.size());
    putField(bytes, kMetadataBytesOffset, 8u, metadataBytes - 3u);
    putField(bytes, kSampleOffsetOffset, 8u, field(bytes, kSampleOffsetOffset, 8u) - 3u);
    if (field(bytes, kValidityBytesOffset, 8u) != 0u)
        putField(bytes, kValidityOffsetOffset, 8u, field(bytes, kValidityOffsetOffset, 8u) - 3u);
    const auto* data = reinterpret_cast<const unsigned char*>(bytes.data());
    putField(bytes, kMetadataChecksumOffset, 8u,
             referenceXxh64(std::span<const unsigned char>(data + kHeaderBytes, metadataBytes - 3u)));
    putField(bytes, kHeaderChecksumOffset, 8u,
             referenceXxh64(std::span<const unsigned char>(data, kHeaderChecksumOffset)));
    return bytes;
}

class ImageSnapshotTest : public testing::Test
{
protected:
    void SetUp() override
    {
        m_directory = std::filesystem::temp_directory_path() /
                      ("an-snapshot-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(m_directory);
        an::Result<an::EngineContext> engine = an::EngineContext::create({});
        ASSERT_TRUE(engine.hasValue()) << engine.status().message();
        m_engine = std::move(*engine);
    }

    void TearDown() override
    {
        std::error_code ignored;
        std::filesystem::remove_all(m_directory, ignored);
    }

    /// The UTF-8 path of a file in the test's directory.
    [[nodiscard]] std::string path(std::string_view name) const
    {
        const std::u8string utf8 = (m_directory / name).u8string();
        return {reinterpret_cast<const char*>(utf8.data()), utf8.size()};
    }

    /// An execution of an engine context.
    [[nodiscard]] static an::ExecutionContext execution(const an::EngineContext& engine, std::stop_token stop = {})
    {
        an::ExecutionOptions options;
        options.stopToken = std::move(stop);
        an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, options);
        EXPECT_TRUE(made.hasValue()) << made.status().message();
        return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
    }

    /// An image of one sample type filled with the awkward values, in an arrangement.
    template <class SampleT>
    [[nodiscard]] an::Image<SampleT> awkwardImage(std::uint32_t width, std::uint32_t height, std::uint32_t channels,
                                                  an::StorageArrangement arrangement, an::ImageMetadata metadata)
    {
        const an::ImageDescriptor descriptor = *an::ImageDescriptor::create(
            {.extent = *an::ImageExtent::create(width, height, channels, m_engine.limits()),
             .sampleType = an::kSampleTypeOf<SampleT>,
             .channelSchema = channels == 1u ? an::ChannelSchema::Mono : an::ChannelSchema::Rgb,
             .colorSpace = channels == 1u ? an::ColorSpace::Unspecified : an::ColorSpace::LinearRgb,
             .numericDomain = an::NumericDomain::NormalizedLinear,
             .cfa = {},
             .coordinateOrigin = an::CoordinateOrigin::BottomLeft,
             .pixelAspectRatio = 1.25});
        an::Result<an::Image<SampleT>> image = an::Image<SampleT>::create(
            m_engine, descriptor, arrangement, an::StorageInitialization::Zeroed, std::move(metadata));
        EXPECT_TRUE(image.hasValue()) << image.status().message();
        const std::vector<SampleT> values = awkwardValues<SampleT>();
        std::size_t next = 0u;
        const an::ImageView<SampleT> view = image->write()->view();
        for (std::uint32_t plane = 0u; plane < view.planeCount(); ++plane)
            for (std::uint32_t y = 0u; y < view.height(); ++y)
            {
                // The row's span is copied out of the optional that carries it, which ends with this statement.
                const std::span<SampleT> row = *view.row(plane, y);
                for (SampleT& sample : row)
                    sample = values[next++ % values.size()];
            }
        return std::move(*image);
    }

    /// Writes a frame's snapshot, or fails the test.
    void write(const std::string& destination, const an::RawFrame& frame, const an::ImageMetadata& metadata,
               const an::ValidityMask* mask = nullptr)
    {
        const an::ExecutionContext run = execution(m_engine);
        const an::Result<an::ImageSnapshotWriteReport> written = an::writeImageSnapshot(
            m_engine, run, {.destinationPath = destination, .identity = &testIdentity()}, frame, metadata, mask);
        ASSERT_TRUE(written.hasValue()) << written.status().message();
        EXPECT_EQ(written->bytesWritten,
                  std::filesystem::file_size(m_directory / std::filesystem::path(destination).filename()));
    }

    /// Reads a snapshot, or reports why not.
    [[nodiscard]] an::Result<an::ImageSnapshotContents> read(const std::string& source)
    {
        const an::ExecutionContext run = execution(m_engine);
        return an::readImageSnapshot(m_engine, run, source);
    }

    /// The bytes of a file in the test's directory.
    [[nodiscard]] std::string bytesOf(const std::string& file) const
    {
        std::ifstream in(
            std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(file.data()), file.size())),
            std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    /// Writes bytes to a file in the test's directory.
    void store(const std::string& file, const std::string& bytes) const
    {
        std::ofstream out(
            std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(file.data()), file.size())),
            std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    /// A small snapshot holding something in every section: a colour frame, full metadata and a mask.
    [[nodiscard]] std::string smallSnapshot(std::string_view name)
    {
        const an::ValidityDescriptor masked = *an::ValidityDescriptor::create({.rule = an::ValidityRule::ExplicitMask});
        an::Image<std::uint16_t> image =
            awkwardImage<std::uint16_t>(3u, 2u, 3u, an::StorageArrangement::Interleaved, fullMetadata(masked));
        an::ValidityMask mask =
            *an::ValidityMask::createAllValid(m_engine, *an::ImageExtent::create(3u, 2u, 1u, m_engine.limits()));
        (void)mask.view()->setValid(0u, 1u, 1u, false);
        const std::string destination = path(name);
        write(destination, *an::borrowImageFrame(image), image.metadata(), &mask);
        return destination;
    }

    std::filesystem::path m_directory;
    an::EngineContext m_engine;
};

} // namespace

#define AN_SNAPSHOT_ROUND_TRIP(SampleT)                                                                                \
    do                                                                                                                 \
    {                                                                                                                  \
        struct Shape                                                                                                   \
        {                                                                                                              \
            std::uint32_t width;                                                                                       \
            std::uint32_t channels;                                                                                    \
            an::StorageArrangement arrangement;                                                                        \
        };                                                                                                             \
        for (const Shape shape :                                                                                       \
             {Shape{7u, 1u, an::StorageArrangement::Planar}, Shape{16u, 3u, an::StorageArrangement::Planar},           \
              Shape{5u, 3u, an::StorageArrangement::Interleaved}})                                                     \
        {                                                                                                              \
            an::Image<SampleT> image =                                                                                 \
                awkwardImage<SampleT>(shape.width, 4u, shape.channels, shape.arrangement, an::ImageMetadata{});        \
            const an::RawFrame frame = *an::borrowImageFrame(image);                                                   \
            const std::string destination =                                                                            \
                path(std::string(an::sampleTypeName(an::kSampleTypeOf<SampleT>)) + "-" + std::to_string(shape.width) + \
                     "-" + std::to_string(shape.channels) + ".ansnap");                                                \
            write(destination, frame, image.metadata());                                                               \
            an::Result<an::ImageSnapshotContents> restored = read(destination);                                        \
            ASSERT_TRUE(restored.hasValue()) << restored.status().message();                                           \
            const an::RawFrameDescriptor expected = *frame.descriptor();                                               \
            EXPECT_EQ(*restored->frame.descriptor(), expected);                                                        \
            const an::ImageLayout layout = *restored->frame.layout();                                                  \
            EXPECT_EQ(layout.samplesPerPixel(), frame.layout()->samplesPerPixel());                                    \
            EXPECT_EQ(layout.planeCount(), frame.layout()->planeCount());                                              \
            EXPECT_EQ(layout.rowStrideBytes(),                                                                         \
                      std::uint64_t{shape.width} * layout.samplesPerPixel() * sizeof(SampleT));                        \
            EXPECT_EQ(packedBytes(restored->frame), packedBytes(frame))                                                \
                << an::sampleTypeName(an::kSampleTypeOf<SampleT>) << " width " << shape.width;                         \
            EXPECT_EQ(restored->metadata, an::ImageMetadata{});                                                        \
            EXPECT_FALSE(restored->validity.has_value());                                                              \
            EXPECT_EQ(restored->identity, testIdentity());                                                             \
            an::Result<an::Image<SampleT>> adopted = an::Image<SampleT>::adopt(std::move(restored->frame));            \
            ASSERT_TRUE(adopted.hasValue()) << adopted.status().message();                                             \
            EXPECT_EQ(*adopted->descriptor(), *image.descriptor());                                                    \
        }                                                                                                              \
    } while (false)

TEST_F(ImageSnapshotTest, EveryResidentTypeRoundTripsBitForBitInEveryLayout)
{
    AN_SNAPSHOT_ROUND_TRIP(std::uint8_t);
    AN_SNAPSHOT_ROUND_TRIP(std::int8_t);
    AN_SNAPSHOT_ROUND_TRIP(std::uint16_t);
    AN_SNAPSHOT_ROUND_TRIP(std::int16_t);
    AN_SNAPSHOT_ROUND_TRIP(std::uint32_t);
    AN_SNAPSHOT_ROUND_TRIP(std::int32_t);
    AN_SNAPSHOT_ROUND_TRIP(float);
    AN_SNAPSHOT_ROUND_TRIP(double);
}

#undef AN_SNAPSHOT_ROUND_TRIP

TEST_F(ImageSnapshotTest, AMosaicKeepsItsFilterArrayAndPhase)
{
    for (const auto& [pattern, x, y] :
         {std::tuple{an::CfaPattern::Rggb, 1u, 0u}, std::tuple{an::CfaPattern::Gbrg, 0u, 1u}})
    {
        const an::ImageDescriptor descriptor =
            *an::ImageDescriptor::create({.extent = *an::ImageExtent::create(6u, 4u, 1u, m_engine.limits()),
                                          .sampleType = an::SampleType::UInt16,
                                          .channelSchema = an::ChannelSchema::Mono,
                                          .colorSpace = an::ColorSpace::Unspecified,
                                          .numericDomain = an::NumericDomain::RawSensorCode,
                                          .cfa = *an::CfaDescriptor::create(pattern, x, y),
                                          .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                          .pixelAspectRatio = 1.0});
        an::Image<std::uint16_t> image = *an::Image<std::uint16_t>::createFilled(
            m_engine, descriptor, an::StorageArrangement::Planar, std::uint16_t{4095u});
        const std::string destination = path("mosaic.ansnap");
        write(destination, *an::borrowImageFrame(image), image.metadata());
        an::Result<an::ImageSnapshotContents> restored = read(destination);
        ASSERT_TRUE(restored.hasValue()) << restored.status().message();
        EXPECT_EQ(restored->frame.descriptor()->cfa(), descriptor.cfa());
    }
}

TEST_F(ImageSnapshotTest, EveryMetadataObjectRoundTripsExactly)
{
    const an::ValidityDescriptor ranged = *an::ValidityDescriptor::create(
        {.rule = an::ValidityRule::DomainRange, .lowestValid = -0.5, .highestValid = 1.5});
    const an::ImageMetadata metadata = fullMetadata(ranged);
    an::Image<float> image = awkwardImage<float>(8u, 6u, 1u, an::StorageArrangement::Planar, metadata);
    const std::string destination = path("full.ansnap");
    write(destination, *an::borrowImageFrame(image), image.metadata());

    an::Result<an::ImageSnapshotContents> restored = read(destination);
    ASSERT_TRUE(restored.hasValue()) << restored.status().message();
    EXPECT_EQ(restored->metadata, metadata);
    EXPECT_EQ(restored->metadata.acquisition(), metadata.acquisition());
    EXPECT_EQ(restored->metadata.semantics(), metadata.semantics());
    EXPECT_EQ(restored->metadata.astrometry(), metadata.astrometry());
    EXPECT_EQ(restored->metadata.provenance(), metadata.provenance());
    EXPECT_EQ(restored->metadata.codec(), metadata.codec());
    EXPECT_EQ(restored->metadata.outputIntent(), metadata.outputIntent());

    // The world coordinate solution, distortion and all, is still the one the cards describe.
    const an::Result<an::WcsSolution> solution = an::parseWcsCards(restored->metadata.codec().cards(), 8u, 6u);
    ASSERT_TRUE(solution.hasValue()) << solution.status().message();
    EXPECT_EQ(an::checkWcsCards(restored->metadata.codec().cards()), an::WcsCardError::None);

    // An intent that is absent stays absent: absence is a statement too.
    const an::ImageMetadata withoutIntent =
        an::ImageMetadata::create(metadata.acquisition(), metadata.semantics(), metadata.astrometry(),
                                  metadata.provenance(), metadata.codec(), std::nullopt);
    write(destination, *an::borrowImageFrame(image), withoutIntent);
    an::Result<an::ImageSnapshotContents> again = read(destination);
    ASSERT_TRUE(again.hasValue()) << again.status().message();
    EXPECT_EQ(again->metadata, withoutIntent);
}

TEST_F(ImageSnapshotTest, TheValidityMaskRoundTripsAndTheMaskRuleNeedsOne)
{
    const an::ValidityDescriptor masked = *an::ValidityDescriptor::create({.rule = an::ValidityRule::ExplicitMask});
    an::Image<float> image = awkwardImage<float>(
        13u, 3u, 3u, an::StorageArrangement::Planar,
        an::ImageMetadata::create({}, *an::ImageSemantics::create({.validity = masked}), {}, {}, {}, std::nullopt));
    const an::RawFrame frame = *an::borrowImageFrame(image);

    for (const std::uint32_t planes : {1u, 3u})
    {
        an::ValidityMask mask =
            *an::ValidityMask::createAllValid(m_engine, *an::ImageExtent::create(13u, 3u, planes, m_engine.limits()));
        for (std::uint32_t plane = 0u; plane < planes; ++plane)
            for (std::uint32_t x = plane; x < 13u; x += 3u)
                (void)mask.view()->setValid(plane, x, (x + plane) % 3u, false);
        const std::string destination = path("masked-" + std::to_string(planes) + ".ansnap");
        write(destination, frame, image.metadata(), &mask);
        an::Result<an::ImageSnapshotContents> restored = read(destination);
        ASSERT_TRUE(restored.hasValue()) << restored.status().message();
        ASSERT_TRUE(restored->validity.has_value());
        EXPECT_EQ(restored->validity->extent(), mask.extent());
        EXPECT_EQ(bitsOf(*restored->validity), bitsOf(mask));
        const an::ExecutionContext probing = execution(m_engine);
        const an::Result<an::ImageSnapshotInfo> info = an::probeImageSnapshot(m_engine, probing, destination);
        ASSERT_TRUE(info.hasValue()) << info.status().message();
        EXPECT_EQ(info->validityPlanes, planes);
    }

    // The mask rule with no mask, a mask of the wrong size and a mask with planes neither shared nor per channel.
    const auto refusal = [&](const an::ValidityMask* given) {
        const an::ExecutionContext run = execution(m_engine);
        const an::Result<an::ImageSnapshotWriteReport> written = an::writeImageSnapshot(
            m_engine, run, {.destinationPath = path("refused.ansnap"), .identity = &testIdentity()}, frame,
            image.metadata(), given);
        return written.hasValue() ? an::ErrorCode::Ok : written.status().code();
    };
    EXPECT_EQ(refusal(nullptr), an::ErrorCode::IncompatibleArguments);
    an::ValidityMask narrow =
        *an::ValidityMask::createAllValid(m_engine, *an::ImageExtent::create(12u, 3u, 1u, m_engine.limits()));
    EXPECT_EQ(refusal(&narrow), an::ErrorCode::IncompatibleArguments);
    an::ValidityMask twoPlanes =
        *an::ValidityMask::createAllValid(m_engine, *an::ImageExtent::create(13u, 3u, 2u, m_engine.limits()));
    EXPECT_EQ(refusal(&twoPlanes), an::ErrorCode::IncompatibleArguments);
    an::ValidityMask released;
    EXPECT_EQ(refusal(&released), an::ErrorCode::NotInitialized);
    EXPECT_FALSE(std::filesystem::exists(m_directory / "refused.ansnap"));

    // A mask beside a rule decided by value is stored and given back as it was.
    an::Image<float> plain = awkwardImage<float>(13u, 3u, 1u, an::StorageArrangement::Planar, an::ImageMetadata{});
    an::ValidityMask cached =
        *an::ValidityMask::create(m_engine, *an::ImageExtent::create(13u, 3u, 1u, m_engine.limits()));
    (void)cached.view()->setValid(0u, 12u, 2u, true);
    write(path("cached.ansnap"), *an::borrowImageFrame(plain), plain.metadata(), &cached);
    an::Result<an::ImageSnapshotContents> restored = read(path("cached.ansnap"));
    ASSERT_TRUE(restored.hasValue()) << restored.status().message();
    ASSERT_TRUE(restored->validity.has_value());
    EXPECT_EQ(bitsOf(*restored->validity), bitsOf(cached));
}

TEST_F(ImageSnapshotTest, TheProbeReadsTheHeaderAndTheMetadataWithoutTheSamples)
{
    const std::string destination = smallSnapshot("probe.ansnap");
    std::string bytes = bytesOf(destination);
    const std::size_t samples = static_cast<std::size_t>(field(bytes, kSampleOffsetOffset, 8u));
    bytes[samples] = static_cast<char>(bytes[samples] ^ 0x10);
    store(destination, bytes);

    const an::Result<an::ImageSnapshotInfo> info = [&] {
        const an::ExecutionContext probing = execution(m_engine);
        return an::probeImageSnapshot(m_engine, probing, destination);
    }();
    ASSERT_TRUE(info.hasValue()) << info.status().message();
    EXPECT_EQ(info->formatVersion, an::kImageSnapshotFormatVersion);
    EXPECT_EQ(info->descriptor.extent(), *an::ImageExtent::create(3u, 2u, 3u, m_engine.limits()));
    EXPECT_EQ(info->descriptor.sampleFormat(), (an::RawSampleFormat{2u, an::RawSampleClass::UnsignedInteger}));
    EXPECT_EQ(info->layout.samplesPerPixel(), 3u);
    EXPECT_EQ(info->layout.planeCount(), 1u);
    EXPECT_EQ(info->validityPlanes, 1u);
    EXPECT_EQ(info->fileBytes, bytes.size());
    EXPECT_EQ(info->identity, testIdentity());

    const an::Result<an::ImageSnapshotContents> restored = read(destination);
    ASSERT_FALSE(restored.hasValue());
    EXPECT_EQ(restored.status().code(), an::ErrorCode::InvalidData);
    EXPECT_EQ(an::codecFailureReason(restored.status()), an::CodecFailure::PayloadUnreadable);
}

TEST_F(ImageSnapshotTest, TheHeaderStatesEverySectionAndEachSectionsXxh64)
{
    // The reference implementation agrees with the algorithm's published vectors before it judges anything.
    EXPECT_EQ(referenceXxh64(std::string_view{}), 0xEF46DB3751D8E999ull);
    EXPECT_EQ(referenceXxh64("a"), 0xD24EC4F1A98C6E5Bull);
    EXPECT_EQ(referenceXxh64("abc"), 0x44BC2CF5AD770999ull);
    EXPECT_EQ(referenceXxh64("Nobody inspects the spammish repetition"), 0xFBCEA83C8A378BF1ull);

    const std::string bytes = bytesOf(smallSnapshot("layout.ansnap"));
    ASSERT_GT(bytes.size(), kHeaderBytes);
    EXPECT_EQ(bytes.substr(0u, 8u), std::string("ANSNAP\r\n"));
    EXPECT_EQ(field(bytes, kVersionOffset, 4u), 2u);
    EXPECT_EQ(field(bytes, 12u, 4u), kHeaderBytes);
    EXPECT_EQ(field(bytes, kFileBytesOffset, 8u), bytes.size());
    const std::uint64_t metadataOffset = field(bytes, kMetadataOffsetOffset, 8u);
    const std::uint64_t metadataBytes = field(bytes, kMetadataBytesOffset, 8u);
    const std::uint64_t sampleOffset = field(bytes, kSampleOffsetOffset, 8u);
    const std::uint64_t sampleBytes = field(bytes, kSampleBytesOffset, 8u);
    const std::uint64_t validityOffset = field(bytes, kValidityOffsetOffset, 8u);
    const std::uint64_t validityBytes = field(bytes, kValidityBytesOffset, 8u);
    EXPECT_EQ(metadataOffset, kHeaderBytes);
    EXPECT_EQ(sampleOffset, metadataOffset + metadataBytes);
    EXPECT_EQ(sampleBytes, 3u * 2u * 3u * 2u);
    EXPECT_EQ(validityOffset, sampleOffset + sampleBytes);
    EXPECT_EQ(validityBytes, 2u);
    EXPECT_EQ(validityOffset + validityBytes, bytes.size());
    EXPECT_EQ(field(bytes, kValidityPlanesOffset, 4u), 1u);

    const auto section = [&](std::uint64_t offset, std::uint64_t count) {
        return referenceXxh64(std::span<const unsigned char>(
            reinterpret_cast<const unsigned char*>(bytes.data()) + offset, static_cast<std::size_t>(count)));
    };
    EXPECT_EQ(field(bytes, kMetadataChecksumOffset, 8u), section(metadataOffset, metadataBytes));
    EXPECT_EQ(field(bytes, kSampleChecksumOffset, 8u),
              referenceBlockwise(std::span<const unsigned char>(
                  reinterpret_cast<const unsigned char*>(bytes.data()) + sampleOffset, sampleBytes)));
    EXPECT_EQ(field(bytes, kValidityChecksumOffset, 8u), section(validityOffset, validityBytes));
    EXPECT_EQ(field(bytes, kHeaderChecksumOffset, 8u), section(0u, kHeaderChecksumOffset));

    // The samples are stored packed and little-endian, in the order the frame's layout gave them.
    const std::vector<std::uint16_t> values = awkwardValues<std::uint16_t>();
    for (std::size_t index = 0u; index < sampleBytes / 2u; ++index)
        EXPECT_EQ(field(bytes, static_cast<std::size_t>(sampleOffset) + 2u * index, 2u), values[index % values.size()])
            << index;
    // The mask's two rows, one byte each, the cleared pixel at (1, 1) and the pad bits zero.
    EXPECT_EQ(field(bytes, static_cast<std::size_t>(validityOffset), 1u), 0x07u);
    EXPECT_EQ(field(bytes, static_cast<std::size_t>(validityOffset) + 1u, 1u), 0x05u);
}

TEST_F(ImageSnapshotTest, ASnapshotOfSeveralBlocksIsReadAndCheckedByEveryAdmittedWorker)
{
    // A frame of five 4 MiB blocks and a little more, read by an engine that admits four workers.
    an::Result<an::EngineContext> wide = an::EngineContext::create({.maxConcurrency = 4u});
    ASSERT_TRUE(wide.hasValue()) << wide.status().message();
    an::Image<float> image = awkwardImage<float>(2049u, 2561u, 1u, an::StorageArrangement::Planar, an::ImageMetadata{});
    const an::RawFrame frame = *an::borrowImageFrame(image);
    const std::string destination = path("blocks.ansnap");
    write(destination, frame, image.metadata());

    const std::string bytes = bytesOf(destination);
    const std::uint64_t sampleOffset = field(bytes, kSampleOffsetOffset, 8u);
    const std::uint64_t sampleBytes = field(bytes, kSampleBytesOffset, 8u);
    ASSERT_GT(sampleBytes, 5u * 4u * 1024u * 1024u);
    EXPECT_EQ(field(bytes, kSampleChecksumOffset, 8u),
              referenceBlockwise(std::span<const unsigned char>(
                  reinterpret_cast<const unsigned char*>(bytes.data()) + sampleOffset, sampleBytes)));

    {
        const an::ExecutionContext run = execution(*wide);
        an::Result<an::ImageSnapshotContents> restored = an::readImageSnapshot(*wide, run, destination);
        ASSERT_TRUE(restored.hasValue()) << restored.status().message();
        EXPECT_EQ(packedBytes(restored->frame), packedBytes(frame));
    }

    // A flipped bit in the fourth block is found by whichever worker reads it.
    std::string damaged = bytes;
    const std::size_t inFourthBlock = static_cast<std::size_t>(sampleOffset) + 3u * 4u * 1024u * 1024u + 12345u;
    damaged[inFourthBlock] = static_cast<char>(damaged[inFourthBlock] ^ 0x40);
    store(destination, damaged);
    const an::ExecutionContext run = execution(*wide);
    const an::Result<an::ImageSnapshotContents> refused = an::readImageSnapshot(*wide, run, destination);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidData);
    EXPECT_EQ(an::codecFailureReason(refused.status()), an::CodecFailure::PayloadUnreadable);
}

TEST_F(ImageSnapshotTest, AnyDamagedByteIsRefused)
{
    const std::string destination = smallSnapshot("damaged.ansnap");
    const std::string pristine = bytesOf(destination);
    ASSERT_TRUE(read(destination).hasValue());
    for (std::size_t offset = 0u; offset < pristine.size(); ++offset)
    {
        std::string damaged = pristine;
        damaged[offset] = static_cast<char>(damaged[offset] ^ 0x01);
        store(destination, damaged);
        const an::Result<an::ImageSnapshotContents> restored = read(destination);
        ASSERT_FALSE(restored.hasValue()) << "a flipped bit at byte " << offset << " was read";
        const an::ErrorCode expected = offset >= kVersionOffset && offset < kVersionOffset + 4u
                                           ? an::ErrorCode::Unsupported
                                           : an::ErrorCode::InvalidData;
        EXPECT_EQ(restored.status().code(), expected) << "byte " << offset << ": " << restored.status().message();
    }
}

TEST_F(ImageSnapshotTest, EveryTruncationAndATrailingByteAreRefused)
{
    const std::string destination = smallSnapshot("truncated.ansnap");
    const std::string pristine = bytesOf(destination);
    for (std::size_t length = 0u; length < pristine.size(); ++length)
    {
        store(destination, pristine.substr(0u, length));
        const an::Result<an::ImageSnapshotContents> restored = read(destination);
        ASSERT_FALSE(restored.hasValue()) << "a file cut to " << length << " bytes was read";
        EXPECT_EQ(restored.status().code(), an::ErrorCode::InvalidData) << length;
        EXPECT_EQ(an::codecFailureReason(restored.status()), an::CodecFailure::MetadataUnreadable) << length;
    }
    store(destination, pristine + std::string(1u, '\0'));
    const an::Result<an::ImageSnapshotContents> grown = read(destination);
    ASSERT_FALSE(grown.hasValue());
    EXPECT_EQ(grown.status().code(), an::ErrorCode::InvalidData);
}

TEST_F(ImageSnapshotTest, AVersionThisBuildDoesNotReadIsRefusedBeforeAnythingElse)
{
    const std::string destination = smallSnapshot("future.ansnap");
    std::string future = bytesOf(destination);
    future[kVersionOffset] = 3;
    // A future version may lay its header out differently, so nothing past the version is consulted.
    future[12] = 0;
    future.resize(kHeaderBytes + 3u);
    store(destination, future);
    const an::Result<an::ImageSnapshotContents> restored = read(destination);
    ASSERT_FALSE(restored.hasValue());
    EXPECT_EQ(restored.status().code(), an::ErrorCode::Unsupported);
    EXPECT_EQ(an::codecFailureReason(restored.status()), an::CodecFailure::MetadataUnreadable);
    const an::ExecutionContext probing = execution(m_engine);
    const an::Result<an::ImageSnapshotInfo> probed = an::probeImageSnapshot(m_engine, probing, destination);
    ASSERT_FALSE(probed.hasValue());
    EXPECT_EQ(probed.status().code(), an::ErrorCode::Unsupported);
}

// The full metadata with an intent that states a compression and no form of its payload.
[[nodiscard]] an::ImageMetadata metadataCompressedAs(an::OutputCompression compression)
{
    const an::ValidityDescriptor ranged = *an::ValidityDescriptor::create(
        {.rule = an::ValidityRule::DomainRange, .lowestValid = -0.5, .highestValid = 1.5});
    const an::ImageMetadata full = fullMetadata(ranged);
    const an::OutputIntent intent =
        *an::OutputIntent::create({.sampleType = an::SampleType::UInt16, .compression = compression});
    return an::ImageMetadata::create(full.acquisition(), full.semantics(), full.astrometry(), full.provenance(),
                                     full.codec(), intent);
}

TEST_F(ImageSnapshotTest, AVersionOneSnapshotIsStillReadAndItsIntentStatesNoFormOfItsPayload)
{
    // Version 2 added three facts of an output intent after its compression level and changed nothing else, so the
    // version 1 file of an image is its version 2 file without them: read, it gives back the same image, its intent
    // stating no form of its payload, and probed, it says it is of version 1.
    const an::ImageMetadata metadata = metadataCompressedAs(an::OutputCompression::Deflate);
    an::Image<float> image = awkwardImage<float>(8u, 6u, 1u, an::StorageArrangement::Planar, metadata);
    const std::string destination = path("one.ansnap");
    write(destination, *an::borrowImageFrame(image), image.metadata());
    store(destination, asVersionOne(bytesOf(destination)));

    const an::Result<an::ImageSnapshotContents> restored = read(destination);
    ASSERT_TRUE(restored.hasValue()) << restored.status().message();
    EXPECT_EQ(restored->metadata, metadata);
    ASSERT_TRUE(restored->metadata.outputIntent().has_value());
    EXPECT_EQ(restored->metadata.outputIntent()->compression(), an::OutputCompression::Deflate);
    EXPECT_FALSE(restored->metadata.outputIntent()->byteShuffle().has_value());
    EXPECT_FALSE(restored->metadata.outputIntent()->compressionBlockBytes().has_value());
    EXPECT_EQ(restored->metadata.outputIntent()->checksum(), an::OutputChecksum::None);
    const an::ExecutionContext probing = execution(m_engine);
    const an::Result<an::ImageSnapshotInfo> probed = an::probeImageSnapshot(m_engine, probing, destination);
    ASSERT_TRUE(probed.hasValue()) << probed.status().message();
    EXPECT_EQ(probed->formatVersion, 1u);
}

TEST_F(ImageSnapshotTest, AVersionOneSnapshotNamingACompressionOnlyALaterVersionNamesIsRefused)
{
    // Version 1 named the compressions up to JPEG, so Zstandard in a version 1 file is not something version 1 wrote.
    const an::ImageMetadata metadata = metadataCompressedAs(an::OutputCompression::Zstd);
    an::Image<float> image = awkwardImage<float>(8u, 6u, 1u, an::StorageArrangement::Planar, metadata);
    const std::string destination = path("later.ansnap");
    write(destination, *an::borrowImageFrame(image), image.metadata());
    ASSERT_TRUE(read(destination).hasValue()) << "version 2 names it";
    store(destination, asVersionOne(bytesOf(destination)));

    const an::Result<an::ImageSnapshotContents> restored = read(destination);
    ASSERT_FALSE(restored.hasValue());
    EXPECT_EQ(restored.status().code(), an::ErrorCode::InvalidData);
    EXPECT_EQ(an::codecFailureReason(restored.status()), an::CodecFailure::MetadataUnreadable);
}

TEST_F(ImageSnapshotTest, OtherFilesAreRefusedAsNotSnapshots)
{
    // A history dump: a 40-byte header of a Float32 mono 2 x 2 image and its samples.
    std::string headerDump(40u + 16u, '\0');
    headerDump[1] = 7;
    headerDump[2] = 1;
    headerDump[8] = 2;
    headerDump[16] = 2;
    headerDump[24] = 1;
    headerDump[32] = 16;
    store(path("header.dump"), headerDump);
    // The developer dump's magic.
    std::string developer = "ANDUMP\r\n";
    developer.resize(300u, '\0');
    store(path("developer.dump"), developer);
    store(path("empty.ansnap"), "");
    for (const std::string_view name : {"header.dump", "developer.dump", "empty.ansnap"})
    {
        const an::Result<an::ImageSnapshotContents> restored = read(path(name));
        ASSERT_FALSE(restored.hasValue()) << name;
        EXPECT_EQ(restored.status().code(), an::ErrorCode::InvalidData) << name;
        EXPECT_EQ(an::codecFailureReason(restored.status()), an::CodecFailure::MetadataUnreadable) << name;
    }
    const an::Result<an::ImageSnapshotContents> missing = read(path("missing.ansnap"));
    ASSERT_FALSE(missing.hasValue());
    EXPECT_EQ(missing.status().code(), an::ErrorCode::ExternalFailure);
    EXPECT_EQ(an::codecFailureReason(missing.status()), an::CodecFailure::MetadataUnreadable);
}

TEST_F(ImageSnapshotTest, AWriteIsWholeOrNothing)
{
    an::Image<float> image = awkwardImage<float>(8u, 8u, 1u, an::StorageArrangement::Planar, an::ImageMetadata{});
    const an::RawFrame frame = *an::borrowImageFrame(image);
    const std::string destination = path("whole.ansnap");
    const auto attempt = [&](an::ImageSnapshotWriteRequest request, std::stop_token stop = {}) {
        const an::ExecutionContext run = execution(m_engine, std::move(stop));
        return an::writeImageSnapshot(m_engine, run, request, frame, image.metadata());
    };
    const auto leftovers = [&] {
        std::uint64_t pending = 0u;
        for (const auto& entry : std::filesystem::directory_iterator(m_directory))
            if (an::isPendingOutputPath(entry.path().filename().string()))
                ++pending;
        return pending;
    };

    // A first write publishes; a second replaces and says so; a refusing one leaves the first in place.
    const an::Result<an::ImageSnapshotWriteReport> first =
        attempt({.destinationPath = destination, .identity = &testIdentity()});
    ASSERT_TRUE(first.hasValue()) << first.status().message();
    EXPECT_TRUE(first->flushed);
    EXPECT_FALSE(first->replacedExisting);
    const std::string published = bytesOf(destination);
    const an::Result<an::ImageSnapshotWriteReport> second = attempt(
        {.destinationPath = destination, .identity = &testIdentity(), .durability = an::OutputDurability::CloseOnly});
    ASSERT_TRUE(second.hasValue()) << second.status().message();
    EXPECT_FALSE(second->flushed);
    EXPECT_TRUE(second->replacedExisting);
    const an::Result<an::ImageSnapshotWriteReport> refused =
        attempt({.destinationPath = destination,
                 .identity = &testIdentity(),
                 .replacePolicy = an::OutputReplacePolicy::RefuseExisting});
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidState);
    EXPECT_EQ(an::codecFailureReason(refused.status()), an::CodecFailure::ExistingDestination);
    EXPECT_EQ(bytesOf(destination), published);

    // A stop before anything is written publishes nothing.
    std::stop_source stop;
    stop.request_stop();
    const an::Result<an::ImageSnapshotWriteReport> stopped =
        attempt({.destinationPath = path("stopped.ansnap"), .identity = &testIdentity()}, stop.get_token());
    ASSERT_FALSE(stopped.hasValue());
    EXPECT_EQ(stopped.status().code(), an::ErrorCode::Cancelled);
    EXPECT_FALSE(std::filesystem::exists(m_directory / "stopped.ansnap"));

    // A destination whose directory is missing.
    const an::Result<an::ImageSnapshotWriteReport> nowhere =
        attempt({.destinationPath = path("missing/history.ansnap"), .identity = &testIdentity()});
    ASSERT_FALSE(nowhere.hasValue());
    EXPECT_EQ(nowhere.status().code(), an::ErrorCode::ExternalFailure);
    EXPECT_EQ(an::codecFailureReason(nowhere.status()), an::CodecFailure::DestinationUnavailable);

    // A destination that is a directory cannot be replaced, and the pending file goes.
    std::filesystem::create_directories(m_directory / "occupied.ansnap");
    const an::Result<an::ImageSnapshotWriteReport> occupied =
        attempt({.destinationPath = path("occupied.ansnap"), .identity = &testIdentity()});
    ASSERT_FALSE(occupied.hasValue());
    EXPECT_EQ(occupied.status().code(), an::ErrorCode::ExternalFailure);
    EXPECT_EQ(an::codecFailureReason(occupied.status()), an::CodecFailure::PublishFailure);
    EXPECT_EQ(leftovers(), 0u);
}

TEST_F(ImageSnapshotTest, APendingSiblingIsAnotherWritersAndASweepRecoversAnAbandonedOne)
{
    an::Image<float> image = awkwardImage<float>(4u, 4u, 1u, an::StorageArrangement::Planar, an::ImageMetadata{});
    const an::RawFrame frame = *an::borrowImageFrame(image);
    const std::string destination = path("recovered.ansnap");
    {
        // The sibling a write by this process, this context and this operation reserves, left by one that crashed.
        const an::ExecutionContext run = execution(m_engine);
#if defined(_WIN32)
        const auto process = static_cast<unsigned long long>(_getpid());
#else
        const auto process = static_cast<unsigned long long>(getpid());
#endif
        const std::string pending = destination + "." + std::to_string(process) + "-" + std::to_string(m_engine.id()) +
                                    "-" + std::to_string(run.operationId()) + std::string(an::kPendingOutputSuffix);
        store(pending, "an abandoned payload");
        const an::Result<an::ImageSnapshotWriteReport> busy = an::writeImageSnapshot(
            m_engine, run, {.destinationPath = destination, .identity = &testIdentity()}, frame, image.metadata());
        ASSERT_FALSE(busy.hasValue());
        EXPECT_EQ(busy.status().code(), an::ErrorCode::Busy);
        EXPECT_EQ(an::codecFailureReason(busy.status()), an::CodecFailure::PendingConflict);
        EXPECT_EQ(bytesOf(pending), "an abandoned payload") << "another writer's payload is not this write's to remove";
        EXPECT_FALSE(std::filesystem::exists(m_directory / "recovered.ansnap"));
    }

    const std::u8string directory = m_directory.u8string();
    const an::Result<std::uint64_t> swept =
        an::sweepPendingOutputs(std::string_view(reinterpret_cast<const char*>(directory.data()), directory.size()));
    ASSERT_TRUE(swept.hasValue()) << swept.status().message();
    EXPECT_EQ(*swept, 1u);
    write(destination, frame, image.metadata());
    EXPECT_TRUE(read(destination).hasValue());
}

TEST_F(ImageSnapshotTest, WritesRefuseWhatTheyCannotStore)
{
    an::Image<float> image = awkwardImage<float>(4u, 4u, 1u, an::StorageArrangement::Planar, an::ImageMetadata{});
    const an::RawFrame frame = *an::borrowImageFrame(image);
    const auto refusal = [&](const an::ImageSnapshotWriteRequest& request, const an::RawFrame& given,
                             const an::ImageMetadata& metadata) {
        const an::ExecutionContext run = execution(m_engine);
        const an::Result<an::ImageSnapshotWriteReport> written =
            an::writeImageSnapshot(m_engine, run, request, given, metadata);
        return written.hasValue() ? an::ErrorCode::Ok : written.status().code();
    };
    const std::string destination = path("refused.ansnap");
    EXPECT_EQ(refusal({.destinationPath = destination}, frame, image.metadata()), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(refusal({.destinationPath = "", .identity = &testIdentity()}, frame, image.metadata()),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(refusal({.destinationPath = destination,
                       .identity = &testIdentity(),
                       .durability = static_cast<an::OutputDurability>(9u)},
                      frame, image.metadata()),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(refusal({.destinationPath = destination, .identity = &testIdentity()}, an::RawFrame{}, image.metadata()),
              an::ErrorCode::NotInitialized);

    // A frame the processing model does not hold, and one stated big-endian.
    std::vector<std::byte> wide(4u * 4u * 8u);
    const auto described = [&](an::RawSampleFormat format, an::ByteOrder order) {
        const an::RawFrameDescriptor descriptor =
            *an::RawFrameDescriptor::create({.extent = *an::ImageExtent::create(4u, 4u, 1u, m_engine.limits()),
                                             .sampleFormat = format,
                                             .byteOrder = order,
                                             .channelSchema = an::ChannelSchema::Mono,
                                             .colorSpace = an::ColorSpace::Unspecified,
                                             .numericDomain = an::NumericDomain::NormalizedLinear,
                                             .cfa = {},
                                             .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                             .pixelAspectRatio = 1.0});
        const an::ImageLayout layout = *an::ImageLayout::create(
            {*an::ImageExtent::create(4u, 4u, 1u, m_engine.limits()), format.sizeBytes, 1u, 1u, 1u}, m_engine.limits());
        return *an::RawFrame::create(descriptor, layout, *an::SampleBuffer::borrow(std::span<std::byte>(wide)));
    };
    EXPECT_EQ(refusal({.destinationPath = destination, .identity = &testIdentity()},
                      described({8u, an::RawSampleClass::UnsignedInteger}, an::ByteOrder::Little), image.metadata()),
              an::ErrorCode::Unsupported);
    EXPECT_EQ(refusal({.destinationPath = destination, .identity = &testIdentity()},
                      described({4u, an::RawSampleClass::FloatingPoint}, an::ByteOrder::Big), image.metadata()),
              an::ErrorCode::InvalidArgument);

    // Metadata past the section's bound.
    std::vector<an::CodecCard> cards;
    for (std::uint32_t index = 0u; index < 8192u; ++index)
        cards.push_back(card(an::CodecNamespace::Fits, "K" + std::to_string(index), std::string(4096u, 'v'),
                             std::string(4096u, 'c')));
    const an::ImageMetadata oversized =
        an::ImageMetadata::create({}, {}, {}, {}, *an::CodecMetadata::create({}, std::move(cards)), std::nullopt);
    EXPECT_EQ(refusal({.destinationPath = destination, .identity = &testIdentity()}, frame, oversized),
              an::ErrorCode::LimitExceeded);
    EXPECT_FALSE(std::filesystem::exists(m_directory / "refused.ansnap"));
}

TEST_F(ImageSnapshotTest, ReadsAreChargedToTheContextAndRefusedByItsBudget)
{
    an::Image<double> image = awkwardImage<double>(64u, 64u, 3u, an::StorageArrangement::Planar, an::ImageMetadata{});
    const std::string destination = path("budget.ansnap");
    write(destination, *an::borrowImageFrame(image), image.metadata());

    an::EngineContextOptions small;
    small.limits = an::defaultEngineLimits();
    small.limits.memoryBudgetBytes = 64u * 1024u;
    an::Result<an::EngineContext> constrained = an::EngineContext::create(small);
    ASSERT_TRUE(constrained.hasValue()) << constrained.status().message();
    {
        const an::ExecutionContext run = execution(*constrained);
        const an::Result<an::ImageSnapshotContents> refused = an::readImageSnapshot(*constrained, run, destination);
        ASSERT_FALSE(refused.hasValue());
        EXPECT_EQ(refused.status().code(), an::ErrorCode::MemoryBudgetExceeded);
    }
    EXPECT_EQ(constrained->memoryResource()->counters().liveBytes, 0u);

    {
        const an::ExecutionContext run = execution(m_engine);
        an::Result<an::ImageSnapshotContents> restored = an::readImageSnapshot(m_engine, run, destination);
        ASSERT_TRUE(restored.hasValue()) << restored.status().message();
        EXPECT_GE(m_engine.memoryResource()->counters().liveBytes, 64u * 64u * 3u * 8u);
    }
}

TEST_F(ImageSnapshotTest, AStoppedOrForeignReadReturnsNothing)
{
    const std::string destination = smallSnapshot("stopped.ansnap");
    std::stop_source stop;
    stop.request_stop();
    {
        const an::ExecutionContext run = execution(m_engine, stop.get_token());
        const an::Result<an::ImageSnapshotContents> restored = an::readImageSnapshot(m_engine, run, destination);
        ASSERT_FALSE(restored.hasValue());
        EXPECT_EQ(restored.status().code(), an::ErrorCode::Cancelled);
    }

    an::Result<an::EngineContext> other = an::EngineContext::create({});
    ASSERT_TRUE(other.hasValue());
    const an::ExecutionContext foreign = execution(*other);
    const an::Result<an::ImageSnapshotContents> restored = an::readImageSnapshot(m_engine, foreign, destination);
    ASSERT_FALSE(restored.hasValue());
    EXPECT_EQ(restored.status().code(), an::ErrorCode::IncompatibleArguments);
    const an::Result<an::ImageSnapshotContents> unnamed = an::readImageSnapshot(*other, foreign, "");
    ASSERT_FALSE(unnamed.hasValue());
    EXPECT_EQ(unnamed.status().code(), an::ErrorCode::InvalidArgument);
}
