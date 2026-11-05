// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Typed acquisition facts read out of preserved cards, and written back.
///
/// A file spells an exposure time as `EXPOSURE`, or `EXPTIME`, or `Exif.Photo.ExposureTime` with a
/// fraction in it. Answering "how long was the shutter open" from those spellings is the reading
/// half of a bridge, and it belongs at the codec boundary rather than inside the metadata: the
/// typed record states a number, the cards state what a file wrote, and this is the table that
/// says which spelling of a card answers which typed question and in what order of precedence.
///
/// The table is read in both directions. Projecting cards into facts is what a decoder does once,
/// so that nothing downstream searches cards; stating facts as cards is what an output intent does
/// so that a written file spells them the way its format expects. Both directions are exact where
/// the format allows it and say so where it does not - an Exif exposure shorter than a second is
/// written as the fraction a camera would have written.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/AcquisitionMetadata.h>
#include <AfterNightEngine/Image/CodecMetadata.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace an
{

/// What a frame was taken as, as its file declared it.
///
/// The four kinds a calibration workflow sorts by. A file spells them freely - `LIGHT`, `Light
/// Frame`, `Flat Field`, `Master Dark` - and the projection reads the kind out of the spelling
/// while keeping the spelling beside it.
enum class FrameType : std::uint8_t
{
    /// The file did not say, or said something this engine does not recognise.
    Unknown,

    /// An exposure of the target.
    Light,

    /// An exposure with the shutter closed, at the light's exposure time.
    Dark,

    /// An exposure of an evenly lit field.
    Flat,

    /// A zero-length exposure. A file that says `OFFSET` means this.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Bias
};

/// Whether a value is one of the frame kinds this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param type the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isFrameType(FrameType type) noexcept
{
    return static_cast<std::uint8_t>(type) <= static_cast<std::uint8_t>(FrameType::Bias);
}

/// The frame kind's own name, for a log line, a diagnostic field or a report.
///
/// Thread-safe and allocation-free.
///
/// @param type the kind to name. A value outside the enumeration is named `"UnknownFrameType"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* frameTypeName(FrameType type) noexcept;

/// The typed facts a card set states about how a frame was acquired.
///
/// The acquisition request carries what `AcquisitionMetadata` models; the members beside it are
/// the camera settings and file facts the typed record has no field for. Every member is absent or
/// empty when no consulted card stated it.
///
/// Every member of `acquisition` is one `AcquisitionMetadata::create` accepts. A stated number the
/// record refuses - an exposure or an electron gain that is not positive, a temperature at or below
/// absolute zero, a binning of zero - is treated as a card whose value could not be read: counted as
/// malformed and skipped, so a later spelling may still answer. Capture programs write `EXPTIME = 0`
/// for a bias frame, and a decoder builds the record from this projection, so a value handed on
/// unchecked would make a readable frame unreadable over one descriptive card. The card itself stays
/// in the preserved set exactly as the file wrote it, and the record's own `create` is what decides,
/// so the projection and the record cannot come to disagree.
struct AcquisitionProjection
{
    /// What `AcquisitionMetadata` models, ready for its `create`.
    AcquisitionMetadataRequest acquisition{};

    /// The film-speed equivalent a camera reported, from `ISOSPEED` or `Exif.Photo.ISOSpeedRatings`.
    std::optional<std::int64_t> isoSpeed{};

    /// The camera's gain setting in its own units, from `GAIN`; not the electrons-per-ADU figure.
    std::optional<std::int64_t> gainSetting{};

    /// The camera's offset setting in its own units, from `OFFSET`.
    std::optional<std::int64_t> cameraOffset{};

    /// What the frame was taken as, read out of `IMAGETYP`.
    FrameType frameType = FrameType::Unknown;

    /// The spelling `IMAGETYP` carried, kept beside the kind read from it.
    std::string frameTypeText{};

    /// The software that wrote the file, from `SOFTWARE`, then `SWCREATE`, or `Exif.Image.Software`.
    std::string software{};

    /// When the file was created as the file states it, from `DATE`, then `DATE-LOC`, or
    /// `Exif.Photo.DateTimeDigitized`. Distinct from the exposure start, which is `DATE-OBS`.
    std::optional<UtcInstant> creationTime{};

    /// How many cards supplied a value.
    std::uint32_t consultedCards = 0u;

    /// How many cards had a spelling the projection knows and a value it could not read, or a value
    /// the typed record refuses. Such a card is skipped and the next spelling in precedence is
    /// consulted.
    std::uint32_t malformedCards = 0u;
};

/// Reads the typed acquisition facts a card set states.
///
/// Exif spellings are consulted first, from the Jpeg, Tiff, Png and CameraRaw namespaces alike,
/// because all four carry Exif; FITS spellings second. The Xisf, Ser and Unspecified namespaces
/// are not consulted. Within one fact the spellings are consulted in the order the members of
/// `AcquisitionProjection` document, and the first readable value wins. A string value is taken
/// without the surrounding whitespace and one pair of single quotes a FITS card carries; a number
/// is read by `parseNumericCard` and a date by `parseDateCard`.
///
/// Allocates the returned strings.
///
/// @param codec the preserved cards.
/// @return the facts stated, with the counts of cards consulted and malformed.
[[nodiscard]] AFTERNIGHTENGINE_API AcquisitionProjection projectAcquisition(const CodecMetadata& codec);

/// States typed acquisition facts as cards in a destination's vocabulary.
///
/// FITS spells every member; the Exif targets - Jpeg, Tiff and Png - spell the instrument, the
/// exposure, the sensor temperature, the film-speed equivalent, the software and the two dates,
/// and have no spelling for the rest. A member that is absent or empty produces no card. Numbers
/// are written in their shortest exact decimal form, except an Exif exposure below one second,
/// which is written as the reciprocal fraction a camera writes.
///
/// @param projection the facts to state.
/// @param target the namespace the destination's cards belong to.
/// @return the cards in a fixed order, each with a comment saying what it is;
///         `ErrorCode::Unsupported` for a namespace with no encoder vocabulary; or
///         `ErrorCode::InvalidArgument` when a text member is longer than one card carries.
[[nodiscard]] AFTERNIGHTENGINE_API Result<std::vector<CodecCard>>
acquisitionCards(const AcquisitionProjection& projection, CodecNamespace target);

/// Reads a date card as the file spelled it.
///
/// Accepts the FITS form `YYYY-MM-DDThh:mm:ss`, the same with a space in place of the `T`, the
/// Exif form `YYYY:MM:DD hh:mm:ss`, and a date alone, each with an optional fraction of a second
/// of any length after a point or a comma - digits past the ninth are dropped - and an optional
/// trailing `Z` or `z`. A time of day may instead end in the zone offset capture programs write,
/// `+hh`, `+hhmm` or `+hh:mm` and the same with `-`, less than a day, which the instant is moved
/// by to UTC; without one the instant is taken as UTC, because neither standard spelling carries a
/// zone. Surrounding whitespace and one pair of single quotes are ignored. The calendar is
/// proleptic Gregorian and the day is checked against its month.
///
/// Thread-safe and allocation-free.
///
/// @param text the card's value.
/// @return the instant, or `ErrorCode::InvalidData` for anything else.
[[nodiscard]] AFTERNIGHTENGINE_API Result<UtcInstant> parseDateCard(std::string_view text);

/// Writes an instant the way a namespace spells a date.
///
/// The FITS form for Fits, Xisf, Ser and Unspecified, with a fraction only when it is nonzero and
/// no trailing zeros in it; the Exif form, to the second, for the four raster namespaces.
///
/// Thread-safe; allocates the returned string.
///
/// @param instant the moment to write.
/// @param target the namespace the card belongs to.
/// @return the spelling.
[[nodiscard]] AFTERNIGHTENGINE_API std::string formatDateCard(UtcInstant instant, CodecNamespace target);

/// Reads a number as a card spells it.
///
/// A decimal or exponent form as `double`, or a fraction `a/b` of two such numbers, with
/// surrounding whitespace and one pair of single quotes ignored. A non-finite result, a zero
/// denominator and any trailing text are refused.
///
/// Thread-safe and allocation-free.
///
/// @param text the card's value.
/// @return the number, or `ErrorCode::InvalidData`.
[[nodiscard]] AFTERNIGHTENGINE_API Result<double> parseNumericCard(std::string_view text);

} // namespace an
