// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// How the integration report prints a value for a person. One unit formats every figure, count, duration, size,
/// time and group name the report states, so two places never print the same value two ways, and a reader meets
/// `0.01743` where the measurement was `0.01742656553`.
///
/// Nothing here reads a run: each function takes the value and returns its text. The text is the same on every host
/// and in every locale - a decimal point, English month names - since a report is read away from the machine that
/// wrote it.

#include "AfterNightProcessesExport.h"

#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QString>
#include <QTimeZone>

#include <optional>

namespace ImageIntegration::Reporting
{
/// What the report prints where a value is not stated: an en dash. A missing value is never printed as a zero.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString noValueText();

/// Whether a path names a file inside the folder it is relative to: it is relative, names no drive, and no part of
/// it climbs out of the folder.
///
/// A stored report and a run's records are files on disk - one may have been edited, or have come in a folder
/// someone sent - so a path one of them states is held to this before a file is read, copied or linked by it. A
/// path that fails is no picture of the run: read, it would bring a file from elsewhere on the machine into a
/// document meant to be sent.
///
/// @param relativePath the path as stated, with either separator.
/// @return whether the path stays inside its folder; false for an empty path.
[[nodiscard]] AFTERNIGHTPROCESSES_API bool staysInsideItsFolder(const QString& relativePath);

/// A measured figure at four significant digits, the zeros its digits end in dropped.
///
/// - From one thousandth up to a thousand, in plain decimals: `0.01743`, `2.5`, `123.5`.
/// - From a thousand up to a trillion, as a whole number in groups of three set apart by thin spaces: `12 346`.
/// - Below one thousandth, and from a trillion up, as a power of ten: `6.749 × 10⁻⁵`.
///
/// The form is decided after the rounding, so a value that rounds up to one thousandth prints as `0.001`. A negative
/// figure carries the minus sign (U+2212); zero prints as `0` whatever its sign; an infinity prints as `∞`; a value
/// that is not a number prints as `noValueText()`.
///
/// @param value the figure.
/// @return its text.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString formatFigure(double value);

/// A measured figure that may not be stated: `formatFigure` of the value, or `noValueText()` without one.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString formatFigure(const std::optional<double>& value);

/// A count in groups of three digits set apart by thin spaces (U+2009): `1 284`. A negative count carries the minus
/// sign.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString formatCount(qint64 value);

/// A duration in its two largest units: `297 ms`, `1.2 s`, `59 s`, `1 m 01 s`, `9 h 50 m`.
///
/// Under one second it is whole milliseconds; under ten seconds it keeps a tenth of a second; under a minute it is
/// whole seconds; under an hour, minutes and seconds; from an hour up, hours and minutes, the hours unbounded since
/// an integration time is said in hours. A duration that rounds up to the next unit is said in it: 59.6 seconds is
/// `1 m 00 s`. A negative duration carries the minus sign.
///
/// @param milliseconds the duration.
/// @return its text.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString formatDurationMs(qint64 milliseconds);

/// A size in the largest unit it fills, in powers of 1,024 under the names the report has always used (`KB`, `MB`,
/// `GB`, `TB`): `512 B`, `99.6 MB`, `234 MB`. Under a hundred of its unit a size keeps one decimal; a size that
/// rounds up to 1,024 of a unit is said in the next. A negative size carries the minus sign.
///
/// @param bytes the size.
/// @return its text.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString formatByteSize(qint64 bytes);

/// The zone a run's archive states its times in: the zone's name and the offset from UTC the archive was written at.
struct ReportTimeZone
{
    /// The zone's IANA name, as the archive's `timeZoneId` states it; empty when the archive states none.
    QString id;

    /// Seconds ahead of UTC when the archive was written, as its `utcOffsetSeconds` states them.
    int utcOffsetSeconds = 0;

    /// The zone times are printed in: the named zone where this host knows it, which gives every instant the offset
    /// it had - a light taken in winter and archived in summer - and otherwise the stated offset, fixed.
    [[nodiscard]] AFTERNIGHTPROCESSES_API QTimeZone resolved() const;
};

/// An instant in the archive's zone, to the minute: `10 Nov 2026, 11:15`. An instant that is not valid prints as
/// `noValueText()`.
///
/// @param instant the instant, in any zone.
/// @param zone the archive's zone.
/// @return its text.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString formatDateTime(const QDateTime& instant, const ReportTimeZone& zone);

/// A date with its month named: `24 Mar 2026`. A date that is not valid prints as `noValueText()`.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString formatDate(const QDate& date);

/// An offset from UTC in hours and minutes: `UTC−03:00`, `UTC+05:30`, and `UTC` for none.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString formatUtcOffset(int utcOffsetSeconds);

/// Whether a part of a group key is a resolution: digits, one lower-case `x`, digits.
[[nodiscard]] AFTERNIGHTPROCESSES_API bool looksLikeResolution(const QString& part);

/// A resolution with the multiplication sign between its sides: `3008 × 3004` for `3008x3004`.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString friendlyResolution(const QString& resolution);

/// A group key as a name: its stated parts joined by em dashes, its resolution last in parentheses, its placeholder
/// parts left out - `Blue — M 83 (3008 × 3004)` for `3008x3004|-|-|-|Blue|M 83|-`. A key that is no composite key
/// is printed as it is.
///
/// @param rawKey the group key as the manifest or a result states it.
/// @return the name; empty for an empty key.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString friendlyGroupKey(const QString& rawKey);

/// The anchor names of one run's groups.
///
/// A slug is what a link inside the report points at: lower-case letters, digits and hyphens made from the group's
/// key. Two keys never share a slug - a second key whose own name is taken gets a number after it - and a key keeps
/// the slug it was first given, so the slugs of a run depend only on the order its groups are asked in.
///
/// A slug names anchors only. A group's folder in the archive keeps the name the archiver gave it, which the report
/// model stores beside the slug.
class AFTERNIGHTPROCESSES_API ReportSlugs
{
public:
    /// The slug of a group key, given on the first call and returned again on every later one.
    ///
    /// @param groupKey the group key as the manifest or a result states it.
    /// @return the slug; never empty.
    [[nodiscard]] QString slugFor(const QString& groupKey);

private:
    QHash<QString, QString> m_slugByKey;
    QSet<QString> m_taken;
};
} // namespace ImageIntegration::Reporting
