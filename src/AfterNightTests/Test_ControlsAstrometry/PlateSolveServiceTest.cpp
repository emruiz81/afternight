// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The plate solve's order of solvers, the hints it gives them and how it reads their answers: ASTAP and a local
/// astrometry.net run through a runner that answers as the case queues, an image's own solution returned unless a
/// solve is forced, and the settings that pick and configure the solvers. Every solve runs under an installed context
/// that lends the session.

#include "Test_ControlsAstrometry/AstrometryTestSupport.h"

#include "app/CancellationToken.h"
#include "app/GaiaCatalogSettings.h"
#include "app/ObservationEpochUtils.h"
#include "app/PlateSolveConfiguration.h"
#include "app/PlateSolveService.h"
#include "app/PlateSolveUtils.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <vector>

namespace
{

using an::tests::astrometry::addCard;
using an::tests::astrometry::InstalledContext;
using an::tests::astrometry::makeSolvedHeaderBytes;
using an::tests::astrometry::metadataWithCards;
using an::tests::astrometry::MockSolverProcessRunner;
using an::tests::astrometry::northUpPlane;
using an::tests::astrometry::openViewerContext;
using an::tests::astrometry::tangentPlaneCards;
using an::tests::astrometry::valueAfterFlag;
using an::tests::astrometry::ViewerTestContext;
using an::tests::astrometry::writeDummyInputFile;
using an::tests::astrometry::writeFileBytes;
using an::tests::astrometry::writeGaiaCatalogMeta;
using an::ui::CancellationToken;

/// Cards stating nothing about the sky.
[[nodiscard]] std::vector<an::CodecCard> plainCards()
{
    std::vector<an::CodecCard> cards;
    addCard(cards, "OBJECT", "'unit'");
    return cards;
}

/// Cards stating the target's position as a capture program writes it.
[[nodiscard]] std::vector<an::CodecCard> hintCards(const std::string& rightAscension, const std::string& declination)
{
    std::vector<an::CodecCard> cards = plainCards();
    addCard(cards, "OBJCTRA", rightAscension);
    addCard(cards, "OBJCTDEC", declination);
    return cards;
}

/// The plate solve's cases: a context lending a session, installed for the case, and a directory for the files a case
/// writes.
class PlateSolveServiceTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_directory.isValid());
        m_context = openViewerContext();
        ASSERT_NE(m_context, nullptr);
        m_installed.emplace(*m_context);
    }

    void TearDown() override
    {
        m_installed.reset();
        m_context.reset();
    }

    /// A stand-in image file in the case's directory; the solvers the runner answers never read it.
    [[nodiscard]] QString imageFile(const QString& name) const
    {
        const QString path = QDir(m_directory.path()).filePath(name);
        EXPECT_TRUE(writeFileBytes(path, QByteArray("dummy")));
        return path;
    }

    /// A stand-in solver executable in the case's directory.
    [[nodiscard]] QString solverFile(const QString& name) const
    {
        return writeDummyInputFile(m_directory, name);
    }

    /// A request about an image of a size that carries cards, its solver files kept in the case's directory.
    [[nodiscard]] an::ui::PlateSolveRequest request(const QString& name, std::vector<an::CodecCard> cards,
                                                    const int width = 640, const int height = 480) const
    {
        an::ui::PlateSolveRequest made;
        made.imagePath = imageFile(name);
        made.existingMetadata = metadataWithCards(std::move(cards));
        made.imageWidth = width;
        made.imageHeight = height;
        made.workDirectoryRoot = m_directory.path();
        made.requestTag = QFileInfo(name).completeBaseName();
        return made;
    }

    QTemporaryDir m_directory;
    std::unique_ptr<ViewerTestContext> m_context;
    std::optional<InstalledContext> m_installed;
};

} // namespace

TEST(PlateSolveUtils, CreatesDistinctStagingPathsInSharedRoot)
{
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());

    const QString first_path = an::ui::createUniquePlateSolveInputPath(temp_dir.path());
    const QString second_path = an::ui::createUniquePlateSolveInputPath(temp_dir.path());

    ASSERT_FALSE(first_path.isEmpty());
    ASSERT_FALSE(second_path.isEmpty());
    EXPECT_NE(first_path, second_path);
    EXPECT_EQ(QFileInfo(first_path).suffix(), QStringLiteral("fits"));
    EXPECT_EQ(QFileInfo(second_path).suffix(), QStringLiteral("fits"));
    EXPECT_EQ(QFileInfo(first_path).absolutePath(), QFileInfo(second_path).absolutePath());
    EXPECT_TRUE(QFileInfo(first_path).absoluteDir().exists());
}

TEST(PlateSolveUtils, RunGuardDistinguishesTimeoutFromExternalCancellation)
{
    an::ui::PlateSolveRunGuard expired(std::chrono::milliseconds(0));
    EXPECT_TRUE(expired.shouldStop(false));
    EXPECT_TRUE(expired.timedOut());

    an::ui::PlateSolveRunGuard cancelled(std::chrono::seconds(5));
    EXPECT_TRUE(cancelled.shouldStop(true));
    EXPECT_FALSE(cancelled.timedOut());
}

TEST(PlateSolveUtils, RunGuardObservesExpiredDeadlineWithoutPriorStopCheck)
{
    an::ui::PlateSolveRunGuard expired(std::chrono::milliseconds(0));

    EXPECT_TRUE(expired.timedOut());
}

TEST(GaiaCatalogSettings, UsesSelectedPackWhenDirectoryIsBlank)
{
    an::ui::PlateSolveSettings settings;
    settings.gaiaCatalogDirectory.clear();
    settings.gaiaCatalogPack = QStringLiteral("gaia_dr3_g14");

    const QString resolved = an::ui::resolveGaiaCatalogDirectory(settings);
    EXPECT_EQ(QFileInfo(resolved).fileName(), QStringLiteral("gaia_dr3_g14"));
}

TEST(PlateSolveSettings, NormalizesTheDefaultSolversSpellings)
{
    EXPECT_EQ(an::ui::normalizePlateSolveDefaultSolver(QStringLiteral("Internal Gaia")),
              QStringLiteral("internal_gaia"));
    EXPECT_EQ(an::ui::normalizePlateSolveDefaultSolver(QStringLiteral("internal-gaia")),
              QStringLiteral("internal_gaia"));
    EXPECT_EQ(an::ui::normalizePlateSolveDefaultSolver(QStringLiteral("Astrometry.net (local)")),
              QStringLiteral("astrometry_local"));
    EXPECT_EQ(an::ui::normalizePlateSolveDefaultSolver(QStringLiteral("Astrometry.net (web)")),
              QStringLiteral("astrometry_web"));
    EXPECT_EQ(an::ui::normalizePlateSolveDefaultSolver(QStringLiteral("ASTAP")), QStringLiteral("astap"));
    EXPECT_TRUE(an::ui::normalizePlateSolveDefaultSolver(QStringLiteral("")).isEmpty());
    EXPECT_TRUE(an::ui::normalizePlateSolveDefaultSolver(QStringLiteral("unknown")).isEmpty());
}

TEST(PlateSolveSettings, PicksOnlyConfiguredDefaultSolver)
{
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());

    an::ui::PlateSolveSettings settings;
    settings.defaultSolver = QStringLiteral("astap");
    settings.astrometryApiKey = QStringLiteral("api-key");

    EXPECT_EQ(an::ui::preferredConfiguredPlateSolveSolver(settings), QStringLiteral("astrometry_web"));

    settings.astapPath = writeDummyInputFile(temp_dir, QStringLiteral("astap_mock"));
    EXPECT_EQ(an::ui::preferredConfiguredPlateSolveSolver(settings), QStringLiteral("astap"));
}

