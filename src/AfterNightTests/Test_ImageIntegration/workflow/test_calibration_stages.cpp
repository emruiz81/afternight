// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The bias, dark and flat stages: each group's readable frames stacked into a Float32 master - a flat's
/// calibrated by its masters as they are read - published whole and stamped with the manifest entry it
/// satisfies; a frame that cannot be read skipped and named; a master that cannot be read failing its group and named;
/// the stage refused outside a run; and a planned run's reuse of a recorded master decided from a stat or a header
/// read, never from its samples.

#include "Test_ImageIntegration/workflow/CalibrationRun.h"

#include "fixtures/common/IntegrationFrameFiles.h"

#include "processes/ImageIntegration/core/CalibrationMasterDefaults.h"
#include "processes/ImageIntegration/core/CalibrationMasterSet.h"
#include "processes/ImageIntegration/model/FrameType.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/workflow/ArtifactTracker.h"
#include "processes/ImageIntegration/workflow/ExecutionPlanner.h"
#include "processes/ImageIntegration/workflow/ExecutionPreparationSnapshot.h"
#include "processes/ImageIntegration/workflow/FrameHeaderReader.h"
#include "processes/ImageIntegration/workflow/ManifestBuilder.h"
#include "processes/ImageIntegration/workflow/ManifestHashing.h"
#include "processes/ImageIntegration/workflow/WorkflowStageServices.h"
#include "processes/ImageIntegration/workflow/stages/BiasStackingStage.h"
#include "processes/ImageIntegration/workflow/stages/CalibrationStackInputs.h"
#include "processes/ImageIntegration/workflow/stages/DarkStackingStage.h"
#include "processes/ImageIntegration/workflow/stages/FlatStackingStage.h"
#include "processes/ImageIntegration/workflow/stages/LightsCalibrationStage.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSize>
#include <QTemporaryDir>
#include <QTimeZone>

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace
{

using an::tests::integration::CalibrationRun;
using an::tests::integration::fractionOf;
using an::tests::integration::readWrittenImage;
using an::tests::integration::writeCalibrationFrame;

class CalibrationStageTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_root.isValid());
        ASSERT_TRUE(m_run.valid());
        m_manifest.outputDirectory = QDir(m_root.path()).filePath(QStringLiteral("output"));
        m_manifest.executionId = QStringLiteral("20261009_120000-001");
        ASSERT_TRUE(QDir().mkpath(m_manifest.outputDirectory));
    }

    /// Writes frames of one kind at a level and returns their identities.
    std::vector<FrameId> writeFrames(const QString& prefix, const std::string& kind, double level, int count)
    {
        std::vector<FrameId> frames;
        for (int index = 1; index <= count; ++index)
        {
            const QString path = framePath(QStringLiteral("%1_%2.fits").arg(prefix).arg(index, 3, 10, QChar('0')));
            std::string error;
            EXPECT_TRUE(writeCalibrationFrame(path, kind, level, {}, error)) << error;
            frames.emplace_back(path);
        }
        return frames;
    }

    [[nodiscard]] QString framePath(const QString& name) const
    {
        return QDir(m_root.path()).filePath(QStringLiteral("frames/") + name);
    }

    [[nodiscard]] QString outputPath(const QString& name) const
    {
        return QDir(m_manifest.outputDirectory).filePath(name);
    }

    static WorkflowManifest::CalibrationStage::Group group(const QString& key, std::vector<FrameId> inputs,
                                                           const QString& output, FrameType type)
    {
        WorkflowManifest::CalibrationStage::Group made;
        made.groupKey = GroupKey{key};
        made.outputFilename = output;
        made.inputFiles = std::move(inputs);
        made.stacking = defaultCalibrationStackingSettings(type);
        return made;
    }

    void setBiasGroup(WorkflowManifest::CalibrationStage::Group made)
    {
        WorkflowManifest::CalibrationStage stage;
        stage.frameType = QStringLiteral("BIAS");
        stage.groups.push_back(std::move(made));
        m_manifest.biasStage = stage;
    }

    /// Every sample of a written master, expected to be one value.
    static void expectEverySample(const QString& path, double expected)
    {
        const auto written = readWrittenImage(path);
        ASSERT_TRUE(written.has_value()) << path.toStdString();
        EXPECT_EQ(written->card("BITPIX").value_or(""), "-32");
        for (std::uint32_t y = 0u; y < written->samples.height(); ++y)
        {
            for (std::uint32_t x = 0u; x < written->samples.width(); ++x)
            {
                ASSERT_NEAR(written->samples.value(x, y, 0u), expected, 1.0e-6) << x << "," << y;
            }
        }
    }

    QTemporaryDir m_root;
    CalibrationRun m_run;
    WorkflowManifest m_manifest;
};

TEST_F(CalibrationStageTest, BiasStackingWritesAFloatMasterStampedWithItsManifestEntry)
{
    setBiasGroup(group(QStringLiteral("BIAS|16x12"), writeFrames(QStringLiteral("bias"), "Bias Frame", 1000.0, 3),
                       QStringLiteral("master_bias.fit"), FrameType::Bias));
    BiasStackingStage stage;
    const StageResult result = m_run.run(stage, m_manifest);
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();

    const QString master = outputPath(QStringLiteral("master_bias.fit"));
    expectEverySample(master, fractionOf(1000.0));
    const auto written = readWrittenImage(master);
    ASSERT_TRUE(written.has_value());
    const QString hash = computeCalibrationGroupHash(m_manifest, m_manifest.biasStage->groups.front());
    const auto identity = an::tests::integration::writtenIdentity(m_run.session(), master);
    ASSERT_TRUE(identity.has_value());
    EXPECT_EQ(QString::fromStdString(identity->artifactId), hash.left(32));
    EXPECT_EQ(identity->runId, "20261009_120000-001");

    // The record follows the write and holds the stamp the header carries.
    ArtifactTracker tracker(m_manifest.outputDirectory, nullptr, sessionArtifactStampReader(m_run.session()));
    const auto records = tracker.artifactRecords();
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records.front().artifactStamp, hash.left(32));
    EXPECT_TRUE(tracker.isArtifactValid(master, hash));
}

