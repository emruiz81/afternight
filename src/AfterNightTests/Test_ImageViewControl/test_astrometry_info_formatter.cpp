// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include "Test_ControlsAstrometry/AstrometryTestSupport.h"

#include "app/PlateSolveTypes.h"
#include "controls/ImageAstrometryInfoFormatter.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <optional>
#include <vector>

namespace
{

using an::tests::astrometry::addCard;
using an::tests::astrometry::metadataWithCards;
using an::tests::astrometry::northUpPlane;
using an::tests::astrometry::tangentPlaneCards;

std::vector<an::CodecCard> makeCardsWithoutWcs()
{
    std::vector<an::CodecCard> cards;
    addCard(cards, "OBJECT", "'unsolved'");
    return cards;
}

/// A solved image's cards as the product wrote them before it stated the frame: the solver's equinox, its polynomial
/// and the solver's card.
std::vector<an::CodecCard> makeSolvedCards()
{
    an::tests::astrometry::TangentPlane plane = northUpPlane(320.0, 240.0, 187.5, -45.5, 0.0005);
    std::vector<an::CodecCard> cards = tangentPlaneCards(plane, true);
    addCard(cards, "EQUINOX", "2000.0");
    addCard(cards, "PLTSOLVR", "'ASTAP'");
    return cards;
}

an::ui::ImageAstrometryInfoSummary summaryOf(const std::vector<an::CodecCard>& cards)
{
    an::Result<an::WcsSolution> solution = an::ui::readImageWcsSolution(cards, 640u, 480u);
    return an::ui::buildImageAstrometryInfoSummary(
        solution.hasValue() ? std::optional<an::WcsSolution>(std::move(*solution)) : std::nullopt, cards);
}

} // namespace

TEST(ImageAstrometryInfoFormatter, FormatRaUsesColonSexagesimal)
{
    EXPECT_EQ(an::ui::formatAstrometryRa(187.5), QStringLiteral("12:30:00.00"));
}

TEST(ImageAstrometryInfoFormatter, FormatDecUsesSignedColonSexagesimal)
{
    EXPECT_EQ(an::ui::formatAstrometryDec(-45.5), QStringLiteral("-45:30:00.0"));
}

TEST(ImageAstrometryInfoFormatter, FormatRaRoundsOnceIntoEveryField)
{
    // 12h 29m 59.996s rounds once, into every field: never 12:29:60.00.
    EXPECT_EQ(an::ui::formatAstrometryRa((12.0 + 29.0 / 60.0 + 59.996 / 3600.0) * 15.0), QStringLiteral("12:30:00.00"));
    EXPECT_EQ(an::ui::formatAstrometryDec(-(45.0 + 29.0 / 60.0 + 59.96 / 3600.0)), QStringLiteral("-45:30:00.0"));
}

TEST(ImageAstrometryInfoFormatter, BuildSummaryReturnsNoSolutionForUnsolvedMetadata)
{
    const auto summary = summaryOf(makeCardsWithoutWcs());

    EXPECT_FALSE(summary.hasSolution);
    EXPECT_EQ(summary.statusMessage, QStringLiteral("No astrometric solution"));
}

TEST(ImageAstrometryInfoFormatter, BuildSummaryExtractsSolvedMetadataFields)
{
    const auto summary = summaryOf(makeSolvedCards());

    ASSERT_TRUE(summary.hasSolution);
    EXPECT_FALSE(summary.fieldCenterRa.isEmpty());
    EXPECT_FALSE(summary.fieldCenterDec.isEmpty());
    EXPECT_NE(summary.fieldCenterRa, QStringLiteral("-"));
    EXPECT_NE(summary.fieldCenterDec, QStringLiteral("-"));
    EXPECT_EQ(summary.crvalRa, QStringLiteral("12:30:00.00"));
    EXPECT_EQ(summary.crvalDec, QStringLiteral("-45:30:00.0"));
    EXPECT_EQ(summary.projection, QStringLiteral("RA---TAN-SIP / DEC--TAN-SIP"));
    EXPECT_EQ(summary.equinox, QStringLiteral("J2000.0"));
    EXPECT_EQ(summary.solver, QStringLiteral("ASTAP"));
    EXPECT_TRUE(summary.imageScale.contains(QStringLiteral("arcsec/px")));
    EXPECT_TRUE(summary.rotation.contains(QChar(0x00B0)));
    EXPECT_TRUE(summary.fieldOfView.contains(QChar(0x00D7)));
    EXPECT_TRUE(summary.sip.contains(QStringLiteral("A/B 2/2")));
    EXPECT_TRUE(summary.sip.contains(QStringLiteral("2 coeffs")));
}

