// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the plate solve does around its solvers: a solver's header-only answer read through the engine's header reader
/// and card bridge with the reference frame stated; a solution written into an image's own metadata, and withdrawn
/// from it, through one keyword vocabulary; the solution an image's cards state, including an image the product
/// solved before it stated the frame; and the copy a metadata edit shows.

#include "Test_ControlsAstrometry/AstrometryTestSupport.h"

#include "app/PlateSolveTypes.h"
#include "app/PlateSolveUtils.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Algorithms/Astrometry/WcsCards.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <QDir>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace
{

using an::tests::astrometry::addCard;
using an::tests::astrometry::cardValue;
using an::tests::astrometry::Channels;
using an::tests::astrometry::joined;
using an::tests::astrometry::makeAssetWithMetadata;
using an::tests::astrometry::makeSolvedHeaderBytes;
using an::tests::astrometry::metadataWithCards;
using an::tests::astrometry::northUpPlane;
using an::tests::astrometry::openViewerContext;
using an::tests::astrometry::solutionOf;
using an::tests::astrometry::tangentPlaneCards;
using an::tests::astrometry::wcsCardCount;
using an::tests::astrometry::writeFileBytes;

/// The bytes of a header as std::string_view.
[[nodiscard]] std::string_view viewOf(const QByteArray& bytes)
{
    return std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

/// A header as eighty-byte records with no line ends, each text line padded with spaces.
[[nodiscard]] QByteArray asRecords(const QByteArray& lines)
{
    QByteArray records;
    for (const QByteArray& line : lines.split('\n'))
    {
        if (line.isEmpty())
        {
            continue;
        }
        records += line.leftJustified(80, ' ', true);
    }
    return records;
}

/// How many cards of a list state a keyword.
[[nodiscard]] std::size_t countOf(std::span<const an::CodecCard> cards, const std::string_view keyword)
{
    return static_cast<std::size_t>(std::count_if(cards.begin(), cards.end(), [keyword](const an::CodecCard& card) {
        return card.origin() == an::CodecNamespace::Fits && card.keyword() == keyword;
    }));
}

/// A previous solution as a solver wrote it: a polynomial with its bounds, the frame keywords another program writes,
/// and the solver and optics the window wrote beside it.
[[nodiscard]] std::vector<an::CodecCard> previousSolutionCards()
{
    std::vector<an::CodecCard> cards = tangentPlaneCards(northUpPlane(100.0, 80.0, 10.0, 41.0, 0.001), true);
    addCard(cards, "A_DMAX", "1.5");
    addCard(cards, "B_DMAX", "1.2");
    addCard(cards, "LONPOLE", "180.0");
    addCard(cards, "WCSNAME", "'astrometry'");
    addCard(cards, "EQUINOX", "2000.0");
    addCard(cards, "PLTSOLVR", "'ASTAP'");
    addCard(cards, "FOCALLEN", "100.0");
    return cards;
}

/// Cards a solution never touches.
[[nodiscard]] std::vector<an::CodecCard> unrelatedCards()
{
    std::vector<an::CodecCard> cards;
    addCard(cards, "EXPTIME", "300.0");
    addCard(cards, "FILTER", "'L'");
    addCard(cards, "OBJECT", "'M31'");
    return cards;
}

} // namespace

TEST(SolvedHeaderCards, AHeaderNamingNoFrameIsGivenTheIcrs)
{
    const an::Result<std::vector<an::CodecCard>> cards =
        an::ui::solvedHeaderCards(viewOf(makeSolvedHeaderBytes(320.0, 240.0, 120.5, -22.25, 0.0005)));
    ASSERT_TRUE(cards.hasValue()) << cards.status().message();
    ASSERT_FALSE(cards->empty());
    EXPECT_EQ(cards->back().keyword(), "RADESYS");
    EXPECT_EQ(cards->back().value(), "'ICRS'");
    EXPECT_EQ(cards->back().comment(), "stated by the plate solve");
    EXPECT_EQ(countOf(*cards, "RADESYS"), 1u);
}

TEST(SolvedHeaderCards, AHeaderNamingAFrameKeepsItAndTheBridgeDecides)
{
    QByteArray header = makeSolvedHeaderBytes(320.0, 240.0, 120.5, -22.25, 0.0005);
    header.replace("END\n", "RADESYS = 'FK5'\nEQUINOX = 2000.0\nEND\n");
    const an::Result<std::vector<an::CodecCard>> cards = an::ui::solvedHeaderCards(viewOf(header));
    ASSERT_TRUE(cards.hasValue()) << cards.status().message();
    EXPECT_EQ(countOf(*cards, "RADESYS"), 1u);
    EXPECT_EQ(cardValue(*cards, "RADESYS"), std::optional<std::string>("'FK5'"));

    const an::Result<an::WcsSolution> solution = an::ui::readSolvedWcsHeader(viewOf(header), 640u, 480u);
    ASSERT_FALSE(solution.hasValue());
    EXPECT_EQ(solution.status().code(), an::ErrorCode::Unsupported);
}

TEST(SolvedHeaderCards, EightyByteRecordsReadAsTextLinesDo)
{
    const QByteArray lines = makeSolvedHeaderBytes(320.0, 240.0, 120.5, -22.25, 0.0005);
    const an::Result<an::WcsSolution> fromLines = an::ui::readSolvedWcsHeader(viewOf(lines), 640u, 480u);
    const an::Result<an::WcsSolution> fromRecords = an::ui::readSolvedWcsHeader(viewOf(asRecords(lines)), 640u, 480u);
    ASSERT_TRUE(fromLines.hasValue()) << fromLines.status().message();
    ASSERT_TRUE(fromRecords.hasValue()) << fromRecords.status().message();
    EXPECT_TRUE(*fromLines == *fromRecords);
}

TEST(SolvedHeaderCards, AHeaderWithNoEndRecordIsRefused)
{
    QByteArray header = makeSolvedHeaderBytes(320.0, 240.0, 120.5, -22.25, 0.0005);
    header.replace("END\n", "");
    EXPECT_FALSE(an::ui::solvedHeaderCards(viewOf(header)).hasValue());
    EXPECT_FALSE(an::ui::readSolvedWcsHeader(viewOf(header), 640u, 480u).hasValue());
}

TEST(SolvedHeaderCards, NothingAfterTheEndRecordIsRead)
{
    QByteArray header = makeSolvedHeaderBytes(320.0, 240.0, 120.5, -22.25, 0.0005);
    header += "CRVAL1  = 300.0\nA_ORDER = 2\n";
    const an::Result<std::vector<an::CodecCard>> cards = an::ui::solvedHeaderCards(viewOf(header));
    ASSERT_TRUE(cards.hasValue()) << cards.status().message();
    EXPECT_EQ(countOf(*cards, "CRVAL1"), 1u);
    EXPECT_EQ(countOf(*cards, "A_ORDER"), 0u);
}

TEST(SolvedHeaderCards, ADuplicatedKeywordIsRefusedRatherThanReplaced)
{
    // The bridge refuses a header that states one keyword twice rather than letting the later card replace the
    // earlier one.
    QByteArray header = makeSolvedHeaderBytes(320.0, 240.0, 120.5, -22.25, 0.0005);
    header.replace("END\n", "CRVAL1  = 121.0\nEND\n");
    const an::Result<an::WcsSolution> solution = an::ui::readSolvedWcsHeader(viewOf(header), 640u, 480u);
    ASSERT_FALSE(solution.hasValue());
    EXPECT_EQ(solution.status().code(), an::ErrorCode::InvalidData);
}

TEST(SolvedHeaderCards, AStringValueKeepsItsSlash)
{
    QByteArray header = makeSolvedHeaderBytes(320.0, 240.0, 120.5, -22.25, 0.0005);
    header.replace("END\n", "OBJECT  = 'M 31/NGC 224' / the target\nEND\n");
    const an::Result<std::vector<an::CodecCard>> cards = an::ui::solvedHeaderCards(viewOf(header));
    ASSERT_TRUE(cards.hasValue()) << cards.status().message();
    EXPECT_EQ(cardValue(*cards, "OBJECT"), std::optional<std::string>("'M 31/NGC 224'"));
}

TEST(SolvedHeaderCards, AnAnswerFileIsReadAndAMissingOneNamed)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString answer = QDir(directory.path()).filePath(QStringLiteral("solution.wcs"));
    ASSERT_TRUE(writeFileBytes(answer, asRecords(makeSolvedHeaderBytes(320.0, 240.0, 120.5, -22.25, 0.0005))));

    const an::Result<an::WcsSolution> read = an::ui::readSolvedWcsHeaderFile(answer, 640u, 480u);
    ASSERT_TRUE(read.hasValue()) << read.status().message();
    EXPECT_NEAR(read->referencePoint().rightAscensionDegrees, 120.5, 1e-12);

    const QString missing = QDir(directory.path()).filePath(QStringLiteral("absent.wcs"));
    const an::Result<an::WcsSolution> absent = an::ui::readSolvedWcsHeaderFile(missing, 640u, 480u);
    ASSERT_FALSE(absent.hasValue());
    EXPECT_EQ(absent.status().code(), an::ErrorCode::ExternalFailure);
    EXPECT_NE(std::string(absent.status().message()).find("absent.wcs"), std::string::npos);
}

