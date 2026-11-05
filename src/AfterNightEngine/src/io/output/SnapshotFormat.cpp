// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the image snapshot's wire format: the header, the metadata section's encoding and XXH64.
//
// The metadata encoding is one template walked by two sinks - one that counts and one that writes - so the
// length a writer allocates and the bytes it writes cannot disagree. Decoding is the same walk in reverse through a
// cursor that refuses to read past the section; a value is never trusted until its object's own `create` has
// accepted it, which is what makes a decoded snapshot exactly as valid as one the engine built.

#include "io/output/SnapshotFormat.h"

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/AcquisitionMetadata.h>
#include <AfterNightEngine/Image/AstrometricSolution.h>
#include <AfterNightEngine/Image/CfaPattern.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/OutputIntent.h>
#include <AfterNightEngine/Image/Provenance.h>
#include <AfterNightEngine/Image/RawCameraMetadata.h>
#include <AfterNightEngine/Image/RawSampleFormat.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>

#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace an
{
namespace
{
// ---- Wire codes. Versions 1 and 2 store these; a reorder is refused until its mapping is explicit.

template <class Enum, std::size_t Count> constexpr bool wireCodes(const std::array<Enum, Count>& values)
{
    for (std::size_t i = 0u; i < Count; ++i)
        if (static_cast<std::size_t>(values[i]) != i)
            return false;
    return true;
}
static_assert(wireCodes(std::array{RawSampleClass::UnsignedInteger, RawSampleClass::SignedInteger,
                                   RawSampleClass::FloatingPoint, RawSampleClass::Opaque}));
static_assert(wireCodes(std::array{ByteOrder::Native, ByteOrder::Little, ByteOrder::Big}));
static_assert(wireCodes(std::array{ChannelSchema::Mono, ChannelSchema::Rgb, ChannelSchema::Rgba, ChannelSchema::Xyz,
                                   ChannelSchema::Lab, ChannelSchema::Lch, ChannelSchema::Hsl, ChannelSchema::Hsv,
                                   ChannelSchema::Hsi, ChannelSchema::YCbCr, ChannelSchema::Named}));
static_assert(wireCodes(std::array{ColorSpace::Unspecified, ColorSpace::LinearRgb, ColorSpace::DisplayRgb,
                                   ColorSpace::CieXyz, ColorSpace::CieLab, ColorSpace::NarrowbandMapping}));
static_assert(wireCodes(std::array{NumericDomain::NormalizedLinear, NumericDomain::EncodedNonlinear,
                                   NumericDomain::RawSensorCode, NumericDomain::PhysicalScaled}));
static_assert(wireCodes(std::array{CoordinateOrigin::TopLeft, CoordinateOrigin::BottomLeft}));
static_assert(wireCodes(std::array{CfaPattern::None, CfaPattern::Rggb, CfaPattern::Bggr, CfaPattern::Grbg,
                                   CfaPattern::Gbrg, CfaPattern::XTrans}));
static_assert(wireCodes(std::array{CfaColor::Red, CfaColor::Green, CfaColor::Blue}));
static_assert(wireCodes(std::array{TransferFunction::Unspecified, TransferFunction::Linear, TransferFunction::Srgb,
                                   TransferFunction::Rec709, TransferFunction::PureGamma}));
static_assert(wireCodes(std::array{ValidityRule::AllValid, ValidityRule::FiniteOnly, ValidityRule::ZeroIsNoData,
                                   ValidityRule::ExplicitMask, ValidityRule::DomainRange}));
static_assert(wireCodes(std::array{ProvenanceSourceKind::Unknown, ProvenanceSourceKind::File,
                                   ProvenanceSourceKind::Generated, ProvenanceSourceKind::Derived}));
static_assert(wireCodes(std::array{CodecNamespace::Unspecified, CodecNamespace::Fits, CodecNamespace::Tiff,
                                   CodecNamespace::Png, CodecNamespace::Jpeg, CodecNamespace::Xisf,
                                   CodecNamespace::CameraRaw, CodecNamespace::Ser, CodecNamespace::Ffmpeg}));
static_assert(wireCodes(std::array{SampleType::UInt8, SampleType::Int8, SampleType::UInt16, SampleType::Int16,
                                   SampleType::UInt32, SampleType::Int32, SampleType::Float32, SampleType::Float64}));
static_assert(wireCodes(std::array{OutputScaling::None, OutputScaling::Explicit, OutputScaling::NormalizeFullRange}));
static_assert(wireCodes(std::array{OutputRounding::Nearest, OutputRounding::TowardZero, OutputRounding::Floor,
                                   OutputRounding::Ceiling}));
static_assert(wireCodes(std::array{OutputCompression::None, OutputCompression::Rice, OutputCompression::Gzip,
                                   OutputCompression::Plio, OutputCompression::Deflate, OutputCompression::Lzw,
                                   OutputCompression::Jpeg, OutputCompression::Lz4, OutputCompression::Lz4Hc,
                                   OutputCompression::Zstd}));
static_assert(wireCodes(std::array{OutputChecksum::None, OutputChecksum::Sha1, OutputChecksum::Sha256,
                                   OutputChecksum::Sha512, OutputChecksum::Sha3Digest256,
                                   OutputChecksum::Sha3Digest512}));

// ---- XXH64.

constexpr std::uint64_t kPrime1 = 0x9E3779B185EBCA87ull;
constexpr std::uint64_t kPrime2 = 0xC2B2AE3D27D4EB4Full;
constexpr std::uint64_t kPrime3 = 0x165667B19E3779F9ull;
constexpr std::uint64_t kPrime4 = 0x85EBCA77C2B2AE63ull;
constexpr std::uint64_t kPrime5 = 0x27D4EB2F165667C5ull;

// The hash reads its input as little-endian words. A word is copied out in one load and swapped on a big-endian
// host, which none this engine builds for is; a byte-at-a-time assembly costs the hash several times over.
[[nodiscard]] std::uint64_t read64(const std::byte* bytes) noexcept
{
    std::uint64_t value = 0u;
    std::memcpy(&value, bytes, sizeof(value));
    if constexpr (std::endian::native == std::endian::big)
    {
        std::uint64_t swapped = 0u;
        for (std::size_t i = 0u; i < 8u; ++i)
            swapped |= ((value >> (8u * i)) & 0xFFu) << (8u * (7u - i));
        value = swapped;
    }
    return value;
}

[[nodiscard]] std::uint64_t read32(const std::byte* bytes) noexcept
{
    std::uint32_t value = 0u;
    std::memcpy(&value, bytes, sizeof(value));
    if constexpr (std::endian::native == std::endian::big)
    {
        std::uint32_t swapped = 0u;
        for (std::uint32_t i = 0u; i < 4u; ++i)
            swapped |= ((value >> (8u * i)) & 0xFFu) << (8u * (3u - i));
        value = swapped;
    }
    return value;
}

[[nodiscard]] std::uint64_t round(std::uint64_t accumulator, std::uint64_t input) noexcept
{
    return std::rotl(accumulator + input * kPrime2, 31) * kPrime1;
}

[[nodiscard]] std::uint64_t merge(std::uint64_t accumulator, std::uint64_t lane) noexcept
{
    return (accumulator ^ round(0u, lane)) * kPrime1 + kPrime4;
}

/// Folds whole 32-byte stripes into the lanes, returning how many bytes were consumed.
std::size_t stripes(std::array<std::uint64_t, 4u>& lanes, const std::byte* data, std::size_t size) noexcept
{
    std::uint64_t v1 = lanes[0];
    std::uint64_t v2 = lanes[1];
    std::uint64_t v3 = lanes[2];
    std::uint64_t v4 = lanes[3];
    std::size_t used = 0u;
    for (; size - used >= 32u; used += 32u)
    {
        v1 = round(v1, read64(data + used));
        v2 = round(v2, read64(data + used + 8u));
        v3 = round(v3, read64(data + used + 16u));
        v4 = round(v4, read64(data + used + 24u));
    }
    lanes = {v1, v2, v3, v4};
    return used;
}

// ---- Little-endian sinks and the metadata walk.

/// Counts what a walk would write.
class MeasuringSink
{
public:
    void put(const void*, std::size_t count) noexcept
    {
        m_bytes += count;
    }
    [[nodiscard]] std::uint64_t bytes() const noexcept
    {
        return m_bytes;
    }

private:
    std::uint64_t m_bytes = 0u;
};

/// Writes a walk into a span exactly its length.
class BufferSink
{
public:
    explicit BufferSink(std::span<std::byte> destination) noexcept : m_at(destination.data()) {}
    void put(const void* bytes, std::size_t count) noexcept
    {
        if (count != 0u)
            std::memcpy(m_at, bytes, count);
        m_at += count;
    }

private:
    std::byte* m_at;
};

template <class Sink> void putUnsigned(Sink& sink, std::uint64_t value, std::size_t count) noexcept
{
    std::array<unsigned char, 8u> bytes{};
    for (std::size_t i = 0u; i < count; ++i)
        bytes[i] = static_cast<unsigned char>(value >> (8u * i));
    sink.put(bytes.data(), count);
}
template <class Sink> void putU8(Sink& sink, std::uint64_t value) noexcept
{
    putUnsigned(sink, value, 1u);
}
template <class Sink> void putU32(Sink& sink, std::uint64_t value) noexcept
{
    putUnsigned(sink, value, 4u);
}
template <class Sink> void putI32(Sink& sink, int value) noexcept
{
    putUnsigned(sink, static_cast<std::uint32_t>(value), 4u);
}
template <class Sink> void putI64(Sink& sink, std::int64_t value) noexcept
{
    putUnsigned(sink, static_cast<std::uint64_t>(value), 8u);
}
template <class Sink> void putF64(Sink& sink, double value) noexcept
{
    putUnsigned(sink, std::bit_cast<std::uint64_t>(value), 8u);
}
template <class Sink> void putText(Sink& sink, std::string_view text) noexcept
{
    putU32(sink, text.size());
    sink.put(text.data(), text.size());
}
template <class Sink> void putOptionalF64(Sink& sink, const std::optional<double>& value) noexcept
{
    putU8(sink, value.has_value() ? 1u : 0u);
    if (value.has_value())
        putF64(sink, *value);
}
template <class Sink> void putCards(Sink& sink, std::span<const CodecCard> cards) noexcept
{
    putU32(sink, cards.size());
    for (const CodecCard& card : cards)
    {
        putU8(sink, static_cast<std::uint8_t>(card.origin()));
        putText(sink, card.keyword());
        putText(sink, card.value());
        putText(sink, card.comment());
    }
}

template <class Sink> void putCamera(Sink& sink, const RawCameraMetadata& camera) noexcept
{
    putU32(sink, camera.blackLevel);
    for (const std::uint32_t black : camera.channelBlack)
        putU32(sink, black);
    putU32(sink, camera.whiteLevel);
    putU32(sink, camera.blackPatternRows);
    putU32(sink, camera.blackPatternColumns);
    putU32(sink, camera.blackPattern.size());
    for (const std::uint32_t black : camera.blackPattern)
        putU32(sink, black);
    for (const double gain : camera.cameraMultipliers)
        putF64(sink, gain);
    for (const double gain : camera.daylightMultipliers)
        putF64(sink, gain);
    for (const auto& row : camera.cameraToRgb)
        for (const double entry : row)
            putF64(sink, entry);
    for (const auto& row : camera.cameraToXyz)
        for (const double entry : row)
            putF64(sink, entry);
    putF64(sink, camera.isoSpeed);
    putF64(sink, camera.focalLengthMillimeters);
    putF64(sink, camera.aperture);
    putI32(sink, camera.orientation);
    putI32(sink, camera.exifColorSpace);
    putU8(sink, camera.asShotWhiteBalanceApplied ? 1u : 0u);
}

/// The processing steps as bits, in the order `ProcessingState` declares them.
[[nodiscard]] std::uint32_t stateBits(const ProcessingState& state) noexcept
{
    const std::array<bool, 9u> steps{state.biasSubtracted,    state.darkSubtracted, state.flatFielded,
                                     state.cosmeticCorrected, state.demosaiced,     state.backgroundExtracted,
                                     state.registered,        state.normalized,     state.colorCalibrated};
    std::uint32_t bits = 0u;
    for (std::size_t i = 0u; i < steps.size(); ++i)
        if (steps[i])
            bits |= 1u << i;
    return bits;
}

template <class Sink>
void walkMetadata(Sink& sink, const ArtifactIdentity& identity, const ImageMetadata& metadata) noexcept
{
    putText(sink, identity.artifactId());
    putText(sink, identity.runId());

    const AcquisitionMetadata& acquisition = metadata.acquisition();
    putText(sink, acquisition.instrumentName());
    putText(sink, acquisition.telescopeName());
    putText(sink, acquisition.observerName());
    putText(sink, acquisition.objectName());
    putText(sink, acquisition.filterName());
    putOptionalF64(sink, acquisition.exposureSeconds());
    putOptionalF64(sink, acquisition.sensorTemperatureCelsius());
    putOptionalF64(sink, acquisition.gainElectronsPerAdu());
    putU8(sink, acquisition.binning().has_value() ? 1u : 0u);
    if (const std::optional<PixelBinning> binning = acquisition.binning(); binning.has_value())
    {
        putU32(sink, binning->horizontal);
        putU32(sink, binning->vertical);
    }
    putU8(sink, acquisition.exposureStart().has_value() ? 1u : 0u);
    if (const std::optional<UtcInstant> start = acquisition.exposureStart(); start.has_value())
        putI64(sink, start->nanosecondsSinceEpoch);
    putU8(sink, acquisition.rawCamera().has_value() ? 1u : 0u);
    if (acquisition.rawCamera().has_value())
        putCamera(sink, *acquisition.rawCamera());

    const ImageSemantics& semantics = metadata.semantics();
    putU8(sink, static_cast<std::uint8_t>(semantics.transferFunction()));
    putOptionalF64(sink, semantics.gammaExponent());
    putUnsigned(sink, stateBits(semantics.processingState()), 2u);
    putU8(sink, static_cast<std::uint8_t>(semantics.validity().rule()));
    putOptionalF64(sink, semantics.validity().lowestValid());
    putOptionalF64(sink, semantics.validity().highestValid());

    const std::optional<AstrometricSolutionRequest> solution = metadata.astrometry().toRequest();
    putU8(sink, solution.has_value() ? 1u : 0u);
    if (solution.has_value())
    {
        putF64(sink, solution->referencePoint.rightAscensionDegrees);
        putF64(sink, solution->referencePoint.declinationDegrees);
        putF64(sink, solution->referencePixelX);
        putF64(sink, solution->referencePixelY);
        putF64(sink, solution->pixelScaleArcsecPerPixel);
        putF64(sink, solution->orientationDegrees);
        putU8(sink, solution->mirrored ? 1u : 0u);
        putU8(sink, solution->hasDistortion ? 1u : 0u);
    }

    const Provenance& provenance = metadata.provenance();
    putU8(sink, static_cast<std::uint8_t>(provenance.sourceKind()));
    putText(sink, provenance.sourceName());
    putU32(sink, provenance.entries().size());
    for (const ProvenanceEntry& entry : provenance.entries())
    {
        putText(sink, entry.operation());
        putText(sink, entry.detail());
    }

    const SourceEncoding& encoding = metadata.codec().sourceEncoding();
    putU8(sink, encoding.sampleFormat.has_value() ? 1u : 0u);
    if (encoding.sampleFormat.has_value())
    {
        putU8(sink, encoding.sampleFormat->sizeBytes);
        putU8(sink, static_cast<std::uint8_t>(encoding.sampleFormat->sampleClass));
    }
    putU8(sink, encoding.byteOrder.has_value() ? 1u : 0u);
    if (encoding.byteOrder.has_value())
        putU8(sink, static_cast<std::uint8_t>(*encoding.byteOrder));
    putOptionalF64(sink, encoding.scale);
    putOptionalF64(sink, encoding.zeroOffset);
    putCards(sink, metadata.codec().cards());

    const std::optional<OutputIntent>& intent = metadata.outputIntent();
    putU8(sink, intent.has_value() ? 1u : 0u);
    if (intent.has_value())
    {
        putU8(sink, static_cast<std::uint8_t>(intent->sampleType()));
        putU8(sink, static_cast<std::uint8_t>(intent->byteOrder()));
        putU8(sink, static_cast<std::uint8_t>(intent->scaling()));
        putOptionalF64(sink, intent->scale());
        putOptionalF64(sink, intent->zeroOffset());
        putU8(sink, static_cast<std::uint8_t>(intent->rounding()));
        putU8(sink, static_cast<std::uint8_t>(intent->compression()));
        putU8(sink, intent->compressionLevel().has_value() ? 1u : 0u);
        if (intent->compressionLevel().has_value())
            putU32(sink, *intent->compressionLevel());
        // Version 2: byte shuffling, the compression block size and the checksum.
        putU8(sink, intent->byteShuffle().has_value() ? 1u : 0u);
        if (intent->byteShuffle().has_value())
            putU8(sink, *intent->byteShuffle() ? 1u : 0u);
        putU8(sink, intent->compressionBlockBytes().has_value() ? 1u : 0u);
        if (intent->compressionBlockBytes().has_value())
            putI64(sink, static_cast<std::int64_t>(*intent->compressionBlockBytes()));
        putU8(sink, static_cast<std::uint8_t>(intent->checksum()));
        putU8(sink, intent->preserveSourceCards() ? 1u : 0u);
        putCards(sink, intent->cards());
    }
}

// ---- The decoding cursor.

[[nodiscard]] Status undecodable(const char* what)
{
    return Status::failure(ErrorCode::InvalidData,
                           StatusDetail{.message = std::string("the snapshot's metadata does not decode: ") + what,
                                        .operation = "an::readImageSnapshot",
                                        .domain = ErrorDomain::Io});
}

/// Reads the section's fields in order, refusing any read past its end.
class Cursor
{
public:
    explicit Cursor(std::span<const std::byte> bytes) noexcept : m_rest(bytes) {}

    [[nodiscard]] bool failed() const noexcept
    {
        return m_failed;
    }
    [[nodiscard]] bool exhausted() const noexcept
    {
        return m_rest.empty();
    }
    [[nodiscard]] std::uint64_t remaining() const noexcept
    {
        return m_rest.size();
    }
    [[nodiscard]] std::uint64_t unsignedOf(std::size_t count) noexcept
    {
        if (m_failed || m_rest.size() < count)
        {
            m_failed = true;
            return 0u;
        }
        std::uint64_t value = 0u;
        for (std::size_t i = 0u; i < count; ++i)
            value |= std::uint64_t{std::to_integer<unsigned char>(m_rest[i])} << (8u * i);
        m_rest = m_rest.subspan(count);
        return value;
    }
    [[nodiscard]] std::uint8_t u8() noexcept
    {
        return static_cast<std::uint8_t>(unsignedOf(1u));
    }
    [[nodiscard]] std::uint32_t u32() noexcept
    {
        return static_cast<std::uint32_t>(unsignedOf(4u));
    }
    [[nodiscard]] int i32() noexcept
    {
        return static_cast<int>(static_cast<std::int32_t>(static_cast<std::uint32_t>(unsignedOf(4u))));
    }
    [[nodiscard]] std::int64_t i64() noexcept
    {
        return static_cast<std::int64_t>(unsignedOf(8u));
    }
    [[nodiscard]] double f64() noexcept
    {
        return std::bit_cast<double>(unsignedOf(8u));
    }
    /// A presence or logical byte: zero or one, and anything else is a failure.
    [[nodiscard]] bool flag() noexcept
    {
        const std::uint8_t value = u8();
        if (value > 1u)
            m_failed = true;
        return value == 1u;
    }
    [[nodiscard]] std::optional<double> optionalF64() noexcept
    {
        if (!flag())
            return std::nullopt;
        return f64();
    }
    [[nodiscard]] std::string text() noexcept
    {
        const std::uint32_t length = u32();
        if (m_failed || m_rest.size() < length)
        {
            m_failed = true;
            return {};
        }
        std::string value(reinterpret_cast<const char*>(m_rest.data()), length);
        m_rest = m_rest.subspan(length);
        return value;
    }
    /// A count of records each at least `minimumBytes` long, refused when the rest cannot hold them.
    [[nodiscard]] std::uint32_t count(std::uint64_t minimumBytes) noexcept
    {
        const std::uint32_t value = u32();
        if (std::uint64_t{value} * minimumBytes > m_rest.size())
            m_failed = true;
        return m_failed ? 0u : value;
    }

private:
    std::span<const std::byte> m_rest;
    bool m_failed = false;
};

[[nodiscard]] std::optional<std::vector<CodecCard>> takeCards(Cursor& cursor)
{
    const std::uint32_t count = cursor.count(13u);
    std::vector<CodecCard> cards;
    cards.reserve(count);
    for (std::uint32_t index = 0u; index < count && !cursor.failed(); ++index)
    {
        const auto origin = static_cast<CodecNamespace>(cursor.u8());
        std::string keyword = cursor.text();
        std::string value = cursor.text();
        std::string comment = cursor.text();
        if (cursor.failed())
            return std::nullopt;
        Result<CodecCard> card = CodecCard::create(origin, std::move(keyword), std::move(value), std::move(comment));
        if (!card.hasValue())
            return std::nullopt;
        cards.push_back(std::move(*card));
    }
    if (cursor.failed())
        return std::nullopt;
    return cards;
}

[[nodiscard]] std::optional<RawCameraMetadata> takeCamera(Cursor& cursor)
{
    RawCameraMetadata camera;
    camera.blackLevel = cursor.u32();
    for (std::uint32_t& black : camera.channelBlack)
        black = cursor.u32();
    camera.whiteLevel = cursor.u32();
    camera.blackPatternRows = cursor.u32();
    camera.blackPatternColumns = cursor.u32();
    const std::uint32_t pattern = cursor.count(4u);
    camera.blackPattern.reserve(pattern);
    for (std::uint32_t index = 0u; index < pattern; ++index)
        camera.blackPattern.push_back(cursor.u32());
    for (double& gain : camera.cameraMultipliers)
        gain = cursor.f64();
    for (double& gain : camera.daylightMultipliers)
        gain = cursor.f64();
    for (auto& row : camera.cameraToRgb)
        for (double& entry : row)
            entry = cursor.f64();
    for (auto& row : camera.cameraToXyz)
        for (double& entry : row)
            entry = cursor.f64();
    camera.isoSpeed = cursor.f64();
    camera.focalLengthMillimeters = cursor.f64();
    camera.aperture = cursor.f64();
    camera.orientation = cursor.i32();
    camera.exifColorSpace = cursor.i32();
    camera.asShotWhiteBalanceApplied = cursor.flag();
    if (cursor.failed())
        return std::nullopt;
    return camera;
}

// ---- Header fields.

using Header = std::array<std::byte, kSnapshotHeaderBytes>;

void put(Header& header, std::size_t offset, std::size_t count, std::uint64_t value) noexcept
{
    for (std::size_t i = 0u; i < count; ++i)
        header[offset + i] = static_cast<std::byte>(value >> (8u * i));
}

[[nodiscard]] std::uint64_t get(std::span<const std::byte, kSnapshotHeaderBytes> header, std::size_t offset,
                                std::size_t count) noexcept
{
    std::uint64_t value = 0u;
    for (std::size_t i = 0u; i < count; ++i)
        value |= std::uint64_t{std::to_integer<unsigned char>(header[offset + i])} << (8u * i);
    return value;
}

[[nodiscard]] Status damaged(const char* what)
{
    return Status::failure(ErrorCode::InvalidData,
                           StatusDetail{.message = std::string("the snapshot's header is damaged: ") + what,
                                        .operation = "an::readImageSnapshot",
                                        .domain = ErrorDomain::Io});
}

[[nodiscard]] Status beyondLimits(const char* what)
{
    return Status::failure(ErrorCode::LimitExceeded, StatusDetail{.message = std::string("the snapshot's ") + what +
                                                                             " is beyond the reading context's limits",
                                                                  .operation = "an::readImageSnapshot",
                                                                  .domain = ErrorDomain::Io});
}
} // namespace

void SnapshotChecksum::update(std::span<const std::byte> bytes) noexcept
{
    if (!m_started)
    {
        m_lanes = {kPrime1 + kPrime2, kPrime2, 0u, 0u - kPrime1};
        m_started = true;
    }
    m_total += bytes.size();
    const std::byte* data = bytes.data();
    std::size_t size = bytes.size();
    if (m_pendingBytes != 0u)
    {
        const std::size_t taken = (std::min)(size, m_pending.size() - m_pendingBytes);
        std::memcpy(m_pending.data() + m_pendingBytes, data, taken);
        m_pendingBytes += taken;
        data += taken;
        size -= taken;
        if (m_pendingBytes < m_pending.size())
            return;
        (void)stripes(m_lanes, m_pending.data(), m_pending.size());
        m_pendingBytes = 0u;
    }
    const std::size_t used = size >= 32u ? stripes(m_lanes, data, size) : 0u;
    m_pendingBytes = size - used;
    if (m_pendingBytes != 0u)
        std::memcpy(m_pending.data(), data + used, m_pendingBytes);
}

std::uint64_t SnapshotChecksum::digest() const noexcept
{
    std::uint64_t hash = 0u;
    if (m_total >= 32u)
    {
        const auto& [v1, v2, v3, v4] = m_lanes;
        hash = std::rotl(v1, 1) + std::rotl(v2, 7) + std::rotl(v3, 12) + std::rotl(v4, 18);
        hash = merge(hash, v1);
        hash = merge(hash, v2);
        hash = merge(hash, v3);
        hash = merge(hash, v4);
    }
    else
    {
        hash = kPrime5;
    }
    hash += m_total;
    const std::byte* p = m_pending.data();
    const std::byte* const end = p + m_pendingBytes;
    for (; end - p >= 8; p += 8)
        hash = std::rotl(hash ^ round(0u, read64(p)), 27) * kPrime1 + kPrime4;
    if (end - p >= 4)
    {
        hash = std::rotl(hash ^ (read32(p) * kPrime1), 23) * kPrime2 + kPrime3;
        p += 4;
    }
    for (; p < end; ++p)
        hash = std::rotl(hash ^ (std::uint64_t{std::to_integer<unsigned char>(*p)} * kPrime5), 11) * kPrime1;
    hash ^= hash >> 33;
    hash *= kPrime2;
    hash ^= hash >> 29;
    hash *= kPrime3;
    hash ^= hash >> 32;
    return hash;
}

std::uint64_t snapshotChecksum(std::span<const std::byte> bytes) noexcept
{
    SnapshotChecksum checksum;
    checksum.update(bytes);
    return checksum.digest();
}

std::uint64_t combineSnapshotBlockDigests(std::span<const std::uint64_t> digests) noexcept
{
    SnapshotChecksum combined;
    for (const std::uint64_t digest : digests)
    {
        std::array<std::byte, 8u> bytes{};
        for (std::size_t i = 0u; i < bytes.size(); ++i)
            bytes[i] = static_cast<std::byte>(digest >> (8u * i));
        combined.update(bytes);
    }
    return combined.digest();
}

void SnapshotBlockChecksum::update(std::span<const std::byte> bytes)
{
    while (!bytes.empty())
    {
        const std::size_t taken =
            static_cast<std::size_t>((std::min)(std::uint64_t{bytes.size()}, kSnapshotHashBlockBytes - m_inBlock));
        m_block.update(bytes.first(taken));
        m_inBlock += taken;
        bytes = bytes.subspan(taken);
        if (m_inBlock == kSnapshotHashBlockBytes)
        {
            m_digests.push_back(m_block.digest());
            m_block = SnapshotChecksum{};
            m_inBlock = 0u;
        }
    }
}

std::uint64_t SnapshotBlockChecksum::digest() const
{
    if (m_inBlock == 0u)
        return combineSnapshotBlockDigests(m_digests);
    std::vector<std::uint64_t> digests = m_digests;
    digests.push_back(m_block.digest());
    return combineSnapshotBlockDigests(digests);
}

std::array<std::byte, kSnapshotHeaderBytes> encodeSnapshotHeader(const SnapshotHeader& header) noexcept
{
    Header bytes{};
    std::copy(kSnapshotMagic.begin(), kSnapshotMagic.end(), bytes.begin());
    put(bytes, kSnapshotVersionOffset, 4u, kSnapshotVersion);
    put(bytes, 12u, 4u, kSnapshotHeaderBytes);
    put(bytes, 16u, 8u, header.fileBytes);
    put(bytes, 24u, 8u, kSnapshotHeaderBytes);
    put(bytes, 32u, 8u, header.metadataBytes);
    put(bytes, 40u, 8u, snapshotSampleOffset(header));
    put(bytes, 48u, 8u, header.sampleBytes);
    put(bytes, 56u, 8u, snapshotValidityOffset(header));
    put(bytes, 64u, 8u, header.validityBytes);
    const RawFrameDescriptor& d = header.descriptor;
    put(bytes, 72u, 4u, d.extent().width());
    put(bytes, 76u, 4u, d.extent().height());
    put(bytes, 80u, 4u, d.extent().channelCount());
    put(bytes, 84u, 1u, d.sampleFormat().sizeBytes);
    put(bytes, 85u, 1u, static_cast<std::uint8_t>(d.sampleFormat().sampleClass));
    put(bytes, 86u, 1u,
        static_cast<std::uint8_t>(isRawByteOrderMeaningful(d.sampleFormat()) ? ByteOrder::Little : ByteOrder::Native));
    put(bytes, 87u, 1u, static_cast<std::uint8_t>(d.channelSchema()));
    put(bytes, 88u, 1u, static_cast<std::uint8_t>(d.colorSpace()));
    put(bytes, 89u, 1u, static_cast<std::uint8_t>(d.numericDomain()));
    put(bytes, 90u, 1u, static_cast<std::uint8_t>(d.coordinateOrigin()));
    put(bytes, 91u, 1u, static_cast<std::uint8_t>(d.cfa().pattern()));
    put(bytes, 92u, 1u, d.cfa().originX());
    put(bytes, 93u, 1u, d.cfa().originY());
    put(bytes, 96u, 8u, std::bit_cast<std::uint64_t>(d.pixelAspectRatio()));
    put(bytes, 104u, 4u, header.layout.samplesPerPixel());
    put(bytes, 108u, 4u, header.layout.planeCount());
    put(bytes, 112u, 8u, header.layout.rowStrideBytes());
    put(bytes, 120u, 8u, header.layout.planeStrideBytes());
    if (d.cfa().hasFilterTable())
        for (std::size_t i = 0u; i < 36u; ++i)
            put(bytes, 128u + i, 1u, static_cast<std::uint8_t>(d.cfa().filterTable().entries()[i]));
    put(bytes, 164u, 4u, header.validityPlanes);
    put(bytes, 168u, 8u, header.metadataChecksum);
    put(bytes, 176u, 8u, header.sampleChecksum);
    put(bytes, 184u, 8u, header.validityChecksum);
    put(bytes, 248u, 8u, snapshotChecksum(std::span<const std::byte>(bytes.data(), 248u)));
    return bytes;
}

Result<SnapshotHeader> parseSnapshotHeader(std::span<const std::byte, kSnapshotHeaderBytes> h, std::uint64_t fileBytes,
                                           const EngineLimits& limits)
{
    const auto version = static_cast<std::uint32_t>(get(h, kSnapshotVersionOffset, 4u));
    if (!std::equal(kSnapshotMagic.begin(), kSnapshotMagic.end(), h.begin()) || version < 1u ||
        version > kSnapshotVersion)
        return damaged("it is not a snapshot of a version this build reads");
    if (get(h, 12u, 4u) != kSnapshotHeaderBytes)
        return damaged("its stated size is not 256 bytes");
    if (get(h, 248u, 8u) != snapshotChecksum(h.first<248u>()))
        return damaged("its checksum does not match");
    if (get(h, 94u, 2u) != 0u ||
        std::any_of(h.begin() + 192, h.begin() + 248, [](std::byte b) { return b != std::byte{0}; }))
        return damaged("a reserved byte is set");
    if (get(h, 16u, 8u) != fileBytes)
        return damaged("the file is not the size the header states");

    const auto extent =
        ImageExtent::create(static_cast<std::uint32_t>(get(h, 72u, 4u)), static_cast<std::uint32_t>(get(h, 76u, 4u)),
                            static_cast<std::uint32_t>(get(h, 80u, 4u)), limits);
    if (!extent)
        return beyondLimits("extent");
    const RawSampleFormat format{static_cast<std::uint8_t>(get(h, 84u, 1u)),
                                 static_cast<RawSampleClass>(get(h, 85u, 1u))};
    if (!isRawSampleFormat(format) || !residentSampleTypeOf(format).has_value())
        return damaged("its samples are not one of the eight resident types");
    const auto order = static_cast<ByteOrder>(get(h, 86u, 1u));
    if (order != (isRawByteOrderMeaningful(format) ? ByteOrder::Little : ByteOrder::Native))
        return damaged("its byte order is not the format's");
    const auto pattern = static_cast<CfaPattern>(get(h, 91u, 1u));
    const auto phaseX = static_cast<std::uint32_t>(get(h, 92u, 1u));
    const auto phaseY = static_cast<std::uint32_t>(get(h, 93u, 1u));
    std::optional<CfaDescriptor> cfa;
    if (pattern == CfaPattern::XTrans)
    {
        CfaColorEntries entries{};
        for (std::size_t i = 0u; i < 36u; ++i)
            entries[i] = static_cast<CfaColor>(get(h, 128u + i, 1u));
        cfa = CfaDescriptor::create(entries, phaseX, phaseY);
    }
    else
    {
        if (std::any_of(h.begin() + 128, h.begin() + 164, [](std::byte b) { return b != std::byte{0}; }))
            return damaged("a filter table is stated for a pattern that has none");
        cfa = CfaDescriptor::create(pattern, phaseX, phaseY);
    }
    if (!cfa)
        return damaged("its filter array is not one");
    const auto descriptor =
        RawFrameDescriptor::create({.extent = *extent,
                                    .sampleFormat = format,
                                    .byteOrder = ByteOrder::Native,
                                    .channelSchema = static_cast<ChannelSchema>(get(h, 87u, 1u)),
                                    .colorSpace = static_cast<ColorSpace>(get(h, 88u, 1u)),
                                    .numericDomain = static_cast<NumericDomain>(get(h, 89u, 1u)),
                                    .cfa = *cfa,
                                    .coordinateOrigin = static_cast<CoordinateOrigin>(get(h, 90u, 1u)),
                                    .pixelAspectRatio = std::bit_cast<double>(get(h, 96u, 8u))});
    if (!descriptor)
        return damaged("its description is not one");
    const auto samplesPerPixel = static_cast<std::uint32_t>(get(h, 104u, 4u));
    const auto planeCount = static_cast<std::uint32_t>(get(h, 108u, 4u));
    const auto layout = ImageLayout::create({*extent, format.sizeBytes, samplesPerPixel, planeCount, 1u}, limits);
    if (!layout)
        return beyondLimits("layout");
    if (layout->rowStrideBytes() != get(h, 112u, 8u) || layout->planeStrideBytes() != get(h, 120u, 8u))
        return damaged("its strides are not the packed layout's");

    const auto validityPlanes = static_cast<std::uint32_t>(get(h, 164u, 4u));
    if (validityPlanes != 0u && validityPlanes != 1u && validityPlanes != extent->channelCount())
        return damaged("its mask has neither one plane nor one per channel");
    const std::uint64_t maskBytes =
        std::uint64_t{validityPlanes} * extent->height() * packedMaskRowBytes(extent->width());

    SnapshotHeader header{.version = version,
                          .fileBytes = fileBytes,
                          .metadataBytes = get(h, 32u, 8u),
                          .sampleBytes = get(h, 48u, 8u),
                          .validityBytes = get(h, 64u, 8u),
                          .descriptor = *descriptor,
                          .layout = *layout,
                          .validityPlanes = validityPlanes,
                          .metadataChecksum = get(h, 168u, 8u),
                          .sampleChecksum = get(h, 176u, 8u),
                          .validityChecksum = get(h, 184u, 8u)};
    if (get(h, 24u, 8u) != kSnapshotHeaderBytes || header.metadataBytes == 0u ||
        header.metadataBytes > fileBytes - kSnapshotHeaderBytes)
        return damaged("its metadata section is not where the header says");
    if (header.sampleBytes != layout->totalBytes() || get(h, 40u, 8u) != snapshotSampleOffset(header))
        return damaged("its sample section is not the layout's");
    if (header.validityBytes != maskBytes || get(h, 56u, 8u) != snapshotValidityOffset(header) ||
        (validityPlanes == 0u && header.validityChecksum != 0u))
        return damaged("its validity section is not the mask's");
    // Every length is bounded by the file's real size, so the sum cannot overflow.
    if (snapshotSampleOffset(header) > fileBytes || header.sampleBytes > fileBytes - snapshotSampleOffset(header) ||
        snapshotSampleOffset(header) + header.sampleBytes + header.validityBytes != fileBytes)
        return damaged("its sections do not end where the file does");
    return header;
}

std::uint64_t encodedSnapshotMetadataBytes(const ArtifactIdentity& identity, const ImageMetadata& metadata) noexcept
{
    MeasuringSink sink;
    walkMetadata(sink, identity, metadata);
    return sink.bytes();
}

void encodeSnapshotMetadata(const ArtifactIdentity& identity, const ImageMetadata& metadata,
                            std::span<std::byte> destination) noexcept
{
    BufferSink sink(destination);
    walkMetadata(sink, identity, metadata);
}

Result<DecodedSnapshotMetadata> decodeSnapshotMetadata(std::span<const std::byte> section, std::uint32_t version)
{
    Cursor cursor(section);
    std::string artifactId = cursor.text();
    std::string runId = cursor.text();

    AcquisitionMetadataRequest acquisition;
    acquisition.instrumentName = cursor.text();
    acquisition.telescopeName = cursor.text();
    acquisition.observerName = cursor.text();
    acquisition.objectName = cursor.text();
    acquisition.filterName = cursor.text();
    acquisition.exposureSeconds = cursor.optionalF64();
    acquisition.sensorTemperatureCelsius = cursor.optionalF64();
    acquisition.gainElectronsPerAdu = cursor.optionalF64();
    if (cursor.flag())
    {
        const std::uint32_t horizontal = cursor.u32();
        const std::uint32_t vertical = cursor.u32();
        acquisition.binning = PixelBinning{horizontal, vertical};
    }
    if (cursor.flag())
        acquisition.exposureStart = UtcInstant{cursor.i64()};
    if (cursor.flag())
    {
        acquisition.rawCamera = takeCamera(cursor);
        if (!acquisition.rawCamera.has_value())
            return undecodable("the camera record");
    }

    ImageSemanticsRequest semantics;
    semantics.transferFunction = static_cast<TransferFunction>(cursor.u8());
    semantics.gammaExponent = cursor.optionalF64();
    const auto steps = static_cast<std::uint32_t>(cursor.unsignedOf(2u));
    if (steps >= (1u << 9u))
        return undecodable("an unknown processing step");
    ProcessingState& state = semantics.processingState;
    std::array<bool*, 9u> flags{&state.biasSubtracted,    &state.darkSubtracted, &state.flatFielded,
                                &state.cosmeticCorrected, &state.demosaiced,     &state.backgroundExtracted,
                                &state.registered,        &state.normalized,     &state.colorCalibrated};
    for (std::size_t i = 0u; i < flags.size(); ++i)
        *flags[i] = (steps >> i & 1u) != 0u;
    ValidityDescriptorRequest validity;
    validity.rule = static_cast<ValidityRule>(cursor.u8());
    validity.lowestValid = cursor.optionalF64();
    validity.highestValid = cursor.optionalF64();
    if (cursor.failed())
        return undecodable("the meaning");
    const std::optional<ValidityDescriptor> rule = ValidityDescriptor::create(validity);
    if (!rule.has_value())
        return undecodable("the validity rule");
    semantics.validity = *rule;

    std::optional<AstrometricSolutionRequest> solution;
    if (cursor.flag())
    {
        AstrometricSolutionRequest request{};
        request.referencePoint.rightAscensionDegrees = cursor.f64();
        request.referencePoint.declinationDegrees = cursor.f64();
        request.referencePixelX = cursor.f64();
        request.referencePixelY = cursor.f64();
        request.pixelScaleArcsecPerPixel = cursor.f64();
        request.orientationDegrees = cursor.f64();
        request.mirrored = cursor.flag();
        request.hasDistortion = cursor.flag();
        solution = request;
    }

    const auto sourceKind = static_cast<ProvenanceSourceKind>(cursor.u8());
    std::string sourceName = cursor.text();
    const std::uint32_t entryCount = cursor.count(8u);
    std::vector<std::pair<std::string, std::string>> entries;
    entries.reserve(entryCount);
    for (std::uint32_t index = 0u; index < entryCount && !cursor.failed(); ++index)
    {
        std::string operation = cursor.text();
        std::string detail = cursor.text();
        entries.emplace_back(std::move(operation), std::move(detail));
    }

    SourceEncoding encoding;
    if (cursor.flag())
    {
        const std::uint8_t width = cursor.u8();
        encoding.sampleFormat = RawSampleFormat{width, static_cast<RawSampleClass>(cursor.u8())};
    }
    if (cursor.flag())
        encoding.byteOrder = static_cast<ByteOrder>(cursor.u8());
    encoding.scale = cursor.optionalF64();
    encoding.zeroOffset = cursor.optionalF64();
    std::optional<std::vector<CodecCard>> cards = takeCards(cursor);
    if (!cards.has_value())
        return undecodable("a preserved card");

    std::optional<OutputIntentRequest> intent;
    if (cursor.flag())
    {
        OutputIntentRequest request{.sampleType = static_cast<SampleType>(cursor.u8())};
        request.byteOrder = static_cast<ByteOrder>(cursor.u8());
        request.scaling = static_cast<OutputScaling>(cursor.u8());
        request.scale = cursor.optionalF64();
        request.zeroOffset = cursor.optionalF64();
        request.rounding = static_cast<OutputRounding>(cursor.u8());
        request.compression = static_cast<OutputCompression>(cursor.u8());
        // Version 1 named the compressions up to JPEG; a later one in a version 1 file is not what version 1 wrote.
        if (version < 2u && request.compression > OutputCompression::Jpeg)
            return undecodable("a compression version 1 does not name");
        if (cursor.flag())
            request.compressionLevel = cursor.u32();
        if (version >= 2u)
        {
            if (cursor.flag())
                request.byteShuffle = cursor.flag();
            if (cursor.flag())
                request.compressionBlockBytes = static_cast<std::uint64_t>(cursor.i64());
            request.checksum = static_cast<OutputChecksum>(cursor.u8());
        }
        request.preserveSourceCards = cursor.flag();
        std::optional<std::vector<CodecCard>> intentCards = takeCards(cursor);
        if (!intentCards.has_value())
            return undecodable("an output card");
        request.cards = std::move(*intentCards);
        intent = std::move(request);
    }
    if (cursor.failed())
        return undecodable("the section ends early");
    if (!cursor.exhausted())
        return undecodable("bytes follow the last field");

    Result<ArtifactIdentity> identity = ArtifactIdentity::create(std::move(artifactId), std::move(runId));
    if (!identity.hasValue())
        return undecodable("the artifact identity");
    Result<AcquisitionMetadata> acquired = AcquisitionMetadata::create(acquisition);
    if (!acquired.hasValue())
        return undecodable("the acquisition record");
    Result<ImageSemantics> meaning = ImageSemantics::create(semantics);
    if (!meaning.hasValue())
        return undecodable("the meaning");
    AstrometricSolution astrometry;
    if (solution.has_value())
    {
        Result<AstrometricSolution> solved = AstrometricSolution::create(*solution);
        if (!solved.hasValue())
            return undecodable("the astrometric solution");
        astrometry = *solved;
    }
    Provenance provenance;
    if (sourceKind != ProvenanceSourceKind::Unknown || !sourceName.empty())
    {
        Result<Provenance> created = Provenance::create(sourceKind, std::move(sourceName));
        if (!created.hasValue())
            return undecodable("the lineage's source");
        provenance = std::move(*created);
    }
    for (auto& [operation, detail] : entries)
    {
        Result<ProvenanceEntry> entry = ProvenanceEntry::create(std::move(operation), std::move(detail));
        if (!entry.hasValue())
            return undecodable("a lineage entry");
        Result<Provenance> appended = provenance.appended(*entry);
        if (!appended.hasValue())
            return undecodable("the lineage");
        provenance = std::move(*appended);
    }
    Result<CodecMetadata> codec = CodecMetadata::create(encoding, std::move(*cards));
    if (!codec.hasValue())
        return undecodable("the preserved cards");
    std::optional<OutputIntent> output;
    if (intent.has_value())
    {
        Result<OutputIntent> made = OutputIntent::create(*intent);
        if (!made.hasValue())
            return undecodable("the output intent");
        output = std::move(*made);
    }
    return DecodedSnapshotMetadata{.identity = std::move(*identity),
                                   .metadata = ImageMetadata::create(std::move(*acquired), std::move(*meaning),
                                                                     astrometry, std::move(provenance),
                                                                     std::move(*codec), std::move(output))};
}

} // namespace an
