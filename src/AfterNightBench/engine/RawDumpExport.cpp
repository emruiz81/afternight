// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "RawDumpExport.h"

#include "FixtureBridge.h"

#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/FitsCodec.h>
#include <AfterNightEngine/IO/ImageCodec.h>
#include <AfterNightEngine/IO/JpegCodec.h>
#include <AfterNightEngine/IO/PngCodec.h>
#include <AfterNightEngine/IO/RawCodec.h>
#include <AfterNightEngine/IO/TiffCodec.h>
#include <AfterNightEngine/IO/XisfCodec.h>
#include <AfterNightEngine/Image/CfaPattern.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/RawFrame.h>
#include <AfterNightEngine/Image/RawSampleFormat.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>

namespace an::bench::engine
{
namespace
{

/// The schema's name for a decoded sample format, or nothing for one it does not name.
std::optional<SampleType> sampleTypeOf(const an::RawSampleFormat& format) noexcept
{
    switch (format.sampleClass)
    {
    case an::RawSampleClass::UnsignedInteger:
        switch (format.sizeBytes)
        {
        case 1u:
            return SampleType::UInt8;
        case 2u:
            return SampleType::UInt16;
        case 4u:
            return SampleType::UInt32;
        case 8u:
            return SampleType::UInt64;
        default:
            return std::nullopt;
        }
    case an::RawSampleClass::SignedInteger:
        switch (format.sizeBytes)
        {
        case 1u:
            return SampleType::Int8;
        case 2u:
            return SampleType::Int16;
        case 4u:
            return SampleType::Int32;
        case 8u:
            return SampleType::Int64;
        default:
            return std::nullopt;
        }
    case an::RawSampleClass::FloatingPoint:
        switch (format.sizeBytes)
        {
        case 4u:
            return SampleType::Float32;
        case 8u:
            return SampleType::Float64;
        default:
            return std::nullopt;
        }
    case an::RawSampleClass::Opaque:
        return std::nullopt;
    }
    return std::nullopt;
}

/// The letter the schema spells a filter colour with.
char colorLetter(an::CfaColor color) noexcept
{
    switch (color)
    {
    case an::CfaColor::Red:
        return 'R';
    case an::CfaColor::Green:
        return 'G';
    case an::CfaColor::Blue:
        return 'B';
    }
    return '?';
}

/// The mosaic as the schema names it: the two-by-two Bayer pattern seen from the frame's first pixel.
///
/// The schema names a pattern by its four colours at the frame's origin, so a phase the engine states
/// separately is folded into the name rather than lost.
bool describeMosaic(const an::CfaDescriptor& cfa, std::string& pattern, std::string& error)
{
    pattern.clear();
    if (!cfa.isMosaiced())
    {
        return true;
    }
    if (an::cfaPatternBlockSize(cfa.pattern()) != 2u)
    {
        error = std::string("the frame's ") + an::cfaPatternName(cfa.pattern()) +
                " filter array is not a two-by-two Bayer pattern, the only kind the raw-dump schema names";
        return false;
    }
    pattern = {colorLetter(cfa.colorAt(0u, 0u)), colorLetter(cfa.colorAt(1u, 0u)), colorLetter(cfa.colorAt(0u, 1u)),
               colorLetter(cfa.colorAt(1u, 1u))};
    if (pattern != "RGGB" && pattern != "BGGR" && pattern != "GRBG" && pattern != "GBRG")
    {
        error = "the frame's filter array reads " + pattern + " from its first pixel, which is not a Bayer pattern";
        return false;
    }
    return true;
}

/// Reverse every sample's bytes in place.
void reverseSamples(std::vector<std::uint8_t>& payload, std::size_t sampleBytes) noexcept
{
    for (std::size_t offset = 0u; offset + sampleBytes <= payload.size(); offset += sampleBytes)
    {
        std::reverse(payload.begin() + static_cast<std::ptrdiff_t>(offset),
                     payload.begin() + static_cast<std::ptrdiff_t>(offset + sampleBytes));
    }
}

/// The codecs the product reads files with, in one registry.
an::Result<std::shared_ptr<const an::CodecRegistry>> exportCodecRegistry()
{
    const std::array<an::Result<std::shared_ptr<const an::ImageCodec>>, 6u> adapters{
        an::createFitsCodec(), an::createXisfCodec(), an::createTiffCodec(),
        an::createPngCodec(),  an::createJpegCodec(), an::createRawCodec()};
    std::vector<std::shared_ptr<const an::ImageCodec>> codecs;
    codecs.reserve(adapters.size());
    for (const an::Result<std::shared_ptr<const an::ImageCodec>>& adapter : adapters)
    {
        if (!adapter.hasValue())
        {
            return adapter.status();
        }
        codecs.push_back(*adapter);
    }
    return an::CodecRegistry::create(codecs);
}

} // namespace

const char* rawDumpFormatName(an::CodecFormat format) noexcept
{
    switch (format)
    {
    case an::CodecFormat::Fits:
        return "FITS";
    case an::CodecFormat::Xisf:
        return "XISF";
    case an::CodecFormat::Tiff:
        return "TIFF";
    case an::CodecFormat::Png:
        return "PNG";
    case an::CodecFormat::Jpeg:
        return "JPEG";
    case an::CodecFormat::Raw:
        return "RAW";
    case an::CodecFormat::Unknown:
    case an::CodecFormat::Dump:
        return "unknown";
    }
    return "unknown";
}

bool buildRawDump(const an::CodecDecodeResult& decoded, const std::string& sourcePath, const std::string& sourceFormat,
                  RawDump& dump, std::vector<std::uint8_t>& payload, std::string& error)
{
    const an::RawFrame& frame = decoded.frame;
    const std::optional<an::RawFrameDescriptor> frameDescriptor = frame.descriptor();
    const std::optional<an::ImageLayout> layout = frame.layout();
    if (!frame.valid() || !frameDescriptor.has_value() || !layout.has_value())
    {
        error = "there is no image to export";
        return false;
    }

    SampleDescriptor descriptor;
    const std::optional<SampleType> sampleType = sampleTypeOf(frameDescriptor->sampleFormat());
    if (!sampleType.has_value())
    {
        error = "the image has no representation this schema can name";
        return false;
    }
    descriptor.width = frameDescriptor->extent().width();
    descriptor.height = frameDescriptor->extent().height();
    descriptor.channels = frameDescriptor->extent().channelCount();
    descriptor.sampleType = *sampleType;
    descriptor.layout = Layout::Planar;
    descriptor.byteOrder = ByteOrder::Native;
    if (!describeMosaic(frameDescriptor->cfa(), descriptor.cfaPattern, error))
    {
        return false;
    }

    // Planar, top row first and tightly packed, whatever arrangement and row order the decoder chose:
    // one plane per channel, or one plane holding every channel of a pixel side by side.
    const std::size_t sampleBytes = descriptor.sampleSizeBytes();
    const std::uint32_t width = descriptor.width;
    const std::uint32_t height = descriptor.height;
    const std::uint32_t channels = descriptor.channels;
    const bool interleaved = layout->planeCount() == 1u && channels > 1u;
    const bool bottomUp = frameDescriptor->coordinateOrigin() == an::CoordinateOrigin::BottomLeft;
    const std::size_t rowBytes = static_cast<std::size_t>(width) * sampleBytes;
    payload.assign(static_cast<std::size_t>(descriptor.totalBytes()), 0u);
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        std::uint8_t* const plane = payload.data() + static_cast<std::size_t>(channel) * height * rowBytes;
        for (std::uint32_t row = 0u; row < height; ++row)
        {
            const std::optional<std::span<const std::byte>> source = frame.row(interleaved ? 0u : channel, row);
            const std::size_t needed = interleaved ? rowBytes * channels : rowBytes;
            if (!source.has_value() || source->size() < needed)
            {
                error = "the decoded frame has no row " + std::to_string(row) + " of plane " + std::to_string(channel);
                return false;
            }
            const std::uint32_t y = bottomUp ? height - 1u - row : row;
            std::uint8_t* const target = plane + static_cast<std::size_t>(y) * rowBytes;
            if (interleaved)
            {
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    std::memcpy(target + static_cast<std::size_t>(x) * sampleBytes,
                                source->data() + (static_cast<std::size_t>(x) * channels + channel) * sampleBytes,
                                sampleBytes);
                }
            }
            else
            {
                std::memcpy(target, source->data(), rowBytes);
            }
        }
    }

    // In the host's order first, then the format's canonical little-endian order.
    if (!frameDescriptor->isHostOrder())
    {
        reverseSamples(payload, sampleBytes);
    }
    if constexpr (std::endian::native == std::endian::big)
    {
        reverseSamples(payload, sampleBytes);
    }
    else if constexpr (std::endian::native != std::endian::little)
    {
        error = "the host uses a mixed byte order the raw-dump format cannot represent";
        return false;
    }
    descriptor.byteOrder = ByteOrder::Little;

    dump = RawDump{};
    dump.descriptor = descriptor;
    dump.sourcePath = sourcePath;
    dump.sourceFormat = sourceFormat;
    dump.exporterVersion = kRawDumpExporterVersion;
    for (const an::CodecCard& card : decoded.metadata.codec().cards())
    {
        if (card.keyword().empty())
        {
            continue;
        }
        dump.cards.push_back(MetadataCard{card.keyword(), card.value(), card.comment()});
    }
    return true;
}

