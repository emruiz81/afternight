// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The lights calibration stage: each light calibrated by its masters - the bias dropped beside a
/// dark in standard mode, subtracted from the dark once in CCD mode, the pedestal in sixteen-bit codes - corrected
/// where the dark marks defective pixels, and published as stamped Float32 FITS; a light with nothing to calibrate it
/// left as it is; a dark scale the optimization cannot fit replaced by the fixed one and said; a master that cannot be
/// read fails only the lights that name it; the in-memory mode skips the stage. The cosmetic sigmas, the dark's factor
/// and its optimization, and the flat's auto-level applied alike by the stage and by the stack's reader in memory. And
/// the master set it reads them through.

#include "Test_ImageIntegration/workflow/CalibrationRun.h"

#include "fixtures/common/IntegrationFrameFiles.h"

#include "processes/ImageIntegration/core/CalibrationMasterSet.h"
#include "processes/ImageIntegration/core/FilenameUtils.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/workflow/ArtifactTracker.h"
#include "processes/ImageIntegration/workflow/ManifestBuilder.h"
#include "processes/ImageIntegration/workflow/ManifestHashing.h"
#include "processes/ImageIntegration/workflow/stages/LightsCalibrationStage.h"
#include "processes/ImageIntegration/workflow/stages/LightsStackingStage.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace
{

using an::tests::fixtures::FixtureSampleOverride;
using an::tests::integration::CalibrationRun;
using an::tests::integration::fractionOf;
using an::tests::integration::readWrittenImage;
using an::tests::integration::writeCalibrationFrame;

class LightsCalibrationTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_root.isValid());
        ASSERT_TRUE(m_run.valid());
        m_manifest.outputDirectory = QDir(m_root.path()).filePath(QStringLiteral("output"));
        m_manifest.executionId = QStringLiteral("run-7");
        m_manifest.calibrateLightsInMemory = false;
        ASSERT_TRUE(QDir().mkpath(m_manifest.outputDirectory));
    }

    QString write(const QString& name, const std::string& kind, double level,
                  const std::vector<FixtureSampleOverride>& overrides = {})
    {
        const QString path = QDir(m_root.path()).filePath(QStringLiteral("frames/") + name);
        std::string error;
        EXPECT_TRUE(writeCalibrationFrame(path, kind, level, overrides, error)) << error;
        return path;
    }

    /// One light named in the manifest with these masters and settings.
    WorkflowManifest::LightsCalibrationParams& addLight(const QString& path, const std::optional<QString>& bias,
                                                        const std::optional<QString>& dark,
                                                        const std::optional<QString>& flat,
                                                        WorkflowManifest::CalibrationSettings settings = {})
    {
        WorkflowManifest::LightsCalibrationParams params;
        params.lightPath = FrameId(path);
        if (bias.has_value())
        {
            params.biasPath = FrameId(*bias);
        }
        if (dark.has_value())
        {
            params.darkPath = FrameId(*dark);
        }
        if (flat.has_value())
        {
            params.flatPath = FrameId(*flat);
        }
        params.settings = settings;
        m_manifest.lightsCalibration.push_back(params);
        return m_manifest.lightsCalibration.back();
    }

    [[nodiscard]] QString calibratedPath(const QString& light) const
    {
        return QDir(QDir(m_manifest.outputDirectory).filePath(QStringLiteral("calibrated_lights")))
            .filePath(ImageIntegration::makeCalibratedOutputFilename(QFileInfo(light).filePath()));
    }

    static double sampleAt(const QString& path, std::uint32_t x, std::uint32_t y)
    {
        const auto written = readWrittenImage(path);
        if (!written.has_value())
        {
            ADD_FAILURE() << "no image at " << path.toStdString();
            return 0.0;
        }
        return written->samples.value(x, y, 0u);
    }

    QTemporaryDir m_root;
    CalibrationRun m_run;
    WorkflowManifest m_manifest;
};

