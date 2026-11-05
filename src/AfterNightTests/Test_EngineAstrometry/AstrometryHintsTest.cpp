// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for what a frame's header says about its sky before it is solved: the pointing hint,
// the optics and the scale they imply, the observation epoch, and a mosaic's mean pointing.
//
// The card sets are the shapes capture programs write: a mount's decimal `RA` and `DEC`, a target's
// sexagesimal `OBJCTRA` and `OBJCTDEC`, a solved frame's `CRVAL` pair, and the optics keywords under
// each of the spellings the product reads. The scale is held to the small-angle relation with the
// exact constant, and the mean pointing to an independent unit-vector sum in extended precision.

#include <AfterNightEngine/Algorithms/Astrometry/AstrometryHints.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/AcquisitionMetadata.h>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace
{

/// One preserved card in a stated namespace.
[[nodiscard]] an::CodecCard card(std::string keyword, std::string value,
                                 an::CodecNamespace origin = an::CodecNamespace::Fits)
{
    an::Result<an::CodecCard> made = an::CodecCard::create(origin, std::move(keyword), std::move(value), std::string{});
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.value();
}

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

/// The count a failure carries under a key, or the largest count when it carries none.
[[nodiscard]] std::uint64_t countOf(const an::Status& status, an::ContextKey key)
{
    const an::StatusContextField* const field = status.findContext(key);
    return field != nullptr && field->kind == an::ContextValueKind::Count ? field->count
                                                                          : std::numeric_limits<std::uint64_t>::max();
}

/// The parameter a failure names, or an empty string.
[[nodiscard]] std::string parameterOf(const an::Status& status)
{
    const an::StatusContextField* const field = status.findContext(an::ContextKey::Parameter);
    return field != nullptr && field->kind == an::ContextValueKind::Text ? field->text : std::string{};
}

/// An instant from whole seconds since the Unix epoch and a fraction of one in nanoseconds.
[[nodiscard]] an::UtcInstant instantOf(std::int64_t seconds, std::int64_t nanoseconds = 0)
{
    return an::UtcInstant{.nanosecondsSinceEpoch = seconds * 1000000000 + nanoseconds};
}

/// The mean direction of a set, by an independent sum of unit vectors in extended precision.
[[nodiscard]] an::SkyCoordinate oracleMean(std::span<const an::SkyCoordinate> directions)
{
    long double x = 0.0L;
    long double y = 0.0L;
    long double z = 0.0L;
    const long double radiansPerDegree = std::numbers::pi_v<long double> / 180.0L;
    for (const an::SkyCoordinate& direction : directions)
    {
        const long double rightAscension = direction.rightAscensionDegrees * radiansPerDegree;
        const long double declination = direction.declinationDegrees * radiansPerDegree;
        x += std::cos(declination) * std::cos(rightAscension);
        y += std::cos(declination) * std::sin(rightAscension);
        z += std::sin(declination);
    }
    long double rightAscension = std::atan2(y, x) / radiansPerDegree;
    if (rightAscension < 0.0L)
    {
        rightAscension += 360.0L;
    }
    const long double declination = std::atan2(z, std::hypot(x, y)) / radiansPerDegree;
    return an::SkyCoordinate{.rightAscensionDegrees = static_cast<double>(rightAscension),
                             .declinationDegrees = static_cast<double>(declination)};
}

} // namespace