TEST(GaiaCatalogSettings, OffersMultipleCatalogPacksAndKeepsG14Recommended)
{
    const QVector<an::ui::CatalogPackDescriptor> packs = an::ui::availableGaiaCatalogPacks();
    ASSERT_EQ(packs.size(), 4);
    EXPECT_EQ(packs.at(0).id, QStringLiteral("gaia_dr3_g12"));
    EXPECT_EQ(packs.at(1).id, QStringLiteral("gaia_dr3_g14"));
    EXPECT_EQ(packs.at(2).id, QStringLiteral("gaia_dr3_g16"));
    EXPECT_EQ(packs.at(3).id, QStringLiteral("gaia_dr3_g18"));
    for (const an::ui::CatalogPackDescriptor& pack : packs)
    {
        EXPECT_TRUE(pack.manifestUrl.isValid());
        EXPECT_EQ(pack.distributionType, QStringLiteral("chunks"));
        EXPECT_EQ(pack.downloadSizeBytes, pack.installedSizeBytes);
    }
    EXPECT_FALSE(packs.at(0).recommended);
    EXPECT_TRUE(packs.at(1).recommended);
    EXPECT_FALSE(packs.at(2).recommended);
    EXPECT_FALSE(packs.at(3).recommended);
    EXPECT_EQ(an::ui::defaultGaiaCatalogPack(), QStringLiteral("gaia_dr3_g14"));
}

namespace
{

/// The manifest of a one-chunk catalog whose chunk is four bytes long.
[[nodiscard]] QJsonObject oneChunkManifest(const bool named)
{
    QJsonObject root{
        {QStringLiteral("format_version"), 3},
        {QStringLiteral("storage"), QStringLiteral("chunks")},
        {QStringLiteral("catalog_id"), QStringLiteral("gaia_dr3_g14")},
        {QStringLiteral("chunk_count"), 1},
        {QStringLiteral("chunks"),
         QJsonArray{
             QJsonObject{
                 {QStringLiteral("index"), 0},
                 {QStringLiteral("file"), QStringLiteral("gaia_dr3_g14_chunk_00.ansp")},
                 {QStringLiteral("size_bytes"), 4},
             },
         }},
    };
    if (named)
    {
        root.insert(QStringLiteral("catalog_name"), QStringLiteral("Gaia DR3 G<=14"));
    }
    return root;
}

} // namespace

TEST(GaiaCatalogSettings, DetectsReadyChunkCatalogInstallation)
{
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());

    const QString catalogDir = QDir(temp_dir.path()).filePath(QStringLiteral("gaia_dr3_g14"));
    ASSERT_TRUE(writeGaiaCatalogMeta(catalogDir, oneChunkManifest(true)));
    ASSERT_TRUE(
        writeFileBytes(QDir(catalogDir).filePath(QStringLiteral("gaia_dr3_g14_chunk_00.ansp")), QByteArray("ansp")));
    ASSERT_TRUE(writeFileBytes(QDir(catalogDir).filePath(QStringLiteral(".install_complete")), QByteArray("ok\n")));

    EXPECT_EQ(an::ui::inspectGaiaCatalogDirectory(catalogDir), an::ui::GaiaCatalogDirectoryStatus::Ready);
    EXPECT_TRUE(an::ui::isGaiaCatalogReady(catalogDir));
}

TEST(GaiaCatalogSettings, DetectsManualChunkCatalogInstallationWithoutInstallMarker)
{
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());

    const QString catalogDir = QDir(temp_dir.path()).filePath(QStringLiteral("gaia_dr3_g14"));
    ASSERT_TRUE(writeGaiaCatalogMeta(catalogDir, oneChunkManifest(true)));
    ASSERT_TRUE(
        writeFileBytes(QDir(catalogDir).filePath(QStringLiteral("gaia_dr3_g14_chunk_00.ansp")), QByteArray("ansp")));

    const an::ui::GaiaCatalogDirectoryInspection inspection = an::ui::inspectGaiaCatalogDirectoryDetailed(catalogDir);
    EXPECT_EQ(inspection.status, an::ui::GaiaCatalogDirectoryStatus::Ready);
    EXPECT_EQ(inspection.catalogId, QStringLiteral("gaia_dr3_g14"));
    EXPECT_EQ(inspection.catalogName, QStringLiteral("Gaia DR3 G<=14"));
    EXPECT_EQ(inspection.chunkCount, 1);
    EXPECT_EQ(inspection.chunkFilesPresent, 1);
}

TEST(GaiaCatalogSettings, DetectsIncompleteChunkCatalogInstallationFromManifest)
{
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());

    const QString catalogDir = QDir(temp_dir.path()).filePath(QStringLiteral("gaia_dr3_g14"));
    ASSERT_TRUE(writeGaiaCatalogMeta(catalogDir, oneChunkManifest(false)));

    EXPECT_EQ(an::ui::inspectGaiaCatalogDirectory(catalogDir), an::ui::GaiaCatalogDirectoryStatus::IncompleteInstall);
    EXPECT_FALSE(an::ui::isGaiaCatalogReady(catalogDir));
}

TEST(GaiaCatalogSettings, DetectsTiledCatalogInstallation)
{
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());

    const QString catalogDir = QDir(temp_dir.path()).filePath(QStringLiteral("gaia_dr3_g14"));
    ASSERT_TRUE(
        writeGaiaCatalogMeta(catalogDir, QJsonObject{
                                             {QStringLiteral("format_version"), 2},
                                             {QStringLiteral("storage"), QStringLiteral("tiles")},
                                             {QStringLiteral("catalog_id"), QStringLiteral("gaia_dr3_g14")},
                                             {QStringLiteral("tile_directory"), QStringLiteral("healpix_order5")},
                                         }));
    ASSERT_TRUE(QDir().mkpath(QDir(catalogDir).filePath(QStringLiteral("healpix_order5"))));

    EXPECT_EQ(an::ui::inspectGaiaCatalogDirectory(catalogDir), an::ui::GaiaCatalogDirectoryStatus::UnsupportedFormat);
    EXPECT_FALSE(an::ui::isGaiaCatalogReady(catalogDir));
}

TEST(ObservationEpochUtils, TheEpochIsTheJulianEpochOfTheExposureStart)
{
    // 2024-01-15T06:00:00Z, counted in Julian years of 365.25 days from J2000.0 at noon.
    constexpr std::int64_t start = 1705298400;
    const std::optional<double> epoch_year =
        an::ui::resolveObservationEpochYear(an::tests::astrometry::acquisitionStartingAt(start * 1000000000LL));
    ASSERT_TRUE(epoch_year.has_value());
    EXPECT_NEAR(*epoch_year, 2000.0 + static_cast<double>(start - 946728000) / 31557600.0, 1e-9);
}

TEST(ObservationEpochUtils, J2000IsNoonOfTheFirstDayOfTwoThousand)
{
    // Counted from midnight instead, every epoch would be half a day early: 2000.00137 here.
    const std::optional<double> noon =
        an::ui::resolveObservationEpochYear(an::tests::astrometry::acquisitionStartingAt(946728000LL * 1000000000LL));
    ASSERT_TRUE(noon.has_value());
    EXPECT_DOUBLE_EQ(*noon, 2000.0);
}

TEST(ObservationEpochUtils, AnExposureWithNoStartHasNoEpoch)
{
    EXPECT_FALSE(an::ui::resolveObservationEpochYear(an::AcquisitionMetadata{}).has_value());
}

TEST(PlateSolveService, InternalGaiaDetectionChannelsIncludeAllImagePlanes)
{
    EXPECT_TRUE(an::ui::internalGaiaDetectionChannels(0).empty());
    EXPECT_EQ(an::ui::internalGaiaDetectionChannels(1), (std::vector<int>{0}));
    EXPECT_EQ(an::ui::internalGaiaDetectionChannels(3), (std::vector<int>{0, 1, 2}));
}

TEST(PlateSolveService, ASolveWithoutASessionSaysSo)
{
    an::ui::PlateSolveService service(std::make_unique<MockSolverProcessRunner>());
    an::ui::PlateSolveRequest request;
    request.imagePath = QStringLiteral("unused.fit");
    CancellationToken token;
    const an::ui::PlateSolveResult result = service.solve(request, an::ui::PlateSolveSettings{}, token);
    EXPECT_FALSE(result.succeeded);
    EXPECT_TRUE(result.errorMessage.contains(QStringLiteral("session")));
}

