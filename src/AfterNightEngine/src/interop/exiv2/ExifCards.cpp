// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the Exiv2 calls behind the Exif card translation, and Exiv2's version
// report.
//
// Every call into the library runs inside the engine's exception boundary, so an `Exiv2::Error` or
// an allocation failure comes back as a typed failure. The one exception caught here instead is the
// one a single field raises while cards are applied: that field is skipped and the others are
// written. XMLPacket entries are removed before Exiv2 reads the root directory: the engine needs
// Exif, not XMP, and never sends a file's XML to the library's XMP toolkit.

#include "interop/exiv2/ExifCards.h"

#include <AfterNightEngine/Core/ExceptionBoundary.h>

#include "runtime/DependencyVersions.h"

#if defined(_MSC_VER)
// The metadata library exports classes holding standard containers and deriving from
// std::exception. Both diagnostics describe that library's own boundary rather than this engine's
// use of it, and they are silenced around the include rather than for the project.
#pragma warning(push)
#pragma warning(disable : 4251 4275)
#endif
#include <exiv2/basicio.hpp>
#include <exiv2/cr2image.hpp>
#include <exiv2/error.hpp>
#include <exiv2/exif.hpp>
#include <exiv2/orfimage.hpp>
#include <exiv2/rw2image.hpp>
#include <exiv2/tiffimage.hpp>
#include <exiv2/types.hpp>
#include <exiv2/value.hpp>
#include <exiv2/version.hpp>
#include <exiv2/xmp_exiv2.hpp>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <optional>
#include <string>
#include <utility>

namespace an
{
namespace
{

/// Where every failure this adapter translates says it came from.
constexpr ExternalBoundary kExifBoundary{"exiv2", "raster metadata", ErrorDomain::Io};

/// Whether a field describes the source file's own structure rather than the picture.
///
/// Where a container put its strips, where its sub-directories begin and how long its thumbnail is
/// are facts about the file the metadata was read from. The destination decides every one of them
/// afresh, and a thumbnail's offset in particular means nothing outside the file it points into -
/// which is why the thumbnail travels as its own bytes instead.
[[nodiscard]] bool isSourceStructureKey(std::string_view key) noexcept
{
    return key == "Exif.Image.StripOffsets" || key == "Exif.Image.StripByteCounts" || key == "Exif.Image.TileOffsets" ||
           key == "Exif.Image.TileByteCounts" || key == "Exif.Image.ExifTag" || key == "Exif.Image.GPSTag" ||
           key == "Exif.Image.SubIFDs" || key == "Exif.Photo.InteroperabilityTag" || key == "Exif.Photo.MakerNote" ||
           key == kExifThumbnailKeyword || key == "Exif.Thumbnail.JPEGInterchangeFormatLength";
}

/// Whether the number a TIFF structure's header carries after its byte-order mark names one this
/// adapter reads: the classic TIFF's 42, the 0x4f52 and 0x5352 of the Olympus ORF dialect and the
/// 0x55 of the Panasonic RW2 one, which differ from the classic header in that number alone.
///
/// @param magic the number, read in the header's byte order.
/// @return true for the four.
[[nodiscard]] constexpr bool isTiffStructureMagic(std::uint32_t magic) noexcept
{
    return magic == 42u || magic == 0x4f52u || magic == 0x5352u || magic == 0x55u;
}

/// A TIFF structure's root directory, located and bounded before anything in it is moved.
struct RootDirectory
{
    /// Whether the structure's numbers are little-endian.
    bool little = false;

    /// Where the directory's entry count is.
    std::size_t at = 0u;

    /// How many entries it has.
    std::size_t count = 0u;

