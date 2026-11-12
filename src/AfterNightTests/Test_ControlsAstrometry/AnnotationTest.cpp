// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The annotated picture of a solved image: the choices and the text they are stored as, the scene a solution and the
/// catalogues' entries build - the grid clipped to the frame, one mark for an object the catalogues list twice, the
/// objects each level of detail keeps, the stars' names, the compass's directions and the panel's facts - the layout
/// that keeps every label inside the frame and off every other, prefers the more prominent object and scales with the
/// image, and the picture drawn over the presented image, smaller, alone over transparent pixels, and as an image of
/// its own that carries the frame's facts and its solution at its scale.

#include "Test_ControlsAstrometry/AstrometryTestSupport.h"

#include "controls/annotation/AnnotationLayout.h"
#include "controls/annotation/AnnotationPicture.h"
#include "controls/annotation/AnnotationRenderer.h"
#include "controls/annotation/AnnotationScene.h"
#include "controls/annotation/AnnotationSettings.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Algorithms/Astrometry/WcsCards.h>
#include <AfterNightEngine/Algorithms/Catalog/CatalogRecord.h>
#include <AfterNightEngine/IO/ProcessingStateCards.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <QColor>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QStringList>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <vector>

namespace
{

using an::tests::astrometry::addCard;
using an::tests::astrometry::cardValue;
using an::tests::astrometry::Channels;
using an::tests::astrometry::fixedCatalogs;
using an::tests::astrometry::makeAssetWithMetadata;
using an::tests::astrometry::metadataWithCards;
using an::tests::astrometry::northUpPlane;
using an::tests::astrometry::openExecution;
using an::tests::astrometry::solutionOf;
using an::tests::astrometry::tangentPlaneCards;

constexpr std::uint32_t kWidth = 400u;
constexpr std::uint32_t kHeight = 300u;

/// A field of 0.01 degrees a pixel about (120, 10), north toward increasing rows and east toward decreasing columns,
/// the reference at the frame's centre; scaled by a factor, the same sky on a frame that many times larger.
[[nodiscard]] std::vector<an::CodecCard> fieldCards(const double factor = 1.0)
{
    std::vector<an::CodecCard> cards =
        tangentPlaneCards(northUpPlane(200.0 * factor + 0.5, 150.0 * factor + 0.5, 120.0, 10.0, 0.01 / factor));
    addCard(cards, "RADESYS", "'ICRS'");
    return cards;
}

[[nodiscard]] an::CatalogEntry deepSkyEntry(std::string identifier, std::string name, const double ra, const double dec,
                                            const double majorArcmin, std::string type,
                                            const std::optional<double> magnitude)
{
    an::CatalogEntry entry;
    entry.identifier = std::move(identifier);
    entry.commonName = std::move(name);
    entry.objectType = std::move(type);
    entry.coordinate = an::SkyCoordinate{.rightAscensionDegrees = ra, .declinationDegrees = dec};
    entry.magnitude = magnitude;
    if (majorArcmin > 0.0)
    {
        entry.extent = an::CatalogExtent{
            .majorAxisArcminutes = majorArcmin, .minorAxisArcminutes = majorArcmin * 0.6, .positionAngleDegrees = 30.0};
    }
    return entry;
}

[[nodiscard]] an::CatalogEntry starEntry(std::string name, std::string bayer, const double ra, const double dec,
                                         const double magnitude)
{
    an::CatalogEntry entry;
    entry.identifier = "HIP " + std::to_string(static_cast<int>(ra * 100.0));
    entry.commonName = std::move(name);
    entry.designation = std::move(bayer);
    entry.objectType = "Star";
    entry.coordinate = an::SkyCoordinate{.rightAscensionDegrees = ra, .declinationDegrees = dec};
    entry.magnitude = magnitude;
    return entry;
}

/// The scene of a field, its catalogues' entries and cards, under choices.
[[nodiscard]] an::ui::AnnotationScene sceneOf(const an::WcsSolution& solution,
                                              const std::vector<an::CatalogEntry>& deepSky,
                                              const std::vector<an::CatalogEntry>& stars,
                                              const std::vector<an::CodecCard>& cards,
                                              const an::ui::AnnotationContent& content)
{
    return an::ui::buildAnnotationScene(
        an::ui::AnnotationSceneSources{.solution = &solution, .deepSky = deepSky, .stars = stars, .cards = cards},
        content);
}

[[nodiscard]] std::vector<QString> designationsOf(const an::ui::AnnotationScene& scene)
{
    std::vector<QString> designations;
    for (const an::ui::AnnotationMark& mark : scene.marks)
    {
        designations.push_back(mark.designation.isEmpty() ? mark.name : mark.designation);
    }
    std::sort(designations.begin(), designations.end());
    return designations;
}

[[nodiscard]] an::ui::AnnotationContent onlyDeepSky(const an::ui::AnnotationDetail detail)
{
    an::ui::AnnotationContent content = an::ui::defaultAnnotationSettings().content;
    an::ui::applyAnnotationDetail(content, detail);
    content.layers = an::ui::AnnotationLayers{
        .grid = false, .deepSky = true, .stars = false, .compass = false, .scaleBar = false, .infoPanel = false};
    return content;
}

// ---------------------------------------------------------------------------------------------------------------------
// The choices

TEST(AnnotationSettings, StoredChoicesReadBackAsThemselvesAndTheTitleIsNotStored)
{
    an::ui::AnnotationSettings settings = an::ui::defaultAnnotationSettings();
    an::ui::applyAnnotationDetail(settings.content, an::ui::AnnotationDetail::Detailed);
    settings.content.layers.compass = false;
    settings.content.labelText = an::ui::AnnotationLabelText::Name;
    settings.content.gridDensity = an::ui::AnnotationGridDensity::Fine;
    settings.content.facts.camera = false;
    settings.content.credit = QStringLiteral("Ezequiel Ruiz");
    settings.content.title = QStringLiteral("Carina Nebula");
    settings.style.colors = an::ui::annotationPaletteColors(an::ui::AnnotationPalette::Ember);
    settings.style.colors.halo = QColor(1, 2, 3, 45);
    settings.style.customColors = an::ui::annotationPaletteColors(an::ui::AnnotationPalette::Rose);
    settings.style.customColors->galaxy = QColor(10, 200, 30, 240);
    settings.style.fontFamily = QStringLiteral("Georgia");
    settings.style.fontWeight = an::ui::AnnotationFontWeight::Bold;
    settings.style.italicLabels = true;
    settings.style.textSize = 1.7;
    settings.style.panelSize = 0.65;
    settings.style.labelsInObjectColor = false;
    settings.style.lineWidth = 0.75;
    settings.style.lineOpacity = 0.4;
    settings.style.lineHalo = false;
    settings.style.fillObjects = true;
    settings.style.fillOpacity = 0.3;
    settings.style.starMarker = an::ui::AnnotationStarMarker::Reticle;
    settings.style.gridOpacity = 0.2;
    settings.style.labelBacking = an::ui::AnnotationLabelBacking::Badge;
    settings.style.marker = an::ui::AnnotationMarker::Brackets;
    settings.style.panelCorner = an::ui::AnnotationCorner::TopLeft;
    settings.content.stars.naming = an::ui::AnnotationStarNaming::CatalogueNumbers;
    settings.content.stars.markUnnamed = true;
    settings.output.longEdge = 2048u;
    settings.output.stretch = an::ui::AnnotationStretch::AsStored;
    settings.output.background = an::ui::AnnotationBackground::Transparent;

    const an::ui::AnnotationSettings read =
        an::ui::annotationSettingsFromStored(an::ui::storedAnnotationSettings(settings));

    an::ui::AnnotationSettings expected = settings;
    expected.content.title.clear();
    EXPECT_EQ(read, expected);
}

TEST(AnnotationSettings, AStoredTextOfAnotherVersionReadsAsFarAsItGoes)
{
    const an::ui::AnnotationSettings defaults = an::ui::defaultAnnotationSettings();
    EXPECT_EQ(an::ui::annotationSettingsFromStored(QString()), defaults);
    EXPECT_EQ(an::ui::annotationSettingsFromStored(QStringLiteral("not json")), defaults);

    const an::ui::AnnotationSettings read = an::ui::annotationSettingsFromStored(QStringLiteral(
        R"({"version":7,"labelText":"sideways","gridDensity":"coarse","style":{"textSize":40,"lineWidth":2,)"
        R"("colors":{"galaxy":"#ff00ff00","nebula":"no colour"}},"deepSky":{"magnitudeLimit":"bright"},"future":1})"));
    EXPECT_EQ(read.content.labelText, defaults.content.labelText);
    EXPECT_EQ(read.content.gridDensity, an::ui::AnnotationGridDensity::Coarse);
    EXPECT_DOUBLE_EQ(read.style.textSize, defaults.style.textSize);
    EXPECT_DOUBLE_EQ(read.style.lineWidth, 2.0);
    EXPECT_EQ(read.style.colors.galaxy, QColor(0, 255, 0, 255));
    EXPECT_EQ(read.style.colors.nebula, defaults.style.colors.nebula);
    EXPECT_DOUBLE_EQ(read.content.deepSky.magnitudeLimit, defaults.content.deepSky.magnitudeLimit);
}

// The first version stored whether a star was named by its Bayer letter; the naming that says the same is read from it,
// and a style's opacities and sizes out of their ranges read as their defaults.
TEST(AnnotationSettings, TheFirstVersionsStarNamingAndOutOfRangeLooksReadAsTheySaid)
{
    const an::ui::AnnotationSettings defaults = an::ui::defaultAnnotationSettings();
    EXPECT_EQ(an::ui::annotationSettingsFromStored(QStringLiteral(R"({"version":1,"stars":{"bayerLetters":false}})"))
                  .content.stars.naming,
              an::ui::AnnotationStarNaming::ProperNames);
    EXPECT_EQ(an::ui::annotationSettingsFromStored(QStringLiteral(R"({"version":1,"stars":{"bayerLetters":true}})"))
                  .content.stars.naming,
              an::ui::AnnotationStarNaming::Designations);
    const an::ui::AnnotationSettings read = an::ui::annotationSettingsFromStored(QStringLiteral(
        R"({"style":{"lineOpacity":0.0,"fillOpacity":3,"panelSize":9,"fontWeight":"heavy","starMarker":"dot"}})"));
    EXPECT_DOUBLE_EQ(read.style.lineOpacity, defaults.style.lineOpacity) << "a line is never stored invisible";
    EXPECT_DOUBLE_EQ(read.style.fillOpacity, defaults.style.fillOpacity);
    EXPECT_DOUBLE_EQ(read.style.panelSize, defaults.style.panelSize);
    EXPECT_EQ(read.style.fontWeight, defaults.style.fontWeight);
    EXPECT_EQ(read.style.starMarker, defaults.style.starMarker);
}

// A person's own colours are kept apart from the palette chosen; text written before they were reads colours no
// palette has as theirs, and a palette's as no colours of their own yet.
TEST(AnnotationSettings, ThePersonsOwnColoursAreReadAsTheTextKeptThem)
{
    an::ui::AnnotationSettings custom = an::ui::defaultAnnotationSettings();
    custom.style.colors.nebula = QColor(1, 2, 3);
    QJsonObject root = QJsonDocument::fromJson(an::ui::storedAnnotationSettings(custom).toUtf8()).object();
    QJsonObject style = root.value(QStringLiteral("style")).toObject();
    style.remove(QStringLiteral("customColors"));
    root.insert(QStringLiteral("style"), style);
    const an::ui::AnnotationSettings older =
        an::ui::annotationSettingsFromStored(QString::fromUtf8(QJsonDocument(root).toJson()));
    ASSERT_TRUE(older.style.customColors.has_value());
    EXPECT_EQ(*older.style.customColors, custom.style.colors);

    const an::ui::AnnotationSettings preset =
        an::ui::annotationSettingsFromStored(an::ui::storedAnnotationSettings(an::ui::defaultAnnotationSettings()));
    EXPECT_FALSE(preset.style.customColors.has_value());
}

// Every palette offered is its own: no two share their colours, and each reads back as itself.
TEST(AnnotationSettings, EveryPaletteOfferedIsDistinctAndReadsBackAsItself)
{
    const std::span<const an::ui::AnnotationPalette> palettes = an::ui::annotationPalettes();
    ASSERT_EQ(palettes.size(), 10u);
    EXPECT_EQ(std::count(palettes.begin(), palettes.end(), an::ui::AnnotationPalette::Custom), 0);
    for (std::size_t first = 0u; first < palettes.size(); ++first)
    {
        const an::ui::AnnotationColors colors = an::ui::annotationPaletteColors(palettes[first]);
        EXPECT_EQ(an::ui::annotationPaletteOf(colors), palettes[first]);
        for (std::size_t second = first + 1u; second < palettes.size(); ++second)
        {
            EXPECT_FALSE(colors == an::ui::annotationPaletteColors(palettes[second]))
                << "palettes " << first << " and " << second << " are one";
        }
        // Each kind of object stays told apart from the others inside its palette.
        const std::array<QColor, 5> kinds{colors.galaxy, colors.nebula, colors.planetaryNebula, colors.openCluster,
                                          colors.globularCluster};
        if (palettes[first] != an::ui::AnnotationPalette::Moonlight)
        {
            for (std::size_t a = 0u; a < kinds.size(); ++a)
            {
                for (std::size_t b = a + 1u; b < kinds.size(); ++b)
                {
                    EXPECT_NE(kinds[a], kinds[b]) << "palette " << first << " kinds " << a << " and " << b;
                }
            }
        }
    }
}

// The levels and the palettes a person chooses from are offered by names of their own, each level with what it names.
TEST(AnnotationSettings, EveryLevelAndPaletteOfferedHasANameOfItsOwn)
{
    const std::span<const an::ui::AnnotationDetail> details = an::ui::annotationDetails();
    EXPECT_TRUE(std::ranges::equal(details,
                                   std::array{an::ui::AnnotationDetail::Essential, an::ui::AnnotationDetail::Standard,
                                              an::ui::AnnotationDetail::Detailed, an::ui::AnnotationDetail::Complete}));
    EXPECT_EQ(an::ui::annotationDetailName(an::ui::AnnotationDetail::Standard), QStringLiteral("Standard"));
    QStringList detailNames;
    for (const an::ui::AnnotationDetail detail : details)
    {
        EXPECT_FALSE(an::ui::annotationDetailDescription(detail).isEmpty());
        detailNames.push_back(an::ui::annotationDetailName(detail));
    }
    EXPECT_EQ(detailNames.removeDuplicates(), 0);
    EXPECT_FALSE(detailNames.contains(QString()));

    EXPECT_EQ(an::ui::annotationPaletteName(an::ui::AnnotationPalette::Aurora), QStringLiteral("Aurora"));
    QStringList paletteNames;
    for (const an::ui::AnnotationPalette palette : an::ui::annotationPalettes())
    {
        paletteNames.push_back(an::ui::annotationPaletteName(palette));
    }
    EXPECT_EQ(paletteNames.removeDuplicates(), 0);
    EXPECT_FALSE(paletteNames.contains(QString()));
}

// A view offers three text sizes by names of their own: the interface's captions, then each a step larger.
TEST(AnnotationViewChoices, AViewsTextSizesAreTheCaptionsAndTwoLargerSteps)
{
    const std::span<const an::ui::AnnotationViewTextSize> sizes = an::ui::annotationViewTextSizes();
    EXPECT_TRUE(std::ranges::equal(sizes, std::array{an::ui::AnnotationViewTextSize::Small,
                                                     an::ui::AnnotationViewTextSize::Medium,
                                                     an::ui::AnnotationViewTextSize::Large}));
    EXPECT_EQ(an::ui::annotationViewTextSizeName(an::ui::AnnotationViewTextSize::Medium), QStringLiteral("Medium"));
    QStringList names;
    for (const an::ui::AnnotationViewTextSize size : sizes)
    {
        names.push_back(an::ui::annotationViewTextSizeName(size));
    }
    EXPECT_EQ(names.removeDuplicates(), 0);
    EXPECT_FALSE(names.contains(QString()));
    EXPECT_DOUBLE_EQ(an::ui::annotationViewTextScale(an::ui::AnnotationViewTextSize::Small), 1.0)
        << "the small size is the captions'";
    EXPECT_DOUBLE_EQ(an::ui::annotationViewTextScale(an::ui::AnnotationViewTextSize::Medium), 1.25);
    EXPECT_DOUBLE_EQ(an::ui::annotationViewTextScale(an::ui::AnnotationViewTextSize::Large), 1.5);
}

// A view's live annotations keep the choices its menu offers - a level of detail, what a label says, a palette and a
// text size - as a text of their own, apart from a picture's.
TEST(AnnotationViewChoices, StoredChoicesReadBackAsThemselves)
{
    const an::ui::AnnotationViewChoices choices{.detail = an::ui::AnnotationDetail::Detailed,
                                                .labelText = an::ui::AnnotationLabelText::DesignationAndName,
                                                .palette = an::ui::AnnotationPalette::Ember,
                                                .textSize = an::ui::AnnotationViewTextSize::Large};
    EXPECT_EQ(an::ui::annotationViewChoicesFromStored(an::ui::storedAnnotationViewChoices(choices)), choices);
    for (const an::ui::AnnotationDetail detail : an::ui::annotationDetails())
    {
        const an::ui::AnnotationViewChoices level{.detail = detail};
        EXPECT_EQ(an::ui::annotationViewChoicesFromStored(an::ui::storedAnnotationViewChoices(level)), level);
    }
    for (const an::ui::AnnotationPalette palette : an::ui::annotationPalettes())
    {
        const an::ui::AnnotationViewChoices coloured{.palette = palette};
        EXPECT_EQ(an::ui::annotationViewChoicesFromStored(an::ui::storedAnnotationViewChoices(coloured)), coloured);
    }
    for (const an::ui::AnnotationLabelText text :
         {an::ui::AnnotationLabelText::Designation, an::ui::AnnotationLabelText::Name,
          an::ui::AnnotationLabelText::DesignationAndName})
    {
        const an::ui::AnnotationViewChoices labelled{.labelText = text};
        EXPECT_EQ(an::ui::annotationViewChoicesFromStored(an::ui::storedAnnotationViewChoices(labelled)), labelled);
    }
    for (const an::ui::AnnotationViewTextSize size : an::ui::annotationViewTextSizes())
    {
        const an::ui::AnnotationViewChoices sized{.textSize = size};
        EXPECT_EQ(an::ui::annotationViewChoicesFromStored(an::ui::storedAnnotationViewChoices(sized)), sized);
    }
}

// A view starts at the standard level, labelling by designation in the Aurora palette at the medium text size; a choice
// the text does not name - missing, unknown, or a custom level or palette, which a view does not offer - reads as its
// default.
TEST(AnnotationViewChoices, AChoiceTheTextDoesNotNameReadsAsItsDefault)
{
    const an::ui::AnnotationViewChoices defaults;
    EXPECT_EQ(defaults.detail, an::ui::AnnotationDetail::Standard);
    EXPECT_EQ(defaults.labelText, an::ui::AnnotationLabelText::Designation);
    EXPECT_EQ(defaults.palette, an::ui::AnnotationPalette::Aurora);
    EXPECT_EQ(defaults.textSize, an::ui::AnnotationViewTextSize::Medium);
    EXPECT_EQ(an::ui::annotationViewChoicesFromStored(QString()), defaults);
    EXPECT_EQ(an::ui::annotationViewChoicesFromStored(QStringLiteral("not json")), defaults);
    EXPECT_EQ(an::ui::annotationViewChoicesFromStored(
                  QStringLiteral(R"({"detail":"custom","labelText":"sideways","palette":"custom","textSize":"huge"})")),
              defaults);

    const an::ui::AnnotationViewChoices partial =
        an::ui::annotationViewChoicesFromStored(QStringLiteral(R"({"version":9,"palette":"ink","future":true})"));
    EXPECT_EQ(partial.palette, an::ui::AnnotationPalette::Ink);
    EXPECT_EQ(partial.detail, defaults.detail);
    EXPECT_EQ(partial.labelText, defaults.labelText);
    EXPECT_EQ(partial.textSize, defaults.textSize) << "choices kept before the view offered sizes read as the default";
}

// A view's former menu held its annotations to a magnitude limit; the level that names about as much replaces it: the
// brightest limits the notable objects alone, the middle ones what the standard level names, the faintest every object.
TEST(AnnotationViewChoices, ASavedMagnitudeLimitChoosesTheLevelThatNamesAboutAsMuch)
{
    EXPECT_EQ(an::ui::annotationDetailOfMagnitudeLimit(4.0), an::ui::AnnotationDetail::Essential);
    EXPECT_EQ(an::ui::annotationDetailOfMagnitudeLimit(6.0), an::ui::AnnotationDetail::Essential);
    EXPECT_EQ(an::ui::annotationDetailOfMagnitudeLimit(8.0), an::ui::AnnotationDetail::Standard);
    EXPECT_EQ(an::ui::annotationDetailOfMagnitudeLimit(10.0), an::ui::AnnotationDetail::Standard);
    EXPECT_EQ(an::ui::annotationDetailOfMagnitudeLimit(12.0), an::ui::AnnotationDetail::Standard);
    EXPECT_EQ(an::ui::annotationDetailOfMagnitudeLimit(14.0), an::ui::AnnotationDetail::Detailed);
    EXPECT_EQ(an::ui::annotationDetailOfMagnitudeLimit(16.0), an::ui::AnnotationDetail::Complete);
    EXPECT_EQ(an::ui::annotationDetailOfMagnitudeLimit(an::ui::kAnnotationNoMagnitudeLimit),
              an::ui::AnnotationDetail::Complete);
    EXPECT_EQ(an::ui::annotationDetailOfMagnitudeLimit(std::numeric_limits<double>::quiet_NaN()),
              an::ui::AnnotationDetail::Standard);
}

// The text and the lines are set apart: the halo around a letter follows the text's size alone, never the lines'.
TEST(AnnotationSettings, TheLettersHaloFollowsTheTextAndNeverTheLines)
{
    an::ui::AnnotationStyle style = an::ui::defaultAnnotationSettings().style;
    const an::ui::AnnotationMetrics plain = an::ui::annotationMetrics(1000.0, style);
    style.lineWidth = an::ui::kAnnotationMaximumLineWidth;
    const an::ui::AnnotationMetrics thick = an::ui::annotationMetrics(1000.0, style);
    EXPECT_DOUBLE_EQ(thick.haloWidth, plain.haloWidth);
    EXPECT_DOUBLE_EQ(thick.fontSize, plain.fontSize);
    EXPECT_GT(thick.lineWidth, 2.5 * plain.lineWidth);
    style.lineWidth = 1.0;
    style.textSize = 2.0;
    const an::ui::AnnotationMetrics large = an::ui::annotationMetrics(1000.0, style);
    EXPECT_DOUBLE_EQ(large.haloWidth, 2.0 * plain.haloWidth);
    EXPECT_DOUBLE_EQ(large.lineWidth, plain.lineWidth);
}

TEST(AnnotationSettings, ALevelOfDetailAndAPaletteReadBackFromTheirValuesUntilOneIsChanged)
{
    an::ui::AnnotationContent content;
    for (const an::ui::AnnotationDetail detail :
         {an::ui::AnnotationDetail::Essential, an::ui::AnnotationDetail::Standard, an::ui::AnnotationDetail::Detailed,
          an::ui::AnnotationDetail::Complete})
    {
        an::ui::applyAnnotationDetail(content, detail);
        EXPECT_EQ(an::ui::annotationDetailOf(content), detail);
    }
    content.stars.magnitudeLimit = 3.3;
    EXPECT_EQ(an::ui::annotationDetailOf(content), an::ui::AnnotationDetail::Custom);

    for (const an::ui::AnnotationPalette palette : an::ui::annotationPalettes())
    {
        EXPECT_EQ(an::ui::annotationPaletteOf(an::ui::annotationPaletteColors(palette)), palette);
    }
    an::ui::AnnotationColors colors = an::ui::annotationPaletteColors(an::ui::AnnotationPalette::Ink);
    colors.star = QColor(10, 20, 30);
    EXPECT_EQ(an::ui::annotationPaletteOf(colors), an::ui::AnnotationPalette::Custom);
}

TEST(AnnotationSettings, APictureKeepsTheImagesAspectAndIsNeverLargerThanIt)
{
    EXPECT_EQ(an::ui::annotationPictureSize(QSize(7246, 5429), 0u), QSize(7246, 5429));
    EXPECT_EQ(an::ui::annotationPictureSize(QSize(7246, 5429), 9000u), QSize(7246, 5429));
    EXPECT_EQ(an::ui::annotationPictureSize(QSize(7246, 5429), 2048u), QSize(2048, 1534));
    EXPECT_EQ(an::ui::annotationPictureSize(QSize(3000, 6000), 1500u), QSize(750, 1500));
    EXPECT_DOUBLE_EQ(an::ui::annotationReferenceLength(QSize(400, 100)), 200.0);
}

// ---------------------------------------------------------------------------------------------------------------------
// The scene

TEST(AnnotationScene, TheGridIsClippedToTheFrameAndLabelledWhereItMeetsTheEdgesInRoundSteps)
{
    const std::optional<an::WcsSolution> solution = solutionOf(fieldCards(), kWidth, kHeight);
    ASSERT_TRUE(solution.has_value());
    an::ui::AnnotationContent content = an::ui::defaultAnnotationSettings().content;
    const an::ui::AnnotationScene scene = sceneOf(*solution, {}, {}, {}, content);

    int declinations = 0;
    int rightAscensions = 0;
    for (const an::ui::AnnotationGridLine& line : scene.grid)
    {
        ASSERT_FALSE(line.segments.empty());
        for (const QPolygonF& segment : line.segments)
        {
            for (const QPointF& point : segment)
            {
                EXPECT_GE(point.x(), -1.0e-6);
                EXPECT_LE(point.x(), kWidth + 1.0e-6);
                EXPECT_GE(point.y(), -1.0e-6);
                EXPECT_LE(point.y(), kHeight + 1.0e-6);
            }
        }
        EXPECT_FALSE(line.crossings.empty());
        if (line.rightAscension)
        {
            ++rightAscensions;
            // A field of four degrees takes lines five minutes of time apart, each crossing the top and the bottom.
            EXPECT_NEAR(std::fmod(line.coordinateDegrees, 1.25), 0.0, 1.0e-9) << line.label.toStdString();
            ASSERT_EQ(line.crossings.size(), 2u);
            EXPECT_EQ(line.label.size(), 7) << line.label.toStdString();
        }
        else
        {
            ++declinations;
            EXPECT_NEAR(std::fmod(line.coordinateDegrees, 0.5), 0.0, 1.0e-9);
            EXPECT_TRUE(line.label.contains(QChar(0x2032))) << line.label.toStdString();
        }
    }
    EXPECT_GE(declinations, 5);
    EXPECT_LE(declinations, 7);
    EXPECT_EQ(rightAscensions, 3);
    const auto centre = std::find_if(scene.grid.begin(), scene.grid.end(), [](const an::ui::AnnotationGridLine& line) {
        return line.rightAscension && std::abs(line.coordinateDegrees - 120.0) < 1.0e-9;
    });
    ASSERT_NE(centre, scene.grid.end());
    EXPECT_EQ(centre->label, QStringLiteral("08h 00m"));
    EXPECT_NEAR(centre->crossings.front().point.x(), 200.0, 0.5);
    const auto parallel =
        std::find_if(scene.grid.begin(), scene.grid.end(), [](const an::ui::AnnotationGridLine& line) {
            return !line.rightAscension && std::abs(line.coordinateDegrees - 10.0) < 1.0e-9;
        });
    ASSERT_NE(parallel, scene.grid.end());
    EXPECT_EQ(parallel->label, QStringLiteral("+10° 00′"));
    // Drawn toward increasing right ascension - east, toward decreasing columns here - it enters at the right edge.
    ASSERT_EQ(parallel->crossings.size(), 2u);
    EXPECT_EQ(parallel->crossings[0].edge, an::ui::AnnotationEdge::Right);
    EXPECT_EQ(parallel->crossings[1].edge, an::ui::AnnotationEdge::Left);

    content.gridDensity = an::ui::AnnotationGridDensity::Fine;
    EXPECT_GT(sceneOf(*solution, {}, {}, {}, content).grid.size(), scene.grid.size());
    content.layers.grid = false;
    EXPECT_TRUE(sceneOf(*solution, {}, {}, {}, content).grid.empty());
}

TEST(AnnotationScene, EntriesOfOneObjectInTwoCataloguesAreOneMarkUnderItsBestKnownDesignation)
{
    const std::optional<an::WcsSolution> solution = solutionOf(fieldCards(), kWidth, kHeight);
    ASSERT_TRUE(solution.has_value());
    const std::vector<an::CatalogEntry> deepSky{
        deepSkyEntry("NGC 1976", "Orion Nebula", 120.5, 10.2, 40.0, "Bright Nebula", 4.0),
        deepSkyEntry("M 42", "Orion Nebula", 120.5, 10.2, 40.0, "Bright Nebula", 4.0),
        // One catalogue states another position for an object of one name: still the same object.
        deepSkyEntry("C 92", "Carina Nebula", 119.25, 9.5, 30.0, "Bright Nebula", 3.0),
        deepSkyEntry("NGC 3372", "Carina Nebula", 119.4, 9.5, 30.0, "Bright Nebula", std::nullopt),
        // Two objects of one catalogue close together stay two.
        deepSkyEntry("NGC 4038", "Antennae", 119.8, 10.6, 3.0, "Galaxy", 10.5),
        deepSkyEntry("NGC 4039", "Antennae", 119.81, 10.61, 3.0, "Galaxy", 10.7)};
    const an::ui::AnnotationScene scene =
        sceneOf(*solution, deepSky, {}, {}, onlyDeepSky(an::ui::AnnotationDetail::Standard));

    EXPECT_EQ(designationsOf(scene), (std::vector<QString>{QStringLiteral("M 42"), QStringLiteral("NGC 3372"),
                                                           QStringLiteral("NGC 4038"), QStringLiteral("NGC 4039")}));
    const auto orion = std::find_if(scene.marks.begin(), scene.marks.end(),
                                    [](const an::ui::AnnotationMark& mark) { return mark.designation == "M 42"; });
    ASSERT_NE(orion, scene.marks.end());
    EXPECT_EQ(orion->name, QStringLiteral("Orion Nebula"));
    EXPECT_EQ(orion->kind, an::ui::AnnotationMarkKind::Nebula);
    EXPECT_GE(orion->outline.size(), 3);
    // The Messier object is the most prominent, and the scene lists the marks so.
    EXPECT_EQ(scene.marks.front().designation, QStringLiteral("M 42"));
}

TEST(AnnotationScene, EachLevelOfDetailKeepsTheObjectsItNames)
{
    const std::optional<an::WcsSolution> solution = solutionOf(fieldCards(), kWidth, kHeight);
    ASSERT_TRUE(solution.has_value());
    const std::vector<an::CatalogEntry> deepSky{
        deepSkyEntry("M 81", "", 120.2, 10.3, 20.0, "Galaxy", 6.9),
        deepSkyEntry("NGC 2000", "Named Faint", 119.7, 9.4, 1.0, "Galaxy", 13.0),
        deepSkyEntry("NGC 2001", "", 120.6, 9.9, 15.0, "Open Cluster", 11.0),
        // A third of a pixel across: too small to show short of the complete level.
        deepSkyEntry("NGC 2002", "", 119.3, 10.4, 0.2, "Galaxy", 11.0),
        deepSkyEntry("NGC 2003", "", 120.9, 10.9, 5.0, "Galaxy", 13.5),
        deepSkyEntry("IC 2004", "", 119.1, 9.1, 4.0, "Planetary Nebula", std::nullopt),
        deepSkyEntry("NGC 2005", "", 120.1, 9.2, 0.0, "Star", 12.0),
        deepSkyEntry("NGC 2006", "", 250.0, -40.0, 10.0, "Galaxy", 8.0)};
    const auto kept = [&](const an::ui::AnnotationDetail detail) {
        return designationsOf(sceneOf(*solution, deepSky, {}, {}, onlyDeepSky(detail)));
    };
    EXPECT_EQ(kept(an::ui::AnnotationDetail::Essential),
              (std::vector<QString>{QStringLiteral("M 81"), QStringLiteral("NGC 2000")}));
    EXPECT_EQ(kept(an::ui::AnnotationDetail::Standard),
              (std::vector<QString>{QStringLiteral("M 81"), QStringLiteral("NGC 2001")}));
    EXPECT_EQ(kept(an::ui::AnnotationDetail::Detailed),
              (std::vector<QString>{QStringLiteral("IC 2004"), QStringLiteral("M 81"), QStringLiteral("NGC 2000"),
                                    QStringLiteral("NGC 2001"), QStringLiteral("NGC 2003")}));
    EXPECT_EQ(kept(an::ui::AnnotationDetail::Complete),
              (std::vector<QString>{QStringLiteral("IC 2004"), QStringLiteral("M 81"), QStringLiteral("NGC 2000"),
                                    QStringLiteral("NGC 2001"), QStringLiteral("NGC 2002"), QStringLiteral("NGC 2003"),
                                    QStringLiteral("NGC 2005")}));

    an::ui::AnnotationContent galaxiesOnly = onlyDeepSky(an::ui::AnnotationDetail::Complete);
    galaxiesOnly.deepSky.nebulae = galaxiesOnly.deepSky.planetaryNebulae = galaxiesOnly.deepSky.openClusters =
        galaxiesOnly.deepSky.globularClusters = galaxiesOnly.deepSky.otherObjects = false;
    galaxiesOnly.deepSky.messier = false;
    EXPECT_EQ(
        designationsOf(sceneOf(*solution, deepSky, {}, {}, galaxiesOnly)),
        (std::vector<QString>{QStringLiteral("NGC 2000"), QStringLiteral("NGC 2002"), QStringLiteral("NGC 2003")}));
}

TEST(AnnotationScene, AStarIsNamedByItsProperNameAndByItsBayerLetterWhenThoseAreChosen)
{
    const std::optional<an::WcsSolution> solution = solutionOf(fieldCards(), kWidth, kHeight);
    ASSERT_TRUE(solution.has_value());
    const std::vector<an::CatalogEntry> stars{
        starEntry("Alpha Field", "α Fld", 120.2, 10.2, 1.5), starEntry("", "β Fld", 119.8, 9.8, 3.0),
        starEntry("", "γ Fld", 119.6, 9.7, 4.6), starEntry("Far Away", "δ Fld", 200.0, 10.0, 1.0)};
    an::ui::AnnotationContent content = an::ui::defaultAnnotationSettings().content;
    content.layers.deepSky = false;
    content.stars.magnitudeLimit = 4.0;
    an::ui::AnnotationScene scene = sceneOf(*solution, {}, stars, {}, content);
    ASSERT_EQ(scene.marks.size(), 2u);
    EXPECT_EQ(scene.marks[0].name, QStringLiteral("Alpha Field"));
    EXPECT_EQ(scene.marks[0].designation, QStringLiteral("α Fld"));
    EXPECT_EQ(scene.marks[0].kind, an::ui::AnnotationMarkKind::Star);
    EXPECT_GT(scene.marks[0].radius, scene.marks[1].radius);
    EXPECT_EQ(scene.marks[1].designation, QStringLiteral("β Fld"));

    content.stars.naming = an::ui::AnnotationStarNaming::ProperNames;
    scene = sceneOf(*solution, {}, stars, {}, content);
    ASSERT_EQ(scene.marks.size(), 1u);
    EXPECT_TRUE(scene.marks[0].designation.isEmpty());

    content.stars =
        an::ui::AnnotationStarSelection{.magnitudeLimit = 5.0, .naming = an::ui::AnnotationStarNaming::Designations};
    EXPECT_EQ(sceneOf(*solution, {}, stars, {}, content).marks.size(), 3u);
}

// Every star, named or not, as the viewer's star labels do: by its name, its designation, else its catalogue number;
// and a star the naming leaves unnamed is still marked when the choices say so, without a label.
TEST(AnnotationScene, EveryStarIsNamedByItsNumberWhenItHasNoNameOrMarkedWithoutOne)
{
    const std::optional<an::WcsSolution> solution = solutionOf(fieldCards(), kWidth, kHeight);
    ASSERT_TRUE(solution.has_value());
    const std::vector<an::CatalogEntry> stars{starEntry("Alpha Field", "α Fld", 120.2, 10.2, 1.5),
                                              starEntry("", "β Fld", 119.8, 9.8, 3.0),
                                              starEntry("", "", 119.6, 9.7, 5.2)};
    an::ui::AnnotationContent content = an::ui::defaultAnnotationSettings().content;
    content.layers.deepSky = false;
    content.stars = an::ui::AnnotationStarSelection{.magnitudeLimit = 6.0,
                                                    .naming = an::ui::AnnotationStarNaming::CatalogueNumbers};
    an::ui::AnnotationScene scene = sceneOf(*solution, {}, stars, {}, content);
    ASSERT_EQ(scene.marks.size(), 3u);
    EXPECT_EQ(scene.marks[0].name, QStringLiteral("Alpha Field"));
    EXPECT_EQ(scene.marks[0].designation, QStringLiteral("α Fld"));
    EXPECT_EQ(scene.marks[1].designation, QStringLiteral("β Fld"));
    EXPECT_EQ(scene.marks[2].designation, QString::fromStdString(stars[2].identifier))
        << "a star of no name or designation is named by its catalogue number";
    EXPECT_TRUE(std::none_of(scene.marks.begin(), scene.marks.end(),
                             [](const an::ui::AnnotationMark& mark) { return mark.markOnly; }));

    content.stars.naming = an::ui::AnnotationStarNaming::ProperNames;
    EXPECT_EQ(sceneOf(*solution, {}, stars, {}, content).marks.size(), 1u);
    content.stars.markUnnamed = true;
    scene = sceneOf(*solution, {}, stars, {}, content);
    ASSERT_EQ(scene.marks.size(), 3u);
    EXPECT_FALSE(scene.marks[0].markOnly);
    for (std::size_t index = 1u; index < scene.marks.size(); ++index)
    {
        EXPECT_TRUE(scene.marks[index].markOnly);
        EXPECT_TRUE(scene.marks[index].designation.isEmpty() && scene.marks[index].name.isEmpty());
    }
    const an::ui::AnnotationLayout layout =
        an::ui::layoutAnnotations(scene, content, an::ui::defaultAnnotationSettings().style);
    EXPECT_TRUE(layout.labelled[0]);
    EXPECT_EQ(std::count(layout.labelled.begin(), layout.labelled.end(), true), 1)
        << "a star marked without a name takes no label";
}

TEST(AnnotationScene, TheCompassAndTheScaleFollowTheSolutionAtEachCorner)
{
    const std::optional<an::WcsSolution> solution = solutionOf(fieldCards(), kWidth, kHeight);
    ASSERT_TRUE(solution.has_value());
    const an::ui::AnnotationScene scene = sceneOf(*solution, {}, {}, {}, an::ui::defaultAnnotationSettings().content);
    for (const std::optional<an::ui::AnnotationSkyAxes>& axes : scene.cornerAxes)
    {
        ASSERT_TRUE(axes.has_value());
        // North toward increasing rows, east toward decreasing columns, as the matrix states.
        EXPECT_NEAR(axes->north.x(), 0.0, 0.02);
        EXPECT_NEAR(axes->north.y(), 1.0, 1.0e-3);
        EXPECT_NEAR(axes->east.x(), -1.0, 1.0e-3);
        EXPECT_NEAR(axes->east.y(), 0.0, 0.02);
        EXPECT_NEAR(axes->arcsecondsPerPixel, 36.0, 0.1);
    }
    EXPECT_DOUBLE_EQ(an::ui::annotationScaleBarArcseconds(4.0 * 3600.0), 1800.0);
    EXPECT_DOUBLE_EQ(an::ui::annotationScaleBarArcseconds(600.0), 120.0);
    EXPECT_EQ(an::ui::annotationAngleText(1800.0), QStringLiteral("30′"));
    EXPECT_EQ(an::ui::annotationAngleText(5400.0), QStringLiteral("1.5°"));
    EXPECT_EQ(an::ui::annotationAngleText(15.0), QStringLiteral("15″"));
    EXPECT_DOUBLE_EQ(an::ui::annotationGridSpacingDegrees(3.0, false, an::ui::AnnotationGridDensity::Normal), 0.5);
    EXPECT_DOUBLE_EQ(an::ui::annotationGridSpacingDegrees(4.0, true, an::ui::AnnotationGridDensity::Normal), 1.25);
}

TEST(AnnotationScene, ThePanelStatesTheTargetTheFieldTheOpticsAndTheIntegrationTheCardsState)
{
    const std::optional<an::WcsSolution> solution = solutionOf(fieldCards(), kWidth, kHeight);
    ASSERT_TRUE(solution.has_value());
    std::vector<an::CodecCard> cards;
    addCard(cards, "OBJECT", "'Carina Nebula'");
    addCard(cards, "TELESCOP", "'Askar 107PHQ'");
    addCard(cards, "FOCALLEN", "432");
    addCard(cards, "APTDIA", "108");
    addCard(cards, "INSTRUME", "'ZWO ASI2600MM Pro'");
    addCard(cards, "DATE-OBS", "'2026-03-14T01:02:03'");
    addCard(cards, "EXPTIME", "120");
    addCard(cards, "TOTEXP", "58560");
    addCard(cards, "STACKCNT", "488");
    an::ui::AnnotationContent content = an::ui::defaultAnnotationSettings().content;
    content.credit = QStringLiteral("  E. Ruiz ");
    an::ui::AnnotationScene scene = sceneOf(*solution, {}, {}, cards, content);

    EXPECT_EQ(scene.panel.title, QStringLiteral("Carina Nebula"));
    EXPECT_EQ(scene.panel.credit, QStringLiteral("E. Ruiz"));
    const auto valueOf = [&scene](const char* label) {
        for (const an::ui::AnnotationFact& fact : scene.panel.facts)
        {
            if (fact.label == QLatin1String(label))
            {
                return fact.value;
            }
        }
        return QString();
    };
    EXPECT_EQ(valueOf("Center"), QStringLiteral("08h 00m 00s   +10° 00′ 00″"));
    EXPECT_EQ(valueOf("Field"), QStringLiteral("4.00° × 3.00°  ·  36.0″/px"));
    EXPECT_EQ(valueOf("Optics"), QStringLiteral("Askar 107PHQ  ·  432 mm f/4"));
    EXPECT_EQ(valueOf("Camera"), QStringLiteral("ZWO ASI2600MM Pro"));
    EXPECT_EQ(valueOf("Date"), QStringLiteral("2026-03-14"));
    EXPECT_EQ(valueOf("Integration"), QStringLiteral("16 h 16 min  ·  488 frames"));
    EXPECT_TRUE(valueOf("Filter").isEmpty());

    // The person's title is kept over the target's, and a fact left out is not stated.
    content.title = QStringLiteral("NGC 3372");
    content.facts.integration = false;
    scene = sceneOf(*solution, {}, {}, cards, content);
    EXPECT_EQ(scene.panel.title, QStringLiteral("NGC 3372"));
    EXPECT_TRUE(valueOf("Integration").isEmpty());

    // A card's number is read in every spelling a card's number takes: the exponent letter of a FITS real, a sign
    // and a pair of quotes are the same numbers.
    std::vector<an::CodecCard> spelled;
    addCard(spelled, "TELESCOP", "'Askar 107PHQ'");
    addCard(spelled, "FOCALLEN", "4.32D2");
    addCard(spelled, "APTDIA", "'+108'");
    addCard(spelled, "EXPTIME", "1.2D2");
    addCard(spelled, "TOTEXP", "5.856D4");
    addCard(spelled, "STACKCNT", "488");
    scene = sceneOf(*solution, {}, {}, spelled, an::ui::defaultAnnotationSettings().content);
    EXPECT_EQ(valueOf("Optics"), QStringLiteral("Askar 107PHQ  ·  432 mm f/4"));
    EXPECT_EQ(valueOf("Integration"), QStringLiteral("16 h 16 min  ·  488 frames"));
}

// ---------------------------------------------------------------------------------------------------------------------
// The layout

/// A scene of a frame and marks at points, of decreasing priority, and nothing else.
[[nodiscard]] an::ui::AnnotationScene pointScene(const QSize frame, const std::vector<QPointF>& points)
{
    an::ui::AnnotationScene scene;
    scene.imageSize = frame;
    scene.referenceLength = an::ui::annotationReferenceLength(frame);
    for (std::size_t index = 0u; index < points.size(); ++index)
    {
        an::ui::AnnotationMark mark;
        mark.kind = an::ui::AnnotationMarkKind::Galaxy;
        mark.center = points[index];
        mark.radius = 0.0055 * scene.referenceLength;
        mark.designation = QStringLiteral("NGC %1").arg(1000 + index);
        mark.priority = 1000.0 - static_cast<double>(index);
        scene.marks.push_back(std::move(mark));
    }
    return scene;
}

[[nodiscard]] an::ui::AnnotationContent labelsOnly()
{
    an::ui::AnnotationContent content = an::ui::defaultAnnotationSettings().content;
    content.layers = an::ui::AnnotationLayers{
        .grid = false, .deepSky = true, .stars = true, .compass = false, .scaleBar = false, .infoPanel = false};
    return content;
}

TEST(AnnotationLayout, NoTwoLabelsOverlapAndEveryLabelLiesInsideTheFrame)
{
    std::mt19937 random(17u);
    std::uniform_real_distribution<double> across(0.0, 1200.0);
    std::uniform_real_distribution<double> down(0.0, 800.0);
    std::vector<QPointF> points;
    for (int index = 0; index < 160; ++index)
    {
        points.emplace_back(across(random), down(random));
    }
    const an::ui::AnnotationScene scene = pointScene(QSize(1200, 800), points);
    const an::ui::AnnotationLayout layout =
        an::ui::layoutAnnotations(scene, labelsOnly(), an::ui::defaultAnnotationSettings().style);

    ASSERT_FALSE(layout.labels.empty());
    EXPECT_TRUE(layout.labelled.front());
    const QRectF frame(0.0, 0.0, 1200.0, 800.0);
    for (std::size_t first = 0u; first < layout.labels.size(); ++first)
    {
        EXPECT_TRUE(frame.contains(layout.labels[first].box));
        EXPECT_FALSE(layout.labels[first].primary.isEmpty());
        for (std::size_t second = first + 1u; second < layout.labels.size(); ++second)
        {
            EXPECT_FALSE(layout.labels[first].box.intersects(layout.labels[second].box))
                << layout.labels[first].source << " and " << layout.labels[second].source;
        }
    }
    const auto placed = static_cast<std::size_t>(std::count(layout.labelled.begin(), layout.labelled.end(), true));
    EXPECT_EQ(placed, layout.labels.size());
    // The same scene and style lay out the same way.
    const an::ui::AnnotationLayout again =
        an::ui::layoutAnnotations(scene, labelsOnly(), an::ui::defaultAnnotationSettings().style);
    ASSERT_EQ(again.labels.size(), layout.labels.size());
    for (std::size_t index = 0u; index < layout.labels.size(); ++index)
    {
        EXPECT_EQ(again.labels[index].box, layout.labels[index].box);
    }
}

// A picture sets a label's second line - the name under the designation - a step smaller than its first. A smallest
// size asked of the layout, a view's, raises a line set smaller to it and lowers none.
TEST(AnnotationLayout, ASmallestTextSizeRaisesANameUnderADesignationAndLowersNoLine)
{
    an::ui::AnnotationScene scene = pointScene(QSize(1200, 800), {QPointF(600.0, 400.0)});
    scene.marks.front().designation = QStringLiteral("M 31");
    scene.marks.front().name = QStringLiteral("Andromeda Galaxy");
    an::ui::AnnotationContent content = labelsOnly();
    content.labelText = an::ui::AnnotationLabelText::DesignationAndName;
    const an::ui::AnnotationStyle style = an::ui::defaultAnnotationSettings().style;
    // The heights of the label's two lines: the designation's and the name's.
    const auto heights = [&](const double smallest) {
        const an::ui::AnnotationLayout layout = an::ui::layoutAnnotations(scene, content, style, smallest);
        EXPECT_EQ(layout.labels.size(), 1u);
        return layout.labels.empty() ? std::pair{0.0, 0.0}
                                     : std::pair{layout.labels.front().primary.boundingRect().height(),
                                                 layout.labels.front().secondary.boundingRect().height()};
    };
    const double fontSize = an::ui::annotationMetrics(scene.referenceLength, style).fontSize;
    const auto [designation, name] = heights(0.0);
    ASSERT_GT(designation, 0.0);
    ASSERT_GT(name, 0.0);

    const auto [raisedDesignation, raisedName] = heights(fontSize);
    EXPECT_NEAR(raisedName, name / an::ui::kAnnotationSecondLineScale, 1e-9 * fontSize)
        << "the name is set at the designation's size";
    EXPECT_DOUBLE_EQ(raisedDesignation, designation);

    const auto [lowDesignation, lowName] = heights(0.5 * an::ui::kAnnotationSecondLineScale * fontSize);
    EXPECT_DOUBLE_EQ(lowDesignation, designation);
    EXPECT_DOUBLE_EQ(lowName, name) << "a smallest size below both lines changes neither";

    const auto [largeDesignation, largeName] = heights(2.0 * fontSize);
    EXPECT_NEAR(largeDesignation, 2.0 * designation, 1e-9 * fontSize) << "a smallest size above both raises both";
    EXPECT_NEAR(largeName, 2.0 * name / an::ui::kAnnotationSecondLineScale, 1e-9 * fontSize);
}

TEST(AnnotationLayout, ALabelThatCannotSitBesideItsObjectTakesALeaderOrIsLeftOutWithoutOne)
{
    // Twelve objects at one place: a few labels fit beside it, more farther away on leaders. Their marker is as wide as
    // a line of text, so a label above it and one below clear the label set to its right whatever the typeface's line
    // height: with a marker much smaller than a label is tall, how many fit beside it depends on the host's fonts.
    an::ui::AnnotationScene scene = pointScene(QSize(1600, 1200), std::vector<QPointF>(12u, QPointF(800, 600)));
    for (an::ui::AnnotationMark& mark : scene.marks)
    {
        mark.radius = 0.012 * scene.referenceLength;
    }
    an::ui::AnnotationStyle style = an::ui::defaultAnnotationSettings().style;
    style.leaderLines = false;
    const an::ui::AnnotationLayout unled = an::ui::layoutAnnotations(scene, labelsOnly(), style);
    const auto besideIt = std::count(unled.labelled.begin(), unled.labelled.end(), true);
    EXPECT_GE(besideIt, 3);
    EXPECT_LE(besideIt, 8);
    EXPECT_TRUE(unled.labelled.front());
    EXPECT_FALSE(unled.labelled.back());
    for (const an::ui::AnnotationLabel& label : unled.labels)
    {
        EXPECT_FALSE(label.leader.has_value());
    }

    style.leaderLines = true;
    const an::ui::AnnotationLayout led = an::ui::layoutAnnotations(scene, labelsOnly(), style);
    EXPECT_GE(std::count(led.labelled.begin(), led.labelled.end(), true), besideIt + 6);
    int leaders = 0;
    for (const an::ui::AnnotationLabel& label : led.labels)
    {
        if (!label.leader.has_value())
        {
            continue;
        }
        ++leaders;
        // A leader runs from its label's edge to the object's marker.
        const QRectF box = label.box.adjusted(-1.0e-6, -1.0e-6, 1.0e-6, 1.0e-6);
        EXPECT_TRUE(box.contains(label.leader->p1()));
        const QPointF end = label.leader->p2() - QPointF(800, 600);
        EXPECT_NEAR(std::hypot(end.x(), end.y()), scene.marks.front().radius, 1.0e-6);
    }
    EXPECT_GE(leaders, 6);
    // The most prominent object sits beside itself, as the first label placed.
    EXPECT_FALSE(led.labels.front().leader.has_value());
    EXPECT_EQ(led.labels.front().source, 0u);
}

TEST(AnnotationLayout, TheDecorationsTakeTheirCornersAndStackWhenTheyShareOne)
{
    const std::optional<an::WcsSolution> solution = solutionOf(fieldCards(), kWidth, kHeight);
    ASSERT_TRUE(solution.has_value());
    std::vector<an::CodecCard> cards;
    addCard(cards, "OBJECT", "'Field'");
    an::ui::AnnotationContent content = an::ui::defaultAnnotationSettings().content;
    const an::ui::AnnotationScene scene = sceneOf(*solution, {}, {}, cards, content);
    an::ui::AnnotationStyle style = an::ui::defaultAnnotationSettings().style;
    style.textSize = 0.5;

    an::ui::AnnotationLayout layout = an::ui::layoutAnnotations(scene, content, style);
    ASSERT_TRUE(layout.panel.has_value());
    ASSERT_TRUE(layout.compass.has_value());
    ASSERT_TRUE(layout.scaleBar.has_value());
    const QPointF middle(kWidth * 0.5, kHeight * 0.5);
    EXPECT_LT(layout.panel->box.center().x(), middle.x());
    EXPECT_GT(layout.panel->box.center().y(), middle.y());
    EXPECT_GT(layout.compass->box.center().x(), middle.x());
    EXPECT_LT(layout.compass->box.center().y(), middle.y());
    EXPECT_GT(layout.scaleBar->box.center().x(), middle.x());
    EXPECT_GT(layout.scaleBar->box.center().y(), middle.y());
    // Thirty arcminutes at some 36 arcseconds a pixel.
    EXPECT_NEAR(layout.scaleBar->bar.length(), 50.0, 0.5);
    // The north arrow points down the frame, as the solution's north does here.
    EXPECT_GT(layout.compass->northTip.y(), layout.compass->origin.y());

    style.compassCorner = style.scaleBarCorner = style.panelCorner = an::ui::AnnotationCorner::TopLeft;
    layout = an::ui::layoutAnnotations(scene, content, style);
    ASSERT_TRUE(layout.panel.has_value() && layout.compass.has_value() && layout.scaleBar.has_value());
    EXPECT_FALSE(layout.panel->box.intersects(layout.scaleBar->box));
    EXPECT_FALSE(layout.scaleBar->box.intersects(layout.compass->box));
    EXPECT_LT(layout.panel->box.top(), layout.scaleBar->box.top());
    EXPECT_LT(layout.scaleBar->box.top(), layout.compass->box.top());

    // No grid label lies under a decoration.
    for (const an::ui::AnnotationLabel& label : layout.labels)
    {
        EXPECT_FALSE(label.box.intersects(layout.panel->box));
        EXPECT_FALSE(label.box.intersects(layout.compass->box));
    }
}

TEST(AnnotationLayout, TheSameSkyOnAFrameTwiceTheSizeIsLaidOutTheSameWayTwiceAsLarge)
{
    const std::vector<an::CatalogEntry> deepSky{deepSkyEntry("M 81", "Bode's Galaxy", 120.2, 10.3, 20.0, "Galaxy", 6.9),
                                                deepSkyEntry("NGC 2001", "", 120.6, 9.9, 15.0, "Open Cluster", 11.0),
                                                deepSkyEntry("NGC 2003", "", 119.5, 10.9, 5.0, "Galaxy", 11.5)};
    const std::vector<an::CatalogEntry> stars{starEntry("Alpha Field", "α Fld", 119.9, 9.6, 1.5)};
    an::ui::AnnotationContent content = an::ui::defaultAnnotationSettings().content;
    content.layers.infoPanel = false;
    const an::ui::AnnotationStyle style = an::ui::defaultAnnotationSettings().style;

    const std::optional<an::WcsSolution> small = solutionOf(fieldCards(1.0), kWidth, kHeight);
    const std::optional<an::WcsSolution> large = solutionOf(fieldCards(2.0), 2u * kWidth, 2u * kHeight);
    ASSERT_TRUE(small.has_value() && large.has_value());
    const an::ui::AnnotationScene smallScene = sceneOf(*small, deepSky, stars, {}, content);
    const an::ui::AnnotationScene largeScene = sceneOf(*large, deepSky, stars, {}, content);
    const an::ui::AnnotationLayout smallLayout = an::ui::layoutAnnotations(smallScene, content, style);
    const an::ui::AnnotationLayout largeLayout = an::ui::layoutAnnotations(largeScene, content, style);

    EXPECT_NEAR(largeLayout.metrics.fontSize, 2.0 * smallLayout.metrics.fontSize, 1.0e-9);
    ASSERT_EQ(largeLayout.labels.size(), smallLayout.labels.size());
    ASSERT_GE(smallLayout.labels.size(), 4u);
    for (std::size_t index = 0u; index < smallLayout.labels.size(); ++index)
    {
        const QRectF& one = smallLayout.labels[index].box;
        const QRectF& two = largeLayout.labels[index].box;
        EXPECT_NEAR(two.left(), 2.0 * one.left(), 0.05);
        EXPECT_NEAR(two.top(), 2.0 * one.top(), 0.05);
        EXPECT_NEAR(two.width(), 2.0 * one.width(), 0.05);
        EXPECT_NEAR(two.height(), 2.0 * one.height(), 0.05);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// The drawing

/// A frame large enough that a line is some pixels wide.
constexpr QSize kDrawingFrame(2000, 1500);

/// A scene of the drawing frame holding one mark: a galaxy outlined as a circle of a radius about a point, or a star of
/// that marker radius.
[[nodiscard]] an::ui::AnnotationScene sceneOfOne(const an::ui::AnnotationMarkKind kind, const QPointF centre,
                                                 const double radius, const bool markOnly = false)
{
    an::ui::AnnotationScene scene;
    scene.imageSize = kDrawingFrame;
    scene.referenceLength = an::ui::annotationReferenceLength(scene.imageSize);
    an::ui::AnnotationMark mark;
    mark.kind = kind;
    mark.center = centre;
    if (kind == an::ui::AnnotationMarkKind::Star)
    {
        mark.radius = radius;
    }
    else
    {
        for (int step = 0; step < 96; ++step)
        {
            const double angle = 2.0 * std::numbers::pi * step / 96.0;
            mark.outline << centre + QPointF(std::cos(angle), std::sin(angle)) * radius;
        }
    }
    mark.markOnly = markOnly;
    mark.designation = markOnly ? QString() : QStringLiteral("NGC 1");
    mark.priority = 100.0;
    scene.marks.push_back(mark);
    return scene;
}

/// The choices of a scene of marks alone: no grid, no decoration, no panel.
[[nodiscard]] an::ui::AnnotationContent marksAlone()
{
    an::ui::AnnotationContent content = an::ui::defaultAnnotationSettings().content;
    content.layers = an::ui::AnnotationLayers{
        .grid = false, .deepSky = true, .stars = true, .compass = false, .scaleBar = false, .infoPanel = false};
    return content;
}

/// A scene laid out and drawn over transparent pixels, as a transparent picture is.
[[nodiscard]] QImage drawn(const an::ui::AnnotationScene& scene, const an::ui::AnnotationStyle& style)
{
    const an::ui::AnnotationLayout layout = an::ui::layoutAnnotations(scene, marksAlone(), style);
    QImage image(scene.imageSize, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    an::ui::paintAnnotations(painter, scene, layout, style);
    painter.end();
    return image;
}

[[nodiscard]] int alphaAt(const QImage& image, const QPointF point)
{
    return image.pixelColor(point.toPoint()).alpha();
}

// The fill tints an outlined object at its opacity, in its colour; brackets have no area to fill, and a region that
// spans the frame is left clear, or it would tint the whole image.
TEST(AnnotationDrawing, AFillTintsAnOutlinedObjectAndLeavesBracketsAndRegionsClear)
{
    const QPointF centre(1000.0, 750.0);
    const an::ui::AnnotationScene object = sceneOfOne(an::ui::AnnotationMarkKind::Galaxy, centre, 120.0);
    an::ui::AnnotationStyle style = an::ui::defaultAnnotationSettings().style;
    EXPECT_EQ(alphaAt(drawn(object, style), centre), 0) << "an object is not filled unless the style says so";

    style.fillObjects = true;
    style.fillOpacity = 0.3;
    const QImage filled = drawn(object, style);
    const QColor tint = filled.pixelColor(centre.toPoint());
    EXPECT_NEAR(tint.alpha(), 0.3 * 255.0, 6.0);
    EXPECT_NEAR(tint.red(), style.colors.galaxy.red(), 6.0) << "the fill is the object's colour";
    EXPECT_NEAR(tint.green(), style.colors.galaxy.green(), 6.0);
    EXPECT_NEAR(tint.blue(), style.colors.galaxy.blue(), 6.0);
    EXPECT_EQ(alphaAt(filled, QPointF(200.0, 200.0)), 0) << "nothing outside the object is tinted";

    style.marker = an::ui::AnnotationMarker::Brackets;
    EXPECT_EQ(alphaAt(drawn(object, style), centre), 0);
    style.marker = an::ui::AnnotationMarker::Outline;

    const an::ui::AnnotationScene region = sceneOfOne(an::ui::AnnotationMarkKind::Nebula, centre, 900.0);
    EXPECT_EQ(alphaAt(drawn(region, style), centre + QPointF(300.0, 0.0)), 0);
}

// The grid of a part of the image - what a view shows of it - is spaced for that part, as a picture's grid is for its
// whole frame: a corner a tenth of the field across takes lines spaced for a tenth of the field.
TEST(AnnotationScene, TheGridOfAPartOfTheImageIsSpacedForThatPart)
{
    const std::optional<an::WcsSolution> solution = solutionOf(fieldCards(), kWidth, kHeight);
    ASSERT_TRUE(solution.has_value());
    const QRectF corner(0.0, 0.0, kWidth / 10.0, kHeight / 10.0);
    const std::vector<an::ui::AnnotationGridLine> lines =
        an::ui::buildAnnotationGrid(*solution, corner, an::ui::AnnotationGridDensity::Normal);

    int declinations = 0;
    int rightAscensions = 0;
    for (const an::ui::AnnotationGridLine& line : lines)
    {
        for (const QPolygonF& segment : line.segments)
        {
            for (const QPointF& point : segment)
            {
                EXPECT_TRUE(corner.adjusted(-1.0e-6, -1.0e-6, 1.0e-6, 1.0e-6).contains(point));
            }
        }
        (line.rightAscension ? rightAscensions : declinations) += 1;
    }
    EXPECT_GE(declinations, 3);
    EXPECT_GE(rightAscensions, 3);
}

// A field across right ascension zero is gridded on both sides of it, its lines labelled in the hours that end and
// begin the day, every point a finite place in the frame.
TEST(AnnotationScene, AGridAcrossRightAscensionZeroIsDrawnOnBothSides)
{
    const std::optional<an::WcsSolution> solution =
        solutionOf(tangentPlaneCards(northUpPlane(200.5, 150.5, 0.25, 10.0, 0.01)), kWidth, kHeight);
    ASSERT_TRUE(solution.has_value());
    const QRectF frame(0.0, 0.0, kWidth, kHeight);
    const std::vector<an::ui::AnnotationGridLine> lines =
        an::ui::buildAnnotationGrid(*solution, frame, an::ui::AnnotationGridDensity::Normal);

    bool endsTheDay = false;
    bool beginsTheDay = false;
    for (const an::ui::AnnotationGridLine& line : lines)
    {
        for (const QPolygonF& segment : line.segments)
        {
            for (const QPointF& point : segment)
            {
                EXPECT_TRUE(std::isfinite(point.x()) && std::isfinite(point.y()));
            }
        }
        if (line.rightAscension)
        {
            endsTheDay = endsTheDay || line.label.startsWith(QStringLiteral("23h"));
            beginsTheDay = beginsTheDay || line.label.startsWith(QStringLiteral("00h"));
        }
    }
    EXPECT_TRUE(endsTheDay);
    EXPECT_TRUE(beginsTheDay);
}

// A field holding the pole, where every hour of right ascension meets, is gridded around it, inside its frame.
TEST(AnnotationScene, AGridAboutThePoleStaysInsideItsFrame)
{
    const std::optional<an::WcsSolution> solution =
        solutionOf(tangentPlaneCards(northUpPlane(200.5, 150.5, 0.0, 89.5, 0.01)), kWidth, kHeight);
    ASSERT_TRUE(solution.has_value());
    const QRectF frame(0.0, 0.0, kWidth, kHeight);
    const std::vector<an::ui::AnnotationGridLine> lines =
        an::ui::buildAnnotationGrid(*solution, frame, an::ui::AnnotationGridDensity::Normal);

    int rightAscensions = 0;
    for (const an::ui::AnnotationGridLine& line : lines)
    {
        for (const QPolygonF& segment : line.segments)
        {
            for (const QPointF& point : segment)
            {
                EXPECT_TRUE(frame.adjusted(-1.0e-6, -1.0e-6, 1.0e-6, 1.0e-6).contains(point));
            }
        }
        rightAscensions += line.rightAscension ? 1 : 0;
    }
    EXPECT_GE(rightAscensions, 6) << "the hours meet at the pole";
}

// The cells a layout keeps its placed boxes in are four labels' sizes, but text set a fraction of a pixel tall against
// a large frame - a view zoomed far into a large image - never makes more of them than a bound.
TEST(AnnotationLayout, ALayoutsCellsStayBoundedHoweverSmallItsTextIsSet)
{
    const QSizeF frame(9576.0, 6388.0);
    EXPECT_DOUBLE_EQ(an::ui::annotationLayoutCellSize(frame, 100.0), 400.0);
    for (const double fontSize : {2.0, 0.5, 0.1, 0.001})
    {
        const double cell = an::ui::annotationLayoutCellSize(frame, fontSize);
        EXPECT_GE(cell, 4.0 * fontSize);
        EXPECT_LE(std::ceil(frame.width() / cell) * std::ceil(frame.height() / cell),
                  static_cast<double>(an::ui::kAnnotationLayoutMostCells))
            << "text " << fontSize << " pixels tall";
    }
}

// Every grid a layout keeps boxes in is bounded, whatever cell it is asked for. The grid of the marks a label must not
// cover is asked for cells of four font sizes as the labels' own is, and a view zoomed far into a large image sets its
// text a fraction of an image pixel tall: a cell under a pixel over sixty megapixels was a grid of sixty million cells.
TEST(AnnotationLayout, NoGridOfALayoutHoldsMoreCellsThanTheBoundHoweverSmallACellIsAsked)
{
    const QSizeF frame(9576.0, 6388.0);
    for (const double cell : {1600.0, 60.0, 3.75, 1.0, 0.9375, 0.004})
    {
        const std::size_t cells = an::ui::annotationLayoutGridCells(frame, cell);
        EXPECT_GE(cells, 1u);
        EXPECT_LE(static_cast<double>(cells), an::ui::kAnnotationLayoutMostCells) << "a cell of " << cell << " pixels";
    }
    // A cell the frame holds few of is the cell asked for.
    EXPECT_EQ(an::ui::annotationLayoutGridCells(QSizeF(1000.0, 500.0), 100.0), 50u);
    // The labels' grid is the same grid, asked for cells of four font sizes.
    EXPECT_EQ(an::ui::annotationLayoutGridCells(frame, an::ui::annotationLayoutCellSize(frame, 0.25)),
              an::ui::annotationLayoutGridCells(frame, 1.0));
}

// Drawn for the part of the image a view shows, only what lies in that part is drawn, so a view's paint costs what it
// shows: an object and its label outside the part are not drawn.
TEST(AnnotationDrawing, OnlyWhatLiesInThePartShownIsDrawn)
{
    an::ui::AnnotationScene scene = sceneOfOne(an::ui::AnnotationMarkKind::Galaxy, QPointF(400.0, 400.0), 100.0);
    an::ui::AnnotationScene far = sceneOfOne(an::ui::AnnotationMarkKind::Galaxy, QPointF(1600.0, 1150.0), 100.0);
    far.marks.front().designation = QStringLiteral("NGC 2");
    scene.marks.push_back(far.marks.front());
    const an::ui::AnnotationStyle style = an::ui::defaultAnnotationSettings().style;
    const an::ui::AnnotationLayout layout = an::ui::layoutAnnotations(scene, marksAlone(), style);
    ASSERT_EQ(layout.labelled, (std::vector<bool>{true, true}));

    QImage image(scene.imageSize, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    an::ui::paintAnnotations(painter, scene, layout, style, QRectF(0.0, 0.0, 1000.0, 750.0));
    painter.end();

    EXPECT_GT(alphaAt(image, QPointF(500.0, 400.0)), 0) << "the object in the part shown is drawn";
    int drawnOutside = 0;
    for (int y = 800; y < image.height(); y += 2)
    {
        for (int x = 1050; x < image.width(); x += 2)
        {
            drawnOutside += image.pixelColor(x, y).alpha() > 0 ? 1 : 0;
        }
    }
    EXPECT_EQ(drawnOutside, 0) << "nothing of the object outside it is drawn";
}

TEST(AnnotationPicture, AnImageIsPresentedThroughTheLargestReductionThatLeavesItAtLeastThePicturesSize)
{
    const QSize image(2400, 1800);
    EXPECT_EQ(an::ui::annotationPresentationFactor(image, QSize(600, 450)), 4u);
    EXPECT_EQ(an::ui::annotationPresentationFactor(image, QSize(700, 525)), 3u);
    EXPECT_EQ(an::ui::annotationPresentationFactor(image, QSize(1600, 1200)), 1u);
    EXPECT_EQ(an::ui::annotationPresentationFactor(image, image), 1u);
    EXPECT_EQ(an::ui::annotationPresentationFactor(image, QSize(4800, 3600)), 1u) << "a reduction never magnifies";
    EXPECT_EQ(an::ui::annotationPresentationFactor(image, QSize()), 1u);
    EXPECT_EQ(an::ui::annotationPresentationFactor(QSize(), QSize(600, 450)), 1u);
}

// The lines' opacity and the dark edge behind them are the lines' own choices: a plain label keeps the edge behind the
// lines, and no text choice takes it away.
TEST(AnnotationDrawing, TheLinesOpacityAndTheirDarkEdgeAreTheLinesOwn)
{
    const QPointF centre(1000.0, 750.0);
    const double radius = 200.0;
    const an::ui::AnnotationScene object = sceneOfOne(an::ui::AnnotationMarkKind::Galaxy, centre, radius);
    an::ui::AnnotationStyle style = an::ui::defaultAnnotationSettings().style;
    style.lineWidth = an::ui::kAnnotationMaximumLineWidth;
    const an::ui::AnnotationMetrics metrics = an::ui::annotationMetrics(object.referenceLength, style);
    ASSERT_GT(metrics.lineWidth, 4.0);
    const QPointF onLine = centre + QPointF(radius, 0.0);
    const QPointF besideLine = centre + QPointF(radius + 0.95 * metrics.lineWidth, 0.0);

    style.lineHalo = false;
    EXPECT_GT(alphaAt(drawn(object, style), onLine), 240);
    style.lineOpacity = 0.3;
    EXPECT_NEAR(alphaAt(drawn(object, style), onLine), 0.3 * 255.0, 8.0);
    style.lineOpacity = 1.0;
    EXPECT_EQ(alphaAt(drawn(object, style), besideLine), 0) << "no edge when the lines have none";

    style.lineHalo = true;
    style.labelBacking = an::ui::AnnotationLabelBacking::Plain;
    const QImage edged = drawn(object, style);
    EXPECT_GT(alphaAt(edged, besideLine), 0) << "a plain label leaves the lines their edge";
    const QColor edge = edged.pixelColor(besideLine.toPoint());
    EXPECT_LT(edge.red() + edge.green() + edge.blue(), 120) << "the edge is the halo's dark colour";
}

// A ring surrounds its star; a reticle points at it from four sides and leaves it clear, and a star marked without a
// label is drawn whether or not the style marks the objects whose labels found no room.
TEST(AnnotationDrawing, AReticleLeavesItsStarClearWhereARingSurroundsIt)
{
    const QPointF centre(1000.0, 750.0);
    const double radius = 20.0;
    const an::ui::AnnotationScene star = sceneOfOne(an::ui::AnnotationMarkKind::Star, centre, radius, true);
    an::ui::AnnotationStyle style = an::ui::defaultAnnotationSettings().style;
    style.lineWidth = an::ui::kAnnotationMaximumLineWidth;
    style.lineHalo = false;
    style.unlabelledMarks = false;
    const QPointF onRing = centre + QPointF(radius * std::cos(0.8), radius * std::sin(0.8));
    const QPointF alongTick = centre + QPointF(0.5 * (1.0 + an::ui::kAnnotationReticleReach) * radius, 0.0);

    const QImage ring = drawn(star, style);
    EXPECT_GT(alphaAt(ring, onRing), 200) << "a star marked without a label is drawn";
    EXPECT_EQ(alphaAt(ring, centre), 0);

    style.starMarker = an::ui::AnnotationStarMarker::Reticle;
    const QImage reticle = drawn(star, style);
    EXPECT_GT(alphaAt(reticle, alongTick), 200);
    EXPECT_EQ(alphaAt(reticle, onRing), 0) << "a reticle draws no ring";
    EXPECT_EQ(alphaAt(reticle, centre), 0) << "the star itself is left clear";
    EXPECT_EQ(alphaAt(reticle, centre + QPointF(0.6 * radius, 0.0)), 0);
}

// The panel's text has a size of its own: a larger panel leaves the labels' size as it is.
TEST(AnnotationLayout, ThePanelsTextFollowsItsOwnSize)
{
    an::ui::AnnotationScene scene = sceneOfOne(an::ui::AnnotationMarkKind::Galaxy, QPointF(1000.0, 750.0), 60.0);
    scene.panel.title = QStringLiteral("A field");
    scene.panel.facts = {an::ui::AnnotationFact{.label = QStringLiteral("Field"), .value = QStringLiteral("2° × 1°")},
                         an::ui::AnnotationFact{.label = QStringLiteral("Scale"), .value = QStringLiteral("1.1″/px")}};
    an::ui::AnnotationContent content = marksAlone();
    content.layers.infoPanel = true;
    an::ui::AnnotationStyle style = an::ui::defaultAnnotationSettings().style;
    const an::ui::AnnotationLayout ordinary = an::ui::layoutAnnotations(scene, content, style);
    style.panelSize = 2.0;
    const an::ui::AnnotationLayout larger = an::ui::layoutAnnotations(scene, content, style);
    ASSERT_TRUE(ordinary.panel.has_value() && larger.panel.has_value());
    EXPECT_NEAR(larger.panel->box.width(), 2.0 * ordinary.panel->box.width(), 0.02 * ordinary.panel->box.width());
    EXPECT_NEAR(larger.panel->box.height(), 2.0 * ordinary.panel->box.height(), 0.02 * ordinary.panel->box.height());
    EXPECT_DOUBLE_EQ(larger.metrics.fontSize, ordinary.metrics.fontSize);
    ASSERT_EQ(larger.labels.size(), ordinary.labels.size());
    EXPECT_NEAR(larger.labels.front().box.height(), ordinary.labels.front().box.height(), 1.0e-9);
}

// A label is set where it crosses no other object's outline: a small galaxy just under a large one is labelled below
// itself, though above is the place it tries first. The large galaxy's lowest point lies a few pixels above where a
// label above the small one would end, so that label crosses its outline however tall and wide the typeface sets it;
// were the lowest point below that, a label narrow enough would sit wholly inside the large galaxy, crossing nothing.
TEST(AnnotationLayout, ALabelIsSetWhereItCrossesNoOtherObjectsOutline)
{
    an::ui::AnnotationScene scene = sceneOfOne(an::ui::AnnotationMarkKind::Galaxy, QPointF(1000.0, 685.0), 150.0);
    scene.marks.front().designation = QStringLiteral("NGC 2");
    an::ui::AnnotationScene small = sceneOfOne(an::ui::AnnotationMarkKind::Galaxy, QPointF(1000.0, 872.0), 20.0);
    small.marks.front().priority = 50.0;
    scene.marks.push_back(small.marks.front());
    const an::ui::AnnotationLayout layout =
        an::ui::layoutAnnotations(scene, marksAlone(), an::ui::defaultAnnotationSettings().style);
    ASSERT_EQ(layout.labels.size(), 2u);
    const an::ui::AnnotationLabel& label = layout.labels[1];
    ASSERT_EQ(label.source, 1u);
    const QPolygonF& large = scene.marks.front().outline;
    EXPECT_TRUE(std::none_of(large.begin(), large.end(), [&label](const QPointF& point) {
        return label.box.contains(point);
    })) << "the label crosses the large galaxy's outline";
    EXPECT_GT(label.box.top(), scene.marks[1].center.y()) << "the free place is below the small galaxy";
}

// ---------------------------------------------------------------------------------------------------------------------
// The picture

/// A deep-sky catalogue of one large galaxy in the field's upper left and one far away, and a bright-star catalogue of
/// one named star.
[[nodiscard]] QByteArray deepSkyText()
{
    return QByteArray(
        "id,common_name,ra_deg,dec_deg,major_axis_arcmin,minor_axis_arcmin,position_angle_deg,object_type,magnitude\n"
        "NGC 1001,Bright One,120.5,10.6,40.0,24.0,10,Galaxy,8.0\n"
        "NGC 1003,,250.0,-40.0,5.0,5.0,0,Galaxy,7.0\n");
}

[[nodiscard]] QByteArray brightStarText()
{
    return QByteArray("id,common_name,bayer_designation,ra_deg,dec_deg,magnitude,spectral_type\n"
                      "HIP 1,Alpha Field,alf Fld,119.4,9.4,1.5,A0V\n");
}

/// The pictures' image: the field on a frame twice the size, so its labels cover whole pixels at half its size.
constexpr std::uint32_t kPictureWidth = 2u * kWidth;
constexpr std::uint32_t kPictureHeight = 2u * kHeight;

class AnnotatedPictureTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_direct = openExecution();
        ASSERT_NE(m_direct, nullptr);
        m_cards = fieldCards(2.0);
        addCard(m_cards, "OBJECT", "'Test Field'");
        addCard(m_cards, "FOCALLEN", "432");
        addCard(m_cards, "EXPTIME", "120");
        m_asset = makeAssetWithMetadata<float>(
            session(), kPictureWidth, kPictureHeight, Channels::Grey,
            [](std::uint32_t, std::uint32_t, std::uint32_t) { return 0.25F; }, metadataWithCards(m_cards));
        ASSERT_TRUE(m_asset.has_value());
        m_solution = solutionOf(m_cards, kPictureWidth, kPictureHeight);
        ASSERT_TRUE(m_solution.has_value());
        // The case's own operation is ended: the steps admit their own.
        m_direct->scope.reset();

        an::Result<an::ui::AnnotationCatalogEntries> entries =
            an::ui::queryAnnotationCatalogs(session(), *m_solution, fixedCatalogs(deepSkyText(), brightStarText()), {});
        ASSERT_TRUE(entries.hasValue()) << entries.status().message();
        m_entries = std::move(*entries);
        m_content = an::ui::defaultAnnotationSettings().content;
        m_content.layers = an::ui::AnnotationLayers{
            .grid = false, .deepSky = true, .stars = true, .compass = false, .scaleBar = false, .infoPanel = false};
        m_scene = an::ui::buildAnnotationScene(
            an::ui::AnnotationSceneSources{
                .solution = &*m_solution, .deepSky = m_entries.deepSky, .stars = m_entries.stars, .cards = m_cards},
            m_content);
        m_style.textSize = an::ui::kAnnotationMaximumTextSize;
        m_layout = an::ui::layoutAnnotations(m_scene, m_content, m_style);
    }

    void TearDown() override
    {
        m_asset.reset();
        m_direct.reset();
    }

    [[nodiscard]] const an::consumer::EngineSession& session() const
    {
        return m_direct->provider->engineSession();
    }

    /// The image presented with nothing stretched, for a picture of a size, and the picture drawn over it.
    [[nodiscard]] std::optional<QImage> picture(const QSize size, const bool transparent = false)
    {
        std::optional<an::ui::AnnotationBase> base;
        if (!transparent)
        {
            an::Result<an::consumer::AssetReadLease> lease = m_asset->read({});
            EXPECT_TRUE(lease.hasValue());
            std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session());
            EXPECT_TRUE(scope.has_value());
            if (!lease.hasValue() || !scope.has_value())
            {
                return std::nullopt;
            }
            const an::ui::ViewPresentationSettings settings =
                an::ui::annotationPresentationSettings(std::nullopt, an::ui::AnnotationStretch::AsStored, true);
            an::Result<an::ui::AnnotationBase> presented =
                an::ui::presentAnnotationBase(*scope, *lease, settings, nullptr, &session().memory(), size);
            EXPECT_TRUE(presented.hasValue()) << presented.status().message();
            if (!presented.hasValue())
            {
                return std::nullopt;
            }
            base = std::move(*presented);
        }
        an::Result<an::ui::AnnotatedPicture> drawn = an::ui::composeAnnotatedPicture(
            base.has_value() ? &*base : nullptr, size, m_scene, m_layout, m_style, &session().memory());
        EXPECT_TRUE(drawn.hasValue()) << drawn.status().message();
        if (!drawn.hasValue())
        {
            return std::nullopt;
        }
        return drawn->image.copy();
    }

    /// The label of a mark the scene holds.
    [[nodiscard]] const an::ui::AnnotationLabel* labelOf(const QString& designation) const
    {
        for (const an::ui::AnnotationLabel& label : m_layout.labels)
        {
            if (label.role == an::ui::AnnotationLabelRole::Object &&
                (m_scene.marks[label.source].designation == designation ||
                 m_scene.marks[label.source].name == designation))
            {
                return &label;
            }
        }
        return nullptr;
    }

    /// A point of the image, in its own pixels, that no label, halo, outline or marker reaches: where the picture is
    /// the image alone. Taken from the layout, so it holds whatever size the style gives the labels.
    [[nodiscard]] QPointF untouchedPoint() const
    {
        const double clearance = 2.0 * (m_layout.metrics.haloWidth + m_layout.metrics.lineWidth) + 2.0;
        for (double y = 20.0; y < kPictureHeight - 20.0; y += 10.0)
        {
            for (double x = 20.0; x < kPictureWidth - 20.0; x += 10.0)
            {
                const QPointF point(x, y);
                const auto near = [&point, clearance](const QRectF& box) {
                    return box.adjusted(-clearance, -clearance, clearance, clearance).contains(point);
                };
                bool touched = std::any_of(m_layout.labels.begin(), m_layout.labels.end(),
                                           [&near](const an::ui::AnnotationLabel& label) { return near(label.box); });
                for (const an::ui::AnnotationMark& mark : m_scene.marks)
                {
                    touched = touched || near(mark.outline.size() >= 3
                                                  ? mark.outline.boundingRect()
                                                  : QRectF(mark.center.x() - mark.radius, mark.center.y() - mark.radius,
                                                           2.0 * mark.radius, 2.0 * mark.radius));
                }
                if (!touched)
                {
                    return point;
                }
            }
        }
        ADD_FAILURE() << "every point of the picture is annotated";
        return QPointF();
    }

    /// Whether some pixel of a box of the picture is of a colour.
    [[nodiscard]] static bool holdsColor(const QImage& image, const QRectF& box, const QColor& color)
    {
        const QRect pixels = box.toAlignedRect().intersected(image.rect());
        for (int y = pixels.top(); y <= pixels.bottom(); ++y)
        {
            for (int x = pixels.left(); x <= pixels.right(); ++x)
            {
                const QColor pixel = image.pixelColor(x, y);
                if (std::abs(pixel.red() - color.red()) <= 8 && std::abs(pixel.green() - color.green()) <= 8 &&
                    std::abs(pixel.blue() - color.blue()) <= 8 && pixel.alpha() > 200)
                {
                    return true;
                }
            }
        }
        return false;
    }

    std::unique_ptr<an::tests::astrometry::DirectExecution> m_direct;
    std::vector<an::CodecCard> m_cards;
    std::optional<an::consumer::ImageAsset> m_asset;
    std::optional<an::WcsSolution> m_solution;
    an::ui::AnnotationCatalogEntries m_entries;
    an::ui::AnnotationContent m_content;
    an::ui::AnnotationStyle m_style = an::ui::defaultAnnotationSettings().style;
    an::ui::AnnotationScene m_scene;
    an::ui::AnnotationLayout m_layout;
};

// Every reader of fields - a solved view's overlay, an Annotate Image window - reads the product's own catalogues,
// and each kept a copy of its own. They share one now: read once, handed to every reader that asks while one holds
// it, and read again only once its last reader let it go.
TEST_F(AnnotatedPictureTest, TheProductsCataloguesAreReadOnceAndSharedByEveryReaderOfFields)
{
    for (const an::ui::BundledCatalog catalog : {an::ui::BundledCatalog::DeepSky, an::ui::BundledCatalog::BrightStars})
    {
        SCOPED_TRACE(catalog == an::ui::BundledCatalog::DeepSky ? "the deep-sky objects" : "the bright stars");
        an::Result<std::shared_ptr<const an::SkyCatalog>> first = an::ui::sharedBundledCatalog(session(), catalog, {});
        ASSERT_TRUE(first.hasValue()) << first.status().message();
        ASSERT_NE(*first, nullptr);
        EXPECT_GT((*first)->entryCount(), 1000u) << "the catalogue the product ships";

        an::Result<std::shared_ptr<const an::SkyCatalog>> second = an::ui::sharedBundledCatalog(session(), catalog, {});
        ASSERT_TRUE(second.hasValue()) << second.status().message();
        EXPECT_EQ(second->get(), first->get()) << "the second reader holds the first one's catalogue";

        const std::weak_ptr<const an::SkyCatalog> held = *first;
        first->reset();
        EXPECT_FALSE(held.expired()) << "kept while a reader holds it";
        second->reset();
        EXPECT_TRUE(held.expired()) << "and let go with its last reader";

        const an::Result<std::shared_ptr<const an::SkyCatalog>> again =
            an::ui::sharedBundledCatalog(session(), catalog, {});
        ASSERT_TRUE(again.hasValue()) << again.status().message();
        ASSERT_NE(*again, nullptr);
        EXPECT_GT((*again)->entryCount(), 1000u);
    }
}

TEST_F(AnnotatedPictureTest, TheCataloguesAreAskedForTheFieldAndTwoDegreesAroundIt)
{
    ASSERT_EQ(m_entries.deepSky.size(), 1u);
    EXPECT_EQ(m_entries.deepSky.front().identifier, "NGC 1001");
    ASSERT_EQ(m_entries.stars.size(), 1u);
    EXPECT_EQ(m_entries.stars.front().commonName, "Alpha Field");
    ASSERT_EQ(m_scene.marks.size(), 2u);
}

TEST_F(AnnotatedPictureTest, APictureDrawsItsAnnotationsOverThePresentedImage)
{
    const QSize full(static_cast<int>(kPictureWidth), static_cast<int>(kPictureHeight));
    const std::optional<QImage> drawn = picture(full);
    ASSERT_TRUE(drawn.has_value());
    ASSERT_EQ(drawn->size(), full);
    // Where nothing is drawn the picture is the image: a quarter of full scale, unstretched.
    const QPointF untouched = untouchedPoint();
    const QColor plain = drawn->pixelColor(untouched.toPoint());
    EXPECT_NEAR(plain.red(), 64, 1);
    EXPECT_EQ(plain.red(), plain.green());
    EXPECT_EQ(plain.alpha(), 255);
    // The galaxy's label is drawn in the galaxy's colour, the star's in the star's.
    const an::ui::AnnotationLabel* galaxy = labelOf(QStringLiteral("NGC 1001"));
    const an::ui::AnnotationLabel* star = labelOf(QStringLiteral("Alpha Field"));
    ASSERT_NE(galaxy, nullptr);
    ASSERT_NE(star, nullptr);
    EXPECT_TRUE(holdsColor(*drawn, galaxy->box, m_style.colors.galaxy));
    EXPECT_TRUE(holdsColor(*drawn, star->box, m_style.colors.star));
}

TEST_F(AnnotatedPictureTest, ASmallerPictureIsTheSameDrawingAtItsScale)
{
    const QSize size =
        an::ui::annotationPictureSize(QSize(static_cast<int>(kPictureWidth), static_cast<int>(kPictureHeight)), 400u);
    ASSERT_EQ(size, QSize(400, 300));
    const std::optional<QImage> drawn = picture(size);
    ASSERT_TRUE(drawn.has_value());
    ASSERT_EQ(drawn->size(), size);
    EXPECT_NEAR(drawn->pixelColor((untouchedPoint() * 0.5).toPoint()).red(), 64, 1);
    const an::ui::AnnotationLabel* galaxy = labelOf(QStringLiteral("NGC 1001"));
    ASSERT_NE(galaxy, nullptr);
    const QRectF scaled(galaxy->box.left() * 0.5, galaxy->box.top() * 0.5, galaxy->box.width() * 0.5,
                        galaxy->box.height() * 0.5);
    EXPECT_TRUE(holdsColor(*drawn, scaled, m_style.colors.galaxy));
}

// A reduction leaves out the partial blocks at an image's right and bottom edges, so the presented image covers a
// little less than the image and, scaled to the picture, may end a row or a column short of it. The picture is the
// image to its last row and column all the same: none of it is left the colour the picture was cleared to.
TEST_F(AnnotatedPictureTest, APictureOfAnImageNoReductionDividesIsTheImageToItsLastRowAndColumn)
{
    // 958 by 639 pixels at a sixth: the reduction by five covers 955 by 635 of them.
    constexpr std::uint32_t kWide = 958u;
    constexpr std::uint32_t kTall = 639u;
    const QSize image(static_cast<int>(kWide), static_cast<int>(kTall));
    const std::optional<an::consumer::ImageAsset> asset = makeAssetWithMetadata<float>(
        session(), kWide, kTall, Channels::Grey, [](std::uint32_t, std::uint32_t, std::uint32_t) { return 0.25F; },
        metadataWithCards(m_cards));
    ASSERT_TRUE(asset.has_value());
    const QSize size = an::ui::annotationPictureSize(image, 160u);
    ASSERT_EQ(an::ui::annotationPresentationFactor(image, size), 5u);

    an::Result<an::consumer::AssetReadLease> lease = asset->read({});
    ASSERT_TRUE(lease.hasValue());
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session());
    ASSERT_TRUE(scope.has_value());
    const an::ui::ViewPresentationSettings settings =
        an::ui::annotationPresentationSettings(std::nullopt, an::ui::AnnotationStretch::AsStored, true);
    an::Result<an::ui::AnnotationBase> base =
        an::ui::presentAnnotationBase(*scope, *lease, settings, nullptr, &session().memory(), size);
    ASSERT_TRUE(base.hasValue()) << base.status().message();
    ASSERT_LT(base->covers.width(), static_cast<double>(kWide)) << "the reduction left a partial block out";
    ASSERT_LT(base->covers.height(), static_cast<double>(kTall));

    // Nothing is annotated: the picture is the image alone.
    an::ui::AnnotationScene nothing;
    nothing.imageSize = image;
    nothing.referenceLength = m_scene.referenceLength;
    const an::ui::AnnotationLayout empty = an::ui::layoutAnnotations(nothing, m_content, m_style);
    an::Result<an::ui::AnnotatedPicture> drawn =
        an::ui::composeAnnotatedPicture(&*base, size, nothing, empty, m_style, &session().memory());
    ASSERT_TRUE(drawn.hasValue()) << drawn.status().message();
    ASSERT_EQ(drawn->image.size(), size);
    for (int x = 0; x < size.width(); ++x)
    {
        EXPECT_NEAR(drawn->image.pixelColor(x, size.height() - 1).red(), 64, 1) << "the last row, at column " << x;
    }
    for (int y = 0; y < size.height(); ++y)
    {
        EXPECT_NEAR(drawn->image.pixelColor(size.width() - 1, y).red(), 64, 1) << "the last column, at row " << y;
    }
}

TEST_F(AnnotatedPictureTest, ATransparentPictureHoldsTheAnnotationsAlone)
{
    const std::optional<QImage> drawn =
        picture(QSize(static_cast<int>(kPictureWidth), static_cast<int>(kPictureHeight)), true);
    ASSERT_TRUE(drawn.has_value());
    EXPECT_EQ(drawn->pixelColor(untouchedPoint().toPoint()).alpha(), 0);
    const an::ui::AnnotationLabel* galaxy = labelOf(QStringLiteral("NGC 1001"));
    ASSERT_NE(galaxy, nullptr);
    EXPECT_TRUE(holdsColor(*drawn, galaxy->box, m_style.colors.galaxy));
}

// The picture's pixels are laid out into its image a band of rows at a time, so the picture is never held twice more
// beside the image it becomes. A picture taller than a band is the same image all the same, pixel for pixel and plane
// for plane, with its transparency kept when it has one.
TEST_F(AnnotatedPictureTest, ThePicturesImageHoldsEveryPixelOfAPictureTallerThanOneBand)
{
    an::Result<an::CodecMetadata> frame = an::CodecMetadata::create(an::SourceEncoding{}, m_cards);
    ASSERT_TRUE(frame.hasValue());
    for (const bool alpha : {false, true})
    {
        SCOPED_TRACE(alpha ? "with transparency" : "opaque");
        QImage picture(37, 700, QImage::Format_ARGB32_Premultiplied);
        for (int y = 0; y < picture.height(); ++y)
        {
            for (int x = 0; x < picture.width(); ++x)
            {
                picture.setPixelColor(
                    x, y, QColor((x * 7 + y) % 256, (y * 3) % 256, (x + 2 * y) % 256, alpha ? 55 + y % 200 : 255));
            }
        }
        const QImage expected = picture.convertToFormat(alpha ? QImage::Format_RGBA8888 : QImage::Format_RGB888);
        const std::uint32_t channels = alpha ? 4u : 3u;

        an::Result<an::consumer::ImageAsset> asset =
            an::ui::annotatedPictureAsset(session(), picture, *frame, *m_solution, alpha, {});
        ASSERT_TRUE(asset.hasValue()) << asset.status().message();
        an::Result<an::consumer::AssetReadLease> lease = asset->read({});
        ASSERT_TRUE(lease.hasValue());
        const auto* image = std::get_if<an::Image<std::uint8_t>>(&lease->image());
        ASSERT_NE(image, nullptr);
        ASSERT_TRUE(image->view().has_value());
        ASSERT_EQ(image->view()->layout().extent().channelCount(), channels);
        std::size_t different = 0u;
        for (int y = 0; y < picture.height(); ++y)
        {
            const uchar* const row = expected.constScanLine(y);
            for (int x = 0; x < picture.width(); ++x)
            {
                for (std::uint32_t channel = 0u; channel < channels; ++channel)
                {
                    const std::uint8_t* sample = image->view()->sampleAt(channel, static_cast<std::uint32_t>(x),
                                                                         static_cast<std::uint32_t>(y), 0u);
                    ASSERT_NE(sample, nullptr);
                    different += *sample == row[static_cast<std::size_t>(x) * channels + channel] ? 0u : 1u;
                }
            }
        }
        EXPECT_EQ(different, 0u) << "samples that are not the picture's";
    }
}

TEST_F(AnnotatedPictureTest, ThePicturesImageCarriesTheFramesFactsAndItsSolutionAtItsScale)
{
    const QSize size(400, 300);
    const std::optional<QImage> drawn = picture(size);
    ASSERT_TRUE(drawn.has_value());
    an::Result<an::CodecMetadata> frame = an::CodecMetadata::create(an::SourceEncoding{}, m_cards);
    ASSERT_TRUE(frame.hasValue());
    an::Result<an::consumer::ImageAsset> asset =
        an::ui::annotatedPictureAsset(session(), *drawn, *frame, *m_solution, false, {});
    ASSERT_TRUE(asset.hasValue()) << asset.status().message();

    an::Result<an::consumer::AssetReadLease> lease = asset->read({});
    ASSERT_TRUE(lease.hasValue());
    std::vector<an::CodecCard> cards;
    EXPECT_EQ(an::consumer::sampleTypeOf(lease->image()), an::SampleType::UInt8);
    lease->visit([&](const auto& typed) {
        const std::optional<an::ImageDescriptor> described = typed.descriptor();
        ASSERT_TRUE(described.has_value());
        EXPECT_EQ(described->extent().width(), 400u);
        EXPECT_EQ(described->extent().height(), 300u);
        EXPECT_EQ(described->extent().channelCount(), 3u);
        const auto stated = typed.metadata().codec().cards();
        cards.assign(stated.begin(), stated.end());
        EXPECT_EQ(an::readProcessingStateCards(typed.metadata().codec()).linearity, an::RecordedLinearity::Nonlinear);
    });
    EXPECT_EQ(cardValue(cards, "OBJECT"), std::optional<std::string>("'Test Field'"));
    EXPECT_EQ(cardValue(cards, "FOCALLEN"), std::optional<std::string>("432"));
    // The solution moved to the picture's grid: the same sky over half as many pixels each way.
    const std::optional<an::WcsSolution> placed = solutionOf(cards, 400u, 300u);
    ASSERT_TRUE(placed.has_value());
    EXPECT_NEAR(placed->meanPixelScaleArcsecPerPixel(), 36.0, 1.0e-6);
    const an::Result<an::SkyCoordinate> centre = placed->fieldCenter();
    ASSERT_TRUE(centre.hasValue());
    EXPECT_NEAR(centre->rightAscensionDegrees, 120.0, 1.0e-6);
    EXPECT_NEAR(centre->declinationDegrees, 10.0, 1.0e-6);
}

} // namespace