TEST_F(PlateSolveServiceTest, SolvesWithAstap)
{
    auto runner = std::make_unique<MockSolverProcessRunner>();
    auto* runner_ptr = runner.get();
    runner_ptr->queuedRuns.push_back({.exit_code = 0});
    runner_ptr->queuedReadFiles.push_back(makeSolvedHeaderBytes(320.0, 240.0, 120.5, -22.25, 0.0005));

    an::ui::PlateSolveService service(std::move(runner));

    an::ui::PlateSolveSettings settings;
    settings.astapPath = solverFile(QStringLiteral("astap_mock"));
    settings.astrometryLocalPath = solverFile(QStringLiteral("solve_field_mock"));

    CancellationToken token;
    const auto result = service.solve(request(QStringLiteral("light.fit"), plainCards()), settings, token);

    ASSERT_TRUE(result.succeeded) << result.errorMessage.toStdString();
    EXPECT_EQ(result.solverUsed, QStringLiteral("ASTAP"));
    ASSERT_TRUE(result.solution.has_value());
    EXPECT_EQ(result.solution->imageWidth(), 640u);
    EXPECT_EQ(result.solution->imageHeight(), 480u);
    EXPECT_NEAR(result.imageScaleArcsecPerPixel, 1.8, 1e-9);
    EXPECT_NEAR(result.fieldCenterRaDeg, 120.5, 2.0);
    EXPECT_NEAR(result.fieldCenterDecDeg, -22.25, 2.0);

    ASSERT_EQ(runner_ptr->invocations.size(), 1u);
    EXPECT_EQ(runner_ptr->invocations.front().executable, settings.astapPath);
    EXPECT_TRUE(runner_ptr->invocations.front().arguments.contains(QStringLiteral("-wcs")));
}

TEST_F(PlateSolveServiceTest, TheAnswersReferencePixelIsTheEnginesHalfAPixelBelowTheHeaders)
{
    // A header's CRPIX counts pixel centres from one; the engine's first pixel centre is at one half.
    auto runner = std::make_unique<MockSolverProcessRunner>();
    runner->queuedRuns.push_back({.exit_code = 0});
    runner->queuedReadFiles.push_back(makeSolvedHeaderBytes(320.0, 240.0, 120.5, -22.25, 0.0005));
    an::ui::PlateSolveService service(std::move(runner));

    an::ui::PlateSolveSettings settings;
    settings.astapPath = solverFile(QStringLiteral("astap_mock"));
    CancellationToken token;
    const auto result = service.solve(request(QStringLiteral("reference.fit"), plainCards()), settings, token);

    ASSERT_TRUE(result.succeeded) << result.errorMessage.toStdString();
    ASSERT_TRUE(result.solution.has_value());
    EXPECT_DOUBLE_EQ(result.solution->referencePixel().x, 319.5);
    EXPECT_DOUBLE_EQ(result.solution->referencePixel().y, 239.5);
    EXPECT_NEAR(result.solution->referencePoint().rightAscensionDegrees, 120.5, 1e-12);
}

TEST_F(PlateSolveServiceTest, ASolversEquinoxWithoutAFrameIsReadInTheIcrs)
{
    // ASTAP and astrometry.net write the J2000 equinox and no frame, which the card bridge reads as FK5; the solve
    // states the ICRS its solvers solve in.
    QByteArray header = makeSolvedHeaderBytes(320.0, 240.0, 120.5, -22.25, 0.0005);
    header.replace("END\n", "EQUINOX =               2000.0\nEND\n");
    auto runner = std::make_unique<MockSolverProcessRunner>();
    runner->queuedRuns.push_back({.exit_code = 0});
    runner->queuedReadFiles.push_back(header);
    an::ui::PlateSolveService service(std::move(runner));

    an::ui::PlateSolveSettings settings;
    settings.astapPath = solverFile(QStringLiteral("astap_mock"));
    CancellationToken token;
    const auto result = service.solve(request(QStringLiteral("equinox.fit"), plainCards()), settings, token);

    ASSERT_TRUE(result.succeeded) << result.errorMessage.toStdString();
    EXPECT_EQ(result.solverUsed, QStringLiteral("ASTAP"));
}

TEST_F(PlateSolveServiceTest, AnAnswerWithNoEndRecordIsRefused)
{
    // A truncated download: the header reader refuses a header with no END record rather than reading what is there.
    QByteArray header = makeSolvedHeaderBytes(320.0, 240.0, 120.5, -22.25, 0.0005);
    header.replace("END\n", "");
    auto runner = std::make_unique<MockSolverProcessRunner>();
    runner->queuedRuns.push_back({.exit_code = 0});
    runner->queuedReadFiles.push_back(header);
    an::ui::PlateSolveService service(std::move(runner));

    an::ui::PlateSolveSettings settings;
    settings.astapPath = solverFile(QStringLiteral("astap_mock"));
    CancellationToken token;
    const auto result = service.solve(request(QStringLiteral("truncated.fit"), plainCards()), settings, token);

    EXPECT_FALSE(result.succeeded);
    EXPECT_TRUE(result.errorMessage.contains(QStringLiteral("did not contain a valid WCS")))
        << result.errorMessage.toStdString();
}

TEST_F(PlateSolveServiceTest, CardsAfterTheEndRecordAreNotRead)
{
    QByteArray header = makeSolvedHeaderBytes(320.0, 240.0, 120.5, -22.25, 0.0005);
    header += "CRVAL1  = 300.0\n";
    auto runner = std::make_unique<MockSolverProcessRunner>();
    runner->queuedRuns.push_back({.exit_code = 0});
    runner->queuedReadFiles.push_back(header);
    an::ui::PlateSolveService service(std::move(runner));

    an::ui::PlateSolveSettings settings;
    settings.astapPath = solverFile(QStringLiteral("astap_mock"));
    CancellationToken token;
    const auto result = service.solve(request(QStringLiteral("after_end.fit"), plainCards()), settings, token);

    ASSERT_TRUE(result.succeeded) << result.errorMessage.toStdString();
    ASSERT_TRUE(result.solution.has_value());
    EXPECT_NEAR(result.solution->referencePoint().rightAscensionDegrees, 120.5, 1e-12);
}

TEST_F(PlateSolveServiceTest, FallsBackToSolveField)
{
    auto runner = std::make_unique<MockSolverProcessRunner>();
    auto* runner_ptr = runner.get();
    runner_ptr->queuedRuns.push_back({.exit_code = 1});
    runner_ptr->queuedRuns.push_back({.exit_code = 0});
    runner_ptr->queuedReadFiles.push_back(std::nullopt);
    runner_ptr->queuedReadFiles.push_back(makeSolvedHeaderBytes(300.0, 220.0, 130.0, 15.0, 0.0004));

    an::ui::PlateSolveService service(std::move(runner));

    an::ui::PlateSolveSettings settings;
    settings.astapPath = solverFile(QStringLiteral("astap_mock"));
    settings.astrometryLocalPath = solverFile(QStringLiteral("solve_field_mock"));

    CancellationToken token;
    const auto result = service.solve(request(QStringLiteral("fallback.fit"), plainCards(), 600, 440), settings, token);

    ASSERT_TRUE(result.succeeded) << result.errorMessage.toStdString();
    EXPECT_EQ(result.solverUsed, QStringLiteral("solve-field"));
    ASSERT_EQ(runner_ptr->invocations.size(), 2u);
    EXPECT_EQ(runner_ptr->invocations[0].executable, settings.astapPath);
    EXPECT_EQ(runner_ptr->invocations[1].executable, settings.astrometryLocalPath);
}

TEST_F(PlateSolveServiceTest, DoesNotUseAstapWithoutConfiguredPath)
{
    auto runner = std::make_unique<MockSolverProcessRunner>();
    auto* runner_ptr = runner.get();
    runner_ptr->queuedRuns.push_back({.exit_code = 0});
    runner_ptr->queuedReadFiles.push_back(makeSolvedHeaderBytes(300.0, 220.0, 130.0, 15.0, 0.0004));

    an::ui::PlateSolveService service(std::move(runner));

    an::ui::PlateSolveSettings settings;
    settings.astapPath.clear();
    settings.astrometryLocalPath = solverFile(QStringLiteral("solve_field_mock"));

    CancellationToken token;
    const auto result = service.solve(request(QStringLiteral("fallback.fit"), plainCards(), 600, 440), settings, token);

    ASSERT_TRUE(result.succeeded) << result.errorMessage.toStdString();
    EXPECT_EQ(result.solverUsed, QStringLiteral("solve-field"));
    ASSERT_EQ(runner_ptr->invocations.size(), 1u);
    EXPECT_EQ(runner_ptr->invocations.front().executable, settings.astrometryLocalPath);
}