    /// How many of them are XMLPacket entries.
    std::size_t xmpEntries = 0u;
};

/// A number of a TIFF structure, in its byte order.
///
/// @param bytes the structure; the caller has checked that `width` bytes from `at` are inside it.
/// @param little whether the structure is little-endian.
/// @param at where the number starts.
/// @param width how many bytes it has, at most four.
[[nodiscard]] std::uint32_t structureNumber(std::span<const std::byte> bytes, bool little, std::size_t at,
                                            unsigned width) noexcept
{
    std::uint32_t value = 0u;
    for (unsigned i = 0u; i < width; ++i)
        value |= std::to_integer<std::uint32_t>(bytes[at + i]) << (8u * (little ? i : width - 1u - i));
    return value;
}

/// Locates a TIFF structure's root directory and counts its XMLPacket entries, reading nothing of
/// the packets themselves.
///
/// @param bytes a TIFF container of one of the dialects `isTiffStructureMagic` names, or an Exif
///        payload, starting at its byte-order marker.
/// @return the directory; no value for another dialect or a structure with no root directory, which
///         remain Exiv2's to refuse; or InvalidData for a header or directory whose bounds cannot be
///         proved.
[[nodiscard]] Result<std::optional<RootDirectory>> rootDirectoryOf(std::span<const std::byte> bytes)
{
    if (bytes.size() < 8u)
        return Status::failure(ErrorCode::InvalidData);
    RootDirectory root;
    root.little = bytes[0] == std::byte{'I'} && bytes[1] == std::byte{'I'};
    const bool big = bytes[0] == std::byte{'M'} && bytes[1] == std::byte{'M'};
    if (!root.little && !big)
        return Status::failure(ErrorCode::InvalidData);
    if (!isTiffStructureMagic(structureNumber(bytes, root.little, 2u, 2u)))
        return std::optional<RootDirectory>{};
    root.at = structureNumber(bytes, root.little, 4u, 4u);
    if (root.at == 0u)
        return std::optional<RootDirectory>{};
    if (root.at < 8u || root.at > bytes.size() - 2u)
        return Status::failure(ErrorCode::InvalidData);
    root.count = structureNumber(bytes, root.little, root.at, 2u);
    const std::size_t entries = root.at + 2u;
    if (root.count * 12u + 4u > bytes.size() - entries)
        return Status::failure(ErrorCode::InvalidData);
    for (std::size_t index = 0u; index < root.count; ++index)
        root.xmpEntries += structureNumber(bytes, root.little, entries + index * 12u, 2u) == 700u ? 1u : 0u;
    return std::optional<RootDirectory>{root};
}

/// Removes a root directory's XMLPacket entries where they stand. All offsets stay absolute; only
/// the directory's entries, its count and the pointer that follows them move.
///
/// @param bytes the structure `root` was located in, writable.
/// @param root its root directory.
void removeXmpEntries(std::span<std::byte> bytes, const RootDirectory& root) noexcept
{
    const std::size_t entries = root.at + 2u;
    std::size_t kept = 0u;
    for (std::size_t index = 0u; index < root.count; ++index)
    {
        const std::size_t at = entries + index * 12u;
        if (structureNumber(bytes, root.little, at, 2u) == 700u)
            continue;
        if (kept != index)
            std::memmove(bytes.data() + entries + kept * 12u, bytes.data() + at, 12u);
        ++kept;
    }
    bytes[root.at] = static_cast<std::byte>(root.little ? kept & 0xffu : kept >> 8u);
    bytes[root.at + 1u] = static_cast<std::byte>(root.little ? kept >> 8u : kept & 0xffu);
    std::memmove(bytes.data() + entries + kept * 12u, bytes.data() + entries + root.count * 12u, 4u);
}

/// Removes the root TIFF directory's XMLPacket entries before the metadata library can dispatch
/// them to its XML parser. Exiv2's TIFF mapping decodes XMP only in this directory, including when
/// reached through ExifParser. Duplicates are removed too, without reading the packet or trusting
/// its size or offset.
///
/// @param bytes a TIFF container of one of the dialects `isTiffStructureMagic` names, or an Exif
///        payload, starting at its byte-order marker.
/// @return a replacement when XMLPacket entries existed, an empty vector otherwise, or InvalidData
///         for a directory whose bounds cannot be proved. Other TIFF dialects remain Exiv2's refusal.
[[nodiscard]] Result<std::vector<std::byte>> withoutXmp(std::span<const std::byte> bytes)
{
    auto root = rootDirectoryOf(bytes);
    if (!root.hasValue())
        return root.status();
    if (!root->has_value() || (*root)->xmpEntries == 0u)
        return std::vector<std::byte>{};
    std::vector<std::byte> clean(bytes.begin(), bytes.end());
    removeXmpEntries(clean, **root);
    return clean;
}

/// Removes the root TIFF directory's XMLPacket entries in the structure itself, as `withoutXmp`
/// does in a copy, so a whole file is never copied to lose one directory entry.
///
/// @param bytes the structure, writable.
/// @return success, also when there was nothing to remove; or InvalidData, as `withoutXmp` reports.
[[nodiscard]] Status removeXmpInPlace(std::span<std::byte> bytes)
{
    auto root = rootDirectoryOf(bytes);
    if (!root.hasValue())
        return root.status();
    if (root->has_value() && (*root)->xmpEntries != 0u)
        removeXmpEntries(bytes, **root);
    return {};
}

/// Renders a block of bytes the way the metadata library renders one, so its reader parses it back.
///
/// @param bytes the block.
/// @return the decimal values, separated by single spaces.
[[nodiscard]] std::string renderBytes(std::span<const std::byte> bytes)
{
    std::string text;
    text.reserve(bytes.size() * 4u);
    for (const std::byte value : bytes)
    {
        if (!text.empty())
        {
            text.push_back(' ');
        }
        text += std::to_string(static_cast<unsigned>(std::to_integer<unsigned char>(value)));
    }
    return text;
}

/// Reads back what `renderBytes` wrote.
///
/// @param text the rendered block.
/// @return the bytes; a value outside a byte ends the parse, because the rest cannot be trusted.
[[nodiscard]] std::vector<std::byte> parseBytes(std::string_view text)
{
    std::vector<std::byte> bytes;
    std::size_t offset = 0u;
    while (offset < text.size())
    {
        while (offset < text.size() && text[offset] == ' ')
        {
            ++offset;
        }
        unsigned value = 0u;
        std::size_t digits = 0u;
        while (offset + digits < text.size() && text[offset + digits] >= '0' && text[offset + digits] <= '9')
        {
            value = value * 10u + static_cast<unsigned>(text[offset + digits] - '0');
            if (value > 255u)
            {
                return bytes;
            }
            ++digits;
        }
        if (digits == 0u)
        {
            return bytes;
        }
        bytes.push_back(static_cast<std::byte>(value));
        offset += digits;
    }
    return bytes;
}

} // namespace

Status appendContinuedCard(std::vector<CodecCard>& cards, CodecNamespace origin, const std::string& keyword,
                           const std::string& text, const std::string& comment)
{
    return appendFieldCards(cards, origin, keyword, text, comment, "read Exif cards");
}

namespace
{

/// Turns every field of a metadata container into cards.
///
/// @param origin the namespace the reading container's cards belong to.
/// @param data the library's own metadata container.
/// @return the cards, or whatever appending one reported.
[[nodiscard]] Result<std::vector<CodecCard>> cardsOf(CodecNamespace origin, const Exiv2::ExifData& data)
{
    std::vector<CodecCard> cards;
    for (const Exiv2::Exifdatum& datum : data)
    {
        const std::string key = datum.key();
        if (isSourceStructureKey(key))
        {
            continue;
        }
        const char* const type = datum.typeName();
        if (const auto added =
                appendContinuedCard(cards, origin, key, datum.toString(), type != nullptr ? type : "Undefined");
            added.failed())
        {
            return added;
        }
    }
    const Exiv2::DataBuf thumbnail = Exiv2::ExifThumbC(data).copy();
    if (thumbnail.size() != 0u)
    {
        const std::span<const std::byte> bytes{reinterpret_cast<const std::byte*>(thumbnail.c_data()),
                                               thumbnail.size()};
        if (const auto added =
                appendContinuedCard(cards, origin, std::string(kExifThumbnailKeyword), renderBytes(bytes), "Undefined");
            added.failed())
        {
            return added;
        }
    }
    return cards;
}

/// Reads a decimal number into a field whose type is a ratio of two integers.
///
/// This engine states its own numbers in decimal - an exposure of two and a half seconds is "2.5" -
/// and this vocabulary's fields for them are ratios, which the metadata library's own reader will
/// not take a decimal for. The conversion takes a denominator large enough to keep six decimal
/// places, reduces the ratio by the greatest common divisor, and narrows it while the numerator would
/// not fit the field.
///
/// @param value the field to read into.
/// @param type the field's type.
/// @param text the decimal number.
/// @return whether the value was read.
[[nodiscard]] bool readDecimalAsRational(Exiv2::Value& value, Exiv2::TypeId type, const std::string& text)
{
    if (type != Exiv2::unsignedRational && type != Exiv2::signedRational)
    {
        return false;
    }
    char* end = nullptr;
    const double number = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || end == nullptr || *end != '\0' || !std::isfinite(number))
    {
        return false;
    }
    if (type == Exiv2::unsignedRational && number < 0.0)
    {
        return false;
    }
    constexpr double kLargestNumerator = 2147483000.0;
    std::int64_t denominator = 1000000;
    while (denominator > 1 && std::abs(number) * static_cast<double>(denominator) > kLargestNumerator)
    {
        denominator /= 10;
    }
    if (std::abs(number) * static_cast<double>(denominator) > kLargestNumerator)
    {
        return false;
    }
    const auto numerator = static_cast<std::int64_t>(std::llround(number * static_cast<double>(denominator)));
    const auto divisor = std::gcd(numerator < 0 ? -numerator : numerator, denominator);
    const auto reduced = divisor > 0 ? divisor : std::int64_t{1};
    return value.read(std::to_string(numerator / reduced) + "/" + std::to_string(denominator / reduced)) == 0;
}

/// One field, rebuilt from the card that states it and the cards that continue it.
struct CardField
{
    /// The field's key.
    std::string keyword;

