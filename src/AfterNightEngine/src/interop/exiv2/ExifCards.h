// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/CodecMetadata.h>

#include "image/CardContinuation.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// AfterNightEngine - Exif, as cards, and nothing of the library that reads it.
//
// The three raster containers state their acquisition facts in one vocabulary, and it is neither
// theirs nor this engine's: an Exif block is a TIFF structure that a JPEG carries in a marker, a
// PNG in a chunk, and a TIFF in its own directories. Reading and writing that structure is a
// solved problem with a mature implementation, so this adapter translates between that library's
// metadata model and preserved cards, and its types appear nowhere else in the engine.
//
// **A card carries the field's own text and its Exif type.** The value is the library's own
// rendering, which is what its reader parses back, and the type the field was read in travels in
// the card's comment - so a short that a file wrote as a short is written back as one rather than
// as whatever the tag's default happens to be.
//
// **A field longer than one card continues in the next.** An embedded colour profile, a camera's
// thumbnail and a packet of rights metadata are all longer than the four thousand bytes one card
// holds. Splitting the text across consecutive cards of the same keyword is what FITS does with a
// long string, and it keeps every one of those fields inspectable rather than an opaque binary blob.

namespace an
{

/// The comment a card carries when it continues the value of the card before it: the rule every container whose
/// fields can outgrow one card shares.
inline constexpr std::string_view kExifContinuedComment = kContinuedCardComment;

/// The keyword under which a thumbnail's own bytes are preserved.
///
/// The offset the field holds is a fact about the file the thumbnail was read from and means
/// nothing anywhere else, so what is preserved under that keyword is the thumbnail itself, rendered
/// the way the library renders any block of bytes.
inline constexpr std::string_view kExifThumbnailKeyword = "Exif.Thumbnail.JPEGInterchangeFormat";

/// Appends one field as cards, continuing into further cards when its text does not fit one.
///
/// The continuation rule the containers share, so that a long field is preserved rather than
/// truncated or dropped. Each card holds at most `kMaxCodecCardTextBytes` bytes and never splits a
/// UTF-8 character across two cards; an empty value is one empty card. The first card carries the
/// stated comment and each further card carries `kExifContinuedComment`; a reader joins them in
/// order.
///
/// @param cards where to append.
/// @param origin the namespace the container's cards belong to.
/// @param keyword the field's key.
/// @param text the field's complete value, which may be empty.
/// @param comment what to write beside the first card.
/// @return success; `ErrorCode::LimitExceeded` when the list would hold more than `kMaxCodecCards`
///         cards, naming the keyword and the bound; or whatever building a card reported. Cards
///         appended before a failure stay in the list.
[[nodiscard]] Status appendContinuedCard(std::vector<CodecCard>& cards, CodecNamespace origin,
                                         const std::string& keyword, const std::string& text,
                                         const std::string& comment);

/// Whether a keyword names an Exif field rather than one of a container's own text entries.
///
/// Thread-safe and allocation-free.
///
/// @param keyword the card's keyword.
/// @return true when the keyword begins with `Exif.`, the metadata library's prefix for an Exif
///         field.
[[nodiscard]] bool isExifKeyword(std::string_view keyword) noexcept;

/// Reads a binary Exif block into cards of one container's namespace.
///
/// Every field becomes a card in the library's own rendering, with its Exif type as the comment,
/// except the fields that describe the source file's own structure - strip and tile offsets and
/// counts, the pointers to the sub-directories, the maker note and the thumbnail's offset and
/// length - which mean nothing outside that file and are dropped. A thumbnail is preserved as its
/// own bytes under `kExifThumbnailKeyword`, after every other field.
///
/// @param origin the namespace the reading container's cards belong to.
/// @param block the block, starting at its own byte-order mark, without any container prefix.
/// @return the cards in the order the block states them, and none for an empty block; or a typed
///         failure translated from an exception the metadata library threw, or
///         `ErrorCode::LimitExceeded` when the fields need more cards than one image preserves. A
///         block the library parses to nothing without throwing yields no cards.
[[nodiscard]] Result<std::vector<CodecCard>> readExifBlock(CodecNamespace origin, std::span<const std::byte> block);

/// Reads the Exif a complete TIFF file states, across its directories.
///
/// The same translation as `readExifBlock`, over the whole container.
///
/// @param file the whole container; the structure addresses its own bytes by absolute offset, so a
///        partial read cannot be proved to reach every field. Writable, because the root
///        directory's XMLPacket entries are removed where they stand rather than in a copy of the
///        file; nothing else in it is written.
/// @return the cards in `CodecNamespace::Tiff`, and none for an empty file; or a typed failure, as
///         `readExifBlock` reports one.
[[nodiscard]] Result<std::vector<CodecCard>> readTiffExifCards(std::span<std::byte> file);

/// Reads the Exif a camera RAW file states, from the structure its container keeps it in.
///
/// The same translation as `readExifBlock`. A container that is a TIFF structure - the classic TIFF
/// header of a NEF, DNG, ARW, PEF or SRW file, a CR2 file's, or the ORF and RW2 dialects of it - is
/// read across its directories with the library's parser for that container, maker notes included; a
/// RAF file's Exif is the block in the JPEG preview it embeds. The root directory's XMP packet is
/// removed first, as it is for a TIFF. Any other container - CR3 and CRW among them - states no Exif
/// this adapter reads.
///
/// @param file the whole file; the structure addresses its own bytes by absolute offset. Writable,
///        as for `readTiffExifCards`; nothing but the root directory's XMLPacket entries is written.
/// @return the cards in `CodecNamespace::CameraRaw`, and none for a container this adapter does not
///         read; or a typed failure, as `readExifBlock` reports one.
[[nodiscard]] Result<std::vector<CodecCard>> readCameraRawExifCards(std::span<std::byte> file);

/// Writes cards back as a binary Exif block, little-endian, for a marker or a chunk.
///
/// Continuation cards are joined first. Cards that are not Exif keywords are ignored, as are
/// keywords the library does not know and fields whose text it cannot read back in the type the
/// card states - or in the keyword's default type when the card states none. A decimal number in a
/// field whose type is a ratio is written as a ratio with at most six decimal places.
///
/// @param cards the cards to state, continuations included.
/// @return the block, starting at its byte-order mark, or a typed failure. An empty result means
///         there was nothing to write.
[[nodiscard]] Result<std::vector<std::byte>> writeExifBlock(std::span<const CodecCard> cards);

/// One Exif field a card set states, whole, with what the metadata library says of its tag.
///
/// The interop layer's own record of `ExifField`, which the IO layer publishes; the members mean
/// what that type's members mean.
struct ExifFieldFacts
{
    /// The field's key.
    std::string keyword{};