TEST_F(PlateSolveServiceTest, SolverOrderInternalGaiaPreferredFallsBackToAstap)
{
    auto runner = std::make_unique<MockSolverProcessRunner>();
    auto* runner_ptr = runner.get();
    runner_ptr->queuedRuns.push_back({.exit_code = 0});
    runner_ptr->queuedReadFiles.push_back(makeSolvedHeaderBytes(320.0, 240.0, 122.0, -20.5, 0.00045));

    an::ui::PlateSolveService service(std::move(runner));

    an::ui::PlateSolveSettings settings;
    settings.defaultSolver = QStringLiteral("internal_gaia");
    settings.gaiaCatalogDirectory = QDir(m_directory.path()).filePath(QStringLiteral("missing_catalog"));
    settings.gaiaCatalogPack = QStringLiteral("gaia_dr3_g14");
    settings.astapPath = solverFile(QStringLiteral("astap_mock"));

    CancellationToken token;
    const auto result =
        service.solve(request(QStringLiteral("internal_gaia_first.fit"), plainCards()), settings, token);

    ASSERT_TRUE(result.succeeded) << result.errorMessage.toStdString();
    EXPECT_EQ(result.solverUsed, QStringLiteral("ASTAP"));
    ASSERT_EQ(runner_ptr->invocations.size(), 1u);
    EXPECT_EQ(runner_ptr->invocations.front().executable, settings.astapPath);
}

TEST_F(PlateSolveServiceTest, TiledInternalGaiaCatalogFallsBackToAstap)
{
    const QString tiledCatalogDir = QDir(m_directory.path()).filePath(QStringLiteral("tiled_catalog"));
    ASSERT_TRUE(
        writeGaiaCatalogMeta(tiledCatalogDir, QJsonObject{
                                                  {QStringLiteral("format_version"), 2},
                                                  {QStringLiteral("storage"), QStringLiteral("tiles")},
                                                  {QStringLiteral("catalog_id"), QStringLiteral("gaia_dr3_g14")},
                                                  {QStringLiteral("tile_directory"), QStringLiteral("healpix_order5")},
                                              }));
    ASSERT_TRUE(QDir().mkpath(QDir(tiledCatalogDir).filePath(QStringLiteral("healpix_order5"))));

    auto runner = std::make_unique<MockSolverProcessRunner>();
    auto* runner_ptr = runner.get();
    runner_ptr->queuedRuns.push_back({.exit_code = 0});
    runner_ptr->queuedReadFiles.push_back(makeSolvedHeaderBytes(320.0, 240.0, 122.0, -20.5, 0.00045));

    an::ui::PlateSolveService service(std::move(runner));

    an::ui::PlateSolveSettings settings;
    settings.defaultSolver = QStringLiteral("internal_gaia");
    settings.gaiaCatalogDirectory = tiledCatalogDir;
    settings.gaiaCatalogPack = QStringLiteral("gaia_dr3_g14");
    settings.astapPath = solverFile(QStringLiteral("astap_mock"));

    CancellationToken token;
    const auto result =
        service.solve(request(QStringLiteral("tiled_internal_gaia.fit"), plainCards()), settings, token);

    ASSERT_TRUE(result.succeeded) << result.errorMessage.toStdString();
    EXPECT_EQ(result.solverUsed, QStringLiteral("ASTAP"));
    ASSERT_EQ(runner_ptr->invocations.size(), 1u);
    EXPECT_EQ(runner_ptr->invocations.front().executable, settings.astapPath);
}

TEST_F(PlateSolveServiceTest, AllowExternalSolveFalseSkipsInternalGaiaToo)
{
    auto runner = std::make_unique<MockSolverProcessRunner>();
    auto* runner_ptr = runner.get();

    an::ui::PlateSolveService service(std::move(runner));

    an::ui::PlateSolveRequest solve = request(QStringLiteral("no_auto_solve.fit"), plainCards());
    solve.allowExternalSolve = false;

    an::ui::PlateSolveSettings settings;
    settings.defaultSolver = QStringLiteral("internal_gaia");
    settings.gaiaCatalogDirectory = QDir(m_directory.path()).filePath(QStringLiteral("missing_catalog"));
    settings.astapPath = solverFile(QStringLiteral("astap_mock"));
    settings.astrometryLocalPath = solverFile(QStringLiteral("solve_field_mock"));
    settings.astrometryApiKey = QStringLiteral("dummy-api-key");

    CancellationToken token;
    const auto result = service.solve(solve, settings, token);

    EXPECT_FALSE(result.succeeded);
    EXPECT_TRUE(result.errorMessage.contains(QStringLiteral("Internal Gaia")));
    EXPECT_TRUE(result.errorMessage.contains(QStringLiteral("ASTAP")));
    EXPECT_TRUE(result.errorMessage.contains(QStringLiteral("solve-field")));
    EXPECT_TRUE(result.errorMessage.contains(QStringLiteral("web")));
    EXPECT_TRUE(runner_ptr->invocations.empty());
}

TEST_F(PlateSolveServiceTest, ReportsErrorWhenBothSolversFail)
{
    auto runner = std::make_unique<MockSolverProcessRunner>();
    runner->queuedRuns.push_back({.exit_code = 1, .std_err = QByteArray("astap failed")});
    runner->queuedRuns.push_back({.exit_code = 1, .std_err = QByteArray("solve-field failed")});
    runner->queuedReadFiles.push_back(std::nullopt);
    runner->queuedReadFiles.push_back(std::nullopt);

    an::ui::PlateSolveService service(std::move(runner));

    an::ui::PlateSolveSettings settings;
    settings.astapPath = solverFile(QStringLiteral("astap_mock"));
    settings.astrometryLocalPath = solverFile(QStringLiteral("solve_field_mock"));

    CancellationToken token;
    const auto result = service.solve(request(QStringLiteral("error.fit"), plainCards()), settings, token);

    EXPECT_FALSE(result.succeeded);
    EXPECT_FALSE(result.errorMessage.isEmpty());
    EXPECT_NE(result.errorMessage.indexOf(QStringLiteral("ASTAP")), -1);
    EXPECT_NE(result.errorMessage.indexOf(QStringLiteral("solve-field")), -1);
}

TEST_F(PlateSolveServiceTest, ExtractsHintsFromMetadata)
{
    auto runner = std::make_unique<MockSolverProcessRunner>();
    auto* runner_ptr = runner.get();
    runner_ptr->queuedRuns.push_back({.exit_code = 0});
    runner_ptr->queuedReadFiles.push_back(makeSolvedHeaderBytes(320.0, 240.0, 187.5, -45.5, 0.0005));

    an::ui::PlateSolveService service(std::move(runner));

    an::ui::PlateSolveSettings settings;
    settings.astapPath = solverFile(QStringLiteral("astap_mock"));
    settings.useHints = true;

    CancellationToken token;
    const auto result =
        service.solve(request(QStringLiteral("hints.fit"), hintCards("'12:30:00'", "'-45:30:00'")), settings, token);

    ASSERT_TRUE(result.succeeded) << result.errorMessage.toStdString();
    ASSERT_EQ(runner_ptr->invocations.size(), 1u);
    const QStringList& args = runner_ptr->invocations.front().arguments;
    EXPECT_NEAR(valueAfterFlag(args, QStringLiteral("-ra")).toDouble(), 12.5, 1e-6);
    EXPECT_NEAR(valueAfterFlag(args, QStringLiteral("-spd")).toDouble(), 44.5, 1e-6);
}