    /// Its complete rendered value.
    std::string text;

    /// The Exif type the card states, or empty when it states none.
    std::string type;
};

/// Rebuilds the fields a card set states, joining each continuation onto the card before it.
///
/// @param cards the cards to read.
/// @return the fields, in the order the cards state them.
[[nodiscard]] std::vector<CardField> joinCards(std::span<const CodecCard> cards)
{
    std::vector<CardField> fields;
    for (const CodecCard& card : cards)
    {
        if (!isExifKeyword(card.keyword()))
        {
            continue;
        }
        if (card.comment() == kExifContinuedComment && !fields.empty() && fields.back().keyword == card.keyword())
        {
            fields.back().text += card.value();
            continue;
        }
        fields.push_back(CardField{card.keyword(), card.value(), card.comment()});
    }
    return fields;
}

/// States the fields a card set carries in a metadata container, replacing what it already said.
///
/// A keyword the library does not know, and a value it cannot read back in the type the card
/// states, are both skipped: one unreadable field is not a reason to write none of the others.
///
/// @param data the container to state them in.
/// @param cards the cards to apply.
void applyCards(Exiv2::ExifData& data, std::span<const CodecCard> cards)
{
    for (const CardField& field : joinCards(cards))
    {
        try
        {
            if (field.keyword == "Exif.Image.XMLPacket")
                continue;
            if (field.keyword == kExifThumbnailKeyword)
            {
                const auto bytes = parseBytes(field.text);
                if (!bytes.empty())
                {
                    Exiv2::ExifThumb(data).setJpegThumbnail(reinterpret_cast<const Exiv2::byte*>(bytes.data()),
                                                            bytes.size());
                }
                continue;
            }
            const Exiv2::ExifKey key(field.keyword);
            auto typeId = Exiv2::TypeInfo::typeId(field.type);
            if (typeId == Exiv2::invalidTypeId)
            {
                typeId = key.defaultTypeId();
            }
            auto value = Exiv2::Value::create(typeId);
            if (!value)
            {
                continue;
            }
            if (value->read(field.text) != 0 && !readDecimalAsRational(*value, typeId, field.text))
            {
                continue;
            }
            data[field.keyword] = *value;
        }
        catch (const Exiv2::Error&)
        {
            continue;
        }
    }
}

} // namespace