TEST_F(CalibrationStageTest, TheMasterIsReusedByTheIdenticalRunAndByTheStampOfARestoredCopy)
{
    WorkflowManifest::CalibrationStage::Group made =
        group(QStringLiteral("BIAS|16x12"), writeFrames(QStringLiteral("bias"), "Bias Frame", 1000.0, 3),
              QStringLiteral("master_bias.fit"), FrameType::Bias);
    made.skipIfExists = true;
    setBiasGroup(made);
    BiasStackingStage stage;
    ASSERT_TRUE(m_run.run(stage, m_manifest).isSuccess());
    const QString master = outputPath(QStringLiteral("master_bias.fit"));
    const QDateTime written = QFileInfo(master).lastModified();

    EXPECT_TRUE(stage.shouldSkip(m_manifest)) << "the identical run reuses the master it published";
    ASSERT_TRUE(m_run.run(stage, m_manifest).isSuccess());
    EXPECT_EQ(QFileInfo(master).lastModified(), written) << "the master was written again";

    // The same bytes restored under a fresh file identity: the stamp the stage wrote decides.
    QFile file(master);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const QByteArray bytes = file.readAll();
    file.close();
    ASSERT_TRUE(QFile::remove(master));
    QFile restored(master);
    ASSERT_TRUE(restored.open(QIODevice::WriteOnly));
    ASSERT_EQ(restored.write(bytes), bytes.size());
    ASSERT_TRUE(restored.flush());
    ASSERT_TRUE(restored.setFileTime(written, QFileDevice::FileModificationTime));
    restored.close();
    ArtifactTracker tracker(m_manifest.outputDirectory, nullptr, sessionArtifactStampReader(m_run.session()));
    EXPECT_TRUE(
        tracker.isArtifactValid(master, computeCalibrationGroupHash(m_manifest, m_manifest.biasStage->groups.front())));
}

/// A bias master a run published and recorded, and the preparation snapshot a later planned run is made from.
class PlannedMasterReuseTest : public CalibrationStageTest
{
protected:
    /// Runs the bias stage over three frames into a master a later run may reuse.
    void publishMaster()
    {
        WorkflowManifest::CalibrationStage::Group made =
            group(QStringLiteral("BIAS|16x12"), writeFrames(QStringLiteral("bias"), "Bias Frame", 1000.0, 3),
                  QStringLiteral("master_bias.fit"), FrameType::Bias);
        made.skipIfExists = true;
        setBiasGroup(made);
        BiasStackingStage stage;
        const StageResult result = m_run.run(stage, m_manifest);
        ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
        m_master = outputPath(QStringLiteral("master_bias.fit"));
        m_published = QFileInfo(m_master).lastModified();

        // The record a run makes holds the master's digest beside its stamp and its platform identity.
        ArtifactTracker tracker(m_manifest.outputDirectory, nullptr, sessionArtifactStampReader(m_run.session()));
        const auto records = tracker.artifactRecords();
        ASSERT_EQ(records.size(), 1u);
        ASSERT_EQ(records.front().contentDigest.size(), 64);
        ASSERT_FALSE(records.front().artifactStamp.isEmpty());
        ASSERT_FALSE(records.front().identityToken.isEmpty());
        m_recordedIdentity = records.front().identityToken;
    }

    [[nodiscard]] QString fingerprint() const
    {
        return computeCalibrationGroupHash(m_manifest, m_manifest.biasStage->groups.front());
    }

    /// The snapshot a planned run is prepared from, reading headers through the run's session unless told not to.
    [[nodiscard]] std::shared_ptr<const ExecutionPreparationSnapshot> prepare(const bool readHeaders = true) const
    {
        return ExecutionPreparationSnapshot::build(m_manifest, m_manifest, {},
                                                   readHeaders
                                                       ? ImageIntegration::sessionFrameHeaderReader(m_run.session())
                                                       : ImageIntegration::FrameHeaderReader{});
    }

    /// Whether the plan made from a snapshot stacks the bias group again.
    [[nodiscard]] bool planRestacks(const ExecutionPreparationSnapshot& snapshot) const
    {
        return !ExecutionPlanner().buildPlan(m_manifest, snapshot).biasGroupsToProcess.empty();
    }