TEST_F(PlateSolveServiceTest, PreservesNegativeZeroDeclinationInMetadataHints)
{
    auto runner = std::make_unique<MockSolverProcessRunner>();
    auto* runner_ptr = runner.get();
    runner_ptr->queuedRuns.push_back({.exit_code = 0});
    runner_ptr->queuedReadFiles.push_back(makeSolvedHeaderBytes(320.0, 240.0, 187.5, -0.5, 0.0005));

    an::ui::PlateSolveService service(std::move(runner));

    an::ui::PlateSolveSettings settings;
    settings.astapPath = solverFile(QStringLiteral("astap_mock"));
    settings.useHints = true;

    CancellationToken token;
    const auto result =
        service.solve(request(QStringLiteral("negative_zero_declination.fit"), hintCards("'12:30:00'", "'-00:30:00'")),
                      settings, token);

    ASSERT_TRUE(result.succeeded) << result.errorMessage.toStdString();
    ASSERT_EQ(runner_ptr->invocations.size(), 1u);
    const QStringList& args = runner_ptr->invocations.front().arguments;
    EXPECT_NEAR(valueAfterFlag(args, QStringLiteral("-ra")).toDouble(), 12.5, 1e-6);
    EXPECT_NEAR(valueAfterFlag(args, QStringLiteral("-spd")).toDouble(), 89.5, 1e-6);
}

TEST_F(PlateSolveServiceTest, AFullTurnOfRightAscensionHintsZero)
{
    // Twenty-four hours is folded into the turn every other direction is, not kept as 360 degrees.
    auto runner = std::make_unique<MockSolverProcessRunner>();
    auto* runner_ptr = runner.get();
    runner_ptr->queuedRuns.push_back({.exit_code = 0});
    runner_ptr->queuedReadFiles.push_back(makeSolvedHeaderBytes(320.0, 240.0, 0.5, 10.0, 0.0005));
    an::ui::PlateSolveService service(std::move(runner));

    an::ui::PlateSolveSettings settings;
    settings.astapPath = solverFile(QStringLiteral("astap_mock"));
    CancellationToken token;
    const auto result = service.solve(request(QStringLiteral("full_turn.fit"), hintCards("'24 00 00'", "'+10 00 00'")),
                                      settings, token);

    ASSERT_TRUE(result.succeeded) << result.errorMessage.toStdString();
    ASSERT_EQ(runner_ptr->invocations.size(), 1u);
    EXPECT_DOUBLE_EQ(valueAfterFlag(runner_ptr->invocations.front().arguments, QStringLiteral("-ra")).toDouble(), 0.0);
}

TEST_F(PlateSolveServiceTest, AFractionInAFieldAnotherFollowsGivesNoHint)
{
    // `12.5 30` read as twelve and a half hours and thirty minutes - thirteen hours - before; it is refused now.
    auto runner = std::make_unique<MockSolverProcessRunner>();
    auto* runner_ptr = runner.get();
    runner_ptr->queuedRuns.push_back({.exit_code = 0});
    runner_ptr->queuedReadFiles.push_back(makeSolvedHeaderBytes(320.0, 240.0, 187.5, 10.0, 0.0005));
    an::ui::PlateSolveService service(std::move(runner));

    an::ui::PlateSolveSettings settings;
    settings.astapPath = solverFile(QStringLiteral("astap_mock"));
    CancellationToken token;
    const auto result = service.solve(request(QStringLiteral("fractional.fit"), hintCards("'12.5 30'", "'+10 00 00'")),
                                      settings, token);

    ASSERT_TRUE(result.succeeded) << result.errorMessage.toStdString();
    ASSERT_EQ(runner_ptr->invocations.size(), 1u);
    EXPECT_EQ(runner_ptr->invocations.front().arguments.indexOf(QStringLiteral("-ra")), -1);
}

TEST_F(PlateSolveServiceTest, TheSolveReadsUpperCaseUnitLetters)
{
    // The window read the unit letters in either case and the solve refused the upper-case spelling; both read the
    // pointing through one engine reader now.
    auto runner = std::make_unique<MockSolverProcessRunner>();
    auto* runner_ptr = runner.get();
    runner_ptr->queuedRuns.push_back({.exit_code = 0});
    runner_ptr->queuedReadFiles.push_back(makeSolvedHeaderBytes(320.0, 240.0, 83.633, 22.0145, 0.0005));
    an::ui::PlateSolveService service(std::move(runner));

    an::ui::PlateSolveSettings settings;
    settings.astapPath = solverFile(QStringLiteral("astap_mock"));
    CancellationToken token;
    const auto result = service.solve(
        request(QStringLiteral("spellings.fit"), hintCards("'05H34M32S'", "'+22D00M52S'")), settings, token);

    ASSERT_TRUE(result.succeeded) << result.errorMessage.toStdString();
    ASSERT_EQ(runner_ptr->invocations.size(), 1u);
    const QStringList& args = runner_ptr->invocations.front().arguments;
    EXPECT_NEAR(valueAfterFlag(args, QStringLiteral("-ra")).toDouble(), 5.0 + 34.0 / 60.0 + 32.0 / 3600.0, 1e-8);
    EXPECT_NEAR(valueAfterFlag(args, QStringLiteral("-spd")).toDouble(), 90.0 + 22.0 + 52.0 / 3600.0, 1e-8);
}

TEST_F(PlateSolveServiceTest, ReportsCancellation)
{
    auto runner = std::make_unique<MockSolverProcessRunner>();
    runner->queuedRuns.push_back({.was_cancelled = true});
    runner->onRun = [](const CancellationToken& token) { const_cast<CancellationToken&>(token).requestCancel(); };

    an::ui::PlateSolveService service(std::move(runner));

    an::ui::PlateSolveSettings settings;
    settings.astapPath = solverFile(QStringLiteral("astap_mock"));

    CancellationToken token;
    const auto result = service.solve(request(QStringLiteral("cancel.fit"), plainCards()), settings, token);

    EXPECT_FALSE(result.succeeded);
    EXPECT_TRUE(result.cancelled);
    EXPECT_NE(result.errorMessage.indexOf(QStringLiteral("cancel"), 0, Qt::CaseInsensitive), -1);
}

TEST_F(PlateSolveServiceTest, RequestHintOverridesMetadataForAstap)
{
    auto runner = std::make_unique<MockSolverProcessRunner>();
    auto* runner_ptr = runner.get();
    runner_ptr->queuedRuns.push_back({.exit_code = 0});
    runner_ptr->queuedReadFiles.push_back(makeSolvedHeaderBytes(320.0, 240.0, 45.0, 30.0, 0.0005));

    an::ui::PlateSolveService service(std::move(runner));

    // The cards state 12h 30m and -45 30; the request states its own position.
    an::ui::PlateSolveRequest solve =
        request(QStringLiteral("hint_override.fit"), hintCards("'12:30:00'", "'-45:30:00'"));
    solve.hintRaDeg = 45.0;
    solve.hintDecDeg = 30.0;

    an::ui::PlateSolveSettings settings;
    settings.astapPath = solverFile(QStringLiteral("astap_mock"));
    settings.useHints = true;

    CancellationToken token;
    const auto result = service.solve(solve, settings, token);

    ASSERT_TRUE(result.succeeded) << result.errorMessage.toStdString();
    ASSERT_EQ(runner_ptr->invocations.size(), 1u);
    const QStringList& args = runner_ptr->invocations.front().arguments;
    EXPECT_NEAR(valueAfterFlag(args, QStringLiteral("-ra")).toDouble(), 3.0, 1e-6);
    EXPECT_NEAR(valueAfterFlag(args, QStringLiteral("-spd")).toDouble(), 120.0, 1e-6);
}

TEST_F(PlateSolveServiceTest, DisabledMetadataPositionHintsDoNotConstrainSolve)
{
    auto runner = std::make_unique<MockSolverProcessRunner>();
    auto* runner_ptr = runner.get();
    runner_ptr->queuedRuns.push_back({.exit_code = 0});
    runner_ptr->queuedReadFiles.push_back(makeSolvedHeaderBytes(320.0, 240.0, 210.0, 35.0, 0.0005));

    an::ui::PlateSolveService service(std::move(runner));

    std::vector<an::CodecCard> cards = plainCards();
    addCard(cards, "CRVAL1", "45.0");
    addCard(cards, "CRVAL2", "-20.0");
    an::ui::PlateSolveRequest solve = request(QStringLiteral("disabled_metadata_hint.fit"), std::move(cards));
    solve.allowMetadataPositionHints = false;
    solve.validateSolutionAgainstHints = true;

    an::ui::PlateSolveSettings settings;
    settings.defaultSolver = QStringLiteral("astap");
    settings.astapPath = solverFile(QStringLiteral("astap_mock"));
    settings.searchRadius = 2.0;
    settings.useHints = true;

    CancellationToken token;
    const auto result = service.solve(solve, settings, token);

    EXPECT_TRUE(result.succeeded) << result.errorMessage.toStdString();
    ASSERT_EQ(runner_ptr->invocations.size(), 1u);
    EXPECT_EQ(runner_ptr->invocations.front().arguments.indexOf(QStringLiteral("-ra")), -1);
    EXPECT_EQ(runner_ptr->invocations.front().arguments.indexOf(QStringLiteral("-spd")), -1);
}