bool isExifKeyword(std::string_view keyword) noexcept
{
    return keyword.starts_with("Exif.");
}

Result<std::vector<CodecCard>> readExifBlock(CodecNamespace origin, std::span<const std::byte> block)
{
    if (block.empty())
    {
        return std::vector<CodecCard>{};
    }
    auto read = evaluateAcrossBoundary(kExifBoundary, [&]() -> Result<std::vector<CodecCard>> {
        auto clean = withoutXmp(block);
        if (!clean.hasValue())
            return clean.status();
        if (!clean->empty())
            block = *clean;
        Exiv2::ExifData data;
        static_cast<void>(
            Exiv2::ExifParser::decode(data, reinterpret_cast<const Exiv2::byte*>(block.data()), block.size()));
        return cardsOf(origin, data);
    });
    if (!read.hasValue())
    {
        return read.status();
    }
    return std::move(*read);
}

Result<std::vector<CodecCard>> readTiffExifCards(std::span<std::byte> file)
{
    if (file.empty())
    {
        return std::vector<CodecCard>{};
    }
    auto read = evaluateAcrossBoundary(kExifBoundary, [&]() -> Result<std::vector<CodecCard>> {
        if (const auto removed = removeXmpInPlace(file); removed.failed())
            return removed;
        Exiv2::ExifData exif;
        Exiv2::IptcData iptc;
        Exiv2::XmpData xmp;
        static_cast<void>(
            Exiv2::TiffParser::decode(exif, iptc, xmp, reinterpret_cast<const Exiv2::byte*>(file.data()), file.size()));
        return cardsOf(CodecNamespace::Tiff, exif);
    });
    if (!read.hasValue())
    {
        return read.status();
    }
    return std::move(*read);
}

