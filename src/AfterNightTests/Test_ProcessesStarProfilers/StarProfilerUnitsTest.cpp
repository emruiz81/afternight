// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The star profilers' support, unit by unit: the detector's request each layout's dialog makes, the first layout's
/// shape and size rule, the figures each table shows and orders by, the documents each writes and reads and the
/// coordinate convention they are read and written in - documents the engine's writer wrote over planted stars among
/// them - the one detector over a planted field, and the catalog enrichment against an installed Gaia catalog.

#include "Test_ProcessesStarProfilers/StarProfilerTestSupport.h"

#include "processes/StarProfileStars.h"
#include "processes/StarProfilerSupport.h"
#include "session/ExecutionScope.h"

#include <AfterNightEngine/Algorithms/Catalog/GaiaCatalogRecord.h>
#include <AfterNightEngine/IO/StarProfileFile.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

namespace
{

using namespace an::tests::processes;
using namespace an::tests::processes::stars;
namespace profiler = an::ui::processes;

/// A detected star at a position, its fit stated by its widths.
[[nodiscard]] an::DetectedStar starAt(double x, double y, double fullWidthX, double fullWidthY, double amplitude = 0.3)
{
    an::DetectedStar star;
    star.positionX = x;
    star.positionY = y;
    star.psf.amplitude = amplitude;
    star.psf.background = 0.05;
    star.psf.fullWidthX = fullWidthX;
    star.psf.fullWidthY = fullWidthY;
    star.psf.scaleX = fullWidthX / an::kGaussianFullWidthPerSigma;
    star.psf.scaleY = fullWidthY / an::kGaussianFullWidthPerSigma;
    star.psf.angleDegrees = 12.5;
    star.psf.residualRootMeanSquare = 0.001;
    star.psf.boxRadius = 7u;
    star.metrics.meanFullWidth = 0.5 * (fullWidthX + fullWidthY);
    star.metrics.roundness = fullWidthY / fullWidthX;
    star.metrics.circularity = amplitude / (0.1 + std::abs(fullWidthX - fullWidthY));
    star.instrumentalMagnitude = -2.5 * std::log10(amplitude * 20.0);
    star.signalToNoise = 40.0;
    return star;
}

/// Runs a body under an admitted execution of the case's session.
template <typename BodyT> void underScope(const an::consumer::EngineSession& session, BodyT body)
{
    an::Result<an::ui::ExecutionScope> scope = an::ui::ExecutionScope::admit(
        session, an::ui::ExecutionRequest{.purpose = an::consumer::OperationPurpose::Apply},
        an::ui::ExecutionObserver{});
    ASSERT_TRUE(scope.hasValue()) << scope.status().message();
    body(*scope);
    scope->finish(an::Status::success());
}

using StarProfilerUnitsTest = ProfilerCase;

// ---- The requests.

// The first layout's dialog states its controls in the detector's terms: the threshold is the smallest fitted
// amplitude, the detector's other defaults stand and its roundness window is opened wide for the tolerance to hold.
TEST(StarProfilerRequestTest, TheFirstLayoutsThresholdIsTheSmallestAmplitude)
{
    const profiler::FirstProfilerSettings settings{.detectionThreshold = 0.2, .roundnessTolerance = 1.5};
    const an::StarDetectionRequest request = profiler::firstProfilerDetection(settings);
    EXPECT_DOUBLE_EQ(request.minimumAmplitude, 0.2);
    EXPECT_GT(request.maximumAmplitude, 1.0e100);
    EXPECT_LT(request.minimumRoundness, 0.05);
    EXPECT_DOUBLE_EQ(request.maximumRoundness, 1.0);
    EXPECT_EQ(request.searchRadius, an::StarDetectionRequest{}.searchRadius);
    EXPECT_DOUBLE_EQ(request.noiseSigma, an::StarDetectionRequest{}.noiseSigma);
    EXPECT_EQ(request.model, an::PsfModel::Gaussian);
}

// The second layout's dialog is the detector's own request, field for field, and an unchecked range is none.
TEST(StarProfilerRequestTest, TheSecondLayoutsDialogIsTheDetectorsRequest)
{
    profiler::SecondProfilerSettings settings;
    settings.radius = 9;
    settings.sigma = 2.5;
    settings.roundness = 0.3;
    settings.convergence = 3;
    settings.relaxChecks = true;
    settings.moffat = true;
    settings.minimumBeta = 2.0;
    // The range's figures stand in the dialog while its box is unchecked, and mean nothing then.
    settings.minimumAmplitude = 0.1;
    settings.maximumAmplitude = 0.8;
    an::StarDetectionRequest request = profiler::secondProfilerDetection(settings);
    EXPECT_EQ(request.searchRadius, 9u);
    EXPECT_DOUBLE_EQ(request.noiseSigma, 2.5);
    EXPECT_DOUBLE_EQ(request.minimumRoundness, 0.3);
    EXPECT_DOUBLE_EQ(request.maximumRoundness, 1.0);
    EXPECT_EQ(request.convergence, 3u);
    EXPECT_TRUE(request.relaxedChecks);
    EXPECT_EQ(request.model, an::PsfModel::MoffatFreeBeta);
    EXPECT_DOUBLE_EQ(request.minimumBeta, 2.0);
    EXPECT_DOUBLE_EQ(request.minimumAmplitude, 0.0);
    EXPECT_DOUBLE_EQ(request.maximumAmplitude, 0.0);

    settings.amplitudeRange = true;
    settings.roundnessRange = true;
    settings.maximumRoundness = 0.9;
    request = profiler::secondProfilerDetection(settings);
    EXPECT_DOUBLE_EQ(request.minimumAmplitude, 0.1);
    EXPECT_DOUBLE_EQ(request.maximumAmplitude, 0.8);
    EXPECT_DOUBLE_EQ(request.maximumRoundness, 0.9);
}

// The first layout keeps a star whose two fitted standard deviations lie within the tolerance of each other and whose
// mean full width lies inside the size window, both ends included.
TEST(StarProfilerRequestTest, TheFirstLayoutHoldsTheShapeAndTheSize)
{
    const profiler::FirstProfilerSettings settings{
        .detectionThreshold = 0.1, .roundnessTolerance = 1.0, .minimumSize = 2, .maximumSize = 10};
    EXPECT_TRUE(profiler::firstProfilerKeeps(starAt(50.5, 50.5, 4.0, 3.0), settings));
    // Standard deviations 2.0 apart: the difference of the widths over 2.3548.
    EXPECT_FALSE(
        profiler::firstProfilerKeeps(starAt(50.5, 50.5, 8.0, 8.0 - 2.0 * an::kGaussianFullWidthPerSigma), settings));
    EXPECT_TRUE(profiler::firstProfilerKeeps(starAt(50.5, 50.5, 2.0, 2.0), settings));
    EXPECT_TRUE(profiler::firstProfilerKeeps(starAt(50.5, 50.5, 10.0, 10.0), settings));
    EXPECT_FALSE(profiler::firstProfilerKeeps(starAt(50.5, 50.5, 1.9, 1.9), settings));
    EXPECT_FALSE(profiler::firstProfilerKeeps(starAt(50.5, 50.5, 10.2, 10.2), settings));
}

// The size window holds the mean of the two full widths, not either of them: a star whose major width lies outside the
// window but whose mean lies inside is kept, and the other way round.
TEST(StarProfilerRequestTest, TheSizeWindowHoldsTheMeanFullWidth)
{
    const profiler::FirstProfilerSettings settings{
        .detectionThreshold = 0.1, .roundnessTolerance = 10.0, .minimumSize = 4, .maximumSize = 10};
    EXPECT_TRUE(profiler::firstProfilerKeeps(starAt(50.5, 50.5, 12.0, 6.0), settings)) << "mean 9, major 12";
    EXPECT_FALSE(profiler::firstProfilerKeeps(starAt(50.5, 50.5, 12.0, 10.0), settings)) << "mean 11";
    EXPECT_TRUE(profiler::firstProfilerKeeps(starAt(50.5, 50.5, 3.0, 6.0), settings)) << "mean 4.5, minor 3";
    EXPECT_FALSE(profiler::firstProfilerKeeps(starAt(50.5, 50.5, 3.0, 4.0), settings)) << "mean 3.5";
}

// ---- The figures.

// The first layout's thirteen figures, from the fitted star: the amplitude, the circularity and half the mean width -
// the figures its filter offers - and the rest stated from the widths and the angle.
TEST(StarProfilerFigureTest, TheFirstLayoutsFiguresComeFromTheFit)
{
    const an::DetectedStar star = starAt(10.5, 20.5, 5.0, 3.0, 0.4);
    using Column = profiler::FirstProfilerColumn;
    EXPECT_DOUBLE_EQ(profiler::firstProfilerFigure(star, Column::X), 10.5);
    EXPECT_DOUBLE_EQ(profiler::firstProfilerFigure(star, Column::Y), 20.5);
    EXPECT_DOUBLE_EQ(profiler::firstProfilerFigure(star, Column::Intensity), 0.4);
    EXPECT_DOUBLE_EQ(profiler::firstProfilerFigure(star, Column::Percentage), 1.0);
    EXPECT_DOUBLE_EQ(profiler::firstProfilerFigure(star, Column::DeltaRadius), 1.0);
    EXPECT_DOUBLE_EQ(profiler::firstProfilerFigure(star, Column::Quality), star.metrics.circularity);
    EXPECT_DOUBLE_EQ(profiler::firstProfilerFigure(star, Column::Radius), 2.0);
    EXPECT_DOUBLE_EQ(profiler::firstProfilerFigure(star, Column::MeanRadius), 2.0 * 1.5 / 2.35);
    EXPECT_DOUBLE_EQ(profiler::firstProfilerFigure(star, Column::LargeMajorAxis), 2.5);
    EXPECT_DOUBLE_EQ(profiler::firstProfilerFigure(star, Column::SmallMinorAxis), 1.5);
    EXPECT_DOUBLE_EQ(profiler::firstProfilerFigure(star, Column::MajorAxisAngle), 12.5);
    // The filter window's radius is the same figure as the table's.
    EXPECT_DOUBLE_EQ(*an::ui::starFilterValue(star, an::ui::StarFilterField::Radius),
                     profiler::firstProfilerFigure(star, Column::Radius));
}

// The second layout's fifteen figures; a figure the star does not have - a Gaussian's exponent, a world coordinate the
// solution did not give it - shows as "-".
TEST(StarProfilerFigureTest, TheSecondLayoutsFiguresAndTheirAbsence)
{
    an::DetectedStar star = starAt(10.5, 20.5, 5.0, 4.0, 0.4);
    using Column = profiler::SecondProfilerColumn;
    EXPECT_EQ(profiler::secondProfilerFigure(star, nullptr, Column::Beta), std::nullopt);
    EXPECT_EQ(profiler::secondProfilerFigure(star, nullptr, Column::RightAscension), std::nullopt);
    EXPECT_EQ(profiler::figureText(std::nullopt, 6), QStringLiteral("-"));
    EXPECT_DOUBLE_EQ(*profiler::secondProfilerFigure(star, nullptr, Column::Ratio), 0.8);
    EXPECT_DOUBLE_EQ(*profiler::secondProfilerFigure(star, nullptr, Column::FullWidth), 4.5);
    star.psf.beta = 2.75;
    EXPECT_DOUBLE_EQ(*profiler::secondProfilerFigure(star, nullptr, Column::Beta), 2.75);
    const profiler::StarEnrichment enrichment{
        .direction = an::SkyCoordinate{.rightAscensionDegrees = 83.5, .declinationDegrees = 22.0}};
    EXPECT_DOUBLE_EQ(*profiler::secondProfilerFigure(star, &enrichment, Column::RightAscension), 83.5);
    EXPECT_DOUBLE_EQ(*profiler::secondProfilerFigure(star, &enrichment, Column::Declination), 22.0);
    EXPECT_EQ(profiler::figureText(83.5, 6), QStringLiteral("83.500000"));
}

// An ordering is stable, largest first when asked, and a star without the figure comes after every star with it
// ascending and before them descending.
TEST(StarProfilerFigureTest, AnOrderingIsStableAndPutsTheAbsentLastAscending)
{
    const an::ui::StarList stars{starAt(1.5, 1.5, 3.0, 3.0, 0.2), starAt(2.5, 2.5, 3.0, 3.0, 0.5),
                                 starAt(3.5, 3.5, 3.0, 3.0, 0.2)};
    const an::ui::StarList brightest =
        profiler::orderedForFirstProfiler(stars, profiler::FirstProfilerColumn::Intensity, true);
    ASSERT_EQ(brightest.size(), 3u);
    EXPECT_DOUBLE_EQ(brightest[0].positionX, 2.5);
    EXPECT_DOUBLE_EQ(brightest[1].positionX, 1.5) << "equal figures keep their order";
    EXPECT_DOUBLE_EQ(brightest[2].positionX, 3.5);

    // Only the second star is placed on the sky.
    profiler::StarEnrichmentTable enrichment;
    enrichment[profiler::starPositionKey(stars[1])].direction =
        an::SkyCoordinate{.rightAscensionDegrees = 10.0, .declinationDegrees = 5.0};
    const an::ui::StarList ascending =
        profiler::orderedForSecondProfiler(stars, enrichment, profiler::SecondProfilerColumn::RightAscension, false);
    EXPECT_DOUBLE_EQ(ascending[0].positionX, 2.5);
    const an::ui::StarList descending =
        profiler::orderedForSecondProfiler(stars, enrichment, profiler::SecondProfilerColumn::RightAscension, true);
    EXPECT_DOUBLE_EQ(descending[2].positionX, 2.5);

    // Where every star has the figure, a column no engine key covers orders by it: smallest first, then largest first.
    profiler::StarEnrichmentTable placed;
    const double rightAscensions[] = {30.0, 10.0, 20.0};
    for (std::size_t index = 0u; index < stars.size(); ++index)
    {
        placed[profiler::starPositionKey(stars[index])].direction =
            an::SkyCoordinate{.rightAscensionDegrees = rightAscensions[index], .declinationDegrees = 5.0};
    }
    const an::ui::StarList smallest =
        profiler::orderedForSecondProfiler(stars, placed, profiler::SecondProfilerColumn::RightAscension, false);
    EXPECT_DOUBLE_EQ(smallest[0].positionX, 2.5);
    EXPECT_DOUBLE_EQ(smallest[1].positionX, 3.5);
    EXPECT_DOUBLE_EQ(smallest[2].positionX, 1.5);
    const an::ui::StarList largest =
        profiler::orderedForSecondProfiler(stars, placed, profiler::SecondProfilerColumn::RightAscension, true);
    EXPECT_DOUBLE_EQ(largest[0].positionX, 1.5);
    EXPECT_DOUBLE_EQ(largest[1].positionX, 3.5);
    EXPECT_DOUBLE_EQ(largest[2].positionX, 2.5);

    const an::ui::StarList widths{starAt(1.5, 1.5, 4.0, 3.0), starAt(2.5, 2.5, 8.0, 3.0), starAt(3.5, 3.5, 5.0, 3.0)};
    const an::ui::StarList widest =
        profiler::orderedForFirstProfiler(widths, profiler::FirstProfilerColumn::DeltaRadius, true);
    EXPECT_DOUBLE_EQ(widest[0].positionX, 2.5);
    EXPECT_DOUBLE_EQ(widest[1].positionX, 3.5);
    EXPECT_DOUBLE_EQ(widest[2].positionX, 1.5);
    const an::ui::StarList narrowest =
        profiler::orderedForFirstProfiler(widths, profiler::FirstProfilerColumn::DeltaRadius, false);
    EXPECT_DOUBLE_EQ(narrowest[0].positionX, 1.5);
    EXPECT_DOUBLE_EQ(narrowest[2].positionX, 2.5);
}

// An enrichment is filed under a star's whole position: two stars in one column are two records.
TEST(StarProfilerFigureTest, AnEnrichmentIsFiledUnderTheWholePosition)
{
    const an::ui::StarList stars{starAt(40.5, 10.5, 3.0, 3.0), starAt(40.5, 90.5, 3.0, 3.0)};
    profiler::ProfileEnrichment enrichment;
    enrichment.stars.resize(2u);
    enrichment.stars[0].direction = an::SkyCoordinate{.rightAscensionDegrees = 11.0, .declinationDegrees = 1.0};
    enrichment.stars[1].direction = an::SkyCoordinate{.rightAscensionDegrees = 22.0, .declinationDegrees = 2.0};
    const profiler::StarEnrichmentTable table = profiler::enrichmentTable(stars, enrichment);
    EXPECT_EQ(table.size(), 2u);
    const profiler::StarEnrichment* first = profiler::enrichmentOf(table, stars[0]);
    const profiler::StarEnrichment* second = profiler::enrichmentOf(table, stars[1]);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_DOUBLE_EQ(first->direction->rightAscensionDegrees, 11.0);
    EXPECT_DOUBLE_EQ(second->direction->rightAscensionDegrees, 22.0);
}

// ---- The documents and the coordinate boundary.

// A second-generation document written from a star list and read back gives the same stars at the same positions and
// the same enrichment: nothing shifts by half a pixel either way, and reading twice converts nothing twice.
TEST_F(StarProfilerUnitsTest, ASecondGenerationDocumentRoundTripsWithoutAShift)
{
    an::DetectedStar first = starAt(100.5, 60.5, 3.2, 2.9, 0.42);
    first.psf.beta = 3.1;
    first.psf.model = an::PsfModel::MoffatFreeBeta;
    const an::ui::StarList stars{first, starAt(7.25, 199.75, 2.5, 2.5, 0.3)};
    profiler::StarEnrichmentTable enrichment;
    enrichment[profiler::starPositionKey(stars[0])] = profiler::StarEnrichment{
        .direction = an::SkyCoordinate{.rightAscensionDegrees = 83.6, .declinationDegrees = 22.1},
        .match = profiler::StarCatalogMatch{
            .identifier = "1001", .magnitudeG = 9.5, .colorIndexBpMinusRp = 0.6, .separationArcseconds = 0.4}};
    const an::StarProfileV2 header = profiler::secondProfileHeader(
        profiler::SecondProfilerSettings{}, an::StarFieldSummary{}, profiler::ProfileCatalogFigures{}, 1u);
    const an::StarProfileFile document = profiler::profileDocument(header, stars, enrichment, true);
    ASSERT_EQ(document.generation, an::StarProfileGeneration::Version2);

    // A Gaussian's exponent is written as minus one, as the format states it.
    const auto& payload = std::get<an::StarProfileV2>(document.payload);
    ASSERT_EQ(payload.stars.size(), 2u);
    EXPECT_DOUBLE_EQ(payload.stars[0].psf.beta, 3.1);
    EXPECT_DOUBLE_EQ(payload.stars[1].psf.beta, -1.0);

    const QString path = temporaryPath(QStringLiteral("round-trip.anspv2"));
    underScope(session(), [&](const an::ui::ExecutionScope& scope) {
        const an::Result<an::StarProfileWriteReport> written =
            an::writeStarProfileFile(scope.engine(), scope.execution(),
                                     an::StarProfileWriteRequest{.destinationPath = path.toStdString()}, document);
        ASSERT_TRUE(written.hasValue()) << written.status().message();
        for (int pass = 0; pass < 2; ++pass)
        {
            const an::Result<an::StarProfileFile> read = an::readStarProfileFile(scope.execution(), path.toStdString());
            ASSERT_TRUE(read.hasValue()) << read.status().message();
            const profiler::LoadedProfile loaded = profiler::loadedProfile(*read);
            ASSERT_EQ(loaded.stars.size(), 2u);
            EXPECT_DOUBLE_EQ(loaded.stars[0].positionX, 100.5);
            EXPECT_DOUBLE_EQ(loaded.stars[0].positionY, 60.5);
            EXPECT_DOUBLE_EQ(loaded.stars[1].positionX, 7.25);
            EXPECT_DOUBLE_EQ(loaded.stars[1].positionY, 199.75);
            EXPECT_DOUBLE_EQ(loaded.stars[0].psf.fullWidthX, 3.2);
            EXPECT_EQ(loaded.stars[0].psf.model, an::PsfModel::MoffatFreeBeta);
            ASSERT_TRUE(loaded.stars[0].psf.beta.has_value());
            EXPECT_DOUBLE_EQ(*loaded.stars[0].psf.beta, 3.1);
            EXPECT_FALSE(loaded.stars[1].psf.beta.has_value()) << "a Gaussian's exponent is read as none";
            const profiler::StarEnrichment* recorded = profiler::enrichmentOf(loaded.enrichment, loaded.stars[0]);
            ASSERT_NE(recorded, nullptr);
            ASSERT_TRUE(recorded->direction.has_value());
            EXPECT_DOUBLE_EQ(recorded->direction->rightAscensionDegrees, 83.6);
            ASSERT_TRUE(recorded->match.has_value());
            EXPECT_EQ(recorded->match->identifier, "1001");
            EXPECT_EQ(profiler::enrichmentOf(loaded.enrichment, loaded.stars[1]), nullptr);
        }
    });
}

// A second-generation document - a star fitted at a pixel's centre stands at its whole coordinate plus one half,
// because a fit reads every sample at its pixel's centre - is read where it stands, which is the engine's convention:
// the reader adds no half pixel.
TEST(StarProfilerDocumentTest, ASecondGenerationDocumentIsReadInTheEnginesConvention)
{
    an::StarProfileV2 payload;
    an::StarProfileV2Star star;
    star.position = {.x = 100.5, .y = 60.5};
    star.psf.fullWidthX = 3.0;
    star.psf.fullWidthY = 3.0;
    star.astrometry.channel = 0;
    payload.stars.push_back(star);
    const an::StarProfileFile document{.generation = an::StarProfileGeneration::Version2, .payload = payload};
    const profiler::LoadedProfile loaded = profiler::loadedProfile(document);
    ASSERT_EQ(loaded.stars.size(), 1u);
    EXPECT_DOUBLE_EQ(loaded.stars[0].positionX, 100.5);
    EXPECT_DOUBLE_EQ(loaded.stars[0].positionY, 60.5);
    // Written again, it stands where it stood.
    const an::StarProfileFile again = profiler::profileDocument(loaded.header, loaded.stars, loaded.enrichment, false);
    const auto& written = std::get<an::StarProfileV2>(again.payload);
    ASSERT_EQ(written.stars.size(), 1u);
    EXPECT_DOUBLE_EQ(written.stars[0].position.x, 100.5);
    EXPECT_DOUBLE_EQ(written.stars[0].position.y, 60.5);
}

// A first-generation document carries what the first generation holds: the position as it is, the amplitude as the
// intensity, and a radius the profile's readers - the profiled recombination among them - paint the star at its own
// full width with.
TEST(StarProfilerDocumentTest, AFirstGenerationStarIsReadBackAtItsPositionAndWidth)
{
    const an::DetectedStar star = starAt(31.5, 44.5, 3.0, 3.0, 0.35);
    const an::StarProfileV1Star record = profiler::firstProfileStar(star);
    EXPECT_DOUBLE_EQ(record.x, 31.5);
    EXPECT_DOUBLE_EQ(record.y, 44.5);
    EXPECT_DOUBLE_EQ(record.intensity, 0.35);
    an::StarProfileV1 header = profiler::firstProfileHeader(profiler::FirstProfilerSettings{.detectionThreshold = 0.15},
                                                            an::StarFieldSummary{});
    EXPECT_DOUBLE_EQ(header.parameters.detectionThreshold, 0.15);
    const an::StarProfileFile document = profiler::profileDocument(header, an::ui::StarList{star}, {}, false);
    ASSERT_EQ(document.generation, an::StarProfileGeneration::Version1);
    const an::ui::StarList read = process_utils::starsOfProfile(document);
    ASSERT_EQ(read.size(), 1u);
    EXPECT_DOUBLE_EQ(read[0].positionX, 31.5);
    EXPECT_DOUBLE_EQ(read[0].positionY, 44.5);
    EXPECT_DOUBLE_EQ(read[0].psf.fullWidthX, 3.0) << "the star is read back with the width it was written with";
    EXPECT_DOUBLE_EQ(read[0].psf.amplitude, 0.35);
}

// ---- The detector.

// The one detector finds every planted star of a field at its engine pixel coordinate, its amplitude a fraction of
// full scale, with both layouts' requests.
TEST_F(StarProfilerUnitsTest, TheDetectorFindsEveryPlantedStarWhereItIs)
{
    const StarField field = makeStarField();
    ASSERT_GT(field.stars.size(), 15u);
    std::optional<an::consumer::ImageAsset> asset = fieldAsset<float>(session(), field, fieldMetadata(field));
    ASSERT_TRUE(asset.has_value());
    an::Result<an::consumer::AssetReadLease> lease = asset->read();
    ASSERT_TRUE(lease.hasValue());
    underScope(session(), [&](const an::ui::ExecutionScope& scope) {
        for (const an::StarDetectionRequest& request :
             {profiler::secondProfilerDetection(profiler::SecondProfilerSettings{}),
              profiler::firstProfilerDetection(profiler::FirstProfilerSettings{})})
        {
            an::Result<profiler::ProfileDetection> detection =
                profiler::detectProfileStars(scope, an::ui::ProcessSource::of(*lease), request, false);
            ASSERT_TRUE(detection.hasValue()) << detection.status().message();
            EXPECT_EQ(plantedStarsFound(field, detection->stars, 0.1), field.stars.size());
            for (const an::DetectedStar& star : detection->stars)
            {
                const NearestStar nearest = nearestPlanted(field, star.positionX, star.positionY);
                ASSERT_NE(nearest.star, nullptr);
                EXPECT_LT(nearest.distance, 0.1) << "a detection away from every planted star";
                EXPECT_DOUBLE_EQ(star.psf.saturationLevel, 1.0) << "full scale is one: the image is read in fractions";
                EXPECT_NEAR(star.psf.amplitude, nearest.star->amplitude, 0.03);
                EXPECT_NEAR(star.psf.fullWidthX, nearest.star->sigma * an::kGaussianFullWidthPerSigma, 0.25);
            }
        }
    });
}

// A sample that holds no number is left out of the search: a field with some of its sky gone to NaN and infinity gives
// the stars it gave without them.
TEST_F(StarProfilerUnitsTest, ASampleThatHoldsNoNumberIsLeftOut)
{
    StarField field = makeStarField();
    // What the detector finds in the field before any sample is spoiled.
    std::vector<an::DetectedStar> unspoiled;
    {
        std::optional<an::consumer::ImageAsset> clean = fieldAsset<float>(session(), field, fieldMetadata(field));
        ASSERT_TRUE(clean.has_value());
        an::Result<an::consumer::AssetReadLease> lease = clean->read();
        ASSERT_TRUE(lease.hasValue());
        underScope(session(), [&](const an::ui::ExecutionScope& scope) {
            an::Result<profiler::ProfileDetection> detection = profiler::detectProfileStars(
                scope, an::ui::ProcessSource::of(*lease), profiler::secondProfilerDetection({}), false);
            ASSERT_TRUE(detection.hasValue()) << detection.status().message();
            unspoiled.assign(detection->stars.begin(), detection->stars.end());
        });
    }
    ASSERT_FALSE(unspoiled.empty());
    std::size_t spoiled = 0u;
    for (std::uint32_t y = 12u; y + 12u < field.height; y += 9u)
    {
        for (std::uint32_t x = 12u; x + 12u < field.width; x += 11u)
        {
            if (nearestPlanted(field, x + 0.5, y + 0.5).distance > 14.0)
            {
                field.samples[static_cast<std::size_t>(y) * field.width + x] =
                    (spoiled % 2u == 0u) ? std::numeric_limits<float>::quiet_NaN()
                                         : std::numeric_limits<float>::infinity();
                ++spoiled;
            }
        }
    }
    ASSERT_GT(spoiled, 20u);
    std::optional<an::consumer::ImageAsset> asset = fieldAsset<float>(session(), field, fieldMetadata(field));
    ASSERT_TRUE(asset.has_value());
    an::Result<an::consumer::AssetReadLease> lease = asset->read();
    ASSERT_TRUE(lease.hasValue());
    underScope(session(), [&](const an::ui::ExecutionScope& scope) {
        an::Result<profiler::ProfileDetection> detection = profiler::detectProfileStars(
            scope, an::ui::ProcessSource::of(*lease), profiler::secondProfilerDetection({}), false);
        ASSERT_TRUE(detection.hasValue()) << detection.status().message();
        EXPECT_EQ(plantedStarsFound(field, detection->stars, 0.1), field.stars.size());
        // Nothing is found at a spoiled sample, and nothing found without them is lost or moved.
        ASSERT_EQ(detection->stars.size(), unspoiled.size());
        for (const an::DetectedStar& star : unspoiled)
        {
            EXPECT_TRUE(std::any_of(detection->stars.begin(), detection->stars.end(),
                                    [&star](const an::DetectedStar& found) {
                                        return std::hypot(found.positionX - star.positionX,
                                                          found.positionY - star.positionY) <= 1e-6;
                                    }))
                << star.positionX << "," << star.positionY;
        }
    });
}

// ---- The enrichment.

// Every star is placed on the sky where the solution projects its engine pixel coordinate - not half a pixel off, as a
// projection of a half-centred position as an integer-centred one would be - and each matches its Gaia entry one to
// one; the matched box sums fit a zero point and the faintest match is the limiting magnitude.
TEST_F(StarProfilerUnitsTest, EveryStarIsPlacedWhereTheSolutionProjectsItAndMatchesItsEntry)
{
    const StarField field = makeStarField();
    ASSERT_TRUE(field.solution.has_value());
    InstalledCatalog catalog(context());
    ASSERT_TRUE(catalog.install(field));
    an::ui::StarList stars;
    for (const PlantedStar& planted : field.stars)
    {
        stars.push_back(starAt(planted.x, planted.y, 3.5, 3.5, planted.amplitude));
    }
    underScope(session(), [&](const an::ui::ExecutionScope& scope) {
        const an::Result<profiler::ProfileEnrichment> enrichment =
            profiler::enrichProfileStars(scope.execution(), stars, *field.solution,
                                         profiler::ProfileEnrichmentRequest{.catalogDirectory = catalog.directory()});
        ASSERT_TRUE(enrichment.hasValue()) << enrichment.status().message();
        EXPECT_FALSE(enrichment->catalogFailure.has_value())
            << (enrichment->catalogFailure.has_value() ? enrichment->catalogFailure->message() : std::string());
        ASSERT_EQ(enrichment->stars.size(), stars.size());
        EXPECT_EQ(enrichment->placedCount, stars.size());
        EXPECT_EQ(enrichment->figures.matchedCount, stars.size());
        for (std::size_t index = 0u; index < stars.size(); ++index)
        {
            const profiler::StarEnrichment& star = enrichment->stars[index];
            ASSERT_TRUE(star.direction.has_value());
            EXPECT_NEAR(star.direction->rightAscensionDegrees, field.stars[index].direction.rightAscensionDegrees,
                        1.0e-9);
            EXPECT_NEAR(star.direction->declinationDegrees, field.stars[index].direction.declinationDegrees, 1.0e-9);
            // Half a pixel is 1.2 arcseconds on this sky; the projection is exact.
            ASSERT_TRUE(star.match.has_value());
            EXPECT_EQ(star.match->identifier, an::makeGaiaSourceDesignation(field.stars[index].sourceIdentifier));
            EXPECT_LT(star.match->separationArcseconds, 0.01);
            ASSERT_TRUE(star.match->colorIndexBpMinusRp.has_value());
            EXPECT_NEAR(*star.match->colorIndexBpMinusRp, 0.6, 1.0e-3);
            EXPECT_NEAR(star.match->magnitudeG, field.stars[index].magnitude, 1.0e-4);
        }
        ASSERT_TRUE(enrichment->figures.limitingMagnitude.has_value());
        EXPECT_NEAR(*enrichment->figures.limitingMagnitude, field.stars.back().magnitude, 1.0e-4);
        EXPECT_TRUE(enrichment->figures.zeroPointG.has_value());
    });
}

// A star matches an entry only within the request's radius: stars three pixels (7.2 arcseconds) from every entry match
// nothing at five arcseconds and match all at ten.
TEST_F(StarProfilerUnitsTest, AStarMatchesOnlyWithinTheRequestsRadius)
{
    const StarField field = makeStarField();
    ASSERT_TRUE(field.solution.has_value());
    InstalledCatalog catalog(context());
    ASSERT_TRUE(catalog.install(field));
    an::ui::StarList stars;
    for (const PlantedStar& planted : field.stars)
    {
        stars.push_back(starAt(planted.x + 3.0, planted.y, 3.5, 3.5, planted.amplitude));
    }
    underScope(session(), [&](const an::ui::ExecutionScope& scope) {
        const auto enrich = [&](double radius) {
            return profiler::enrichProfileStars(
                scope.execution(), stars, *field.solution,
                profiler::ProfileEnrichmentRequest{.catalogDirectory = catalog.directory(),
                                                   .matchRadiusArcseconds = radius});
        };
        const an::Result<profiler::ProfileEnrichment> near = enrich(5.0);
        ASSERT_TRUE(near.hasValue()) << near.status().message();
        EXPECT_FALSE(near->catalogFailure.has_value());
        EXPECT_EQ(near->figures.matchedCount, 0u);
        const an::Result<profiler::ProfileEnrichment> wide = enrich(10.0);
        ASSERT_TRUE(wide.hasValue()) << wide.status().message();
        EXPECT_EQ(wide->figures.matchedCount, stars.size());
    });
}

// Without a catalog the stars are still placed on the sky and match nothing; a catalog that cannot be read is said by
// the enrichment's failure, not by refusing the stars.
TEST_F(StarProfilerUnitsTest, WithoutACatalogTheStarsAreStillPlaced)
{
    const StarField field = makeStarField();
    ASSERT_TRUE(field.solution.has_value());
    const an::ui::StarList stars{starAt(field.stars[0].x, field.stars[0].y, 3.5, 3.5)};
    underScope(session(), [&](const an::ui::ExecutionScope& scope) {
        const an::Result<profiler::ProfileEnrichment> none =
            profiler::enrichProfileStars(scope.execution(), stars, *field.solution, {});
        ASSERT_TRUE(none.hasValue());
        EXPECT_EQ(none->placedCount, 1u);
        EXPECT_FALSE(none->stars[0].match.has_value());
        EXPECT_FALSE(none->catalogFailure.has_value());

        const an::Result<profiler::ProfileEnrichment> unreadable = profiler::enrichProfileStars(
            scope.execution(), stars, *field.solution,
            profiler::ProfileEnrichmentRequest{.catalogDirectory = temporaryPath(QStringLiteral("no-catalog"))});
        ASSERT_TRUE(unreadable.hasValue());
        EXPECT_EQ(unreadable->placedCount, 1u);
        EXPECT_TRUE(unreadable->catalogFailure.has_value());
    });
}

// ---- Documents written outside the windows.

/// A star as a document states it, read with Qt's JSON reader rather than the engine's.
struct WrittenStar
{
    double x = 0.0;
    double y = 0.0;
    QJsonObject record;
};

/// The stars of a profile file, as written: a first-generation star's `x` and `y`, a second-generation star's `xpos`
/// and `ypos`.
[[nodiscard]] std::vector<WrittenStar> writtenStars(const QString& path)
{
    QFile file(path);
    EXPECT_TRUE(file.open(QIODevice::ReadOnly));
    const QJsonObject profile =
        QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("stars_profile")).toObject();
    const bool first = profile.value(QStringLiteral("engine")).toString() == QStringLiteral("v1");
    std::vector<WrittenStar> stars;
    for (const auto& value : profile.value(QStringLiteral("stars")).toArray())
    {
        const QJsonObject record = value.toObject();
        stars.push_back(WrittenStar{.x = record.value(first ? QStringLiteral("x") : QStringLiteral("xpos")).toDouble(),
                                    .y = record.value(first ? QStringLiteral("y") : QStringLiteral("ypos")).toDouble(),
                                    .record = record});
    }
    return stars;
}

