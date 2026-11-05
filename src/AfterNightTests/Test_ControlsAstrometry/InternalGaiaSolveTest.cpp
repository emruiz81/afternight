// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The internal plate solve end to end: an installed Gaia chunk catalog written as the downloader installs one, read
/// for regional catalogs by `GaiaCatalogSource`, and a rendered star field solved against it through the service - the
/// image read in the session, its stars detected and matched by the engine's plate solver.

#include "Test_ControlsAstrometry/AstrometryTestSupport.h"
#include "Test_ControlsAstrometry/SolvableField.h"

#include "app/CancellationToken.h"
#include "app/GaiaCatalogSettings.h"
#include "app/GaiaCatalogSource.h"
#include "app/PlateSolveService.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/Catalog/CatalogRecord.h>

#include <QDir>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace
{

using an::tests::astrometry::InstalledContext;
using an::tests::astrometry::metadataWithCards;
using an::tests::astrometry::MockSolverProcessRunner;
using an::tests::astrometry::openViewerContext;
using an::tests::astrometry::ViewerTestContext;
using an::tests::astrometry::writeFileBytes;
using an::tests::astrometry::field::catalogStars;
using an::tests::astrometry::field::kCenter;
using an::tests::astrometry::field::kHeight;
using an::tests::astrometry::field::kScaleArcsec;
using an::tests::astrometry::field::kStars;
using an::tests::astrometry::field::kWidth;
using an::tests::astrometry::field::makeFieldAsset;
using an::tests::astrometry::field::truthSolution;
using an::tests::astrometry::field::writeGaiaCatalog;

/// The internal solver's cases: an installed context, a directory holding the catalog and the image.
class InternalGaiaSolveTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_directory.isValid());
        m_catalog = QDir(m_directory.path()).filePath(QStringLiteral("gaia_dr3_g14"));
        ASSERT_TRUE(QDir().mkpath(m_catalog));
        ASSERT_TRUE(writeGaiaCatalog(m_catalog, catalogStars()));
        m_context = openViewerContext();
        ASSERT_NE(m_context, nullptr);
        m_installed.emplace(*m_context);
    }

    void TearDown() override
    {
        m_installed.reset();
        m_context.reset();
    }

    /// An admitted operation of the context's session.
    [[nodiscard]] std::optional<an::consumer::OperationScope> admitted() const
    {
        return an::tests::viewer::admitted(m_context->engineSession());
    }

    QTemporaryDir m_directory;
    QString m_catalog;
    std::unique_ptr<ViewerTestContext> m_context;
    std::optional<InstalledContext> m_installed;
};

} // namespace

TEST_F(InternalGaiaSolveTest, TheInstalledCatalogIsReady)
{
    EXPECT_EQ(an::ui::inspectGaiaCatalogDirectory(m_catalog), an::ui::GaiaCatalogDirectoryStatus::Ready);
}

TEST_F(InternalGaiaSolveTest, ARegionHoldsTheStarsInsideItsConeAndNoneOutside)
{
    std::optional<an::consumer::OperationScope> scope = admitted();
    ASSERT_TRUE(scope.has_value());
    an::Result<an::ui::GaiaCatalogSource> source = an::ui::GaiaCatalogSource::open(scope->execution(), m_catalog);
    ASSERT_TRUE(source.hasValue()) << source.status().message();
    EXPECT_TRUE(source->valid());
    EXPECT_DOUBLE_EQ(source->metadata().magnitudeLimit, 14.0);

    const an::Result<an::SkyCatalog> region =
        source->regionCatalog(scope->execution(), kCenter, 0.5, std::nullopt, std::nullopt);
    ASSERT_TRUE(region.hasValue()) << region.status().message();
    EXPECT_EQ(region->entryCount(), kStars);
    for (const an::CatalogEntry& entry : region->entries())
    {
        EXPECT_LE(an::angularSeparationDegrees(kCenter, entry.coordinate), 0.5);
    }
}

