// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the header-card bridge, driven by headers real solvers write.
//
// The card sets below are the shapes this engine actually meets: an astrometry.net solve with a
// second-order polynomial and its written inverse, an older survey cutout that states a scale and a
// rotation instead of a matrix, and a solver that writes its exponents with the Fortran letter. Each
// is here because it is a path a hand-written fixture would not have exercised.
//
// The half-pixel case is the one worth reading twice. A header counts pixels from one with the first
// pixel's centre at exactly one; this engine counts from zero with that centre at one half. A bridge
// that forgets the difference produces a solution that projects, round-trips, and is half a pixel
// out everywhere - which is invisible in every test that only checks a round trip.

#include <AfterNightEngine/Algorithms/Astrometry/WcsCards.h>
#include <AfterNightEngine/Core/ErrorCode.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace
{

/// One preserved header card in the format's own namespace.
[[nodiscard]] an::CodecCard card(std::string keyword, std::string value)
{
    an::Result<an::CodecCard> made =
        an::CodecCard::create(an::CodecNamespace::Fits, std::move(keyword), std::move(value), std::string{});
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.value();
}

/// A solved frame's header as an astrometry.net index run writes it.
///
/// A 2048 by 1536 frame of the Orion belt region at about two point two arcseconds a pixel, with a
/// second-order polynomial in both directions and the written inverse the solver emits beside it.
[[nodiscard]] std::vector<an::CodecCard> solvedHeader()
{
    return {card("SIMPLE", "T"),
            card("BITPIX", "16"),
            card("NAXIS", "2"),
            card("NAXIS1", "2048"),
            card("NAXIS2", "1536"),
            card("OBJECT", "'M42'"),
            card("CTYPE1", "'RA---TAN-SIP'"),
            card("CTYPE2", "'DEC--TAN-SIP'"),
            card("CUNIT1", "'deg'"),
            card("CUNIT2", "'deg'"),
            card("EQUINOX", "2000.0"),
            card("RADESYS", "'ICRS'"),
            card("CRPIX1", "1024.5"),
            card("CRPIX2", "768.5"),
            card("CRVAL1", "83.822083000000"),
            card("CRVAL2", "-5.391111000000"),
            card("CD1_1", "-6.1111111111E-04"),
            card("CD1_2", "1.2000000000E-06"),
            card("CD2_1", "1.1000000000E-06"),
            card("CD2_2", "6.1111111111E-04"),
            card("A_ORDER", "2"),
            card("A_0_2", "2.5077000000E-07"),
            card("A_1_1", "-1.1857000000E-06"),
            card("A_2_0", "8.9146000000E-07"),
            card("B_ORDER", "2"),
            card("B_0_2", "-1.3437000000E-06"),
            card("B_1_1", "6.3402000000E-07"),
            card("B_2_0", "3.2088000000E-07"),
            card("AP_ORDER", "2"),
            card("AP_0_2", "-2.5074000000E-07"),
            card("AP_1_1", "1.1856000000E-06"),
            card("AP_2_0", "-8.9139000000E-07"),
            card("BP_ORDER", "2"),
            card("BP_0_2", "1.3436000000E-06"),
            card("BP_1_1", "-6.3398000000E-07"),
            card("BP_2_0", "-3.2085000000E-07")};
}

/// The same field written the older way: a scale per axis and one rotation angle.
[[nodiscard]] std::vector<an::CodecCard> rotatedScaleHeader(const bool explicitFrame = true)
{
    std::vector<an::CodecCard> cards{card("CTYPE1", "'RA---TAN'"),     card("CTYPE2", "'DEC--TAN'"),
                                     card("CRPIX1", "512.0"),          card("CRPIX2", "512.0"),
                                     card("CRVAL1", "202.469583"),     card("CRVAL2", "47.195278"),
                                     card("CDELT1", "-2.7777778E-04"), card("CDELT2", "2.7777778E-04"),
                                     card("CROTA2", "1.53"),           card("EQUINOX", "2000.0")};
    if (explicitFrame)
    {
        cards.push_back(card("RADESYS", "'ICRS'"));
    }
    return cards;
}

/// Replaces one card's value, keeping every other card as it was.
void setCard(std::vector<an::CodecCard>& cards, const std::string& keyword, const std::string& value)
{
    for (an::CodecCard& existing : cards)
    {
        if (existing.keyword() == keyword)
        {
            existing = card(keyword, value);
            return;
        }
    }
    cards.push_back(card(keyword, value));
}

/// Removes every card with a keyword.
void eraseCard(std::vector<an::CodecCard>& cards, const std::string& keyword)
{
    cards.erase(std::remove_if(cards.begin(), cards.end(),
                               [&keyword](const an::CodecCard& existing) { return existing.keyword() == keyword; }),
                cards.end());
}

/// The value of one card of a built header.
[[nodiscard]] std::string valueOf(const std::vector<an::CodecCard>& cards, const std::string& keyword)
{
    for (const an::CodecCard& existing : cards)
    {
        if (existing.keyword() == keyword)
        {
            return existing.value();
        }
    }
    return {};
}

TEST(WcsCardsTest, TheVocabularyCoversWhatASolutionWritesAndNothingElse)
{
    for (const char* keyword :
         {"CTYPE1", "CRVAL2",  "CRPIX1",  "CD1_1",   "CD2_2",   "PC1_2",   "CDELT1",   "CUNIT2", "CROTA2", "PV2_1",
          "PS1_0",  "EQUINOX", "RADESYS", "LONPOLE", "WCSAXES", "A_ORDER", "BP_ORDER", "A_2_0",  "AP_0_2", "B_DMAX"})
    {
        EXPECT_TRUE(an::isWcsKeyword(keyword)) << keyword;
    }
    for (const char* keyword : {"", "SIMPLE", "BITPIX", "NAXIS1", "OBJECT", "EXPTIME", "ANARTID", "A_ORDERS",
                                "APERTURE", "PCOUNT", "GCOUNT", "PSCALE"})
    {
        EXPECT_FALSE(an::isWcsKeyword(keyword)) << keyword;
    }
}

TEST(WcsCardsTest, UnsupportedReferenceFramesAndPolesAreNeverSilentlyReinterpreted)
{
    for (const auto& frame : {std::string("'FK4'"), std::string("'FK5'"), std::string("'GAPPT'")})
    {
        auto cards = rotatedScaleHeader(false);
        cards.push_back(card("RADESYS", frame));
        EXPECT_EQ(an::checkWcsCards(cards), an::WcsCardError::UnsupportedFrame);
        EXPECT_EQ(an::parseWcsCards(cards, 1024u, 1024u).code(), an::ErrorCode::Unsupported);
    }
    auto implicitFrame = rotatedScaleHeader(false);
    EXPECT_EQ(an::checkWcsCards(implicitFrame), an::WcsCardError::UnsupportedFrame);
    for (const auto* keyword : {"RADESYS", "RADECSYS"})
    {
        auto explicitIcrs = rotatedScaleHeader(false);
        explicitIcrs.push_back(card(keyword, "'ICRS'"));
        EXPECT_EQ(an::checkWcsCards(explicitIcrs), an::WcsCardError::None);
        explicitIcrs.push_back(card("LONPOLE", "0.0"));
        EXPECT_EQ(an::checkWcsCards(explicitIcrs), an::WcsCardError::UnsupportedFrame);
    }
}

TEST(WcsCardsTest, ALatitudePoleNeverMovesAZenithalProjectionsPole)
{
    // astrometry.net writes the default LONPOLE and a LATPOLE of zero into every solution. A zenithal
    // projection's native pole is its reference point, so LATPOLE fixes nothing and must not refuse
    // the solution; it still has to be a number.
    std::vector<an::CodecCard> solved = solvedHeader();
    solved.push_back(card("LONPOLE", "180.0"));
    solved.push_back(card("LATPOLE", "0.0"));
    EXPECT_EQ(an::checkWcsCards(solved), an::WcsCardError::None);
    const an::Result<an::WcsSolution> withPole = an::parseWcsCards(solved, 2048u, 1536u);
    ASSERT_TRUE(withPole.hasValue()) << withPole.status().message();
    const an::Result<an::WcsSolution> withoutPole = an::parseWcsCards(solvedHeader(), 2048u, 1536u);
    ASSERT_TRUE(withoutPole.hasValue()) << withoutPole.status().message();
    EXPECT_EQ(*withPole, *withoutPole);

    for (const char* latitude : {"90.0", "-45.5", "-90"})
    {
        setCard(solved, "LATPOLE", latitude);
        EXPECT_EQ(an::checkWcsCards(solved), an::WcsCardError::None) << latitude;
    }
    setCard(solved, "LATPOLE", "'north'");
    EXPECT_EQ(an::checkWcsCards(solved), an::WcsCardError::MalformedValue);
}

TEST(WcsCardsTest, ASeparateSipOriginIsRefusedBeforeSerialization)
{
    an::SipDistortionRequest distortion;
    distortion.referencePixel = {0., 0.};
    distortion.forwardX.order = 2u;
    distortion.forwardY.order = 2u;
    distortion.forwardX.coefficients[2][0] = 1.e-4;
    const auto sip = an::SipDistortion::create(distortion);
    ASSERT_TRUE(sip.hasValue());
    const auto solution = an::WcsSolution::create({.referencePixel = {100., 100.},
                                                   .referencePoint = {80., 20.},
                                                   .linear = {-.001, 0., 0., .001},
                                                   .imageWidth = 200u,
                                                   .imageHeight = 200u,
                                                   .distortion = *sip});
    EXPECT_EQ(solution.code(), an::ErrorCode::IncompatibleArguments);
}

TEST(WcsCardsTest, AHeaderWithNoSolutionSaysSoRatherThanReportingABrokenOne)
{
    const std::vector<an::CodecCard> plain = {card("SIMPLE", "T"), card("NAXIS1", "2048"), card("EQUINOX", "2000.0")};
    EXPECT_EQ(an::checkWcsCards(plain), an::WcsCardError::NoSolution);
    EXPECT_EQ(an::parseWcsCards(plain, 2048u, 1536u).code(), an::ErrorCode::EmptySelection);

    EXPECT_EQ(an::checkWcsCards(std::span<const an::CodecCard>{}), an::WcsCardError::NoSolution);

    // A card that spells a world-coordinate keyword under another format's namespace means whatever
    // that format means by it, and is not read here.
    an::Result<an::CodecCard> foreign =
        an::CodecCard::create(an::CodecNamespace::Xisf, "CRVAL1", "10.0", std::string{});
    ASSERT_TRUE(foreign.hasValue()) << foreign.status().message();
    const std::vector<an::CodecCard> elsewhere = {foreign.value()};
    EXPECT_EQ(an::checkWcsCards(elsewhere), an::WcsCardError::NoSolution);
}

TEST(WcsCardsTest, ASolvedHeaderParsesIntoTheNumbersItStates)
{
    const std::vector<an::CodecCard> cards = solvedHeader();
    ASSERT_EQ(an::checkWcsCards(cards), an::WcsCardError::None);

    const an::Result<an::WcsSolution> solution = an::parseWcsCards(cards, 2048u, 1536u);
    ASSERT_TRUE(solution.hasValue()) << solution.status().message();

    // The half pixel between the two conventions, applied once.
    EXPECT_DOUBLE_EQ(solution->referencePixel().x, 1024.0);
    EXPECT_DOUBLE_EQ(solution->referencePixel().y, 768.0);
    EXPECT_DOUBLE_EQ(solution->distortion().referencePixel().x, 1024.0);
    EXPECT_DOUBLE_EQ(solution->distortion().referencePixel().y, 768.0);

    EXPECT_DOUBLE_EQ(solution->referencePoint().rightAscensionDegrees, 83.822083);
    EXPECT_DOUBLE_EQ(solution->referencePoint().declinationDegrees, -5.391111);
    EXPECT_DOUBLE_EQ(solution->linear().cd11, -6.1111111111E-04);
    EXPECT_DOUBLE_EQ(solution->linear().cd22, 6.1111111111E-04);
    EXPECT_EQ(solution->projection(), an::WcsProjection::Gnomonic);
    EXPECT_EQ(solution->imageWidth(), 2048u);

    EXPECT_TRUE(solution->distortion().hasForwardModel());
    EXPECT_TRUE(solution->distortion().hasInverseModel());
    EXPECT_EQ(solution->distortion().forwardX().order, 2u);
    EXPECT_DOUBLE_EQ(solution->distortion().forwardX().coefficients[2][0], 8.9146E-07);
    EXPECT_DOUBLE_EQ(solution->distortion().forwardY().coefficients[0][2], -1.3437E-06);
    EXPECT_DOUBLE_EQ(solution->distortion().inverseX().coefficients[1][1], 1.1856E-06);

    // The square-pixel scale is the determinant's root, so the header's small off-diagonal terms
    // move it away from the diagonal scale by a few parts in a million. That is the number being
    // reported, not an approximation of it.
    EXPECT_NEAR(solution->meanPixelScaleArcsecPerPixel(), 2.2, 1.0e-4);
    EXPECT_FALSE(solution->mirrored());

    // The frame centre of a header whose reference pixel is the frame centre is the reference
    // direction, to within the polynomial's own correction there.
    const an::Result<an::SkyCoordinate> centre = solution->fieldCenter();
    ASSERT_TRUE(centre.hasValue()) << centre.status().message();
    EXPECT_NEAR(centre->rightAscensionDegrees, 83.822083, 1.0e-4);
    EXPECT_NEAR(centre->declinationDegrees, -5.391111, 1.0e-4);
}

TEST(WcsCardsTest, AHeaderThatStatesAScaleAndARotationIsReadAsTheMatrixItMeans)
{
    const std::vector<an::CodecCard> cards = rotatedScaleHeader();
    ASSERT_EQ(an::checkWcsCards(cards), an::WcsCardError::None);

    const an::Result<an::WcsSolution> solution = an::parseWcsCards(cards, 1024u, 1024u);
    ASSERT_TRUE(solution.hasValue()) << solution.status().message();

    constexpr double kCdelt = 2.7777778E-04;
    const double angle = 1.53 * std::numbers::pi / 180.0;
    EXPECT_DOUBLE_EQ(solution->linear().cd11, -kCdelt * std::cos(angle));
    EXPECT_DOUBLE_EQ(solution->linear().cd12, -kCdelt * std::sin(angle));
    EXPECT_DOUBLE_EQ(solution->linear().cd21, -kCdelt * std::sin(angle));
    EXPECT_DOUBLE_EQ(solution->linear().cd22, kCdelt * std::cos(angle));
    EXPECT_NEAR(solution->orientationDegrees(), 1.53, 1.0e-9);
    EXPECT_FALSE(solution->mirrored());
    EXPECT_FALSE(solution->distortion().hasForwardModel());
}

TEST(WcsCardsTest, AHeaderThatStatesAScaleAndAnOrientationMatrixIsReadThroughBoth)
{
    std::vector<an::CodecCard> cards = rotatedScaleHeader();
    eraseCard(cards, "CROTA2");
    setCard(cards, "PC1_1", "0.5");
    setCard(cards, "PC1_2", "0.25");
    setCard(cards, "PC2_1", "-0.25");
    setCard(cards, "PC2_2", "0.5");

    const an::Result<an::WcsSolution> solution = an::parseWcsCards(cards, 1024u, 1024u);
    ASSERT_TRUE(solution.hasValue()) << solution.status().message();

    constexpr double kCdelt = 2.7777778E-04;
    EXPECT_DOUBLE_EQ(solution->linear().cd11, -kCdelt * 0.5);
    EXPECT_DOUBLE_EQ(solution->linear().cd12, -kCdelt * 0.25);
    EXPECT_DOUBLE_EQ(solution->linear().cd21, kCdelt * -0.25);
    EXPECT_DOUBLE_EQ(solution->linear().cd22, kCdelt * 0.5);
}

TEST(WcsCardsTest, TheFortranExponentAndAQuotedNumberAreReadAsNumbers)
{
    std::vector<an::CodecCard> cards = solvedHeader();
    setCard(cards, "CRPIX1", "1.0245000000D+003");
    setCard(cards, "CRPIX2", " +768.5 ");

    const an::Result<an::WcsSolution> solution = an::parseWcsCards(cards, 2048u, 1536u);
    ASSERT_TRUE(solution.hasValue()) << solution.status().message();
    EXPECT_DOUBLE_EQ(solution->referencePixel().x, 1024.0);
    EXPECT_DOUBLE_EQ(solution->referencePixel().y, 768.0);
}

TEST(WcsCardsTest, ASolutionSurvivesBeingWrittenAndReadBack)
{
    for (const std::vector<an::CodecCard>& original : {solvedHeader(), rotatedScaleHeader()})
    {
        const an::Result<an::WcsSolution> first = an::parseWcsCards(original, 2048u, 1536u);
        ASSERT_TRUE(first.hasValue()) << first.status().message();

        const an::Result<std::vector<an::CodecCard>> written = an::buildWcsCards(*first);
        ASSERT_TRUE(written.hasValue()) << written.status().message();

        const an::Result<an::WcsSolution> second = an::parseWcsCards(*written, 2048u, 1536u);
        ASSERT_TRUE(second.hasValue()) << second.status().message();
        EXPECT_EQ(*first, *second);
    }
}

TEST(WcsCardsTest, WhatIsWrittenIsTheConventionsOwnSpelling)
{
    const an::Result<an::WcsSolution> solution = an::parseWcsCards(solvedHeader(), 2048u, 1536u);
    ASSERT_TRUE(solution.hasValue()) << solution.status().message();

    const an::Result<std::vector<an::CodecCard>> written = an::buildWcsCards(*solution);
    ASSERT_TRUE(written.hasValue()) << written.status().message();
    EXPECT_EQ(valueOf(*written, "CTYPE1"), "'RA---TAN-SIP'");
    EXPECT_EQ(valueOf(*written, "CTYPE2"), "'DEC--TAN-SIP'");
    EXPECT_EQ(valueOf(*written, "CUNIT1"), "'deg'");
    // The half pixel goes back the way it came.
    EXPECT_EQ(valueOf(*written, "CRPIX1"), "1024.5");
    EXPECT_EQ(valueOf(*written, "A_ORDER"), "2");

    const an::Result<std::vector<an::CodecCard>> linear = an::buildWcsCards(solution->withoutDistortion());
    ASSERT_TRUE(linear.hasValue()) << linear.status().message();
    EXPECT_EQ(valueOf(*linear, "CTYPE1"), "'RA---TAN'");
    EXPECT_TRUE(valueOf(*linear, "A_ORDER").empty());
}

TEST(WcsCardsTest, ADuplicatedKeywordIsRefusedRatherThanResolvedByOrder)
{
    std::vector<an::CodecCard> cards = solvedHeader();
    cards.push_back(card("CRPIX1", "999.5"));
    EXPECT_EQ(an::checkWcsCards(cards), an::WcsCardError::DuplicateKeyword);
    EXPECT_EQ(an::parseWcsCards(cards, 2048u, 1536u).code(), an::ErrorCode::InvalidData);

    // Two cards that say the same thing say one thing, and are not a contradiction.
    std::vector<an::CodecCard> repeated = solvedHeader();
    repeated.push_back(card("CRPIX1", "1024.5"));
    EXPECT_EQ(an::checkWcsCards(repeated), an::WcsCardError::None);
}

TEST(WcsCardsTest, AProjectionFrameOrUnitThisBuildDoesNotReadIsRefusedByName)
{
    std::vector<an::CodecCard> projection = solvedHeader();
    setCard(projection, "CTYPE1", "'RA---SIN-SIP'");
    setCard(projection, "CTYPE2", "'DEC--SIN-SIP'");
    EXPECT_EQ(an::checkWcsCards(projection), an::WcsCardError::UnsupportedProjection);
    EXPECT_EQ(an::parseWcsCards(projection, 2048u, 1536u).code(), an::ErrorCode::Unsupported);

    std::vector<an::CodecCard> frame = rotatedScaleHeader();
    setCard(frame, "CTYPE1", "'GLON-TAN'");
    setCard(frame, "CTYPE2", "'GLAT-TAN'");
    EXPECT_EQ(an::checkWcsCards(frame), an::WcsCardError::UnsupportedFrame);

    std::vector<an::CodecCard> order = rotatedScaleHeader();
    setCard(order, "CTYPE1", "'DEC--TAN'");
    setCard(order, "CTYPE2", "'RA---TAN'");
    EXPECT_EQ(an::checkWcsCards(order), an::WcsCardError::UnsupportedAxisOrder);

    std::vector<an::CodecCard> mixed = rotatedScaleHeader();
    setCard(mixed, "CTYPE2", "'DEC--SIN'");
    EXPECT_EQ(an::checkWcsCards(mixed), an::WcsCardError::InconsistentAxes);

    std::vector<an::CodecCard> unit = rotatedScaleHeader();
    setCard(unit, "CUNIT1", "'arcsec'");
    EXPECT_EQ(an::checkWcsCards(unit), an::WcsCardError::UnsupportedUnit);
}

TEST(WcsCardsTest, TheDistortionSuffixAndTheDistortionKeywordsMustAgree)
{
    std::vector<an::CodecCard> claimed = rotatedScaleHeader();
    setCard(claimed, "CTYPE1", "'RA---TAN-SIP'");
    setCard(claimed, "CTYPE2", "'DEC--TAN-SIP'");
    EXPECT_EQ(an::checkWcsCards(claimed), an::WcsCardError::InconsistentAxes);

    std::vector<an::CodecCard> unclaimed = solvedHeader();
    setCard(unclaimed, "CTYPE1", "'RA---TAN'");
    setCard(unclaimed, "CTYPE2", "'DEC--TAN'");
    EXPECT_EQ(an::checkWcsCards(unclaimed), an::WcsCardError::InconsistentAxes);

    std::vector<an::CodecCard> halfWritten = solvedHeader();
    eraseCard(halfWritten, "B_ORDER");
    EXPECT_EQ(an::checkWcsCards(halfWritten), an::WcsCardError::MissingKeyword);
}

TEST(WcsCardsTest, AMissingOrMalformedOrOutOfRangeValueIsRefusedWithItsOwnReason)
{
    std::vector<an::CodecCard> missing = solvedHeader();
    eraseCard(missing, "CRVAL2");
    EXPECT_EQ(an::checkWcsCards(missing), an::WcsCardError::MissingKeyword);

    std::vector<an::CodecCard> malformed = solvedHeader();
    setCard(malformed, "CRVAL1", "'not a number'");
    EXPECT_EQ(an::checkWcsCards(malformed), an::WcsCardError::MalformedValue);
    EXPECT_EQ(an::parseWcsCards(malformed, 2048u, 1536u).code(), an::ErrorCode::InvalidArgument);

    std::vector<an::CodecCard> pastThePole = solvedHeader();
    setCard(pastThePole, "CRVAL2", "-91.0");
    EXPECT_EQ(an::checkWcsCards(pastThePole), an::WcsCardError::ValueOutOfRange);

    std::vector<an::CodecCard> tooHigh = solvedHeader();
    setCard(tooHigh, "A_ORDER", "9");
    EXPECT_EQ(an::checkWcsCards(tooHigh), an::WcsCardError::ValueOutOfRange);

    // A coefficient of a degree the order excludes would be dropped by every evaluation, so the
    // header is refused rather than read as if the card were not there.
    std::vector<an::CodecCard> extra = solvedHeader();
    setCard(extra, "A_3_0", "5.0E-09");
    EXPECT_EQ(an::checkWcsCards(extra), an::WcsCardError::ValueOutOfRange);

    std::vector<an::CodecCard> singular = solvedHeader();
    setCard(singular, "CD1_1", "0.0");
    setCard(singular, "CD1_2", "0.0");
    EXPECT_EQ(an::checkWcsCards(singular), an::WcsCardError::SingularTransform);
    EXPECT_EQ(an::parseWcsCards(singular, 2048u, 1536u).code(), an::ErrorCode::InvalidData);

    EXPECT_EQ(an::parseWcsCards(solvedHeader(), 0u, 1536u).code(), an::ErrorCode::InvalidArgument);
}

TEST(WcsCardsTest, WithdrawingASolutionRemovesItsCardsAndKeepsEverythingElse)
{
    const an::Result<an::CodecMetadata> metadata = an::CodecMetadata::create(an::SourceEncoding{}, solvedHeader());
    ASSERT_TRUE(metadata.hasValue()) << metadata.status().message();

    const an::Result<an::CodecMetadata> stripped = an::withoutWcsCards(*metadata);
    ASSERT_TRUE(stripped.hasValue()) << stripped.status().message();
    EXPECT_EQ(an::checkWcsCards(stripped->cards()), an::WcsCardError::NoSolution);

    for (const char* keyword : {"SIMPLE", "BITPIX", "NAXIS", "NAXIS1", "NAXIS2", "OBJECT"})
    {
        EXPECT_NE(stripped->find(an::CodecNamespace::Fits, keyword), nullptr) << keyword;
    }
    for (const char* keyword : {"CTYPE1", "CRPIX1", "CD1_1", "A_ORDER", "AP_2_0", "EQUINOX"})
    {
        EXPECT_EQ(stripped->find(an::CodecNamespace::Fits, keyword), nullptr) << keyword;
    }
}

TEST(WcsCardsTest, WritingASolutionReplacesWhateverSolutionWasThere)
{
    const an::Result<an::CodecMetadata> metadata = an::CodecMetadata::create(an::SourceEncoding{}, solvedHeader());
    ASSERT_TRUE(metadata.hasValue()) << metadata.status().message();

    const an::Result<an::WcsSolution> other = an::parseWcsCards(rotatedScaleHeader(), 1024u, 1024u);
    ASSERT_TRUE(other.hasValue()) << other.status().message();

    const an::Result<an::CodecMetadata> written = an::withWcsCards(*metadata, *other);
    ASSERT_TRUE(written.hasValue()) << written.status().message();

    // One solution, and it is the one just written.
    const an::Result<an::WcsSolution> read = an::parseWcsCards(written->cards(), 1024u, 1024u);
    ASSERT_TRUE(read.hasValue()) << read.status().message();
    EXPECT_EQ(*read, *other);
    EXPECT_EQ(written->find(an::CodecNamespace::Fits, "A_ORDER"), nullptr);
    EXPECT_NE(written->find(an::CodecNamespace::Fits, "OBJECT"), nullptr);
}

TEST(WcsCardsTest, AnXisfSolutionLeavesWithTheKeywordsAndIsReplacedWithThem)
{
    // A PixInsight-solved image's cards: the keywords translated from its solution, the properties of the solution
    // in XISF's namespace and in the one its writer defines - one long enough to continue in a second card - and
    // properties of other namespaces, which have nothing to do with the solution.
    const auto xisf = [](std::string keyword, std::string value, std::string comment) {
        an::Result<an::CodecCard> made =
            an::CodecCard::create(an::CodecNamespace::Xisf, std::move(keyword), std::move(value), std::move(comment));
        EXPECT_TRUE(made.hasValue()) << made.status().message();
        return made.value();
    };
    std::vector<an::CodecCard> cards = solvedHeader();
    cards.push_back(xisf("AstrometricSolution:ProjectionSystem", "Gnomonic", "String"));
    cards.push_back(xisf("AstrometricSolution:ControlPoints:Image", std::string(4096u, '1'), "F64Matrix 1000x2"));
    cards.push_back(xisf("AstrometricSolution:ControlPoints:Image", "2,3", "continued"));
    cards.push_back(xisf("PCL:AstrometricSolution:Grid:Fingerprint", "f00d", "String"));
    cards.push_back(xisf("Observation:Center:RA", "83.8", "Float64"));
    cards.push_back(xisf("PixInsight:ProcessingHistory", "<history/>", "String"));
    const an::Result<an::CodecMetadata> metadata = an::CodecMetadata::create(an::SourceEncoding{}, cards);
    ASSERT_TRUE(metadata.hasValue()) << metadata.status().message();
    const auto statesXisfSolution = [](const an::CodecMetadata& set) {
        return std::any_of(set.cards().begin(), set.cards().end(), [](const an::CodecCard& entry) {
            return entry.origin() == an::CodecNamespace::Xisf &&
                   (entry.keyword().starts_with("AstrometricSolution:") ||
                    entry.keyword().starts_with("PCL:AstrometricSolution:"));
        });
    };

    const an::Result<an::CodecMetadata> stripped = an::withoutWcsCards(*metadata);
    ASSERT_TRUE(stripped.hasValue()) << stripped.status().message();
    EXPECT_EQ(an::checkWcsCards(stripped->cards()), an::WcsCardError::NoSolution);
    EXPECT_FALSE(statesXisfSolution(*stripped));
    EXPECT_NE(stripped->find(an::CodecNamespace::Xisf, "Observation:Center:RA"), nullptr);
    EXPECT_NE(stripped->find(an::CodecNamespace::Xisf, "PixInsight:ProcessingHistory"), nullptr);
    EXPECT_NE(stripped->find(an::CodecNamespace::Fits, "OBJECT"), nullptr);

    // A new solution - a solve, a crop's - replaces the writer's solution properties as well as the keywords.
    const an::Result<an::WcsSolution> other = an::parseWcsCards(rotatedScaleHeader(), 1024u, 1024u);
    ASSERT_TRUE(other.hasValue()) << other.status().message();
    const an::Result<an::CodecMetadata> written = an::withWcsCards(*metadata, *other);
    ASSERT_TRUE(written.hasValue()) << written.status().message();
    EXPECT_FALSE(statesXisfSolution(*written));
    const an::Result<an::WcsSolution> read = an::parseWcsCards(written->cards(), 1024u, 1024u);
    ASSERT_TRUE(read.hasValue()) << read.status().message();
    EXPECT_EQ(*read, *other);
    EXPECT_NE(written->find(an::CodecNamespace::Xisf, "Observation:Center:RA"), nullptr);
}

TEST(WcsCardsTest, EveryReasonHasAName)
{
    EXPECT_STREQ(an::wcsCardErrorName(an::WcsCardError::None), "None");
    EXPECT_STREQ(an::wcsCardErrorName(an::WcsCardError::NoSolution), "NoSolution");
    EXPECT_STREQ(an::wcsCardErrorName(an::WcsCardError::MissingKeyword), "MissingKeyword");
    EXPECT_STREQ(an::wcsCardErrorName(an::WcsCardError::MalformedValue), "MalformedValue");
    EXPECT_STREQ(an::wcsCardErrorName(an::WcsCardError::DuplicateKeyword), "DuplicateKeyword");
    EXPECT_STREQ(an::wcsCardErrorName(an::WcsCardError::UnsupportedProjection), "UnsupportedProjection");
    EXPECT_STREQ(an::wcsCardErrorName(an::WcsCardError::UnsupportedFrame), "UnsupportedFrame");
    EXPECT_STREQ(an::wcsCardErrorName(an::WcsCardError::InconsistentAxes), "InconsistentAxes");
    EXPECT_STREQ(an::wcsCardErrorName(an::WcsCardError::UnsupportedAxisOrder), "UnsupportedAxisOrder");
    EXPECT_STREQ(an::wcsCardErrorName(an::WcsCardError::UnsupportedUnit), "UnsupportedUnit");
    EXPECT_STREQ(an::wcsCardErrorName(an::WcsCardError::ValueOutOfRange), "ValueOutOfRange");
    EXPECT_STREQ(an::wcsCardErrorName(an::WcsCardError::SingularTransform), "SingularTransform");
    EXPECT_STREQ(an::wcsCardErrorName(static_cast<an::WcsCardError>(200)), "UnknownWcsCardError");
    EXPECT_TRUE(an::isWcsCardError(an::WcsCardError::SingularTransform));
    EXPECT_FALSE(an::isWcsCardError(static_cast<an::WcsCardError>(200)));
}

} // namespace