TEST_F(LightsCalibrationTest, ALightIsCalibratedByItsMastersAndPublishedStampedAsFloatFits)
{
    const QString bias = write(QStringLiteral("master_bias.fits"), "Bias Frame", 1000.0);
    const QString dark = write(QStringLiteral("master_dark.fits"), "Dark Frame", 1300.0);
    const QString flat = write(QStringLiteral("master_flat.fits"), "Flat Field", 20000.0);
    const QString light = write(QStringLiteral("light_001.fits"), "Light Frame", 2000.0);
    const WorkflowManifest::LightsCalibrationParams params = addLight(light, bias, dark, flat);

    LightsCalibrationStage stage;
    const StageResult result = m_run.run(stage, m_manifest);
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();

    // Standard mode: the dark holds the bias, so the bias is not subtracted beside it; a flat of one level divides by
    // one after its central mean is taken.
    const QString output = calibratedPath(light);
    const auto written = readWrittenImage(output);
    ASSERT_TRUE(written.has_value());
    EXPECT_EQ(written->card("BITPIX").value_or(""), "-32");
    EXPECT_NEAR(written->samples.value(3u, 4u, 0u), fractionOf(2000.0) - fractionOf(1300.0), 1.0e-6);
    const QString hash = computeCalibratedLightArtifactHash(params);
    const auto identity = an::tests::integration::writtenIdentity(m_run.session(), output);
    ASSERT_TRUE(identity.has_value());
    EXPECT_EQ(QString::fromStdString(identity->artifactId), hash.left(32));
    EXPECT_EQ(identity->runId, "run-7");
    ArtifactTracker tracker(m_manifest.outputDirectory, nullptr, sessionArtifactStampReader(m_run.session()));
    EXPECT_TRUE(tracker.isArtifactValid(output, hash));
}

TEST_F(LightsCalibrationTest, CcdModeSubtractsTheBiasFromTheDarkAndFromTheLight)
{
    const QString bias = write(QStringLiteral("master_bias.fits"), "Bias Frame", 1000.0);
    const QString dark = write(QStringLiteral("master_dark.fits"), "Dark Frame", 1300.0);
    WorkflowManifest::CalibrationSettings ccd;
    ccd.subtractBiasFromDark = true;
    std::vector<QString> lights;
    for (int index = 1; index <= 3; ++index)
    {
        lights.push_back(write(QStringLiteral("light_%1.fits").arg(index), "Light Frame", 2000.0));
        addLight(lights.back(), bias, dark, std::nullopt, ccd);
    }
    LightsCalibrationStage stage;
    ASSERT_TRUE(m_run.run(stage, m_manifest).isSuccess());
    for (const QString& light : lights)
    {
        EXPECT_NEAR(sampleAt(calibratedPath(light), 5u, 5u),
                    fractionOf(2000.0) - fractionOf(1000.0) - (fractionOf(1300.0) - fractionOf(1000.0)), 1.0e-6);
    }
}

TEST_F(LightsCalibrationTest, ThePedestalIsStatedInSixteenBitCodes)
{
    const QString dark = write(QStringLiteral("master_dark.fits"), "Dark Frame", 300.0);
    const QString light = write(QStringLiteral("light_001.fits"), "Light Frame", 2000.0);
    WorkflowManifest::CalibrationSettings settings;
    settings.pedestal = 100.0;
    addLight(light, std::nullopt, dark, std::nullopt, settings);
    LightsCalibrationStage stage;
    ASSERT_TRUE(m_run.run(stage, m_manifest).isSuccess());
    EXPECT_NEAR(sampleAt(calibratedPath(light), 2u, 2u), fractionOf(2000.0) + fractionOf(100.0) - fractionOf(300.0),
                1.0e-6);
}

TEST_F(LightsCalibrationTest, TheCosmeticCorrectionReplacesThePixelTheDarkMarksHot)
{
    const std::vector<FixtureSampleOverride> hot{FixtureSampleOverride{.x = 7u, .y = 6u, .value = 40000.0}};
    const QString dark = write(QStringLiteral("master_dark.fits"), "Dark Frame", 300.0, hot);
    const QString light = write(QStringLiteral("light_001.fits"), "Light Frame", 2000.0,
                                {FixtureSampleOverride{.x = 7u, .y = 6u, .value = 60000.0}});
    WorkflowManifest::CalibrationSettings corrected;
    corrected.cosmeticCorrection = true;
    addLight(light, std::nullopt, dark, std::nullopt, corrected);
    LightsCalibrationStage stage;
    ASSERT_TRUE(m_run.run(stage, m_manifest).isSuccess());
    const double neighbour = sampleAt(calibratedPath(light), 6u, 6u);
    EXPECT_NEAR(neighbour, fractionOf(2000.0) - fractionOf(300.0), 1.0e-6);
    EXPECT_NEAR(sampleAt(calibratedPath(light), 7u, 6u), neighbour, 1.0e-6);

    // Without it the defect stays where the light holds it.
    m_manifest.lightsCalibration.front().settings.cosmeticCorrection = false;
    ASSERT_TRUE(m_run.run(stage, m_manifest).isSuccess());
    EXPECT_NEAR(sampleAt(calibratedPath(light), 7u, 6u), fractionOf(60000.0) - fractionOf(40000.0), 1.0e-6);
}