TEST_F(InternalGaiaSolveTest, AMagnitudeLimitKeepsTheBrighterStars)
{
    std::optional<an::consumer::OperationScope> scope = admitted();
    ASSERT_TRUE(scope.has_value());
    an::Result<an::ui::GaiaCatalogSource> source = an::ui::GaiaCatalogSource::open(scope->execution(), m_catalog);
    ASSERT_TRUE(source.hasValue()) << source.status().message();

    // The magnitudes run from 8.0 in steps of 0.12, so 9.0 keeps the first nine.
    const an::Result<an::SkyCatalog> region =
        source->regionCatalog(scope->execution(), kCenter, 0.5, std::nullopt, 9.0);
    ASSERT_TRUE(region.hasValue()) << region.status().message();
    EXPECT_EQ(region->entryCount(), 9u);
}

TEST_F(InternalGaiaSolveTest, AnEmptyConeIsAnEmptySelection)
{
    std::optional<an::consumer::OperationScope> scope = admitted();
    ASSERT_TRUE(scope.has_value());
    an::Result<an::ui::GaiaCatalogSource> source = an::ui::GaiaCatalogSource::open(scope->execution(), m_catalog);
    ASSERT_TRUE(source.hasValue()) << source.status().message();

    const an::Result<an::SkyCatalog> region = source->regionCatalog(
        scope->execution(), an::SkyCoordinate{.rightAscensionDegrees = 300.0, .declinationDegrees = 0.0}, 0.5,
        std::nullopt, std::nullopt);
    ASSERT_FALSE(region.hasValue());
    EXPECT_EQ(region.status().code(), an::ErrorCode::EmptySelection);
}

TEST_F(InternalGaiaSolveTest, ATileIsDecodedOnceAcrossTheRegionsThatReachIt)
{
    std::optional<an::consumer::OperationScope> scope = admitted();
    ASSERT_TRUE(scope.has_value());
    an::Result<an::ui::GaiaCatalogSource> source = an::ui::GaiaCatalogSource::open(scope->execution(), m_catalog);
    ASSERT_TRUE(source.hasValue()) << source.status().message();

    ASSERT_TRUE(source->regionCatalog(scope->execution(), kCenter, 0.5, std::nullopt, std::nullopt).hasValue());
    const std::uint64_t first = source->tilesDecoded();
    EXPECT_GT(first, 0u);
    ASSERT_TRUE(source->regionCatalog(scope->execution(), kCenter, 0.4, std::nullopt, std::nullopt).hasValue());
    EXPECT_EQ(source->tilesDecoded(), first);
}

TEST_F(InternalGaiaSolveTest, ACatalogStoredAsTileFilesIsRefused)
{
    const QString tiles = QDir(m_directory.path()).filePath(QStringLiteral("tiles"));
    ASSERT_TRUE(an::tests::astrometry::writeGaiaCatalogMeta(
        tiles, QJsonObject{{QStringLiteral("format_version"), 2},
                           {QStringLiteral("storage"), QStringLiteral("tiles")},
                           {QStringLiteral("catalog_id"), QStringLiteral("gaia_dr3")},
                           {QStringLiteral("tile_directory"), QStringLiteral("healpix_order5")}}));

    std::optional<an::consumer::OperationScope> scope = admitted();
    ASSERT_TRUE(scope.has_value());
    const an::Result<an::ui::GaiaCatalogSource> source = an::ui::GaiaCatalogSource::open(scope->execution(), tiles);
    ASSERT_FALSE(source.hasValue());
    EXPECT_NE(source.status().code(), an::ErrorCode::Internal);
}

TEST_F(InternalGaiaSolveTest, AMissingManifestIsAnExternalFailureNamingIt)
{
    std::optional<an::consumer::OperationScope> scope = admitted();
    ASSERT_TRUE(scope.has_value());
    const an::Result<an::ui::GaiaCatalogSource> source = an::ui::GaiaCatalogSource::open(
        scope->execution(), QDir(m_directory.path()).filePath(QStringLiteral("nowhere")));
    ASSERT_FALSE(source.hasValue());
    EXPECT_EQ(source.status().code(), an::ErrorCode::ExternalFailure);
    EXPECT_NE(std::string(source.status().message()).find("catalog_meta.json"), std::string::npos);
}