TEST(PlateSolution, WritingASolutionKeepsEveryOtherCardAndReplacesTheOldOne)
{
    const an::ImageMetadata before = metadataWithCards(joined(unrelatedCards(), previousSolutionCards()));
    const std::optional<an::WcsSolution> solution =
        solutionOf(tangentPlaneCards(northUpPlane(320.0, 240.0, 120.5, -22.25, 0.0005)), 640u, 480u);
    ASSERT_TRUE(solution.has_value());

    const an::Result<an::ImageMetadata> after = an::ui::metadataWithPlateSolution(
        before, *solution,
        an::ui::PlateSolutionFacts{
            .solver = QStringLiteral("Internal Gaia"), .focalLengthMillimetres = 435.5, .pixelSizeMicrometres = 3.8});
    ASSERT_TRUE(after.hasValue()) << after.status().message();
    const std::span<const an::CodecCard> cards = after->codec().cards();

    // The unrelated cards first, in their order.
    ASSERT_GE(cards.size(), 3u);
    EXPECT_EQ(cards[0].keyword(), "EXPTIME");
    EXPECT_EQ(cards[1].keyword(), "FILTER");
    EXPECT_EQ(cards[2].keyword(), "OBJECT");
    // Nothing of the old solution: its polynomial, its bounds, its frame keywords.
    for (const char* gone :
         {"A_ORDER", "B_ORDER", "A_2_0", "B_0_2", "A_DMAX", "B_DMAX", "LONPOLE", "WCSNAME", "EQUINOX"})
    {
        EXPECT_EQ(countOf(cards, gone), 0u) << gone;
    }
    // The new solution, written once, in the ICRS.
    EXPECT_EQ(cardValue(cards, "CTYPE1"), std::optional<std::string>("'RA---TAN'"));
    EXPECT_EQ(cardValue(cards, "RADESYS"), std::optional<std::string>("'ICRS'"));
    EXPECT_EQ(countOf(cards, "CRVAL1"), 1u);
    // The facts beside it, each once.
    EXPECT_EQ(cardValue(cards, "PLTSOLVR"), std::optional<std::string>("'Internal Gaia'"));
    EXPECT_EQ(cardValue(cards, "FOCALLEN"), std::optional<std::string>("435.500000"));
    EXPECT_EQ(cardValue(cards, "XPIXSZ"), std::optional<std::string>("3.800000"));
    EXPECT_EQ(cardValue(cards, "YPIXSZ"), std::optional<std::string>("3.800000"));
    for (const char* once : {"PLTSOLVR", "FOCALLEN", "XPIXSZ", "YPIXSZ"})
    {
        EXPECT_EQ(countOf(cards, once), 1u) << once;
    }
    // The summary that travels with the image, and the change recorded.
    EXPECT_TRUE(after->astrometry().present());
    ASSERT_FALSE(after->provenance().entries().empty());
    EXPECT_EQ(after->provenance().entries().back().operation(), "plateSolve");
    EXPECT_EQ(after->provenance().entries().back().detail(), "solved by Internal Gaia");
}

