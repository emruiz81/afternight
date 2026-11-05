// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for reading, splitting and writing sky angles in hours or degrees, minutes and
// seconds.
//
// The spellings below are the ones the product reads: capture programs' `OBJCTRA` strings in hours,
// mounts' decimal `RA` in degrees, solvers' colon forms, and what a person types with unit letters
// and the degree sign. The arithmetic is held to one stated order of operations - the whole field
// plus minutes over sixty plus seconds over thirty-six hundred, then fifteen for hours - so one
// spelling always reads as the same double.

#include <AfterNightEngine/Algorithms/Astrometry/Sexagesimal.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace
{

/// The right ascension of a sexagesimal hour reading, in the product's order of operations.
[[nodiscard]] double hoursToDegrees(double hours, double minutes, double seconds)
{
    return (hours + minutes / 60.0 + seconds / 3600.0) * 15.0;
}

/// The declination of a sexagesimal degree reading, in the product's order of operations.
[[nodiscard]] double arcToDegrees(double degrees, double minutes, double seconds)
{
    return degrees + minutes / 60.0 + seconds / 3600.0;
}

/// The value a successful read produced, or a sentinel that fails every comparison.
[[nodiscard]] double readOr(const an::Result<double>& read)
{
    return read.hasValue() ? *read : std::numeric_limits<double>::quiet_NaN();
}

/// The text a successful write produced, or a marker naming the failure.
[[nodiscard]] std::string textOr(const an::Result<std::string>& written)
{
    return written.hasValue() ? *written : std::string("<failed: ") + written.status().message() + ">";
}

/// The parameter a failure names, or an empty string.
[[nodiscard]] std::string parameterOf(const an::Status& status)
{
    const an::StatusContextField* const field = status.findContext(an::ContextKey::Parameter);
    return field != nullptr && field->kind == an::ContextValueKind::Text ? field->text : std::string{};
}

} // namespace

TEST(SexagesimalTest, NamesEverySeparatorAndRefusesAnythingElse)
{
    EXPECT_TRUE(an::isSexagesimalSeparator(an::SexagesimalSeparator::Space));
    EXPECT_TRUE(an::isSexagesimalSeparator(an::SexagesimalSeparator::Colon));
    EXPECT_FALSE(an::isSexagesimalSeparator(static_cast<an::SexagesimalSeparator>(2)));
    EXPECT_STREQ(an::sexagesimalSeparatorName(an::SexagesimalSeparator::Space), "Space");
    EXPECT_STREQ(an::sexagesimalSeparatorName(an::SexagesimalSeparator::Colon), "Colon");
    EXPECT_STREQ(an::sexagesimalSeparatorName(static_cast<an::SexagesimalSeparator>(9)), "UnknownSexagesimalSeparator");
}

TEST(SexagesimalTest, ReadsDecimalDegreesOnEitherAxis)
{
    EXPECT_EQ(readOr(an::parseRightAscensionDegrees("83.6331")), 83.6331);
    EXPECT_EQ(readOr(an::parseRightAscensionDegrees("  83.6331\t")), 83.6331);
    EXPECT_EQ(readOr(an::parseRightAscensionDegrees("'83.5'")), 83.5);
    EXPECT_EQ(readOr(an::parseRightAscensionDegrees("  ' 83.5  '  ")), 83.5);
    EXPECT_EQ(readOr(an::parseRightAscensionDegrees("+1.5e1")), 15.0);
    EXPECT_EQ(readOr(an::parseRightAscensionDegrees("0")), 0.0);

    // A full turn is the start of the next one, and a negative zero is zero.
    const an::Result<double> fullTurn = an::parseRightAscensionDegrees("360");
    ASSERT_TRUE(fullTurn.hasValue()) << fullTurn.status().message();
    EXPECT_EQ(*fullTurn, 0.0);
    const an::Result<double> negativeZero = an::parseRightAscensionDegrees("-0.0");
    ASSERT_TRUE(negativeZero.hasValue()) << negativeZero.status().message();
    EXPECT_EQ(*negativeZero, 0.0);
    EXPECT_FALSE(std::signbit(*negativeZero));

    EXPECT_EQ(readOr(an::parseDeclinationDegrees("-5.391111")), -5.391111);
    EXPECT_EQ(readOr(an::parseDeclinationDegrees("90")), 90.0);
    EXPECT_EQ(readOr(an::parseDeclinationDegrees("-90.0")), -90.0);
    EXPECT_EQ(readOr(an::parseDeclinationDegrees("'+22.0145'")), 22.0145);
}