namespace
{

/// The camera RAW containers whose Exif this adapter reads, told apart by the header they open with.
enum class CameraContainer : std::uint8_t
{
    /// A classic TIFF header: NEF, NRW, DNG, ARW, SR2, PEF, SRW, ERF, 3FR and the others written as one.
    Tiff,
    /// A classic TIFF header followed by Canon's `CR` signature.
    CanonCr2,
    /// The Olympus dialect of the header.
    OlympusOrf,
    /// The Panasonic dialect of the header, which Leica's RWL shares.
    PanasonicRw2,
    /// Fujifilm's own header, ahead of a JPEG preview that carries the Exif.
    FujifilmRaf,
    /// Anything else, whose Exif this adapter does not read.
    Other
};

/// The signature a RAF file opens with.
constexpr std::string_view kRafSignature = "FUJIFILMCCD-RAW ";

/// Where a RAF header states its JPEG preview: a big-endian offset, then a big-endian length.
constexpr std::size_t kRafPreviewAt = 84u;

/// What the payload of a JPEG segment that carries Exif opens with.
constexpr std::string_view kJpegExifIdentifier{"Exif\0\0", 6u};

/// A byte of a file, as a number.
[[nodiscard]] unsigned byteAt(std::span<const std::byte> file, std::size_t at) noexcept
{
    return std::to_integer<unsigned>(file[at]);
}

/// Whether a file's bytes at a position are a text's.
[[nodiscard]] bool bytesAre(std::span<const std::byte> file, std::size_t at, std::string_view text) noexcept
{
    if (at > file.size() || text.size() > file.size() - at)
        return false;
    for (std::size_t i = 0u; i < text.size(); ++i)
        if (byteAt(file, at + i) != static_cast<unsigned char>(text[i]))
            return false;
    return true;
}

/// A big-endian number of a file.
///
/// @param file the file; the caller has checked that `width` bytes from `at` are inside it.
/// @param at where the number starts.
/// @param width how many bytes it has, at most four.
[[nodiscard]] std::uint32_t bigEndianAt(std::span<const std::byte> file, std::size_t at, unsigned width) noexcept
{
    std::uint32_t value = 0u;
    for (unsigned i = 0u; i < width; ++i)
        value = (value << 8u) | byteAt(file, at + i);
    return value;
}

/// Which container a camera file is, by its header.
///
/// @param file the whole file.
/// @return the container; `Other` for a header none of the others has.
[[nodiscard]] CameraContainer cameraContainerOf(std::span<const std::byte> file) noexcept
{
    if (bytesAre(file, 0u, kRafSignature))
        return CameraContainer::FujifilmRaf;
    if (file.size() < 8u)
        return CameraContainer::Other;
    const bool little = bytesAre(file, 0u, "II");
    if (!little && !bytesAre(file, 0u, "MM"))
        return CameraContainer::Other;
    const std::uint32_t magic = little ? byteAt(file, 2u) | (byteAt(file, 3u) << 8u) : bigEndianAt(file, 2u, 2u);
    if (magic == 42u)
        return bytesAre(file, 8u, "CR\x02") ? CameraContainer::CanonCr2 : CameraContainer::Tiff;
    if (magic == 0x4f52u || magic == 0x5352u)
        return CameraContainer::OlympusOrf;
    if (magic == 0x55u)
        return CameraContainer::PanasonicRw2;
    return CameraContainer::Other;
}

/// The Exif block of the JPEG preview a RAF file embeds: the payload, after the identifier, of the
/// preview's first segment that carries Exif.
///
/// Every offset and length is checked against the file before it is followed, and the segments are
/// walked only up to the start of the preview's image data.
///
/// @param file the whole RAF file.
/// @return the block; empty when the header names no preview inside the file, the preview is not a
///         JPEG, or it carries no Exif before its image data.
[[nodiscard]] std::span<const std::byte> rafExifBlock(std::span<const std::byte> file) noexcept
{
    if (file.size() < kRafPreviewAt + 8u)
        return {};
    const std::uint64_t offset = bigEndianAt(file, kRafPreviewAt, 4u);
    const std::uint64_t length = bigEndianAt(file, kRafPreviewAt + 4u, 4u);
    if (offset > file.size() || length > file.size() - offset)
        return {};
    const auto jpeg = file.subspan(static_cast<std::size_t>(offset), static_cast<std::size_t>(length));
    if (jpeg.size() < 4u || byteAt(jpeg, 0u) != 0xffu || byteAt(jpeg, 1u) != 0xd8u)
        return {};
    std::size_t at = 2u;
    while (at + 2u <= jpeg.size() && byteAt(jpeg, at) == 0xffu)
    {
        const unsigned marker = byteAt(jpeg, at + 1u);
        if (marker == 0xffu)
        {
            // A fill byte before the marker.
            ++at;
            continue;
        }
        if (marker == 0xd9u || marker == 0xdau)
            return {};
        if (marker == 0x01u || (marker >= 0xd0u && marker <= 0xd7u))
        {
            // A marker that stands alone, with no length.
            at += 2u;
            continue;
        }
        if (at + 4u > jpeg.size())
            return {};
        const std::size_t segment = bigEndianAt(jpeg, at + 2u, 2u);
        if (segment < 2u || segment > jpeg.size() - at - 2u)
            return {};
        const auto payload = jpeg.subspan(at + 4u, segment - 2u);
        if (marker == 0xe1u && bytesAre(payload, 0u, kJpegExifIdentifier))
            return payload.subspan(kJpegExifIdentifier.size());
        at += 2u + segment;
    }
    return {};
}

} // namespace