TEST(AstrometryHintsTest, NamesEveryPointingPairAndItsKeywords)
{
    EXPECT_TRUE(an::isPointingCards(an::PointingCards::ReferenceValue));
    EXPECT_TRUE(an::isPointingCards(an::PointingCards::Telescope));
    EXPECT_FALSE(an::isPointingCards(static_cast<an::PointingCards>(5)));
    EXPECT_STREQ(an::pointingCardsName(an::PointingCards::ReferenceValue), "ReferenceValue");
    EXPECT_STREQ(an::pointingCardsName(an::PointingCards::Pointing), "Pointing");
    EXPECT_STREQ(an::pointingCardsName(an::PointingCards::Object), "Object");
    EXPECT_STREQ(an::pointingCardsName(an::PointingCards::ObjectShort), "ObjectShort");
    EXPECT_STREQ(an::pointingCardsName(an::PointingCards::Telescope), "Telescope");
    EXPECT_STREQ(an::pointingCardsName(static_cast<an::PointingCards>(9)), "UnknownPointingCards");

    const std::array<std::pair<const char*, const char*>, 5> keywords{
        {{"CRVAL1", "CRVAL2"}, {"RA", "DEC"}, {"OBJCTRA", "OBJCTDEC"}, {"OBJRA", "OBJDEC"}, {"TELRA", "TELDEC"}}};
    for (std::uint8_t index = 0u; index < keywords.size(); ++index)
    {
        const an::PointingKeywords named = an::pointingCardKeywords(static_cast<an::PointingCards>(index));
        EXPECT_EQ(named.rightAscension, keywords[index].first);
        EXPECT_EQ(named.declination, keywords[index].second);
    }
    const an::PointingKeywords unknown = an::pointingCardKeywords(static_cast<an::PointingCards>(9));
    EXPECT_TRUE(unknown.rightAscension.empty());
    EXPECT_TRUE(unknown.declination.empty());

    const std::vector<an::PointingCards> plateSolve(an::plateSolveHintCards().begin(), an::plateSolveHintCards().end());
    EXPECT_EQ(plateSolve, (std::vector<an::PointingCards>{
                              an::PointingCards::ReferenceValue, an::PointingCards::Pointing, an::PointingCards::Object,
                              an::PointingCards::ObjectShort, an::PointingCards::Telescope}));
    const std::vector<an::PointingCards> observed(an::observedPointingCards().begin(),
                                                  an::observedPointingCards().end());
    EXPECT_EQ(observed, (std::vector<an::PointingCards>{an::PointingCards::Pointing, an::PointingCards::Object,
                                                        an::PointingCards::ObjectShort, an::PointingCards::Telescope}));
}

TEST(AstrometryHintsTest, ReadsThePointingTheFirstPairOfTheOrderStates)
{
    const std::vector<an::CodecCard> cards{card("OBJECT", "'M42'"),         card("OBJCTRA", "'05 35 17.3'"),
                                           card("OBJCTDEC", "'-05 23 28'"), card("RA", "83.8221"),
                                           card("DEC", "-5.3911"),          card("CRVAL1", "83.822083"),
                                           card("CRVAL2", "-5.391111")};

    const an::Result<an::PointingHint> solve = an::readPointingHint(cards, an::plateSolveHintCards());
    ASSERT_TRUE(solve.hasValue()) << solve.status().message();
    EXPECT_EQ(solve->source, an::PointingCards::ReferenceValue);
    EXPECT_EQ(solve->direction.rightAscensionDegrees, 83.822083);
    EXPECT_EQ(solve->direction.declinationDegrees, -5.391111);

    const an::Result<an::PointingHint> observed = an::readPointingHint(cards, an::observedPointingCards());
    ASSERT_TRUE(observed.hasValue()) << observed.status().message();
    EXPECT_EQ(observed->source, an::PointingCards::Pointing);
    EXPECT_EQ(observed->direction.rightAscensionDegrees, 83.8221);
    EXPECT_EQ(observed->direction.declinationDegrees, -5.3911);

    // A caller's own order, and a sexagesimal pair read in hours and degrees.
    const std::array<an::PointingCards, 1> objectOnly{an::PointingCards::Object};
    const an::Result<an::PointingHint> object = an::readPointingHint(cards, objectOnly);
    ASSERT_TRUE(object.hasValue()) << object.status().message();
    EXPECT_EQ(object->source, an::PointingCards::Object);
    EXPECT_EQ(object->direction.rightAscensionDegrees, hoursToDegrees(5.0, 35.0, 17.3));
    EXPECT_EQ(object->direction.declinationDegrees, -arcToDegrees(5.0, 23.0, 28.0));
}

