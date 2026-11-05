// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The sky-angle labels Controls draws: a right ascension in hours, minutes and seconds and a declination in degrees,
/// arcminutes and arcseconds, decorated as each window shows them and drawn from the engine's split of the angle.
///
/// The engine rounds an angle once, to the precision a label shows, and only then splits it into integer fields
/// (`splitRightAscension`, `splitDeclination`), so a carry reaches every field and a label never shows sixty seconds
/// under the same minute. The decorations - the unit letters, the degree sign, the arcminute and arcsecond marks and
/// the colons - are the product's and stay here. An angle that is not finite is labelled as zero, as the windows
/// always labelled it.
#pragma once

#include <AfterNightEngine/Algorithms/Astrometry/Sexagesimal.h>
#include <AfterNightEngine/Algorithms/Catalog/CatalogRecord.h>

#include <QChar>
#include <QString>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace an::ui
{

/// How a label separates the fields of an angle.
enum class SexagesimalLabelStyle : std::uint8_t
{
    /// Unit marks: `05h 34m 31.94s` and `+22° 00' 52.2"`.
    UnitMarks,

    /// Colons: `05:34:31.94` and `+22:00:52.2`.
    Colons
};

namespace detail
{

/// A field of two digits, zero-padded.
[[nodiscard]] inline QString twoDigitField(const std::uint32_t value)
{
    return QStringLiteral("%1").arg(value, 2, 10, QChar(u'0'));
}

/// The seconds of a split angle, with the decimals the split kept.
[[nodiscard]] inline QString sexagesimalSecondsText(const an::SexagesimalAngle& angle)
{
    QString text = twoDigitField(angle.seconds);
    if (angle.fractionDigits > 0u)
    {
        text += QLatin1Char('.');
        text += QStringLiteral("%1").arg(angle.fraction, static_cast<int>(angle.fractionDigits), 10, QChar(u'0'));
    }
    return text;
}

/// A declination held to the sphere, and zero for one that is not finite.
[[nodiscard]] inline double labelledDeclination(const double degrees)
{
    return std::isfinite(degrees) ? std::clamp(degrees, -90.0, 90.0) : 0.0;
}

} // namespace detail

/// A right ascension's label, its seconds rounded to a stated number of decimals.
///
/// @param degrees the angle in degrees; any finite value is folded into one turn.
/// @param fractionDigits the seconds' decimals, at most `kMaxSexagesimalFractionDigits`.
/// @param style how the fields are separated.
/// @return the label, such as `05h 34m 31.94s`.
[[nodiscard]] inline QString rightAscensionLabel(const double degrees, const std::uint32_t fractionDigits,
                                                 const SexagesimalLabelStyle style = SexagesimalLabelStyle::UnitMarks)
{
    const an::Result<an::SexagesimalAngle> split =
        an::splitRightAscension(std::isfinite(degrees) ? degrees : 0.0, fractionDigits);
    if (!split.hasValue())
    {
        return QStringLiteral("-");
    }
    const QString hours = detail::twoDigitField(split->whole);
    const QString minutes = detail::twoDigitField(split->minutes);
    const QString seconds = detail::sexagesimalSecondsText(*split);
    return style == SexagesimalLabelStyle::Colons ? QStringLiteral("%1:%2:%3").arg(hours, minutes, seconds)
                                                  : QStringLiteral("%1h %2m %3s").arg(hours, minutes, seconds);
}

/// A declination's label, its arcseconds rounded to a stated number of decimals and always signed.
///
/// @param degrees the angle in degrees; a value past a pole is held at it.
/// @param fractionDigits the arcseconds' decimals, at most `kMaxSexagesimalFractionDigits`.
/// @param style how the fields are separated.
/// @return the label, such as `+22° 00' 52.2"`; the sign is minus only for an angle that rounds below zero.
[[nodiscard]] inline QString declinationLabel(const double degrees, const std::uint32_t fractionDigits,
                                              const SexagesimalLabelStyle style = SexagesimalLabelStyle::UnitMarks)
{
    const an::Result<an::SexagesimalAngle> split =
        an::splitDeclination(detail::labelledDeclination(degrees), fractionDigits);
    if (!split.hasValue())
    {
        return QStringLiteral("-");
    }
    const QChar sign = split->negative ? QChar(u'-') : QChar(u'+');
    const QString whole = detail::twoDigitField(split->whole);
    const QString minutes = detail::twoDigitField(split->minutes);
    const QString seconds = detail::sexagesimalSecondsText(*split);
    return style == SexagesimalLabelStyle::Colons
               ? QStringLiteral("%1%2:%3:%4").arg(sign).arg(whole, minutes, seconds)
               : QStringLiteral("%1%2° %3' %4\"").arg(sign).arg(whole, minutes, seconds);
}

/// A right ascension's label to the nearest minute of time, for a coarse grid line.
///
/// @param degrees the angle in degrees; any finite value is folded into one turn.
/// @return the label, such as `05h 35m`.
[[nodiscard]] inline QString rightAscensionMinutesLabel(const double degrees)
{
    // A minute of time is a quarter of a degree, so a whole number of minutes is exact before it is split.
    const double minutes = std::round(an::normalizeRightAscensionDegrees(std::isfinite(degrees) ? degrees : 0.0) * 4.0);
    const an::Result<an::SexagesimalAngle> split = an::splitRightAscension(minutes / 4.0, 0u);
    if (!split.hasValue())
    {
        return QStringLiteral("-");
    }
    return QStringLiteral("%1h %2m").arg(detail::twoDigitField(split->whole), detail::twoDigitField(split->minutes));
}

/// A declination's label to the nearest arcminute, for a coarse grid line.
///
/// @param degrees the angle in degrees; a value past a pole is held at it.
/// @return the label, such as `+22° 01'`.
[[nodiscard]] inline QString declinationMinutesLabel(const double degrees)
{
    const double clamped = detail::labelledDeclination(degrees);
    const double arcminutes = std::round(std::abs(clamped) * 60.0);
    const an::Result<an::SexagesimalAngle> split = an::splitDeclination(std::copysign(arcminutes / 60.0, clamped), 0u);
    if (!split.hasValue())
    {
        return QStringLiteral("-");
    }
    const QChar sign = split->negative ? QChar(u'-') : QChar(u'+');
    return QStringLiteral("%1%2° %3'")
        .arg(sign)
        .arg(detail::twoDigitField(split->whole), detail::twoDigitField(split->minutes));
}

} // namespace an::ui