TEST(SexagesimalTest, ReadsHoursMinutesAndSecondsInEverySpellingTheProductReads)
{
    const double expected = hoursToDegrees(5.0, 34.0, 31.94);
    for (const std::string_view text :
         {"05 34 31.94", "05:34:31.94", "05h34m31.94s", "05h 34m 31.94s", "05H34M31.94S", " 5 34 31.94 ",
          "'05 34 31.94'", "'05 34 31.94     '", "05  34\t31.94", "+05 34 31.94", "05:34:31.94s"})
    {
        const an::Result<double> read = an::parseRightAscensionDegrees(text);
        ASSERT_TRUE(read.hasValue()) << "'" << text << "': " << read.status().message();
        EXPECT_EQ(*read, expected) << "'" << text << "'";
    }

    // Two fields are hours and minutes, and the last field may carry the fraction.
    EXPECT_EQ(readOr(an::parseRightAscensionDegrees("05 34")), hoursToDegrees(5.0, 34.0, 0.0));
    EXPECT_EQ(readOr(an::parseRightAscensionDegrees("05 34.5")), hoursToDegrees(5.0, 34.5, 0.0));
    EXPECT_EQ(readOr(an::parseRightAscensionDegrees("5h34.5m")), hoursToDegrees(5.0, 34.5, 0.0));
    EXPECT_EQ(readOr(an::parseRightAscensionDegrees("23 59 59.999")), hoursToDegrees(23.0, 59.0, 59.999));

    // Twenty-four hours with nothing after it is the start of the next day.
    const an::Result<double> midnight = an::parseRightAscensionDegrees("24 00 00");
    ASSERT_TRUE(midnight.hasValue()) << midnight.status().message();
    EXPECT_EQ(*midnight, 0.0);
}

TEST(SexagesimalTest, ReadsDegreesMinutesAndSecondsWithTheirSign)
{
    const double positive = arcToDegrees(22.0, 0.0, 52.2);
    for (const std::string_view text :
         {"+22 00 52.2", "22 00 52.2", "22:00:52.2", "+22d00m52.2s", "22D00M52.2S", "22\xC2\xB0 00' 52.2\"",
          "+22\xC2\xB0"
          "00'52.2\"",
          "'+22 00 52.2'"})
    {
        const an::Result<double> read = an::parseDeclinationDegrees(text);
        ASSERT_TRUE(read.hasValue()) << "'" << text << "': " << read.status().message();
        EXPECT_EQ(*read, positive) << "'" << text << "'";
    }

    EXPECT_EQ(readOr(an::parseDeclinationDegrees("-05 23 28")), -arcToDegrees(5.0, 23.0, 28.0));
    EXPECT_EQ(readOr(an::parseDeclinationDegrees("-05:23:28.5")), -arcToDegrees(5.0, 23.0, 28.5));

    // The sign belongs to the whole angle, so a whole field of zero still carries it.
    EXPECT_EQ(readOr(an::parseDeclinationDegrees("-00 30 00")), -0.5);
    EXPECT_EQ(readOr(an::parseDeclinationDegrees("-0 30")), -0.5);
    EXPECT_EQ(readOr(an::parseDeclinationDegrees("-90 00 00")), -90.0);
    EXPECT_EQ(readOr(an::parseDeclinationDegrees("89 59 59.99")), arcToDegrees(89.0, 59.0, 59.99));
}