TEST(AstrometryHintsTest, PassesOverAPairMissingAKeywordOrAnAngle)
{
    // CRVAL2 is missing, RA does not read, and OBJRA's pair is whole.
    const std::vector<an::CodecCard> cards{
        card("CRVAL1", "10.5"),        card("RA", "'not an angle'"), card("DEC", "12.0"),
        card("OBJCTRA", "'25 00 00'"), card("OBJCTDEC", "+10"),      card("OBJRA", "'12 30 00'"),
        card("OBJDEC", "'+41 16 09'"), card("TELRA", "1.0"),         card("TELDEC", "2.0")};
    const an::Result<an::PointingHint> hint = an::readPointingHint(cards, an::plateSolveHintCards());
    ASSERT_TRUE(hint.hasValue()) << hint.status().message();
    EXPECT_EQ(hint->source, an::PointingCards::ObjectShort);
    EXPECT_EQ(hint->direction.rightAscensionDegrees, hoursToDegrees(12.0, 30.0, 0.0));
    EXPECT_EQ(hint->direction.declinationDegrees, arcToDegrees(41.0, 16.0, 9.0));

    // The first card of a keyword speaks, even when a later one would read.
    const std::vector<an::CodecCard> duplicated{card("RA", "'bad'"), card("RA", "10.0"), card("DEC", "20.0"),
                                                card("TELRA", "30.0"), card("TELDEC", "40.0")};
    const an::Result<an::PointingHint> first = an::readPointingHint(duplicated, an::plateSolveHintCards());
    ASSERT_TRUE(first.hasValue()) << first.status().message();
    EXPECT_EQ(first->source, an::PointingCards::Telescope);

    // A full turn of right ascension is folded, as every direction's is.
    const std::vector<an::CodecCard> turn{card("RA", "360"), card("DEC", "0")};
    const an::Result<an::PointingHint> folded = an::readPointingHint(turn, an::plateSolveHintCards());
    ASSERT_TRUE(folded.hasValue()) << folded.status().message();
    EXPECT_EQ(folded->direction.rightAscensionDegrees, 0.0);
}

TEST(AstrometryHintsTest, ReadsOnlyFitsCardsAndSaysWhenNothingPoints)
{
    const std::vector<an::CodecCard> foreign{card("RA", "10.0", an::CodecNamespace::Xisf),
                                             card("DEC", "20.0", an::CodecNamespace::Xisf)};
    const an::Result<an::PointingHint> none = an::readPointingHint(foreign, an::plateSolveHintCards());
    ASSERT_FALSE(none.hasValue());
    EXPECT_EQ(none.code(), an::ErrorCode::EmptySelection);

    const std::vector<an::CodecCard> cards{card("RA", "10.0"), card("DEC", "20.0")};
    EXPECT_EQ(an::readPointingHint(cards, std::span<const an::PointingCards>{}).code(), an::ErrorCode::EmptySelection);
    EXPECT_EQ(an::readPointingHint({}, an::plateSolveHintCards()).code(), an::ErrorCode::EmptySelection);

    const std::array<an::PointingCards, 2> outside{an::PointingCards::Pointing, static_cast<an::PointingCards>(8)};
    const an::Result<an::PointingHint> refused = an::readPointingHint(cards, outside);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(parameterOf(refused.status()), "order");
}

TEST(AstrometryHintsTest, ReadsEachOpticalFactFromTheFirstKeywordThatStatesIt)
{
    const std::vector<an::CodecCard> cards{
        card("FOCALLEN", "0"),    card("FOCAL", "530.0"),     card("XPIXSZ", "'n/a'"), card("PIXSIZE", "-3.76"),
        card("PIXSIZE1", "3.76"), card("D001SCAL", "0.7314"), card("DRIZMULT", "2.0"), card("EXPTIME", "300")};
    const an::OpticalHints optics = an::readOpticalHints(cards);
    EXPECT_EQ(optics.focalLengthMillimetres, std::optional<double>(530.0));
    EXPECT_EQ(optics.pixelSizeMicrometres, std::optional<double>(3.76));
    EXPECT_EQ(optics.drizzleScaleArcsecPerPixel, std::optional<double>(0.7314));
    EXPECT_EQ(optics.drizzleMultiplier, std::optional<double>(2.0));

    // The preferred spellings win, a quoted number is a number, and the Fortran exponent reads.
    const std::vector<an::CodecCard> preferred{card("FOCALLEN", "'400.5'"), card("FOCAL", "999"),
                                               card("XPIXSZ", "4.63D0"), card("PIXSIZE", "1.0")};
    const an::OpticalHints first = an::readOpticalHints(preferred);
    EXPECT_EQ(first.focalLengthMillimetres, std::optional<double>(400.5));
    EXPECT_EQ(first.pixelSizeMicrometres, std::optional<double>(4.63));
    EXPECT_FALSE(first.drizzleScaleArcsecPerPixel.has_value());
    EXPECT_FALSE(first.drizzleMultiplier.has_value());

    // Nothing that is not a positive finite number is a fact, and another namespace states nothing.
    const std::vector<an::CodecCard> nothing{card("FOCALLEN", "inf"), card("XPIXSZ", "nan"), card("D001SCAL", "1e400"),
                                             card("DRIZMULT", "-1"), card("FOCAL", "200", an::CodecNamespace::Tiff)};
    EXPECT_EQ(an::readOpticalHints(nothing), an::OpticalHints{});
}