Result<std::vector<CodecCard>> readCameraRawExifCards(std::span<std::byte> file)
{
    const CameraContainer container = cameraContainerOf(file);
    if (container == CameraContainer::Other)
    {
        return std::vector<CodecCard>{};
    }
    if (container == CameraContainer::FujifilmRaf)
    {
        return readExifBlock(CodecNamespace::CameraRaw, rafExifBlock(file));
    }
    auto read = evaluateAcrossBoundary(kExifBoundary, [&]() -> Result<std::vector<CodecCard>> {
        if (const auto removed = removeXmpInPlace(file); removed.failed())
            return removed;
        Exiv2::ExifData exif;
        Exiv2::IptcData iptc;
        Exiv2::XmpData xmp;
        const auto* const bytes = reinterpret_cast<const Exiv2::byte*>(file.data());
        // Each parser is the one the library's own reader of that container decodes with; they differ
        // in the header they accept and in where the root directory points.
        switch (container)
        {
        case CameraContainer::CanonCr2:
            static_cast<void>(Exiv2::Cr2Parser::decode(exif, iptc, xmp, bytes, file.size()));
            break;
        case CameraContainer::OlympusOrf:
            static_cast<void>(Exiv2::OrfParser::decode(exif, iptc, xmp, bytes, file.size()));
            break;
        case CameraContainer::PanasonicRw2:
            static_cast<void>(Exiv2::Rw2Parser::decode(exif, iptc, xmp, bytes, file.size()));
            break;
        default:
            static_cast<void>(Exiv2::TiffParser::decode(exif, iptc, xmp, bytes, file.size()));
            break;
        }
        return cardsOf(CodecNamespace::CameraRaw, exif);
    });
    if (!read.hasValue())
    {
        return read.status();
    }
    return std::move(*read);
}

Result<std::vector<std::byte>> writeExifBlock(std::span<const CodecCard> cards)
{
    auto written = evaluateAcrossBoundary(kExifBoundary, [&]() -> std::vector<std::byte> {
        Exiv2::ExifData data;
        applyCards(data, cards);
        std::vector<std::byte> bytes;
        if (data.empty())
        {
            return bytes;
        }
        Exiv2::Blob blob;
        Exiv2::ExifParser::encode(blob, Exiv2::littleEndian, data);
        bytes.resize(blob.size());
        if (!blob.empty())
        {
            std::memcpy(bytes.data(), blob.data(), blob.size());
        }
        return bytes;
    });
    if (!written.hasValue())
    {
        return written.status();
    }
    return std::move(*written);
}