    [[nodiscard]] QByteArray masterBytes() const
    {
        QFile file(m_master);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

    /// The offset of the first sample: past the header's `END` card and the rest of its 2880-byte block.
    [[nodiscard]] static qsizetype firstSampleOffset(const QByteArray& bytes)
    {
        for (qsizetype card = 0; card + 80 <= bytes.size(); card += 80)
        {
            if (bytes.mid(card, 8) == QByteArrayLiteral("END     "))
            {
                return ((card + 80 + 2879) / 2880) * 2880;
            }
        }
        return -1;
    }

    /// Puts `bytes` at the master's path under a fresh file identity with its modification time put back, as a
    /// backup restore or a sync client does.
    void restoreMaster(const QByteArray& bytes) const
    {
        ASSERT_TRUE(QFile::remove(m_master));
        QFile restored(m_master);
        ASSERT_TRUE(restored.open(QIODevice::WriteOnly));
        ASSERT_EQ(restored.write(bytes), bytes.size());
        ASSERT_TRUE(restored.flush());
        ASSERT_TRUE(restored.setFileTime(m_published, QFileDevice::FileModificationTime));
        restored.close();
        ASSERT_EQ(QFileInfo(m_master).lastModified().toMSecsSinceEpoch(), m_published.toMSecsSinceEpoch());
        ASSERT_NE(ArtifactTracker::fileIdentityForPath(m_master), m_recordedIdentity);
    }

    QString m_master;
    QDateTime m_published;
    QString m_recordedIdentity;
};

TEST_F(PlannedMasterReuseTest, APlanDecidesARecordedMasterWithoutReadingItsSamples)
{
    ASSERT_NO_FATAL_FAILURE(publishMaster());

    const auto snapshot = prepare();
    EXPECT_EQ(snapshot->diagnostics().bytesHashed, 0u) << "preparing the plan read the master's payload to rehash it";
    EXPECT_EQ(snapshot->diagnostics().strictHeaderReads, 0u) << "an untouched master is decided by its stat";
    EXPECT_TRUE(snapshot->isArtifactValid(m_master, fingerprint(), PreparationArtifactValidation::StrictContent));
    EXPECT_FALSE(planRestacks(*snapshot));

    BiasStackingStage stage;
    stage.setExecutionPreparationSnapshot(snapshot);
    EXPECT_TRUE(stage.shouldSkip(m_manifest));
}

TEST_F(PlannedMasterReuseTest, TheLaunchCheckHoldsARecordedMasterToItsStatAndReadsNoneOfItsSamples)
{
    ASSERT_NO_FATAL_FAILURE(publishMaster());
    const auto snapshot = prepare();
    ASSERT_TRUE(snapshot->matchesCurrentFacts());
    const ExecutionPreparationSnapshot::FileStamp* planned = snapshot->fileStamp(m_master);
    ASSERT_NE(planned, nullptr);
    ASSERT_FALSE(planned->identityToken.isEmpty());
    const QString plannedPath = planned->canonicalPath;
    const QString plannedIdentity = planned->identityToken;

    // The master's first sample rewritten in place, its size and modification time put back.
    const QByteArray original = masterBytes();
    const qsizetype offset = firstSampleOffset(original);
    ASSERT_GT(offset, 0);
    ASSERT_LT(offset + 4, original.size());
    {
        QFile file(m_master);
        ASSERT_TRUE(file.open(QIODevice::ReadWrite));
        ASSERT_TRUE(file.seek(offset + 3));
        ASSERT_EQ(file.write(QByteArray(1, static_cast<char>(original.at(offset + 3) ^ 0x01))), 1);
    }
    {
        QFile file(m_master);
        ASSERT_TRUE(file.open(QIODevice::ReadWrite));
        ASSERT_TRUE(file.setFileTime(m_published, QFileDevice::FileModificationTime));
    }
    ASSERT_EQ(QFileInfo(m_master).size(), original.size());
    ASSERT_EQ(QFileInfo(m_master).lastModified().toMSecsSinceEpoch(), m_published.toMSecsSinceEpoch());
    ASSERT_NE(masterBytes(), original);

    // The rewrite moved the master's platform identity, which the check before the stages start sees. Told the
    // identities the plan was made on, it is the stat it is - it does not read the samples again to compare them.
    EXPECT_FALSE(snapshot->matchesCurrentFacts());
    const auto identityAsPlanned = [&plannedPath, &plannedIdentity](const QString& path) {
        return path == plannedPath ? plannedIdentity : ArtifactTracker::fileIdentityForPath(path);
    };
    EXPECT_TRUE(snapshot->matchesCurrentFacts({}, identityAsPlanned))
        << "the launch check read the master's payload again to rehash it";
}

TEST_F(PlannedMasterReuseTest, AMasterRestoredUnderAFreshIdentityIsReusedByTheIdentityItsHeaderCarries)
{
    ASSERT_NO_FATAL_FAILURE(publishMaster());
    ASSERT_NO_FATAL_FAILURE(restoreMaster(masterBytes()));

    const auto snapshot = prepare();
    EXPECT_TRUE(snapshot->isArtifactValid(m_master, fingerprint(), PreparationArtifactValidation::StrictContent));
    EXPECT_FALSE(planRestacks(*snapshot));
    EXPECT_EQ(snapshot->diagnostics().strictHeaderReads, 1u) << "the restored master's header decides it";
    EXPECT_EQ(snapshot->diagnostics().bytesHashed, 0u) << "the restored master's payload was read to rehash it";
}

TEST_F(PlannedMasterReuseTest, AMasterWhoseHeaderDoesNotStateItsIdentityIsRecomputed)
{
    ASSERT_NO_FATAL_FAILURE(publishMaster());
    const QByteArray original = masterBytes();

    // Restored with the same bytes but planned without a header reader: nothing can say it is the recorded master.
    ASSERT_NO_FATAL_FAILURE(restoreMaster(original));
    const auto unread = prepare(false);
    EXPECT_FALSE(unread->isArtifactValid(m_master, fingerprint(), PreparationArtifactValidation::StrictContent))
        << "a master whose header was not read was reused by its digest";
    EXPECT_TRUE(planRestacks(*unread));

    // A rewrite that dropped the identity card, and one whose card names another identity, at the same size.
    const qsizetype card = original.indexOf("ANARTID = '");
    ASSERT_GE(card, 0);
    QByteArray dropped = original;
    dropped.replace(card, 8, "ANARTIX ");
    QByteArray other = original;
    other[card + 11] = other.at(card + 11) == 'z' ? 'y' : 'z';
    for (const QByteArray& rewritten : {dropped, other})
    {
        ASSERT_NO_FATAL_FAILURE(restoreMaster(rewritten));
        const auto snapshot = prepare();
        EXPECT_FALSE(snapshot->isArtifactValid(m_master, fingerprint(), PreparationArtifactValidation::StrictContent));
        EXPECT_TRUE(planRestacks(*snapshot));
    }
}

TEST_F(PlannedMasterReuseTest, AMasterRecordHoldingADigestAndNoStampIsRecomputedOnceTheMasterMoves)
{
    ASSERT_NO_FATAL_FAILURE(publishMaster());

    // The record made again by a tracker that reads no header: the master's digest and platform identity, no stamp.
    {
        ArtifactTracker blind(m_manifest.outputDirectory);
        const auto committed = FinalCommittedArtifact::fromFile(m_master);
        ASSERT_TRUE(committed.has_value());
        ASSERT_TRUE(blind.recordCurrentRunCommittedArtifact(*committed, fingerprint()));
        const auto records = blind.artifactRecords();
        ASSERT_EQ(records.size(), 1u);
        ASSERT_EQ(records.front().contentDigest.size(), 64);
        ASSERT_TRUE(records.front().artifactStamp.isEmpty());
    }
    EXPECT_TRUE(prepare()->isArtifactValid(m_master, fingerprint(), PreparationArtifactValidation::StrictContent))
        << "an untouched master is reused by its stat";

    // The same bytes restored under a fresh identity: no stamp is left to decide it, and the digest decides nothing.
    ASSERT_NO_FATAL_FAILURE(restoreMaster(masterBytes()));
    const auto snapshot = prepare();
    EXPECT_FALSE(snapshot->isArtifactValid(m_master, fingerprint(), PreparationArtifactValidation::StrictContent))
        << "the plan reused a moved master by its digest";
    EXPECT_TRUE(planRestacks(*snapshot));
    EXPECT_EQ(snapshot->diagnostics().bytesHashed, 0u);

    WorkflowExecutionCounterAccumulator counters;
    ArtifactTracker tracker(m_manifest.outputDirectory, &counters, sessionArtifactStampReader(m_run.session()));
    EXPECT_FALSE(tracker.isArtifactValid(m_master, fingerprint())) << "the stage reused a moved master by its digest";
    EXPECT_EQ(counters.snapshot().bytesHashed(), 0u);
}

TEST_F(CalibrationStageTest, AnUnreadableFrameIsSkippedAndNamedWhenEnoughFramesRemain)
{
    std::vector<FrameId> frames = writeFrames(QStringLiteral("bias"), "Bias Frame", 1000.0, 3);
    const QString corrupt = framePath(QStringLiteral("bias_corrupt.fits"));
    {
        QFile file(corrupt);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write("not an image");
    }
    frames.emplace_back(corrupt);
    setBiasGroup(group(QStringLiteral("BIAS|16x12"), frames, QStringLiteral("master_bias.fit"), FrameType::Bias));

    BiasStackingStage stage;
    const StageResult result = m_run.run(stage, m_manifest);
    ASSERT_TRUE(result.isPartialSuccess()) << result.error.toLogString().toStdString();
    ASSERT_EQ(result.errorReport.errors.size(), 1u);
    EXPECT_EQ(result.errorReport.errors.front().filePath(), FrameId(corrupt).canonicalPath());
    expectEverySample(outputPath(QStringLiteral("master_bias.fit")), fractionOf(1000.0));
}

TEST_F(CalibrationStageTest, AFrameOfAnotherSizeIsSkippedAndNamed)
{
    std::vector<FrameId> frames = writeFrames(QStringLiteral("bias"), "Bias Frame", 1000.0, 3);
    an::tests::fixtures::AcquisitionFacts facts;
    facts.kind = "Bias Frame";
    const QString wide = framePath(QStringLiteral("bias_wide.fits"));
    std::string error;
    ASSERT_TRUE(
        an::tests::fixtures::writeAcquisitionFrame(an::tests::fixtures::acquisitionFrame("wide", facts, 20u, 12u),
                                                   std::filesystem::path(wide.toStdWString()), error))
        << error;
    frames.insert(frames.begin() + 1, FrameId(wide));
    setBiasGroup(group(QStringLiteral("BIAS|16x12"), frames, QStringLiteral("master_bias.fit"), FrameType::Bias));

    BiasStackingStage stage;
    const StageResult result = m_run.run(stage, m_manifest);
    ASSERT_TRUE(result.isPartialSuccess()) << result.error.toLogString().toStdString();
    ASSERT_EQ(result.errorReport.errors.size(), 1u);
    EXPECT_EQ(result.errorReport.errors.front().filePath(), FrameId(wide).canonicalPath());
    expectEverySample(outputPath(QStringLiteral("master_bias.fit")), fractionOf(1000.0));
}

TEST_F(CalibrationStageTest, TooFewReadableFramesFailTheGroup)
{
    std::vector<FrameId> frames = writeFrames(QStringLiteral("bias"), "Bias Frame", 1000.0, 1);
    const QString corrupt = framePath(QStringLiteral("bias_corrupt.fits"));
    {
        QFile file(corrupt);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write("SIMPLE  =                    T");
    }
    frames.emplace_back(corrupt);
    setBiasGroup(group(QStringLiteral("BIAS|16x12"), frames, QStringLiteral("master_bias.fit"), FrameType::Bias));

    BiasStackingStage stage;
    const StageResult result = m_run.run(stage, m_manifest);
    ASSERT_TRUE(result.isFailure());
    EXPECT_EQ(result.error.code(), IntegrationErrorCode::InsufficientFrames);
    EXPECT_EQ(result.errorReport.errors.size(), 1u);
    EXPECT_FALSE(QFileInfo::exists(outputPath(QStringLiteral("master_bias.fit"))));
}

TEST_F(CalibrationStageTest, ARejectionTwoFramesCannotApplyIsLeftOutAndSaid)
{
    setBiasGroup(group(QStringLiteral("BIAS|16x12"), writeFrames(QStringLiteral("bias"), "Bias Frame", 1000.0, 2),
                       QStringLiteral("master_bias.fit"), FrameType::Bias));
    BiasStackingStage stage;
    const StageResult result = m_run.run(stage, m_manifest);
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    ASSERT_EQ(result.warnings.size(), 1);
    EXPECT_TRUE(result.warnings.front().contains(QStringLiteral("needs at least 3")))
        << result.warnings.front().toStdString();
    expectEverySample(outputPath(QStringLiteral("master_bias.fit")), fractionOf(1000.0));
}

TEST_F(CalibrationStageTest, ASingleFrameGroupPublishesItsFrameAsAStampedMaster)
{
    setBiasGroup(group(QStringLiteral("BIAS|16x12"), writeFrames(QStringLiteral("bias"), "Bias Frame", 1234.0, 1),
                       QStringLiteral("master_bias.fit"), FrameType::Bias));
    BiasStackingStage stage;
    const StageResult result = m_run.run(stage, m_manifest);
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    const QString master = outputPath(QStringLiteral("master_bias.fit"));
    expectEverySample(master, fractionOf(1234.0));
    EXPECT_TRUE(an::tests::integration::writtenIdentity(m_run.session(), master).has_value());
}

TEST_F(CalibrationStageTest, AnExistingMasterIsUsedAsItIs)
{
    WorkflowManifest::CalibrationStage::Group made =
        group(QStringLiteral("BIAS|16x12"), writeFrames(QStringLiteral("bias"), "Bias Frame", 1000.0, 3),
              QStringLiteral("master_bias.fit"), FrameType::Bias);
    made.existingMaster = FrameId(framePath(QStringLiteral("bias_001.fits")));
    setBiasGroup(made);
    BiasStackingStage stage;
    EXPECT_TRUE(stage.shouldSkip(m_manifest));
    ASSERT_TRUE(m_run.run(stage, m_manifest).isSuccess());
    EXPECT_FALSE(QFileInfo::exists(outputPath(QStringLiteral("master_bias.fit"))));
}

// A master dark is stacked from its frames as they are, holding their bias, in CCD mode as in standard mode: CCD
// mode subtracts the bias from the master once, where a light is calibrated.
TEST_F(CalibrationStageTest, DarkStackingStacksTheFramesAsTheyAre)
{
    WorkflowManifest::CalibrationStage stage;
    stage.frameType = QStringLiteral("DARK");
    stage.groups.push_back(group(QStringLiteral("DARK|16x12|300s"),
                                 writeFrames(QStringLiteral("dark"), "Dark Frame", 1500.0, 3),
                                 QStringLiteral("master_dark.fit"), FrameType::Dark));
    m_manifest.darkStage = stage;
    DarkStackingStage dark;
    ASSERT_TRUE(m_run.run(dark, m_manifest).isSuccess());
    expectEverySample(outputPath(QStringLiteral("master_dark.fit")), fractionOf(1500.0));
}

TEST_F(CalibrationStageTest, FlatStackingSubtractsTheBiasAndTheDarkAndNormalizesMultiplicatively)
{
    const QString bias = framePath(QStringLiteral("master_bias.fits"));
    const QString darkMaster = framePath(QStringLiteral("master_dark.fits"));
    std::string error;
    ASSERT_TRUE(writeCalibrationFrame(bias, "Bias Frame", 1000.0, {}, error)) << error;
    ASSERT_TRUE(writeCalibrationFrame(darkMaster, "Dark Frame", 200.0, {}, error)) << error;
    WorkflowManifest::CalibrationStage::Group made =
        group(QStringLiteral("FLAT|16x12|L"), writeFrames(QStringLiteral("flat"), "Flat Field", 30000.0, 3),
              QStringLiteral("master_flat.fit"), FrameType::Flat);
    made.biasMaster = FrameId(bias);
    made.darkMaster = FrameId(darkMaster);
    WorkflowManifest::CalibrationStage stage;
    stage.frameType = QStringLiteral("FLAT");
    stage.groups.push_back(made);
    m_manifest.flatStage = stage;

    FlatStackingStage flat;
    const StageResult result = m_run.run(flat, m_manifest);
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    expectEverySample(outputPath(QStringLiteral("master_flat.fit")),
                      fractionOf(30000.0) - fractionOf(1000.0) - fractionOf(200.0));
}

TEST_F(CalibrationStageTest, AFlatGroupOfOneFrameIsCalibratedByItsMasters)
{
    const QString bias = framePath(QStringLiteral("master_bias.fits"));
    std::string error;
    ASSERT_TRUE(writeCalibrationFrame(bias, "Bias Frame", 1000.0, {}, error)) << error;
    WorkflowManifest::CalibrationStage::Group made =
        group(QStringLiteral("FLAT|16x12|L"), writeFrames(QStringLiteral("flat"), "Flat Field", 30000.0, 1),
              QStringLiteral("master_flat.fit"), FrameType::Flat);
    made.biasMaster = FrameId(bias);
    WorkflowManifest::CalibrationStage stage;
    stage.frameType = QStringLiteral("FLAT");
    stage.groups.push_back(made);
    m_manifest.flatStage = stage;

    FlatStackingStage flat;
    const StageResult result = m_run.run(flat, m_manifest);
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    expectEverySample(outputPath(QStringLiteral("master_flat.fit")), fractionOf(30000.0) - fractionOf(1000.0));
}

TEST_F(CalibrationStageTest, AnUncalibratedFlatGroupSaysSo)
{
    WorkflowManifest::CalibrationStage stage;
    stage.frameType = QStringLiteral("FLAT");
    stage.groups.push_back(group(QStringLiteral("FLAT|16x12|L"),
                                 writeFrames(QStringLiteral("flat"), "Flat Field", 30000.0, 3),
                                 QStringLiteral("master_flat.fit"), FrameType::Flat));
    m_manifest.flatStage = stage;
    FlatStackingStage flat;
    const StageResult result = m_run.run(flat, m_manifest);
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    ASSERT_FALSE(result.warnings.isEmpty());
    EXPECT_TRUE(result.warnings.front().contains(QStringLiteral("master flat will be uncalibrated")));
    expectEverySample(outputPath(QStringLiteral("master_flat.fit")), fractionOf(30000.0));
}

// A bias or dark master is combined without normalization. A group that still states one - saved before the dialog
// fixed the choice at None - has it left out, and the stage says so as it says a weighting it does not apply.
TEST_F(CalibrationStageTest, ANormalizationABiasGroupStatesIsLeftOutAndSaid)
{
    WorkflowManifest::CalibrationStage::Group made =
        group(QStringLiteral("BIAS|16x12"), writeFrames(QStringLiteral("bias"), "Bias Frame", 1000.0, 3),
              QStringLiteral("master_bias.fit"), FrameType::Bias);
    made.stacking.normalizationMode = NormalizationMode::Global;
    made.stacking.globalNormalizationMethod = GlobalNormalizationMethod::AdditiveScaling;
    setBiasGroup(made);
    BiasStackingStage stage;
    const StageResult result = m_run.run(stage, m_manifest);
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    ASSERT_EQ(result.warnings.size(), 1);
    EXPECT_TRUE(result.warnings.front().contains(QStringLiteral("without normalization")))
        << result.warnings.front().toStdString();
    expectEverySample(outputPath(QStringLiteral("master_bias.fit")), fractionOf(1000.0));
}

// The manifest keeps a normalization a dark group still states beside the settings its master is made with; the stage
// says it leaves it out by its name, not as the disabled local normalization the master's own settings hold.
TEST_F(CalibrationStageTest, ALocalNormalizationADarkGroupStatesIsSaidByItsName)
{
    WorkflowManifest::CalibrationStage::Group made =
        group(QStringLiteral("DARK|16x12"), writeFrames(QStringLiteral("dark"), "Dark Frame", 1200.0, 3),
              QStringLiteral("master_dark.fit"), FrameType::Dark);
    ASSERT_EQ(made.stacking.normalizationMode, NormalizationMode::None);
    made.statedNormalization = NormalizationMode::Local;
    WorkflowManifest::CalibrationStage darks;
    darks.frameType = QStringLiteral("DARK");
    darks.groups.push_back(std::move(made));
    m_manifest.darkStage = darks;
    DarkStackingStage stage;
    const StageResult result = m_run.run(stage, m_manifest);
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    ASSERT_EQ(result.warnings.size(), 1);
    EXPECT_TRUE(result.warnings.front().contains(QStringLiteral("group's Local normalization is not applied")))
        << result.warnings.front().toStdString();
    EXPECT_FALSE(result.warnings.front().contains(QStringLiteral("disabled"))) << result.warnings.front().toStdString();
    expectEverySample(outputPath(QStringLiteral("master_dark.fit")), fractionOf(1200.0));
}

/// The masters a flat group or a light is calibrated with, as the manifest a model builds names them, and what the
/// stages make with them: bias frames, darks, flats and lights, each frame a file written at a stated code.
class FlatCalibrationRuleTest : public CalibrationStageTest
{
protected:
    void SetUp() override
    {
        CalibrationStageTest::SetUp();
        m_model.setOutputDirectory(m_manifest.outputDirectory);
    }