TEST(AstrometryHintsTest, ComputesThePlateScaleAndItsInverseWithTheExactConstant)
{
    EXPECT_EQ(an::kArcsecondsPerRadian, 648000.0 / std::numbers::pi);

    const an::Result<double> scale = an::computePlateScaleArcsecPerPixel(3.76, 530.0);
    ASSERT_TRUE(scale.hasValue()) << scale.status().message();
    EXPECT_DOUBLE_EQ(*scale, an::kArcsecondsPerRadian * 3.76 / (1000.0 * 530.0));
    EXPECT_NEAR(*scale, 1.4633, 1e-4);

    const an::Result<double> focal = an::computeFocalLengthMillimetres(3.76, *scale);
    ASSERT_TRUE(focal.hasValue()) << focal.status().message();
    EXPECT_NEAR(*focal, 530.0, 530.0 * 1e-14);

    const double bad[] = {0.0, -1.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()};
    for (const double value : bad)
    {
        const an::Result<double> pixel = an::computePlateScaleArcsecPerPixel(value, 530.0);
        ASSERT_FALSE(pixel.hasValue());
        EXPECT_EQ(pixel.code(), an::ErrorCode::InvalidArgument);
        EXPECT_EQ(parameterOf(pixel.status()), "pixelSizeMicrometres");
        const an::Result<double> focalLength = an::computePlateScaleArcsecPerPixel(3.76, value);
        ASSERT_FALSE(focalLength.hasValue());
        EXPECT_EQ(parameterOf(focalLength.status()), "focalLengthMillimetres");
        const an::Result<double> solved = an::computeFocalLengthMillimetres(3.76, value);
        ASSERT_FALSE(solved.hasValue());
        EXPECT_EQ(parameterOf(solved.status()), "plateScaleArcsecPerPixel");
    }

    // A scale that overflows is refused rather than returned as infinity.
    EXPECT_EQ(an::computePlateScaleArcsecPerPixel(1e305, 1e-10).code(), an::ErrorCode::OutOfRange);
    EXPECT_EQ(an::computeFocalLengthMillimetres(1e305, 1e-10).code(), an::ErrorCode::OutOfRange);
}