TEST_F(LightsCalibrationTest, ACosmeticCorrectionWithoutADarkSaysItCorrectsNothing)
{
    const QString light = write(QStringLiteral("light_001.fits"), "Light Frame", 2000.0);
    const QString flat = write(QStringLiteral("master_flat.fits"), "Flat Field", 20000.0);
    WorkflowManifest::CalibrationSettings corrected;
    corrected.cosmeticCorrection = true;
    addLight(light, std::nullopt, std::nullopt, flat, corrected);
    LightsCalibrationStage stage;
    const StageResult result = m_run.run(stage, m_manifest);
    ASSERT_TRUE(result.isSuccess());
    ASSERT_EQ(result.warnings.size(), 1);
    EXPECT_TRUE(result.warnings.front().contains(QStringLiteral("no dark master")));
}

// Saving calibrated frames to disk, a light with no master and no pedestal has nothing to be calibrated with: it is
// left as it is - no calibrated copy, no error, no warning, as lights-only processing warns of nothing - and the
// registration reads the light itself, as the same run in memory does. Each such light failed as the engine refused
// a calibration that changes nothing.
TEST_F(LightsCalibrationTest, ALightWithNothingToCalibrateItIsLeftAsItIsOnDisk)
{
    std::vector<QString> lights;
    for (int index = 1; index <= 2; ++index)
    {
        lights.push_back(write(QStringLiteral("light_%1.fits").arg(index), "Light Frame", 2000.0));
        WorkflowManifest::CalibrationSettings settings;
        settings.cosmeticCorrection = true;
        addLight(lights.back(), std::nullopt, std::nullopt, std::nullopt, settings);
    }
    LightsCalibrationStage stage;
    const StageResult result = m_run.run(stage, m_manifest);
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    EXPECT_TRUE(result.errorReport.errors.empty());
    EXPECT_TRUE(result.warnings.isEmpty()) << result.warnings.join(QStringLiteral(" | ")).toStdString();
    for (const QString& light : lights)
    {
        EXPECT_FALSE(QFileInfo::exists(calibratedLightOutputPathFor(m_manifest.outputDirectory, FrameId(light))))
            << "the registration reads a calibrated copy where one exists";
    }
}

// A dark that does not vary over the central square gives the optimization nothing to fit: the light is calibrated
// with the fixed factor, as a light the optimization cannot measure is, and the stage says so once for the lights
// sharing that calibration. Every such light failed.
TEST_F(LightsCalibrationTest, ADarkWithNothingToOptimizeAgainstCalibratesWithTheFixedFactorAndSaysSo)
{
    const auto writeSquare = [&](const QString& name, const std::string& kind, const double level) {
        an::tests::fixtures::AcquisitionFacts facts;
        facts.kind = kind;
        an::tests::fixtures::FixtureImage frame = an::tests::fixtures::acquisitionFrame(
            QFileInfo(name).completeBaseName().toStdString(), facts, an::kCalibrationDarkOptimizationSide,
            an::kCalibrationDarkOptimizationSide);
        frame.levels = {an::tests::fixtures::FixtureChannelLevels{.base = level}};
        const QString path = QDir(m_root.path()).filePath(QStringLiteral("frames/") + name);
        std::string error;
        EXPECT_TRUE(
            an::tests::fixtures::writeAcquisitionFrame(frame, std::filesystem::path(path.toStdWString()), error))
            << error;
        return path;
    };
    const QString dark = writeSquare(QStringLiteral("master_dark.fits"), "Dark Frame", 300.0);
    WorkflowManifest::CalibrationSettings optimized;
    optimized.darkOptimization = true;
    std::vector<QString> lights;
    for (int index = 1; index <= 2; ++index)
    {
        lights.push_back(writeSquare(QStringLiteral("light_%1.fits").arg(index), "Light Frame", 2000.0));
        addLight(lights.back(), std::nullopt, dark, std::nullopt, optimized);
    }
    LightsCalibrationStage stage;
    const StageResult result = m_run.run(stage, m_manifest);
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    for (const QString& light : lights)
    {
        EXPECT_NEAR(sampleAt(calibratedPath(light), 100u, 100u), fractionOf(2000.0) - fractionOf(300.0), 1.0e-6);
    }
    ASSERT_EQ(result.warnings.size(), 1) << result.warnings.join(QStringLiteral(" | ")).toStdString();
    EXPECT_TRUE(result.warnings.front().contains(QStringLiteral("could not be optimized")))
        << result.warnings.front().toStdString();
    EXPECT_TRUE(result.warnings.front().contains(QStringLiteral("master_dark.fits")))
        << result.warnings.front().toStdString();
}

