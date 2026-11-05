// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the calibration stages reuse on the next run and what they make again: a calibrated light or a master whose
/// manifest entry is unchanged is reused from its record without reading its pixels; a missing output, a changed input
/// frame or a changed master makes it again; asking to reprocess makes everything again; and marking a group
/// pre-calibrated moves the fingerprint of what its lights make.

#include "Test_ImageIntegration/workflow/CalibrationRun.h"

#include "processes/ImageIntegration/core/CalibrationMasterDefaults.h"
#include "processes/ImageIntegration/core/FilenameUtils.h"
#include "processes/ImageIntegration/model/FrameType.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/workflow/ArtifactTracker.h"
#include "processes/ImageIntegration/workflow/ManifestBuilder.h"
#include "processes/ImageIntegration/workflow/ManifestHashing.h"
#include "processes/ImageIntegration/workflow/stages/BiasStackingStage.h"
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

#include <string>
#include <vector>

namespace
{

using an::tests::integration::CalibrationRun;
using an::tests::integration::fractionOf;
using an::tests::integration::readWrittenImage;
using an::tests::integration::writeCalibrationFrame;

class CalibrationReuseTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_root.isValid());
        ASSERT_TRUE(m_run.valid());
        m_manifest.outputDirectory = QDir(m_root.path()).filePath(QStringLiteral("output"));
        m_manifest.calibrateLightsInMemory = false;
        ASSERT_TRUE(QDir().mkpath(m_manifest.outputDirectory));
    }

    /// Writes a frame, its modification time moved by `secondsAhead` so a rewrite is seen as one.
    QString write(const QString& name, const std::string& kind, double level, int secondsAhead = 0)
    {
        const QString path = QDir(m_root.path()).filePath(QStringLiteral("frames/") + name);
        std::string error;
        EXPECT_TRUE(writeCalibrationFrame(path, kind, level, {}, error)) << error;
        if (secondsAhead != 0)
        {
            QFile file(path);
            EXPECT_TRUE(file.open(QIODevice::ReadWrite));
            EXPECT_TRUE(file.setFileTime(QDateTime::currentDateTime().addSecs(secondsAhead),
                                         QFileDevice::FileModificationTime));
        }
        return path;
    }

    [[nodiscard]] QString calibratedPath(const QString& light) const
    {
        return QDir(QDir(m_manifest.outputDirectory).filePath(QStringLiteral("calibrated_lights")))
            .filePath(ImageIntegration::makeCalibratedOutputFilename(QFileInfo(light).filePath()));
    }

    void addLight(const QString& light, const QString& dark)
    {
        WorkflowManifest::LightsCalibrationParams params;
        params.lightPath = FrameId(light);
        params.darkPath = FrameId(dark);
        m_manifest.lightsCalibration.push_back(params);
    }

    QTemporaryDir m_root;
    CalibrationRun m_run;
    WorkflowManifest m_manifest;
};

TEST_F(CalibrationReuseTest, ACalibratedLightIsReusedUnlessReprocessingIsAsked)
{
    const QString dark = write(QStringLiteral("master_dark.fits"), "Dark Frame", 300.0);
    const QString light = write(QStringLiteral("light_001.fits"), "Light Frame", 2000.0);
    addLight(light, dark);
    LightsCalibrationStage stage;
    ASSERT_TRUE(m_run.run(stage, m_manifest).isSuccess());
    const QString output = calibratedPath(light);
    const QString first = ArtifactTracker::fileIdentityForPath(output);
    ASSERT_FALSE(first.isEmpty());

    ASSERT_TRUE(m_run.run(stage, m_manifest).isSuccess());
    EXPECT_EQ(ArtifactTracker::fileIdentityForPath(output), first) << "the identical run wrote the light again";

    m_manifest.reprocessExisting = true;
    ASSERT_TRUE(m_run.run(stage, m_manifest).isSuccess());
    EXPECT_NE(ArtifactTracker::fileIdentityForPath(output), first) << "reprocessing reused the light";
}

TEST_F(CalibrationReuseTest, OnlyAMissingOutputIsCalibratedAgain)
{
    const QString dark = write(QStringLiteral("master_dark.fits"), "Dark Frame", 300.0);
    const QString kept = write(QStringLiteral("light_001.fits"), "Light Frame", 2000.0);
    const QString lost = write(QStringLiteral("light_002.fits"), "Light Frame", 2000.0);
    addLight(kept, dark);
    addLight(lost, dark);
    LightsCalibrationStage stage;
    ASSERT_TRUE(m_run.run(stage, m_manifest).isSuccess());
    const QString keptIdentity = ArtifactTracker::fileIdentityForPath(calibratedPath(kept));
    ASSERT_TRUE(QFile::remove(calibratedPath(lost)));

    ASSERT_TRUE(m_run.run(stage, m_manifest).isSuccess());
    EXPECT_TRUE(QFileInfo::exists(calibratedPath(lost)));
    EXPECT_EQ(ArtifactTracker::fileIdentityForPath(calibratedPath(kept)), keptIdentity);
}

TEST_F(CalibrationReuseTest, ALightIsCalibratedAgainWhenItsMasterChanges)
{
    const QString dark = write(QStringLiteral("master_dark.fits"), "Dark Frame", 300.0);
    const QString light = write(QStringLiteral("light_001.fits"), "Light Frame", 2000.0);
    addLight(light, dark);
    LightsCalibrationStage stage;
    ASSERT_TRUE(m_run.run(stage, m_manifest).isSuccess());

    write(QStringLiteral("master_dark.fits"), "Dark Frame", 500.0, 120);
    ASSERT_TRUE(m_run.run(stage, m_manifest).isSuccess());
    const auto written = readWrittenImage(calibratedPath(light));
    ASSERT_TRUE(written.has_value());
    EXPECT_NEAR(written->samples.value(0u, 0u, 0u), fractionOf(2000.0) - fractionOf(500.0), 1.0e-6);
}

