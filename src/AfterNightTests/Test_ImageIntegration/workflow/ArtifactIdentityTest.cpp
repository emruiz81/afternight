// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Every derived family's semantic identity, and what it does to a project built on another engine: an artifact
/// stamped and recorded completely under a fingerprint with no family identity is recomputed once, because its record
/// was taken for another engine, and the identical run after that reuses what the first one wrote.

#include "Test_ImageIntegration/ImportSession.h"

#include "fixtures/common/IntegrationFrameFiles.h"

#include "processes/ImageIntegration/workflow/ArtifactSchema.h"
#include "processes/ImageIntegration/workflow/ArtifactTracker.h"
#include "processes/ImageIntegration/workflow/ExecutionPlanner.h"
#include "processes/ImageIntegration/workflow/ExecutionPreparationSnapshot.h"
#include "processes/ImageIntegration/workflow/ManifestHashing.h"
#include "processes/ImageIntegration/workflow/WorkflowManifest.h"
#include "processes/ImageIntegration/workflow/WorkflowRunStateStore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QSet>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <optional>
#include <string>

namespace
{

using namespace ImageIntegration::schema;

constexpr std::array kFamilies{ArtifactFamily::CalibrationMaster,
                               ArtifactFamily::CalibratedLight,
                               ArtifactFamily::StarDetections,
                               ArtifactFamily::Registration,
                               ArtifactFamily::ChromaticCorrection,
                               ArtifactFamily::RegisteredLight,
                               ArtifactFamily::NormalizationCoefficients,
                               ArtifactFamily::LocalNormalizationGrid,
                               ArtifactFamily::DrizzleDescriptor,
                               ArtifactFamily::DrizzleDensePayload,
                               ArtifactFamily::Stack,
                               ArtifactFamily::StackAuxiliaryMap,
                               ArtifactFamily::CometComposite};

TEST(ArtifactFamilyTest, EveryFamilyHasItsOwnIdentityUnderThisEngineGeneration)
{
    QSet<QString> names;
    for (const ArtifactFamily family : kFamilies)
    {
        const QJsonObject identity = artifactFamilyIdentity(family);
        EXPECT_EQ(identity.value(QStringLiteral("engineGeneration")).toString(),
                  QString::fromLatin1(kWorkflowEngineGeneration));
        EXPECT_GE(identity.value(QStringLiteral("revision")).toInt(), 1) << artifactFamilyName(family);
        names.insert(identity.value(QStringLiteral("family")).toString());
    }
    EXPECT_EQ(names.size(), static_cast<qsizetype>(kFamilies.size()));
}

TEST(ArtifactFamilyTest, ASidecarStatesOnlyItsOwnCurrentFamily)
{
    QJsonObject sidecar;
    insertArtifactFamilyIdentity(sidecar, ArtifactFamily::NormalizationCoefficients);
    EXPECT_TRUE(statesCurrentArtifactFamily(sidecar, ArtifactFamily::NormalizationCoefficients));
    EXPECT_FALSE(statesCurrentArtifactFamily(sidecar, ArtifactFamily::DrizzleDescriptor));

    // A sidecar that states a coefficient schema and no family.
    QJsonObject unfamilied;
    unfamilied.insert(QStringLiteral("schemaVersion"), 5);
    unfamilied.insert(QStringLiteral("coefficientSchemaVersion"), 3);
    EXPECT_FALSE(statesCurrentArtifactFamily(unfamilied, ArtifactFamily::NormalizationCoefficients));

    QJsonObject older = artifactFamilyIdentity(ArtifactFamily::NormalizationCoefficients);
    older.insert(QStringLiteral("revision"), 0);
    QJsonObject stale;
    stale.insert(QStringLiteral("artifactFamily"), older);
    EXPECT_FALSE(statesCurrentArtifactFamily(stale, ArtifactFamily::NormalizationCoefficients));
}

/// A bias group of two frames and its master, in an output directory of its own.
class OtherEngineMasterTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_root.isValid());
        auto session = an::tests::integration::openImportSession();
        ASSERT_TRUE(session.hasValue()) << session.status().message();
        m_session.emplace(std::move(session).value());

        const QDir root(m_root.path());
        an::tests::fixtures::AcquisitionFacts bias;
        bias.kind = "Bias Frame";
        for (const char* name : {"bias_001.fits", "bias_002.fits"})
        {
            const QString path = root.filePath(QStringLiteral("frames/%1").arg(QLatin1String(name)));
            writeFrame(an::tests::fixtures::acquisitionFrame(name, bias), path);
            m_group.inputFiles.emplace_back(path);
        }
        m_group.groupKey = GroupKey{QStringLiteral("BIAS|16x12|-|-|-|-|-")};
        m_group.outputFilename = QStringLiteral("master_bias.fits");

        m_manifest.outputDirectory = root.filePath(QStringLiteral("output"));
        ASSERT_TRUE(QDir().mkpath(m_manifest.outputDirectory));
        WorkflowManifest::CalibrationStage stage;
        stage.frameType = QStringLiteral("BIAS");
        stage.groups.push_back(m_group);
        m_manifest.biasStage = stage;
        m_masterPath = QDir(m_manifest.outputDirectory).filePath(m_group.outputFilename);
    }

    void writeFrame(an::tests::fixtures::FixtureImage frame, const QString& path,
                    const std::optional<std::string>& artifactId = std::nullopt)
    {
        if (artifactId.has_value())
        {
            frame.cards.push_back(an::tests::fixtures::FixtureCard{"ANARTID", *artifactId, "artifact identity"});
        }
        std::string error;
        ASSERT_TRUE(
            an::tests::fixtures::writeAcquisitionFrame(frame, std::filesystem::path(path.toStdWString()), error))
            << error;
    }

    [[nodiscard]] ExecutionPlan plan() const
    {
        const auto snapshot = ExecutionPreparationSnapshot::build(m_manifest);
        return ExecutionPlanner().buildPlan(m_manifest, *snapshot);
    }

    QTemporaryDir m_root;
    std::optional<an::consumer::EngineSession> m_session;
    WorkflowManifest m_manifest;
    WorkflowManifest::CalibrationStage::Group m_group;
    QString m_masterPath;
};

