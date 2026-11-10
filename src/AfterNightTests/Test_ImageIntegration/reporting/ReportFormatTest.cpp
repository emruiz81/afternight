// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// How the integration report prints a value for a person: a figure at four significant digits, a count in groups of
/// three, a duration and a size in words, a time in the zone its archive states, a group's key as a name, and the
/// anchor name each group of a run gets.

#include <gtest/gtest.h>

#include "processes/ImageIntegration/core/FilenameUtils.h"
#include "processes/ImageIntegration/reporting/ReportFormat.h"

#include <QDate>
#include <QDateTime>
#include <QSet>
#include <QString>
#include <QTime>
#include <QTimeZone>

#include <cmath>
#include <limits>
#include <optional>

using namespace ImageIntegration::Reporting;

namespace
{
/// The text as UTF-8, for a failure message.
std::string utf8(const QString& text)
{
    return text.toStdString();
}

/// An instant stated in UTC.
QDateTime utc(int year, int month, int day, int hour, int minute, int second = 0)
{
    return QDateTime(QDate(year, month, day), QTime(hour, minute, second), QTimeZone::UTC);
}
} // namespace

TEST(ReportFormat, AFigureKeepsFourSignificantDigits)
{
    EXPECT_EQ(utf8(formatFigure(0.01742656553)), "0.01743");
    EXPECT_EQ(utf8(formatFigure(3.14159)), "3.142");
    EXPECT_EQ(utf8(formatFigure(123.456)), "123.5");
    EXPECT_EQ(utf8(formatFigure(0.2701124707680701)), "0.2701");
    EXPECT_EQ(utf8(formatFigure(0.001)), "0.001");
}

TEST(ReportFormat, AFigureDropsTheZerosItsDigitsEndIn)
{
    EXPECT_EQ(utf8(formatFigure(1.0)), "1");
    EXPECT_EQ(utf8(formatFigure(2.5)), "2.5");
    EXPECT_EQ(utf8(formatFigure(120.0)), "120");
    EXPECT_EQ(utf8(formatFigure(0.25)), "0.25");
    EXPECT_EQ(utf8(formatFigure(0.1 + 0.2)), "0.3");
}

TEST(ReportFormat, AFigureBelowOneThousandthIsAPowerOfTen)
{
    EXPECT_EQ(formatFigure(6.749e-05), QStringLiteral("6.749 × 10⁻⁵"));
    EXPECT_EQ(formatFigure(1e-9), QStringLiteral("1 × 10⁻⁹"));
    EXPECT_EQ(formatFigure(2.5e-12), QStringLiteral("2.5 × 10⁻¹²"));

    // The rounding to four digits is what decides the form: a value that rounds up to one thousandth prints as one,
    // and one that rounds up to the next power of ten is that power.
    EXPECT_EQ(utf8(formatFigure(0.00099996)), "0.001");
    EXPECT_EQ(formatFigure(9.99996e-05), QStringLiteral("1 × 10⁻⁴"));
}

TEST(ReportFormat, AFigureOfAThousandOrMoreIsAWholeNumberInGroupsOfThree)
{
    EXPECT_EQ(formatFigure(1284.4), QStringLiteral("1 284"));
    EXPECT_EQ(formatFigure(12345.678), QStringLiteral("12 346"));
    EXPECT_EQ(formatFigure(999.96), QStringLiteral("1 000"));
    EXPECT_EQ(formatFigure(104438989.0), QStringLiteral("104 438 989"));
    EXPECT_EQ(formatFigure(999999999999.0), QStringLiteral("999 999 999 999"));
}

TEST(ReportFormat, AFigureOfATrillionOrMoreIsAPowerOfTen)
{
    EXPECT_EQ(formatFigure(1e12), QStringLiteral("1 × 10¹²"));
    EXPECT_EQ(formatFigure(2.5e15), QStringLiteral("2.5 × 10¹⁵"));
    EXPECT_EQ(formatFigure(6.02214076e23), QStringLiteral("6.022 × 10²³"));
}

TEST(ReportFormat, ANegativeAZeroAnInfinityAndANaNEachHaveAStatedForm)
{
    // A negative figure carries the minus sign, not a hyphen.
    EXPECT_EQ(formatFigure(-0.01742656553), QStringLiteral("−0.01743"));
    EXPECT_EQ(formatFigure(-1284.4), QStringLiteral("−1 284"));
    EXPECT_EQ(formatFigure(-6.749e-05), QStringLiteral("−6.749 × 10⁻⁵"));

    // Zero is one character, whatever its sign.
    EXPECT_EQ(utf8(formatFigure(0.0)), "0");
    EXPECT_EQ(utf8(formatFigure(-0.0)), "0");

    EXPECT_EQ(formatFigure(std::numeric_limits<double>::infinity()), QStringLiteral("∞"));
    EXPECT_EQ(formatFigure(-std::numeric_limits<double>::infinity()), QStringLiteral("−∞"));

    // A value that is not a number prints as a value that is not stated does.
    EXPECT_EQ(formatFigure(std::numeric_limits<double>::quiet_NaN()), noValueText());
    EXPECT_EQ(formatFigure(std::optional<double>{}), noValueText());
    EXPECT_EQ(utf8(formatFigure(std::optional<double>{2.5})), "2.5");
    EXPECT_EQ(noValueText(), QStringLiteral("–"));
}