TEST(PlateSolution, AWrittenSolutionReadsBackAsItself)
{
    const std::optional<an::WcsSolution> solution =
        solutionOf(tangentPlaneCards(northUpPlane(320.0, 240.0, 120.5, -22.25, 0.0005), true), 640u, 480u);
    ASSERT_TRUE(solution.has_value());
    const an::Result<an::ImageMetadata> written =
        an::ui::metadataWithPlateSolution(metadataWithCards(unrelatedCards()), *solution, {});
    ASSERT_TRUE(written.hasValue()) << written.status().message();

    const an::Result<an::WcsSolution> read = an::parseWcsCards(written->codec().cards(), 640u, 480u);
    ASSERT_TRUE(read.hasValue()) << read.status().message();
    EXPECT_TRUE(*read == *solution);
    EXPECT_EQ(countOf(written->codec().cards(), "PLTSOLVR"), 0u);
}

TEST(PlateSolution, ClearingRemovesExactlyWhatWritingReplaces)
{
    // One vocabulary decides both, the polynomial's bounds included.
    const std::vector<an::CodecCard> stated = joined(unrelatedCards(), previousSolutionCards());
    const an::ImageMetadata before = metadataWithCards(stated);

    const an::Result<an::ImageMetadata> cleared = an::ui::metadataWithoutPlateSolution(before);
    ASSERT_TRUE(cleared.hasValue()) << cleared.status().message();
    const std::span<const an::CodecCard> kept = cleared->codec().cards();
    EXPECT_EQ(wcsCardCount(kept), 0u);
    EXPECT_EQ(countOf(kept, "PLTSOLVR"), 0u);
    EXPECT_FALSE(cleared->astrometry().present());
    EXPECT_EQ(cleared->provenance().entries().back().operation(), "clearAstrometry");

    // What clearing kept is exactly the cards outside the vocabulary - the unrelated cards and the optics.
    std::vector<std::string> expected;
    for (const an::CodecCard& card : stated)
    {
        if (!an::isWcsKeyword(card.keyword()) && card.keyword() != an::ui::kPlateSolveSolverKeyword)
        {
            expected.push_back(card.keyword());
        }
    }
    std::vector<std::string> actual;
    for (const an::CodecCard& card : kept)
    {
        actual.push_back(card.keyword());
    }
    EXPECT_EQ(actual, expected);

    // Writing a solution over the old one leaves the same cards outside the vocabulary.
    const std::optional<an::WcsSolution> solution =
        solutionOf(tangentPlaneCards(northUpPlane(320.0, 240.0, 120.5, -22.25, 0.0005)), 640u, 480u);
    ASSERT_TRUE(solution.has_value());
    const an::Result<an::ImageMetadata> written = an::ui::metadataWithPlateSolution(before, *solution, {});
    ASSERT_TRUE(written.hasValue()) << written.status().message();
    std::vector<std::string> outside;
    for (const an::CodecCard& card : written->codec().cards())
    {
        if (!an::isWcsKeyword(card.keyword()))
        {
            outside.push_back(card.keyword());
        }
    }
    EXPECT_EQ(outside, expected);
}