TEST(ImageAstrometryInfoFormatter, TheFiguresAreTheSolutionsOwn)
{
    // 0.0005 degrees a pixel is 1.8 arcseconds; north up and east left is an orientation of zero; 640 by 480 pixels
    // of it are 19.2 by 14.4 arcminutes.
    const auto summary = summaryOf(makeSolvedCards());
    ASSERT_TRUE(summary.hasSolution);
    EXPECT_EQ(summary.imageScale, QStringLiteral("1.800 arcsec/px"));
    EXPECT_TRUE(summary.rotation.contains(QStringLiteral("0.0") + QChar(0x00B0))) << summary.rotation.toStdString();
    EXPECT_TRUE(summary.fieldOfView.startsWith(QStringLiteral("19.20"))) << summary.fieldOfView.toStdString();
}

TEST(ImageAstrometryInfoFormatter, ASolutionWrittenInTheIcrsShowsItsFrameForTheEquinox)
{
    std::vector<an::CodecCard> cards = tangentPlaneCards(northUpPlane(320.0, 240.0, 187.5, -45.5, 0.0005));
    addCard(cards, "RADESYS", "'ICRS'");
    const auto summary = summaryOf(cards);
    ASSERT_TRUE(summary.hasSolution);
    EXPECT_EQ(summary.equinox, QStringLiteral("ICRS"));
    EXPECT_EQ(summary.projection, QStringLiteral("RA---TAN / DEC--TAN"));
    EXPECT_EQ(summary.sip, QStringLiteral("None"));
    EXPECT_EQ(summary.solver, QStringLiteral("Unknown"));
}

TEST(ImageAstrometryInfoFormatter, ChainedImageDuplicateKeepsAstrometryMetadata)
{
    std::unique_ptr<an::tests::astrometry::DirectExecution> direct = an::tests::astrometry::openExecution();
    ASSERT_NE(direct, nullptr);
    const an::consumer::EngineSession& session = direct->provider->engineSession();
    std::optional<an::consumer::ImageAsset> source = an::tests::astrometry::makeAssetWithMetadata<float>(
        session, 640u, 480u, an::tests::astrometry::Channels::Grey,
        [](std::uint32_t, std::uint32_t, std::uint32_t) { return 0.25f; }, metadataWithCards(makeSolvedCards()));
    ASSERT_TRUE(source.has_value());

    const auto cardsOf = [](const an::consumer::ImageAsset& asset) {
        std::vector<an::CodecCard> cards;
        an::Result<an::consumer::AssetReadLease> lease = asset.read();
        EXPECT_TRUE(lease.hasValue());
        if (lease.hasValue())
        {
            lease->visit([&cards](const auto& typed) {
                const auto stated = typed.metadata().codec().cards();
                cards.assign(stated.begin(), stated.end());
            });
        }
        return cards;
    };
    ASSERT_TRUE(summaryOf(cardsOf(*source)).hasSolution);

    an::Result<an::consumer::ImageAsset> firstDuplicate = source->snapshot();
    ASSERT_TRUE(firstDuplicate.hasValue()) << firstDuplicate.status().message();
    ASSERT_TRUE(summaryOf(cardsOf(*firstDuplicate)).hasSolution);

    an::Result<an::consumer::ImageAsset> secondDuplicate = firstDuplicate->snapshot();
    ASSERT_TRUE(secondDuplicate.hasValue()) << secondDuplicate.status().message();
    const auto summary = summaryOf(cardsOf(*secondDuplicate));
    EXPECT_TRUE(summary.hasSolution);
    EXPECT_EQ(summary.solver, QStringLiteral("ASTAP"));
    EXPECT_EQ(summary.projection, QStringLiteral("RA---TAN-SIP / DEC--TAN-SIP"));
}
