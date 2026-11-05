// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Which preserved cards a destination format writes back, decided by rule.
///
/// A source file's cards are kept so a round trip loses nothing, and that promise has an edge: a FITS
/// keyword is not a TIFF tag, an image's own structural cards describe the file it came from rather
/// than the one being written, and the identity a generated file carried belongs to that file and
/// never to a product derived from it. The rule is one function of the destination format and the
/// card, its answer is a named reason, and the allowlists are immutable tables published beside it.
///
/// Beside the rule, what a raster's preserved Exif cards say: `describeExifFields` joins a field's
/// continuation cards and names its tag as the metadata library that read it does, for a consumer that
/// hands an image's fields on whole.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/IO/CodecFormat.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/ImageMetadata.h>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace an
{

/// The vocabulary a format's own cards belong to.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param format the container.
/// @return the namespace its cards are preserved under; `CodecNamespace::Unspecified` for Dump,
///         Unknown and any value outside the enumeration.
[[nodiscard]] constexpr CodecNamespace codecNamespaceOf(CodecFormat format) noexcept
{
    switch (format)
    {
    case CodecFormat::Fits:
        return CodecNamespace::Fits;
    case CodecFormat::Xisf:
        return CodecNamespace::Xisf;
    case CodecFormat::Tiff:
        return CodecNamespace::Tiff;
    case CodecFormat::Png:
        return CodecNamespace::Png;
    case CodecFormat::Jpeg:
        return CodecNamespace::Jpeg;
    case CodecFormat::Raw:
        return CodecNamespace::CameraRaw;
    case CodecFormat::Unknown:
    case CodecFormat::Dump:
        return CodecNamespace::Unspecified;
    }
    return CodecNamespace::Unspecified;
}

/// What happens to one preserved card when an image is written to a destination format.
enum class CardDisposition : std::uint8_t
{
    /// The card is handed to the encoder.
    Written,

    /// The card belongs to a vocabulary the destination does not speak.
    ForeignNamespace,

    /// The card describes the source file's structure or encoding, which the destination decides
    /// for itself; see `isStructuralFitsKeyword`.
    StructuralKeyword,

    /// The card is an artifact or run identity, which an output transaction states or withholds and
    /// never inherits.
    IdentityKeyword,

    /// The card is in the destination's vocabulary and outside the set it writes back.
    NotInAllowlist,

    /// The destination writes no cards: it has no encoder, or its format is unknown.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    UnsupportedDestination
};

/// The disposition's own name, for a log line, a diagnostic field or a report.
///
/// Thread-safe and allocation-free.
///
/// @param disposition the value to name. A value outside the enumeration is named
///        `"UnknownCardDisposition"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* cardDispositionName(CardDisposition disposition) noexcept;

/// Whether a FITS keyword describes the file's structure or sample encoding.
///
/// The primary-header and extension structure cards, the sample scaling cards, the tile
/// compression cards and the checksum cards. Every one of them is a fact about the file that
/// carried the samples in, is recorded as a source fact where the engine needs it, and is decided
/// afresh by whatever writes the samples out. Compared exactly, in the upper-case spelling FITS
/// uses; an indexed keyword matches with any positive index.
///
/// Thread-safe and allocation-free.
///
/// @param keyword the keyword to test.
/// @return true for a structural keyword.
[[nodiscard]] AFTERNIGHTENGINE_API bool isStructuralFitsKeyword(std::string_view keyword) noexcept;

/// The Exif keys a raster destination writes back, in sorted order.
///
/// Three tables: what Photoshop emits for a JPEG and a TIFF, and a minimal set for PNG. A key that is
/// not listed is preserved on the way in and not written on the way out, which is what keeps a
/// maker note from being re-encoded by a library that cannot.
///
/// Thread-safe and allocation-free.
///
/// @param destination the container being written.
/// @return process-lifetime literals for Jpeg, Tiff and Png; empty for every other value.
[[nodiscard]] AFTERNIGHTENGINE_API std::span<const std::string_view> writableExifKeys(CodecFormat destination) noexcept;

/// One Exif field a card set states, described as the metadata library that read it names it.
///
/// A raster's Exif reaches the engine as cards - the field's key, its rendered value and the Exif type
/// it was read in - and a field longer than one card continues in the cards after it. This is the field
/// whole, with what the library knows of its tag: the number a file stores it under, the name its key
/// ends in, the label and description a person reads, and how many components its value holds. A
/// consumer that hands an image's metadata on states them from here.
struct ExifField
{
    /// The field's key, `Exif.<group>.<tag>`.
    std::string keyword{};

    /// The field's complete value as the library renders it, its continuation cards joined.
    std::string value{};

    /// The Exif type the field was read in, as the library names it - `Ascii`, `Short`, `Rational` -
    /// or, when its card states none, the type the library gives the key by default; empty when the
    /// library does not know the key.
    std::string typeName{};

    /// The tag number a file stores the field under; zero when the library does not know the key.
    std::uint16_t tag = 0u;

    /// The tag's name, the last part of its key; empty when the library does not know the key.
    std::string tagName{};

    /// The tag's label, as a person reads it; empty when the library does not know the key or has no
    /// label for it.
    std::string tagLabel{};

    /// The tag's description; empty when the library does not know the key or describes no tag.
    std::string tagDescription{};

    /// How many components the value holds read in its type - an `Ascii` value's bytes and its
    /// terminator, a `Rational` value's ratios; zero when the value cannot be read in its type.
    std::uint64_t count = 0u;
};

/// The Exif fields a card set states, each whole and described.
///
/// Cards whose keyword is not an Exif key - FITS, XISF and a container's own text entries - are
/// skipped. A field continued across consecutive cards of one key is one field, its text joined in
/// order. A key the library does not know is still a field, with its key, its value and the type its
/// card states, and nothing the library would have said of it; a value the library cannot read in its
/// type counts no components. The order is the cards' order.
///
/// Thread-safe and reentrant. Allocates the fields and their text.
///
/// @param cards the cards to read, continuations included, borrowed for the call.
/// @return the fields; or a typed failure translated from an exception the metadata library threw
///         outside one field.
[[nodiscard]] AFTERNIGHTENGINE_API Result<std::vector<ExifField>> describeExifFields(std::span<const CodecCard> cards);

/// What happens to one preserved card when an image is written to a destination format.
///
/// FITS writes its own namespace less the structural and identity keywords. XISF carries FITS
/// keywords natively, so it writes its own namespace and FITS cards under the same rules. The
/// three raster formats write Exif keys from any raster namespace - Jpeg, Tiff, Png and CameraRaw
/// all carry Exif - when the key is in the destination's allowlist. Raw, Dump and Unknown write
/// nothing.
///
/// Thread-safe and allocation-free.
///
/// @param destination the container being written.
/// @param card the preserved card.
/// @return the disposition, never a failure: a card that cannot be written has a reason.
[[nodiscard]] AFTERNIGHTENGINE_API CardDisposition cardDisposition(CodecFormat destination,
                                                                   const CodecCard& card) noexcept;

/// The cards a destination receives, and how many were withheld for each reason.
struct WritableCardReport
{
    /// The cards the encoder is handed, in source order.
    std::vector<CodecCard> cards{};

    /// Cards withheld as `CardDisposition::ForeignNamespace`.
    std::uint32_t droppedForeign = 0u;

    /// Cards withheld as `CardDisposition::StructuralKeyword`.
    std::uint32_t droppedStructural = 0u;

    /// Cards withheld as `CardDisposition::IdentityKeyword`.
    std::uint32_t droppedIdentity = 0u;

    /// Cards withheld as `CardDisposition::NotInAllowlist`.
    std::uint32_t droppedNotAllowed = 0u;

    /// Cards withheld as `CardDisposition::UnsupportedDestination`.
    std::uint32_t droppedUnsupported = 0u;
};

/// Applies `cardDisposition` to every preserved card.
///
/// Allocates the returned vector.
///
/// @param source the preserved cards.
/// @param destination the container being written.
/// @return the written cards in source order and the count withheld under each reason.
[[nodiscard]] AFTERNIGHTENGINE_API WritableCardReport writableCards(const CodecMetadata& source,
                                                                    CodecFormat destination);

/// The metadata an encoder receives for a destination.
///
/// The acquisition record, the processing meaning, the astrometric solution, the lineage and the
/// source encoding facts travel unchanged; the preserved cards are reduced to `writableCards`; and
/// the output intent is absent, because how the image is written is stated to the encoder
/// separately and never read out of the metadata it is given.
///
/// @param source the image's metadata.
/// @param destination the container being written.
/// @return the reduced aggregate, or whatever rebuilding the preserved cards reported.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ImageMetadata> outputMetadata(const ImageMetadata& source,
                                                                        CodecFormat destination);

} // namespace an
