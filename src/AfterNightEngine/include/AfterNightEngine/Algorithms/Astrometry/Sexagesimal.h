// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Sky angles as people and headers write them: hours or degrees, minutes and seconds.
///
/// A right ascension is written in hours of time and a declination in degrees of arc, each split
/// into minutes and seconds - `05 34 31.94` and `+22 00 52.2` - or as plain decimal degrees. Headers
/// carry both spellings, a capture program's `OBJCTRA` beside a mount's `RA`, and a person types
/// either into a search box. This header reads every spelling the product reads and writes the one a
/// FITS header carries, and splits an angle into its fields at a stated precision for a caller that
/// draws its own label.
///
/// **Reading.** Blanks around the text are ignored, and so is one pair of enclosing single quotes - a
/// FITS string's - with its doubled quotes undone. Text that is one decimal number is degrees, on
/// either axis; anything else is two or three fields: whole hours or degrees, minutes, and seconds.
/// Fields are separated by any run of blanks, colons and unit marks - `h`, `d`, `m`, `s`, the degree
/// sign and the arcminute and arcsecond marks `'` and `"` - in either case. Only the last field may
/// carry a fraction, and only the first a sign; minutes and seconds are below sixty; the whole field
/// reaches twenty-four hours or ninety degrees only with nothing after it. A right ascension is folded
/// into `[0, 360)` and is never negative.
///
/// **Splitting and writing.** An angle is rounded once, to the nearest whole number of the smallest
/// increment the caller states - ties away from zero - and only then split, so a carry reaches the
/// minutes and the whole field: `59.9996` seconds at three decimals is the next minute, never
/// `60.000`. Twenty-four hours carries round to zero. A declination is negative only when it rounds to
/// something below zero. The fields are integers, so a label printed from them shows exactly the digits
/// the rounding chose.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/SkyCoordinate.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace an
{

/// The most digits a split or a spelling carries after the seconds' decimal point.
///
/// Six is a microsecond of time and a microarcsecond of arc, beyond any plate solution; a seventh
/// digit would be below the rounding of the double the angle arrived in.
inline constexpr std::uint32_t kMaxSexagesimalFractionDigits = 6u;

/// How a written angle separates its fields.
enum class SexagesimalSeparator : std::uint8_t
{
    /// Single spaces, `05 34 31.940` - the spelling a FITS header carries in `OBJCTRA` and
    /// `OBJCTDEC`. The default.
    Space,

    /// Colons, `05:34:31.940`.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Colon
};

/// Whether a value names a separator this build writes.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param separator the value to inspect.
/// @return true for one of the two declared values.
[[nodiscard]] constexpr bool isSexagesimalSeparator(SexagesimalSeparator separator) noexcept
{
    return static_cast<std::uint8_t>(separator) <= static_cast<std::uint8_t>(SexagesimalSeparator::Colon);
}

/// The separator's own name, for a log line, a diagnostic field or a failure message.
///
/// Thread-safe and allocation-free.
///
/// @param separator the value to name.
/// @return a null-terminated string literal compiled into this library;
///         `"UnknownSexagesimalSeparator"` for an out-of-range cast.
[[nodiscard]] AFTERNIGHTENGINE_API const char* sexagesimalSeparatorName(SexagesimalSeparator separator) noexcept;

/// One angle split into its whole field, minutes and seconds at a stated precision.
///
/// Every member is an integer, so the digits a label shows are the digits the rounding chose; the
/// seconds' fraction is `fraction` over ten to the power `fractionDigits`.
struct SexagesimalAngle
{
    /// Whether the angle is below zero. Only a declination is ever negative, and never one that
    /// rounds to zero.
    bool negative = false;

    /// Whole hours of right ascension, in `[0, 24)`, or whole degrees of declination, in `[0, 90]`.
    std::uint32_t whole = 0u;

    /// Whole minutes, in `[0, 60)`.
    std::uint32_t minutes = 0u;

    /// Whole seconds, in `[0, 60)`.
    std::uint32_t seconds = 0u;

    /// The seconds' fractional digits as one integer, below ten to the power `fractionDigits`.
    std::uint32_t fraction = 0u;

    /// How many digits `fraction` carries, at most `kMaxSexagesimalFractionDigits`.
    std::uint32_t fractionDigits = 0u;

    /// Whether two splits are the same six numbers.
    ///
    /// @param left one split.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend constexpr bool operator==(const SexagesimalAngle& left,
                                                   const SexagesimalAngle& right) noexcept = default;
};

/// Reads a right ascension written in decimal degrees or in hours, minutes and seconds.
///
/// Thread-safe and reentrant. Allocates only to undo a FITS string's doubled quotes.
///
/// @param text the written angle, borrowed for the call.
/// @return the angle in degrees, in `[0, 360)`; `ErrorCode::InvalidArgument` for text that is not
///         one decimal number or two or three fields under the rules this header states, or a sign
///         on a field other than the first; or `ErrorCode::OutOfRange` for a well-formed angle that
///         is negative, not finite, above 360 degrees or twenty-four hours, or has minutes or
///         seconds of sixty or more.
[[nodiscard]] AFTERNIGHTENGINE_API Result<double> parseRightAscensionDegrees(std::string_view text);