    /// Writes three frames of one kind at a level and adds them to the model with the exposure and filter stated.
    void addFrames(const FrameType type, const QString& prefix, const std::string& kind, const double level,
                   const double exposure, const QString& filter)
    {
        std::vector<FrameInfo> frames;
        for (const FrameId& frame : writeFrames(prefix, kind, level, 3))
        {
            const FrameMetadata metadata(
                type,
                QSize(an::tests::integration::kCalibrationFrameWidth, an::tests::integration::kCalibrationFrameHeight),
                exposure, 0.0, 100, 10, filter, QStringLiteral("M42"),
                QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC), QStringLiteral("Default"));
            frames.emplace_back(frame, frame.canonicalPath(), metadata, QStringLiteral("2026-10-27"));
        }
        m_model.addFrames(type, frames);
    }

    /// Subtracts the bias from the darks - CCD mode - or not.
    void useCcdMode(const bool ccdMode)
    {
        CalibrationMatchingPolicy policy = m_model.calibrationMatchingPolicy();
        policy.subtractBiasFromDark = ccdMode;
        m_model.setCalibrationMatchingPolicy(policy);
    }

    /// The manifest the model builds, run by this fixture's run.
    [[nodiscard]] WorkflowManifest buildManifest() const
    {
        WorkflowManifest manifest = ManifestBuilder(m_model).build();
        manifest.executionId = m_manifest.executionId;
        return manifest;
    }