TEST(ReportFormat, ACountIsPrintedInGroupsOfThree)
{
    EXPECT_EQ(utf8(formatCount(0)), "0");
    EXPECT_EQ(utf8(formatCount(174)), "174");
    EXPECT_EQ(formatCount(1284), QStringLiteral("1 284"));
    EXPECT_EQ(formatCount(46776512), QStringLiteral("46 776 512"));
    EXPECT_EQ(formatCount(-1284), QStringLiteral("−1 284"));
    EXPECT_EQ(formatCount(std::numeric_limits<qint64>::min()), QStringLiteral("−9 223 372 036 854 775 808"));
}

TEST(ReportFormat, ADurationIsSaidInItsTwoLargestUnits)
{
    EXPECT_EQ(utf8(formatDurationMs(297)), "297 ms");
    EXPECT_EQ(utf8(formatDurationMs(59 * 1000)), "59 s");
    EXPECT_EQ(utf8(formatDurationMs(61 * 1000)), "1 m 01 s");
    EXPECT_EQ(utf8(formatDurationMs(3600 * 1000)), "1 h 00 m");
    EXPECT_EQ(utf8(formatDurationMs(35400 * 1000)), "9 h 50 m");
    EXPECT_EQ(utf8(formatDurationMs(qint64{30} * 3600 * 1000)), "30 h 00 m");
}

TEST(ReportFormat, ADurationUnderTenSecondsKeepsATenth)
{
    EXPECT_EQ(utf8(formatDurationMs(0)), "0 ms");
    EXPECT_EQ(utf8(formatDurationMs(999)), "999 ms");
    EXPECT_EQ(utf8(formatDurationMs(1000)), "1 s");
    EXPECT_EQ(utf8(formatDurationMs(1234)), "1.2 s");
    EXPECT_EQ(utf8(formatDurationMs(9940)), "9.9 s");
    EXPECT_EQ(utf8(formatDurationMs(12345)), "12 s");
}

TEST(ReportFormat, ADurationThatRoundsUpToTheNextUnitIsSaidInIt)
{
    EXPECT_EQ(utf8(formatDurationMs(9960)), "10 s");
    EXPECT_EQ(utf8(formatDurationMs(59600)), "1 m 00 s");
    EXPECT_EQ(utf8(formatDurationMs(3599600)), "1 h 00 m");
    EXPECT_EQ(utf8(formatDurationMs(3629 * 1000)), "1 h 00 m");
    EXPECT_EQ(utf8(formatDurationMs(3630 * 1000)), "1 h 01 m");
}

TEST(ReportFormat, ANegativeDurationCarriesTheMinusSign)
{
    EXPECT_EQ(formatDurationMs(-297), QStringLiteral("−297 ms"));
    EXPECT_EQ(formatDurationMs(-61 * 1000), QStringLiteral("−1 m 01 s"));
}

TEST(ReportFormat, ASizeIsSaidInTheLargestUnitItFills)
{
    EXPECT_EQ(utf8(formatByteSize(0)), "0 B");
    EXPECT_EQ(utf8(formatByteSize(512)), "512 B");
    EXPECT_EQ(utf8(formatByteSize(1024)), "1.0 KB");
    EXPECT_EQ(utf8(formatByteSize(104438989)), "99.6 MB");
    EXPECT_EQ(utf8(formatByteSize(245096640)), "234 MB");
    EXPECT_EQ(utf8(formatByteSize(qint64{5} * 1024 * 1024 * 1024)), "5.0 GB");
    EXPECT_EQ(utf8(formatByteSize(qint64{3} * 1024 * 1024 * 1024 * 1024)), "3.0 TB");
    EXPECT_EQ(formatByteSize(qint64{5000} * 1024 * 1024 * 1024 * 1024), QStringLiteral("5 000 TB"));
}

