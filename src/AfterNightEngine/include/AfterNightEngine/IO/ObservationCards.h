// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The observing context a derived FITS product carries forward from its source.
///
/// A stacked master, a converted frame or a cropped copy is a new file, and this engine's FITS writer
/// states in it only what the image says about itself: its structure, its identity and the cards an
/// output intent names. The observing context of the frame a product came from - where the telescope
/// stood and pointed, which camera, lens, filter and settings took the exposure, and when - reaches the
/// new header only through this carry: copied from the source's FITS cards when the source was a FITS
/// file, and derived from the source's Exif when the source was a camera file.
///
/// The carry is a function of two card sets. A product states its context because a caller asked for
/// it, and never because a writer inherited a header it had no reason to trust.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/CodecMetadata.h>

#include <cstdint>
#include <span>
#include <string_view>

namespace an
{

/// What happens to an observing-context card the destination already states.
enum class ObservationCardPolicy : std::uint8_t
{
    /// A destination card that states a value is kept, and only an absent or empty one is written. The
    /// default.
    KeepExisting,

    /// Every fact the source states is written, replacing the value the destination held.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Replace
};

/// Whether a value names a policy this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param policy the value to inspect.
/// @return true for either policy.
[[nodiscard]] constexpr bool isObservationCardPolicy(ObservationCardPolicy policy) noexcept
{
    return static_cast<std::uint8_t>(policy) <= static_cast<std::uint8_t>(ObservationCardPolicy::Replace);
}

/// The FITS keywords whose value a FITS source hands over as it wrote it.
///
/// The telescope and its optics, the binning, the site, where the mount pointed and how it was set up,
/// the camera's identity and set temperature, and the filter wheel and focuser. Layout and world
/// coordinates are absent on purpose: a product's geometry is its own.
///
/// Thread-safe and allocation-free.
///
/// @return the forty-four keywords in the order they are written, as process-lifetime literals.
[[nodiscard]] AFTERNIGHTENGINE_API std::span<const std::string_view> copiedObservationKeywords() noexcept;

/// States a source's observing context as FITS cards on a destination's cards.
///
/// A card's value is read with quotes, spaces, dots, tabs and line breaks stripped from both ends, and a
/// value that is then empty or `-` states nothing. Exif keywords are looked up in the `Jpeg`, `Tiff`,
/// `Png` and `CameraRaw` namespaces in that order, and FITS keywords in the `Fits` namespace. Where a
/// fact has an Exif spelling and a FITS spelling, the Exif one is read when the source holds any Exif
/// card - a card of those four namespaces whose keyword begins with `Exif.` - and the FITS one otherwise.
/// The facts, in the order they are written:
///
/// 1. When the source holds any FITS card: each of `copiedObservationKeywords` and `OBSGEO-H` as the
///    source wrote it, and `CAMMAKE`, `LENSMODE`, `DATE-OBS` and `DATE-LOC` as strings.
/// 2. `EXPOSURE`, in seconds, from `Exif.Photo.ExposureTime` or from `EXPOSURE` then `EXPTIME`, unless the
///    destination states `EXPOSURE` or `EXPTIME`.
/// 3. `ISOSPEED` from `Exif.Photo.ISOSpeedRatings` or `ISOSPEED`, or else `GAIN` from `GAIN`, unless the
///    destination states either.
/// 4. `OFFSET` from `OFFSET`, when the source holds no Exif card.
/// 5. `INSTRUME` from `Exif.Image.Model` or `INSTRUME`.
/// 6. `FILTER` from `FILTER`; `IMAGETYP` from `IMAGETYP` in upper case, with `OFFSET` read as `BIAS`; and
///    `OBJECT` from `OBJECT`.
/// 7. `SWCREATE` from `Exif.Image.Software` or from `SOFTWARE` then `SWCREATE`.
/// 8. `CCD-TEMP` from `Exif.MakerNote.SensorTemperature` or `CCD-TEMP`.
/// 9. `DATE-LOC` and then `DATE-OBS` from `Exif.Photo.DateTimeOriginal` then `Exif.Photo.DateTimeDigitized`,
///    or from `DATE` then `DATE-LOC`.
/// 10. `DATE-OBS` from `Exif.GPSInfo.GPSDateStamp` and `Exif.GPSInfo.GPSTimeStamp`.
/// 11. `CAMMAKE` from `Exif.Image.Make`, and `LENSMODE` from `Exif.Photo.LensModel`, `Exif.Image.LensModel`
///     then `Exif.CanonCs.LensModel`.
/// 12. `FOCALLEN` from `Exif.Photo.FocalLength`, and `APTDIA` and `APTAREA` from it over
///     `Exif.Photo.FNumber`.
/// 13. `XPIXSZ` and `YPIXSZ` in micrometres from `Exif.Photo.FocalPlaneXResolution` and
///     `Exif.Photo.FocalPlaneYResolution` in `Exif.Photo.FocalPlaneResolutionUnit`.
/// 14. `SITELAT`, `SITELONG` and `SITEELEV` from `Exif.GPSInfo.GPSLatitude`, `Exif.GPSInfo.GPSLongitude`
///     and `Exif.GPSInfo.GPSAltitude` with their references.
///
/// A number is read as a decimal or as a ratio of two; an integer as the one its text begins with; a GPS
/// position as degrees, minutes and seconds, negative for a reference beginning `S` or `W` and for an
/// altitude reference of 1; a date and time as `YYYY:MM:DD hh:mm:ss`, `YYYY-MM-DD hh:mm:ss` or
/// `YYYY-MM-DDThh:mm:ss`; and a GPS time as a `YYYY:MM:DD` or `YYYY-MM-DD` date and up to three numbers
/// rounded to whole hours, minutes and seconds. Text that does not hold its kind of value, and a date or
/// time that names no instant, states nothing. A date and time is written as `YYYY-MM-DDThh:mm:ss`; a
/// derived number with at most six decimals and no trailing zero; an integer, a sensor temperature and a
/// copied card as they read; and every other value as a quoted FITS string with each quote inside it
/// doubled, unless it arrived quoted.
///
/// A fact is written when the destination's first FITS card of its keyword states nothing, or always under
/// `ObservationCardPolicy::Replace`. Writing replaces that first card's value in place, keeping its
/// comment, and removes the keyword's later cards; a keyword the destination lacks is appended with no
/// comment. Facts are written in turn, so a card an earlier fact wrote is one a later fact finds. Cards of
/// other namespaces are kept as they are.
///
/// Thread-safe with respect to both arguments, which are only read.
///
/// @param destination the cards the product carries already.
/// @param source the preserved cards of the frame the product came from.
/// @param policy whether a value the destination states is replaced.
/// @return the destination's cards with the context stated; `ErrorCode::InvalidArgument` for a policy
///         outside its enumeration; or `ErrorCode::LimitExceeded` carrying the keyword, the size and the
///         bound when a value is longer than one card holds or the cards would be more than one image
///         preserves.
[[nodiscard]] AFTERNIGHTENGINE_API Result<CodecMetadata>
withObservationCards(const CodecMetadata& destination, const CodecMetadata& source,
                     ObservationCardPolicy policy = ObservationCardPolicy::KeepExisting);

} // namespace an
