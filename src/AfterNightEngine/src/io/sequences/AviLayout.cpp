// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - planning an uncompressed AVI container and encoding its headers and indexes.
//
// The header's shape is fixed up to the stream format, whose size depends only on whether a grey
// frame carries its 256-entry grey palette, so every field before the master index sits at a constant
// offset. The master index's room is the one variable part, and the header's size and the first
// section's capacity each depend on the other, so a derived room is recomputed until it stops growing.
// Every field is written little-endian byte by byte, which makes the encoding independent of the
// host's byte order. A value written through `clampTo32` saturates at its 32-bit field's maximum
// rather than wrapping.

#include "io/sequences/AviLayout.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <string>
#include <utility>

namespace an::avi_detail
{
namespace
{
// The fixed chunk sizes of the header.
constexpr std::uint64_t kListHeaderBytes = 12u;
constexpr std::uint64_t kBitmapHeaderBytes = 40u;
constexpr std::uint64_t kPaletteBytes = 256u * 4u;
constexpr std::uint64_t kMasterIndexHeaderBytes = 24u;
constexpr std::uint64_t kMasterIndexEntryBytes = 16u;
constexpr std::uint64_t kExtendedHeaderBytes = 248u;
constexpr std::uint64_t kOdmlListBytes = kListHeaderBytes + 8u + kExtendedHeaderBytes;

// Where the fixed-position fields sit.
constexpr std::size_t kRiffSizeOffset = 4u;
constexpr std::size_t kHdrlSizeOffset = 16u;
constexpr std::size_t kMainHeaderOffset = 32u;
constexpr std::size_t kStrlSizeOffset = 92u;
constexpr std::size_t kStreamHeaderOffset = 108u;
constexpr std::size_t kStreamFormatOffset = 172u;

constexpr std::uint32_t kHasIndexFlag = 0x10u;
constexpr std::uint32_t kKeyframeFlag = 0x10u;
constexpr std::uint64_t kMaximumContainerFrames = (std::numeric_limits<std::uint32_t>::max)();

void putFourcc(std::span<std::byte> target, std::size_t offset, const char* code) noexcept
{
    std::memcpy(target.data() + offset, code, 4u);
}

void putLe16(std::span<std::byte> target, std::size_t offset, std::uint16_t value) noexcept
{
    target[offset] = static_cast<std::byte>(value & 0xffu);
    target[offset + 1u] = static_cast<std::byte>(value >> 8u);
}

void putLe32(std::span<std::byte> target, std::size_t offset, std::uint32_t value) noexcept
{
    for (std::size_t i = 0u; i < 4u; ++i)
        target[offset + i] = static_cast<std::byte>((value >> (8u * i)) & 0xffu);
}

void putLe64(std::span<std::byte> target, std::size_t offset, std::uint64_t value) noexcept
{
    for (std::size_t i = 0u; i < 8u; ++i)
        target[offset + i] = static_cast<std::byte>((value >> (8u * i)) & 0xffu);
}

std::uint32_t clampTo32(std::uint64_t value) noexcept
{
    return static_cast<std::uint32_t>((std::min)(value, std::uint64_t{(std::numeric_limits<std::uint32_t>::max)()}));
}

std::uint64_t streamFormatBytes(const Layout& layout) noexcept
{
    return kBitmapHeaderBytes + (layout.bytesPerPixel == 1u ? kPaletteBytes : 0u);
}

std::uint64_t masterIndexOffset(const Layout& layout) noexcept
{
    return kStreamFormatOffset + streamFormatBytes(layout);
}

std::uint64_t odmlOffset(const Layout& layout) noexcept
{
    return masterIndexOffset(layout) + 8u + kMasterIndexHeaderBytes + kMasterIndexEntryBytes * layout.riffCapacity;
}

std::uint64_t headerBytesFor(const Layout& layout) noexcept
{
    return odmlOffset(layout) + kOdmlListBytes + kListHeaderBytes;
}

Status unsupported(const char* message, const char* parameter)
{
    return aviFailure(ErrorCode::Unsupported, SequenceFailure::UnsupportedEncoding, message, parameter, {});
}

Status descriptorArgument(const char* message, const char* parameter)
{
    return aviFailure(ErrorCode::InvalidArgument, SequenceFailure::InvalidDescriptor, message, parameter, {});
}

Status limit(const char* message, const char* parameter, std::uint64_t requested, std::uint64_t available)
{
    return aviFailure(ErrorCode::LimitExceeded, SequenceFailure::UnsupportedEncoding, message, parameter, {},
                      {StatusContextField::ofCount(ContextKey::Requested, requested),
                       StatusContextField::ofCount(ContextKey::Limit, available)});
}

/// How many frames the first and the later sections hold under the layout's header, or zero when one
/// frame does not fit.
void sectionFrames(Layout& layout) noexcept
{
    const std::uint64_t chunk = chunkBytes(layout);
    const std::uint64_t firstFixed = layout.headerBytes + kStandardIndexHeaderBytes + kIdx1HeaderBytes;
    const std::uint64_t firstPerFrame = chunk + kStandardIndexEntryBytes + kIdx1EntryBytes;
    layout.firstSectionFrames = layout.riffBytes > firstFixed ? (layout.riffBytes - firstFixed) / firstPerFrame : 0u;
    const std::uint64_t laterFixed = kExtensionSectionHeaderBytes + kStandardIndexHeaderBytes;
    const std::uint64_t laterPerFrame = chunk + kStandardIndexEntryBytes;
    layout.laterSectionFrames = layout.riffBytes > laterFixed ? (layout.riffBytes - laterFixed) / laterPerFrame : 0u;
}

void containerFrames(Layout& layout) noexcept
{
    const std::uint64_t later = layout.laterSectionFrames * (layout.riffCapacity - 1u);
    layout.maximumFrames = (std::min)(layout.firstSectionFrames + later, kMaximumContainerFrames);
}

/// The sections a count of frames needs, at least one.
std::uint64_t sectionsFor(const Layout& layout, std::uint64_t frames) noexcept
{
    if (frames <= layout.firstSectionFrames)
        return 1u;
    const std::uint64_t rest = frames - layout.firstSectionFrames;
    return 1u + (rest + layout.laterSectionFrames - 1u) / layout.laterSectionFrames;
}
} // namespace

Result<Layout> planLayout(const SequenceDescriptor& descriptor, const AviSinkRequest& request)
{
    if (static_cast<std::uint8_t>(request.mosaicFrames) >
        static_cast<std::uint8_t>(AviMosaicPolicy::WriteSamplesAsMono))
        return argumentFailure("the mosaic policy is not one this build knows", "mosaicFrames");
    if (request.riffBytes < kAviMinimumRiffBytes || request.riffBytes > kAviDefaultRiffBytes)
        return argumentFailure("a RIFF section is bounded to between one mebibyte and one gibibyte", "riffBytes");
    if (request.riffCapacity > kAviMaximumRiffCapacity)
        return argumentFailure("the master index has room for at most 65536 sections", "riffCapacity");
    if (descriptor.format != SequenceFormat::Avi)
        return descriptorArgument("an AVI sink writes an AVI container", "format");
    if (!descriptor.fixedFrame)
        return descriptorArgument("an AVI container stores one frame description for every frame", "fixedFrame");
    if (descriptor.seekByIndex)
        return descriptorArgument("a sink cannot seek", "seekByIndex");
    if (descriptor.seekByTime)
        return descriptorArgument("a sink cannot seek", "seekByTime");
    if (const Status checked = validateSequenceDescriptor(descriptor); checked.failed())
        return checked;
    if (!descriptor.nominalFrameRate)
        return descriptorArgument("an AVI container presents frames at a stated rate", "nominalFrameRate");
    if (descriptor.timestampsRequired)
        return unsupported("an AVI container stores a frame rate rather than a time for each frame",
                           "timestampsRequired");
    if (descriptor.frameCount && *descriptor.frameCount > kMaximumContainerFrames)
        return limit("an AVI container counts at most UINT32_MAX frames", "frameCount", *descriptor.frameCount,
                     kMaximumContainerFrames);

    const RawFrameDescriptor& frame = *descriptor.fixedFrame;
    const RawSampleFormat format = frame.sampleFormat();
    if (format.sampleClass != RawSampleClass::UnsignedInteger || format.sizeBytes != 1u)
        return unsupported("an uncompressed AVI stores one unsigned byte per sample", "sampleFormat");
    if (frame.channelSchema() != ChannelSchema::Mono && frame.channelSchema() != ChannelSchema::Rgb)
        return unsupported("an uncompressed AVI stores grey and RGB frames", "channelSchema");
    if (frame.isMosaiced() && request.mosaicFrames == AviMosaicPolicy::Refuse)
        return unsupported("a device-independent bitmap cannot state a filter array", "cfa");
    if (frame.pixelAspectRatio() != 1.0)
        return unsupported("an uncompressed AVI stores square pixels", "pixelAspectRatio");
    if (frame.extent().width() > kAviMaximumFrameDimension || frame.extent().height() > kAviMaximumFrameDimension)
        return unsupported("an AVI stream header states a frame of at most 32767 pixels a side", "extent");

    Layout layout;
    layout.width = frame.extent().width();
    layout.height = frame.extent().height();
    layout.bytesPerPixel = frame.channelSchema() == ChannelSchema::Rgb ? 3u : 1u;
    const std::uint64_t rowSamples = std::uint64_t{layout.width} * layout.bytesPerPixel;
    layout.storedRowBytes = static_cast<std::uint32_t>((rowSamples + 3u) & ~std::uint64_t{3u});
    layout.frameBytes = std::uint64_t{layout.storedRowBytes} * layout.height;
    layout.rowsArriveBottomUp = frame.coordinateOrigin() == CoordinateOrigin::BottomLeft;
    layout.rateNumerator = descriptor.nominalFrameRate->numerator;
    layout.rateDenominator = descriptor.nominalFrameRate->denominator;
    layout.riffBytes = request.riffBytes;

    // The header's size depends on the master index's room and the first section's capacity depends on
    // the header, so a derived room is settled by recomputing until it stops growing.
    layout.riffCapacity = request.riffCapacity != 0u ? request.riffCapacity : 1u;
    for (;;)
    {
        layout.headerBytes = headerBytesFor(layout);
        sectionFrames(layout);
        if (layout.firstSectionFrames == 0u || layout.laterSectionFrames == 0u)
            return limit("one frame does not fit in a RIFF section of the stated size", "riffBytes", chunkBytes(layout),
                         request.riffBytes);
        if (request.riffCapacity != 0u)
            break;
        const std::uint64_t needed =
            descriptor.frameCount ? sectionsFor(layout, *descriptor.frameCount) : kAviDefaultRiffCapacity;
        if (needed > kAviMaximumRiffCapacity)
            return limit("the stated frames need more RIFF sections than a master index has room for", "frameCount",
                         *descriptor.frameCount, kAviMaximumRiffCapacity);
        if (needed <= layout.riffCapacity)
            break;
        layout.riffCapacity = static_cast<std::uint32_t>(needed);
    }
    containerFrames(layout);
    if (descriptor.frameCount && *descriptor.frameCount > layout.maximumFrames)
        return limit("the stated frames need more RIFF sections than the master index has room for", "frameCount",
                     *descriptor.frameCount, layout.maximumFrames);
    return layout;
}

void encodeHeader(std::span<std::byte> header, const Layout& layout, const HeaderCounts& counts) noexcept
{
    const auto formatBytes = streamFormatBytes(layout);
    const auto indexOffset = static_cast<std::size_t>(masterIndexOffset(layout));
    const auto odml = static_cast<std::size_t>(odmlOffset(layout));
    const auto movi = static_cast<std::size_t>(layout.headerBytes - kListHeaderBytes);
    const std::uint64_t firstChunks = counts.firstSectionFrames * chunkBytes(layout);
    const std::uint64_t firstIndex =
        counts.firstSectionFrames != 0u
            ? kStandardIndexHeaderBytes + kStandardIndexEntryBytes * counts.firstSectionFrames
            : 0u;
    const std::uint32_t suggestedBuffer = clampTo32(chunkBytes(layout));

    putFourcc(header, 0u, "RIFF");
    putLe32(header, kRiffSizeOffset, clampTo32((std::max)(counts.firstSectionBytes, layout.headerBytes) - 8u));
    putFourcc(header, 8u, "AVI ");
    putFourcc(header, 12u, "LIST");
    putLe32(header, kHdrlSizeOffset, static_cast<std::uint32_t>(movi - 20u));
    putFourcc(header, 20u, "hdrl");

    putFourcc(header, 24u, "avih");
    putLe32(header, 28u, 56u);
    const std::uint64_t microseconds =
        (std::uint64_t{1000000u} * layout.rateDenominator + layout.rateNumerator / 2u) / layout.rateNumerator;
    putLe32(header, kMainHeaderOffset, clampTo32(microseconds));
    const std::uint64_t bytesPerSecond =
        (chunkBytes(layout) * layout.rateNumerator + layout.rateDenominator - 1u) / layout.rateDenominator;
    putLe32(header, kMainHeaderOffset + 4u, clampTo32(bytesPerSecond));
    putLe32(header, kMainHeaderOffset + 8u, 0u);
    putLe32(header, kMainHeaderOffset + 12u, kHasIndexFlag);
    putLe32(header, kMainHeaderOffset + 16u, clampTo32(counts.firstSectionFrames));
    putLe32(header, kMainHeaderOffset + 20u, 0u);
    putLe32(header, kMainHeaderOffset + 24u, 1u);
    putLe32(header, kMainHeaderOffset + 28u, suggestedBuffer);
    putLe32(header, kMainHeaderOffset + 32u, layout.width);
    putLe32(header, kMainHeaderOffset + 36u, layout.height);
    std::memset(header.data() + kMainHeaderOffset + 40u, 0, 16u);

    putFourcc(header, 88u, "LIST");
    putLe32(header, kStrlSizeOffset, static_cast<std::uint32_t>(odml - 96u));
    putFourcc(header, 96u, "strl");

    putFourcc(header, 100u, "strh");
    putLe32(header, 104u, 56u);
    putFourcc(header, kStreamHeaderOffset, "vids");
    putFourcc(header, kStreamHeaderOffset + 4u, "DIB ");
    putLe32(header, kStreamHeaderOffset + 8u, 0u);
    putLe16(header, kStreamHeaderOffset + 12u, 0u);
    putLe16(header, kStreamHeaderOffset + 14u, 0u);
    putLe32(header, kStreamHeaderOffset + 16u, 0u);
    putLe32(header, kStreamHeaderOffset + 20u, layout.rateDenominator);
    putLe32(header, kStreamHeaderOffset + 24u, layout.rateNumerator);
    putLe32(header, kStreamHeaderOffset + 28u, 0u);
    putLe32(header, kStreamHeaderOffset + 32u, clampTo32(counts.totalFrames));
    putLe32(header, kStreamHeaderOffset + 36u, suggestedBuffer);
    putLe32(header, kStreamHeaderOffset + 40u, 0xFFFFFFFFu);
    putLe32(header, kStreamHeaderOffset + 44u, 0u);
    putLe16(header, kStreamHeaderOffset + 48u, 0u);
    putLe16(header, kStreamHeaderOffset + 50u, 0u);
    putLe16(header, kStreamHeaderOffset + 52u, static_cast<std::uint16_t>(layout.width));
    putLe16(header, kStreamHeaderOffset + 54u, static_cast<std::uint16_t>(layout.height));

    putFourcc(header, 164u, "strf");
    putLe32(header, 168u, static_cast<std::uint32_t>(formatBytes));
    putLe32(header, kStreamFormatOffset, 40u);
    putLe32(header, kStreamFormatOffset + 4u, layout.width);
    // A positive height states rows stored from the bottom of the picture, which every reader supports.
    putLe32(header, kStreamFormatOffset + 8u, layout.height);
    putLe16(header, kStreamFormatOffset + 12u, 1u);
    putLe16(header, kStreamFormatOffset + 14u, static_cast<std::uint16_t>(8u * layout.bytesPerPixel));
    putLe32(header, kStreamFormatOffset + 16u, 0u);
    putLe32(header, kStreamFormatOffset + 20u, static_cast<std::uint32_t>(layout.frameBytes));
    putLe32(header, kStreamFormatOffset + 24u, 0u);
    putLe32(header, kStreamFormatOffset + 28u, 0u);
    putLe32(header, kStreamFormatOffset + 32u, layout.bytesPerPixel == 1u ? 256u : 0u);
    putLe32(header, kStreamFormatOffset + 36u, 0u);
    if (layout.bytesPerPixel == 1u)
        for (std::size_t entry = 0u; entry < 256u; ++entry)
        {
            const std::size_t at = kStreamFormatOffset + kBitmapHeaderBytes + 4u * entry;
            header[at] = static_cast<std::byte>(entry);
            header[at + 1u] = static_cast<std::byte>(entry);
            header[at + 2u] = static_cast<std::byte>(entry);
            header[at + 3u] = std::byte{0};
        }

    putFourcc(header, indexOffset, "indx");
    putLe32(header, indexOffset + 4u,
            static_cast<std::uint32_t>(kMasterIndexHeaderBytes + kMasterIndexEntryBytes * layout.riffCapacity));
    putLe16(header, indexOffset + 8u, 4u);
    header[indexOffset + 10u] = std::byte{0};
    header[indexOffset + 11u] = std::byte{0};
    putLe32(header, indexOffset + 12u, counts.sectionsInUse);
    putFourcc(header, indexOffset + 16u, "00db");
    std::memset(header.data() + indexOffset + 20u, 0, 12u);

    putFourcc(header, odml, "LIST");
    putLe32(header, odml + 4u, static_cast<std::uint32_t>(kOdmlListBytes - 8u));
    putFourcc(header, odml + 8u, "odml");
    putFourcc(header, odml + 12u, "dmlh");
    putLe32(header, odml + 16u, static_cast<std::uint32_t>(kExtendedHeaderBytes));
    putLe32(header, odml + 20u, clampTo32(counts.totalFrames));
    std::memset(header.data() + odml + 24u, 0, kExtendedHeaderBytes - 4u);

    putFourcc(header, movi, "LIST");
    putLe32(header, movi + 4u, static_cast<std::uint32_t>(4u + firstChunks + firstIndex));
    putFourcc(header, movi + 8u, "movi");
}

void encodeMasterIndexEntry(std::span<std::byte> header, const Layout& layout, std::uint32_t section,
                            std::uint64_t indexOffset, std::uint32_t indexBytes, std::uint32_t frames) noexcept
{
    const auto at = static_cast<std::size_t>(masterIndexOffset(layout) + 8u + kMasterIndexHeaderBytes +
                                             kMasterIndexEntryBytes * section);
    putLe64(header, at, indexOffset);
    putLe32(header, at + 8u, indexBytes);
    putLe32(header, at + 12u, frames);
}

void encodeExtensionSectionHeader(std::span<std::byte> target, const Layout& layout, std::uint64_t frames) noexcept
{
    const std::uint64_t index = frames != 0u ? kStandardIndexHeaderBytes + kStandardIndexEntryBytes * frames : 0u;
    const std::uint64_t sectionBytes = kExtensionSectionHeaderBytes + frames * chunkBytes(layout) + index;
    putFourcc(target, 0u, "RIFF");
    putLe32(target, 4u, static_cast<std::uint32_t>(sectionBytes - 8u));
    putFourcc(target, 8u, "AVIX");
    putFourcc(target, 12u, "LIST");
    putLe32(target, 16u, static_cast<std::uint32_t>(sectionBytes - 20u));
    putFourcc(target, 20u, "movi");
}

void encodeChunkHeader(std::span<std::byte> target, const Layout& layout) noexcept
{
    putFourcc(target, 0u, "00db");
    putLe32(target, 4u, static_cast<std::uint32_t>(layout.frameBytes));
}

void encodeStandardIndexHeader(std::span<std::byte> target, std::uint64_t frames, std::uint64_t moviOffset) noexcept
{
    putFourcc(target, 0u, "ix00");
    putLe32(target, 4u, static_cast<std::uint32_t>(24u + kStandardIndexEntryBytes * frames));
    putLe16(target, 8u, 2u);
    target[10u] = std::byte{0};
    target[11u] = std::byte{1};
    putLe32(target, 12u, static_cast<std::uint32_t>(frames));
    putFourcc(target, 16u, "00db");
    putLe64(target, 20u, moviOffset);
    putLe32(target, 28u, 0u);
}

void encodeStandardIndexEntries(std::span<std::byte> target, const Layout& layout, std::uint64_t first) noexcept
{
    // Relative to the section's `movi` fourcc: four bytes to the first chunk, then its eight-byte header.
    const std::uint64_t chunk = chunkBytes(layout);
    const auto size = static_cast<std::uint32_t>(layout.frameBytes);
    const std::size_t count = target.size() / kStandardIndexEntryBytes;
    for (std::size_t i = 0u; i < count; ++i)
    {
        const std::size_t at = i * kStandardIndexEntryBytes;
        putLe32(target, at, static_cast<std::uint32_t>(12u + (first + i) * chunk));
        putLe32(target, at + 4u, size);
    }
}

void encodeIdx1Header(std::span<std::byte> target, std::uint64_t frames) noexcept
{
    putFourcc(target, 0u, "idx1");
    putLe32(target, 4u, static_cast<std::uint32_t>(kIdx1EntryBytes * frames));
}

void encodeIdx1Entries(std::span<std::byte> target, const Layout& layout, std::uint64_t first) noexcept
{
    const std::uint64_t chunk = chunkBytes(layout);
    const auto size = static_cast<std::uint32_t>(layout.frameBytes);
    const std::size_t count = target.size() / kIdx1EntryBytes;
    for (std::size_t i = 0u; i < count; ++i)
    {
        const std::size_t at = i * kIdx1EntryBytes;
        putFourcc(target, at, "00db");
        putLe32(target, at + 4u, kKeyframeFlag);
        putLe32(target, at + 8u, static_cast<std::uint32_t>(4u + (first + i) * chunk));
        putLe32(target, at + 12u, size);
    }
}

Status aviFailure(ErrorCode code, SequenceFailure reason, const char* message, std::string_view parameter,
                  std::string_view path, std::vector<StatusContextField> extra)
{
    std::vector<StatusContextField> context;
    context.reserve(3u + extra.size());
    context.push_back(StatusContextField::ofCount(ContextKey::SequenceReason, static_cast<std::uint64_t>(reason)));
    if (!parameter.empty())
        context.push_back(StatusContextField::ofText(ContextKey::Parameter, std::string(parameter)));
    if (!path.empty())
        context.push_back(StatusContextField::ofText(ContextKey::Path, std::string(path)));
    for (auto& field : extra)
        context.push_back(std::move(field));
    return Status::failure(
        code, {.message = message, .operation = "avi", .domain = ErrorDomain::Io, .context = std::move(context)});
}

Status argumentFailure(const char* message, const char* parameter)
{
    return Status::failure(ErrorCode::InvalidArgument,
                           {.message = message,
                            .operation = "avi",
                            .domain = ErrorDomain::Io,
                            .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
}

Status ioFailure(int platformError, const char* message, std::string_view path)
{
    return aviFailure(ErrorCode::ExternalFailure, SequenceFailure::IoFailure, message, {}, path,
                      {StatusContextField::ofNumber(ContextKey::LibraryCode, platformError)});
}

} // namespace an::avi_detail
