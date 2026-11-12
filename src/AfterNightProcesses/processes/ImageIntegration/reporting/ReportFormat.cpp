// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ReportFormat.h"

#include "processes/ImageIntegration/model/GroupingKeys.h"

#include <QByteArray>
#include <QDir>
#include <QLocale>
#include <QStringList>
#include <QTime>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace ImageIntegration::Reporting
{
namespace
{
constexpr QChar kThinSpace{u' '};
constexpr QChar kMinusSign{u'−'};
constexpr QChar kMultiplicationSign{u'×'};
constexpr QChar kSuperscriptMinus{u'⁻'};

/// The significant digits a figure keeps.
constexpr int kSignificantDigits = 4;

/// The exponent a figure's plain decimals start at, and the magnitude its whole numbers end below.
constexpr int kSmallestPlainExponent = -3;
constexpr double kPowerOfTenFrom = 1e12;

/// A magnitude's digits in groups of three.
[[nodiscard]] QString groupedDigits(quint64 magnitude)
{
    QString digits = QString::number(magnitude);
    for (qsizetype position = digits.size() - 3; position > 0; position -= 3)
    {
        digits.insert(position, kThinSpace);
    }
    return digits;
}

/// A decimal number's text without the zeros it ends in, nor a point left with nothing after it.
[[nodiscard]] QString withoutTrailingZeros(QString text)
{
    if (!text.contains(QLatin1Char('.')))
    {
        return text;
    }
    while (text.endsWith(QLatin1Char('0')))
    {
        text.chop(1);
    }
    if (text.endsWith(QLatin1Char('.')))
    {
        text.chop(1);
    }
    return text;
}

/// An exponent in superscript digits.
[[nodiscard]] QString superscript(int exponent)
{
    static constexpr std::array<char16_t, 10> kDigits = {u'⁰', u'¹', u'²', u'³', u'⁴', u'⁵', u'⁶', u'⁷', u'⁸', u'⁹'};
    QString text;
    if (exponent < 0)
    {
        text.append(kSuperscriptMinus);
    }
    const QString digits = QString::number(std::abs(exponent));
    for (const QChar digit : digits)
    {
        text.append(QChar(kDigits[static_cast<std::size_t>(digit.digitValue())]));
    }
    return text;
}

/// A magnitude rounded to the figure's significant digits: its mantissa, from one up to ten, and its exponent.
struct RoundedMagnitude
{
    double mantissa = 0.0;
    int exponent = 0;
};

[[nodiscard]] RoundedMagnitude roundToSignificantDigits(double magnitude)
{
    // The C locale's scientific form is the rounding: `1.743e-02` states both parts, and a mantissa that rounds up to
    // ten arrives already carried into the exponent.
    const QByteArray scientific = QByteArray::number(magnitude, 'e', kSignificantDigits - 1);
    const qsizetype split = scientific.indexOf('e');
    RoundedMagnitude rounded;
    rounded.mantissa = scientific.left(split).toDouble();
    rounded.exponent = scientific.mid(split + 1).toInt();
    return rounded;
}

/// A magnitude as a mantissa times a power of ten.
[[nodiscard]] QString powerOfTen(const RoundedMagnitude& rounded)
{
    const QString mantissa = withoutTrailingZeros(QString::number(rounded.mantissa, 'f', kSignificantDigits - 1));
    return QStringLiteral("%1 %2 10%3").arg(mantissa, QString(kMultiplicationSign), superscript(rounded.exponent));
}

/// A whole number at least two digits wide.
[[nodiscard]] QString twoDigits(qint64 value)
{
    return QStringLiteral("%1").arg(value, 2, 10, QLatin1Char('0'));
}

/// What a value's text starts with: the minus sign for a negative one.
[[nodiscard]] QString signPrefix(bool negative)
{
    return negative ? QString(kMinusSign) : QString();
}

/// The magnitude of a signed value, the most negative one included.
[[nodiscard]] quint64 magnitudeOf(qint64 value)
{
    return value < 0 ? quint64{0} - static_cast<quint64>(value) : static_cast<quint64>(value);
}

/// A group key's own name for a slug: lower-case letters and digits, every run of anything else one hyphen.
[[nodiscard]] QString slugBase(const QString& groupKey)
{
    QString slug;
    slug.reserve(groupKey.size());
    bool pendingHyphen = false;
    for (const QChar character : groupKey)
    {
        const char16_t unit = character.toLower().unicode();
        const bool kept = (unit >= u'a' && unit <= u'z') || (unit >= u'0' && unit <= u'9');
        if (!kept)
        {
            pendingHyphen = !slug.isEmpty();
            continue;
        }
        if (pendingHyphen)
        {
            slug.append(QLatin1Char('-'));
            pendingHyphen = false;
        }
        slug.append(QChar(unit));
    }
    return slug.isEmpty() ? QStringLiteral("group") : slug;
}
} // namespace

QString noValueText()
{
    return QStringLiteral("–");
}

bool staysInsideItsFolder(const QString& relativePath)
{
    QString path = relativePath.trimmed();
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    // Nothing; a root's or a share's path; one that names a drive or a scheme.
    if (path.isEmpty() || path.startsWith(QLatin1Char('/')) || path.contains(QLatin1Char(':')))
    {
        return false;
    }
    const QString clean = QDir::cleanPath(path);
    return clean != QStringLiteral(".") && clean != QStringLiteral("..") && !clean.startsWith(QStringLiteral("../"));
}

QString formatFigure(double value)
{
    if (std::isnan(value))
    {
        return noValueText();
    }
    const bool negative = std::signbit(value);
    const double magnitude = std::abs(value);
    if (magnitude == 0.0)
    {
        return QStringLiteral("0");
    }
    if (std::isinf(magnitude))
    {
        return signPrefix(negative) + QStringLiteral("∞");
    }

    const RoundedMagnitude rounded = roundToSignificantDigits(magnitude);
    if (magnitude >= kPowerOfTenFrom || rounded.exponent < kSmallestPlainExponent)
    {
        return signPrefix(negative) + powerOfTen(rounded);
    }
    if (rounded.exponent >= kSignificantDigits - 1)
    {
        // Every digit kept is before the point: the whole number, not the four digits, is what a person reads.
        return signPrefix(negative) + groupedDigits(static_cast<quint64>(std::llround(magnitude)));
    }

    const int decimals = kSignificantDigits - 1 - rounded.exponent;
    return signPrefix(negative) + withoutTrailingZeros(QString::number(magnitude, 'f', decimals));
}

QString formatFigure(const std::optional<double>& value)
{
    return value.has_value() ? formatFigure(*value) : noValueText();
}

QString formatCount(qint64 value)
{
    return signPrefix(value < 0) + groupedDigits(magnitudeOf(value));
}

QString formatDurationMs(qint64 milliseconds)
{
    const QString sign = signPrefix(milliseconds < 0);
    const quint64 magnitude = magnitudeOf(milliseconds);
    if (magnitude < 1000u)
    {
        return QStringLiteral("%1%2 ms").arg(sign).arg(magnitude);
    }

    const quint64 tenths = (magnitude + 50u) / 100u;
    if (tenths < 100u)
    {
        const quint64 fraction = tenths % 10u;
        return fraction == 0u ? QStringLiteral("%1%2 s").arg(sign).arg(tenths / 10u)
                              : QStringLiteral("%1%2.%3 s").arg(sign).arg(tenths / 10u).arg(fraction);
    }

    const quint64 seconds = (magnitude + 500u) / 1000u;
    if (seconds < 60u)
    {
        return QStringLiteral("%1%2 s").arg(sign).arg(seconds);
    }
    if (seconds < 3600u)
    {
        return QStringLiteral("%1%2 m %3 s")
            .arg(sign)
            .arg(seconds / 60u)
            .arg(twoDigits(static_cast<qint64>(seconds % 60u)));
    }

    const quint64 minutes = (magnitude + 30000u) / 60000u;
    return QStringLiteral("%1%2 h %3 m")
        .arg(sign, groupedDigits(minutes / 60u), twoDigits(static_cast<qint64>(minutes % 60u)));
}

QString formatByteSize(qint64 bytes)
{
    const QString sign = signPrefix(bytes < 0);
    const quint64 magnitude = magnitudeOf(bytes);
    if (magnitude < 1024u)
    {
        return QStringLiteral("%1%2 B").arg(sign, groupedDigits(magnitude));
    }

    static constexpr std::array<const char*, 4> kUnits = {"KB", "MB", "GB", "TB"};
    double inUnit = static_cast<double>(magnitude) / 1024.0;
    std::size_t unit = 0;
    // A size is said in the next unit once it would print as 1,024 of this one.
    while (unit + 1 < kUnits.size() && std::llround(inUnit) >= 1024)
    {
        inUnit /= 1024.0;
        ++unit;
    }

    // One decimal while the size prints under a hundred of its unit, none from there up.
    const double tenths = std::round(inUnit * 10.0) / 10.0;
    const QString number =
        tenths < 100.0 ? QString::number(tenths, 'f', 1) : groupedDigits(static_cast<quint64>(std::llround(inUnit)));
    return QStringLiteral("%1%2 %3").arg(sign, number, QString::fromLatin1(kUnits[unit]));
}

QTimeZone ReportTimeZone::resolved() const
{
    const QString name = id.trimmed();
    if (!name.isEmpty())
    {
        const QTimeZone named(name.toUtf8());
        if (named.isValid())
        {
            return named;
        }
    }
    return QTimeZone::fromSecondsAheadOfUtc(utcOffsetSeconds);
}

QString formatDateTime(const QDateTime& instant, const ReportTimeZone& zone)
{
    if (!instant.isValid())
    {
        return noValueText();
    }
    const QDateTime local = instant.toTimeZone(zone.resolved());
    return QStringLiteral("%1, %2").arg(formatDate(local.date()), local.time().toString(QStringLiteral("HH:mm")));
}

QString formatDate(const QDate& date)
{
    if (!date.isValid())
    {
        return noValueText();
    }
    return QStringLiteral("%1 %2 %3")
        .arg(date.day())
        .arg(QLocale::c().monthName(date.month(), QLocale::ShortFormat))
        .arg(date.year());
}

QString formatUtcOffset(int utcOffsetSeconds)
{
    if (utcOffsetSeconds == 0)
    {
        return QStringLiteral("UTC");
    }
    const int minutes = std::abs(utcOffsetSeconds) / 60;
    return QStringLiteral("UTC%1%2:%3")
        .arg(utcOffsetSeconds < 0 ? QString(kMinusSign) : QStringLiteral("+"), twoDigits(minutes / 60),
             twoDigits(minutes % 60));
}

bool looksLikeResolution(const QString& part)
{
    const qsizetype xPos = part.indexOf(QLatin1Char('x'));
    if (xPos <= 0 || xPos >= part.length() - 1)
    {
        return false;
    }
    for (qsizetype i = 0; i < xPos; ++i)
    {
        if (!part[i].isDigit())
        {
            return false;
        }
    }
    for (qsizetype i = xPos + 1; i < part.length(); ++i)
    {
        if (!part[i].isDigit())
        {
            return false;
        }
    }
    return true;
}

QString friendlyResolution(const QString& resolution)
{
    return QString(resolution).replace(QLatin1Char('x'), QStringLiteral(" × "));
}

QString friendlyGroupKey(const QString& rawKey)
{
    const QString trimmed = rawKey.trimmed();
    if (trimmed.isEmpty())
    {
        return {};
    }

    const auto parsed = GroupKey::fromString(trimmed);
    if (!parsed.has_value())
    {
        return trimmed;
    }

    const QString compact = parsed->toCompactString().trimmed();
    if (compact.isEmpty())
    {
        return trimmed;
    }

    const QStringList parts = compact.split(QLatin1Char('|'));
    if (parts.size() <= 1)
    {
        return looksLikeResolution(compact) ? friendlyResolution(compact) : compact;
    }

    QString resolution;
    QStringList otherParts;
    for (const auto& part : parts)
    {
        const QString p = part.trimmed();
        if (resolution.isEmpty() && looksLikeResolution(p))
        {
            resolution = friendlyResolution(p);
        }
        else
        {
            otherParts << p;
        }
    }

    if (otherParts.isEmpty())
    {
        return resolution;
    }

    QString display = otherParts.join(QStringLiteral(" — "));
    if (!resolution.isEmpty())
    {
        display += QStringLiteral(" (%1)").arg(resolution);
    }
    return display;
}

QString ReportSlugs::slugFor(const QString& groupKey)
{
    const auto known = m_slugByKey.constFind(groupKey);
    if (known != m_slugByKey.constEnd())
    {
        return known.value();
    }

    const QString base = slugBase(groupKey);
    QString slug = base;
    for (int number = 2; m_taken.contains(slug); ++number)
    {
        slug = QStringLiteral("%1-%2").arg(base).arg(number);
    }
    m_taken.insert(slug);
    m_slugByKey.insert(groupKey, slug);
    return slug;
}
} // namespace ImageIntegration::Reporting