TEST_F(LightsCalibrationTest, AMasterThatCannotBeReadFailsOnlyItsLightsEachNamed)
{
    const QString corrupt = QDir(m_root.path()).filePath(QStringLiteral("frames/corrupt_dark.fits"));
    const QString good = write(QStringLiteral("master_dark.fits"), "Dark Frame", 300.0);
    {
        QFile file(corrupt);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write("SIMPLE  =                    T");
    }
    const QString first = write(QStringLiteral("light_a.fits"), "Light Frame", 2000.0);
    const QString second = write(QStringLiteral("light_b.fits"), "Light Frame", 2000.0);
    const QString third = write(QStringLiteral("light_c.fits"), "Light Frame", 2000.0);
    addLight(first, std::nullopt, corrupt, std::nullopt);
    addLight(second, std::nullopt, corrupt, std::nullopt);
    addLight(third, std::nullopt, good, std::nullopt);

    LightsCalibrationStage stage;
    const StageResult result = m_run.run(stage, m_manifest);
    ASSERT_TRUE(result.isPartialSuccess()) << result.error.toLogString().toStdString();
    ASSERT_EQ(result.errorReport.errors.size(), 2u);
    EXPECT_EQ(result.errorReport.errors[0].filePath(), FrameId(first).canonicalPath());
    EXPECT_EQ(result.errorReport.errors[1].filePath(), FrameId(second).canonicalPath());
    EXPECT_FALSE(QFileInfo::exists(calibratedPath(first)));
    EXPECT_NEAR(sampleAt(calibratedPath(third), 1u, 1u), fractionOf(2000.0) - fractionOf(300.0), 1.0e-6);
}

TEST_F(LightsCalibrationTest, AMasterOfAnotherSizeFailsItsLightsWithTheEnginesReason)
{
    an::tests::fixtures::AcquisitionFacts facts;
    facts.kind = "Dark Frame";
    const QString dark = QDir(m_root.path()).filePath(QStringLiteral("frames/wide_dark.fits"));
    std::string error;
    ASSERT_TRUE(
        an::tests::fixtures::writeAcquisitionFrame(an::tests::fixtures::acquisitionFrame("dark", facts, 20u, 12u),
                                                   std::filesystem::path(dark.toStdWString()), error))
        << error;
    const QString light = write(QStringLiteral("light_001.fits"), "Light Frame", 2000.0);
    addLight(light, std::nullopt, dark, std::nullopt);
    LightsCalibrationStage stage;
    const StageResult result = m_run.run(stage, m_manifest);
    ASSERT_TRUE(result.isPartialSuccess());
    ASSERT_EQ(result.errorReport.errors.size(), 1u);
    EXPECT_EQ(result.errorReport.errors.front().code(), IntegrationErrorCode::CalibrationFailed);
    EXPECT_FALSE(result.errorReport.errors.front().details().isEmpty());
}

TEST_F(LightsCalibrationTest, ACancelledRunWritesNoLight)
{
    const QString dark = write(QStringLiteral("master_dark.fits"), "Dark Frame", 300.0);
    const QString light = write(QStringLiteral("light_001.fits"), "Light Frame", 2000.0);
    addLight(light, std::nullopt, dark, std::nullopt);
    LightsCalibrationStage stage;
    m_run.requestStop();
    EXPECT_TRUE(m_run.run(stage, m_manifest).isCancelled());
    EXPECT_FALSE(QFileInfo::exists(calibratedPath(light)));
}