Result<TiffExifRewrite> embedTiffExifCards(std::span<std::byte> file, std::span<const CodecCard> cards)
{
    if (file.empty())
    {
        return Status::failure(ErrorCode::InvalidArgument);
    }
    auto rewritten = evaluateAcrossBoundary(kExifBoundary, [&]() -> Result<TiffExifRewrite> {
        if (const auto removed = removeXmpInPlace(file); removed.failed())
            return removed;
        Exiv2::ExifData exif;
        Exiv2::IptcData iptc;
        Exiv2::XmpData xmp;
        auto* const bytes = reinterpret_cast<Exiv2::byte*>(file.data());
        // The container is read before it is written, so every tag the encoder already wrote stays:
        // this library states the complete metadata of the file it produces, and a tag absent from
        // what it is given is a tag it removes.
        const auto order = Exiv2::TiffParser::decode(exif, iptc, xmp, bytes, file.size());
        applyCards(exif, cards);
        xmp.usePacket(true);
        Exiv2::MemIo io;
        const auto method = Exiv2::TiffParser::encode(io, bytes, file.size(), order, exif, iptc, xmp);
        TiffExifRewrite result;
        if (method != Exiv2::wmIntrusive)
        {
            return result;
        }
        result.rewritten = true;
        result.bytes.resize(io.size());
        if (!result.bytes.empty())
        {
            static_cast<void>(io.seek(0, Exiv2::BasicIo::beg));
            static_cast<void>(io.read(reinterpret_cast<Exiv2::byte*>(result.bytes.data()), result.bytes.size()));
        }
        return result;
    });
    if (!rewritten.hasValue())
    {
        return rewritten.status();
    }
    return std::move(*rewritten);
}

Result<std::vector<ExifFieldFacts>> describeExifCards(std::span<const CodecCard> cards)
{
    auto described = evaluateAcrossBoundary(kExifBoundary, [&]() -> std::vector<ExifFieldFacts> {
        std::vector<ExifFieldFacts> fields;
        for (CardField& joined : joinCards(cards))
        {
            ExifFieldFacts field;
            field.keyword = std::move(joined.keyword);
            field.value = std::move(joined.text);
            field.typeName = joined.type;
            try
            {
                const Exiv2::ExifKey key(field.keyword);
                field.tag = key.tag();
                field.tagName = key.tagName();
                field.tagLabel = key.tagLabel();
                field.tagDescription = key.tagDesc();
                auto typeId = Exiv2::TypeInfo::typeId(joined.type);
                if (typeId == Exiv2::invalidTypeId)
                {
                    typeId = key.defaultTypeId();
                    const char* const name = Exiv2::TypeInfo::typeName(typeId);
                    field.typeName = name != nullptr ? name : "";
                }
                auto value = Exiv2::Value::create(typeId);
                if (value && (value->read(field.value) == 0 || readDecimalAsRational(*value, typeId, field.value)))
                {
                    field.count = static_cast<std::uint64_t>(value->count());
                }
            }
            catch (const Exiv2::Error&)
            {
                // A key the library does not know is still a field: its key, its value and its card's type.
            }
            fields.push_back(std::move(field));
        }
        return fields;
    });
    if (!described.hasValue())
    {
        return described.status();
    }
    return std::move(*described);
}

namespace dependency_detail
{

void reportExiv2(DependencyVersion& entry) noexcept
{
    entry.name = "exiv2";
    entry.linkage = DependencyLinkage::SharedLibrary;
    setVersionTextFromParts(entry.compiled, EXIV2_MAJOR_VERSION, EXIV2_MINOR_VERSION, EXIV2_PATCH_VERSION);

    // The packed number rather than the library's own string accessor, which returns a `std::string`
    // by value: this report allocates nothing, and a version query is not a place to find out what
    // a container does when it cannot. The packing is the library's own `EXIV2_MAKE_VERSION`.
    const std::uint32_t loaded = Exiv2::versionNumber();
    setVersionTextFromParts(entry.loaded, loaded >> 16u, (loaded >> 8u) & 0xFFu, loaded & 0xFFu);
}

} // namespace dependency_detail

} // namespace an