TEST_F(PlateSolveServiceTest, RejectsSolvedFieldOutsideValidatedRequestHint)
{
    auto runner = std::make_unique<MockSolverProcessRunner>();
    auto* runner_ptr = runner.get();
    runner_ptr->queuedRuns.push_back({.exit_code = 0});
    runner_ptr->queuedReadFiles.push_back(makeSolvedHeaderBytes(320.0, 240.0, 210.0, 35.0, 0.0005));

    an::ui::PlateSolveService service(std::move(runner));

    an::ui::PlateSolveRequest solve = request(QStringLiteral("validated_hint.fit"), plainCards());
    solve.hintRaDeg = 45.0;
    solve.hintDecDeg = -20.0;
    solve.validateSolutionAgainstHints = true;

    an::ui::PlateSolveSettings settings;
    settings.defaultSolver = QStringLiteral("astap");
    settings.astapPath = solverFile(QStringLiteral("astap_mock"));
    settings.searchRadius = 2.0;
    settings.useHints = true;

    CancellationToken token;
    const auto result = service.solve(solve, settings, token);

    EXPECT_FALSE(result.succeeded);
    EXPECT_NE(result.errorMessage.indexOf(QStringLiteral("outside the requested search radius")), -1);
    EXPECT_EQ(runner_ptr->invocations.size(), 1u);
}

TEST_F(PlateSolveServiceTest, RejectsSolvedFieldOutsideValidatedRequestHintWhenSolverHintsAreDisabled)
{
    auto runner = std::make_unique<MockSolverProcessRunner>();
    auto* runner_ptr = runner.get();
    runner_ptr->queuedRuns.push_back({.exit_code = 0});
    runner_ptr->queuedReadFiles.push_back(makeSolvedHeaderBytes(320.0, 240.0, 210.0, 35.0, 0.0005));

    an::ui::PlateSolveService service(std::move(runner));

    an::ui::PlateSolveRequest solve = request(QStringLiteral("validated_hint_without_solver_hint.fit"), plainCards());
    solve.hintRaDeg = 45.0;
    solve.hintDecDeg = -20.0;
    solve.validateSolutionAgainstHints = true;

    an::ui::PlateSolveSettings settings;
    settings.defaultSolver = QStringLiteral("astap");
    settings.astapPath = solverFile(QStringLiteral("astap_mock_without_solver_hint"));
    settings.searchRadius = 2.0;
    settings.useHints = false;

    CancellationToken token;
    const auto result = service.solve(solve, settings, token);

    EXPECT_FALSE(result.succeeded);
    EXPECT_NE(result.errorMessage.indexOf(QStringLiteral("outside the requested search radius")), -1);
    ASSERT_EQ(runner_ptr->invocations.size(), 1u);
    EXPECT_EQ(runner_ptr->invocations.front().arguments.indexOf(QStringLiteral("-ra")), -1);
}

TEST_F(PlateSolveServiceTest, FocalLengthAndPixelSizeProduceAstapFov)
{
    auto runner = std::make_unique<MockSolverProcessRunner>();
    auto* runner_ptr = runner.get();
    runner_ptr->queuedRuns.push_back({.exit_code = 0});
    runner_ptr->queuedReadFiles.push_back(makeSolvedHeaderBytes(320.0, 240.0, 120.5, -22.25, 0.0005));

    an::ui::PlateSolveService service(std::move(runner));

    an::ui::PlateSolveRequest solve = request(QStringLiteral("scale.fit"), plainCards());
    solve.focalLengthMm = 500.0;
    solve.pixelSizeUm = 3.8;

    an::ui::PlateSolveSettings settings;
    settings.astapPath = solverFile(QStringLiteral("astap_mock"));
    settings.useHints = true;

    CancellationToken token;
    const auto result = service.solve(solve, settings, token);

    ASSERT_TRUE(result.succeeded) << result.errorMessage.toStdString();
    ASSERT_EQ(runner_ptr->invocations.size(), 1u);
    const QStringList& args = runner_ptr->invocations.front().arguments;

    // The scale is 3.8 / 500 radians per thousand, in arcseconds; the field 640 of those pixels.
    const QString fov_str = valueAfterFlag(args, QStringLiteral("-fov"));
    ASSERT_FALSE(fov_str.isEmpty()) << "ASTAP should receive -fov argument";
    EXPECT_NEAR(fov_str.toDouble(), 3.8 / 500.0 * 206.26480624709636 * 640.0 / 3600.0, 1e-4);
}

TEST_F(PlateSolveServiceTest, FocalLengthAndPixelSizeProduceSolveFieldScale)
{
    auto runner = std::make_unique<MockSolverProcessRunner>();
    auto* runner_ptr = runner.get();
    runner_ptr->queuedRuns.push_back({.exit_code = 1});
    runner_ptr->queuedRuns.push_back({.exit_code = 0});
    runner_ptr->queuedReadFiles.push_back(std::nullopt);
    runner_ptr->queuedReadFiles.push_back(makeSolvedHeaderBytes(300.0, 220.0, 130.0, 15.0, 0.0004));

    an::ui::PlateSolveService service(std::move(runner));

    an::ui::PlateSolveRequest solve = request(QStringLiteral("scale_sf.fit"), plainCards(), 600, 440);
    solve.focalLengthMm = 500.0;
    solve.pixelSizeUm = 3.8;

    an::ui::PlateSolveSettings settings;
    settings.astapPath = solverFile(QStringLiteral("astap_mock"));
    settings.astrometryLocalPath = solverFile(QStringLiteral("solve_field_mock"));
    settings.useHints = true;

    CancellationToken token;
    const auto result = service.solve(solve, settings, token);

    ASSERT_TRUE(result.succeeded) << result.errorMessage.toStdString();
    ASSERT_EQ(runner_ptr->invocations.size(), 2u);
    const QStringList& args = runner_ptr->invocations[1].arguments;

    const double scale = 3.8 / 500.0 * 206.26480624709636;
    const QString lo = valueAfterFlag(args, QStringLiteral("--scale-low"));
    const QString hi = valueAfterFlag(args, QStringLiteral("--scale-high"));
    ASSERT_FALSE(lo.isEmpty()) << "solve-field should receive --scale-low";
    ASSERT_FALSE(hi.isEmpty()) << "solve-field should receive --scale-high";
    EXPECT_NEAR(lo.toDouble(), scale * 0.8, 1e-4);
    EXPECT_NEAR(hi.toDouble(), scale * 1.2, 1e-4);
    EXPECT_EQ(valueAfterFlag(args, QStringLiteral("--scale-units")), QStringLiteral("arcsecperpix"));
}

TEST_F(PlateSolveServiceTest, SettingsFocalLengthUsedWhenRequestLacks)
{
    auto runner = std::make_unique<MockSolverProcessRunner>();
    auto* runner_ptr = runner.get();
    runner_ptr->queuedRuns.push_back({.exit_code = 0});
    runner_ptr->queuedReadFiles.push_back(makeSolvedHeaderBytes(320.0, 240.0, 120.5, -22.25, 0.0005));

    an::ui::PlateSolveService service(std::move(runner));

    an::ui::PlateSolveSettings settings;
    settings.astapPath = solverFile(QStringLiteral("astap_mock"));
    settings.useHints = true;
    settings.focalLengthMm = 1000.0;
    settings.pixelSizeUm = 4.6;

    CancellationToken token;
    const auto result = service.solve(request(QStringLiteral("settings_fl.fit"), plainCards()), settings, token);

    ASSERT_TRUE(result.succeeded) << result.errorMessage.toStdString();
    ASSERT_EQ(runner_ptr->invocations.size(), 1u);
    const QString fov_str = valueAfterFlag(runner_ptr->invocations.front().arguments, QStringLiteral("-fov"));
    ASSERT_FALSE(fov_str.isEmpty()) << "ASTAP should receive -fov from settings-level hints";
    EXPECT_NEAR(fov_str.toDouble(), 4.6 / 1000.0 * 206.26480624709636 * 640.0 / 3600.0, 1e-4);
}