/// Where a field's stars were planted, in engine pixel coordinates.
[[nodiscard]] std::vector<std::pair<double, double>> plantedPositions(const StarField& field)
{
    std::vector<std::pair<double, double>> stars;
    for (const PlantedStar& star : field.stars)
    {
        stars.emplace_back(star.x, star.y);
    }
    return stars;
}

/// How a star list sits on the planted stars: of the stars within a distance of one, how many, their median distance
/// and their mean displacement along each axis.
struct PlantedAgreement
{
    std::size_t matched = 0u;
    double medianDistance = std::numeric_limits<double>::infinity();
    double meanX = 0.0;
    double meanY = 0.0;
};

[[nodiscard]] PlantedAgreement agreementWithPlanted(const an::ui::StarList& stars,
                                                    const std::vector<std::pair<double, double>>& planted,
                                                    double within)
{
    std::vector<double> distances;
    PlantedAgreement agreement;
    for (const an::DetectedStar& star : stars)
    {
        double best = std::numeric_limits<double>::infinity();
        std::pair<double, double> nearest{};
        for (const auto& candidate : planted)
        {
            const double distance = std::hypot(star.positionX - candidate.first, star.positionY - candidate.second);
            if (distance < best)
            {
                best = distance;
                nearest = candidate;
            }
        }
        if (best <= within)
        {
            distances.push_back(best);
            agreement.meanX += star.positionX - nearest.first;
            agreement.meanY += star.positionY - nearest.second;
        }
    }
    agreement.matched = distances.size();
    if (!distances.empty())
    {
        std::sort(distances.begin(), distances.end());
        agreement.medianDistance = distances[distances.size() / 2u];
        agreement.meanX /= static_cast<double>(distances.size());
        agreement.meanY /= static_cast<double>(distances.size());
    }
    return agreement;
}