TEST(ImageWcsSolution, AnImageTheProductSolvedBeforeItStatedTheFrameIsReadInTheIcrs)
{
    std::vector<an::CodecCard> cards = tangentPlaneCards(northUpPlane(320.0, 240.0, 120.5, -22.25, 0.0005));
    addCard(cards, "EQUINOX", "2000.0");
    ASSERT_FALSE(an::parseWcsCards(cards, 640u, 480u).hasValue());
    addCard(cards, "PLTSOLVR", "'astrometry.net'");

    const an::Result<an::WcsSolution> read = an::ui::readImageWcsSolution(cards, 640u, 480u);
    ASSERT_TRUE(read.hasValue()) << read.status().message();
    EXPECT_NEAR(read->referencePoint().declinationDegrees, -22.25, 1e-12);
}

TEST(ImageWcsSolution, TheJ2000EquinoxWithNoFrameIsReadInTheIcrsWhoeverWroteIt)
{
    // What astrometry.net and ASTAP write into a file they solve, with no solver card of the product's.
    std::vector<an::CodecCard> cards = tangentPlaneCards(northUpPlane(320.0, 240.0, 120.5, -22.25, 0.0005));
    addCard(cards, "EQUINOX", "2000.0");
    ASSERT_FALSE(an::parseWcsCards(cards, 640u, 480u).hasValue());

    const an::Result<an::WcsSolution> read = an::ui::readImageWcsSolution(cards, 640u, 480u);
    ASSERT_TRUE(read.hasValue()) << read.status().message();
    EXPECT_NEAR(read->referencePoint().rightAscensionDegrees, 120.5, 1e-12);
}

TEST(ImageWcsSolution, AnOlderEpochCardStandsForTheEquinox)
{
    std::vector<an::CodecCard> cards = tangentPlaneCards(northUpPlane(320.0, 240.0, 120.5, -22.25, 0.0005));
    addCard(cards, "EPOCH", "2000.0");
    EXPECT_TRUE(an::ui::readImageWcsSolution(cards, 640u, 480u).hasValue());

    std::vector<an::CodecCard> older = tangentPlaneCards(northUpPlane(320.0, 240.0, 120.5, -22.25, 0.0005));
    addCard(older, "EPOCH", "1950.0");
    EXPECT_FALSE(an::ui::readImageWcsSolution(older, 640u, 480u).hasValue());
}

TEST(ImageWcsSolution, AStatedFrameIsNeverOverridden)
{
    std::vector<an::CodecCard> cards = tangentPlaneCards(northUpPlane(320.0, 240.0, 120.5, -22.25, 0.0005));
    addCard(cards, "RADESYS", "'FK5'");
    addCard(cards, "EQUINOX", "2000.0");
    addCard(cards, "PLTSOLVR", "'ASTAP'");
    EXPECT_FALSE(an::ui::readImageWcsSolution(cards, 640u, 480u).hasValue());
}