    /// Runs stages over a manifest, in order, each expected to succeed.
    void runStages(const WorkflowManifest& manifest, const std::initializer_list<IWorkflowStage*> stages)
    {
        for (IWorkflowStage* stage : stages)
        {
            const StageResult result = m_run.run(*stage, manifest);
            EXPECT_TRUE(result.isSuccess())
                << stage->name().toStdString() << ": " << result.error.toLogString().toStdString();
        }
    }

    /// Builds the manifest of bias frames at 1000, darks at 1200 - the bias and 200 of thermal signal - taken at the
    /// flats' exposure, flats at 30000 and lights, in CCD mode or not, and runs its bias, dark and flat stages.
    WorkflowManifest stackTheMasters(const bool ccdMode)
    {
        useCcdMode(ccdMode);
        addFrames(FrameType::Bias, QStringLiteral("bias"), "Bias Frame", 1000.0, 0.0, QString());
        addFrames(FrameType::Dark, QStringLiteral("dark"), "Dark Frame", 1200.0, 2.0, QString());
        addFrames(FrameType::Flat, QStringLiteral("flat"), "Flat Field", 30000.0, 2.0, QStringLiteral("L"));
        addFrames(FrameType::Light, QStringLiteral("light"), "Light Frame", 2000.0, 2.0, QStringLiteral("L"));

        const WorkflowManifest manifest = buildManifest();
        BiasStackingStage bias;
        DarkStackingStage dark;
        FlatStackingStage flat;
        runStages(manifest, {&bias, &dark, &flat});
        return manifest;
    }