/// Reads a declination written in decimal degrees or in degrees, minutes and seconds.
///
/// Thread-safe and reentrant. Allocates only to undo a FITS string's doubled quotes.
///
/// @param text the written angle, borrowed for the call.
/// @return the angle in degrees, in `[-90, 90]`; `ErrorCode::InvalidArgument` for text that is not
///         one decimal number or two or three fields under the rules this header states, or a sign
///         on a field other than the first; or `ErrorCode::OutOfRange` for a well-formed angle that
///         is not finite, lies beyond a pole, or has minutes or seconds of sixty or more.
[[nodiscard]] AFTERNIGHTENGINE_API Result<double> parseDeclinationDegrees(std::string_view text);

/// Reads a direction from its two written angles.
///
/// Thread-safe and reentrant.
///
/// @param rightAscension the right ascension, read by `parseRightAscensionDegrees`.
/// @param declination the declination, read by `parseDeclinationDegrees`.
/// @return the direction; the first failure of the two reads, the right ascension's first, carrying
///         `"rightAscension"` or `"declination"` as its `Parameter` context.
[[nodiscard]] AFTERNIGHTENGINE_API Result<SkyCoordinate> parseSkyCoordinate(std::string_view rightAscension,
                                                                            std::string_view declination);

/// Splits a right ascension into hours, minutes and seconds.
///
/// The angle is folded into `[0, 360)`, converted to hours and rounded to the nearest whole number
/// of `10^-fractionDigits` seconds before it is split; a rounding that reaches twenty-four hours is
/// zero hours.
///
/// Thread-safe, reentrant and allocation-free.
///
/// @param degrees the angle in degrees, any finite value.
/// @param fractionDigits how many digits the seconds keep, at most
///        `kMaxSexagesimalFractionDigits`.
/// @return the split, never negative; `ErrorCode::InvalidArgument` for an angle that is not finite
///         or more digits than the maximum.
[[nodiscard]] AFTERNIGHTENGINE_API Result<SexagesimalAngle> splitRightAscension(double degrees,
                                                                                std::uint32_t fractionDigits);

/// Splits a declination into degrees, minutes and seconds.
///
/// The magnitude is rounded to the nearest whole number of `10^-fractionDigits` arcseconds before it
/// is split, and the split is negative only when the angle is below zero and its rounding is not
/// zero.
///
/// Thread-safe, reentrant and allocation-free.
///
/// @param degrees the angle in degrees, in `[-90, 90]`.
/// @param fractionDigits how many digits the seconds keep, at most
///        `kMaxSexagesimalFractionDigits`.
/// @return the split; `ErrorCode::InvalidArgument` for an angle that is not finite or more digits
///         than the maximum; or `ErrorCode::OutOfRange` for an angle beyond a pole.
[[nodiscard]] AFTERNIGHTENGINE_API Result<SexagesimalAngle> splitDeclination(double degrees,
                                                                             std::uint32_t fractionDigits);

/// Writes a right ascension as hours, minutes and seconds.
///
/// Two digits of hours, two of minutes and two of seconds, then a decimal point and the fraction's
/// digits when there are any: `05 34 31.940`. `parseRightAscensionDegrees` reads it back to within
/// half the last digit.
///
/// Thread-safe and reentrant.
///
/// @param degrees the angle in degrees, any finite value.
/// @param fractionDigits how many digits the seconds keep, at most
///        `kMaxSexagesimalFractionDigits`.
/// @param separator what stands between the fields.
/// @return the text; `ErrorCode::InvalidArgument` for an angle that is not finite, more digits than
///         the maximum, or a separator outside its enumeration.
[[nodiscard]] AFTERNIGHTENGINE_API Result<std::string>
formatRightAscension(double degrees, std::uint32_t fractionDigits,
                     SexagesimalSeparator separator = SexagesimalSeparator::Space);

/// Writes a declination as a sign, degrees, minutes and seconds.
///
/// Always signed, then two digits of degrees, two of minutes and two of seconds, then a decimal point
/// and the fraction's digits when there are any: `+22 00 52.200`, `-05 23 28.000`.
/// `parseDeclinationDegrees` reads it back to within half the last digit.
///
/// Thread-safe and reentrant.
///
/// @param degrees the angle in degrees, in `[-90, 90]`.
/// @param fractionDigits how many digits the seconds keep, at most
///        `kMaxSexagesimalFractionDigits`.
/// @param separator what stands between the fields.
/// @return the text; `ErrorCode::InvalidArgument` for an angle that is not finite, more digits than
///         the maximum, or a separator outside its enumeration; or `ErrorCode::OutOfRange` for an
///         angle beyond a pole.
[[nodiscard]] AFTERNIGHTENGINE_API Result<std::string>
formatDeclination(double degrees, std::uint32_t fractionDigits,
                  SexagesimalSeparator separator = SexagesimalSeparator::Space);

} // namespace an