TEST(ReportFormat, ASizeThatRoundsUpToTheNextUnitIsSaidInIt)
{
    EXPECT_EQ(formatByteSize(1023), QStringLiteral("1 023 B"));
    EXPECT_EQ(utf8(formatByteSize(1024 * 1024 - 1)), "1.0 MB");
    EXPECT_EQ(utf8(formatByteSize(qint64{1024} * 1024 * 1024 - 1)), "1.0 GB");
    EXPECT_EQ(formatByteSize(-104438989), QStringLiteral("−99.6 MB"));
}

TEST(ReportFormat, ATimeIsPrintedInTheZoneItsArchiveStates)
{
    const ReportTimeZone buenosAires{QStringLiteral("America/Argentina/Buenos_Aires"), -10800};
    EXPECT_EQ(utf8(formatDateTime(utc(2026, 11, 10, 14, 15, 34), buenosAires)), "10 Nov 2026, 11:15");

    // The instant is what is printed, whatever zone it was handed over in.
    const QDateTime sameInstantElsewhere =
        utc(2026, 11, 10, 14, 15, 34).toTimeZone(QTimeZone::fromSecondsAheadOfUtc(9 * 3600));
    EXPECT_EQ(utf8(formatDateTime(sameInstantElsewhere, buenosAires)), "10 Nov 2026, 11:15");

    // The date is the zone's too: a quarter past one in the morning in UTC is still the day before in Buenos Aires.
    EXPECT_EQ(utf8(formatDateTime(utc(2026, 3, 1, 1, 15), buenosAires)), "28 Feb 2026, 22:15");
}

TEST(ReportFormat, AZoneThisHostDoesNotKnowIsItsStatedOffset)
{
    const ReportTimeZone unknown{QStringLiteral("Nowhere/Land"), 3600};
    EXPECT_EQ(utf8(formatDateTime(utc(2026, 11, 10, 14, 15), unknown)), "10 Nov 2026, 15:15");

    const ReportTimeZone unnamed{QString(), -5 * 3600 - 1800};
    EXPECT_EQ(utf8(formatDateTime(utc(2026, 11, 10, 14, 15), unnamed)), "10 Nov 2026, 08:45");
}

TEST(ReportFormat, AZoneThisHostKnowsGivesEachInstantItsOwnOffset)
{
    if (!QTimeZone(QByteArrayLiteral("Europe/Madrid")).isValid())
    {
        GTEST_SKIP() << "this host has no time zone database entry for Europe/Madrid";
    }

    // Archived in summer, two hours ahead of UTC; a light taken in January was one hour ahead.
    const ReportTimeZone madrid{QStringLiteral("Europe/Madrid"), 7200};
    EXPECT_EQ(utf8(formatDateTime(utc(2026, 1, 15, 21, 30), madrid)), "15 Jan 2026, 22:30");
    EXPECT_EQ(utf8(formatDateTime(utc(2026, 7, 15, 21, 30), madrid)), "15 Jul 2026, 23:30");
}

TEST(ReportFormat, ATimeThatIsNotStatedPrintsAsNoValue)
{
    EXPECT_EQ(formatDateTime(QDateTime(), ReportTimeZone{}), noValueText());
    EXPECT_EQ(formatDate(QDate()), noValueText());
}

TEST(ReportFormat, ADateNamesItsMonth)
{
    EXPECT_EQ(utf8(formatDate(QDate(2026, 3, 24))), "24 Mar 2026");
    EXPECT_EQ(utf8(formatDate(QDate(2023, 10, 9))), "9 Oct 2023");
}

TEST(ReportFormat, AnOffsetFromUtcIsPrintedInHoursAndMinutes)
{
    EXPECT_EQ(utf8(formatUtcOffset(0)), "UTC");
    EXPECT_EQ(formatUtcOffset(-10800), QStringLiteral("UTC−03:00"));
    EXPECT_EQ(utf8(formatUtcOffset(19800)), "UTC+05:30");
    EXPECT_EQ(utf8(formatUtcOffset(45900)), "UTC+12:45");
}

TEST(ReportFormat, AGroupKeyIsPrintedAsANameWithItsResolutionLast)
{
    EXPECT_EQ(friendlyGroupKey(QStringLiteral("3008x3004|-|-|-|Blue|M 83|-")),
              QStringLiteral("Blue — M 83 (3008 × 3004)"));
    EXPECT_EQ(friendlyGroupKey(QStringLiteral("3008x3004|-|-|-|-|M 83|-")), QStringLiteral("M 83 (3008 × 3004)"));
    EXPECT_EQ(friendlyGroupKey(QStringLiteral("reg|5544x3684|NGC253")), QStringLiteral("reg — NGC253 (5544 × 3684)"));
}