TEST_F(LightsCalibrationTest, TheStageIsSkippedWhenLightsAreCalibratedInMemory)
{
    addLight(QStringLiteral("dummy.fit"), std::nullopt, std::nullopt, std::nullopt);
    LightsCalibrationStage stage;
    m_manifest.calibrateLightsInMemory = true;
    EXPECT_TRUE(stage.shouldSkip(m_manifest));
    m_manifest.calibrateLightsInMemory = false;
    EXPECT_FALSE(stage.shouldSkip(m_manifest));
}

TEST_F(LightsCalibrationTest, TheStageIsSkippedWhenAnIncrementalPlanHasNoDirtyRegistrationGroup)
{
    addLight(QStringLiteral("dummy.fit"), std::nullopt, std::nullopt, std::nullopt);
    m_manifest.executionMode = ExecutionMode::Incremental;
    LightsCalibrationStage stage;
    EXPECT_TRUE(stage.shouldSkip(m_manifest));
}

/// Where a light is calibrated: by the stage that saves the calibrated copy, or by the stack's reader as it reads the
/// light, which is a run's default.
enum class CalibrationRoute
{
    Disk,
    InMemory
};

/// A light's calibration settings as each route applies them. Every light is written twice and both copies are
/// calibrated alike, so the master the stack makes of them in memory is the calibrated light, sample for sample.
class CalibrationSettingsTest : public LightsCalibrationTest, public ::testing::WithParamInterface<CalibrationRoute>
{
protected:
    /// Writes a light twice, each copy calibrated by the masters and the settings stated.
    void addLightTwice(const std::string& kind, double level, const std::vector<FixtureSampleOverride>& overrides,
                       const std::optional<QString>& dark, const std::optional<QString>& flat,
                       const WorkflowManifest::CalibrationSettings& settings)
    {
        for (const QString& copy : {QStringLiteral("light_a.fits"), QStringLiteral("light_b.fits")})
        {
            m_lights.push_back(write(copy, kind, level, overrides));
            addLight(m_lights.back(), std::nullopt, dark, flat, settings);
        }
    }

    /// Changes the settings every light is calibrated with.
    void calibrateWith(const WorkflowManifest::CalibrationSettings& settings)
    {
        for (WorkflowManifest::LightsCalibrationParams& light : m_manifest.lightsCalibration)
        {
            light.settings = settings;
        }
    }

    /// The first light as the route calibrates it: the copy the stage saves, or the master the stack makes of the two
    /// copies it calibrates in memory. The stack keeps a master its fingerprint still names, so a setting it does not
    /// fingerprint would leave the master of the settings before.
    [[nodiscard]] std::optional<an::tests::fixtures::FixtureFitsFile> calibrated()
    {
        if (GetParam() == CalibrationRoute::Disk)
        {
            LightsCalibrationStage stage;
            const StageResult result = m_run.run(stage, m_manifest);
            EXPECT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
            return readWrittenImage(calibratedPath(m_lights.front()));
        }
        m_manifest.calibrateLightsInMemory = true;
        WorkflowManifest::IntegrationGroup group;
        group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|lights|Lum")}};
        group.outputFilename = QStringLiteral("master_Lum.fits");
        group.filter = QStringLiteral("Lum");
        group.stacking.normalizationMode = NormalizationMode::None;
        group.stacking.rejectionMethod = QStringLiteral("None");
        group.skipIfExists = true;
        for (const QString& light : m_lights)
        {
            group.lightFiles.emplace_back(light);
            group.lightExposureSeconds.push_back(60.0);
        }
        m_manifest.integrationGroups = {group};
        LightsStackingStage stage;
        const StageResult result = m_run.run(stage, m_manifest);
        EXPECT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
        return readWrittenImage(QDir(m_manifest.outputDirectory).filePath(group.outputFilename));
    }

    std::vector<QString> m_lights;
};

