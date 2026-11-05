// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The lights alignment stage: each registered light warped onto its group's plane - the reference
/// frame's, every light's union for a mosaic - and published stamped under the registered lights; a run that finds its
/// outputs current writes nothing; a group whose lights the stack warps as it reads them writes nothing once the stack
/// has confirmed their registrations; and a registration that left the header's distortion to the alignment has it
/// undone and counted.

#include "Test_ImageIntegration/workflow/CalibrationRun.h"
#include "Test_ImageIntegration/workflow/StarFieldFrames.h"

#include "processes/ImageIntegration/core/FilenameUtils.h"
#include "processes/ImageIntegration/workflow/stages/LightsAlignmentStage.h"
#include "processes/ImageIntegration/workflow/stages/LightsRegistrationStage.h"
#include "processes/ImageIntegration/workflow/stages/StageParsingUtils.h"
#include "processes/core/RegistrationMetricsStateLoader.h"
#include "processes/core/RegistrationMetricsWriter.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointF>
#include <QStringList>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{

using an::tests::fixtures::FixtureCard;
using an::tests::fixtures::FixtureStar;
using an::tests::integration::brightestNear;
using an::tests::integration::CalibrationRun;
using an::tests::integration::exposureStartCard;
using an::tests::integration::movedStars;
using an::tests::integration::readWrittenImage;
using an::tests::integration::starCatalogue;
using an::tests::integration::StarFieldPixel;
using an::tests::integration::tanSolutionCards;
using an::tests::integration::writeStarField;
using an::tests::integration::writtenIdentity;

class LightsAlignmentTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_root.isValid());
        ASSERT_TRUE(m_run.valid());
        ASSERT_TRUE(QDir().mkpath(QDir(m_root.path()).filePath(QStringLiteral("lights"))));
        m_manifest.outputDirectory = QDir(m_root.path()).filePath(QStringLiteral("output"));
        m_manifest.executionId = QStringLiteral("alignment-run");
        m_registration.groupKey = GroupKey{QStringLiteral("reg|160x120|Lum")};
        m_registration.settings.autoReferenceDuringRegistration = false;
        m_registration.settings.enrichWithGaiaCatalog = false;
        m_registration.settings.transformMode = QStringLiteral("Linear");
        m_registration.settings.transformation = QStringLiteral("Affine");
        m_integration.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|160x120|Lum")}};
        m_integration.registrationGroupKey = m_registration.groupKey;
    }

    QString writeLight(const QString& name, const std::vector<FixtureStar>& stars, std::vector<FixtureCard> cards,
                       std::uint64_t seed, const std::string& when = "2026-03-01T01:00:00",
                       const std::vector<StarFieldPixel>& notANumber = {})
    {
        const QString path = QDir(m_root.path()).filePath(QStringLiteral("lights/") + name + QStringLiteral(".fits"));
        cards.push_back(exposureStartCard(when));
        std::string error;
        EXPECT_TRUE(writeStarField(path, stars, cards, seed, error, 0.05, notANumber)) << error;
        return path;
    }

    void useLights(const std::vector<QString>& paths)
    {
        m_registration.lightFiles.clear();
        for (const QString& path : paths)
        {
            m_registration.lightFiles.emplace_back(path);
        }
        m_registration.referenceFrame = m_registration.lightFiles.front();
        m_registration.settings.referenceImage = m_registration.lightFiles.front().canonicalPath();
        m_integration.lightFiles = m_registration.lightFiles;
        m_manifest.registrationGroups = {m_registration};
        m_manifest.integrationGroups = {m_integration};
    }

    /// Two lights of one star field, the second moved by a whole number of pixels, registered.
    void registerTranslatedPair(double dx, double dy)
    {
        m_reference = starCatalogue(30u, 17u);
        m_referencePath = writeLight(QStringLiteral("light_001"), m_reference, {}, 71u);
        m_lightPath = writeLight(
            QStringLiteral("light_002"),
            movedStars(m_reference, [dx, dy](double x, double y) { return std::pair{x + dx, y + dy}; }), {}, 72u);
        useLights({m_referencePath, m_lightPath});
        LightsRegistrationStage registration;
        const StageResult registered = m_run.run(registration, m_manifest);
        ASSERT_TRUE(registered.isSuccess()) << registered.error.toLogString().toStdString();
    }

    StageResult align()
    {
        LightsAlignmentStage stage;
        return m_run.run(stage, m_manifest);
    }

    [[nodiscard]] QString registeredPath(const QString& light) const
    {
        return QDir(m_manifest.outputDirectory)
            .filePath(QStringLiteral("registered_lights/%1/%2_reg.fits")
                          .arg(ImageIntegration::sanitizeFilename(m_registration.groupKey.toString()),
                               QFileInfo(light).completeBaseName()));
    }

    [[nodiscard]] QString metricsPath() const
    {
        return QDir(m_manifest.outputDirectory).filePath(QStringLiteral("registration_metrics.db"));
    }

    QTemporaryDir m_root;
    CalibrationRun m_run;
    WorkflowManifest m_manifest;
    WorkflowManifest::RegistrationGroup m_registration;
    WorkflowManifest::IntegrationGroup m_integration;
    std::vector<FixtureStar> m_reference;
    QString m_referencePath;
    QString m_lightPath;
};

