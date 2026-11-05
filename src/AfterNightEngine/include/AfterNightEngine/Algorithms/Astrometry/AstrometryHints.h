// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What a frame says about its sky before it is solved: where it pointed, through what optics, when,
/// and where a set of frames points together.
///
/// A plate solve starts from hints. A capture program writes where the mount pointed, in one of
/// several keyword pairs; it writes the focal length and the pixel size, from which the pixel scale
/// follows; a drizzled product states its own scale; and the observation's date places a catalog's
/// moving stars where the frame saw them. A mosaic's product is centred on the mean of its panels'
/// pointings. Each of these is read here as an engine function over preserved cards.
///
/// **Cards are read, never trusted.** Only cards in the `CodecNamespace::Fits` namespace are read, and
/// the first card of a keyword is the one that speaks, as `CodecMetadata::find` decides. A value is read
/// with its blanks and a FITS string's quotes removed; a value that is not what its keyword names - a
/// pointing that is not an angle, a focal length that is not a positive finite number - states nothing,
/// and the next spelling is read instead.
///
/// **The pixel scale is the small-angle relation with the exact constant**: a pixel of `p` micrometres
/// behind `f` millimetres of focal length spans `kArcsecondsPerRadian * p / (1000 * f)` arcseconds.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/SkyCoordinate.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace an
{

/// A moment in time as nanoseconds since the Unix epoch. Defined in `Image/AcquisitionMetadata.h`.
struct UtcInstant;

/// Arcseconds in one radian: 180 times 3600 over pi.
inline constexpr double kArcsecondsPerRadian = 206264.80624709636;

/// Which pair of header keywords states a frame's pointing.
enum class PointingCards : std::uint8_t
{
    /// `CRVAL1` and `CRVAL2`: a world-coordinate solution's reference direction, which a solved
    /// frame's header already carries.
    ReferenceValue,

    /// `RA` and `DEC`, which a mount or a capture program writes.
    Pointing,

    /// `OBJCTRA` and `OBJCTDEC`, the target's coordinates, usually in hours and degrees.
    Object,

    /// `OBJRA` and `OBJDEC`, the same under shorter names.
    ObjectShort,

    /// `TELRA` and `TELDEC`, where the telescope reported it pointed.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Telescope
};

/// Whether a value names a keyword pair this build reads.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param cards the value to inspect.
/// @return true for one of the five declared values.
[[nodiscard]] constexpr bool isPointingCards(PointingCards cards) noexcept
{
    return static_cast<std::uint8_t>(cards) <= static_cast<std::uint8_t>(PointingCards::Telescope);
}

/// The pair's own name, for a log line, a diagnostic field or a failure message.
///
/// Thread-safe and allocation-free.
///
/// @param cards the value to name.
/// @return a null-terminated string literal compiled into this library; `"UnknownPointingCards"` for
///         an out-of-range cast.
[[nodiscard]] AFTERNIGHTENGINE_API const char* pointingCardsName(PointingCards cards) noexcept;

/// The two keywords of one pair.
struct PointingKeywords
{
    /// The keyword that carries the right ascension, such as `"OBJCTRA"`.
    std::string_view rightAscension;

    /// The keyword that carries the declination, such as `"OBJCTDEC"`.
    std::string_view declination;
};

/// The keywords a pair names.
///
/// Thread-safe and allocation-free.
///
/// @param cards the pair.
/// @return the two keywords, as literals compiled into this library; two empty views for an
///         out-of-range cast.
[[nodiscard]] AFTERNIGHTENGINE_API PointingKeywords pointingCardKeywords(PointingCards cards) noexcept;

/// The order a plate solve reads its position hint in: every pair, a solution's reference direction
/// first.
///
/// Thread-safe and allocation-free.
///
/// @return `ReferenceValue`, `Pointing`, `Object`, `ObjectShort`, `Telescope`, as a span of a table
///         compiled into this library.
[[nodiscard]] AFTERNIGHTENGINE_API std::span<const PointingCards> plateSolveHintCards() noexcept;

/// The order a frame's observed pointing is read in: every pair but a solution's reference direction.
///
/// What a mosaic's panels contribute to its centre, since a reference direction is where a solution's
/// reference pixel points rather than where the mount did.
///
/// Thread-safe and allocation-free.
///
/// @return `Pointing`, `Object`, `ObjectShort`, `Telescope`, as a span of a table compiled into this
///         library.
[[nodiscard]] AFTERNIGHTENGINE_API std::span<const PointingCards> observedPointingCards() noexcept;

/// Where a frame's header says it pointed, and which pair said so.
struct PointingHint
{
    /// The direction, with its right ascension in `[0, 360)`.
    SkyCoordinate direction{};

    /// The pair that stated it.
    PointingCards source = PointingCards::ReferenceValue;

    /// Whether two hints say the same thing.
    ///
    /// @param left one hint.
    /// @param right the other.
    /// @return true when both members are equal.
    [[nodiscard]] friend constexpr bool operator==(const PointingHint& left,
                                                   const PointingHint& right) noexcept = default;
};

/// The first pointing a frame's cards state, in a stated order of keyword pairs.
///
/// A pair is read when both of its keywords are present and both values read as angles - the right
/// ascension by `parseRightAscensionDegrees` and the declination by `parseDeclinationDegrees`, so a
/// decimal number is degrees and fields are hours or degrees. A pair missing a keyword, or whose values
/// do not read, is passed over for the next.
///
/// Thread-safe and reentrant.
///
/// @param cards the frame's preserved cards, in any order.
/// @param order the pairs to try, first first: `plateSolveHintCards`, `observedPointingCards`, or the
///        caller's own.
/// @return the hint; `ErrorCode::InvalidArgument`, naming `"order"`, for an order entry outside the
///         enumeration; or `ErrorCode::EmptySelection` when no pair of the order states a pointing.
[[nodiscard]] AFTERNIGHTENGINE_API Result<PointingHint> readPointingHint(std::span<const CodecCard> cards,
                                                                         std::span<const PointingCards> order);

/// What a frame - or a caller standing in for it - states about the optics and any drizzle that
/// sampled it.
///
/// Every member is a positive finite number when present. `readOpticalHints` fills it from cards; a
/// caller whose user typed a focal length or a pixel size writes that over the card's before resolving.
struct OpticalHints
{
    /// The focal length, in millimetres: `FOCALLEN`, else `FOCAL`.
    std::optional<double> focalLengthMillimetres{};

    /// The sensor's pixel size, in micrometres: `XPIXSZ`, else `PIXSIZE`, else `PIXSIZE1`.
    std::optional<double> pixelSizeMicrometres{};

    /// A drizzled product's own scale, in arcseconds per output pixel: `D001SCAL`.
    std::optional<double> drizzleScaleArcsecPerPixel{};

    /// How many output pixels a drizzle made of one input pixel along an axis: `DRIZMULT`.
    std::optional<double> drizzleMultiplier{};

    /// Whether two records state the same facts.
    ///
    /// @param left one record.
    /// @param right the other.
    /// @return true when every member is equal, an absent member matching only another absent one.
    [[nodiscard]] friend bool operator==(const OpticalHints& left, const OpticalHints& right) = default;
};

/// What a frame's cards state about its optics.
///
/// Each member is the first of its keywords, in the order `OpticalHints` names them, whose value
/// reads as a positive finite number; a keyword whose value does not is passed over for the next.
///
/// Thread-safe, reentrant and allocation-free.
///
/// @param cards the frame's preserved cards, in any order.
/// @return the facts; a member no keyword states is absent.
[[nodiscard]] AFTERNIGHTENGINE_API OpticalHints readOpticalHints(std::span<const CodecCard> cards) noexcept;

/// The sky one pixel spans behind a lens.
///
/// The small-angle relation,
/// `kArcsecondsPerRadian * pixelSizeMicrometres / (1000 * focalLengthMillimetres)`, which is exact to
/// within a part in ten million for any pixel a camera has.
///
/// Thread-safe, reentrant and allocation-free.
///
/// @param pixelSizeMicrometres the pixel's size, finite and positive.
/// @param focalLengthMillimetres the focal length, finite and positive.
/// @return arcseconds per pixel; `ErrorCode::InvalidArgument`, naming the parameter, for a value that
///         is not finite and positive; or `ErrorCode::OutOfRange` when the scale is not finite.
[[nodiscard]] AFTERNIGHTENGINE_API Result<double> computePlateScaleArcsecPerPixel(double pixelSizeMicrometres,
                                                                                  double focalLengthMillimetres);

/// The focal length that puts this much sky on one pixel of this size.
///
/// The inverse of `computePlateScaleArcsecPerPixel`:
/// `kArcsecondsPerRadian * pixelSizeMicrometres / (1000 * plateScaleArcsecPerPixel)`. What a solved
/// frame says about the telescope that took it.
///
/// Thread-safe, reentrant and allocation-free.
///
/// @param pixelSizeMicrometres the pixel's size, finite and positive.
/// @param plateScaleArcsecPerPixel the solved scale, finite and positive.
/// @return millimetres; `ErrorCode::InvalidArgument`, naming the parameter, for a value that is not
///         finite and positive; or `ErrorCode::OutOfRange` when the focal length is not finite.
[[nodiscard]] AFTERNIGHTENGINE_API Result<double> computeFocalLengthMillimetres(double pixelSizeMicrometres,
                                                                                double plateScaleArcsecPerPixel);

/// The size a frame's pixels have after any drizzle, in micrometres of the sensor.
///
/// The pixel size divided by the drizzle multiplier; without a multiplier but with a drizzle scale and
/// a focal length, by how many times finer the drizzle scale is than the optics' own scale; otherwise
/// the pixel size as stated.
///
/// Thread-safe, reentrant and allocation-free.
///
/// @param optics the frame's optics.
/// @return micrometres; `ErrorCode::EmptySelection` when no pixel size is stated;
///         `ErrorCode::InvalidArgument`, naming the member, for a stated member that is not finite and
///         positive; or `ErrorCode::OutOfRange` when the size is not finite and positive.
[[nodiscard]] AFTERNIGHTENGINE_API Result<double> resolveEffectivePixelSizeMicrometres(const OpticalHints& optics);

/// The pixel scale a frame's optics imply, which a plate solve takes as its scale hint.
///
/// A drizzled product's stated scale when there is one; otherwise `computePlateScaleArcsecPerPixel` of
/// the effective pixel size `resolveEffectivePixelSizeMicrometres` gives and the focal length.
///
/// Thread-safe, reentrant and allocation-free.
///
/// @param optics the frame's optics.
/// @return arcseconds per pixel; `ErrorCode::EmptySelection` when neither a drizzle scale nor both a
///         pixel size and a focal length are stated; or the failures of the two operations it composes.
[[nodiscard]] AFTERNIGHTENGINE_API Result<double> resolvePlateScaleHint(const OpticalHints& optics);

/// The Julian epoch of an instant, which a catalog's proper motions are carried to.
///
/// `2000 + (JD - 2451545) / 365.25`, where JD is the instant's Julian date and 2451545 is J2000.0,
/// 2000-01-01T12:00:00. The instant is read as Terrestrial Time; the 69 seconds between it and UTC
/// are two millionths of a year.
///
/// Thread-safe, reentrant and allocation-free.
///
/// @param instant the moment, usually an exposure's start.
/// @return the epoch in Julian years: 2000.0 at J2000.0, 2016.0 at the epoch a Gaia table states.
[[nodiscard]] AFTERNIGHTENGINE_API double julianEpochYear(const UtcInstant& instant) noexcept;

/// The mean direction of a set of directions: where a mosaic's panels point together.
///
/// The direction of the sum of their unit vectors, which is the mean on the sphere rather than in
/// right ascension, so a set straddling zero hours or a pole has the centre it looks like it has.
///
/// Thread-safe, reentrant and allocation-free.
///
/// @param directions the directions, each a finite in-range `SkyCoordinate`.
/// @return the mean direction, its right ascension in `[0, 360)`; `ErrorCode::EmptySelection` for no
///         direction; `ErrorCode::InvalidArgument`, with the direction's index as its `Completed`
///         context, for a direction that is not a sky direction; or `ErrorCode::InvalidData` when the
///         unit vectors cancel - their sum is shorter than a billionth of their count - and no mean
///         direction exists.
[[nodiscard]] AFTERNIGHTENGINE_API Result<SkyCoordinate>
sphericalMeanDirection(std::span<const SkyCoordinate> directions);

} // namespace an