TEST(ReportFormat, AGroupKeyOfAResolutionAloneIsTheResolution)
{
    EXPECT_EQ(friendlyGroupKey(QStringLiteral("3008x3004")), QStringLiteral("3008 × 3004"));
    EXPECT_EQ(friendlyGroupKey(QStringLiteral("3008x3004|-|-|-")), QStringLiteral("3008 × 3004"));
    EXPECT_EQ(friendlyResolution(QStringLiteral("8288x5644")), QStringLiteral("8288 × 5644"));
}

TEST(ReportFormat, AGroupKeyThatIsNoCompositeKeyIsPrintedAsItIs)
{
    EXPECT_EQ(utf8(friendlyGroupKey(QStringLiteral("Ha_Group"))), "Ha_Group");
    EXPECT_EQ(utf8(friendlyGroupKey(QStringLiteral("  GroupA  "))), "GroupA");
    EXPECT_EQ(utf8(friendlyGroupKey(QStringLiteral("-|-"))), "-|-");
    EXPECT_TRUE(friendlyGroupKey(QString()).isEmpty());
    EXPECT_TRUE(friendlyGroupKey(QStringLiteral("   ")).isEmpty());
}

TEST(ReportFormat, OnlyDigitsAroundOneXLookLikeAResolution)
{
    EXPECT_TRUE(looksLikeResolution(QStringLiteral("3008x3004")));
    EXPECT_FALSE(looksLikeResolution(QStringLiteral("x3004")));
    EXPECT_FALSE(looksLikeResolution(QStringLiteral("3008x")));
    EXPECT_FALSE(looksLikeResolution(QStringLiteral("3008X3004")));
    EXPECT_FALSE(looksLikeResolution(QStringLiteral("30a8x3004")));
    EXPECT_FALSE(looksLikeResolution(QStringLiteral("Luminance")));
}

TEST(ReportFormat, ASlugIsMadeOfLowerCaseLettersDigitsAndHyphens)
{
    ReportSlugs slugs;
    EXPECT_EQ(utf8(slugs.slugFor(QStringLiteral("5544x3684|-|-|-|-|NGC253|-"))), "5544x3684-ngc253");
    EXPECT_EQ(utf8(slugs.slugFor(QStringLiteral("3008x3004|-|-|-|Ha|M 16|-"))), "3008x3004-ha-m-16");
    EXPECT_EQ(utf8(slugs.slugFor(QStringLiteral("  --Group A / (final)--  "))), "group-a-final");
}

TEST(ReportFormat, TheSameKeyKeepsItsSlug)
{
    ReportSlugs slugs;
    const QString first = slugs.slugFor(QStringLiteral("3008x3004|-|-|-|Ha|M 16|-"));
    EXPECT_EQ(slugs.slugFor(QStringLiteral("3008x3004|-|-|-|Oiii|M 16|-")), QStringLiteral("3008x3004-oiii-m-16"));
    EXPECT_EQ(slugs.slugFor(QStringLiteral("3008x3004|-|-|-|Ha|M 16|-")), first);
}

TEST(ReportFormat, TwoGroupsWhoseFolderNamesCollideGetTwoSlugs)
{
    const QString slash = QStringLiteral("Group/A");
    const QString colon = QStringLiteral("Group:A");
    ASSERT_EQ(ImageIntegration::sanitizeFilename(slash), ImageIntegration::sanitizeFilename(colon));

    ReportSlugs slugs;
    const QString first = slugs.slugFor(slash);
    const QString second = slugs.slugFor(colon);
    EXPECT_EQ(utf8(first), "group-a");
    EXPECT_EQ(utf8(second), "group-a-2");
    EXPECT_EQ(slugs.slugFor(slash), first);
    EXPECT_EQ(slugs.slugFor(colon), second);
}

TEST(ReportFormat, ASlugAlreadyTakenByAnotherKeysOwnNameIsNotGivenTwice)
{
    ReportSlugs slugs;
    QSet<QString> seen;
    for (const QString& key : {QStringLiteral("Group A"), QStringLiteral("Group A 2"), QStringLiteral("Group_A"),
                               QStringLiteral("group a"), QStringLiteral("GROUP-A-2")})
    {
        const QString slug = slugs.slugFor(key);
        EXPECT_FALSE(seen.contains(slug)) << utf8(slug) << " was given to two keys";
        seen.insert(slug);
    }
    EXPECT_EQ(seen.size(), 5);
}

TEST(ReportFormat, AKeyWithNothingASlugCanHoldStillGetsOne)
{
    ReportSlugs slugs;
    EXPECT_EQ(utf8(slugs.slugFor(QStringLiteral("|-|-|"))), "group");
    EXPECT_EQ(utf8(slugs.slugFor(QStringLiteral("Ñandú"))), "and");
    EXPECT_EQ(utf8(slugs.slugFor(QStringLiteral("星云"))), "group-2");
    EXPECT_EQ(utf8(slugs.slugFor(QString())), "group-3");
}
