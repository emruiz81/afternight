// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What kind of frame a file says it is, read from the cards the file's codec preserved.
///
/// A calibration workflow sorts its inputs into lights, darks, flats and biases before it does anything
/// with them, and the file is the authority on which is which: a capture program writes the kind into a
/// card. Programs do not agree on the card. Most write `IMAGETYP`; INDI drivers, and the programs built on
/// them, write `FRAME`; an XISF file states the kind as its image's `imageType` attribute, which the XISF
/// codec preserves as an `ImageType` card in the XISF namespace. Nor do they agree on the spelling -
/// `Light`, `LIGHT`, `Light Frame`, `Science`, `Master Dark`, `Flat Field`, `Bias Frame`, `Zero`, `Offset`.
///
/// This is the one reading of all of them, as a function of a card set, in the style of
/// `IO/ObservationCards.h`. It answers with the closed `FrameType` of `IO/CodecMetadataProjection.h` and
/// says which card decided. `AcquisitionMetadata` has no frame kind and gains none: the kind is a statement
/// a file makes about itself, and how a workflow files a frame is the workflow's decision. When no card
/// states a kind, what the caller does instead - a folder name, the user's choice, a default - is the
/// caller's fallback, stated at the call rather than buried in this reading.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/IO/CodecMetadataProjection.h>
#include <AfterNightEngine/Image/CodecMetadata.h>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace an
{

/// Which card stated a frame's kind.
enum class FrameTypeCard : std::uint8_t
{
    /// No card stated a kind this build recognises.
    None,

    /// The FITS `IMAGETYP` card, which most capture programs write.
    ImageType,

    /// The FITS `FRAME` card, which INDI drivers write.
    Frame,

    /// The XISF image's `imageType` attribute, preserved as the XISF-namespace `ImageType` card.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    XisfImageType
};

/// Whether a value is one of the cards this build reads a frame's kind from.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param card the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isFrameTypeCard(FrameTypeCard card) noexcept
{
    return static_cast<std::uint8_t>(card) <= static_cast<std::uint8_t>(FrameTypeCard::XisfImageType);
}

/// The card's own name, for a log line, a diagnostic field or a report.
///
/// Thread-safe and allocation-free.
///
/// @param card the card to name. A value outside the enumeration is named `"UnknownFrameTypeCard"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller and is
///         never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* frameTypeCardName(FrameTypeCard card) noexcept;

/// A frame's kind as its cards state it, and the card that stated it.
struct FrameTypeReading
{
    /// The kind the deciding card names; `FrameType::Unknown` when no card names one.
    FrameType type = FrameType::Unknown;

    /// The card that decided; `FrameTypeCard::None` when none did.
    FrameTypeCard card = FrameTypeCard::None;

    /// The deciding card's value exactly as the file wrote it, quotes and padding included; empty when no
    /// card decided.
    std::string spelling{};

    /// Whether two readings say the same thing.
    ///
    /// @param left one reading.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend bool operator==(const FrameTypeReading& left, const FrameTypeReading& right) = default;
};

/// The keywords a frame's kind is read from, in the order they are consulted.
///
/// `IMAGETYP` and `FRAME` in the FITS namespace, then `ImageType` in the XISF namespace. The order is the
/// precedence: a file that states the kind twice is read from the first.
///
/// Thread-safe and allocation-free.
///
/// @return the three keywords, as process-lifetime literals, in the order of `FrameTypeCard` from
///         `FrameTypeCard::ImageType`.
[[nodiscard]] AFTERNIGHTENGINE_API std::span<const std::string_view> frameTypeKeywords() noexcept;

/// The kind a card's value names.
///
/// The value is compared without regard to case and searched for words in a fixed order, the first that
/// appears deciding: `BIAS`, `ZERO` or `OFFSET` name a bias; then `DARK` a dark; then `FLAT` a flat; then
/// `LIGHT` or `SCIENCE` a light. A word may appear anywhere, so `Master Dark`, `'Light Frame'` and
/// `MasterFlat` read as their kinds, and a dark taken for flats - `Dark Flat`, `FlatDark` - reads as a dark.
/// Anything else - an empty value, `-`, `DefectMap` - names no kind.
///
/// Thread-safe, allocation-free and never fails.
///
/// @param text a card's value, as the file wrote it.
/// @return the kind, or `FrameType::Unknown`.
[[nodiscard]] AFTERNIGHTENGINE_API FrameType classifyFrameTypeText(std::string_view text) noexcept;

/// Reads a frame's kind from the cards a file's codec preserved.
///
/// The keywords of `frameTypeKeywords` are consulted in order, and for each the first card of that keyword
/// in its namespace; the first whose value `classifyFrameTypeText` reads as a kind decides. A card whose
/// value names no kind is passed over, so an unreadable `IMAGETYP` does not hide a readable `FRAME`. An
/// XISF `ImageType` of `Light` is passed over too: it is the attribute's default, and the codec preserves a
/// default it read exactly as a value the file stated, so it says nothing.
///
/// Thread-safe with respect to its argument, which is only read. Allocates the returned spelling.
///
/// @param codec the cards a codec preserved.
/// @return the kind, the card that stated it and that card's value; a reading of `FrameType::Unknown` and
///         `FrameTypeCard::None` when no card states a kind.
[[nodiscard]] AFTERNIGHTENGINE_API FrameTypeReading readFrameTypeCards(const CodecMetadata& codec);

/// The kind a file's cards state, or the caller's fallback when none states one.
///
/// Thread-safe and allocation-free: `readFrameTypeCards` without the spelling.
///
/// @param codec the cards a codec preserved.
/// @param fallback what the caller takes a frame to be when its cards say nothing; may be
///        `FrameType::Unknown`.
/// @return the stated kind, or `fallback`.
[[nodiscard]] AFTERNIGHTENGINE_API FrameType resolveFrameType(const CodecMetadata& codec, FrameType fallback) noexcept;

} // namespace an