TEST(ImageWcsSolution, AnotherEquinoxIsNeverTakenForTheIcrs)
{
    std::vector<an::CodecCard> cards = tangentPlaneCards(northUpPlane(320.0, 240.0, 120.5, -22.25, 0.0005));
    addCard(cards, "EQUINOX", "1950.0");
    addCard(cards, "PLTSOLVR", "'ASTAP'");
    EXPECT_FALSE(an::ui::readImageWcsSolution(cards, 640u, 480u).hasValue());
}

TEST(ImageWcsSolution, CardsThatStateTheIcrsAreReadAsTheyAre)
{
    std::vector<an::CodecCard> cards = tangentPlaneCards(northUpPlane(320.0, 240.0, 120.5, -22.25, 0.0005));
    addCard(cards, "RADESYS", "'ICRS'");
    const an::Result<an::WcsSolution> read = an::ui::readImageWcsSolution(cards, 640u, 480u);
    ASSERT_TRUE(read.hasValue()) << read.status().message();
    EXPECT_TRUE(*read == *an::parseWcsCards(cards, 640u, 480u));
}

TEST(PlateSolveInput, TheSolverReadsSinglePrecisionFits)
{
    const an::Result<an::OutputIntent> intent = an::ui::plateSolveInputIntent();
    ASSERT_TRUE(intent.hasValue()) << intent.status().message();
    EXPECT_EQ(intent->sampleType(), an::SampleType::Float32);
}

TEST(MetadataEdit, TheCopyCarriesTheNewMetadataAndTheSameSamples)
{
    std::unique_ptr<an::tests::viewer::ViewerTestContext> context = openViewerContext();
    ASSERT_NE(context, nullptr);
    const an::consumer::EngineSession& session = context->engineSession();
    std::optional<an::consumer::ImageAsset> asset = makeAssetWithMetadata<std::uint16_t>(
        session, 16u, 12u, Channels::Rgb,
        [](const std::uint32_t x, const std::uint32_t y, const std::uint32_t channel) {
            return static_cast<std::uint16_t>(x * 100u + y * 10u + channel);
        },
        metadataWithCards(unrelatedCards()));
    ASSERT_TRUE(asset.has_value());

    const an::ImageMetadata replacement = metadataWithCards(previousSolutionCards());
    std::optional<an::consumer::ImageAsset> copy;
    {
        an::Result<an::consumer::AssetWriteLease> writer = asset->write();
        ASSERT_TRUE(writer.hasValue()) << writer.status().message();
        an::Result<an::consumer::ImageAsset> copied = an::ui::copyImageWithMetadata(session, *writer, replacement, {});
        ASSERT_TRUE(copied.hasValue()) << copied.status().message();
        copy = std::move(*copied);
    }
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        EXPECT_EQ(an::tests::viewer::sampleOf<std::uint16_t>(*copy, 7u, 5u, channel),
                  an::tests::viewer::sampleOf<std::uint16_t>(*asset, 7u, 5u, channel));
    }
    an::Result<an::consumer::AssetReadLease> read = copy->read();
    ASSERT_TRUE(read.hasValue());
    read->visit([](const auto& typed) {
        EXPECT_EQ(cardValue(typed.metadata().codec().cards(), "PLTSOLVR"), std::optional<std::string>("'ASTAP'"));
        EXPECT_FALSE(cardValue(typed.metadata().codec().cards(), "EXPTIME").has_value());
    });
    EXPECT_NE(copy->identity(), asset->identity());
}

TEST(MetadataEdit, AStoppedEditCopiesNothing)
{
    std::unique_ptr<an::tests::viewer::ViewerTestContext> context = openViewerContext();
    ASSERT_NE(context, nullptr);
    std::optional<an::consumer::ImageAsset> asset = makeAssetWithMetadata<float>(
        context->engineSession(), 8u, 8u, Channels::Grey,
        [](std::uint32_t, std::uint32_t, std::uint32_t) { return 0.5f; }, metadataWithCards({}));
    ASSERT_TRUE(asset.has_value());
    an::Result<an::consumer::AssetWriteLease> writer = asset->write();
    ASSERT_TRUE(writer.hasValue());
    std::stop_source stop;
    stop.request_stop();
    const an::Result<an::consumer::ImageAsset> copied =
        an::ui::copyImageWithMetadata(context->engineSession(), *writer, metadataWithCards({}), stop.get_token());
    ASSERT_FALSE(copied.hasValue());
    EXPECT_EQ(copied.status().code(), an::ErrorCode::Cancelled);
}