TEST_F(LightsAlignmentTest, EachRegisteredLightIsWarpedOntoTheReferenceAndPublishedStamped)
{
    registerTranslatedPair(4.0, -3.0);
    const StageResult result = align();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    ASSERT_EQ(result.alignmentGroupResults.size(), 1u);
    EXPECT_EQ(result.alignmentGroupResults.front().totalAligned, 2);
    EXPECT_EQ(result.alignmentGroupResults.front().sipCorrectedCount, 0);

    for (const QString& light : {m_referencePath, m_lightPath})
    {
        const QString output = registeredPath(light);
        ASSERT_TRUE(QFileInfo::exists(output)) << output.toStdString();
        const auto stamp = writtenIdentity(m_run.session(), output);
        ASSERT_TRUE(stamp.has_value()) << output.toStdString();
        EXPECT_EQ(stamp->runId, "alignment-run");
        EXPECT_FALSE(stamp->artifactId.empty());
        const auto written = readWrittenImage(output);
        ASSERT_TRUE(written.has_value());
        EXPECT_EQ(written->samples.width(), an::tests::integration::kStarFieldWidth);
        EXPECT_EQ(written->samples.height(), an::tests::integration::kStarFieldHeight);
    }

    // The moved light's stars sit where the reference's do.
    for (std::size_t index = 0; index < 5u; ++index)
    {
        const FixtureStar& star = m_reference[index];
        const auto brightest = brightestNear(registeredPath(m_lightPath), star.x, star.y, 3);
        ASSERT_TRUE(brightest.has_value());
        EXPECT_LE(std::abs(static_cast<double>(brightest->x) - star.x), 1.0) << "star " << index;
        EXPECT_LE(std::abs(static_cast<double>(brightest->y) - star.y), 1.0) << "star " << index;
    }

    // The registration database records where each light's registered image is.
    an::ui::RegistrationMetricsStateLoader loader(metricsPath(), QStringLiteral("AlignmentTestReader"));
    ASSERT_TRUE(loader.open());
    std::unordered_map<QString, an::ui::RegistrationMetricStatus> statuses;
    ASSERT_TRUE(
        loader.loadMetricStatuses(m_registration.groupKey.toString(), m_registration.settings.version, statuses));
    bool recorded = false;
    for (const auto& [frame, status] : statuses)
    {
        (void)frame;
        if (QFileInfo(status.outputPath) == QFileInfo(registeredPath(m_lightPath)))
        {
            recorded = true;
        }
    }
    EXPECT_TRUE(recorded);
}