// A first profiler's document written by the engine's writer over a field's planted stars is read by the readers the
// product uses - the window's and the profiled recombination's - with every star where the document puts it: the
// importer's declared conversion is none, so nothing moves, the second read is the first, and the stars stand on the
// planted ones, which they would miss by half a pixel had this reader shifted them.
TEST_F(StarProfilerUnitsTest, AFirstProfilersDocumentIsReadWhereItsStarsStand)
{
    const StarField field = makeStarField();
    const QString path =
        writtenProfile(QStringLiteral("planted.anspv1"), plantedProfile(an::StarProfileGeneration::Version1, field));
    ASSERT_FALSE(path.isEmpty()) << "the profile could not be written";
    const std::vector<WrittenStar> written = writtenStars(path);
    ASSERT_EQ(written.size(), field.stars.size());
    ASSERT_FALSE(written.empty());

    underScope(session(), [&](const an::ui::ExecutionScope& scope) {
        for (int pass = 0; pass < 2; ++pass)
        {
            const an::Result<an::StarProfileFile> read = an::readStarProfileFile(scope.execution(), path.toStdString());
            ASSERT_TRUE(read.hasValue()) << read.status().message();
            ASSERT_EQ(read->generation, an::StarProfileGeneration::Version1);
            const auto& payload = std::get<an::StarProfileV1>(read->payload);
            EXPECT_DOUBLE_EQ(payload.parameters.detectionThreshold, 0.1);
            EXPECT_DOUBLE_EQ(payload.parameters.roundnessTolerance, 2.0);
            EXPECT_DOUBLE_EQ(payload.parameters.minimumSize, 2.0);
            EXPECT_DOUBLE_EQ(payload.parameters.maximumSize, 25.0);
            EXPECT_FALSE(payload.manuallyEdited);

            const profiler::LoadedProfile loaded = profiler::loadedProfile(*read);
            const an::Result<an::ui::StarList> recombination =
                process_utils::readProfileStars(scope.execution(), path.toStdString());
            ASSERT_TRUE(recombination.hasValue()) << recombination.status().message();
            for (const an::ui::StarList* stars :
                 std::array<const an::ui::StarList*, 2u>{&loaded.stars, &*recombination})
            {
                ASSERT_EQ(stars->size(), written.size());
                for (std::size_t index = 0u; index < written.size(); ++index)
                {
                    const an::DetectedStar& star = (*stars)[index];
                    EXPECT_DOUBLE_EQ(star.positionX, written[index].x) << index;
                    EXPECT_DOUBLE_EQ(star.positionY, written[index].y) << index;
                    EXPECT_DOUBLE_EQ(star.psf.amplitude,
                                     written[index].record.value(QStringLiteral("intensity")).toDouble());
                    // A first-generation star is painted at its radius, four full widths across.
                    EXPECT_DOUBLE_EQ(star.psf.fullWidthX,
                                     written[index].record.value(QStringLiteral("radius")).toDouble() / 2.0);
                }
            }

            const PlantedAgreement agreement = agreementWithPlanted(loaded.stars, plantedPositions(field), 1.0);
            EXPECT_EQ(agreement.matched, field.stars.size());
            EXPECT_LT(agreement.medianDistance, 1.0e-9);
            EXPECT_LT(std::abs(agreement.meanX), 1.0e-9) << "the stars sit off the planted ones";
            EXPECT_LT(std::abs(agreement.meanY), 1.0e-9) << "the stars sit off the planted ones";
        }
    });
}