// The cosmetic sigmas are the thresholds the dark's defects are found at: a dark with a strong and a mild hot pixel,
// whose whole-image deviation puts the mild one inside three deviations and the strong one inside twenty, has both
// replaced at one and a half, the strong one at three and neither at twenty.
TEST_P(CalibrationSettingsTest, TheHotSigmaIsTheThresholdTheDarksHotPixelsAreFoundAt)
{
    const QString dark = write(QStringLiteral("master_dark.fits"), "Dark Frame", 300.0,
                               {FixtureSampleOverride{.x = 7u, .y = 6u, .value = 40000.0},
                                FixtureSampleOverride{.x = 2u, .y = 2u, .value = 5300.0}});
    WorkflowManifest::CalibrationSettings settings;
    settings.cosmeticCorrection = true;
    addLightTwice("Light Frame", 2000.0,
                  {FixtureSampleOverride{.x = 7u, .y = 6u, .value = 60000.0},
                   FixtureSampleOverride{.x = 2u, .y = 2u, .value = 9000.0}},
                  dark, std::nullopt, settings);
    const double replaced = fractionOf(2000.0) - fractionOf(300.0);
    struct Expected
    {
        double sigma;
        double strong;
        double mild;
    };
    for (const Expected& expected :
         {Expected{1.5, replaced, replaced}, Expected{3.0, replaced, fractionOf(9000.0) - fractionOf(5300.0)},
          Expected{20.0, fractionOf(60000.0) - fractionOf(40000.0), fractionOf(9000.0) - fractionOf(5300.0)}})
    {
        SCOPED_TRACE(expected.sigma);
        settings.sigmaHot = expected.sigma;
        calibrateWith(settings);
        const auto light = calibrated();
        ASSERT_TRUE(light.has_value());
        EXPECT_NEAR(light->samples.value(7u, 6u, 0u), expected.strong, 1.0e-6);
        EXPECT_NEAR(light->samples.value(2u, 2u, 0u), expected.mild, 1.0e-6);
        EXPECT_NEAR(light->samples.value(12u, 9u, 0u), replaced, 1.0e-6);
    }
}

// The cold sigma, as the hot one: a dark with a strong and a mild cold pixel has both replaced at one and a half, the
// strong one at three and neither at twenty.
TEST_P(CalibrationSettingsTest, TheColdSigmaIsTheThresholdTheDarksColdPixelsAreFoundAt)
{
    const QString dark = write(QStringLiteral("master_dark.fits"), "Dark Frame", 3000.0,
                               {FixtureSampleOverride{.x = 7u, .y = 6u, .value = 0.0},
                                FixtureSampleOverride{.x = 2u, .y = 2u, .value = 2600.0}});
    WorkflowManifest::CalibrationSettings settings;
    settings.cosmeticCorrection = true;
    addLightTwice("Light Frame", 5000.0,
                  {FixtureSampleOverride{.x = 7u, .y = 6u, .value = 1000.0},
                   FixtureSampleOverride{.x = 2u, .y = 2u, .value = 3000.0}},
                  dark, std::nullopt, settings);
    const double replaced = fractionOf(5000.0) - fractionOf(3000.0);
    struct Expected
    {
        double sigma;
        double strong;
        double mild;
    };
    for (const Expected& expected :
         {Expected{1.5, replaced, replaced}, Expected{3.0, replaced, fractionOf(3000.0) - fractionOf(2600.0)},
          Expected{20.0, fractionOf(1000.0), fractionOf(3000.0) - fractionOf(2600.0)}})
    {
        SCOPED_TRACE(expected.sigma);
        settings.sigmaCold = expected.sigma;
        calibrateWith(settings);
        const auto light = calibrated();
        ASSERT_TRUE(light.has_value());
        EXPECT_NEAR(light->samples.value(7u, 6u, 0u), expected.strong, 1.0e-6);
        EXPECT_NEAR(light->samples.value(2u, 2u, 0u), expected.mild, 1.0e-6);
    }
}

// The dark multiplication factor scales the dark before it is subtracted: a light at 2000 over a dark at 1300 is 1350
// at one half and 50 at one and a half.
TEST_P(CalibrationSettingsTest, TheDarkMultiplicationFactorScalesTheDarkBeforeItIsSubtracted)
{
    const QString dark = write(QStringLiteral("master_dark.fits"), "Dark Frame", 1300.0);
    WorkflowManifest::CalibrationSettings settings;
    settings.cosmeticCorrection = false;
    addLightTwice("Light Frame", 2000.0, {}, dark, std::nullopt, settings);
    for (const double factor : {0.5, 1.5})
    {
        SCOPED_TRACE(factor);
        settings.darkMultiplicationFactor = factor;
        calibrateWith(settings);
        const auto light = calibrated();
        ASSERT_TRUE(light.has_value());
        EXPECT_NEAR(light->samples.value(4u, 5u, 0u), fractionOf(2000.0) - factor * fractionOf(1300.0), 1.0e-6);
    }
}