    IntegrationDataModel m_model;
};

// In standard mode the dark holds the bias, so a flat is calibrated by its dark alone, as a light is. Subtracting the
// bias beside the dark took the bias out of every flat twice, and the master flat's profile with it.
TEST_F(FlatCalibrationRuleTest, AStandardModeFlatIsCalibratedByItsDarkAlone)
{
    const WorkflowManifest manifest = stackTheMasters(false);
    ASSERT_TRUE(manifest.flatStage.has_value());
    ASSERT_EQ(manifest.flatStage->groups.size(), 1u);
    const WorkflowManifest::CalibrationStage::Group& flat = manifest.flatStage->groups.front();
    EXPECT_TRUE(flat.darkMaster.has_value());
    EXPECT_FALSE(flat.biasMaster.has_value()) << "the dark holds the bias";
    expectEverySample(outputPath(flat.outputFilename), fractionOf(30000.0) - fractionOf(1200.0));
}

// In CCD mode a master dark the run stacks holds its bias, as a supplied one does, so a flat is calibrated by its dark
// alone there too: taking the bias from the flat and from the dark leaves the same flat at its factor of one.
TEST_F(FlatCalibrationRuleTest, ACcdModeFlatIsCalibratedByItsDarkWhichHoldsTheBias)
{
    const WorkflowManifest manifest = stackTheMasters(true);
    ASSERT_TRUE(manifest.darkStage.has_value() && manifest.flatStage.has_value());
    ASSERT_EQ(manifest.flatStage->groups.size(), 1u);
    const WorkflowManifest::CalibrationStage::Group& flat = manifest.flatStage->groups.front();
    EXPECT_FALSE(flat.biasMaster.has_value()) << "the dark holds the bias";
    EXPECT_TRUE(flat.darkMaster.has_value());
    expectEverySample(outputPath(manifest.darkStage->groups.front().outputFilename), fractionOf(1200.0));
    expectEverySample(outputPath(flat.outputFilename), fractionOf(30000.0) - fractionOf(1200.0));
}