TEST_F(LightsAlignmentTest, AWideRunPublishesWhatOneLightAtATimePublishes)
{
    // Wide enough to read the next light and write the last two while one is warped, each on a thread of its own, the
    // run publishes the registered lights a run of two threads publishes, sample for sample, and records every one.
    m_reference = starCatalogue(30u, 17u);
    std::vector<QString> paths{writeLight(QStringLiteral("light_001"), m_reference, {}, 71u)};
    for (std::uint32_t index = 2u; index <= 7u; ++index)
    {
        const double dx = 1.25 * index;
        const double dy = -0.75 * index;
        paths.push_back(
            writeLight(QStringLiteral("light_%1").arg(index, 3, 10, QLatin1Char('0')),
                       movedStars(m_reference, [dx, dy](double x, double y) { return std::pair{x + dx, y + dy}; }), {},
                       70u + index));
    }
    useLights(paths);
    LightsRegistrationStage registration;
    const StageResult registered = m_run.run(registration, m_manifest);
    ASSERT_TRUE(registered.isSuccess()) << registered.error.toLogString().toStdString();

    // The same registration copied for the wide run, so that only the alignment's width differs.
    const QString wideOutput = QDir(m_root.path()).filePath(QStringLiteral("wide-output"));
    ASSERT_TRUE(QDir().mkpath(wideOutput));
    ASSERT_TRUE(QFile::copy(metricsPath(), QDir(wideOutput).filePath(QStringLiteral("registration_metrics.db"))));

    const StageResult narrow = align();
    ASSERT_TRUE(narrow.isSuccess()) << narrow.error.toLogString().toStdString();

    CalibrationRun wide(16u);
    ASSERT_TRUE(wide.valid());
    WorkflowManifest wideManifest = m_manifest;
    wideManifest.outputDirectory = wideOutput;
    LightsAlignmentStage stage;
    const StageResult wideResult = wide.run(stage, wideManifest);
    ASSERT_TRUE(wideResult.isSuccess()) << wideResult.error.toLogString().toStdString();

    for (const QString& light : paths)
    {
        const QString narrowPath = registeredPath(light);
        const QString widePath =
            QDir(wideOutput).filePath(QDir(m_manifest.outputDirectory).relativeFilePath(narrowPath));
        const auto narrowImage = readWrittenImage(narrowPath);
        const auto wideImage = readWrittenImage(widePath);
        ASSERT_TRUE(narrowImage.has_value()) << narrowPath.toStdString();
        ASSERT_TRUE(wideImage.has_value()) << widePath.toStdString();
        EXPECT_EQ(wideImage->samples.width(), narrowImage->samples.width());
        EXPECT_EQ(wideImage->samples.height(), narrowImage->samples.height());
        EXPECT_EQ(wideImage->samples.digest(), narrowImage->samples.digest()) << light.toStdString();
    }
}

TEST_F(LightsAlignmentTest, ASecondRunFindsItsOutputsCurrentAndWritesNothing)
{
    registerTranslatedPair(2.0, 1.0);
    ASSERT_TRUE(align().isSuccess());
    const QDateTime written = QFileInfo(registeredPath(m_lightPath)).lastModified();
    ASSERT_TRUE(written.isValid());

    ASSERT_TRUE(align().isSuccess());
    EXPECT_EQ(QFileInfo(registeredPath(m_lightPath)).lastModified(), written);
}

TEST_F(LightsAlignmentTest, AMosaicIsAlignedOntoEveryLightsUnion)
{
    m_registration.settings.alignmentMode = QStringLiteral("mosaic");
    registerTranslatedPair(4.0, -3.0);
    ASSERT_TRUE(align().isSuccess());
    const auto written = readWrittenImage(registeredPath(m_lightPath));
    ASSERT_TRUE(written.has_value());
    // The fitted translation is four and three pixels to within a fraction, and the canvas covers it whole.
    EXPECT_NEAR(written->samples.width(), an::tests::integration::kStarFieldWidth + 4u, 1.0);
    EXPECT_NEAR(written->samples.height(), an::tests::integration::kStarFieldHeight + 3u, 1.0);
}

TEST_F(LightsAlignmentTest, LightsTheStackWarpsAsItReadsThemAreNotWritten)
{
    // A group whose registered images are discardable has its lights warped as the stack reads them, once the stack
    // confirms that every light it reads - the reference among them - is registered and its file is there.
    m_integration.stacking.registeredOutputPolicy = RegisteredOutputPolicy::Discardable;
    registerTranslatedPair(3.0, 2.0);

    ASSERT_TRUE(align().isSuccess());
    EXPECT_FALSE(QFileInfo::exists(registeredPath(m_lightPath)));
    EXPECT_FALSE(QFileInfo::exists(registeredPath(m_referencePath)));
}

TEST_F(LightsAlignmentTest, ARouteTheStackDoesNotConfirmIsWritten)
{
    // The settings would warp the lights as the stack reads them, but the group stacks only the moved light - one
    // light, without the registration's reference - which the stack does not confirm: the registered images are
    // written.
    m_integration.stacking.registeredOutputPolicy = RegisteredOutputPolicy::Discardable;
    registerTranslatedPair(3.0, 2.0);
    m_manifest.integrationGroups.front().lightFiles = {FrameId(m_lightPath)};

    ASSERT_TRUE(align().isSuccess());
    EXPECT_TRUE(QFileInfo::exists(registeredPath(m_lightPath)));
}

