// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - encoding an XISF image into a pending path.
//
// Every refusal is decided before the pending file is opened. The image's structural declarations -
// colour space, image type, sample range, filter array, pedestal, orientation, identifiers, working space,
// display function and resolution - are restated from the frame being written, falling back to what the intent or
// the source file declared only where the frame's description says nothing; the cards follow the same
// carry-forward rules as the FITS writer, with FITS keywords written as such and every other card as an XISF
// property of the type its comment states, a value continued across several cards joined. The pixel data are written
// in the form the intent states - its codec and level, byte shuffling, the size of the subblocks a reader decodes on
// their own, a checksum - a compressed frame defaulting to shuffled samples, as PixInsight writes them, in subblocks
// a region read takes one at a time. The unit goes to the transaction's pending file through a sink, which the
// transaction alone publishes. A unit asked for with a blank header prolog is written the same way and then, in the
// pending file while it is still open, loses the XML declaration and the comment its header opens with to spaces.
//
// A reserved image is the same unit with zeros where the samples go, written uncompressed by the library's reserved
// image and then held open in its editor, which writes each band of rows the transaction hands it where it lies and,
// at the commit, replaces the zeros' checksum with that of the rows written.

#include "io/xisf/XisfFile.h"

#include <AfterNightEngine/Algorithms/Astrometry/WcsCards.h>
#include <AfterNightEngine/Core/Version.h>
#include <AfterNightEngine/IO/ArtifactIdentity.h>
#include <AfterNightEngine/IO/CodecMetadataPolicy.h>
#include <AfterNightEngine/IO/CodecMetadataProjection.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "core/Utf8.h"
#include "image/CardContinuation.h"
#include "image/SolutionCards.h"
#include "io/registry/ReservedCodecWrite.h"