    /// The field's complete value, its continuation cards joined.
    std::string value{};

    /// The Exif type the field was read in, or the key's default type; empty for an unknown key.
    std::string typeName{};

    /// The tag number; zero for an unknown key.
    std::uint16_t tag = 0u;

    /// The tag's name; empty for an unknown key.
    std::string tagName{};

    /// The tag's label; empty for an unknown key or a tag without one.
    std::string tagLabel{};

    /// The tag's description; empty for an unknown key or a tag without one.
    std::string tagDescription{};

    /// How many components the value holds read in its type; zero when it cannot be read in it.
    std::uint64_t count = 0u;
};

/// Describes the Exif fields a card set states, each whole, as the metadata library names them.
///
/// Cards that are not Exif keys are skipped; continuation cards are joined as `writeExifBlock`
/// joins them; a key the library does not know is still a field, with its key, value and stated
/// type. The order is the cards' order.
///
/// @param cards the cards, continuations included.
/// @return the fields, or a typed failure translated from an exception the library threw.
[[nodiscard]] Result<std::vector<ExifFieldFacts>> describeExifCards(std::span<const CodecCard> cards);

/// What embedding Exif in a complete TIFF file produced.
struct TiffExifRewrite
{
    /// The complete new container, when one had to be built; empty otherwise.
    std::vector<std::byte> bytes{};

    /// Whether `bytes` replaces the file, or the file's own buffer was updated where it stood.
    bool rewritten = false;
};

/// States cards in a complete TIFF file's directories, keeping everything the file already said.
///
/// The file is read first, so a tag the encoder wrote is retained rather than deleted; the cards
/// are then added or replaced, and the library decides whether it can update the container where it
/// stands or has to build a new one.
///
/// @param file the whole container, which may be updated in place.
/// @param cards the cards to state, applied as `writeExifBlock` applies them.
/// @return what to publish: a new container in `bytes` when `rewritten` is set, and otherwise
///         nothing, because `file` itself now holds the result; a bare `ErrorCode::InvalidArgument`
///         for an empty file; or a typed failure translated from the metadata library.
[[nodiscard]] Result<TiffExifRewrite> embedTiffExifCards(std::span<std::byte> file, std::span<const CodecCard> cards);

} // namespace an
