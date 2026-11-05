// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The SPCC catalog's region as the engine computes it over a solution - the field's centre and the cone that holds it
/// - and the chunks an installed catalog has and lacks for a region, selected by the engine's spectrum-catalog chunk
/// selection.

#include "Test_ControlsAstrometry/AstrometryTestSupport.h"

#include "app/SpccCatalogSettings.h"

#include <AfterNightEngine/Algorithms/Catalog/SpectrumCatalog.h>

#include <gtest/gtest.h>

#include <memory>
#include <optional>

namespace
{

using an::tests::astrometry::DirectExecution;
using an::tests::astrometry::northUpPlane;
using an::tests::astrometry::openExecution;
using an::tests::astrometry::solutionOf;
using an::tests::astrometry::tangentPlaneCards;

/// An installed regional catalog of two chunks of 64 tiles each - the first at the start of the sky, the second the
/// one M 31's tiles fall in - of which only the second is in the cache.
[[nodiscard]] an::ui::SpccCatalogDirectoryInspection twoChunkInspection()
{
    an::ui::SpccCatalogDirectoryInspection inspection;
    inspection.status = an::ui::SpccCatalogDirectoryStatus::Ready;
    inspection.catalogFamilyId = QStringLiteral("spcc_gaia_dr3_xp_sampled");
    inspection.manifestId = QStringLiteral("spcc-test");
    inspection.accessProfile = QStringLiteral("installed-regional-cache");
    inspection.rightsReviewStatus = QStringLiteral("approved-for-managed-download");
    inspection.chunkCount = 2;
    inspection.chunkTileRanges = {qMakePair(0, 64), qMakePair(2688, 64)};
    inspection.chunkSizeBytes = {1000, 2000};
    inspection.chunkFilePresent = {false, true};
    inspection.chunkFiles = {QStringLiteral("chunk_0000.anxp"), QStringLiteral("chunk_0001.anxp")};
    return inspection;
}

} // namespace

TEST(SpccCatalogSettings, TheRegionalRequestIsTheSolutionsFieldAndCone)
{
    const std::optional<an::WcsSolution> solution =
        solutionOf(tangentPlaneCards(northUpPlane(3124.5, 2088.5, 10.6847, 41.2692, 0.0005)), 6248u, 4176u);
    ASSERT_TRUE(solution.has_value());

    QString diagnostic;
    const std::optional<an::ui::SpccRegionalCatalogRequest> request =
        an::ui::makeSpccRegionalCatalogRequestFromWcs(*solution, 30.0, 16.0, QStringLiteral(" M31 Core "), &diagnostic);
    ASSERT_TRUE(request.has_value()) << diagnostic.toStdString();

    const an::Result<an::SkyCoordinate> center = solution->fieldCenter();
    const an::Result<double> radius = solution->queryRadiusDegrees(30.0);
    ASSERT_TRUE(center.hasValue());
    ASSERT_TRUE(radius.hasValue());
    EXPECT_DOUBLE_EQ(request->centerRaDeg, center->rightAscensionDegrees);
    EXPECT_DOUBLE_EQ(request->centerDecDeg, center->declinationDegrees);
    EXPECT_DOUBLE_EQ(request->radiusDeg, *radius);
    EXPECT_DOUBLE_EQ(request->magnitudeLimit, 16.0);
    EXPECT_EQ(request->imageWidth, 6248);
    EXPECT_EQ(request->imageHeight, 4176);
    EXPECT_EQ(request->targetName, QStringLiteral("M31 Core"));
    EXPECT_FALSE(request->createdAtUtc.isEmpty());
}

TEST(SpccCatalogSettings, TheAccessProfilesReadAsTheEnginesAndAnUnknownOneAsNone)
{
    EXPECT_EQ(an::ui::spccCatalogAccessProfileFromString(QStringLiteral("local-full")),
              std::optional(an::SpectrumCatalogAccessProfile::LocalFullInstall));
    EXPECT_EQ(an::ui::spccCatalogAccessProfileFromString(QStringLiteral("installed-regional-cache")),
              std::optional(an::SpectrumCatalogAccessProfile::InstalledRegionalCache));
    EXPECT_FALSE(an::ui::spccCatalogAccessProfileFromString(QStringLiteral("unknown")).has_value());
}

TEST(SpccCatalogSettings, AnInspectionIsTheEnginesManifest)
{
    const an::SpectrumCatalogManifest manifest = an::ui::spccCatalogManifestFromInspection(twoChunkInspection());
    EXPECT_EQ(manifest.manifestId, "spcc-test");
    EXPECT_EQ(manifest.accessProfile, an::SpectrumCatalogAccessProfile::InstalledRegionalCache);
    ASSERT_EQ(manifest.chunks.size(), 2u);
    EXPECT_EQ(manifest.chunks[1].firstTile, 2688);
    EXPECT_EQ(manifest.chunks[1].tileCount, 64);
    EXPECT_EQ(manifest.chunks[1].sizeBytes, 2000u);
}

TEST(SpccCatalogSettings, TheRegionsChunksAreSplitIntoCachedAndMissing)
{
    std::unique_ptr<DirectExecution> direct = openExecution();
    ASSERT_NE(direct, nullptr);

    const an::ui::SpccCatalogRequiredChunkAvailability m31 = an::ui::spccCatalogRequiredChunkAvailabilityForRegion(
        direct->execution(), twoChunkInspection(), 10.6847, 41.2692, 0.05);
    ASSERT_TRUE(m31.valid) << m31.diagnostic.toStdString();
    EXPECT_EQ(m31.requiredChunkIndices, QVector<int>{1});
    EXPECT_EQ(m31.cachedChunkIndices, QVector<int>{1});
    EXPECT_TRUE(m31.missingChunkIndices.isEmpty());
    EXPECT_TRUE(m31.allRequiredChunksCached());
}

TEST(SpccCatalogSettings, ARegionNoChunkDeclaresIsNotValid)
{
    std::unique_ptr<DirectExecution> direct = openExecution();
    ASSERT_NE(direct, nullptr);

    const an::ui::SpccCatalogRequiredChunkAvailability south = an::ui::spccCatalogRequiredChunkAvailabilityForRegion(
        direct->execution(), twoChunkInspection(), 200.0, -60.0, 0.05);
    EXPECT_FALSE(south.valid);
    EXPECT_FALSE(south.diagnostic.isEmpty());
}

TEST(SpccCatalogSettings, AnIncompleteInstallPlansNothing)
{
    std::unique_ptr<DirectExecution> direct = openExecution();
    ASSERT_NE(direct, nullptr);
    an::ui::SpccCatalogDirectoryInspection inspection = twoChunkInspection();
    inspection.status = an::ui::SpccCatalogDirectoryStatus::IncompleteInstall;

    const an::ui::SpccCatalogRequiredChunkAvailability availability =
        an::ui::spccCatalogRequiredChunkAvailabilityForRegion(direct->execution(), inspection, 10.6847, 41.2692, 0.05);
    EXPECT_FALSE(availability.valid);
    EXPECT_FALSE(availability.diagnostic.isEmpty());
}