TEST_F(CalibrationReuseTest, AMasterIsStackedAgainWhenAnInputFrameOrItsOwnMasterChanges)
{
    std::vector<FrameId> biasFrames;
    for (int index = 1; index <= 3; ++index)
    {
        biasFrames.emplace_back(write(QStringLiteral("bias_%1.fits").arg(index), "Bias Frame", 1000.0));
    }
    WorkflowManifest::CalibrationStage::Group biasGroup;
    biasGroup.groupKey = GroupKey{QStringLiteral("BIAS|16x12")};
    biasGroup.outputFilename = QStringLiteral("master_bias.fit");
    biasGroup.inputFiles = biasFrames;
    biasGroup.stacking = defaultCalibrationStackingSettings(FrameType::Bias);
    WorkflowManifest::CalibrationStage biasStage;
    biasStage.groups.push_back(biasGroup);
    m_manifest.biasStage = biasStage;

    BiasStackingStage bias;
    ASSERT_TRUE(m_run.run(bias, m_manifest).isSuccess());
    EXPECT_TRUE(bias.shouldSkip(m_manifest));
    write(QStringLiteral("bias_2.fits"), "Bias Frame", 1100.0, 120);
    EXPECT_FALSE(bias.shouldSkip(m_manifest)) << "a changed input frame did not invalidate the master";

    const QString darkMaster = write(QStringLiteral("master_dark_input.fits"), "Dark Frame", 1200.0);
    WorkflowManifest::CalibrationStage::Group flatGroup;
    flatGroup.groupKey = GroupKey{QStringLiteral("FLAT|16x12|L")};
    flatGroup.outputFilename = QStringLiteral("master_flat.fit");
    for (int index = 1; index <= 3; ++index)
    {
        flatGroup.inputFiles.emplace_back(write(QStringLiteral("flat_%1.fits").arg(index), "Flat Field", 30000.0));
    }
    flatGroup.stacking = defaultCalibrationStackingSettings(FrameType::Flat);
    flatGroup.darkMaster = FrameId(darkMaster);
    WorkflowManifest::CalibrationStage flatStage;
    flatStage.groups.push_back(flatGroup);
    m_manifest.flatStage = flatStage;

    FlatStackingStage flat;
    ASSERT_TRUE(m_run.run(flat, m_manifest).isSuccess());
    EXPECT_TRUE(flat.shouldSkip(m_manifest));
    write(QStringLiteral("master_dark_input.fits"), "Dark Frame", 1250.0, 120);
    EXPECT_FALSE(flat.shouldSkip(m_manifest)) << "a changed dark master did not invalidate the flat";
}

// A group marked pre-calibrated has no calibration entry, so the fingerprint of what its lights make moves when the
// mark is set or cleared - even for lights no master matches - and the manifest names the group, so a report can tell
// "calibrated before import" from "no master matched".
TEST_F(CalibrationReuseTest, MarkingAGroupPreCalibratedMovesItsLightsFingerprintAndNamesTheGroup)
{
    IntegrationDataModel model;
    model.setOutputDirectory(m_manifest.outputDirectory);
    std::vector<FrameInfo> lights;
    std::vector<FrameId> lightIds;
    for (int index = 1; index <= 2; ++index)
    {
        const QString path = write(QStringLiteral("light_%1.fits").arg(index), "Light Frame", 2000.0);
        const FrameMetadata metadata(
            FrameType::Light,
            QSize(an::tests::integration::kCalibrationFrameWidth, an::tests::integration::kCalibrationFrameHeight),
            60.0, 0.0, 100, 10, QStringLiteral("L"), QStringLiteral("M42"),
            QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC), QStringLiteral("Default"));
        lights.emplace_back(FrameId(path), path, metadata, QStringLiteral("2026-10-27"));
        lightIds.emplace_back(path);
    }
    model.addFrames(FrameType::Light, lights);
    ASSERT_EQ(model.lightGroups().size(), 1u);
    const GroupKey groupKey = model.lightGroups().front().key;

    const WorkflowManifest unmarked = ManifestBuilder(model).build();
    ASSERT_EQ(unmarked.lightsCalibration.size(), 2u);
    EXPECT_TRUE(unmarked.preCalibratedLightGroups.empty());
    const QString unmarkedFingerprint = computeLightsCalibrationHashForSubset(unmarked.lightsCalibration, lightIds);

    LightCalibrationSettings preCalibrated;
    preCalibrated.preCalibrated = true;
    model.setLightCalibrationSettings(groupKey, preCalibrated);
    const WorkflowManifest marked = ManifestBuilder(model).build();
    EXPECT_TRUE(marked.lightsCalibration.empty());
    ASSERT_EQ(marked.preCalibratedLightGroups.size(), 1u);
    EXPECT_EQ(marked.preCalibratedLightGroups.front().groupKey, groupKey);
    EXPECT_EQ(marked.preCalibratedLightGroups.front().lightFiles.size(), 2u);
    EXPECT_NE(computeLightsCalibrationHashForSubset(marked.lightsCalibration, lightIds), unmarkedFingerprint)
        << "marking the group pre-calibrated left its lights' fingerprint where it was";

    model.setLightCalibrationSettings(groupKey, LightCalibrationSettings{});
    const WorkflowManifest cleared = ManifestBuilder(model).build();
    EXPECT_TRUE(cleared.preCalibratedLightGroups.empty());
    EXPECT_EQ(computeLightsCalibrationHashForSubset(cleared.lightsCalibration, lightIds), unmarkedFingerprint);
}

} // namespace