// In CCD mode the bias is taken from a light and from its dark once each, so the dark's factor scales its thermal
// signal alone: the light at 2000 over darks at 1200 and a bias at 1000, the factor one half, is 900. The dark stage
// took the bias from every dark frame and the light's calibration took it from that master again, which left the bias
// in every light - 1800 at the factor of one, where 800 is right.
TEST_F(FlatCalibrationRuleTest, ACcdModeLightLosesItsBiasOnceAndItsDarkFactorScalesTheThermalSignal)
{
    useCcdMode(true);
    m_model.setLightCalibrationCacheMode(LightCalibrationCacheMode::Disk);
    addFrames(FrameType::Bias, QStringLiteral("bias"), "Bias Frame", 1000.0, 0.0, QString());
    addFrames(FrameType::Dark, QStringLiteral("dark"), "Dark Frame", 1200.0, 2.0, QString());
    addFrames(FrameType::Light, QStringLiteral("light"), "Light Frame", 2000.0, 2.0, QStringLiteral("L"));
    ASSERT_EQ(m_model.lightGroups().size(), 1u);
    LightCalibrationSettings settings;
    settings.cosmeticCorrection = false;
    settings.darkMultiplicationFactor = 0.5;
    m_model.setLightCalibrationSettings(m_model.lightGroups().front().key, settings);

    const WorkflowManifest manifest = buildManifest();
    ASSERT_TRUE(manifest.darkStage.has_value());
    ASSERT_EQ(manifest.darkStage->groups.size(), 1u);
    EXPECT_FALSE(manifest.darkStage->groups.front().biasMaster.has_value())
        << "a master dark is stacked from its frames as they are";
    BiasStackingStage bias;
    DarkStackingStage dark;
    LightsCalibrationStage lights;
    runStages(manifest, {&bias, &dark, &lights});

    expectEverySample(outputPath(manifest.darkStage->groups.front().outputFilename), fractionOf(1200.0));
    ASSERT_FALSE(manifest.lightsCalibration.empty());
    for (const WorkflowManifest::LightsCalibrationParams& light : manifest.lightsCalibration)
    {
        EXPECT_TRUE(light.biasPath.has_value() && light.darkPath.has_value());
        expectEverySample(calibratedLightOutputPathFor(manifest.outputDirectory, light.lightPath),
                          fractionOf(2000.0) - fractionOf(1000.0) - 0.5 * (fractionOf(1200.0) - fractionOf(1000.0)));
    }
}

// A dark taken at the flats' exposure - a flat-dark - is stacked for the flat that is matched to it, though no light
// shares its exposure. The run stacked only the darks a light matched, while the flat named the flat-dark's master,
// so the flat stage failed reading it.
TEST_F(FlatCalibrationRuleTest, AFlatDarkAtAnExposureNoLightSharesIsStackedForItsFlat)
{
    addFrames(FrameType::Bias, QStringLiteral("bias"), "Bias Frame", 1000.0, 0.0, QString());
    addFrames(FrameType::Dark, QStringLiteral("dark_light"), "Dark Frame", 1300.0, 60.0, QString());
    addFrames(FrameType::Dark, QStringLiteral("dark_flat"), "Dark Frame", 1200.0, 2.0, QString());
    addFrames(FrameType::Flat, QStringLiteral("flat"), "Flat Field", 30000.0, 2.0, QStringLiteral("L"));
    addFrames(FrameType::Light, QStringLiteral("light"), "Light Frame", 2000.0, 60.0, QStringLiteral("L"));

    const WorkflowManifest manifest = buildManifest();
    ASSERT_TRUE(manifest.darkStage.has_value() && manifest.flatStage.has_value());
    EXPECT_EQ(manifest.darkStage->groups.size(), 2u) << "the lights' dark and the flats' dark";
    ASSERT_EQ(manifest.flatStage->groups.size(), 1u);
    const WorkflowManifest::CalibrationStage::Group& flat = manifest.flatStage->groups.front();
    ASSERT_TRUE(flat.darkMaster.has_value());
    const bool darkIsStacked =
        std::any_of(manifest.darkStage->groups.begin(), manifest.darkStage->groups.end(), [&](const auto& group) {
            return QDir(manifest.outputDirectory).filePath(group.outputFilename) == flat.darkMaster->canonicalPath();
        });
    EXPECT_TRUE(darkIsStacked) << "the flat names a dark master the run does not stack";
    BiasStackingStage bias;
    DarkStackingStage dark;
    FlatStackingStage flatStage;
    runStages(manifest, {&bias, &dark, &flatStage});
    expectEverySample(outputPath(flat.outputFilename), fractionOf(30000.0) - fractionOf(1200.0));
}