TEST(AstrometryHintsTest, ResolvesThePixelSizeADrizzleLeftAndTheScaleHint)
{
    // No drizzle: the stated pixel size, and the scale behind the focal length.
    const an::OpticalHints plain{.focalLengthMillimetres = 530.0, .pixelSizeMicrometres = 3.76};
    const an::Result<double> plainPixel = an::resolveEffectivePixelSizeMicrometres(plain);
    ASSERT_TRUE(plainPixel.hasValue()) << plainPixel.status().message();
    EXPECT_EQ(*plainPixel, 3.76);
    const an::Result<double> plainScale = an::resolvePlateScaleHint(plain);
    ASSERT_TRUE(plainScale.hasValue()) << plainScale.status().message();
    EXPECT_EQ(*plainScale, *an::computePlateScaleArcsecPerPixel(3.76, 530.0));

    // A stated multiplier divides the pixel.
    const an::OpticalHints multiplied{
        .focalLengthMillimetres = 530.0, .pixelSizeMicrometres = 3.76, .drizzleMultiplier = 2.0};
    EXPECT_EQ(*an::resolveEffectivePixelSizeMicrometres(multiplied), 1.88);
    EXPECT_EQ(*an::resolvePlateScaleHint(multiplied), *an::computePlateScaleArcsecPerPixel(1.88, 530.0));

    // Without one, the drizzle scale against the optics' own scale is the multiplier - and the stated
    // drizzle scale is the hint itself.
    const an::OpticalHints drizzled{
        .focalLengthMillimetres = 530.0, .pixelSizeMicrometres = 3.76, .drizzleScaleArcsecPerPixel = 0.5};
    const double native = *an::computePlateScaleArcsecPerPixel(3.76, 530.0);
    const an::Result<double> drizzledPixel = an::resolveEffectivePixelSizeMicrometres(drizzled);
    ASSERT_TRUE(drizzledPixel.hasValue()) << drizzledPixel.status().message();
    EXPECT_DOUBLE_EQ(*drizzledPixel, 3.76 / (native / 0.5));
    EXPECT_EQ(*an::resolvePlateScaleHint(drizzled), 0.5);

    // A drizzle scale without the focal length to compare it with leaves the pixel as stated.
    const an::OpticalHints noFocal{.pixelSizeMicrometres = 3.76, .drizzleScaleArcsecPerPixel = 0.5};
    EXPECT_EQ(*an::resolveEffectivePixelSizeMicrometres(noFocal), 3.76);
    EXPECT_EQ(*an::resolvePlateScaleHint(noFocal), 0.5);

    // What cannot be resolved says why.
    EXPECT_EQ(an::resolveEffectivePixelSizeMicrometres(an::OpticalHints{.focalLengthMillimetres = 530.0}).code(),
              an::ErrorCode::EmptySelection);
    EXPECT_EQ(an::resolvePlateScaleHint(an::OpticalHints{.pixelSizeMicrometres = 3.76}).code(),
              an::ErrorCode::EmptySelection);
    EXPECT_EQ(an::resolvePlateScaleHint(an::OpticalHints{}).code(), an::ErrorCode::EmptySelection);
    const an::Result<double> negative =
        an::resolveEffectivePixelSizeMicrometres(an::OpticalHints{.pixelSizeMicrometres = -3.76});
    ASSERT_FALSE(negative.hasValue());
    EXPECT_EQ(negative.code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(parameterOf(negative.status()), "pixelSizeMicrometres");
    const an::Result<double> badMultiplier = an::resolvePlateScaleHint(
        an::OpticalHints{.focalLengthMillimetres = 530.0, .pixelSizeMicrometres = 3.76, .drizzleMultiplier = 0.0});
    ASSERT_FALSE(badMultiplier.hasValue());
    EXPECT_EQ(badMultiplier.code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(parameterOf(badMultiplier.status()), "drizzleMultiplier");
    const an::Result<double> badScale = an::resolvePlateScaleHint(
        an::OpticalHints{.drizzleScaleArcsecPerPixel = std::numeric_limits<double>::infinity()});
    ASSERT_FALSE(badScale.hasValue());
    EXPECT_EQ(parameterOf(badScale.status()), "drizzleScaleArcsecPerPixel");
}

TEST(AstrometryHintsTest, PlacesAnInstantOnTheJulianEpoch)
{
    // J2000.0 is 2000-01-01T12:00:00, and Gaia's 2016.0 is 2016-01-01T12:00:00.
    EXPECT_EQ(an::julianEpochYear(instantOf(946728000)), 2000.0);
    EXPECT_EQ(an::julianEpochYear(instantOf(1451649600)), 2016.0);

    // Half a Julian year later, and the Unix epoch itself, half a day short of thirty Julian years back.
    EXPECT_DOUBLE_EQ(an::julianEpochYear(instantOf(946728000 + 15778800)), 2000.5);
    EXPECT_DOUBLE_EQ(an::julianEpochYear(instantOf(0)), 2000.0 - 10957.5 / 365.25);

    // A fraction of a second is kept, and an instant before 1970 works the same way.
    EXPECT_DOUBLE_EQ(an::julianEpochYear(instantOf(946728000, 500000000)), 2000.0 + 0.5 / 31557600.0);
    EXPECT_DOUBLE_EQ(an::julianEpochYear(instantOf(-631152000)), 2000.0 - (946728000.0 + 631152000.0) / 31557600.0);

    // Every representable instant has an epoch.
    EXPECT_TRUE(std::isfinite(an::julianEpochYear(an::UtcInstant{std::numeric_limits<std::int64_t>::min()})));
    EXPECT_TRUE(std::isfinite(an::julianEpochYear(an::UtcInstant{std::numeric_limits<std::int64_t>::max()})));
}

TEST(AstrometryHintsTest, CentresDirectionsOnTheSphere)
{
    // Two panels either side of zero hours centre on zero hours, not twelve.
    const std::array<an::SkyCoordinate, 2> straddling{
        an::SkyCoordinate{.rightAscensionDegrees = 359.5, .declinationDegrees = 10.0},
        an::SkyCoordinate{.rightAscensionDegrees = 0.5, .declinationDegrees = 10.0}};
    const an::Result<an::SkyCoordinate> zero = an::sphericalMeanDirection(straddling);
    ASSERT_TRUE(zero.hasValue()) << zero.status().message();
    EXPECT_TRUE(zero->rightAscensionDegrees < 1e-9 || zero->rightAscensionDegrees > 360.0 - 1e-9)
        << zero->rightAscensionDegrees;
    EXPECT_GT(zero->declinationDegrees, 10.0);
    EXPECT_NEAR(zero->declinationDegrees, oracleMean(straddling).declinationDegrees, 1e-12);

    // A mosaic of four panels around the pole centres on the pole.
    const std::array<an::SkyCoordinate, 4> polar{
        an::SkyCoordinate{.rightAscensionDegrees = 0.0, .declinationDegrees = 88.0},
        an::SkyCoordinate{.rightAscensionDegrees = 90.0, .declinationDegrees = 88.0},
        an::SkyCoordinate{.rightAscensionDegrees = 180.0, .declinationDegrees = 88.0},
        an::SkyCoordinate{.rightAscensionDegrees = 270.0, .declinationDegrees = 88.0}};
    const an::Result<an::SkyCoordinate> pole = an::sphericalMeanDirection(polar);
    ASSERT_TRUE(pole.hasValue()) << pole.status().message();
    EXPECT_NEAR(pole->declinationDegrees, 90.0, 1e-9);

    // An ordinary mosaic agrees with the independent sum, and one direction is its own mean.
    const std::array<an::SkyCoordinate, 5> panels{
        an::SkyCoordinate{.rightAscensionDegrees = 83.1, .declinationDegrees = -5.9},
        an::SkyCoordinate{.rightAscensionDegrees = 84.3, .declinationDegrees = -5.1},
        an::SkyCoordinate{.rightAscensionDegrees = 83.7, .declinationDegrees = -4.2},
        an::SkyCoordinate{.rightAscensionDegrees = 82.9, .declinationDegrees = -4.8},
        an::SkyCoordinate{.rightAscensionDegrees = 84.0, .declinationDegrees = -6.3}};
    const an::Result<an::SkyCoordinate> centre = an::sphericalMeanDirection(panels);
    ASSERT_TRUE(centre.hasValue()) << centre.status().message();
    const an::SkyCoordinate expected = oracleMean(panels);
    EXPECT_NEAR(centre->rightAscensionDegrees, expected.rightAscensionDegrees, 1e-11);
    EXPECT_NEAR(centre->declinationDegrees, expected.declinationDegrees, 1e-11);
    const std::array<an::SkyCoordinate, 1> single{panels[2]};
    const an::Result<an::SkyCoordinate> alone = an::sphericalMeanDirection(single);
    ASSERT_TRUE(alone.hasValue()) << alone.status().message();
    EXPECT_NEAR(alone->rightAscensionDegrees, panels[2].rightAscensionDegrees, 1e-12);
    EXPECT_NEAR(alone->declinationDegrees, panels[2].declinationDegrees, 1e-12);
}

TEST(AstrometryHintsTest, RefusesASetWithNoMeanDirection)
{
    EXPECT_EQ(an::sphericalMeanDirection({}).code(), an::ErrorCode::EmptySelection);

    const std::array<an::SkyCoordinate, 2> opposite{
        an::SkyCoordinate{.rightAscensionDegrees = 0.0, .declinationDegrees = 0.0},
        an::SkyCoordinate{.rightAscensionDegrees = 180.0, .declinationDegrees = 0.0}};
    EXPECT_EQ(an::sphericalMeanDirection(opposite).code(), an::ErrorCode::InvalidData);

    const std::array<an::SkyCoordinate, 3> invalid{
        an::SkyCoordinate{.rightAscensionDegrees = 10.0, .declinationDegrees = 0.0},
        an::SkyCoordinate{.rightAscensionDegrees = 20.0, .declinationDegrees = 0.0},
        an::SkyCoordinate{.rightAscensionDegrees = 30.0, .declinationDegrees = 91.0}};
    const an::Result<an::SkyCoordinate> refused = an::sphericalMeanDirection(invalid);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(countOf(refused.status(), an::ContextKey::Completed), 2u);
}