TEST_F(LightsAlignmentTest, AHeadersDistortionLeftToTheAlignmentIsUndoneAndCounted)
{
    const QString reference = writeLight(QStringLiteral("light_001"), {}, tanSolutionCards(80.5, 60.5, 2.0e-5), 81u);
    const QString light = writeLight(QStringLiteral("light_002"), {}, tanSolutionCards(80.5, 60.5, 2.0e-5), 82u);
    useLights({reference, light});

    // A registration recorded before the alignment, leaving each light's own distortion for the alignment to undo.
    an::ui::RegistrationMetricsWriter writer(metricsPath(), QStringLiteral("AlignmentTestWriter"));
    ASSERT_TRUE(QDir().mkpath(m_manifest.outputDirectory));
    ASSERT_TRUE(writer.initialize());
    an::ui::RegistrationGroupState state;
    state.groupKey = m_registration.groupKey.toString();
    state.settingsVersion = m_registration.settings.version;
    state.referenceFrameId = FrameId(reference).canonicalPath();
    state.detectionHash = QStringLiteral("detection");
    state.registrationHash = QStringLiteral("registration");
    state.lightFilesHash = QStringLiteral("lights");
    state.calibrationHash = QString::fromLatin1("");
    state.outputWcsJson = QString::fromLatin1("");
    QString error;
    ASSERT_TRUE(writer.upsertGroupState(state, &error)) << error.toStdString();
    for (const QString& path : {reference, light})
    {
        an::ui::RegistrationMetricRecord metric;
        metric.frameId = FrameId(path);
        metric.groupKey = state.groupKey;
        metric.settingsVersion = state.settingsVersion;
        metric.metrics.succeeded = true;
        metric.transformJson = stage_utils::serializeTransformJson(an::RegistrationTransform{});
        ASSERT_TRUE(writer.upsertMetric(metric, &error)) << error.toStdString();
    }

    const StageResult result = align();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    ASSERT_EQ(result.alignmentGroupResults.size(), 1u);
    EXPECT_EQ(result.alignmentGroupResults.front().totalAligned, 2);
    EXPECT_EQ(result.alignmentGroupResults.front().sipCorrectedCount, 2);

    // The registered image carries the plane's solution, which states no distortion of its own.
    const auto written = readWrittenImage(registeredPath(light));
    ASSERT_TRUE(written.has_value());
    EXPECT_TRUE(written->card("CTYPE1").has_value());
    EXPECT_FALSE(written->card("A_ORDER").has_value());
}

TEST_F(LightsAlignmentTest, ADistortionTheRegistrationAlreadyHoldsIsKeptOnThePlane)
{
    m_reference = starCatalogue(30u, 23u);
    const std::vector<FixtureCard> solved = tanSolutionCards(80.5, 60.5, 2.0e-5);
    m_referencePath = writeLight(QStringLiteral("light_001"), m_reference, solved, 101u);
    m_lightPath = writeLight(QStringLiteral("light_002"),
                             movedStars(m_reference, [](double x, double y) { return std::pair{x + 2.0, y + 1.0}; }),
                             tanSolutionCards(82.5, 61.5, 2.0e-5), 102u);
    useLights({m_referencePath, m_lightPath});
    LightsRegistrationStage registration;
    ASSERT_TRUE(m_run.run(registration, m_manifest).isSuccess());

    const StageResult result = align();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    ASSERT_EQ(result.alignmentGroupResults.size(), 1u);
    EXPECT_EQ(result.alignmentGroupResults.front().sipCorrectedCount, 0);
    // The stars were matched as the frames hold them, so the reference is not resampled and keeps its distortion.
    const auto written = readWrittenImage(registeredPath(m_lightPath));
    ASSERT_TRUE(written.has_value());
    EXPECT_TRUE(written->card("A_ORDER").has_value());
}

/// Where the comet's nucleus is in light `index`, in the neutral layer's coordinates: three pixels right and one and a
/// half down every ten minutes.
[[nodiscard]] std::pair<double, double> cometNucleus(int index)
{
    return {60.0 + 3.0 * index, 50.0 + 1.5 * index};
}