TEST_F(CalibrationStageTest, AMasterThatCannotBeReadFailsItsGroupNamingIt)
{
    const QString missing = framePath(QStringLiteral("no_such_dark.fits"));
    const QString corrupt = framePath(QStringLiteral("corrupt_dark.fits"));
    {
        QDir().mkpath(QFileInfo(corrupt).absolutePath());
        QFile file(corrupt);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write("SIMPLE  =                    T");
    }
    for (const QString& master : {missing, corrupt})
    {
        WorkflowManifest::CalibrationStage::Group made =
            group(QStringLiteral("FLAT|16x12|L"), writeFrames(QStringLiteral("flat"), "Flat Field", 30000.0, 3),
                  QStringLiteral("master_flat.fit"), FrameType::Flat);
        made.darkMaster = FrameId(master);
        WorkflowManifest::CalibrationStage stage;
        stage.frameType = QStringLiteral("FLAT");
        stage.groups.push_back(made);
        m_manifest.flatStage = stage;

        FlatStackingStage flat;
        const StageResult result = m_run.run(flat, m_manifest);
        ASSERT_TRUE(result.isFailure()) << master.toStdString();
        EXPECT_EQ(result.error.filePath(), FrameId(master).canonicalPath());
        EXPECT_FALSE(QFileInfo::exists(outputPath(QStringLiteral("master_flat.fit"))));
    }
}

TEST_F(CalibrationStageTest, ACancelledRunWritesNoMaster)
{
    setBiasGroup(group(QStringLiteral("BIAS|16x12"), writeFrames(QStringLiteral("bias"), "Bias Frame", 1000.0, 3),
                       QStringLiteral("master_bias.fit"), FrameType::Bias));
    BiasStackingStage stage;
    CancellationToken token;
    token.requestCancel();
    EXPECT_TRUE(m_run.run(stage, m_manifest, token).isCancelled());

    m_run.requestStop();
    EXPECT_TRUE(m_run.run(stage, m_manifest).isCancelled()) << "the run's own stop is a cancellation too";
    EXPECT_FALSE(QFileInfo::exists(outputPath(QStringLiteral("master_bias.fit"))));
}

TEST_F(CalibrationStageTest, AStageOutsideARunIsRefused)
{
    setBiasGroup(group(QStringLiteral("BIAS|16x12"), writeFrames(QStringLiteral("bias"), "Bias Frame", 1000.0, 3),
                       QStringLiteral("master_bias.fit"), FrameType::Bias));
    BiasStackingStage stage;
    CancellationToken token;
    ProgressReporter reporter;
    const StageResult result = stage.execute(m_manifest, token, reporter);
    ASSERT_TRUE(result.isFailure());
    EXPECT_EQ(result.error.code(), IntegrationErrorCode::InternalError);
}

TEST_F(CalibrationStageTest, ThePreflightProbesEveryFrameAndDecodesNone)
{
    const std::vector<FrameId> frames = writeFrames(QStringLiteral("bias"), "Bias Frame", 1000.0, 4);
    const ImageIntegration::WorkflowStageServices services(m_run.session(), 0u, ImageIntegration::workflowRawPolicy(),
                                                           nullptr, {});
    IntegrationErrorReport report;
    QStringList warnings;
    ProgressReporter reporter;
    stage_utils::StackInputPreloadDiagnostics diagnostics;
    const stage_utils::ReadableStackInputs readable = stage_utils::loadReadableStackInputs(
        services, frames, QStringLiteral("bias"), report, warnings, reporter, &diagnostics);
    EXPECT_EQ(readable.inputFiles.size(), 4u);
    EXPECT_EQ(readable.revisions.size(), 4u);
    EXPECT_EQ(diagnostics.metadataLoadCount, 4u);
    EXPECT_EQ(diagnostics.noPixelProbeCount + diagnostics.inconclusiveProbeCount, 4u);
    EXPECT_EQ(diagnostics.fallbackDecodeCount, 0u);
    EXPECT_TRUE(report.errors.empty());
}

TEST_F(CalibrationStageTest, AFrameChangedAfterItWasCheckedFailsTheStackNamingIt)
{
    const std::vector<FrameId> frames = writeFrames(QStringLiteral("bias"), "Bias Frame", 1000.0, 3);
    const ImageIntegration::WorkflowStageServices services(m_run.session(), 0u, ImageIntegration::workflowRawPolicy(),
                                                           nullptr, {});
    IntegrationErrorReport report;
    QStringList warnings;
    ProgressReporter reporter;
    const stage_utils::ReadableStackInputs readable =
        stage_utils::loadReadableStackInputs(services, frames, QStringLiteral("bias"), report, warnings, reporter);
    ASSERT_EQ(readable.inputFiles.size(), 3u);

    // The second frame is rewritten after the preflight read it, with another modification time.
    std::string error;
    ASSERT_TRUE(writeCalibrationFrame(frames[1].canonicalPath(), "Bias Frame", 2000.0, {}, error)) << error;
    {
        QFile file(frames[1].canonicalPath());
        ASSERT_TRUE(file.open(QIODevice::ReadWrite));
        ASSERT_TRUE(file.setFileTime(QDateTime::currentDateTime().addSecs(120), QFileDevice::FileModificationTime));
    }
    core::CalibrationMasterSet masters;
    CancellationToken token;
    stage_utils::CalibrationStackRequest request;
    request.stage = WorkflowStage::Bias;
    request.groupIndex = 1;
    request.totalGroups = 1;
    request.groupLabel = QStringLiteral("BIAS");
    request.inputs = &readable;
    request.outputPath = outputPath(QStringLiteral("master_bias.fit"));
    request.stacking = defaultCalibrationStackingSettings(FrameType::Bias);
    request.artifactHash = QStringLiteral("abc");
    const StageResult result =
        stage_utils::stackCalibrationGroup(services, request, masters, warnings, token, reporter);
    ASSERT_TRUE(result.isFailure());
    EXPECT_TRUE(result.error.details().contains(QFileInfo(frames[1].canonicalPath()).fileName()))
        << result.error.toLogString().toStdString();
    EXPECT_FALSE(QFileInfo::exists(request.outputPath));
}

} // namespace