// A second profiler's document written by the engine's writer over a field's planted stars is read field for field by
// the same readers, every fitted position as the document states it: a fit samples each pixel at its centre, so its
// positions are the engine's convention already, and a read that added half a pixel would put no star within a
// hundredth of a pixel of its planted position.
TEST_F(StarProfilerUnitsTest, ASecondProfilersDocumentIsReadWhereItsStarsStand)
{
    const StarField field = makeStarField();
    const QString path =
        writtenProfile(QStringLiteral("planted.anspv2"), plantedProfile(an::StarProfileGeneration::Version2, field));
    ASSERT_FALSE(path.isEmpty()) << "the profile could not be written";
    const std::vector<WrittenStar> written = writtenStars(path);
    ASSERT_EQ(written.size(), field.stars.size());
    ASSERT_FALSE(written.empty());

    underScope(session(), [&](const an::ui::ExecutionScope& scope) {
        for (int pass = 0; pass < 2; ++pass)
        {
            const an::Result<an::StarProfileFile> read = an::readStarProfileFile(scope.execution(), path.toStdString());
            ASSERT_TRUE(read.hasValue()) << read.status().message();
            ASSERT_EQ(read->generation, an::StarProfileGeneration::Version2);
            const auto& payload = std::get<an::StarProfileV2>(read->payload);
            EXPECT_EQ(payload.parameters.radius, 5);
            EXPECT_DOUBLE_EQ(payload.parameters.sigma, 1.0);
            EXPECT_DOUBLE_EQ(payload.parameters.roundness, 0.5);
            EXPECT_EQ(payload.parameters.convergence, 1);
            EXPECT_EQ(payload.parameters.profile, an::PersistedPsfModel::Gaussian);
            EXPECT_DOUBLE_EQ(payload.parameters.minimumBeta, 1.5);
            EXPECT_EQ(payload.global.channelCount, 1u);

            const profiler::LoadedProfile loaded = profiler::loadedProfile(*read);
            const an::Result<an::ui::StarList> recombination =
                process_utils::readProfileStars(scope.execution(), path.toStdString());
            ASSERT_TRUE(recombination.hasValue()) << recombination.status().message();
            for (const an::ui::StarList* stars :
                 std::array<const an::ui::StarList*, 2u>{&loaded.stars, &*recombination})
            {
                ASSERT_EQ(stars->size(), written.size());
                for (std::size_t index = 0u; index < written.size(); ++index)
                {
                    const an::DetectedStar& star = (*stars)[index];
                    const QJsonObject& record = written[index].record;
                    EXPECT_DOUBLE_EQ(star.positionX, written[index].x) << index;
                    EXPECT_DOUBLE_EQ(star.positionY, written[index].y) << index;
                    EXPECT_DOUBLE_EQ(star.psf.centreX, record.value(QStringLiteral("x0")).toDouble());
                    EXPECT_DOUBLE_EQ(star.psf.centreY, record.value(QStringLiteral("y0")).toDouble());
                    EXPECT_DOUBLE_EQ(star.psf.amplitude, record.value(QStringLiteral("A")).toDouble());
                    EXPECT_DOUBLE_EQ(star.psf.background, record.value(QStringLiteral("B")).toDouble());
                    EXPECT_DOUBLE_EQ(star.psf.fullWidthX, record.value(QStringLiteral("fwhmx")).toDouble());
                    EXPECT_DOUBLE_EQ(star.psf.fullWidthY, record.value(QStringLiteral("fwhmy")).toDouble());
                    EXPECT_DOUBLE_EQ(star.psf.angleDegrees, record.value(QStringLiteral("angle")).toDouble());
                    EXPECT_EQ(star.psf.boxRadius,
                              static_cast<std::uint32_t>(record.value(QStringLiteral("R")).toInt()));
                    EXPECT_EQ(star.psf.saturated, record.value(QStringLiteral("has_saturated")).toBool());
                    EXPECT_EQ(star.psf.model, an::PsfModel::Gaussian);
                    EXPECT_FALSE(star.psf.beta.has_value()) << "a Gaussian's exponent, written as minus one, is none";
                }
            }

            const PlantedAgreement agreement = agreementWithPlanted(loaded.stars, plantedPositions(field), 0.01);
            EXPECT_EQ(agreement.matched, field.stars.size()) << "the stars sit off the planted ones";
            EXPECT_LT(std::abs(agreement.meanX), 1.0e-9);
            EXPECT_LT(std::abs(agreement.meanY), 1.0e-9);
        }
    });
}

} // namespace