class CometAlignmentTest : public LightsAlignmentTest
{
protected:
    void SetUp() override
    {
        LightsAlignmentTest::SetUp();
        // A comet run's directory is named by its whole run identity, which a long temporary root would push past the
        // Windows path limit; the outputs go below a short root of their own.
        ASSERT_TRUE(m_shortRoot.isValid());
        m_manifest.outputDirectory = QDir(m_shortRoot.path()).filePath(QStringLiteral("output"));
    }

    /// Three lights of fixed stars and a moving comet, observed in the first and the last, registered on the comet; the
    /// second light's samples at the stated pixels are not a number.
    void registerComet(const std::vector<StarFieldPixel>& notANumberInTheSecond = {})
    {
        std::vector<FixtureStar> field;
        for (const FixtureStar& star : starCatalogue(30u, 29u))
        {
            if (std::hypot(star.x - 63.0, star.y - 51.5) > 14.0)
            {
                field.push_back(star);
            }
        }
        RegistrationSettings::CometTrackingSettings comet;
        // The second light's nucleus is measured inside a window no star reaches.
        comet.trackingOptions.searchRadiusPixels = 8.0;
        for (int index = 0; index < 3; ++index)
        {
            std::vector<FixtureStar> stars = field;
            stars.push_back(FixtureStar{cometNucleus(index).first, cometNucleus(index).second, 3.0, 0.8});
            const std::string when = "2026-03-01T01:" + std::to_string(index) + "0:00";
            m_lights.push_back(
                writeLight(QStringLiteral("light_00%1").arg(index + 1), stars,
                           {FixtureCard{"EXPTIME", "60", {}, an::tests::fixtures::FixtureCardKind::Real}},
                           111u + static_cast<std::uint64_t>(index), when,
                           index == 1 ? notANumberInTheSecond : std::vector<StarFieldPixel>{}));
            m_registration.lightCaptureTimestampsUtc.push_back(QString::fromStdString(when));
            m_registration.lightExposureSeconds.push_back(60.0);
        }
        for (const int index : {0, 2})
        {
            RegistrationSettings::CometTrackingObservation observation;
            observation.frameId = FrameId(m_lights[static_cast<std::size_t>(index)]).canonicalPath();
            observation.sourcePosition = QPointF(cometNucleus(index).first + 0.5, cometNucleus(index).second + 0.5);
            comet.observations.push_back(observation);
        }
        m_registration.settings.registrationMethod = QStringLiteral("comet_tracking");
        m_registration.settings.cometTracking = comet;
        m_integration.stacking.cometStackingMode = QStringLiteral("Comet");
        useLights(m_lights);

        LightsRegistrationStage registration;
        const StageResult registered = m_run.run(registration, m_manifest);
        ASSERT_TRUE(registered.isSuccess()) << registered.error.toLogString().toStdString();
    }

    /// The comet run the registration recorded.
    [[nodiscard]] QString cometRunId() const
    {
        an::ui::RegistrationMetricsStateLoader loader(metricsPath(), QStringLiteral("CometAlignmentTestRun"));
        EXPECT_TRUE(loader.open());
        const auto state = loader.loadGroupState(m_registration.groupKey.toString(), m_registration.settings.version);
        if (!state.has_value())
        {
            ADD_FAILURE() << "no registration state";
            return {};
        }
        const auto run =
            loader.loadActiveCometRunForContext(state->groupKey, state->registrationHash, state->lightFilesHash,
                                                state->calibrationHash, state->referenceFrameId);
        if (!run.has_value())
        {
            ADD_FAILURE() << "no active comet run";
            return {};
        }
        return run->cometRunId;
    }

    [[nodiscard]] QDir cometRunDirectory() const
    {
        return QDir(QDir(m_manifest.outputDirectory)
                        .filePath(QStringLiteral("registered_lights/%1/comet_aligned/%2")
                                      .arg(ImageIntegration::sanitizeFilename(m_registration.groupKey.toString()),
                                           ImageIntegration::sanitizeFilename(cometRunId()))));
    }

    QTemporaryDir m_shortRoot{QDir(QDir::tempPath()).filePath(QStringLiteral("c-XXXXXX"))};
    std::vector<QString> m_lights;
};