bool exportRawDump(const std::string& sourcePath, const std::string& descriptorPath, std::string& error)
{
    an::Result<std::shared_ptr<const an::CodecRegistry>> registry = exportCodecRegistry();
    if (!registry.hasValue())
    {
        error = describeEngineFailure("the codecs could not be built", registry.status());
        return false;
    }
    an::EngineContextOptions options;
    options.codecRegistry = *registry;
    an::Result<an::EngineContext> engine = an::EngineContext::create(options);
    if (!engine.hasValue())
    {
        error = describeEngineFailure("the engine context could not be built", engine.status());
        return false;
    }
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, an::ExecutionOptions{});
    if (!execution.hasValue())
    {
        error = describeEngineFailure("the export could not be started", execution.status());
        return false;
    }

    const an::CodecFormat format = an::codecFormatFromPath(sourcePath);
    an::Result<an::CodecDecodeResult> decoded =
        an::decodeImage(*engine, *execution, an::CodecReadRequest{.path = sourcePath, .format = format});
    if (!decoded.hasValue())
    {
        error = describeEngineFailure("cannot decode '" + sourcePath + "'", decoded.status());
        return false;
    }

    RawDump dump;
    std::vector<std::uint8_t> payload;
    if (!buildRawDump(*decoded, sourcePath, rawDumpFormatName(format), dump, payload, error))
    {
        error = "cannot export '" + sourcePath + "': " + error;
        return false;
    }
    dump.payloadFile = std::filesystem::path(descriptorPath).stem().string() + ".anraw";
    return writeRawDump(descriptorPath, dump, payload, error);
}

} // namespace an::bench::engine