TEST_F(InternalGaiaSolveTest, TheInternalSolverSolvesARenderedFieldAgainstTheInstalledCatalog)
{
    const an::ImageMetadata metadata = metadataWithCards(an::tests::astrometry::field::hintCards());
    std::optional<an::consumer::ImageAsset> field = makeFieldAsset(m_context->engineSession(), metadata);
    ASSERT_TRUE(field.has_value());
    const QString path = QDir(m_directory.path()).filePath(QStringLiteral("field.fits"));
    an::tests::viewer::writeFitsFile(m_context->engineSession(), *field, path);

    an::ui::PlateSolveRequest request;
    request.imagePath = path;
    request.existingMetadata = metadata;
    request.imageWidth = static_cast<int>(kWidth);
    request.imageHeight = static_cast<int>(kHeight);
    request.workDirectoryRoot = m_directory.path();
    request.ignoreEmbeddedWcs = true;

    an::ui::PlateSolveSettings settings;
    settings.defaultSolver = QStringLiteral("internal_gaia");
    settings.gaiaCatalogDirectory = m_catalog;
    settings.searchRadius = 1.0;
    settings.useHints = true;

    auto runner = std::make_unique<MockSolverProcessRunner>();
    auto* runner_ptr = runner.get();
    an::ui::PlateSolveService service(std::move(runner));
    an::ui::CancellationToken token;
    const an::ui::PlateSolveResult result = service.solve(request, settings, token);

    ASSERT_TRUE(result.succeeded) << result.errorMessage.toStdString();
    EXPECT_EQ(result.solverUsed, QStringLiteral("Internal Gaia"));
    EXPECT_TRUE(runner_ptr->invocations.empty());
    ASSERT_TRUE(result.solution.has_value());
    const an::WcsSolution truth = truthSolution();
    const an::Result<an::SkyCoordinate> solvedCenter = result.solution->fieldCenter();
    const an::Result<an::SkyCoordinate> trueCenter = truth.fieldCenter();
    ASSERT_TRUE(solvedCenter.hasValue());
    ASSERT_TRUE(trueCenter.hasValue());
    EXPECT_LT(an::angularSeparationDegrees(*solvedCenter, *trueCenter) * 3600.0, 2.0 * kScaleArcsec);
    EXPECT_NEAR(result.imageScaleArcsecPerPixel, kScaleArcsec, 0.01 * kScaleArcsec);
    EXPECT_NEAR(result.rotationDeg, truth.orientationDegrees(), 0.2);
    EXPECT_EQ(result.solution->imageWidth(), kWidth);
    EXPECT_EQ(result.solution->imageHeight(), kHeight);
}

TEST_F(InternalGaiaSolveTest, AStoppedSolveReportsItselfCancelled)
{
    const an::ImageMetadata metadata = metadataWithCards({});
    std::optional<an::consumer::ImageAsset> field = makeFieldAsset(m_context->engineSession(), metadata);
    ASSERT_TRUE(field.has_value());
    const QString path = QDir(m_directory.path()).filePath(QStringLiteral("blind.fits"));
    an::tests::viewer::writeFitsFile(m_context->engineSession(), *field, path);

    an::ui::PlateSolveRequest request;
    request.imagePath = path;
    request.existingMetadata = metadata;
    request.imageWidth = static_cast<int>(kWidth);
    request.imageHeight = static_cast<int>(kHeight);
    request.workDirectoryRoot = m_directory.path();

    an::ui::PlateSolveSettings settings;
    settings.defaultSolver = QStringLiteral("internal_gaia");
    settings.gaiaCatalogDirectory = m_catalog;

    an::ui::PlateSolveService service(std::make_unique<MockSolverProcessRunner>());
    an::ui::CancellationToken token;
    token.requestCancel();
    const an::ui::PlateSolveResult result = service.solve(request, settings, token);
    EXPECT_FALSE(result.succeeded);
    EXPECT_TRUE(result.cancelled);
}