TEST_F(CometAlignmentTest, ACometRegistrationAlignsEachLightOnTheNucleus)
{
    registerComet();
    const StageResult result = align();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();

    const QDir run_dir = cometRunDirectory();
    const QStringList outputs = run_dir.entryList({QStringLiteral("*_comet_reg.fits")}, QDir::Files);
    ASSERT_EQ(outputs.size(), 3) << outputs.join(QStringLiteral(", ")).toStdString() << "\n"
                                 << m_run.loggedWarnings().join(QStringLiteral("\n")).toStdString();
    for (const QString& name : outputs)
    {
        const QString output = run_dir.filePath(name);
        const auto brightest = brightestNear(output, cometNucleus(0).first, cometNucleus(0).second, 4);
        ASSERT_TRUE(brightest.has_value()) << output.toStdString();
        EXPECT_LE(std::abs(static_cast<double>(brightest->x) - cometNucleus(0).first), 1.0) << output.toStdString();
        EXPECT_LE(std::abs(static_cast<double>(brightest->y) - cometNucleus(0).second), 1.0) << output.toStdString();
        EXPECT_TRUE(writtenIdentity(m_run.session(), output).has_value()) << output.toStdString();
    }
}

// A light calibrated where its flat is zero is not a number there. A sample of the second light inside the window its
// nucleus is measured in is not a number, and the nucleus is still refined over the window's finite samples; the
// refinement refused the whole window, and the light kept the position the track predicted.
TEST_F(CometAlignmentTest, ANucleusIsRefinedOverTheFiniteSamplesOfItsWindow)
{
    registerComet({StarFieldPixel{66u, 49u}});
    an::ui::RegistrationMetricsStateLoader loader(metricsPath(), QStringLiteral("CometAlignmentTestPoints"));
    ASSERT_TRUE(loader.open());
    std::vector<an::ui::CometTrackingPointRecord> points;
    QString error;
    ASSERT_TRUE(loader.loadCometTrackingPoints(cometRunId(), points, &error)) << error.toStdString();
    const FrameId second(m_lights[1]);
    const auto point = std::find_if(points.begin(), points.end(), [&second](const an::ui::CometTrackingPointRecord& p) {
        return p.originalFrameId == second;
    });
    ASSERT_NE(point, points.end());
    EXPECT_EQ(point->measurementMethod, QStringLiteral("auto_refine")) << point->rejectionReason.toStdString();
    EXPECT_NEAR(point->sourcePosition.x(), cometNucleus(1).first + 0.5, 0.5);
    EXPECT_NEAR(point->sourcePosition.y(), cometNucleus(1).second + 0.5, 0.5);
}

TEST_F(CometAlignmentTest, AnOutputThatCannotBeWrittenIsRecordedAsAFailedRole)
{
    registerComet();
    // A directory where the second light's comet-aligned image would go.
    const QDir run_dir = cometRunDirectory();
    ASSERT_TRUE(QDir().mkpath(run_dir.filePath(QStringLiteral("light_002_comet_reg.fits"))));

    const StageResult result = align();
    ASSERT_FALSE(result.isSuccess());
    EXPECT_FALSE(result.error.toLogString().contains(QStringLiteral("readiness")))
        << result.error.toLogString().toStdString();
    EXPECT_TRUE(result.error.toLogString().contains(QStringLiteral("light_002")))
        << result.error.toLogString().toStdString();

    an::ui::RegistrationMetricsStateLoader loader(metricsPath(), QStringLiteral("CometAlignmentTestReadiness"));
    ASSERT_TRUE(loader.open());
    const auto readiness = loader.loadCometRoleReadiness(cometRunId(), QStringLiteral("comet_aligned"));
    ASSERT_TRUE(readiness.has_value());
    EXPECT_EQ(readiness->readinessStatus, QStringLiteral("Failed"));
    EXPECT_EQ(readiness->failedOutputCount, 1);
    EXPECT_EQ(readiness->materializedOutputCount, 2);
}

TEST_F(LightsAlignmentTest, TheStageIsRefusedOutsideARun)
{
    registerTranslatedPair(1.0, 1.0);
    LightsAlignmentStage stage;
    CancellationToken token;
    ProgressReporter reporter;
    EXPECT_FALSE(stage.execute(m_manifest, token, reporter).isSuccess());
    EXPECT_FALSE(QFileInfo::exists(registeredPath(m_lightPath)));
}

} // namespace