#include <openxisf/editor.h>
#include <openxisf/stream_io.h>
#include <openxisf/writer.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace an
{
namespace
{
/// The property under which earlier versions of this engine stated their version in every image they wrote.
///
/// The specification defines no such property - the unit's `XISF:CreatorApplication` names the application that wrote
/// it, name and version - so it is no longer written, and a copy a source carries or an intent states is dropped.
constexpr std::string_view kXisfSoftwareKeyword = "Instrument:Software:Name";

/// The keywords of the structural cards the encoder consumes and restates.
constexpr std::array<std::string_view, 11u> kStructuralKeywords = {
    kXisfColorSpaceKeyword,   kXisfImageTypeKeyword,       kXisfBoundsKeyword,    kXisfFilterArrayKeyword,
    kXisfOffsetKeyword,       kXisfOrientationKeyword,     kXisfIdKeyword,        kXisfUuidKeyword,
    kXisfWorkingSpaceKeyword, kXisfDisplayFunctionKeyword, kXisfResolutionKeyword};

/// Whether text can be an attribute's value in the container's markup.
///
/// A property's comment is an XML attribute, and XML 1.0 has no way to spell a control character in one. Non-ASCII
/// bytes are fine - the container is UTF-8 - so the rule is exactly the controls, refused here rather than written
/// into a file this codec could not read back. A property's value is not held to it: a String XML cannot spell as
/// character data - a processing history's line breaks - is written in a data block, which holds any bytes.
bool markupSafe(std::string_view text) noexcept
{
    return std::none_of(text.begin(), text.end(), [](char value) {
        const auto byte = static_cast<unsigned char>(value);
        return byte < 0x20u || byte == 0x7fu;
    });
}

/// Whether text is what FITS allows in a record: printable ASCII.
bool fitsText(std::string_view text) noexcept
{
    return std::all_of(text.begin(), text.end(), [](unsigned char c) { return c >= 32u && c <= 126u; });
}

/// Whether a name is a FITS keyword a FITSKeyword element can carry: one to eight upper-case letters, digits,
/// hyphens and underscores. A longer name is a FITS file's HIERARCH convention, which XISF has no element for.
bool fitsKeywordName(std::string_view name) noexcept
{
    return !name.empty() && name.size() <= 8u && std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
}

/// Whether a name is an XISF property identifier: words of letters, digits and underscores that do not start with a
/// digit, separated by colons.
bool propertyIdentifier(std::string_view name) noexcept
{
    if (name.empty())
        return false;
    bool wordStart = true;
    for (const char c : name)
    {
        const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
        const bool digit = c >= '0' && c <= '9';
        if (c == ':')
        {
            if (wordStart)
                return false;
            wordStart = true;
            continue;
        }
        if (!letter && !(digit && !wordStart))
            return false;
        wordStart = false;
    }
    return !wordStart;
}

/// The commentary keywords, whose text XISF carries as the comment and never as a value.
bool commentaryKeyword(std::string_view keyword) noexcept
{
    return keyword == "HISTORY" || keyword == "COMMENT";
}

/// Whether a card states a fact the encoder decides for itself from the frame it was handed.
///
/// The XISF keywords are the structural facts a written image states as attributes and elements of the image
/// rather than as properties, and the unit's own metadata, which every writer states for itself; they are consumed
/// below and re-stated from the image actually being written. The property earlier versions of this engine stated
/// their version in is consumed too, and not written again: the unit's creator names the writer. A FITS
/// structural keyword describes the FITS file a frame arrived in and says nothing true about the XISF file being
/// written.
bool managedXisfCard(CodecNamespace origin, std::string_view keyword) noexcept
{
    if (origin == CodecNamespace::Fits)
        return isStructuralFitsKeyword(keyword);
    return origin == CodecNamespace::Xisf &&
           (std::find(kStructuralKeywords.begin(), kStructuralKeywords.end(), keyword) != kStructuralKeywords.end() ||
            keyword == kXisfSoftwareKeyword || keyword.starts_with(kXisfUnitMetadataPrefix));
}

/// The value an intent states for one XISF keyword, or the one the source file declared.
///
/// The image's own structural attributes are consulted on both paths, including the raw encoding
/// that writes no cards at all: they describe the picture rather than annotate it, and a round trip
/// that dropped them would change what the samples mean rather than what the file says about them.
/// An identity - the image's UUID - is taken from the intent alone, which is what stops a derived file inheriting it.
const std::string* stated(const CodecEncodeRequest& request, const ImageMetadata& metadata, std::string_view keyword)
{
    const auto find = [&](std::span<const CodecCard> cards) -> const std::string* {
        for (const CodecCard& card : cards)
            if (card.origin() == CodecNamespace::Xisf && card.keyword() == keyword)
                return &card.value();
        return nullptr;
    };
    if (request.intent != nullptr)
        if (const std::string* const overridden = find(request.intent->cards()); overridden != nullptr)
            return overridden;
    if (keyword == kXisfUuidKeyword)
        return nullptr;
    return find(metadata.codec().cards());
}

/// One card the encoder writes: a FITS keyword, or an XISF property whose value may be longer than one card holds.
struct OutputCard
{
    CodecNamespace origin = CodecNamespace::Unspecified;
    std::string keyword;
    std::string value;
    std::string comment;
};

/// A card set's cards as the encoder writes them, each property's continuation cards joined to it.
///
/// A FITS card never continues another - the FITS reader joins a long string itself - and a commentary record keeps
/// its text in its comment, so one whose text is `continued` is a record of its own, never joined.
std::vector<OutputCard> joinedCards(std::span<const CodecCard> cards)
{
    std::vector<OutputCard> joined;
    joined.reserve(cards.size());
    for (std::size_t index = 0u; index < cards.size(); ++index)
    {
        if (index != 0u && cards[index].origin() == CodecNamespace::Xisf &&
            continuesCard(cards[index - 1u], cards[index]))
        {
            joined.back().value += cards[index].value();
            continue;
        }
        joined.push_back({cards[index].origin(), cards[index].keyword(), cards[index].value(), cards[index].comment()});
    }
    return joined;
}

/// The cards the encoder writes, in the order it writes them.
///
/// The source's own cards first when the intent preserves them, then the intent's, so a stated card
/// replaces a preserved one of the same name. Repeated FITS records keep every occurrence, because
/// a history is a sequence rather than a value. An identity the source carried is withheld and an
/// identity the intent states is kept, which is what stops a derived file inheriting a stamp. A FITS keyword is
/// written in upper case, as the FITS writer writes it, and a commentary keyword's text as its comment. The
/// world-coordinate cards this adapter translated from an AstrometricSolution are left out while the solution's own
/// properties are written, so the unit states its solution once, as its writer did.
Result<std::vector<OutputCard>> outputCards(const CodecEncodeRequest& request, const ImageMetadata& metadata)
{
    std::vector<OutputCard> cards;
    if (request.intent == nullptr)
        return cards;
    // XISF states acquisition facts in the FITS vocabulary it carries natively, so the shared
    // projection writes the same spellings a FITS file would and nothing learns a second one.
    auto projected = acquisitionCards({.acquisition = metadata.acquisition().toRequest()}, CodecNamespace::Fits);
    if (!projected.hasValue())
        return projected.status();
    cards = joinedCards(*projected);
    const auto append = [&](OutputCard card, bool inherited) {
        const auto origin = card.origin;
        if (origin != CodecNamespace::Fits && origin != CodecNamespace::Xisf)
            return;
        if (origin == CodecNamespace::Fits)
            for (auto& c : card.keyword)
                if (c >= 'a' && c <= 'z')
                    c = static_cast<char>(c - 'a' + 'A');
        if (managedXisfCard(origin, card.keyword))
            return;
        if (inherited && isArtifactIdentityKeyword(card.keyword))
            return;
        const bool repeated =
            origin == CodecNamespace::Fits && (commentaryKeyword(card.keyword) || card.keyword == "CONTINUE");
        // A commentary record's text is its value when it has one and its comment otherwise, as the FITS writer
        // reads it, and XISF carries that text as the comment of a keyword with no value.
        if (origin == CodecNamespace::Fits && commentaryKeyword(card.keyword) && !card.value.empty())
        {
            card.comment = std::move(card.value);
            card.value.clear();
        }
        if (!repeated)
            std::erase_if(cards,
                          [&](const OutputCard& old) { return old.origin == origin && old.keyword == card.keyword; });
        cards.push_back(std::move(card));
    };
    if (request.intent->preserveSourceCards())
        for (OutputCard& card : joinedCards(metadata.codec().cards()))
            append(std::move(card), true);
    for (OutputCard& card : joinedCards(request.intent->cards()))
        append(std::move(card), false);
    const bool solutionProperties = std::any_of(cards.begin(), cards.end(), [](const OutputCard& card) {
        return card.origin == CodecNamespace::Xisf &&
               (card.keyword.starts_with(kXisfSolutionPrefix) || card.keyword.starts_with(kPixInsightSolutionPrefix));
    });
    if (solutionProperties)
        std::erase_if(cards, [](const OutputCard& card) {
            return card.origin == CodecNamespace::Fits && isWcsKeyword(card.keyword) &&
                   card.comment == kXisfTranslatedSolutionComment;
        });
    if (cards.size() > kMaxCodecCards)
        return Status::failure(ErrorCode::LimitExceeded);
    for (const OutputCard& card : cards)
    {
        // What the container cannot carry is refused here rather than written into a file this codec, or another
        // reader, could not read back: in a FITS keyword anything FITS itself refuses - a name of more than eight
        // characters, text outside printable ASCII - and in a property a name that is not an XISF identifier, a
        // value the library cannot write - one that is not UTF-8, or that holds U+0000 - and a control character in
        // its comment.
        const bool fits = card.origin == CodecNamespace::Fits;
        const bool spellable =
            fits ? fitsKeywordName(card.keyword) && fitsText(card.value) && fitsText(card.comment)
                 : propertyIdentifier(card.keyword) && isValidUtf8WithoutNul(card.value) && markupSafe(card.comment);
        if (!spellable)
            return Status::failure(ErrorCode::InvalidArgument,
                                   {.message = fits ? "a FITS card is not one an XISF FITSKeyword element can hold: "
                                                      "its name is more than eight characters or its text is not "
                                                      "printable ASCII"
                                                    : "a card is not one an XISF property can be: its name is not an "
                                                      "XISF property identifier, its value is not UTF-8 without "
                                                      "U+0000, or its comment holds a control character",
                                    .operation = "encode XISF cards",
                                    .domain = ErrorDomain::Io,
                                    .context = {StatusContextField::ofText(ContextKey::Parameter, card.keyword)}});
    }
    return cards;
}

/// The colour space a written image declares.
///
/// The channel schema decides it wherever the schema names one. A schema whose channels carry their
/// meaning outside this vocabulary does not, so the declaration that travelled with the image is
/// used when it is one of the three and the channel count decides otherwise.
openxisf::color_space outputColorSpace(const RawFrameDescriptor& descriptor, const std::string* declared)
{
    switch (descriptor.channelSchema())
    {
    case ChannelSchema::Mono:
        return openxisf::color_space::gray;
    case ChannelSchema::Rgb:
    case ChannelSchema::Rgba:
        return openxisf::color_space::rgb;
    case ChannelSchema::Lab:
        return openxisf::color_space::cie_lab;
    default:
        break;
    }
    if (declared != nullptr)
        for (const auto space :
             {openxisf::color_space::gray, openxisf::color_space::rgb, openxisf::color_space::cie_lab})
            if (*declared == openxisf::color_space_name(space))
                return space;
    return descriptor.extent().channelCount() >= 3u ? openxisf::color_space::rgb : openxisf::color_space::gray;
}

/// The frame kind a written image declares: the one stated, or a light frame when nothing names a kind this
/// container spells.
openxisf::image_type outputImageType(const std::string* declared)
{
    if (declared != nullptr)
        for (auto kind = static_cast<int>(openxisf::image_type::bias);
             kind <= static_cast<int>(openxisf::image_type::weight_map); ++kind)
            if (*declared == openxisf::image_type_name(static_cast<openxisf::image_type>(kind)))
                return static_cast<openxisf::image_type>(kind);
    return openxisf::image_type::light;
}

/// The filter array a written image declares, folding the descriptor's phase into the pattern.
///
/// A mosaic's phase says where the repeating block starts in this image, and XISF states the block
/// itself rather than an offset into it, so the pattern is read out of the descriptor from the
/// image's own origin. A frame with no typed mosaic falls back to the declaration the source file
/// made, which is what keeps a filter array this engine does not name from being lost on a round
/// trip; the declaration's name travels with the pattern it names.
std::optional<openxisf::color_filter_array> outputFilterArray(const RawFrameDescriptor& descriptor,
                                                              const std::string* declared)
{
    const auto parsed = declared != nullptr ? parseXisfFilterArray(*declared) : std::nullopt;
    if (!descriptor.cfa().isMosaiced())
        return parsed;
    const auto side = cfaPatternBlockSize(descriptor.cfa().pattern());
    openxisf::color_filter_array filters{.pattern = {}, .width = side, .height = side, .name = {}};
    for (std::uint32_t y = 0u; y < side; ++y)
        for (std::uint32_t x = 0u; x < side; ++x)
        {
            const auto color = descriptor.cfa().colorAt(x, y);
            filters.pattern += color == CfaColor::Red ? 'R' : color == CfaColor::Blue ? 'B' : 'G';
        }
    if (parsed && parsed->pattern == filters.pattern)
        filters.name = parsed->name;
    return filters;
}

/// The sample range a written image declares.
std::optional<openxisf::bounds> outputBounds(const std::string* declared)
{
    if (declared == nullptr)
        return std::nullopt;
    const auto text = std::string_view(*declared);
    const auto colon = text.find(':');
    if (colon == std::string_view::npos)
        return std::nullopt;
    const auto number = [](std::string_view field) -> std::optional<double> {
        double value = 0.0;
        const auto* const last = field.data() + field.size();
        const auto parsed = std::from_chars(field.data(), last, value);
        if (parsed.ec != std::errc{} || parsed.ptr != last)
            return std::nullopt;
        return value;
    };
    const auto low = number(text.substr(0u, colon));
    const auto high = number(text.substr(colon + 1u));
    if (!low || !high || !(*low < *high))
        return std::nullopt;
    return openxisf::bounds{.lower = *low, .upper = *high};
}

/// The size of the subblocks a compressed block is written in when the intent states none: a frame meant for region
/// access, a region read decoding only the subblocks that hold it, at a compression ratio within a fraction of a per
/// cent of one subblock's.
constexpr std::uint64_t kDefaultSubblockBytes = std::uint64_t{4u} << 20u;

/// The codec the library compresses with, for an intent this adapter supports: no codec for none.
std::optional<std::optional<openxisf::codec>> outputCodec(OutputCompression compression) noexcept
{
    // Deflate is the whole-payload compression this container defines, as zlib, and LZ4, its high-compression
    // encoder and Zstandard are the container's own. The three tile compressions are FITS's and LZW and the JPEG
    // transform TIFF's or JPEG's; none has an XISF spelling, so they are refused rather than approximated.
    switch (compression)
    {
    case OutputCompression::None:
        return std::optional<openxisf::codec>{};
    case OutputCompression::Deflate:
        return std::optional<openxisf::codec>{openxisf::codec::zlib};
    case OutputCompression::Lz4:
        return std::optional<openxisf::codec>{openxisf::codec::lz4};
    case OutputCompression::Lz4Hc:
        return std::optional<openxisf::codec>{openxisf::codec::lz4hc};
    case OutputCompression::Zstd:
        return std::optional<openxisf::codec>{openxisf::codec::zstd};
    default:
        return std::nullopt;
    }
}

/// The checksum algorithm the library writes, for an intent's checksum: none for none.
std::optional<openxisf::checksum_algorithm> outputChecksum(OutputChecksum checksum) noexcept
{
    switch (checksum)
    {
    case OutputChecksum::Sha1:
        return openxisf::checksum_algorithm::sha1;
    case OutputChecksum::Sha256:
        return openxisf::checksum_algorithm::sha256;
    case OutputChecksum::Sha512:
        return openxisf::checksum_algorithm::sha512;
    case OutputChecksum::Sha3Digest256:
        return openxisf::checksum_algorithm::sha3_256;
    case OutputChecksum::Sha3Digest512:
        return openxisf::checksum_algorithm::sha3_512;
    default:
        return std::nullopt;
    }
}

/// The library's compression level for a level in the codec's own units: that level's place on the library's scale.
/// The library spreads a codec's levels evenly over its levels from 1 to 100, as the XISF specification asks of a
/// compression level - zlib's 0 to 9, LZ4-HC's 1 to 12 and Zstandard's 1 to 22 - and rounds a level of its own to the
/// nearest of the codec's; zlib's level 0 compresses nothing, and the library writes its blocks uncompressed. The place
/// of a codec's level, rounded, is therefore one the library gives back as that level: the ends of the range are the
/// ends of the scale, and every other level lies near the middle of the library's levels that give it, where a reader
/// that rounds another way still finds it.
/// @param codec the codec, one that takes a level.
/// @param level the level, inside the codec's range, which the intent checked.
/// @return the level on the library's scale.
int libraryLevel(openxisf::codec codec, std::uint32_t level) noexcept
{
    const auto [lowest, highest] = codec == openxisf::codec::zlib    ? std::pair{0, 9}
                                   : codec == openxisf::codec::lz4hc ? std::pair{1, 12}
                                                                     : std::pair{1, 22};
    const int span = highest - lowest;
    return 1 + ((static_cast<int>(level) - lowest) * 99 * 2 + span) / (2 * span);
}

/// The most of a header that is read to find where its root element starts: the library writes the declaration and
/// a comment of a few lines before it, some two hundred bytes.
constexpr std::size_t kPrologSearchBytes = 4096u;

/// Overwrites what a written unit's header holds before its root element with spaces, in the open pending file.
///
/// The library opens every header with the XML declaration and a comment naming itself. A virus scanner takes a
/// file that opens so for markup and reads all of it for tags, which the samples of a floating image are dense in
/// the two characters of; without them it does not. Spaces of the same count leave the header its length and every
/// block its place, and white space before the root element of a document that declares nothing is well-formed XML,
/// so the unit reads as it did. Done before the file is closed, because the scanner's pass starts when it is.
///
/// @param file the pending file, open for reading and writing, holding the whole unit.
/// @return success; or `ErrorCode::Internal` when the header is not the one the library writes - too short, or with
///         no root element after a prolog of declarations, comments and white space - or the file refuses the
///         read or the write, in which case the unit must not be published.
Status blankHeaderProlog(std::fstream& file)
{
    const auto unexpected = [](const char* what) {
        return Status::failure(ErrorCode::Internal,
                               {.message = what, .operation = "blank XISF header prolog", .domain = ErrorDomain::Io});
    };
    // The unit's first sixteen bytes are its signature, its header's length and four reserved bytes.
    constexpr std::streamoff kHeaderAt = 16;
    std::array<char, 4u> lengthBytes{};
    file.seekg(8);
    file.read(lengthBytes.data(), static_cast<std::streamsize>(lengthBytes.size()));
    if (!file)
        return unexpected("the written unit's header length could not be read back");
    std::uint32_t length = 0u;
    for (std::size_t index = 0u; index < lengthBytes.size(); ++index)
        length |= static_cast<std::uint32_t>(static_cast<unsigned char>(lengthBytes[index])) << (8u * index);
    std::string head((std::min)(static_cast<std::size_t>(length), kPrologSearchBytes), '\0');
    file.seekg(kHeaderAt);
    file.read(head.data(), static_cast<std::streamsize>(head.size()));
    if (!file)
        return unexpected("the written unit's header could not be read back");
    // The prolog is declarations and processing instructions, comments and white space; the first tag that is
    // neither is the root element.
    std::size_t root = 0u;
    while (root < head.size())
    {
        const std::string_view rest = std::string_view(head).substr(root);
        if (rest.starts_with("<?"))
        {
            const auto end = rest.find("?>");
            if (end == std::string_view::npos)
                return unexpected("the written unit's header holds a declaration that does not end");
            root += end + 2u;
        }
        else if (rest.starts_with("<!--"))
        {
            const auto end = rest.find("-->");
            if (end == std::string_view::npos)
                return unexpected("the written unit's header holds a comment that does not end");
            root += end + 3u;
        }
        else if (rest.front() == ' ' || rest.front() == '\t' || rest.front() == '\r' || rest.front() == '\n')
            ++root;
        else
            break;
    }
    if (root >= head.size() || !std::string_view(head).substr(root).starts_with("<xisf"))
        return unexpected("the written unit's header has no root element where the library writes one");
    const std::string blank(root, ' ');
    file.seekp(kHeaderAt);
    file.write(blank.data(), static_cast<std::streamsize>(blank.size()));
    file.flush();
    if (!file)
        return unexpected("the written unit's header could not be rewritten");
    return Status::success();
}

/// The image model of the unit, every structural fact restated from the image and what was declared beside it.
openxisf::image_info outputImage(const CodecEncodeRequest& request, const RawFrameDescriptor& descriptor,
                                 StorageArrangement arrangement, const ImageMetadata& metadata,
                                 openxisf::sample_format sampleFormat, const std::vector<OutputCard>& cards)
{
    const auto& extent = descriptor.extent();
    openxisf::image_info image;
    image.geometry = {.dimensions = {extent.width(), extent.height()}, .channels = extent.channelCount()};
    image.sample_format = sampleFormat;
    image.color_space = outputColorSpace(descriptor, stated(request, metadata, kXisfColorSpaceKeyword));
    image.pixel_storage =
        arrangement == StorageArrangement::Planar ? openxisf::pixel_storage::planar : openxisf::pixel_storage::normal;
    image.image_type = outputImageType(stated(request, metadata, kXisfImageTypeKeyword));
    image.bounds = outputBounds(stated(request, metadata, kXisfBoundsKeyword));
    // A floating point image always states its range, which the container requires of one; the normalized range
    // is the one every other path of this engine assumes of a frame that says nothing.
    if (!image.bounds && descriptor.sampleFormat().sampleClass == RawSampleClass::FloatingPoint)
        image.bounds = openxisf::bounds{};
    image.color_filter_array = outputFilterArray(descriptor, stated(request, metadata, kXisfFilterArrayKeyword));
    if (const auto* const offset = stated(request, metadata, kXisfOffsetKeyword); offset != nullptr)
    {
        double value = 0.0;
        const auto* const last = offset->data() + offset->size();
        if (const auto parsed = std::from_chars(offset->data(), last, value);
            parsed.ec == std::errc{} && parsed.ptr == last && value >= 0.0)
            image.offset = value;
    }
    if (const auto* const turn = stated(request, metadata, kXisfOrientationKeyword); turn != nullptr)
        for (auto kind = static_cast<int>(openxisf::orientation::none);
             kind <= static_cast<int>(openxisf::orientation::rotate_180_flip); ++kind)
            if (*turn == openxisf::orientation_name(static_cast<openxisf::orientation>(kind)))
                image.orientation = static_cast<openxisf::orientation>(kind);
    if (const auto* const id = stated(request, metadata, kXisfIdKeyword); id != nullptr)
        image.id = *id;
    if (const auto* const uuid = stated(request, metadata, kXisfUuidKeyword); uuid != nullptr)
        image.uuid = *uuid;
    if (const auto* const space = stated(request, metadata, kXisfWorkingSpaceKeyword); space != nullptr)
        image.rgb_working_space = parseXisfWorkingSpace(*space);
    if (const auto* const function = stated(request, metadata, kXisfDisplayFunctionKeyword); function != nullptr)
        image.display_function = parseXisfDisplayFunction(*function);
    if (const auto* const resolution = stated(request, metadata, kXisfResolutionKeyword); resolution != nullptr)
        image.resolution = parseXisfResolution(*resolution);
    for (const OutputCard& card : cards)
    {
        if (card.origin == CodecNamespace::Fits)
        {
            image.fits_keywords.push_back({.name = card.keyword, .value = card.value, .comment = card.comment});
            continue;
        }
        image.properties.set(xisfPropertyOf(card.keyword, card.value, card.comment));
    }
    return image;
}

/// The pending file the transaction reserved, as a path, once it is known to be one this adapter may truncate.
///
/// The transaction reserves the pending name as an empty regular file. Anything else at that path belongs to something
/// this adapter did not create and is never truncated.
Result<std::filesystem::path> pendingFile(const CodecEncodeRequest& request)
{
    std::filesystem::path pending(
        std::u8string_view(reinterpret_cast<const char8_t*>(request.pendingPath.data()), request.pendingPath.size()));
    std::error_code pathError;
    const auto pendingStatus = std::filesystem::symlink_status(pending, pathError);
    if (pathError && pathError != std::errc::no_such_file_or_directory)
        return Status::failure(ErrorCode::ExternalFailure);
    if (std::filesystem::exists(pendingStatus) && !std::filesystem::is_regular_file(pendingStatus))
        return Status::failure(ErrorCode::InvalidState);
    return pending;
}

/// The options a write of the unit is made with: the engine named as its creator, the checksum stated, and progress
/// reported under the operation's name while cancellation is checked.
openxisf::write_options unitOptions(const ExecutionContext& execution,
                                    std::optional<openxisf::checksum_algorithm> checksum, const char* stage)
{
    openxisf::write_options options;
    options.creator_application = std::string("AfterNightEngine ") + std::string(kEngineVersionString);
    options.checksum = checksum;
    options.progress = [&execution, stage](std::uint64_t done, std::uint64_t total) {
        if (execution.checkCancellation().failed())
            return false;
        execution.reportProgress(done, total, stage);
        return true;
    };
    return options;
}

/// The room a reservation wrote: the unit's one image, open in an editor that writes its rows where they lie.
///
/// The editor writes the samples of a region straight into the image's uncompressed block, from any number of threads
/// at once for regions that share no sample, and leaves every other byte of the file as it is. A planar band is
/// written a channel at a time, since a band's planes need not follow one another in memory, each channel's rows one
/// run of the block; an interleaved band is one region of every channel. The block's checksum, when the unit states
/// one, is that of the zeros the reservation wrote until `finish` has the editor compute it over the rows written.
class XisfReservedRows final : public CodecReservedRows
{
public:
    XisfReservedRows(std::unique_ptr<openxisf::editor> editor, std::string path) noexcept
        : m_editor(std::move(editor)), m_path(std::move(path))
    {
    }

    Status writeRows(const ExecutionContext& execution, std::uint32_t firstRow, const ImageLayout& layout,
                     std::span<const std::byte> samples) override
    {
        auto written = evaluateAcrossBoundary(kXisfBoundary, [&]() -> Result<std::uint64_t> {
            try
            {
                const auto& band = layout.extent();
                const bool planar = layout.planeCount() > 1u || band.channelCount() == 1u;
                const std::uint64_t rowBytes =
                    static_cast<std::uint64_t>(band.width()) * layout.samplesPerPixel() * layout.elementSizeBytes();
                openxisf::pixel_write_options options;
                // The reservation wrote zeros, whose digest the editor need not verify before it replaces them: the
                // transaction publishes the unit only once every row has been written.
                options.verify_checksum = false;
                options.progress = [&execution](std::uint64_t, std::uint64_t) {
                    return !execution.checkCancellation().failed();
                };
                // The rows are given where they lie, through a pitch: a layout's stride is a whole number of samples.
                options.pitch = layout.rowStrideBytes() / layout.elementSizeBytes();
                for (std::uint32_t plane = 0u; plane < layout.planeCount(); ++plane)
                {
                    options.region = openxisf::pixel_region{.origin = {0u, firstRow},
                                                            .size = {band.width(), band.height()},
                                                            .first_channel = planar ? plane : 0u,
                                                            .channel_count = planar ? 1u : band.channelCount()};
                    const std::uint64_t span = (band.height() - 1u) * layout.rowStrideBytes() + rowBytes;
                    m_editor->write_pixels(
                        0u, std::span<const std::byte>(samples.data() + plane * layout.planeStrideBytes(), span),
                        options);
                }
                return rowBytes * band.height() * layout.planeCount();
            }
            catch (const openxisf::cancelled_error&)
            {
                const auto stopped = execution.checkCancellation();
                return stopped.failed() ? stopped : Status::failure(ErrorCode::Cancelled);
            }
            catch (const openxisf::error& error)
            {
                return xisfLibraryFailure(error, XisfStage::Write, "write XISF rows", m_path);
            }
        });
        if (!written.hasValue())
            return written.status();
        return written->hasValue() ? Status::success() : written->status();
    }

    Result<std::uint64_t> finish(const ExecutionContext& execution) override
    {
        if (const auto stopped = execution.checkCancellation(); stopped.failed())
            return stopped;
        auto finished = evaluateAcrossBoundary(kXisfBoundary, [&]() -> Result<std::uint64_t> {
            try
            {
                // The digests of the blocks written, computed from their bytes in the file and written in place, and
                // the device flushed; the editor then closes the file.
                m_editor->commit();
                m_editor.reset();
            }
            catch (const openxisf::error& error)
            {
                return xisfLibraryFailure(error, XisfStage::Write, "finish XISF rows", m_path);
            }
            std::error_code sizeError;
            const auto size =
                std::filesystem::file_size(std::filesystem::path(std::u8string_view(
                                               reinterpret_cast<const char8_t*>(m_path.data()), m_path.size())),
                                           sizeError);
            if (sizeError)
                return Status::failure(ErrorCode::ExternalFailure);
            return static_cast<std::uint64_t>(size);
        });
        if (!finished.hasValue())
            return finished.status();
        return std::move(*finished);
    }

private:
    std::unique_ptr<openxisf::editor> m_editor;
    std::string m_path;
};
} // namespace

Result<CodecEncodeReport> writeXisf(const EngineContext& engine, const ExecutionContext& execution,
                                    const CodecEncodeRequest& request, const RawFrame& frame,
                                    const ImageMetadata& metadata)
{
    const auto descriptor = *frame.descriptor();
    const auto layout = *frame.layout();
    const auto format = descriptor.sampleFormat();
    const auto sampleFormat = xisfSampleFormatOf(format);
    const auto arrangement = storageArrangementOf(layout);
    const auto codec = outputCodec(request.intent != nullptr ? request.intent->compression() : OutputCompression::None);
    const auto checksum = outputChecksum(request.intent != nullptr ? request.intent->checksum() : OutputChecksum::None);
    // Every refusal is decided before the pending file is opened. XISF states no scale, no offset
    // and no byte order of its own, and its row zero is the image's top row, so an intent asking
    // for any of those describes a file this container cannot hold.
    if (!sampleFormat || !codec || arrangement == StorageArrangement::SemiPlanar ||
        descriptor.coordinateOrigin() != CoordinateOrigin::TopLeft ||
        (request.intent != nullptr &&
         (request.intent->scaling() != OutputScaling::None || request.intent->byteOrder() == ByteOrder::Big)))
        return codecFailure(ErrorCode::Unsupported, CodecFailure::UnsupportedRepresentation, CodecFormat::Xisf,
                            "encode XISF options", request.pendingPath);
    auto cards = outputCards(request, metadata);
    if (!cards.hasValue())
        return cards.status();
    const auto& extent = descriptor.extent();
    const auto rowBytes = static_cast<std::uint64_t>(extent.width()) * layout.samplesPerPixel() * format.sizeBytes;
    const auto planes = layout.planeCount();
    const bool swapped = format.sizeBytes > 1u && descriptor.byteOrder() != ByteOrder::Native &&
                         ((descriptor.byteOrder() == ByteOrder::Big) != (nativeByteOrder() == ByteOrder::Big));
    if (const auto stopped = execution.checkCancellation(); stopped.failed())
        return stopped;
    const auto pendingPath = pendingFile(request);
    if (!pendingPath.hasValue())
        return pendingPath.status();
    const std::filesystem::path& pending = *pendingPath;
    // The library takes the samples contiguous and native. A frame already so is lent as it is; a padded or
    // foreign-ordered one is repacked once, into memory the engine counts.
    std::span<const std::byte> payload = frame.bytes();
    SampleBuffer packed;
    if (!layout.isContiguous() || swapped || payload.size() != rowBytes * extent.height() * planes)
    {
        const auto contiguous = ImageLayout::create({.extent = extent,
                                                     .elementSizeBytes = format.sizeBytes,
                                                     .samplesPerPixel = layout.samplesPerPixel(),
                                                     .planeCount = planes,
                                                     .rowAlignmentBytes = 1u},
                                                    engine.limits());
        if (!contiguous)
            return Status::failure(ErrorCode::LimitExceeded);
        auto buffer = SampleBuffer::createOwned(engine, *contiguous, StorageInitialization::Uninitialized);
        if (!buffer.hasValue())
            return buffer.status();
        packed = std::move(*buffer);
        auto target = *packed.mutableBytes();
        for (std::uint32_t plane = 0u; plane < planes; ++plane)
            for (std::uint32_t y = 0u; y < extent.height(); ++y)
            {
                if (const auto stopped = execution.checkCancellation(); stopped.failed())
                    return stopped;
                const auto* const row =
                    frame.bytes().data() + plane * layout.planeStrideBytes() + y * layout.rowStrideBytes();
                auto* const into = target.data() + (static_cast<std::uint64_t>(plane) * extent.height() + y) * rowBytes;
                std::memcpy(into, row, static_cast<std::size_t>(rowBytes));
                if (swapped)
                    for (std::uint64_t sample = 0u; sample < rowBytes; sample += format.sizeBytes)
                        std::reverse(into + sample, into + sample + format.sizeBytes);
            }
        payload = packed.bytes();
    }
    auto encoded = evaluateAcrossBoundary(kXisfBoundary, [&]() -> Result<std::uint64_t> {
        try
        {
            openxisf::write_options options = unitOptions(execution, checksum, "encode XISF");
            options.codec = *codec;
            if (*codec)
            {
                // When the intent states none: the bytes of samples of more than one byte shuffled, as PixInsight
                // writes them and which is what makes the codecs pay on them, in subblocks a region read decodes on
                // their own. An unstated level is the codec's own default.
                const auto level = request.intent != nullptr ? request.intent->compressionLevel() : std::nullopt;
                options.compression_level = level ? libraryLevel(**codec, *level) : 0;
                const auto shuffle = request.intent != nullptr ? request.intent->byteShuffle() : std::nullopt;
                options.byte_shuffle = shuffle.value_or(format.sizeBytes > 1u);
                const auto block = request.intent != nullptr ? request.intent->compressionBlockBytes() : std::nullopt;
                options.subblock_size = block.value_or(kDefaultSubblockBytes);
            }
            openxisf::writer writer(std::move(options));
            writer.add_image(outputImage(request, descriptor, arrangement, metadata, *sampleFormat, *cards), payload);
            // Opened for reading as well, which a blank prolog needs to find what it blanks.
            std::fstream output(pending, std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc);
            if (!output.is_open())
                return xisfFailure(ErrorCode::ExternalFailure, CodecFailure::DestinationUnavailable,
                                   "create XISF pending output", request.pendingPath,
                                   "the pending output path could not be opened for writing");
            // The engine's transaction owns the pending name and the one rename that publishes it, so the unit is
            // written into that file as it stands. The sink can rewrite, so the header goes last, once the sizes
            // of the blocks it lists are known, and no block is held in memory to wait for it.
            openxisf::ostream_sink sink(output);
            writer.save(sink);
            if (request.headerProlog == OutputHeaderProlog::Blank)
            {
                output.flush();
                if (Status blanked = blankHeaderProlog(output); blanked.failed())
                    return blanked;
            }
            output.close();
            if (output.fail())
                return xisfFailure(ErrorCode::ExternalFailure, CodecFailure::ShortWrite, "write XISF output",
                                   request.pendingPath, "the pending XISF payload could not be written in full");
            std::error_code sizeError;
            const auto size = std::filesystem::file_size(pending, sizeError);
            if (sizeError)
                return Status::failure(ErrorCode::ExternalFailure);
            return static_cast<std::uint64_t>(size);
        }
        catch (const openxisf::cancelled_error&)
        {
            auto stopped = execution.checkCancellation();
            return stopped.failed() ? stopped : Status::failure(ErrorCode::Cancelled);
        }
        catch (const openxisf::error& error)
        {
            return xisfLibraryFailure(error, XisfStage::Write, "encode XISF image", request.pendingPath);
        }
    });
    if (!encoded.hasValue())
        return codecFailure(encoded.status().code(), CodecFailure::CallbackFailure, CodecFormat::Xisf,
                            "encode XISF image", request.pendingPath, encoded.status());
    if (!encoded->hasValue())
        return encoded->status();
    return CodecEncodeReport{**encoded};
}

Result<std::unique_ptr<CodecReservedRows>> reserveXisf(const EngineContext& engine, const ExecutionContext& execution,
                                                       const CodecEncodeRequest& request,
                                                       const RawFrameDescriptor& descriptor,
                                                       StorageArrangement arrangement, const ImageMetadata& metadata)
{
    using Reserved = Result<std::unique_ptr<CodecReservedRows>>;
    const auto sampleFormat = xisfSampleFormatOf(descriptor.sampleFormat());
    const auto checksum = outputChecksum(request.intent != nullptr ? request.intent->checksum() : OutputChecksum::None);
    // Every refusal is decided before the pending file is opened, as for a whole write; and a reserved image is
    // written uncompressed, the one form whose samples can be written where they lie, so an intent stating a codec
    // describes a unit this path cannot fill.
    if (!sampleFormat || arrangement == StorageArrangement::SemiPlanar ||
        descriptor.coordinateOrigin() != CoordinateOrigin::TopLeft ||
        (request.intent != nullptr &&
         (request.intent->scaling() != OutputScaling::None || request.intent->byteOrder() == ByteOrder::Big ||
          request.intent->compression() != OutputCompression::None)))
        return codecFailure(ErrorCode::Unsupported, CodecFailure::UnsupportedRepresentation, CodecFormat::Xisf,
                            "reserve XISF options", request.pendingPath);
    auto cards = outputCards(request, metadata);
    if (!cards.hasValue())
        return cards.status();
    if (const auto stopped = execution.checkCancellation(); stopped.failed())
        return stopped;
    const auto pending = pendingFile(request);
    if (!pending.hasValue())
        return pending.status();
    auto reserved = evaluateAcrossBoundary(kXisfBoundary, [&]() -> Reserved {
        try
        {
            // The unit with its one image reserved: written whole, header and all, with zeros where the samples go,
            // then opened in an editor that keeps the file until the transaction finishes it.
            openxisf::writer writer(unitOptions(execution, checksum, "reserve XISF"));
            (void)writer.add_reserved_image(
                outputImage(request, descriptor, arrangement, metadata, *sampleFormat, *cards));
            {
                std::ofstream output(*pending, std::ios::binary | std::ios::trunc);
                if (!output.is_open())
                    return xisfFailure(ErrorCode::ExternalFailure, CodecFailure::DestinationUnavailable,
                                       "create XISF pending output", request.pendingPath,
                                       "the pending output path could not be opened for writing");
                openxisf::ostream_sink sink(output);
                writer.save(sink);
                output.close();
                if (output.fail())
                    return xisfFailure(ErrorCode::ExternalFailure, CodecFailure::ShortWrite, "reserve XISF output",
                                       request.pendingPath, "the pending XISF payload could not be written in full");
            }
            openxisf::read_options options;
            options.header_only = true;
            options.limits.max_header_size = kMaxXisfHeaderBytes;
            options.limits.max_allocation = engine.limits().maxImageBytes;
            auto editor = std::make_unique<openxisf::editor>(request.pendingPath, std::move(options));
            if (editor->images().size() != 1u)
                return xisfFailure(ErrorCode::Internal, CodecFailure::DescriptorMismatch, "reserve XISF output",
                                   request.pendingPath, "the reserved unit does not hold the one image written");
            return std::unique_ptr<CodecReservedRows>(
                std::make_unique<XisfReservedRows>(std::move(editor), std::string(request.pendingPath)));
        }
        catch (const openxisf::cancelled_error&)
        {
            auto stopped = execution.checkCancellation();
            return stopped.failed() ? stopped : Status::failure(ErrorCode::Cancelled);
        }
        catch (const openxisf::error& error)
        {
            return xisfLibraryFailure(error, XisfStage::Write, "reserve XISF image", request.pendingPath);
        }
    });
    if (!reserved.hasValue())
        return codecFailure(reserved.status().code(), CodecFailure::CallbackFailure, CodecFormat::Xisf,
                            "reserve XISF image", request.pendingPath, reserved.status());
    if (!reserved->hasValue())
        return reserved->status();
    return std::move(**reserved);
}
} // namespace an