TEST_F(PlateSolveServiceTest, FitsKeywordFocalLengthUsedAsFallback)
{
    auto runner = std::make_unique<MockSolverProcessRunner>();
    auto* runner_ptr = runner.get();
    runner_ptr->queuedRuns.push_back({.exit_code = 0});
    runner_ptr->queuedReadFiles.push_back(makeSolvedHeaderBytes(320.0, 240.0, 120.5, -22.25, 0.0005));

    an::ui::PlateSolveService service(std::move(runner));

    std::vector<an::CodecCard> cards = plainCards();
    addCard(cards, "FOCALLEN", "800.0");
    addCard(cards, "XPIXSZ", "3.76");

    an::ui::PlateSolveSettings settings;
    settings.astapPath = solverFile(QStringLiteral("astap_mock"));
    settings.useHints = true;

    CancellationToken token;
    const auto result = service.solve(request(QStringLiteral("fits_fl.fit"), std::move(cards)), settings, token);

    ASSERT_TRUE(result.succeeded) << result.errorMessage.toStdString();
    ASSERT_EQ(runner_ptr->invocations.size(), 1u);
    const QString fov_str = valueAfterFlag(runner_ptr->invocations.front().arguments, QStringLiteral("-fov"));
    ASSERT_FALSE(fov_str.isEmpty()) << "ASTAP should receive -fov from FITS keywords";
    EXPECT_NEAR(fov_str.toDouble(), 3.76 / 800.0 * 206.26480624709636 * 640.0 / 3600.0, 1e-4);
}

TEST_F(PlateSolveServiceTest, SolvedScaleAndPixelSizeProduceDerivedFocalLength)
{
    auto runner = std::make_unique<MockSolverProcessRunner>();
    runner->queuedRuns.push_back({.exit_code = 0});
    runner->queuedReadFiles.push_back(makeSolvedHeaderBytes(320.0, 240.0, 120.5, -22.25, 0.0005));

    an::ui::PlateSolveService service(std::move(runner));

    an::ui::PlateSolveRequest solve = request(QStringLiteral("derived_focal.fit"), plainCards());
    solve.pixelSizeUm = 3.8;

    an::ui::PlateSolveSettings settings;
    settings.astapPath = solverFile(QStringLiteral("astap_mock"));
    settings.useHints = true;

    CancellationToken token;
    const auto result = service.solve(solve, settings, token);

    ASSERT_TRUE(result.succeeded) << result.errorMessage.toStdString();
    ASSERT_TRUE(result.derivedFocalLengthMm.has_value());
    // 3.8 micrometres at 1.8 arcseconds a pixel, with the exact arcseconds in a radian.
    EXPECT_NEAR(*result.derivedFocalLengthMm, 3.8 * 206.26480624709636 / 1.8, 1e-6);
}

TEST_F(PlateSolveServiceTest, NoScaleHintWithoutBothFocalLengthAndPixelSize)
{
    auto runner = std::make_unique<MockSolverProcessRunner>();
    auto* runner_ptr = runner.get();
    runner_ptr->queuedRuns.push_back({.exit_code = 0});
    runner_ptr->queuedReadFiles.push_back(makeSolvedHeaderBytes(320.0, 240.0, 120.5, -22.25, 0.0005));

    an::ui::PlateSolveService service(std::move(runner));

    an::ui::PlateSolveRequest solve = request(QStringLiteral("noscale.fit"), plainCards());
    solve.focalLengthMm = 500.0;

    an::ui::PlateSolveSettings settings;
    settings.astapPath = solverFile(QStringLiteral("astap_mock"));
    settings.useHints = true;

    CancellationToken token;
    const auto result = service.solve(solve, settings, token);

    ASSERT_TRUE(result.succeeded) << result.errorMessage.toStdString();
    ASSERT_EQ(runner_ptr->invocations.size(), 1u);
    EXPECT_FALSE(runner_ptr->invocations.front().arguments.contains(QStringLiteral("-fov")));
    EXPECT_FALSE(result.derivedFocalLengthMm.has_value());
}

TEST_F(PlateSolveServiceTest, ForceExternalSolveBypassesEmbeddedWcs)
{
    auto runner = std::make_unique<MockSolverProcessRunner>();
    auto* runner_ptr = runner.get();
    runner_ptr->queuedRuns.push_back({.exit_code = 0});
    runner_ptr->queuedReadFiles.push_back(makeSolvedHeaderBytes(320.0, 240.0, 130.0, 10.0, 0.0004));

    an::ui::PlateSolveService service(std::move(runner));

    an::ui::PlateSolveRequest solve =
        request(QStringLiteral("solved.fit"), tangentPlaneCards(northUpPlane(320.0, 240.0, 120.5, -22.25, 0.0005)));
    solve.forceExternalSolve = true;

    an::ui::PlateSolveSettings settings;
    settings.astapPath = solverFile(QStringLiteral("astap_mock"));

    CancellationToken token;
    const auto result = service.solve(solve, settings, token);

    ASSERT_TRUE(result.succeeded) << result.errorMessage.toStdString();
    EXPECT_EQ(result.solverUsed, QStringLiteral("ASTAP"));
    EXPECT_EQ(runner_ptr->invocations.size(), 1u);
    EXPECT_NEAR(result.fieldCenterRaDeg, 130.0, 2.0);
}

TEST_F(PlateSolveServiceTest, EmbeddedWcsReturnedWhenNotForced)
{
    auto runner = std::make_unique<MockSolverProcessRunner>();
    auto* runner_ptr = runner.get();

    an::ui::PlateSolveService service(std::move(runner));

    an::ui::PlateSolveRequest solve =
        request(QStringLiteral("solved.fit"), tangentPlaneCards(northUpPlane(320.0, 240.0, 120.5, -22.25, 0.0005)));
    solve.forceExternalSolve = false;

    an::ui::PlateSolveSettings settings;
    settings.astapPath = solverFile(QStringLiteral("astap_mock"));

    CancellationToken token;
    const auto result = service.solve(solve, settings, token);

    ASSERT_TRUE(result.succeeded) << result.errorMessage.toStdString();
    EXPECT_EQ(result.solverUsed, QStringLiteral("embedded WCS"));
    EXPECT_EQ(runner_ptr->invocations.size(), 0u);
    EXPECT_NEAR(result.fieldCenterRaDeg, 120.5, 0.1);
}

TEST_F(PlateSolveServiceTest, AnImageTheProductSolvedBeforeItStatedTheFrameKeepsItsSolution)
{
    // The product once kept its solvers' cards as written - an equinox and no frame - beside its solver card.
    std::vector<an::CodecCard> cards = tangentPlaneCards(northUpPlane(320.0, 240.0, 120.5, -22.25, 0.0005));
    addCard(cards, "EQUINOX", "2000.0");
    addCard(cards, "PLTSOLVR", "'ASTAP'");

    an::ui::PlateSolveService service(std::make_unique<MockSolverProcessRunner>());
    an::ui::PlateSolveSettings settings;
    settings.astapPath = solverFile(QStringLiteral("astap_mock"));
    CancellationToken token;
    const auto result = service.solve(request(QStringLiteral("solved_before.fit"), std::move(cards)), settings, token);

    ASSERT_TRUE(result.succeeded) << result.errorMessage.toStdString();
    EXPECT_EQ(result.solverUsed, QStringLiteral("embedded WCS"));
}