// With the flat's auto-level the light is divided by the flat over its central mean; without it, by the flat's own
// samples. A flat at 20000 with a vignetted corner at 10000 leaves the light's level where it is and doubles the
// corner with the auto-level, and without it divides every sample by the flat's own fraction of full scale.
TEST_P(CalibrationSettingsTest, AFlatLeftUnlevelledDividesTheLightByItsOwnSamples)
{
    const QString dark = write(QStringLiteral("master_dark.fits"), "Dark Frame", 300.0);
    const QString flat = write(QStringLiteral("master_flat.fits"), "Flat Field", 20000.0,
                               {FixtureSampleOverride{.x = 0u, .y = 0u, .value = 10000.0}});
    WorkflowManifest::CalibrationSettings settings;
    settings.cosmeticCorrection = false;
    addLightTwice("Light Frame", 2000.0, {}, dark, flat, settings);
    const double darkSubtracted = fractionOf(2000.0) - fractionOf(300.0);

    const auto levelled = calibrated();
    ASSERT_TRUE(levelled.has_value());
    EXPECT_NEAR(levelled->samples.value(8u, 6u, 0u), darkSubtracted, 1.0e-6);
    EXPECT_NEAR(levelled->samples.value(0u, 0u, 0u), darkSubtracted * 2.0, 1.0e-6);

    settings.flatAutoLevel = false;
    calibrateWith(settings);
    const auto unlevelled = calibrated();
    ASSERT_TRUE(unlevelled.has_value());
    EXPECT_NEAR(unlevelled->samples.value(8u, 6u, 0u), darkSubtracted / fractionOf(20000.0), 1.0e-5);
    EXPECT_NEAR(unlevelled->samples.value(0u, 0u, 0u), darkSubtracted / fractionOf(10000.0), 1.0e-5);
}

// Dark optimization scales the dark to the thermal signal the light holds: a light holding half its dark's pattern
// over a level is calibrated to that level, the factor at which the difference varies least, where the fixed factor
// of one would leave half the pattern in it, inverted.
TEST_P(CalibrationSettingsTest, DarkOptimizationScalesTheDarkToTheThermalSignalTheLightHolds)
{
    an::tests::fixtures::FixtureImage thermal;
    thermal.name = "master_dark";
    thermal.width = an::kCalibrationDarkOptimizationSide;
    thermal.height = an::kCalibrationDarkOptimizationSide;
    thermal.sampleType = an::tests::fixtures::FixtureSampleType::Float32;
    thermal.pattern = an::tests::fixtures::FixturePattern::Noise;
    thermal.seed = 17u;
    thermal.levels = {an::tests::fixtures::FixtureChannelLevels{.base = 0.02, .amplitude = 0.01}};
    std::string error;
    const std::optional<an::tests::fixtures::FixtureSamples> dark =
        an::tests::fixtures::materializeFixture(thermal, error);
    ASSERT_TRUE(dark.has_value()) << error;
    an::tests::fixtures::FixtureSamples light = *dark;
    for (std::uint32_t y = 0u; y < light.height(); ++y)
    {
        for (std::uint32_t x = 0u; x < light.width(); ++x)
        {
            light.setValue(x, y, 0u, static_cast<double>(static_cast<float>(0.1 + 0.5 * dark->value(x, y, 0u))));
        }
    }
    const QString frames = QDir(m_root.path()).filePath(QStringLiteral("frames"));
    ASSERT_TRUE(QDir().mkpath(frames));
    const QString darkPath = QDir(frames).filePath(QStringLiteral("master_dark.fits"));
    ASSERT_TRUE(
        an::tests::fixtures::writeFixtureFits(thermal, *dark, std::filesystem::path(darkPath.toStdWString()), error))
        << error;
    WorkflowManifest::CalibrationSettings settings;
    settings.cosmeticCorrection = false;
    settings.darkOptimization = true;
    for (const QString& copy : {QStringLiteral("light_a.fits"), QStringLiteral("light_b.fits")})
    {
        an::tests::fixtures::FixtureImage described = thermal;
        described.name = QFileInfo(copy).completeBaseName().toStdString();
        m_lights.push_back(QDir(frames).filePath(copy));
        ASSERT_TRUE(an::tests::fixtures::writeFixtureFits(described, light,
                                                          std::filesystem::path(m_lights.back().toStdWString()), error))
            << error;
        addLight(m_lights.back(), std::nullopt, darkPath, std::nullopt, settings);
    }

    const auto calibratedLight = calibrated();
    ASSERT_TRUE(calibratedLight.has_value());
    for (const auto& [x, y] : {std::pair{100u, 100u}, std::pair{256u, 256u}, std::pair{400u, 37u}})
    {
        EXPECT_NEAR(calibratedLight->samples.value(x, y, 0u), 0.1, 5.0e-5) << x << ", " << y;
    }
}