TEST(SexagesimalTest, RefusesTextThatIsNotAnAngle)
{
    for (const std::string_view text : {"",
                                        "   ",
                                        "''",
                                        "' '",
                                        "abc",
                                        "12h",
                                        "1 2 3 4",
                                        "12.5 30",
                                        "12 30.5 10",
                                        "+-5 30",
                                        "5 -30",
                                        "5 +30",
                                        "5 30 1e1",
                                        "5..5 30",
                                        ". 30",
                                        "5 30 .",
                                        "5 30 x",
                                        "05:34:31.94 extra",
                                        "--5 30",
                                        "5\x01 30",
                                        "5 30 31.9.4",
                                        "'05 34 31",
                                        ":05 34 31",
                                        "\xC2 30 00",
                                        "5 30 00 \xB0",
                                        "inf",
                                        "nan",
                                        "-inf",
                                        "1e5x"})
    {
        const an::Result<double> rightAscension = an::parseRightAscensionDegrees(text);
        ASSERT_FALSE(rightAscension.hasValue()) << "'" << text << "' read as " << *rightAscension;
        EXPECT_EQ(rightAscension.code(), an::ErrorCode::InvalidArgument) << "'" << text << "'";
        const an::Result<double> declination = an::parseDeclinationDegrees(text);
        ASSERT_FALSE(declination.hasValue()) << "'" << text << "' read as " << *declination;
        EXPECT_EQ(declination.code(), an::ErrorCode::InvalidArgument) << "'" << text << "'";
    }
}

TEST(SexagesimalTest, RefusesAnglesOutsideTheirAxis)
{
    for (const std::string_view text : {"-1", "-0.001", "360.5", "1e400", "-01 00 00", "-00 30 00", "25 00 00",
                                        "24 00 01", "24 01", "12 60 00", "12 30 60", "12 60"})
    {
        const an::Result<double> read = an::parseRightAscensionDegrees(text);
        ASSERT_FALSE(read.hasValue()) << "'" << text << "' read as " << *read;
        EXPECT_EQ(read.code(), an::ErrorCode::OutOfRange) << "'" << text << "'";
    }
    for (const std::string_view text :
         {"90.0001", "-90.5", "1e400", "91 00 00", "90 00 01", "-90 00 00.5", "45 60 00", "45 30 60"})
    {
        const an::Result<double> read = an::parseDeclinationDegrees(text);
        ASSERT_FALSE(read.hasValue()) << "'" << text << "' read as " << *read;
        EXPECT_EQ(read.code(), an::ErrorCode::OutOfRange) << "'" << text << "'";
    }
}