TEST_F(PlateSolveServiceTest, AnImageSolvedElsewhereWithTheJ2000EquinoxKeepsItsSolution)
{
    // What astrometry.net and ASTAP write into a file they solve: the J2000 equinox and no frame.
    std::vector<an::CodecCard> cards = tangentPlaneCards(northUpPlane(320.0, 240.0, 120.5, -22.25, 0.0005));
    addCard(cards, "EQUINOX", "2000.0");

    an::ui::PlateSolveService service(std::make_unique<MockSolverProcessRunner>());
    an::ui::PlateSolveSettings settings;
    settings.astapPath = solverFile(QStringLiteral("astap_mock"));
    CancellationToken token;
    const auto result =
        service.solve(request(QStringLiteral("solved_elsewhere.fit"), std::move(cards)), settings, token);

    ASSERT_TRUE(result.succeeded) << result.errorMessage.toStdString();
    EXPECT_EQ(result.solverUsed, QStringLiteral("embedded WCS"));
}

TEST_F(PlateSolveServiceTest, AnImageWhoseCardsStateFk5IsSolvedAgain)
{
    // A frame the cards state is never overridden: FK5 is refused by the bridge, so the solvers run.
    std::vector<an::CodecCard> cards = tangentPlaneCards(northUpPlane(320.0, 240.0, 120.5, -22.25, 0.0005));
    addCard(cards, "RADESYS", "'FK5'");
    addCard(cards, "EQUINOX", "2000.0");

    auto runner = std::make_unique<MockSolverProcessRunner>();
    auto* runner_ptr = runner.get();
    runner_ptr->queuedRuns.push_back({.exit_code = 0});
    runner_ptr->queuedReadFiles.push_back(makeSolvedHeaderBytes(320.0, 240.0, 120.5, -22.25, 0.0005));
    an::ui::PlateSolveService service(std::move(runner));
    an::ui::PlateSolveSettings settings;
    settings.astapPath = solverFile(QStringLiteral("astap_mock"));
    CancellationToken token;
    const auto result = service.solve(request(QStringLiteral("fk5.fit"), std::move(cards)), settings, token);

    ASSERT_TRUE(result.succeeded) << result.errorMessage.toStdString();
    EXPECT_EQ(result.solverUsed, QStringLiteral("ASTAP"));
    EXPECT_EQ(runner_ptr->invocations.size(), 1u);
}

TEST_F(PlateSolveServiceTest, AstrometryWebSkippedWhenApiKeyMissing)
{
    auto runner = std::make_unique<MockSolverProcessRunner>();
    auto* runner_ptr = runner.get();
    runner_ptr->queuedRuns.push_back({.exit_code = 0});
    runner_ptr->queuedReadFiles.push_back(makeSolvedHeaderBytes(320.0, 240.0, 120.5, -22.25, 0.0005));

    an::ui::PlateSolveService service(std::move(runner));

    an::ui::PlateSolveSettings settings;
    settings.defaultSolver = QStringLiteral("astrometry_web");
    settings.astrometryApiKey = QString();
    settings.astapPath = solverFile(QStringLiteral("astap_mock"));

    CancellationToken token;
    const auto result = service.solve(request(QStringLiteral("web_skip.fit"), plainCards()), settings, token);

    ASSERT_TRUE(result.succeeded) << result.errorMessage.toStdString();
    EXPECT_EQ(result.solverUsed, QStringLiteral("ASTAP"));
    ASSERT_EQ(runner_ptr->invocations.size(), 1u);
}

TEST_F(PlateSolveServiceTest, AllSolversFailIncludesWebDiagnostic)
{
    an::ui::PlateSolveService service(std::make_unique<MockSolverProcessRunner>());

    an::ui::PlateSolveSettings settings;
    settings.defaultSolver = QStringLiteral("astap");
    settings.astapPath = QString();
    settings.astrometryLocalPath = QString();
    settings.astrometryApiKey = QString();

    CancellationToken token;
    const auto result = service.solve(request(QStringLiteral("all_fail.fit"), plainCards()), settings, token);

    EXPECT_FALSE(result.succeeded);
    EXPECT_NE(result.errorMessage.indexOf(QStringLiteral("ASTAP")), -1);
    EXPECT_NE(result.errorMessage.indexOf(QStringLiteral("solve-field")), -1);
    EXPECT_NE(result.errorMessage.indexOf(QStringLiteral("web")), -1);
}

TEST_F(PlateSolveServiceTest, AnAsynchronousSolveReportsOnTheGuiThreadAndRunsItsPreparationFirst)
{
    auto runner = std::make_unique<MockSolverProcessRunner>();
    auto* runner_ptr = runner.get();
    runner_ptr->queuedRuns.push_back({.exit_code = 0});
    runner_ptr->queuedReadFiles.push_back(makeSolvedHeaderBytes(320.0, 240.0, 120.5, -22.25, 0.0005));
    an::ui::PlateSolveService service(std::move(runner));

    an::ui::PlateSolveSettings settings;
    settings.astapPath = solverFile(QStringLiteral("astap_mock"));

    std::optional<an::ui::PlateSolveResult> finished;
    bool reportedOnGuiThread = false;
    QObject::connect(&service, &an::ui::PlateSolveService::solveFinished, &service,
                     [&](const an::ui::PlateSolveResult& result) {
                         finished = result;
                         reportedOnGuiThread = QThread::currentThread() == QCoreApplication::instance()->thread();
                     });
    std::atomic<bool> prepared{false};
    std::atomic<bool> preparedBeforeRun{false};
    runner_ptr->onRun = [&](const CancellationToken&) { preparedBeforeRun = prepared.load(); };

    service.solveAsync(request(QStringLiteral("async.fit"), plainCards()), settings, [&prepared]() {
        prepared = true;
        return an::Status::success();
    });
    EXPECT_TRUE(service.isSolving());
    ASSERT_TRUE(an::tests::astrometry::pumpUntil([&finished]() { return finished.has_value(); }));

    EXPECT_TRUE(finished->succeeded) << finished->errorMessage.toStdString();
    EXPECT_TRUE(reportedOnGuiThread);
    EXPECT_TRUE(preparedBeforeRun.load());
    EXPECT_FALSE(service.isSolving());
}

TEST_F(PlateSolveServiceTest, APreparationThatFailsIsTheSolvesAnswer)
{
    auto runner = std::make_unique<MockSolverProcessRunner>();
    auto* runner_ptr = runner.get();
    an::ui::PlateSolveService service(std::move(runner));

    an::ui::PlateSolveSettings settings;
    settings.astapPath = solverFile(QStringLiteral("astap_mock"));
    std::optional<an::ui::PlateSolveResult> finished;
    QObject::connect(&service, &an::ui::PlateSolveService::solveFinished, &service,
                     [&finished](const an::ui::PlateSolveResult& result) { finished = result; });

    service.solveAsync(request(QStringLiteral("unprepared.fit"), plainCards()), settings, []() {
        return an::Status::failure(an::ErrorCode::ExternalFailure, "the staging file could not be written");
    });
    ASSERT_TRUE(an::tests::astrometry::pumpUntil([&finished]() { return finished.has_value(); }));

    EXPECT_FALSE(finished->succeeded);
    EXPECT_TRUE(finished->errorMessage.contains(QStringLiteral("staging file")));
    EXPECT_TRUE(runner_ptr->invocations.empty());
}

TEST_F(PlateSolveServiceTest, ASolveStillRunningWhenTheServiceGoesReportsToNobody)
{
    // The solve outlives the service: destroying it requests the stop and waits for nothing.
    std::atomic<bool> release{false};
    std::atomic<bool> ran{false};
    auto runner = std::make_unique<MockSolverProcessRunner>();
    runner->queuedRuns.push_back({.was_cancelled = true});
    runner->onRun = [&](const CancellationToken& token) {
        ran = true;
        while (!release.load() && !token.isCancellationRequested())
        {
            QThread::msleep(1);
        }
    };

    an::ui::PlateSolveSettings settings;
    settings.astapPath = solverFile(QStringLiteral("astap_mock"));
    {
        an::ui::PlateSolveService service(std::move(runner));
        service.solveAsync(request(QStringLiteral("orphan.fit"), plainCards()), settings);
        ASSERT_TRUE(an::tests::astrometry::pumpUntil([&ran]() { return ran.load(); }));
    }
    release = true;
    QThreadPool::globalInstance()->waitForDone();
    an::tests::astrometry::pumpQuietly();
    SUCCEED();
}
