// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the file said, kept as the file said it.
///
/// A round trip through this engine must not lose what it did not understand. A FITS header carries
/// cards this engine has no field for - an observatory's own conventions, an instrument's readout
/// log, a previous program's annotations - and dropping them is a data loss a user notices only
/// after the original is gone.
///
/// So they are kept, and two rules keep them from becoming a second source of truth.
///
/// **A card never redefines a typed fact.** Where a card and a typed field disagree - a `CTYPE`
/// against the parsed astrometric solution - the typed field is what algorithms read. The card is
/// what a round trip writes back, unless an output intent overrides it. Nothing in the engine reads
/// a card to decide anything.
///
/// **A card is namespaced by where it came from.** A keyword from a FITS header and one from a TIFF
/// tag are different things that can spell the same word, and a preserved card is always
/// distinguishable from anything the engine asserts about the image.
///
/// Beside the cards sit the source's own encoding facts - how wide its samples were, in which order,
/// with what scale and offset applied to reach the values the image holds. Those are *source facts*.
/// They are here and not in an output intent for the reason this whole split exists: the current
/// engine keeps both in one bag, which is how a FITS write inherits its input's identity without
/// anybody deciding that it should.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/RawSampleFormat.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace an
{

/// Which format's vocabulary a preserved card belongs to.
///
/// The namespace half of "namespaced cards". Two formats can spell one keyword and mean different
/// things by it, and a lookup that ignored the origin would find whichever card was read first.
enum class CodecNamespace : std::uint8_t
{
    /// The card came from somewhere this engine does not name. Preserved and never interpreted.
    Unspecified,

    /// A FITS header card.
    Fits,

    /// A TIFF tag rendered as a card.
    Tiff,

    /// A PNG text chunk.
    Png,

    /// A JPEG marker or Exif field.
    Jpeg,

    /// An XISF property.
    Xisf,

    /// A camera raw maker note or tag.
    CameraRaw,

    /// A SER sequence header field.
    Ser,

    /// A video stream's technical facts in FFmpeg's own vocabulary.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Ffmpeg
};

/// Whether a value is one of the namespaces this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param codecNamespace the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isCodecNamespace(CodecNamespace codecNamespace) noexcept
{
    return static_cast<std::uint8_t>(codecNamespace) <= static_cast<std::uint8_t>(CodecNamespace::Ffmpeg);
}

/// The namespace's own name, for a log line, a diagnostic field or a report.
///
/// Thread-safe and allocation-free.
///
/// @param codecNamespace the namespace to name. A value outside the enumeration is named
///        `"UnknownCodecNamespace"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* codecNamespaceName(CodecNamespace codecNamespace) noexcept;

/// How many cards one image preserves.
///
/// A FITS header of any sanity is tens of cards and a pathological one is thousands; eight thousand
/// is above every real file and below the point where a corrupt length turns a header read into an
/// allocation.
inline constexpr std::size_t kMaxCodecCards = 8192u;

/// The longest text one card field carries, in bytes.
inline constexpr std::size_t kMaxCodecCardTextBytes = 4096u;

/// One thing a source file said that this engine did not turn into a typed fact.
///
/// Preserved verbatim. The value is text even when it looks like a number, because the engine does
/// not interpret it: interpreting it would make it a typed fact, and a typed fact belongs in the
/// object that owns it rather than here.
class CodecCard
{
public:
    /// A card from a stated namespace.
    ///
    /// @param origin which format's vocabulary the keyword belongs to. Must be one of the
    ///        enumerators.
    /// @param keyword the key. Must not be empty and must be within `kMaxCodecCardTextBytes`.
    /// @param value what the file said, as text. May be empty - a FITS card with no value is a real
    ///        card - and must be within `kMaxCodecCardTextBytes`.
    /// @param comment whatever the file wrote beside it. May be empty, and must be within
    ///        `kMaxCodecCardTextBytes`.
    /// @return the card, or `ErrorCode::InvalidArgument` naming the first field at fault, checked in
    ///         parameter order.
    [[nodiscard]] static AFTERNIGHTENGINE_API Result<CodecCard> create(CodecNamespace origin, std::string keyword,
                                                                       std::string value, std::string comment);

    /// Which format's vocabulary this keyword belongs to.
    ///
    /// @return the namespace.
    [[nodiscard]] AFTERNIGHTENGINE_API CodecNamespace origin() const noexcept;

    /// The key.
    ///
    /// @return the keyword, never empty.
    [[nodiscard]] AFTERNIGHTENGINE_API const std::string& keyword() const noexcept;

    /// What the file said.
    ///
    /// @return the value as text, which may be empty.
    [[nodiscard]] AFTERNIGHTENGINE_API const std::string& value() const noexcept;

    /// Whatever the file wrote beside it.
    ///
    /// @return the comment, which may be empty.
    [[nodiscard]] AFTERNIGHTENGINE_API const std::string& comment() const noexcept;

    /// Whether two cards say the same thing.
    ///
    /// @param left one card.
    /// @param right the other.
    /// @return true when every field is equal.
    [[nodiscard]] friend bool operator==(const CodecCard& left, const CodecCard& right) = default;

private:
    /// Builds from fields that have already been checked.
    ///
    /// @param origin which format's vocabulary the keyword belongs to.
    /// @param keyword the key.
    /// @param value what the file said.
    /// @param comment whatever the file wrote beside it.
    CodecCard(CodecNamespace origin, std::string keyword, std::string value, std::string comment);

    /// The key.
    std::string m_keyword;

    /// What the file said.
    std::string m_value;

    /// Whatever the file wrote beside it.
    std::string m_comment;

    /// Which format's vocabulary the keyword belongs to.
    CodecNamespace m_origin = CodecNamespace::Unspecified;
};

/// How a source file encoded the values this image now holds.
///
/// Source facts, every one of them, and the reason this type exists separately from an output
/// intent. A file that stored 16-bit integers with an offset says so here; whether the image is
/// written back that way is a decision somebody makes, not a value that carries itself forward.
struct SourceEncoding
{
    /// How wide one stored sample was and what kind of value it held. May name a representation
    /// this engine does not store, which is what a 64-bit integer file looks like here.
    std::optional<RawSampleFormat> sampleFormat{};

    /// In which order the file held the bytes of one sample.
    std::optional<ByteOrder> byteOrder{};

    /// The multiplier applied to stored values to reach the values the image holds. Must be finite
    /// and non-zero when stated.
    std::optional<double> scale{};

    /// The offset added after the multiplier. Must be finite when stated.
    std::optional<double> zeroOffset{};

    /// Whether two encodings describe the same source.
    ///
    /// @param left one encoding.
    /// @param right the other.
    /// @return true when every field is equal, an absent field matching only another absent one.
    [[nodiscard]] friend bool operator==(const SourceEncoding& left, const SourceEncoding& right) = default;
};

/// What the file said, kept as the file said it.
///
/// Immutable. A default-constructed one preserves nothing and claims nothing about the source
/// encoding, which is what an image that was never decoded from a file has.
class CodecMetadata
{
public:
    /// Nothing preserved and nothing claimed.
    CodecMetadata() = default;

    /// The cards and source encoding a decoder read.
    ///
    /// @param encoding how the source encoded the values. Fields left out are simply absent.
    /// @param cards what the file said. Order is preserved, because a header is written back in the
    ///        order it was read wherever an output intent does not say otherwise.
    /// @return the metadata; `ErrorCode::InvalidArgument` naming the field for a source encoding
    ///         whose sample format or byte order is outside its enumeration, whose scale is zero or
    ///         not finite, or whose offset is not finite; or `ErrorCode::LimitExceeded` carrying the
    ///         count and the bound when there are more than `kMaxCodecCards` cards. The encoding is
    ///         checked first.
    [[nodiscard]] static AFTERNIGHTENGINE_API Result<CodecMetadata> create(const SourceEncoding& encoding,
                                                                           std::vector<CodecCard> cards);

    /// How the source encoded the values this image holds.
    ///
    /// @return the source facts, which may state nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API const SourceEncoding& sourceEncoding() const noexcept;

    /// Everything preserved, in the order it was read.
    ///
    /// @return the cards, borrowed from this object and valid for as long as it is.
    [[nodiscard]] AFTERNIGHTENGINE_API std::span<const CodecCard> cards() const noexcept;

    /// The first card of a namespace with this keyword.
    ///
    /// Both halves are matched, because two formats can spell one keyword. The comparison is exact:
    /// this engine does not decide that a keyword is case-insensitive on a format's behalf.
    ///
    /// @param origin which format's vocabulary to search.
    /// @param keyword the key to find.
    /// @return the card, or null when no card of that namespace carries that keyword. Borrowed from
    ///         this object.
    [[nodiscard]] AFTERNIGHTENGINE_API const CodecCard* find(CodecNamespace origin,
                                                             std::string_view keyword) const noexcept;

    /// A second object with one more card at the end.
    ///
    /// @param card what to preserve.
    /// @return the extended metadata, or `ErrorCode::LimitExceeded` carrying the count and the
    ///         bound when this one is already full. This object is never changed.
    [[nodiscard]] AFTERNIGHTENGINE_API Result<CodecMetadata> appended(const CodecCard& card) const;

    /// Whether two objects preserve the same thing.
    ///
    /// @param left one object.
    /// @param right the other.
    /// @return true when the source encoding and every card are equal, in order.
    [[nodiscard]] friend bool operator==(const CodecMetadata& left, const CodecMetadata& right) = default;

private:
    /// How the source encoded the values this image holds.
    SourceEncoding m_sourceEncoding{};

    /// Everything preserved, in the order it was read.
    std::vector<CodecCard> m_cards{};
};

} // namespace an