TEST(SexagesimalTest, ReadsADirectionAndNamesTheAxisThatFailed)
{
    const an::Result<an::SkyCoordinate> direction = an::parseSkyCoordinate("05 34 31.94", "+22 00 52.2");
    ASSERT_TRUE(direction.hasValue()) << direction.status().message();
    EXPECT_EQ(direction->rightAscensionDegrees, hoursToDegrees(5.0, 34.0, 31.94));
    EXPECT_EQ(direction->declinationDegrees, arcToDegrees(22.0, 0.0, 52.2));

    const an::Result<an::SkyCoordinate> badRightAscension = an::parseSkyCoordinate("25 00 00", "nonsense");
    ASSERT_FALSE(badRightAscension.hasValue());
    EXPECT_EQ(badRightAscension.code(), an::ErrorCode::OutOfRange);
    EXPECT_EQ(parameterOf(badRightAscension.status()), "rightAscension");

    const an::Result<an::SkyCoordinate> badDeclination = an::parseSkyCoordinate("83.5", "nonsense");
    ASSERT_FALSE(badDeclination.hasValue());
    EXPECT_EQ(badDeclination.code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(parameterOf(badDeclination.status()), "declination");
}

TEST(SexagesimalTest, SplitsAtTheStatedPrecisionAndCarriesEveryField)
{
    const an::Result<an::SexagesimalAngle> orion = an::splitRightAscension(hoursToDegrees(5.0, 34.0, 31.94), 2u);
    ASSERT_TRUE(orion.hasValue()) << orion.status().message();
    EXPECT_EQ(
        *orion,
        (an::SexagesimalAngle{
            .negative = false, .whole = 5u, .minutes = 34u, .seconds = 31u, .fraction = 94u, .fractionDigits = 2u}));

    // A fraction that rounds up carries into the seconds, the minutes and the hours.
    const an::Result<an::SexagesimalAngle> carried = an::splitRightAscension(hoursToDegrees(5.0, 59.0, 59.9996), 3u);
    ASSERT_TRUE(carried.hasValue()) << carried.status().message();
    EXPECT_EQ(*carried, (an::SexagesimalAngle{.whole = 6u, .fractionDigits = 3u}));

    // Twenty-four hours carries round to zero, and a negative angle is folded first.
    const an::Result<an::SexagesimalAngle> wrapped = an::splitRightAscension(hoursToDegrees(23.0, 59.0, 59.9996), 3u);
    ASSERT_TRUE(wrapped.hasValue()) << wrapped.status().message();
    EXPECT_EQ(*wrapped, (an::SexagesimalAngle{.fractionDigits = 3u}));
    const an::Result<an::SexagesimalAngle> folded = an::splitRightAscension(-15.0, 0u);
    ASSERT_TRUE(folded.hasValue()) << folded.status().message();
    EXPECT_EQ(*folded, (an::SexagesimalAngle{.whole = 23u}));

    const an::Result<an::SexagesimalAngle> south = an::splitDeclination(-arcToDegrees(5.0, 23.0, 28.04), 1u);
    ASSERT_TRUE(south.hasValue()) << south.status().message();
    EXPECT_EQ(
        *south,
        (an::SexagesimalAngle{
            .negative = true, .whole = 5u, .minutes = 23u, .seconds = 28u, .fraction = 0u, .fractionDigits = 1u}));

    // A declination that rounds to zero is not negative, and one that rounds to the pole is the pole.
    const an::Result<an::SexagesimalAngle> nearlyZero = an::splitDeclination(-1.0e-9, 3u);
    ASSERT_TRUE(nearlyZero.hasValue()) << nearlyZero.status().message();
    EXPECT_EQ(*nearlyZero, (an::SexagesimalAngle{.fractionDigits = 3u}));
    const an::Result<an::SexagesimalAngle> negativeZero = an::splitDeclination(-0.0, 0u);
    ASSERT_TRUE(negativeZero.hasValue()) << negativeZero.status().message();
    EXPECT_FALSE(negativeZero->negative);
    const an::Result<an::SexagesimalAngle> pole = an::splitDeclination(89.99999999, 0u);
    ASSERT_TRUE(pole.hasValue()) << pole.status().message();
    EXPECT_EQ(*pole, (an::SexagesimalAngle{.whole = 90u}));

    // Every digit the maximum allows is kept.
    const an::Result<an::SexagesimalAngle> fine = an::splitDeclination(arcToDegrees(1.0, 2.0, 3.456789), 6u);
    ASSERT_TRUE(fine.hasValue()) << fine.status().message();
    EXPECT_EQ(*fine, (an::SexagesimalAngle{
                         .whole = 1u, .minutes = 2u, .seconds = 3u, .fraction = 456789u, .fractionDigits = 6u}));
}

TEST(SexagesimalTest, RefusesToSplitWhatItCannotRepresent)
{
    const double notFinite[] = {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
                                -std::numeric_limits<double>::infinity()};
    for (const double degrees : notFinite)
    {
        EXPECT_EQ(an::splitRightAscension(degrees, 2u).code(), an::ErrorCode::InvalidArgument);
        EXPECT_EQ(an::splitDeclination(degrees, 2u).code(), an::ErrorCode::InvalidArgument);
        EXPECT_EQ(an::formatRightAscension(degrees, 2u).code(), an::ErrorCode::InvalidArgument);
        EXPECT_EQ(an::formatDeclination(degrees, 2u).code(), an::ErrorCode::InvalidArgument);
    }
    EXPECT_EQ(an::splitRightAscension(10.0, an::kMaxSexagesimalFractionDigits + 1u).code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::splitDeclination(10.0, an::kMaxSexagesimalFractionDigits + 1u).code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::splitDeclination(90.000001, 2u).code(), an::ErrorCode::OutOfRange);
    EXPECT_EQ(an::splitDeclination(-91.0, 2u).code(), an::ErrorCode::OutOfRange);
    EXPECT_EQ(an::formatDeclination(95.0, 2u).code(), an::ErrorCode::OutOfRange);
    EXPECT_EQ(an::formatRightAscension(10.0, 2u, static_cast<an::SexagesimalSeparator>(7)).code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::formatDeclination(10.0, 2u, static_cast<an::SexagesimalSeparator>(7)).code(),
              an::ErrorCode::InvalidArgument);
}

TEST(SexagesimalTest, WritesTheSpellingAHeaderCarries)
{
    EXPECT_EQ(textOr(an::formatRightAscension(hoursToDegrees(5.0, 34.0, 31.94), 3u)), "05 34 31.940");
    EXPECT_EQ(textOr(an::formatRightAscension(hoursToDegrees(5.0, 34.0, 31.94), 2u, an::SexagesimalSeparator::Colon)),
              "05:34:31.94");
    EXPECT_EQ(textOr(an::formatRightAscension(hoursToDegrees(5.0, 34.0, 31.94), 0u)), "05 34 32");
    EXPECT_EQ(textOr(an::formatRightAscension(0.0, 1u)), "00 00 00.0");
    EXPECT_EQ(textOr(an::formatRightAscension(359.99999999, 2u)), "00 00 00.00");
    EXPECT_EQ(textOr(an::formatDeclination(arcToDegrees(22.0, 0.0, 52.2), 3u)), "+22 00 52.200");
    EXPECT_EQ(textOr(an::formatDeclination(-arcToDegrees(5.0, 23.0, 28.0), 3u)), "-05 23 28.000");
    EXPECT_EQ(textOr(an::formatDeclination(-arcToDegrees(5.0, 23.0, 28.0), 0u, an::SexagesimalSeparator::Colon)),
              "-05:23:28");
    EXPECT_EQ(textOr(an::formatDeclination(-0.0, 2u)), "+00 00 00.00");
    EXPECT_EQ(textOr(an::formatDeclination(-90.0, 1u)), "-90 00 00.0");
    EXPECT_EQ(textOr(an::formatDeclination(arcToDegrees(0.0, 0.0, 0.05), 6u)), "+00 00 00.050000");
}

TEST(SexagesimalTest, ReadsBackEverythingItWrites)
{
    // Across both axes and every precision, a written angle reads back within half its last digit.
    for (std::uint32_t digits = 0u; digits <= an::kMaxSexagesimalFractionDigits; ++digits)
    {
        const double halfIncrementSeconds = 0.5 * std::pow(10.0, -static_cast<double>(digits));
        for (int step = 0; step < 997; ++step)
        {
            const double rightAscension = std::fmod(step * 0.3612391 + 0.000137, 360.0);
            const an::Result<std::string> written = an::formatRightAscension(rightAscension, digits);
            ASSERT_TRUE(written.hasValue()) << written.status().message();
            const an::Result<double> read = an::parseRightAscensionDegrees(*written);
            ASSERT_TRUE(read.hasValue()) << *written << ": " << read.status().message();
            double difference = std::abs(*read - rightAscension);
            difference = (std::min)(difference, 360.0 - difference);
            EXPECT_LE(difference * 240.0, halfIncrementSeconds * (1.0 + 1e-9) + 1e-9) << *written;

            const double declination = -90.0 + std::fmod(step * 0.1807311, 180.0);
            const an::Result<std::string> writtenDec = an::formatDeclination(declination, digits);
            ASSERT_TRUE(writtenDec.hasValue()) << writtenDec.status().message();
            const an::Result<double> readDec = an::parseDeclinationDegrees(*writtenDec);
            ASSERT_TRUE(readDec.hasValue()) << *writtenDec << ": " << readDec.status().message();
            EXPECT_LE(std::abs(*readDec - declination) * 3600.0, halfIncrementSeconds * (1.0 + 1e-9) + 1e-9)
                << *writtenDec;
        }
    }
}