TEST_F(OtherEngineMasterTest, AMasterRecordedForAnotherEngineIsRecomputedOnceAndThenReused)
{
    // The master was written with its stamp and recorded completely, under a fingerprint of the same inputs and
    // settings but no family identity - one another engine took.
    an::tests::fixtures::AcquisitionFacts master;
    master.kind = "Master Bias";
    writeFrame(an::tests::fixtures::acquisitionFrame("master_bias.fits", master), m_masterPath, "other-master-bias");
    const QString otherFingerprint(64, QLatin1Char('7'));
    {
        ArtifactTracker tracker(m_manifest.outputDirectory, nullptr, sessionArtifactStampReader(*m_session));
        ASSERT_TRUE(tracker.recordArtifact(m_masterPath, otherFingerprint));
        const auto records = tracker.artifactRecords();
        ASSERT_EQ(records.size(), 1u);
        EXPECT_EQ(records.front().artifactStamp, QStringLiteral("other-master-bias"));
        EXPECT_TRUE(tracker.isArtifactValid(m_masterPath, otherFingerprint)) << "the record is complete";
        EXPECT_FALSE(tracker.isArtifactValid(m_masterPath, computeCalibrationGroupHash(m_manifest, m_group)));
    }
    ASSERT_TRUE(WorkflowRunStateStore(m_manifest.outputDirectory).saveLastSuccessfulManifest(m_manifest));

    const ExecutionPlan first = plan();
    EXPECT_EQ(first.mode, ExecutionMode::Incremental);
    ASSERT_EQ(first.biasGroupsToProcess.size(), 1u);
    EXPECT_EQ(first.biasGroupsToProcess.front(), m_group.groupKey);

    // The first run writes the master again and records it under the fingerprint taken now.
    an::tests::fixtures::AcquisitionFacts rewritten;
    rewritten.kind = "Master Bias";
    writeFrame(an::tests::fixtures::acquisitionFrame("master_bias.fits", rewritten), m_masterPath, "next-master-bias");
    {
        ArtifactTracker tracker(m_manifest.outputDirectory, nullptr, sessionArtifactStampReader(*m_session));
        ASSERT_TRUE(tracker.recordArtifact(m_masterPath, computeCalibrationGroupHash(m_manifest, m_group)));
    }

    const ExecutionPlan second = plan();
    EXPECT_EQ(second.mode, ExecutionMode::Incremental);
    EXPECT_TRUE(second.biasGroupsToProcess.empty());
}

TEST_F(OtherEngineMasterTest, ARestoredMasterIsReusedByTheStampItsHeaderCarries)
{
    an::tests::fixtures::AcquisitionFacts master;
    master.kind = "Master Bias";
    const auto frame = an::tests::fixtures::acquisitionFrame("master_bias.fits", master);
    writeFrame(frame, m_masterPath, "next-master-bias");
    const QString fingerprint = computeCalibrationGroupHash(m_manifest, m_group);
    ArtifactTracker tracker(m_manifest.outputDirectory, nullptr, sessionArtifactStampReader(*m_session));
    ASSERT_TRUE(tracker.recordArtifact(m_masterPath, fingerprint));
    const QDateTime modified = QFileInfo(m_masterPath).lastModified();

    // The same bytes restored under a fresh file identity, the modification time put back: the platform identity no
    // longer matches, so the header's stamp decides - a header read, not a payload digest.
    ASSERT_TRUE(QFile::remove(m_masterPath));
    writeFrame(frame, m_masterPath, "next-master-bias");
    {
        QFile restored(m_masterPath);
        ASSERT_TRUE(restored.open(QIODevice::ReadWrite));
        ASSERT_TRUE(restored.setFileTime(modified, QFileDevice::FileModificationTime));
    }
    EXPECT_TRUE(tracker.isArtifactValid(m_masterPath, fingerprint));

    // A rewrite that carries another stamp is not the recorded artifact, and a tracker that cannot read headers
    // cannot tell, so it recomputes.
    ASSERT_TRUE(QFile::remove(m_masterPath));
    writeFrame(frame, m_masterPath, "someone-else");
    {
        QFile rewritten(m_masterPath);
        ASSERT_TRUE(rewritten.open(QIODevice::ReadWrite));
        ASSERT_TRUE(rewritten.setFileTime(modified, QFileDevice::FileModificationTime));
    }
    EXPECT_FALSE(tracker.isArtifactValid(m_masterPath, fingerprint));
    ArtifactTracker blind(m_manifest.outputDirectory);
    EXPECT_FALSE(blind.isArtifactValid(m_masterPath, fingerprint));
}

} // namespace