INSTANTIATE_TEST_SUITE_P(EachRoute, CalibrationSettingsTest,
                         ::testing::Values(CalibrationRoute::Disk, CalibrationRoute::InMemory),
                         [](const ::testing::TestParamInfo<CalibrationRoute>& info) {
                             return info.param == CalibrationRoute::Disk ? std::string("SavedToDisk")
                                                                         : std::string("InMemory");
                         });

TEST(LightsCalibrationBuilderTest, TheBuilderCalibratesInMemoryUnlessTheModelAsksForDisk)
{
    IntegrationDataModel model;
    EXPECT_EQ(model.lightCalibrationCacheMode(), LightCalibrationCacheMode::InMemory);
    EXPECT_TRUE(ManifestBuilder(model).build().calibrateLightsInMemory);
    model.setLightCalibrationCacheMode(LightCalibrationCacheMode::Disk);
    EXPECT_FALSE(ManifestBuilder(model).build().calibrateLightsInMemory);
}

TEST_F(LightsCalibrationTest, TheMasterSetReadsEachMasterOnceAndSubtractsTheBiasFromADarkOnce)
{
    const QString bias = write(QStringLiteral("master_bias.fits"), "Bias Frame", 1000.0);
    const QString dark = write(QStringLiteral("master_dark.fits"), "Dark Frame", 1300.0);
    const QString flat = write(QStringLiteral("master_flat.fits"), "Flat Field", 20000.0);
    WorkflowManifest::CalibrationSettings ccd;
    ccd.subtractBiasFromDark = true;
    ccd.cosmeticCorrection = true;
    WorkflowManifest::CalibrationSettings standard;

    core::CalibrationMasterSet masters;
    an::Result<an::consumer::OperationScope> scope =
        m_run.session().admit(an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::WorkflowStage});
    ASSERT_TRUE(scope.hasValue()) << scope.status().message();
    std::vector<std::shared_ptr<const core::LightCalibration>> prepared;
    for (int index = 0; index < 4; ++index)
    {
        addLight(QStringLiteral("light_%1.fits").arg(index), bias, dark, flat, index < 3 ? ccd : standard);
        auto calibration = masters.prepare(*scope, m_manifest.lightsCalibration.back());
        ASSERT_TRUE(calibration.hasValue()) << calibration.status().message();
        prepared.push_back(*calibration);
    }
    EXPECT_EQ(prepared[0], prepared[1]) << "lights naming the same masters and settings share one calibration";
    EXPECT_NE(prepared[0], prepared[3]);
    EXPECT_NE(prepared[0]->bias, nullptr) << "CCD mode subtracts the bias from the light too";
    EXPECT_EQ(prepared[3]->bias, nullptr) << "standard mode";
    EXPECT_NE(prepared[0]->dark, prepared[3]->dark) << "CCD mode's dark is the dark without its bias";
    ASSERT_NE(prepared[0]->coldMap, nullptr);
    ASSERT_NE(prepared[0]->hotMap, nullptr);
    const core::CalibrationMasterSetDiagnostics& diagnostics = masters.diagnostics();
    EXPECT_EQ(diagnostics.masterReads, 3u);
    EXPECT_EQ(diagnostics.processedDarkSubtractions, 1u);
    EXPECT_EQ(diagnostics.cosmeticMaps, 1u);
}

} // namespace
