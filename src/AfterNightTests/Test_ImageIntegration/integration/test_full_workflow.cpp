// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The integration workflow end to end, over frames the neutral layer writes: the calibration
/// masters, the lights' calibration on disk or in memory, their registration, alignment, normalization, drizzle and
/// stack, each run committed to the history; a second run reusing what the first made and an incremental one
/// recomputing only what changed; the registered images kept or read through the registration's transforms; a stop
/// asked for during the stack; and the failures a run reports by name. Every run is the product's - the default
/// stages and the archiver, in one engine session, the stage companions answering as the stages do.

#include "Test_ImageIntegration/ImportSession.h"
#include "Test_ImageIntegration/TempDirFixture.h"
#include "Test_ImageIntegration/workflow/CalibrationRun.h"
#include "Test_ImageIntegration/workflow/StarFieldFrames.h"

#include "fixtures/common/FixtureFits.h"

#include "processes/ImageIntegration/core/FilenameUtils.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/tabs/WorkflowHistoryModel.h"
#include "processes/ImageIntegration/workflow/ArtifactTracker.h"
#include "processes/ImageIntegration/workflow/DefaultWorkflowStages.h"
#include "processes/ImageIntegration/workflow/ExecutionPlanner.h"
#include "processes/ImageIntegration/workflow/ExecutionPreparationSnapshot.h"
#include "processes/ImageIntegration/workflow/FrameHeaderReader.h"
#include "processes/ImageIntegration/workflow/ManifestBuilder.h"
#include "processes/ImageIntegration/reporting/ReportArchiveReader.h"
#include "processes/ImageIntegration/reporting/ReportModel.h"
#include "processes/ImageIntegration/workflow/WorkflowArchiver.h"
#include "processes/ImageIntegration/workflow/WorkflowExecutor.h"
#include "processes/ImageIntegration/workflow/WorkflowManifest.h"
#include "processes/ImageIntegration/workflow/WorkflowPreparation.h"
#include "processes/ImageIntegration/workflow/stages/BiasStackingStage.h"
#include "processes/ImageIntegration/workflow/stages/DarkStackingStage.h"
#include "processes/ImageIntegration/workflow/stages/FlatStackingStage.h"
#include "processes/ImageIntegration/workflow/stages/LightsCalibrationStage.h"
#include "processes/ImageIntegration/workflow/stages/LightsRegistrationStage.h"
#include "processes/ImageIntegration/workflow/stages/NormalizationArtifactSupport.h"
#include "processes/ImageIntegration/workflow/stages/StackDrizzlePixmapIndex.h"
#include "processes/ImageIntegration/workflow/stages/StageParsingUtils.h"

#include <AfterNightConsumerRuntime/EngineSession.h>

#include <AfterNightEngine/Algorithms/Registration/RegistrationTransform.h>

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimeZone>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{

using an::tests::fixtures::FixtureSampleType;
using an::tests::fixtures::FixtureStar;
using an::tests::integration::kStarFieldHeight;
using an::tests::integration::kStarFieldWidth;
using an::tests::integration::readWrittenImage;

/// Every frame's width and height: the star field's.
constexpr std::uint32_t kWidth = kStarFieldWidth;
constexpr std::uint32_t kHeight = kStarFieldHeight;

/// The frames' size as the manifest's groups state it.
const QString kResolution = QStringLiteral("%1x%2").arg(kWidth).arg(kHeight);

QString ensureDir(const QString& path)
{
    QDir dir(path);
    if (!dir.exists())
    {
        dir.mkpath(QStringLiteral("."));
    }
    return dir.path();
}

/// A fixture's name from a file name: the description takes plain letters, digits, dots, hyphens and underscores only,
/// so every other character - a file name may hold any - is written as an underscore. The file keeps its own name.
std::string fixtureNameOf(const QString& filename)
{
    std::string name;
    for (const QChar character : QFileInfo(filename).completeBaseName())
    {
        const char16_t unit = character.unicode();
        const bool plain = (unit >= u'a' && unit <= u'z') || (unit >= u'A' && unit <= u'Z') ||
                           (unit >= u'0' && unit <= u'9') || unit == u'.' || unit == u'-' || unit == u'_';
        name.push_back(plain ? static_cast<char>(unit) : '_');
    }
    return name;
}

/// Writes a description by the neutral FITS writer; fails the case when it cannot.
void writeDescription(const an::tests::fixtures::FixtureImage& image, const QString& path)
{
    std::string error;
    const std::optional<an::tests::fixtures::FixtureSamples> samples =
        an::tests::fixtures::materializeFixture(image, error);
    ASSERT_TRUE(samples.has_value()) << error;
    ASSERT_TRUE(
        an::tests::fixtures::writeFixtureFits(image, *samples, std::filesystem::path(path.toStdWString()), error))
        << error;
}

/// A grey Float32 frame of one level, the size of the star fields, measured through the filter array named.
QString writeConstantImage(const QString& directory, const QString& filename, float value,
                           const an::tests::fixtures::FixtureCfa cfa = an::tests::fixtures::FixtureCfa::None)
{
    an::tests::fixtures::FixtureImage image;
    image.name = fixtureNameOf(filename);
    image.width = kWidth;
    image.height = kHeight;
    image.sampleType = FixtureSampleType::Float32;
    image.cfa = cfa;
    image.pattern = an::tests::fixtures::FixturePattern::Constant;
    an::tests::fixtures::FixtureChannelLevels levels;
    levels.base = static_cast<double>(value);
    image.levels = {levels};

    const QString path = QDir(ensureDir(directory)).filePath(filename);
    writeDescription(image, path);
    return path;
}

/// A grey star field over a faint noisy background, in a representation of its own: the samples are fractions of full
/// scale in Float32 and the same fractions in codes for an integer representation, measured through the filter array
/// named.
QString writeStarLight(const QString& directory, const QString& filename, const std::vector<FixtureStar>& stars,
                       FixtureSampleType sampleType = FixtureSampleType::Float32,
                       const an::tests::fixtures::FixtureCfa cfa = an::tests::fixtures::FixtureCfa::None)
{
    const double scale = sampleType == FixtureSampleType::Float32 ? 1.0 : 65535.0;
    an::tests::fixtures::FixtureImage image;
    image.name = fixtureNameOf(filename);
    image.width = kWidth;
    image.height = kHeight;
    image.sampleType = sampleType;
    image.pattern = an::tests::fixtures::FixturePattern::StarField;
    image.seed = static_cast<std::uint64_t>(qHash(filename)) + 1u;
    an::tests::fixtures::FixtureChannelLevels levels;
    levels.base = 0.05 * scale;
    levels.amplitude = 0.002 * scale;
    levels.alternate = scale;
    image.levels = {levels};
    image.stars = stars;
    image.cfa = cfa;

    const QString path = QDir(ensureDir(directory)).filePath(filename);
    writeDescription(image, path);
    return path;
}

/// The stars every light of a test shares, moved by `offset` pixels along both axes.
std::vector<FixtureStar> fieldStars(int offset, std::size_t count = 30u, std::uint64_t seed = 11u)
{
    return an::tests::integration::movedStars(
        an::tests::integration::starCatalogue(count, seed),
        [offset](double x, double y) { return std::pair{x + offset, y + offset}; });
}

/// A light of the shared star field moved by `offset` pixels.
QString writeStarField(const QString& directory, const QString& filename, int offset)
{
    return writeStarLight(directory, filename, fieldStars(offset));
}

/// A light of a denser field of stars, moved by `offset` pixels, in the representation asked for.
QString writeStrongStarField(const QString& directory, const QString& filename,
                             const FixtureSampleType sampleType = FixtureSampleType::Float32, const int offset = 0)
{
    return writeStarLight(directory, filename, fieldStars(offset, 40u, 23u), sampleType);
}

/// The calibration masters' stages and the lights' calibration alone, committed through the archiver; or, with the
/// registration, the default executor's nine stages.
std::unique_ptr<WorkflowExecutor> buildExecutor(bool include_registration)
{
    auto executor = std::make_unique<WorkflowExecutor>();
    if (include_registration)
    {
        ImageIntegration::composeDefaultWorkflowStages(*executor);
        return executor;
    }
    executor->addStage(std::make_unique<BiasStackingStage>());
    executor->addStage(std::make_unique<DarkStackingStage>());
    executor->addStage(std::make_unique<FlatStackingStage>());
    executor->addStage(std::make_unique<LightsCalibrationStage>());
    executor->setRunArchiveFactory(&makeWorkflowArchiverRunArchive);
    return executor;
}

WorkflowManifest buildManifest(const QString& output_dir, const std::vector<FrameId>& bias_frames,
                               const std::vector<FrameId>& dark_frames, const std::vector<FrameId>& flat_frames,
                               const std::vector<FrameId>& light_frames)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = output_dir;

    WorkflowManifest::CalibrationStage bias_stage;
    bias_stage.frameType = QStringLiteral("BIAS");
    WorkflowManifest::CalibrationStage::Group bias_group;
    bias_group.groupKey = GroupKey{QStringLiteral("bias|%1").arg(kResolution)};
    bias_group.outputFilename = QStringLiteral("master_bias.fit");
    bias_group.inputFiles = bias_frames;
    bias_stage.groups.push_back(std::move(bias_group));
    manifest.biasStage = bias_stage;

    WorkflowManifest::CalibrationStage dark_stage;
    dark_stage.frameType = QStringLiteral("DARK");
    WorkflowManifest::CalibrationStage::Group dark_group;
    dark_group.groupKey = GroupKey{QStringLiteral("dark|%1").arg(kResolution)};
    dark_group.outputFilename = QStringLiteral("master_dark.fit");
    dark_group.inputFiles = dark_frames;
    dark_stage.groups.push_back(std::move(dark_group));
    manifest.darkStage = dark_stage;

    WorkflowManifest::CalibrationStage flat_stage;
    flat_stage.frameType = QStringLiteral("FLAT");
    WorkflowManifest::CalibrationStage::Group flat_group;
    flat_group.groupKey = GroupKey{QStringLiteral("flat|%1|L").arg(kResolution)};
    flat_group.outputFilename = QStringLiteral("master_flat.fit");
    flat_group.inputFiles = flat_frames;
    flat_stage.groups.push_back(std::move(flat_group));
    manifest.flatStage = flat_stage;

    const QString bias_path = QDir(output_dir).filePath(QStringLiteral("master_bias.fit"));
    const QString dark_path = QDir(output_dir).filePath(QStringLiteral("master_dark.fit"));
    const QString flat_path = QDir(output_dir).filePath(QStringLiteral("master_flat.fit"));

    for (const auto& light : light_frames)
    {
        WorkflowManifest::LightsCalibrationParams params;
        params.lightPath = light;
        params.biasPath = FrameId(bias_path);
        params.darkPath = FrameId(dark_path);
        params.flatPath = FrameId(flat_path);
        manifest.lightsCalibration.push_back(std::move(params));
    }

    WorkflowManifest::RegistrationGroup registration;
    registration.groupKey = GroupKey{QStringLiteral("reg|%1|TestObject").arg(kResolution)};
    registration.resolution = kResolution;
    registration.object = QStringLiteral("TestObject");
    registration.lightFiles = light_frames;
    registration.referenceFrame = light_frames.front();
    registration.settings.version = 1;
    registration.settings.enrichWithGaiaCatalog = false;
    manifest.registrationGroups.push_back(registration);

    WorkflowManifest::IntegrationGroup integration;
    integration.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|TestObject|L")}};
    integration.outputFilename = QStringLiteral("stacked_TestObject_L.fit");
    integration.resolution = kResolution;
    integration.object = QStringLiteral("TestObject");
    integration.filter = QStringLiteral("L");
    integration.exposure = QStringLiteral("60");
    integration.lightFiles = light_frames;
    integration.normalizationReferenceFrames = {light_frames.front()};
    integration.registrationGroupKey = registration.groupKey;
    integration.normalizationCoefficientsArtifactPath =
        QDir(output_dir)
            .filePath(QStringLiteral(
                ".afternight/image_integration/artifacts/normalization/int_TestObject_L/coefficients.json"));

    WorkflowManifest::IntegrationGroup::DrizzleSettings drizzle;
    drizzle.enabled = true;
    drizzle.scale = 1.0f;
    drizzle.pixelFraction = 1.0f;
    drizzle.kernel = QStringLiteral("square");
    integration.drizzle = drizzle;
    integration.drizzlePixmapArtifactDir = QDir(output_dir).filePath(QStringLiteral("pixmaps"));

    manifest.integrationGroups.push_back(std::move(integration));

    return manifest;
}

/// A registration group of the lights stated, the first its reference, measured without the Gaia catalog.
WorkflowManifest::RegistrationGroup registrationOf(const QString& key, const std::vector<FrameId>& lights)
{
    WorkflowManifest::RegistrationGroup registration;
    registration.groupKey = GroupKey{key};
    registration.resolution = kResolution;
    registration.object = QStringLiteral("TestObject");
    registration.lightFiles = lights;
    registration.referenceFrame = lights.empty() ? std::optional<FrameId>{} : std::optional<FrameId>{lights.front()};
    registration.settings.enrichWithGaiaCatalog = false;
    return registration;
}

QString calibratedPathFor(const QString& output_dir, const FrameId& original)
{
    const QFileInfo info(original.canonicalPath());
    const QString filename = info.completeBaseName() + QStringLiteral("_cal.") + info.suffix();
    return QDir(output_dir).filePath(QStringLiteral("calibrated_lights/%1").arg(filename));
}

QString formatLogMessages(const QStringList& logs)
{
    return logs.join(QLatin1Char('\n'));
}

qint64 fileMtimeMs(const QString& path)
{
    const QFileInfo info(path);
    if (!info.exists())
    {
        return -1;
    }
    return info.lastModified().toMSecsSinceEpoch();
}

int countFiles(const QString& directory)
{
    QDirIterator files(directory, QDir::Files, QDirIterator::Subdirectories);
    int count = 0;
    while (files.hasNext())
    {
        files.next();
        ++count;
    }
    return count;
}

/// Compares two written images sample by sample, read by the neutral reader, independently of the engine.
void expectSameSamples(const QString& expectedPath, const QString& actualPath, double tolerance)
{
    const std::optional<an::tests::fixtures::FixtureFitsFile> expected = readWrittenImage(expectedPath);
    const std::optional<an::tests::fixtures::FixtureFitsFile> actual = readWrittenImage(actualPath);
    ASSERT_TRUE(expected.has_value()) << expectedPath.toStdString();
    ASSERT_TRUE(actual.has_value()) << actualPath.toStdString();
    ASSERT_EQ(actual->samples.width(), expected->samples.width());
    ASSERT_EQ(actual->samples.height(), expected->samples.height());
    ASSERT_EQ(actual->samples.channelCount(), expected->samples.channelCount());
    std::size_t mismatches = 0u;
    std::string first;
    for (std::uint32_t channel = 0u; channel < expected->samples.channelCount(); ++channel)
    {
        for (std::uint32_t y = 0u; y < expected->samples.height(); ++y)
        {
            for (std::uint32_t x = 0u; x < expected->samples.width(); ++x)
            {
                const double want = expected->samples.value(x, y, channel);
                const double got = actual->samples.value(x, y, channel);
                if (!(std::abs(got - want) <= tolerance))
                {
                    if (mismatches == 0u)
                    {
                        first = "x=" + std::to_string(x) + " y=" + std::to_string(y) +
                                " channel=" + std::to_string(channel) + " expected " + std::to_string(want) + " got " +
                                std::to_string(got);
                    }
                    ++mismatches;
                }
            }
        }
    }
    EXPECT_EQ(mismatches, 0u) << "first: " << first;
}

} // namespace

class FullWorkflowFixture : public TempDirFixture
{
protected:
    void SetUp() override
    {
        TempDirFixture::SetUp();
        if (HasFatalFailure())
        {
            return;
        }
        an::Result<an::consumer::EngineSession> session = an::tests::integration::openImportSession();
        ASSERT_TRUE(session.hasValue()) << session.status().message();
        m_session.emplace(std::move(session).value());
    }

    /// The run's options: the case's session.
    [[nodiscard]] WorkflowExecutionOptions options() const
    {
        WorkflowExecutionOptions runOptions;
        runOptions.session = &*m_session;
        return runOptions;
    }

    WorkflowExecutor::Result execute(WorkflowExecutor& executor, const WorkflowManifest& manifest) const
    {
        return executor.execute(manifest, {}, options());
    }

    WorkflowExecutor::Result executeWithLogs(WorkflowExecutor& executor, const WorkflowManifest& manifest,
                                             QStringList& logs) const
    {
        executor.setLogCallback([&](WorkflowLogLevel level, const QString& message) {
            QString prefix = QStringLiteral("Info");
            switch (level)
            {
            case WorkflowLogLevel::Warning:
                prefix = QStringLiteral("Warning");
                break;
            case WorkflowLogLevel::Error:
                prefix = QStringLiteral("Error");
                break;
            case WorkflowLogLevel::Info:
            default:
                break;
            }
            logs.append(QStringLiteral("[%1] %2").arg(prefix, message));
        });

        return execute(executor, manifest);
    }

    std::optional<an::consumer::EngineSession> m_session;
};

TEST_F(FullWorkflowFixture, ExecutesEndToEndWithRecoveryAndIncrementalRuns)
{
    const QString output_dir = tempPath();

    const QString bias_a = writeConstantImage(output_dir, QStringLiteral("bias_a.fit"), 0.01f);
    const QString bias_b = writeConstantImage(output_dir, QStringLiteral("bias_b.fit"), 0.01f);
    const QString dark_a = writeConstantImage(output_dir, QStringLiteral("dark_a.fit"), 0.02f);
    const QString dark_b = writeConstantImage(output_dir, QStringLiteral("dark_b.fit"), 0.02f);
    const QString flat_a = writeConstantImage(output_dir, QStringLiteral("flat_a.fit"), 0.8f);
    const QString flat_b = writeConstantImage(output_dir, QStringLiteral("flat_b.fit"), 0.8f);

    const QString light_a = writeStrongStarField(output_dir, QStringLiteral("light_a.fit"));
    const QString light_b = writeStrongStarField(output_dir, QStringLiteral("light_b.fit"));

    WorkflowManifest manifest =
        buildManifest(output_dir, {FrameId(bias_a), FrameId(bias_b)}, {FrameId(dark_a), FrameId(dark_b)},
                      {FrameId(flat_a), FrameId(flat_b)}, {FrameId(light_a), FrameId(light_b)});

    // Explicitly disable in-memory calibration for this test as it verifies
    // the existence of intermediate calibrated files on disk for recovery.
    manifest.calibrateLightsInMemory = false;

    {
        auto executor = buildExecutor(false);
        QStringList logs;
        auto result = executeWithLogs(*executor, manifest, logs);
        EXPECT_TRUE(result.success) << result.errorMessage.toStdString() << "\n"
                                    << formatLogMessages(logs).toStdString();
    }

    EXPECT_TRUE(QFileInfo::exists(QDir(output_dir).filePath(QStringLiteral("master_bias.fit"))));
    EXPECT_TRUE(QFileInfo::exists(QDir(output_dir).filePath(QStringLiteral("master_dark.fit"))));
    EXPECT_TRUE(QFileInfo::exists(QDir(output_dir).filePath(QStringLiteral("master_flat.fit"))));

    EXPECT_TRUE(QFileInfo::exists(calibratedPathFor(output_dir, FrameId(light_a))));
    EXPECT_TRUE(QFileInfo::exists(calibratedPathFor(output_dir, FrameId(light_b))));

    if (manifest.biasStage.has_value())
    {
        for (auto& group : manifest.biasStage->groups)
        {
            group.skipIfExists = true;
        }
    }
    if (manifest.darkStage.has_value())
    {
        for (auto& group : manifest.darkStage->groups)
        {
            group.skipIfExists = true;
        }
    }
    if (manifest.flatStage.has_value())
    {
        for (auto& group : manifest.flatStage->groups)
        {
            group.skipIfExists = true;
        }
    }

    {
        auto executor = buildExecutor(true);
        QStringList logs;
        auto result = executeWithLogs(*executor, manifest, logs);
        EXPECT_TRUE(result.success) << result.errorMessage.toStdString() << "\n"
                                    << formatLogMessages(logs).toStdString();

        // The default executor runs the lights' stages it does not skip in the workflow's order, the drizzle before
        // the stack.
        const QStringList lightsStages = {QStringLiteral("LightsRegistration"), QStringLiteral("LightsAlignment"),
                                          QStringLiteral("LightsNormalization"), QStringLiteral("LightsDrizzle"),
                                          QStringLiteral("LightsStacking")};
        QStringList ran;
        for (const auto& stage : result.stageResults)
        {
            if (lightsStages.contains(stage.stageName) && !ran.contains(stage.stageName))
            {
                ran.append(stage.stageName);
            }
        }
        QStringList inWorkflowOrder;
        for (const QString& stage : lightsStages)
        {
            if (ran.contains(stage))
            {
                inWorkflowOrder.append(stage);
            }
        }
        EXPECT_TRUE(ran.contains(QStringLiteral("LightsDrizzle")) && ran.contains(QStringLiteral("LightsStacking")))
            << ran.join(QStringLiteral(", ")).toStdString();
        EXPECT_EQ(ran.join(QStringLiteral(", ")).toStdString(),
                  inWorkflowOrder.join(QStringLiteral(", ")).toStdString());
    }

    const QString metrics_path = QDir(output_dir).filePath(QStringLiteral("registration_metrics.db"));
    EXPECT_TRUE(QFileInfo::exists(metrics_path));

    // The normalization's coefficient document and the sidecar naming it.
    const QString coefficients_path = manifest.integrationGroups.front().normalizationCoefficientsArtifactPath.value();
    EXPECT_TRUE(QFileInfo::exists(coefficients_path));
    EXPECT_TRUE(QFileInfo::exists(ImageIntegration::normalizationSidecarPath(coefficients_path)));

    // The drizzle's group sidecar listing its descriptors.
    const QString drizzle_directory =
        ImageIntegration::drizzleDescriptorDirectory(manifest, manifest.integrationGroups.front());
    EXPECT_TRUE(QFileInfo::exists(ImageIntegration::drizzleGroupSidecarPath(drizzle_directory)));

    const QString output_path = QDir(output_dir).filePath(QStringLiteral("stacked_TestObject_L.fit"));
    EXPECT_TRUE(QFileInfo::exists(output_path));

    const QString history_dir = QDir(output_dir).filePath(QStringLiteral("history"));
    EXPECT_TRUE(QDir(history_dir).exists());

    WorkflowHistoryModel historyModel;
    historyModel.setHistoryDirectory(history_dir);
    ASSERT_GT(historyModel.rowCount(), 0);

    const QModelIndex latestRun = historyModel.index(0, 0);
    ASSERT_TRUE(latestRun.isValid());
    EXPECT_FALSE(latestRun.data(WorkflowHistoryModel::ManifestPathRole).toString().isEmpty());
    EXPECT_FALSE(latestRun.data(WorkflowHistoryModel::ReportPathRole).toString().isEmpty());

    const QString archivedManifestPath = latestRun.data(WorkflowHistoryModel::ManifestPathRole).toString();
    QFile manifestFile(archivedManifestPath);
    ASSERT_TRUE(manifestFile.open(QIODevice::ReadOnly));
    const QJsonObject archivedRoot = QJsonDocument::fromJson(manifestFile.readAll()).object();
    EXPECT_EQ(archivedRoot.value(QStringLiteral("schemaVersion")).toInt(), 3);
    EXPECT_GT(archivedRoot.value(QStringLiteral("integrationGroupResults")).toArray().size(), 0);

    manifest.integrationGroups.front().skipIfExists = true;

    {
        auto executor = buildExecutor(true);
        QStringList logs;
        auto result = executeWithLogs(*executor, manifest, logs);
        EXPECT_TRUE(result.success) << result.errorMessage.toStdString() << "\n"
                                    << formatLogMessages(logs).toStdString();
    }
}

TEST_F(FullWorkflowFixture, IncrementalLocalNormalizationOnlyRecomputesDirtyIntegrationGroup)
{
    const QString output_dir = tempPath();

    std::vector<FrameId> lights;
    lights.reserve(10);
    for (int i = 0; i < 10; ++i)
    {
        const QString light = writeStrongStarField(output_dir, QStringLiteral("light_%1.fit").arg(i));
        lights.emplace_back(light);
    }

    WorkflowManifest manifest = buildManifest(output_dir, {}, {}, {}, lights);
    manifest.calibrateLightsInMemory = true;
    manifest.lightsCalibration.clear();
    manifest.biasStage.reset();
    manifest.darkStage.reset();
    manifest.flatStage.reset();

    ASSERT_EQ(manifest.registrationGroups.size(), 1u);
    const GroupKey shared_registration_key = manifest.registrationGroups.front().groupKey;
    manifest.integrationGroups.clear();

    for (int group_index = 0; group_index < 5; ++group_index)
    {
        WorkflowManifest::IntegrationGroup group;
        group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|TestObject|L|%1").arg(group_index)}};
        group.outputFilename = QStringLiteral("stacked_TestObject_L_%1.fit").arg(group_index);
        group.resolution = kResolution;
        group.object = QStringLiteral("TestObject");
        group.filter = QStringLiteral("L");
        group.exposure = QStringLiteral("60");
        group.registrationGroupKey = shared_registration_key;
        group.lightFiles = {lights[2 * group_index], lights[2 * group_index + 1]};
        group.normalizationReferenceFrames = {lights[2 * group_index]};
        group.stacking.normalizationMode = NormalizationMode::Local;
        group.stacking.localNormalization.enabled = true;
        group.stacking.localNormalization.gridSize = 64;
        group.stacking.localNormalization.backgroundSmoothingRadius = 48;
        group.normalizationCoefficientsArtifactPath =
            QDir(output_dir)
                .filePath(
                    QStringLiteral(
                        ".afternight/image_integration/artifacts/normalization/int_TestObject_L_%1/coefficients.json")
                        .arg(group_index));
        group.drizzle.reset();
        manifest.integrationGroups.push_back(std::move(group));
    }

    auto executor = buildExecutor(true);
    QStringList logs;
    const auto first_result = executeWithLogs(*executor, manifest, logs);
    ASSERT_TRUE(first_result.success) << first_result.errorMessage.toStdString() << "\n"
                                      << formatLogMessages(logs).toStdString();

    const QString registration_db = QDir(output_dir).filePath(QStringLiteral("registration_metrics.db"));
    ASSERT_TRUE(QFileInfo::exists(registration_db));

    std::vector<qint64> stack_mtime_before;
    std::vector<qint64> coeff_mtime_before;
    stack_mtime_before.reserve(manifest.integrationGroups.size());
    coeff_mtime_before.reserve(manifest.integrationGroups.size());
    for (const auto& group : manifest.integrationGroups)
    {
        const QString stack_path = QDir(output_dir).filePath(group.outputFilename);
        ASSERT_TRUE(QFileInfo::exists(stack_path));
        ASSERT_TRUE(group.normalizationCoefficientsArtifactPath.has_value());
        ASSERT_TRUE(QFileInfo::exists(group.normalizationCoefficientsArtifactPath.value()));
        stack_mtime_before.push_back(fileMtimeMs(stack_path));
        coeff_mtime_before.push_back(fileMtimeMs(group.normalizationCoefficientsArtifactPath.value()));
    }

    const qint64 registration_mtime_before = fileMtimeMs(registration_db);

    std::this_thread::sleep_for(std::chrono::milliseconds(1200));

    const int dirty_group_index = 2;
    manifest.integrationGroups[dirty_group_index].stacking.localNormalization.gridSize = 96;
    manifest.executionMode = ExecutionMode::Incremental;
    manifest.incrementalBiasGroups.clear();
    manifest.incrementalDarkGroups.clear();
    manifest.incrementalFlatGroups.clear();
    manifest.incrementalRegistrationGroups.clear();
    manifest.incrementalIntegrationGroups = {manifest.integrationGroups[dirty_group_index].groupKey};

    QStringList incremental_logs;
    const auto incremental_result = executeWithLogs(*executor, manifest, incremental_logs);
    ASSERT_TRUE(incremental_result.success) << incremental_result.errorMessage.toStdString() << "\n"
                                            << formatLogMessages(incremental_logs).toStdString();

    EXPECT_EQ(fileMtimeMs(registration_db), registration_mtime_before);

    for (int group_index = 0; group_index < static_cast<int>(manifest.integrationGroups.size()); ++group_index)
    {
        const auto& group = manifest.integrationGroups[static_cast<size_t>(group_index)];
        const QString stack_path = QDir(output_dir).filePath(group.outputFilename);
        ASSERT_TRUE(group.normalizationCoefficientsArtifactPath.has_value());
        const QString coeff_path = group.normalizationCoefficientsArtifactPath.value();

        const qint64 stack_mtime_after = fileMtimeMs(stack_path);
        const qint64 coeff_mtime_after = fileMtimeMs(coeff_path);
        ASSERT_GT(stack_mtime_after, 0);
        ASSERT_GT(coeff_mtime_after, 0);

        if (group_index == dirty_group_index)
        {
            EXPECT_GT(stack_mtime_after, stack_mtime_before[static_cast<size_t>(group_index)]);
            EXPECT_GT(coeff_mtime_after, coeff_mtime_before[static_cast<size_t>(group_index)]);
        }
        else
        {
            EXPECT_EQ(stack_mtime_after, stack_mtime_before[static_cast<size_t>(group_index)]);
            EXPECT_EQ(coeff_mtime_after, coeff_mtime_before[static_cast<size_t>(group_index)]);
        }
    }

    const QString incremental_text = formatLogMessages(incremental_logs);
    EXPECT_TRUE(incremental_text.contains(QStringLiteral("No dirty registration groups in incremental plan.")))
        << incremental_text.toStdString();
}

// A one-shot-colour camera's lights calibrated in memory - the default - state that their non-finite samples mean
// nothing, and the alignment demosaiced each through a helper that refused every mosaic stating any rule but every
// sample meaning something: Lights Alignment registered no light and the workflow failed for every OSC set it
// calibrated in memory. The broadband workflow's OSC branch failed that way on real frames.
TEST_F(FullWorkflowFixture, ColourFilterLightsCalibratedInMemoryAreAlignedAndStackedInColour)
{
    const QString output_dir = tempPath();
    const auto rggb = an::tests::fixtures::FixtureCfa::Rggb;
    const QString bias_a = writeConstantImage(output_dir, QStringLiteral("bias_a.fit"), 0.01f, rggb);
    const QString bias_b = writeConstantImage(output_dir, QStringLiteral("bias_b.fit"), 0.01f, rggb);
    const QString dark_a = writeConstantImage(output_dir, QStringLiteral("dark_a.fit"), 0.02f, rggb);
    const QString dark_b = writeConstantImage(output_dir, QStringLiteral("dark_b.fit"), 0.02f, rggb);
    const QString flat_a = writeConstantImage(output_dir, QStringLiteral("flat_a.fit"), 0.8f, rggb);
    const QString flat_b = writeConstantImage(output_dir, QStringLiteral("flat_b.fit"), 0.8f, rggb);
    const QString light_a = writeStarLight(output_dir, QStringLiteral("light_a.fit"), fieldStars(0, 40u, 23u),
                                           FixtureSampleType::Float32, rggb);
    const QString light_b = writeStarLight(output_dir, QStringLiteral("light_b.fit"), fieldStars(2, 40u, 23u),
                                           FixtureSampleType::Float32, rggb);

    WorkflowManifest manifest =
        buildManifest(output_dir, {FrameId(bias_a), FrameId(bias_b)}, {FrameId(dark_a), FrameId(dark_b)},
                      {FrameId(flat_a), FrameId(flat_b)}, {FrameId(light_a), FrameId(light_b)});
    manifest.calibrateLightsInMemory = true;
    manifest.registrationGroups.front().settings.autoReferenceDuringRegistration = false;
    for (auto& group : manifest.integrationGroups)
    {
        group.drizzle.reset();
    }

    auto executor = buildExecutor(true);
    QStringList logs;
    const auto result = executeWithLogs(*executor, manifest, logs);
    ASSERT_TRUE(result.success) << result.errorMessage.toStdString() << "\n" << formatLogMessages(logs).toStdString();

    ASSERT_FALSE(manifest.integrationGroups.empty());
    const QString master = QDir(output_dir).filePath(manifest.integrationGroups.front().outputFilename);
    ASSERT_TRUE(QFileInfo::exists(master)) << master.toStdString();
    const std::optional<an::tests::fixtures::FixtureFitsFile> written = readWrittenImage(master);
    ASSERT_TRUE(written.has_value());
    EXPECT_EQ(written->card("NAXIS3").value_or(""), "3") << "the master is the lights demosaiced into colour";
    EXPECT_EQ(written->cfa, an::tests::fixtures::FixtureCfa::None);
}

TEST_F(FullWorkflowFixture, ExecutesEndToEndWithoutDrizzleAlignsFrames)
{
    const QString output_dir = tempPath();

    const QString light_a = writeStarField(output_dir, QStringLiteral("light_a.fit"), 0);
    const QString light_b = writeStarField(output_dir, QStringLiteral("light_b.fit"), 1);

    WorkflowManifest manifest = buildManifest(output_dir, {}, {}, {}, {FrameId(light_a), FrameId(light_b)});
    manifest.registrationGroups.front().settings.autoReferenceDuringRegistration = false;
    // This test targets non-drizzle registration and alignment, not calibration.
    manifest.calibrateLightsInMemory = true;
    manifest.lightsCalibration.clear();
    manifest.biasStage.reset();
    manifest.darkStage.reset();
    manifest.flatStage.reset();
    if (!manifest.integrationGroups.empty())
    {
        manifest.integrationGroups.front().drizzle.reset();
    }

    auto executor = buildExecutor(true);
    QStringList logs;
    auto result = executeWithLogs(*executor, manifest, logs);
    EXPECT_TRUE(result.success) << result.errorMessage.toStdString() << "\n" << formatLogMessages(logs).toStdString();

    const QString metrics_path = QDir(output_dir).filePath(QStringLiteral("registration_metrics.db"));
    EXPECT_TRUE(QFileInfo::exists(metrics_path));

    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "test_non_drizzle_verify");
        db.setDatabaseName(metrics_path);
        ASSERT_TRUE(db.open()) << db.lastError().text().toStdString();
        const QString reg_key = manifest.registrationGroups.front().groupKey.toString();
        const int settings_version = manifest.registrationGroups.front().settings.version;

        auto requireOutputFor = [&](const FrameId& frame) {
            QSqlQuery q(db);
            q.prepare("SELECT output_path, succeeded FROM registration_metrics "
                      "WHERE frame_id = :id AND group_key = :key AND settings_version = :ver");
            q.bindValue(":id", frame.canonicalPath());
            q.bindValue(":key", reg_key);
            q.bindValue(":ver", settings_version);

            ASSERT_TRUE(q.exec()) << q.lastError().text().toStdString();
            ASSERT_TRUE(q.next()) << "Missing metrics row for frame: " << frame.canonicalPath().toStdString();

            const QString output_path = q.value(0).toString();
            const int succeeded = q.value(1).toInt();
            ASSERT_EQ(succeeded, 1) << "Registration failed for frame: " << frame.canonicalPath().toStdString();

            ASSERT_FALSE(output_path.isEmpty())
                << "Missing alignment output for frame: " << frame.canonicalPath().toStdString();
            EXPECT_TRUE(QFileInfo::exists(output_path))
                << "Aligned file missing on disk: " << output_path.toStdString();
            EXPECT_TRUE(output_path.contains(QStringLiteral("registered_lights")))
                << "Unexpected output location: " << output_path.toStdString();
        };

        requireOutputFor(FrameId(light_a));
        requireOutputFor(FrameId(light_b));

        db.close();
    }
    QSqlDatabase::removeDatabase("test_non_drizzle_verify");
}

TEST_F(FullWorkflowFixture, DiscardableAverageCompletesWithoutRegisteredMaterializationAndReportsArtifact)
{
    const QString output_dir = tempPath();
    const QString light_a = writeStrongStarField(output_dir, QStringLiteral("p215_light_a.fit"));
    const QString light_b = writeStrongStarField(output_dir, QStringLiteral("p215_light_b.fit"));

    WorkflowManifest manifest = buildManifest(output_dir, {}, {}, {}, {FrameId(light_a), FrameId(light_b)});
    manifest.calibrateLightsInMemory = true;
    manifest.lightsCalibration.clear();
    manifest.biasStage.reset();
    manifest.darkStage.reset();
    manifest.flatStage.reset();
    manifest.registrationGroups.front().settings.autoReferenceDuringRegistration = false;
    manifest.registrationGroups.front().settings.registrationMethod = QStringLiteral("phase_correlation");
    manifest.registrationGroups.front().settings.alignmentMode = QStringLiteral("standard");
    manifest.registrationGroups.front().settings.interpolation = QStringLiteral("Bilinear");
    manifest.registrationGroups.front().settings.transformation = QStringLiteral("Translation");
    auto& integration = manifest.integrationGroups.front();
    integration.drizzle.reset();
    integration.stacking.stackMethod = QStringLiteral("Average");
    integration.stacking.rejectionMethod = QStringLiteral("None");
    integration.stacking.normalizationMode = NormalizationMode::None;
    integration.stacking.registeredOutputPolicy = RegisteredOutputPolicy::Discardable;

    auto executor = buildExecutor(true);
    QStringList logs;
    const auto result = executeWithLogs(*executor, manifest, logs);
    ASSERT_TRUE(result.success) << result.errorMessage.toStdString() << "\n" << formatLogMessages(logs).toStdString();
    ASSERT_FALSE(result.cancelled);
    ASSERT_EQ(result.generatedLightMasters.size(), 1);
    EXPECT_TRUE(QFileInfo::exists(result.generatedLightMasters.front()));
    ASSERT_EQ(result.integrationGroupResults.size(), 1u);
    ASSERT_TRUE(result.integrationGroupResults.front().committedArtifact.has_value());
    EXPECT_TRUE(result.integrationGroupResults.front().committedArtifact->matchesFileSnapshot(
        result.generatedLightMasters.front()));
    EXPECT_FALSE(result.htmlReportPath.isEmpty());
    EXPECT_TRUE(QFileInfo::exists(result.htmlReportPath));

    QStringList persisted_transforms;
    {
        const QString connection_name = QStringLiteral("p215_transform_diagnostics");
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection_name);
        db.setDatabaseName(QDir(output_dir).filePath(QStringLiteral("registration_metrics.db")));
        if (db.open())
        {
            QSqlQuery query(db);
            if (query.exec(QStringLiteral("SELECT transform_json FROM registration_metrics ORDER BY frame_id")))
            {
                while (query.next())
                {
                    persisted_transforms.push_back(query.value(0).toString());
                }
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(QStringLiteral("p215_transform_diagnostics"));
    EXPECT_EQ(persisted_transforms.size(), 2);

    EXPECT_EQ(countFiles(QDir(output_dir).filePath(QStringLiteral("registered_lights"))), 0)
        << persisted_transforms.join(QLatin1Char('\n')).toStdString();
    const QString log_text = formatLogMessages(logs);
    EXPECT_TRUE(log_text.contains(QStringLiteral("Transform-on-read"), Qt::CaseInsensitive)) << log_text.toStdString();
    EXPECT_FALSE(log_text.contains(QStringLiteral("production routing is not enabled"), Qt::CaseInsensitive));
}

// The application gives every run the snapshot its execution plan was prepared from, taken before the run writes
// anything, and the stages that read the registration's inputs chose each light's - its calibrated copy, or the light
// itself - by what the snapshot found. On a project's first run with the lights calibrated on disk the snapshot holds
// no calibrated copy, so the alignment, the drizzle and the stack that warps the lights as it reads them read the
// lights uncalibrated. A run given its execution plan's snapshot writes the master a run given none writes, and the
// next run, planned with the copies on disk, reuses it.
TEST_F(FullWorkflowFixture, AFirstRunGivenItsPlansSnapshotStacksTheLightsItCalibratedOnDisk)
{
    enum class Route
    {
        Drizzle,
        TransformOnRead,
        RegisteredImages
    };
    const auto name_of = [](const Route route) {
        return route == Route::Drizzle           ? QStringLiteral("drizzle")
               : route == Route::TransformOnRead ? QStringLiteral("on_read")
                                                 : QStringLiteral("registered");
    };
    for (const Route route : {Route::Drizzle, Route::TransformOnRead, Route::RegisteredImages})
    {
        SCOPED_TRACE(name_of(route).toStdString());
        std::vector<QString> masters;
        for (const bool planned : {false, true})
        {
            const QString output_dir =
                ensureDir(QDir(tempPath())
                              .filePath(QStringLiteral("%1_%2").arg(
                                  name_of(route), planned ? QStringLiteral("planned") : QStringLiteral("unplanned"))));
            const QString bias = writeConstantImage(output_dir, QStringLiteral("bias.fit"), 0.01f);
            const QString dark = writeConstantImage(output_dir, QStringLiteral("dark.fit"), 0.02f);
            const QString flat = writeConstantImage(output_dir, QStringLiteral("flat.fit"), 0.8f);
            const QString light_a = writeStrongStarField(output_dir, QStringLiteral("light_a.fit"));
            const QString light_b = writeStrongStarField(output_dir, QStringLiteral("light_b.fit"));
            WorkflowManifest manifest = buildManifest(output_dir, {FrameId(bias)}, {FrameId(dark)}, {FrameId(flat)},
                                                      {FrameId(light_a), FrameId(light_b)});
            manifest.calibrateLightsInMemory = false;
            if (route != Route::Drizzle)
            {
                auto& integration = manifest.integrationGroups.front();
                integration.drizzle.reset();
                integration.stacking.stackMethod = QStringLiteral("Average");
                integration.stacking.rejectionMethod = QStringLiteral("None");
                integration.stacking.normalizationMode = NormalizationMode::None;
                integration.stacking.registeredOutputPolicy = route == Route::TransformOnRead
                                                                  ? RegisteredOutputPolicy::Discardable
                                                                  : RegisteredOutputPolicy::Retained;
            }

            const auto run = [this, planned](const WorkflowManifest& stated) {
                WorkflowExecutionOptions run_options = options();
                if (planned)
                {
                    run_options.preparationSnapshot = ExecutionPreparationSnapshot::build(
                        stated, {}, ImageIntegration::sessionFrameHeaderReader(*m_session));
                }
                auto executor = buildExecutor(true);
                QStringList logs;
                executor->setLogCallback([&logs](WorkflowLogLevel, const QString& message) { logs.append(message); });
                const auto result = executor->execute(stated, {}, run_options);
                EXPECT_TRUE(result.success) << result.errorMessage.toStdString() << "\n"
                                            << formatLogMessages(logs).toStdString();
                return result;
            };
            const auto first = run(manifest);
            ASSERT_TRUE(first.success);
            ASSERT_EQ(first.generatedLightMasters.size(), 1);
            const QString master = first.generatedLightMasters.front();
            masters.push_back(master);
            if (!planned)
            {
                continue;
            }

            for (auto* stage : {&manifest.biasStage, &manifest.darkStage, &manifest.flatStage})
            {
                for (auto& group : stage->value().groups)
                {
                    group.skipIfExists = true;
                }
            }
            manifest.integrationGroups.front().skipIfExists = true;
            const qint64 written = fileMtimeMs(master);
            ASSERT_TRUE(run(manifest).success);
            EXPECT_EQ(fileMtimeMs(master), written) << "the second run stacked the master again";
        }
        expectSameSamples(masters.front(), masters.back(), 1.0e-6);
    }
}

// A light group marked pre-calibrated is stacked as its lights are, though the run's master dark matches it: the dark
// calibrates the group beside it and leaves the marked one alone, in memory and on disk, where no calibrated copy of a
// marked light is written. The run is the product's, from the model's manifest to the masters.
TEST_F(FullWorkflowFixture, APreCalibratedGroupIsStackedAsItsLightsAreBesideAGroupItsDarkCalibrates)
{
    for (const LightCalibrationCacheMode mode : {LightCalibrationCacheMode::InMemory, LightCalibrationCacheMode::Disk})
    {
        const bool inMemory = mode == LightCalibrationCacheMode::InMemory;
        SCOPED_TRACE(inMemory ? "in memory" : "on disk");
        const QString output_dir =
            ensureDir(QDir(tempPath()).filePath(inMemory ? QStringLiteral("memory") : QStringLiteral("disk")));
        IntegrationDataModel model;
        model.setOutputDirectory(output_dir);
        model.setLightCalibrationCacheMode(mode);
        const auto frameOf = [](const FrameType type, const QString& path, const QString& filter) {
            const FrameMetadata metadata(type, QSize(static_cast<int>(kWidth), static_cast<int>(kHeight)), 60.0, 0.0,
                                         100, 10, filter, QStringLiteral("M42"),
                                         QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC),
                                         QStringLiteral("Default"));
            return FrameInfo(FrameId(path), path, metadata, QStringLiteral("2026-10-28"));
        };
        std::vector<FrameInfo> darks;
        for (int index = 0; index < 3; ++index)
        {
            darks.push_back(frameOf(FrameType::Dark,
                                    writeConstantImage(QDir(output_dir).filePath(QStringLiteral("darks")),
                                                       QStringLiteral("dark_%1.fit").arg(index), 0.02f),
                                    QString()));
        }
        model.addFrames(FrameType::Dark, darks);
        std::map<QString, std::vector<QString>> lightsOf;
        for (const QString& filter : {QStringLiteral("L"), QStringLiteral("R")})
        {
            std::vector<FrameInfo> lights;
            for (int index = 0; index < 3; ++index)
            {
                lightsOf[filter].push_back(writeStarLight(QDir(output_dir).filePath(QStringLiteral("lights")),
                                                          QStringLiteral("light_%1_%2.fit").arg(filter).arg(index),
                                                          fieldStars(index)));
                lights.push_back(frameOf(FrameType::Light, lightsOf[filter].back(), filter));
            }
            model.addFrames(FrameType::Light, lights);
        }
        const auto marked = std::find_if(model.lightGroups().begin(), model.lightGroups().end(), [](const auto& group) {
            return !group.frames.empty() && group.frames.front().metadata.filter() == QStringLiteral("L");
        });
        ASSERT_TRUE(marked != model.lightGroups().end());
        LightCalibrationSettings preCalibrated;
        preCalibrated.preCalibrated = true;
        model.setLightCalibrationSettings(marked->key, preCalibrated);

        WorkflowManifest manifest = ManifestBuilder(model).build();
        ASSERT_EQ(manifest.preCalibratedLightGroups.size(), 1u);
        ASSERT_EQ(manifest.lightsCalibration.size(), 3u) << "only the other group's lights are calibrated";
        WorkflowExecutionOptions run_options = options();
        run_options.preparationSnapshot =
            ExecutionPreparationSnapshot::build(manifest, {}, ImageIntegration::sessionFrameHeaderReader(*m_session));
        auto executor = buildExecutor(true);
        QStringList logs;
        executor->setLogCallback([&logs](WorkflowLogLevel, const QString& message) { logs.append(message); });
        const auto result = executor->execute(manifest, {}, run_options);
        ASSERT_TRUE(result.success) << result.errorMessage.toStdString() << "\n"
                                    << formatLogMessages(logs).toStdString();

        ASSERT_EQ(manifest.integrationGroups.size(), 2u);
        for (const auto& integration : manifest.integrationGroups)
        {
            SCOPED_TRACE(integration.filter.toStdString());
            const std::optional<an::tests::fixtures::FixtureFitsFile> master =
                readWrittenImage(QDir(output_dir).filePath(integration.outputFilename));
            ASSERT_TRUE(master.has_value());
            std::vector<double> samples;
            for (std::uint32_t y = 0u; y < master->samples.height(); ++y)
            {
                for (std::uint32_t x = 0u; x < master->samples.width(); ++x)
                {
                    samples.push_back(master->samples.value(x, y, 0u));
                }
            }
            std::nth_element(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(samples.size() / 2u),
                             samples.end());
            const double background = samples[samples.size() / 2u];
            const bool isMarked = integration.filter == QStringLiteral("L");
            EXPECT_NEAR(background, isMarked ? 0.05 : 0.03, 0.002)
                << (isMarked ? "the marked group's lights were calibrated by the dark"
                             : "the other group's lights were not calibrated by the dark");
        }
        for (const auto& [filter, lights] : lightsOf)
        {
            for (const QString& light : lights)
            {
                EXPECT_EQ(QFileInfo::exists(calibratedPathFor(output_dir, FrameId(light))),
                          !inMemory && filter == QStringLiteral("R"))
                    << light.toStdString();
            }
        }
    }
}

// A light's registration reads its calibrated copy whenever one is on disk, so a copy an earlier run wrote must not
// outlive the calibration it holds: once the group is marked pre-calibrated, or its dark is taken out of the project,
// the next run stacks the lights as they are and leaves no copy of them behind.
TEST_F(FullWorkflowFixture, ACalibratedCopyAnEarlierRunLeftIsNotReadOnceItsLightIsNoLongerCalibrated)
{
    enum class Change
    {
        MarkedPreCalibrated,
        DarkRemoved
    };
    for (const Change change : {Change::MarkedPreCalibrated, Change::DarkRemoved})
    {
        const bool marked = change == Change::MarkedPreCalibrated;
        SCOPED_TRACE(marked ? "marked pre-calibrated" : "dark removed");
        const QString output_dir =
            ensureDir(QDir(tempPath()).filePath(marked ? QStringLiteral("marked") : QStringLiteral("dark_removed")));
        const auto frameOf = [](const FrameType type, const QString& path) {
            const FrameMetadata metadata(
                type, QSize(static_cast<int>(kWidth), static_cast<int>(kHeight)), 60.0, 0.0, 100, 10,
                type == FrameType::Light ? QStringLiteral("L") : QString(), QStringLiteral("M42"),
                QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC), QStringLiteral("Default"));
            return FrameInfo(FrameId(path), path, metadata, QStringLiteral("2026-10-28"));
        };
        std::vector<FrameInfo> darks;
        std::vector<FrameInfo> lights;
        std::vector<QString> lightPaths;
        for (int index = 0; index < 3; ++index)
        {
            darks.push_back(
                frameOf(FrameType::Dark, writeConstantImage(QDir(output_dir).filePath(QStringLiteral("darks")),
                                                            QStringLiteral("dark_%1.fit").arg(index), 0.02f)));
            lightPaths.push_back(writeStarLight(QDir(output_dir).filePath(QStringLiteral("lights")),
                                                QStringLiteral("light_%1.fit").arg(index), fieldStars(index)));
            lights.push_back(frameOf(FrameType::Light, lightPaths.back()));
        }

        const auto runAndReadBackground = [&](const bool calibrated) -> std::optional<double> {
            IntegrationDataModel model;
            model.setOutputDirectory(output_dir);
            model.setLightCalibrationCacheMode(LightCalibrationCacheMode::Disk);
            if (calibrated || marked)
            {
                model.addFrames(FrameType::Dark, darks);
            }
            model.addFrames(FrameType::Light, lights);
            if (!calibrated && marked)
            {
                LightCalibrationSettings preCalibrated;
                preCalibrated.preCalibrated = true;
                model.setLightCalibrationSettings(model.lightGroups().front().key, preCalibrated);
            }
            const WorkflowManifest manifest = ManifestBuilder(model).build();
            EXPECT_EQ(manifest.integrationGroups.size(), 1u);
            if (manifest.integrationGroups.empty())
            {
                return std::nullopt;
            }
            WorkflowExecutionOptions run_options = options();
            run_options.preparationSnapshot = ExecutionPreparationSnapshot::build(
                manifest, {}, ImageIntegration::sessionFrameHeaderReader(*m_session));
            auto executor = buildExecutor(true);
            QStringList logs;
            executor->setLogCallback([&logs](WorkflowLogLevel, const QString& message) { logs.append(message); });
            const auto result = executor->execute(manifest, {}, run_options);
            EXPECT_TRUE(result.success) << result.errorMessage.toStdString() << "\n"
                                        << formatLogMessages(logs).toStdString();
            const std::optional<an::tests::fixtures::FixtureFitsFile> master =
                readWrittenImage(QDir(output_dir).filePath(manifest.integrationGroups.front().outputFilename));
            EXPECT_TRUE(master.has_value());
            if (!result.success || !master.has_value())
            {
                return std::nullopt;
            }
            std::vector<double> samples;
            for (std::uint32_t y = 0u; y < master->samples.height(); ++y)
            {
                for (std::uint32_t x = 0u; x < master->samples.width(); ++x)
                {
                    samples.push_back(master->samples.value(x, y, 0u));
                }
            }
            std::nth_element(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(samples.size() / 2u),
                             samples.end());
            return samples[samples.size() / 2u];
        };

        const std::optional<double> calibrated = runAndReadBackground(true);
        ASSERT_TRUE(calibrated.has_value());
        EXPECT_NEAR(*calibrated, 0.03, 0.002) << "the first run did not calibrate the lights by the dark";
        for (const QString& light : lightPaths)
        {
            ASSERT_TRUE(QFileInfo::exists(calibratedPathFor(output_dir, FrameId(light)))) << light.toStdString();
        }

        const std::optional<double> asTheyAre = runAndReadBackground(false);
        ASSERT_TRUE(asTheyAre.has_value());
        EXPECT_NEAR(*asTheyAre, 0.05, 0.002) << "the second run stacked the copies the first run calibrated";
        for (const QString& light : lightPaths)
        {
            EXPECT_FALSE(QFileInfo::exists(calibratedPathFor(output_dir, FrameId(light)))) << light.toStdString();
        }
    }
}

// A copy of a light the project no longer calibrates, which the run cannot remove - another program holds it open -
// stops a run that calibrates on disk, whose stages would read it. A run that calibrates in memory reads no calibrated
// copy, not even for a group marked pre-calibrated, which has no calibration entry: it leaves the copy, says so, and
// stacks the lights as they are. Each copy here is not an image at all, so a stage that read one would fail.
TEST_F(FullWorkflowFixture, ACalibratedCopyTheRunCannotRemoveStopsOnlyARunThatWouldReadIt)
{
    for (const bool inMemory : {true, false})
    {
        SCOPED_TRACE(inMemory ? "calibrated in memory" : "calibrated on disk");
        const QString output_dir = ensureDir(
            QDir(tempPath()).filePath(inMemory ? QStringLiteral("held_in_memory") : QStringLiteral("held_on_disk")));
        std::vector<FrameInfo> lights;
        std::vector<QString> copies;
        for (int index = 0; index < 3; ++index)
        {
            const QString path = writeStarLight(QDir(output_dir).filePath(QStringLiteral("lights")),
                                                QStringLiteral("light_%1.fit").arg(index), fieldStars(index));
            const FrameMetadata metadata(FrameType::Light, QSize(static_cast<int>(kWidth), static_cast<int>(kHeight)),
                                         60.0, 0.0, 100, 10, QStringLiteral("L"), QStringLiteral("M42"),
                                         QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC),
                                         QStringLiteral("Default"));
            lights.emplace_back(FrameId(path), path, metadata, QStringLiteral("2026-10-29"));
            copies.push_back(calibratedPathFor(output_dir, FrameId(path)));
            ASSERT_TRUE(QDir().mkpath(QFileInfo(copies.back()).absolutePath()));
            QFile copy(copies.back());
            ASSERT_TRUE(copy.open(QIODevice::WriteOnly));
            ASSERT_GT(copy.write("not an image an earlier run calibrated"), 0);
        }

        // Holds every copy where the run cannot remove it: open without delete sharing on Windows, in a directory
        // that cannot be written elsewhere.
        std::vector<std::unique_ptr<QFile>> held;
        const QString copiesDirectory = QFileInfo(copies.front()).absolutePath();
#ifdef Q_OS_WIN
        for (const QString& copy : copies)
        {
            held.push_back(std::make_unique<QFile>(copy));
            ASSERT_TRUE(held.back()->open(QIODevice::ReadOnly)) << copy.toStdString();
        }
#else
        const QFileDevice::Permissions writable = QFile::permissions(copiesDirectory);
        ASSERT_TRUE(QFile::setPermissions(copiesDirectory, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
#endif
        // Lets the copies go however the case ends, so its directory can be removed.
        struct Release
        {
            std::function<void()> release;
            ~Release()
            {
                release();
            }
        } const releaseOnExit{[&]() {
            held.clear();
#ifndef Q_OS_WIN
            QFile::setPermissions(copiesDirectory, writable);
#endif
        }};

        IntegrationDataModel model;
        model.setOutputDirectory(output_dir);
        model.setLightCalibrationCacheMode(inMemory ? LightCalibrationCacheMode::InMemory
                                                    : LightCalibrationCacheMode::Disk);
        model.addFrames(FrameType::Light, lights);
        LightCalibrationSettings preCalibrated;
        preCalibrated.preCalibrated = true;
        model.setLightCalibrationSettings(model.lightGroups().front().key, preCalibrated);
        const WorkflowManifest manifest = ManifestBuilder(model).build();
        ASSERT_EQ(manifest.integrationGroups.size(), 1u);
        ASSERT_EQ(manifest.calibrateLightsInMemory, inMemory);
        WorkflowExecutionOptions run_options = options();
        run_options.preparationSnapshot =
            ExecutionPreparationSnapshot::build(manifest, {}, ImageIntegration::sessionFrameHeaderReader(*m_session));
        auto executor = buildExecutor(true);
        QStringList logs;
        executor->setLogCallback([&logs](WorkflowLogLevel, const QString& message) { logs.append(message); });
        const auto result = executor->execute(manifest, {}, run_options);

        for (const QString& copy : copies)
        {
            EXPECT_TRUE(QFileInfo::exists(copy)) << "the run removed a copy held open: " << copy.toStdString();
        }

        if (!inMemory)
        {
            EXPECT_FALSE(result.success) << "a run whose stages read calibrated copies stops at one it cannot remove";
            EXPECT_TRUE(result.errorMessage.contains(QStringLiteral("could not be removed")))
                << result.errorMessage.toStdString();
            continue;
        }

        ASSERT_TRUE(result.success) << result.errorMessage.toStdString() << "\n"
                                    << formatLogMessages(logs).toStdString();
        EXPECT_TRUE(std::any_of(result.warnings.cbegin(), result.warnings.cend(), [](const QString& warning) {
            return warning.contains(QStringLiteral("could not be removed and were left"));
        })) << result.warnings.join(QStringLiteral("\n")).toStdString();
        // Only the warning that the copies were left names one: no stage read one.
        const QString copiesFolder = QFileInfo(copiesDirectory).fileName() + QLatin1Char('/');
        for (const QString& line : logs + result.warnings)
        {
            if (!line.contains(QStringLiteral("could not be removed and were left")))
            {
                EXPECT_FALSE(QDir::fromNativeSeparators(line).contains(copiesFolder)) << line.toStdString();
            }
        }
        const std::optional<an::tests::fixtures::FixtureFitsFile> master =
            readWrittenImage(QDir(output_dir).filePath(manifest.integrationGroups.front().outputFilename));
        ASSERT_TRUE(master.has_value());
        std::vector<double> samples;
        for (std::uint32_t y = 0u; y < master->samples.height(); ++y)
        {
            for (std::uint32_t x = 0u; x < master->samples.width(); ++x)
            {
                samples.push_back(master->samples.value(x, y, 0u));
            }
        }
        std::nth_element(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(samples.size() / 2u),
                         samples.end());
        EXPECT_NEAR(samples[samples.size() / 2u], 0.05, 0.002) << "the lights were not stacked as they are";
    }
}

TEST_F(FullWorkflowFixture, TransformOnReadCacheUsesRegistrationInputOrderAcrossUnicodeAndPixelTypes)
{
    const QString source_dir = QDir(tempPath()).filePath(QStringLiteral("p215_unicode_sources"));
    const QString materialized_dir = QDir(tempPath()).filePath(QStringLiteral("p215_unicode_materialized"));
    const QString transform_on_read_dir = QDir(tempPath()).filePath(QStringLiteral("p215_unicode_transform"));
    const QString supplementary_name =
        QStringLiteral("p215_") + QString::fromUcs4(U"\U00010000") + QStringLiteral(".fit");
    const QString high_bmp_name = QStringLiteral("p215_") + QChar(0xE000) + QStringLiteral(".fit");
    const QString light_b = writeStrongStarField(source_dir, high_bmp_name, FixtureSampleType::UInt16);
    const QString light_a = writeStrongStarField(source_dir, supplementary_name, FixtureSampleType::Float32, 2);
    ASSERT_LT(FrameId(light_a).canonicalPath(), FrameId(light_b).canonicalPath());
    ASSERT_GT(FrameId(light_a).canonicalPath().toStdString(), FrameId(light_b).canonicalPath().toStdString());

    const auto make_manifest = [&](const QString& output_dir, const RegisteredOutputPolicy output_policy) {
        WorkflowManifest manifest = buildManifest(output_dir, {}, {}, {}, {FrameId(light_a), FrameId(light_b)});
        manifest.calibrateLightsInMemory = true;
        manifest.lightsCalibration.clear();
        manifest.biasStage.reset();
        manifest.darkStage.reset();
        manifest.flatStage.reset();
        manifest.registrationGroups.front().referenceFrame = FrameId(light_b);
        manifest.registrationGroups.front().settings.autoReferenceDuringRegistration = false;
        manifest.registrationGroups.front().settings.registrationMethod = QStringLiteral("phase_correlation");
        manifest.registrationGroups.front().settings.alignmentMode = QStringLiteral("standard");
        manifest.registrationGroups.front().settings.interpolation = QStringLiteral("Bilinear");
        manifest.registrationGroups.front().settings.transformation = QStringLiteral("Translation");
        auto& integration = manifest.integrationGroups.front();
        integration.drizzle.reset();
        integration.stacking.stackMethod = QStringLiteral("Average");
        integration.stacking.rejectionMethod = QStringLiteral("None");
        integration.stacking.weighting = QStringLiteral("None");
        integration.stacking.normalizationMode = NormalizationMode::None;
        integration.stacking.registeredOutputPolicy = output_policy;
        return manifest;
    };

    // Each registration's transforms rewritten as the integer translations nearest to them, so warping a light as it
    // is read and warping it into a registered image sample exactly the same pixels.
    int preparation_index = 0;
    const auto prepare_canonical_transforms = [&](const WorkflowManifest& manifest) {
        WorkflowExecutor registration_executor;
        registration_executor.addStage(std::make_unique<LightsRegistrationStage>());
        QStringList registration_logs;
        const auto registration_result = executeWithLogs(registration_executor, manifest, registration_logs);
        if (!registration_result.success)
        {
            ADD_FAILURE() << registration_result.errorMessage.toStdString() << '\n'
                          << formatLogMessages(registration_logs).toStdString();
            return false;
        }

        struct TransformRow
        {
            qlonglong rowId = 0;
            QString json;
        };
        std::vector<TransformRow> rows;
        bool saw_non_identity = false;
        const QString connection_name = QStringLiteral("p215_canonical_transforms_%1").arg(++preparation_index);
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection_name);
            db.setDatabaseName(QDir(manifest.outputDirectory).filePath(QStringLiteral("registration_metrics.db")));
            if (!db.open())
            {
                ADD_FAILURE() << db.lastError().text().toStdString();
                return false;
            }

            QSqlQuery select(db);
            if (!select.exec(QStringLiteral("SELECT rowid, transform_json FROM registration_metrics")))
            {
                ADD_FAILURE() << select.lastError().text().toStdString();
                db.close();
                return false;
            }
            while (select.next())
                rows.push_back({select.value(0).toLongLong(), select.value(1).toString()});

            for (const auto& row : rows)
            {
                an::RegistrationTransform transform;
                stage_utils::SourceDistortionProvenance provenance;
                QString parse_error;
                if (!stage_utils::parseRegistrationTransformJson(row.json, transform, &provenance, parse_error))
                {
                    ADD_FAILURE() << parse_error.toStdString();
                    db.close();
                    return false;
                }

                // A translation is the same in either pixel convention, so rounding it keeps it a whole-pixel shift.
                const double tx = std::round(transform.translation().x);
                const double ty = std::round(transform.translation().y);
                saw_non_identity = saw_non_identity || tx != 0.0 || ty != 0.0;
                const an::Result<an::RegistrationTransform> integer = an::RegistrationTransform::create(
                    an::RegistrationTransformRequest{.linearKind = an::RegistrationTransformKind::Affine,
                                                     .sourceToReference = {1.0, 0.0, tx, 0.0, 1.0, ty, 0.0, 0.0, 1.0}});
                if (!integer.hasValue())
                {
                    ADD_FAILURE() << integer.status().message();
                    db.close();
                    return false;
                }

                QSqlQuery update(db);
                update.prepare(
                    QStringLiteral("UPDATE registration_metrics SET transform_json = :json WHERE rowid = :rowid"));
                update.bindValue(QStringLiteral(":json"), stage_utils::serializeTransformJson(*integer, provenance));
                update.bindValue(QStringLiteral(":rowid"), row.rowId);
                if (!update.exec())
                {
                    ADD_FAILURE() << update.lastError().text().toStdString();
                    db.close();
                    return false;
                }
            }
            db.close();
        }
        QSqlDatabase::removeDatabase(connection_name);

        if (rows.size() != 2)
        {
            ADD_FAILURE() << "Expected two persisted registration transforms, got " << rows.size();
            return false;
        }
        if (!saw_non_identity)
        {
            ADD_FAILURE() << "Expected a non-identity integer registration transform";
            return false;
        }
        return true;
    };

    const auto materialized_manifest = make_manifest(materialized_dir, RegisteredOutputPolicy::Retained);
    ASSERT_TRUE(prepare_canonical_transforms(materialized_manifest));
    auto materialized_executor = buildExecutor(true);
    QStringList materialized_logs;
    const auto materialized_result = executeWithLogs(*materialized_executor, materialized_manifest, materialized_logs);
    ASSERT_TRUE(materialized_result.success) << materialized_result.errorMessage.toStdString() << '\n'
                                             << formatLogMessages(materialized_logs).toStdString();
    ASSERT_EQ(materialized_result.generatedLightMasters.size(), 1);

    const auto transform_manifest = make_manifest(transform_on_read_dir, RegisteredOutputPolicy::Discardable);
    ASSERT_TRUE(prepare_canonical_transforms(transform_manifest));
    auto transform_executor = buildExecutor(true);
    QStringList transform_logs;
    const auto transform_result = executeWithLogs(*transform_executor, transform_manifest, transform_logs);
    ASSERT_TRUE(transform_result.success) << transform_result.errorMessage.toStdString() << "\n"
                                          << formatLogMessages(transform_logs).toStdString();
    ASSERT_EQ(transform_result.generatedLightMasters.size(), 1);

    expectSameSamples(materialized_result.generatedLightMasters.front(), transform_result.generatedLightMasters.front(),
                      1e-5);

    EXPECT_EQ(countFiles(QDir(transform_on_read_dir).filePath(QStringLiteral("registered_lights"))), 0);
    EXPECT_TRUE(formatLogMessages(transform_logs).contains(QStringLiteral("Transform-on-read"), Qt::CaseInsensitive))
        << formatLogMessages(transform_logs).toStdString();
}

TEST_F(FullWorkflowFixture, CachedGlobalAverageReusesArtifactWithoutRegisteredImagesAndPreservesResults)
{
    const QString output_dir = tempPath();
    const QString light_a = writeStrongStarField(output_dir, QStringLiteral("p216_light_a.fit"));
    const QString light_b = writeStrongStarField(output_dir, QStringLiteral("p216_light_b.fit"));

    WorkflowManifest manifest = buildManifest(output_dir, {}, {}, {}, {FrameId(light_a), FrameId(light_b)});
    manifest.calibrateLightsInMemory = true;
    manifest.lightsCalibration.clear();
    manifest.biasStage.reset();
    manifest.darkStage.reset();
    manifest.flatStage.reset();
    manifest.registrationGroups.front().referenceFrame = FrameId(light_b);
    manifest.registrationGroups.front().settings.autoReferenceDuringRegistration = false;
    manifest.registrationGroups.front().settings.registrationMethod = QStringLiteral("phase_correlation");
    manifest.registrationGroups.front().settings.alignmentMode = QStringLiteral("standard");
    manifest.registrationGroups.front().settings.interpolation = QStringLiteral("Bilinear");
    manifest.registrationGroups.front().settings.transformation = QStringLiteral("Translation");
    auto& integration = manifest.integrationGroups.front();
    integration.drizzle.reset();
    integration.normalizationReferenceFrames = {FrameId(light_a)};
    integration.stacking.stackMethod = QStringLiteral("Average");
    integration.stacking.rejectionMethod = QStringLiteral("None");
    integration.stacking.normalizationMode = NormalizationMode::Global;
    integration.stacking.globalNormalizationMethod = GlobalNormalizationMethod::AdditiveScaling;
    integration.stacking.scaleEstimator = ScaleEstimatorMethod::MAD;
    integration.stacking.registeredOutputPolicy = RegisteredOutputPolicy::Discardable;

    const QString registered_dir = QDir(output_dir).filePath(QStringLiteral("registered_lights"));
    const auto registered_file_count = [&]() { return countFiles(registered_dir); };
    const auto remove_registered_files = [&]() {
        QStringList paths;
        QDirIterator files(registered_dir, QDir::Files, QDirIterator::Subdirectories);
        while (files.hasNext())
        {
            paths.push_back(files.next());
        }
        for (const auto& path : paths)
        {
            EXPECT_TRUE(QFile::remove(path)) << path.toStdString();
        }
    };

    auto first_executor = buildExecutor(true);
    QStringList first_logs;
    const auto first_result = executeWithLogs(*first_executor, manifest, first_logs);
    ASSERT_TRUE(first_result.success) << first_result.errorMessage.toStdString() << "\n"
                                      << formatLogMessages(first_logs).toStdString();
    ASSERT_FALSE(first_result.cancelled);
    ASSERT_EQ(first_result.generatedLightMasters.size(), 1);
    ASSERT_EQ(first_result.integrationGroupResults.size(), 1u);
    ASSERT_TRUE(first_result.integrationGroupResults.front().committedArtifact.has_value());
    EXPECT_TRUE(first_result.integrationGroupResults.front().committedArtifact->matchesFileSnapshot(
        first_result.generatedLightMasters.front()));
    ASSERT_TRUE(integration.normalizationCoefficientsArtifactPath.has_value());
    EXPECT_TRUE(QFileInfo::exists(*integration.normalizationCoefficientsArtifactPath));
    EXPECT_TRUE(QFileInfo::exists(
        ImageIntegration::normalizationSidecarPath(*integration.normalizationCoefficientsArtifactPath)));
    EXPECT_FALSE(first_result.htmlReportPath.isEmpty());
    EXPECT_TRUE(QFileInfo::exists(first_result.htmlReportPath));

    // Whatever registered images the first run kept are removed: the cached run must reuse the master without them.
    remove_registered_files();
    ASSERT_EQ(registered_file_count(), 0);

    // The first master is kept aside, so every later run's master is compared with what the first run wrote.
    const QString first_output_copy = QDir(tempPath()).filePath(QStringLiteral("p216_first_master.fit"));
    ASSERT_TRUE(QFile::copy(first_result.generatedLightMasters.front(), first_output_copy));
    ASSERT_TRUE(readWrittenImage(first_output_copy).has_value());

    ExecutionPlan full_plan;
    full_plan.mode = ExecutionMode::Full;
    full_plan.integrationGroupsToProcess = {integration.groupKey};
    const auto decisions = buildExecutionPlanDecisions(manifest, full_plan, std::nullopt);
    const auto alignment =
        std::find_if(decisions.cbegin(), decisions.cend(), [](const ExecutionPlanDecision& decision) {
            return decision.stage == QStringLiteral("Lights Alignment");
        });
    const auto normalization =
        std::find_if(decisions.cbegin(), decisions.cend(), [](const ExecutionPlanDecision& decision) {
            return decision.stage == QStringLiteral("Lights Normalization");
        });
    ASSERT_NE(alignment, decisions.cend());
    ASSERT_NE(normalization, decisions.cend());
    ASSERT_TRUE(alignment->transformOnReadDecision.has_value());
    EXPECT_TRUE(alignment->transformOnReadDecision->usesTransformOnRead());
    EXPECT_EQ(alignment->action, ExecutionPlanDecision::Action::Skip);

    auto cached_executor = buildExecutor(true);
    QStringList cached_logs;
    const auto cached_result = executeWithLogs(*cached_executor, manifest, cached_logs);
    ASSERT_TRUE(cached_result.success) << cached_result.errorMessage.toStdString() << "\n"
                                       << formatLogMessages(cached_logs).toStdString();
    ASSERT_FALSE(cached_result.cancelled);
    ASSERT_EQ(cached_result.generatedLightMasters.size(), 1);
    ASSERT_EQ(cached_result.integrationGroupResults.size(), 1u);
    ASSERT_TRUE(cached_result.integrationGroupResults.front().committedArtifact.has_value());
    EXPECT_TRUE(cached_result.integrationGroupResults.front().committedArtifact->matchesFileSnapshot(
        cached_result.generatedLightMasters.front()));
    EXPECT_TRUE(cached_result.integrationGroupResults.front().registrationInputPolicy.contains(
        QStringLiteral("Transform-on-read"), Qt::CaseInsensitive));
    EXPECT_TRUE(cached_result.integrationGroupResults.front().registrationInputPolicy.contains(
        QStringLiteral("eligible"), Qt::CaseInsensitive));
    EXPECT_FALSE(cached_result.htmlReportPath.isEmpty());
    EXPECT_TRUE(QFileInfo::exists(cached_result.htmlReportPath));
    // The report states how the stack read its lights, as the group's result does.
    ImageIntegration::Reporting::ReportArchiveRead cached_report =
        ImageIntegration::Reporting::ReportArchiveReader::read(QFileInfo(cached_result.htmlReportPath).absolutePath());
    ASSERT_TRUE(cached_report.model.has_value()) << cached_report.why.toStdString();
    ASSERT_EQ(cached_report.model->masters.size(), 1u);
    EXPECT_EQ(cached_report.model->masters.front().registrationInputPolicy,
              cached_result.integrationGroupResults.front().registrationInputPolicy);
    EXPECT_EQ(registered_file_count(), 0);

    expectSameSamples(first_output_copy, cached_result.generatedLightMasters.front(), 1e-6);
    const QString cached_log_text = formatLogMessages(cached_logs);
    EXPECT_TRUE(cached_log_text.contains(QStringLiteral("transform-on-read"), Qt::CaseInsensitive))
        << cached_log_text.toStdString();

    manifest.executionMode = ExecutionMode::Incremental;
    manifest.incrementalBiasGroups.clear();
    manifest.incrementalDarkGroups.clear();
    manifest.incrementalFlatGroups.clear();
    manifest.incrementalRegistrationGroups.clear();
    manifest.incrementalIntegrationGroups = {integration.groupKey};
    // An incremental run reads the lights as the full run did: warped as the stack reads them, no registered image
    // written.
    auto incremental_executor = buildExecutor(true);
    QStringList incremental_logs;
    const auto incremental_result = executeWithLogs(*incremental_executor, manifest, incremental_logs);
    ASSERT_TRUE(incremental_result.success) << incremental_result.errorMessage.toStdString() << "\n"
                                            << formatLogMessages(incremental_logs).toStdString();
    EXPECT_EQ(registered_file_count(), 0);
    ASSERT_EQ(incremental_result.generatedLightMasters.size(), 1);
    expectSameSamples(first_output_copy, incremental_result.generatedLightMasters.front(), 1e-6);
    ASSERT_EQ(incremental_result.integrationGroupResults.size(), 1u);
    EXPECT_TRUE(incremental_result.integrationGroupResults.front().registrationInputPolicy.contains(
        QStringLiteral("Transform-on-read"), Qt::CaseInsensitive));
    EXPECT_TRUE(incremental_result.integrationGroupResults.front().registrationInputPolicy.contains(
        QStringLiteral("eligible"), Qt::CaseInsensitive));

    remove_registered_files();
    ASSERT_EQ(registered_file_count(), 0);
    manifest.executionMode = ExecutionMode::Full;
    manifest.incrementalIntegrationGroups.clear();

    const QString committed_output_path = cached_result.generatedLightMasters.front();
    const QString committed_output_digest = ArtifactTracker::contentDigestForFile(committed_output_path);
    ASSERT_EQ(committed_output_digest.size(), 64);
    auto cancelled_executor = buildExecutor(true);
    bool cancellation_requested_during_stack = false;
    cancelled_executor->setProgressCallback([&](const WorkflowProgress& progress) {
        if (!cancellation_requested_during_stack && progress.currentStage == WorkflowStage::LightsStacking &&
            progress.stageProgress > 0.0f)
        {
            cancellation_requested_during_stack = true;
            cancelled_executor->requestCancel();
        }
    });
    const auto cancelled_result = execute(*cancelled_executor, manifest);
    EXPECT_FALSE(cancelled_result.success);
    EXPECT_TRUE(cancelled_result.cancelled);
    EXPECT_TRUE(cancellation_requested_during_stack);
    EXPECT_TRUE(QFileInfo::exists(*integration.normalizationCoefficientsArtifactPath));
    EXPECT_TRUE(QFileInfo::exists(
        ImageIntegration::normalizationSidecarPath(*integration.normalizationCoefficientsArtifactPath)));
    EXPECT_EQ(registered_file_count(), 0);
    EXPECT_EQ(ArtifactTracker::contentDigestForFile(committed_output_path), committed_output_digest);
    QDirIterator temporary_outputs(
        output_dir, {QStringLiteral("*.pending.*"), QStringLiteral("*.rollback.*"), QStringLiteral("*.staged.*")},
        QDir::Files, QDirIterator::Subdirectories);
    EXPECT_FALSE(temporary_outputs.hasNext());

    manifest.executionMode = ExecutionMode::Full;
    manifest.incrementalIntegrationGroups.clear();
    manifest.reprocessExisting = true;
    auto reprocess_executor = buildExecutor(true);
    QStringList reprocess_logs;
    const auto reprocess_result = executeWithLogs(*reprocess_executor, manifest, reprocess_logs);
    ASSERT_TRUE(reprocess_result.success) << reprocess_result.errorMessage.toStdString() << "\n"
                                          << formatLogMessages(reprocess_logs).toStdString();
    EXPECT_EQ(registered_file_count(), 2);
    ASSERT_EQ(reprocess_result.generatedLightMasters.size(), 1);
    expectSameSamples(first_output_copy, reprocess_result.generatedLightMasters.front(), 1e-6);
    EXPECT_TRUE(formatLogMessages(reprocess_logs)
                    .contains(QStringLiteral("Materialized registered images"), Qt::CaseInsensitive));
}

TEST_F(FullWorkflowFixture, RetainedPolicyStillMaterializesRegisteredOutputs)
{
    const QString output_dir = tempPath();
    const QString light_a = writeStrongStarField(output_dir, QStringLiteral("p215_retained_a.fit"));
    const QString light_b = writeStrongStarField(output_dir, QStringLiteral("p215_retained_b.fit"));

    WorkflowManifest manifest = buildManifest(output_dir, {}, {}, {}, {FrameId(light_a), FrameId(light_b)});
    manifest.calibrateLightsInMemory = true;
    manifest.lightsCalibration.clear();
    manifest.biasStage.reset();
    manifest.darkStage.reset();
    manifest.flatStage.reset();
    manifest.registrationGroups.front().settings.autoReferenceDuringRegistration = false;
    manifest.registrationGroups.front().settings.alignmentMode = QStringLiteral("standard");
    manifest.registrationGroups.front().settings.interpolation = QStringLiteral("Bilinear");
    auto& integration = manifest.integrationGroups.front();
    integration.drizzle.reset();
    integration.stacking.stackMethod = QStringLiteral("Average");
    integration.stacking.rejectionMethod = QStringLiteral("None");
    integration.stacking.normalizationMode = NormalizationMode::None;
    integration.stacking.registeredOutputPolicy = RegisteredOutputPolicy::Retained;

    auto executor = buildExecutor(true);
    QStringList logs;
    const auto result = executeWithLogs(*executor, manifest, logs);
    ASSERT_TRUE(result.success) << result.errorMessage.toStdString() << "\n" << formatLogMessages(logs).toStdString();

    EXPECT_EQ(countFiles(QDir(output_dir).filePath(QStringLiteral("registered_lights"))), 2);
    EXPECT_TRUE(
        formatLogMessages(logs).contains(QStringLiteral("materialized registered images"), Qt::CaseInsensitive));
}

TEST_F(FullWorkflowFixture, ExecutesLightsOnlyWorkflow)
{
    // Test workflow with only light frames (no calibration masters)
    const QString output_dir = tempPath();

    const QString light_a = writeStarField(output_dir, QStringLiteral("light_a.fit"), 0);
    const QString light_b = writeStarField(output_dir, QStringLiteral("light_b.fit"), 1);
    const QString light_c = writeStarField(output_dir, QStringLiteral("light_c.fit"), 2);

    WorkflowManifest manifest;
    manifest.outputDirectory = output_dir;

    // No bias, dark, or flat stages - just lights
    WorkflowManifest::RegistrationGroup registration = registrationOf(
        QStringLiteral("reg|%1|TestObject").arg(kResolution), {FrameId(light_a), FrameId(light_b), FrameId(light_c)});
    registration.settings.autoReferenceDuringRegistration = false;
    manifest.registrationGroups.push_back(registration);

    WorkflowManifest::IntegrationGroup integration;
    integration.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|TestObject|L")}};
    integration.outputFilename = QStringLiteral("stacked_lights_only.fit");
    integration.resolution = kResolution;
    integration.object = QStringLiteral("TestObject");
    integration.filter = QStringLiteral("L");
    integration.lightFiles = registration.lightFiles;
    integration.registrationGroupKey = registration.groupKey;
    manifest.integrationGroups.push_back(std::move(integration));

    manifest.stageSequence = {QStringLiteral("LightsRegistration"), QStringLiteral("LightsAlignment"),
                              QStringLiteral("LightsNormalization"), QStringLiteral("LightsStacking")};

    auto executor = buildExecutor(true);
    QStringList logs;
    auto result = executeWithLogs(*executor, manifest, logs);

    EXPECT_TRUE(result.success) << result.errorMessage.toStdString() << "\n" << formatLogMessages(logs).toStdString();

    const QString output_path = QDir(output_dir).filePath(QStringLiteral("stacked_lights_only.fit"));
    EXPECT_TRUE(QFileInfo::exists(output_path));
}

TEST_F(FullWorkflowFixture, HandlesCancellationGracefully)
{
    const QString output_dir = tempPath();

    const QString bias = writeConstantImage(output_dir, QStringLiteral("bias.fit"), 0.01f);
    const QString dark = writeConstantImage(output_dir, QStringLiteral("dark.fit"), 0.02f);
    const QString flat = writeConstantImage(output_dir, QStringLiteral("flat.fit"), 0.8f);
    const QString light = writeStarField(output_dir, QStringLiteral("light.fit"), 0);

    WorkflowManifest manifest =
        buildManifest(output_dir, {FrameId(bias)}, {FrameId(dark)}, {FrameId(flat)}, {FrameId(light)});

    auto executor = buildExecutor(false);

    // Request cancellation immediately
    executor->requestCancel();

    auto result = execute(*executor, manifest);

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.cancelled);
}

TEST_F(FullWorkflowFixture, RejectsInvalidManifest)
{
    WorkflowManifest manifest;
    // Empty output directory - should fail validation
    manifest.outputDirectory.clear();

    auto executor = buildExecutor(false);
    auto result = execute(*executor, manifest);

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.cancelled);
    EXPECT_FALSE(result.errorMessage.isEmpty());
}

TEST_F(FullWorkflowFixture, ExecutesMultipleFilterGroups)
{
    const QString output_dir = tempPath();

    // Create lights for two different filters
    const QString light_l1 = writeStarField(output_dir, QStringLiteral("light_L_1.fit"), 0);
    const QString light_l2 = writeStarField(output_dir, QStringLiteral("light_L_2.fit"), 1);
    const QString light_ha1 = writeStarField(output_dir, QStringLiteral("light_Ha_1.fit"), 0);
    const QString light_ha2 = writeStarField(output_dir, QStringLiteral("light_Ha_2.fit"), 1);

    WorkflowManifest manifest;
    manifest.outputDirectory = output_dir;

    // Registration group for L filter
    const WorkflowManifest::RegistrationGroup reg_l =
        registrationOf(QStringLiteral("reg|%1|TestObject|L").arg(kResolution), {FrameId(light_l1), FrameId(light_l2)});
    manifest.registrationGroups.push_back(reg_l);

    // Registration group for Ha filter
    const WorkflowManifest::RegistrationGroup reg_ha = registrationOf(
        QStringLiteral("reg|%1|TestObject|Ha").arg(kResolution), {FrameId(light_ha1), FrameId(light_ha2)});
    manifest.registrationGroups.push_back(reg_ha);

    // Integration group for L
    WorkflowManifest::IntegrationGroup int_l;
    int_l.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|TestObject|L")}};
    int_l.outputFilename = QStringLiteral("stacked_TestObject_L.fit");
    int_l.resolution = kResolution;
    int_l.object = QStringLiteral("TestObject");
    int_l.filter = QStringLiteral("L");
    int_l.lightFiles = reg_l.lightFiles;
    int_l.registrationGroupKey = reg_l.groupKey;
    manifest.integrationGroups.push_back(int_l);

    // Integration group for Ha
    WorkflowManifest::IntegrationGroup int_ha;
    int_ha.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|TestObject|Ha")}};
    int_ha.outputFilename = QStringLiteral("stacked_TestObject_Ha.fit");
    int_ha.resolution = kResolution;
    int_ha.object = QStringLiteral("TestObject");
    int_ha.filter = QStringLiteral("Ha");
    int_ha.lightFiles = reg_ha.lightFiles;
    int_ha.registrationGroupKey = reg_ha.groupKey;
    manifest.integrationGroups.push_back(int_ha);

    manifest.stageSequence = {QStringLiteral("LightsRegistration"), QStringLiteral("LightsAlignment"),
                              QStringLiteral("LightsNormalization"), QStringLiteral("LightsStacking")};

    auto executor = buildExecutor(true);
    QStringList logs;
    auto result = executeWithLogs(*executor, manifest, logs);

    EXPECT_TRUE(result.success) << result.errorMessage.toStdString() << "\n" << formatLogMessages(logs).toStdString();

    // Verify both outputs exist
    EXPECT_TRUE(QFileInfo::exists(QDir(output_dir).filePath(QStringLiteral("stacked_TestObject_L.fit"))));
    EXPECT_TRUE(QFileInfo::exists(QDir(output_dir).filePath(QStringLiteral("stacked_TestObject_Ha.fit"))));
}

TEST_F(FullWorkflowFixture, ReportsProgressThroughoutExecution)
{
    const QString output_dir = tempPath();

    const QString light_a = writeStarField(output_dir, QStringLiteral("light_a.fit"), 0);
    const QString light_b = writeStarField(output_dir, QStringLiteral("light_b.fit"), 1);

    WorkflowManifest manifest;
    manifest.outputDirectory = output_dir;

    const WorkflowManifest::RegistrationGroup registration =
        registrationOf(QStringLiteral("reg|%1|TestObject").arg(kResolution), {FrameId(light_a), FrameId(light_b)});
    manifest.registrationGroups.push_back(registration);

    WorkflowManifest::IntegrationGroup integration;
    integration.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|TestObject|L")}};
    integration.outputFilename = QStringLiteral("stacked.fit");
    integration.resolution = kResolution;
    integration.lightFiles = registration.lightFiles;
    integration.registrationGroupKey = registration.groupKey;
    manifest.integrationGroups.push_back(integration);

    manifest.stageSequence = {QStringLiteral("LightsRegistration"), QStringLiteral("LightsAlignment"),
                              QStringLiteral("LightsNormalization"), QStringLiteral("LightsStacking")};

    auto executor = buildExecutor(true);

    std::vector<WorkflowProgress> progressUpdates;
    executor->setProgressCallback([&](const WorkflowProgress& progress) { progressUpdates.push_back(progress); });

    QStringList logs;
    auto result = executeWithLogs(*executor, manifest, logs);

    EXPECT_TRUE(result.success) << result.errorMessage.toStdString();

    // Verify we received progress updates
    EXPECT_GT(progressUpdates.size(), 0u);

    // Verify progress values are sensible (between 0 and 1)
    for (const auto& progress : progressUpdates)
    {
        EXPECT_GE(progress.overallProgress, 0.0f);
        EXPECT_LE(progress.overallProgress, 1.0f);
        EXPECT_GE(progress.stageProgress, 0.0f);
        EXPECT_LE(progress.stageProgress, 1.0f);
    }
}

TEST_F(FullWorkflowFixture, StackingRequiresMinimumTwoFrames)
{
    const QString output_dir = tempPath();

    // Single light frame - should fail with clear error message
    const QString light = writeStarField(output_dir, QStringLiteral("light.fit"), 0);

    WorkflowManifest manifest;
    manifest.outputDirectory = output_dir;

    const WorkflowManifest::RegistrationGroup registration =
        registrationOf(QStringLiteral("reg|%1|TestObject").arg(kResolution), {FrameId(light)});
    manifest.registrationGroups.push_back(registration);

    WorkflowManifest::IntegrationGroup integration;
    integration.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|TestObject|L")}};
    integration.outputFilename = QStringLiteral("stacked_single.fit");
    integration.resolution = kResolution;
    integration.lightFiles = registration.lightFiles;
    integration.registrationGroupKey = registration.groupKey;
    manifest.integrationGroups.push_back(integration);

    manifest.stageSequence = {QStringLiteral("LightsRegistration"), QStringLiteral("LightsNormalization"),
                              QStringLiteral("LightsStacking")};

    auto executor = buildExecutor(true);
    QStringList logs;
    auto result = executeWithLogs(*executor, manifest, logs);

    // Stacking requires at least 2 frames - should fail with meaningful error
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.errorMessage.contains(QStringLiteral("Two")) ||
                result.errorMessage.contains(QStringLiteral("minimum")) ||
                result.errorMessage.contains(QStringLiteral("images")))
        << "Expected error about minimum frame count, got: " << result.errorMessage.toStdString();
}

TEST_F(FullWorkflowFixture, HandlesTruncatedFitsFile)
{
    const QString output_dir = tempPath();

    // Create one valid file and one truncated file
    const QString valid_light = writeStarField(output_dir, QStringLiteral("valid_light.fit"), 0);

    // Create a truncated FITS file (incomplete header)
    const QString truncated_path = QDir(output_dir).filePath(QStringLiteral("truncated.fit"));
    {
        QFile f(truncated_path);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        // Write incomplete FITS header (FITS requires 2880-byte blocks)
        f.write("SIMPLE  =                    T / Conforms to FITS standard");
        f.close();
    }

    WorkflowManifest manifest;
    manifest.outputDirectory = output_dir;

    const WorkflowManifest::RegistrationGroup registration = registrationOf(
        QStringLiteral("reg|%1|TestObject").arg(kResolution), {FrameId(valid_light), FrameId(truncated_path)});
    manifest.registrationGroups.push_back(registration);

    WorkflowManifest::IntegrationGroup integration;
    integration.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|TestObject|L")}};
    integration.outputFilename = QStringLiteral("stacked_with_truncated.fit");
    integration.resolution = kResolution;
    integration.lightFiles = registration.lightFiles;
    integration.registrationGroupKey = registration.groupKey;
    manifest.integrationGroups.push_back(integration);

    auto executor = buildExecutor(true);
    QStringList logs;
    auto result = executeWithLogs(*executor, manifest, logs);

    // Should fail with error about the corrupted file
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.errorMessage.contains(QStringLiteral("truncated")) ||
                result.errorMessage.contains(QStringLiteral("read")) ||
                result.errorMessage.contains(QStringLiteral("Failed")) ||
                result.errorMessage.contains(QStringLiteral("invalid")))
        << "Expected error about truncated file, got: " << result.errorMessage.toStdString();
}

TEST_F(FullWorkflowFixture, HandlesOutputDirectoryBecomingUnavailable)
{
    const QString output_dir = tempPath();

    const QString light_a = writeStarField(output_dir, QStringLiteral("light_a.fit"), 0);
    const QString light_b = writeStarField(output_dir, QStringLiteral("light_b.fit"), 1);

    // Create a nested output directory that we'll delete mid-workflow
    const QString nested_output = QDir(output_dir).filePath(QStringLiteral("nested_output"));
    QDir().mkpath(nested_output);

    WorkflowManifest manifest;
    manifest.outputDirectory = nested_output;

    const WorkflowManifest::RegistrationGroup registration =
        registrationOf(QStringLiteral("reg|%1|TestObject").arg(kResolution), {FrameId(light_a), FrameId(light_b)});
    manifest.registrationGroups.push_back(registration);

    WorkflowManifest::IntegrationGroup integration;
    integration.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|TestObject|L")}};
    integration.outputFilename = QStringLiteral("stacked.fit");
    integration.resolution = kResolution;
    integration.lightFiles = registration.lightFiles;
    integration.registrationGroupKey = registration.groupKey;
    manifest.integrationGroups.push_back(integration);

    // Delete the output directory before execution
    QDir(nested_output).removeRecursively();

    auto executor = buildExecutor(true);
    QStringList logs;
    auto result = executeWithLogs(*executor, manifest, logs);

    // Workflow should handle missing output directory gracefully
    // Either by recreating it or failing with a clear error
    if (!result.success)
    {
        EXPECT_TRUE(result.errorMessage.contains(QStringLiteral("directory")) ||
                    result.errorMessage.contains(QStringLiteral("output")) ||
                    result.errorMessage.contains(QStringLiteral("path")))
            << "Expected error about output directory, got: " << result.errorMessage.toStdString();
    }
    // If it succeeds, that's also acceptable (executor recreated the directory)
}

TEST_F(FullWorkflowFixture, HandlesEmptyLightsList)
{
    const QString output_dir = tempPath();

    WorkflowManifest manifest;
    manifest.outputDirectory = output_dir;

    // Registration group with no light files
    const WorkflowManifest::RegistrationGroup registration =
        registrationOf(QStringLiteral("reg|%1|TestObject").arg(kResolution), {});
    manifest.registrationGroups.push_back(registration);

    auto executor = buildExecutor(true);
    QStringList logs;
    auto result = executeWithLogs(*executor, manifest, logs);

    // Empty lights list is valid (allows calibration-only workflows)
    // The workflow succeeds with no frames to process
    EXPECT_TRUE(result.success) << result.errorMessage.toStdString();
}

TEST_F(FullWorkflowFixture, HandlesNonExistentInputFile)
{
    const QString output_dir = tempPath();

    // Reference a file that doesn't exist
    const QString nonexistent = QDir(output_dir).filePath(QStringLiteral("does_not_exist.fit"));
    const QString valid_light = writeStarField(output_dir, QStringLiteral("valid.fit"), 0);

    WorkflowManifest manifest;
    manifest.outputDirectory = output_dir;

    const WorkflowManifest::RegistrationGroup registration = registrationOf(
        QStringLiteral("reg|%1|TestObject").arg(kResolution), {FrameId(valid_light), FrameId(nonexistent)});
    manifest.registrationGroups.push_back(registration);

    WorkflowManifest::IntegrationGroup integration;
    integration.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|TestObject|L")}};
    integration.outputFilename = QStringLiteral("stacked.fit");
    integration.resolution = kResolution;
    integration.lightFiles = registration.lightFiles;
    integration.registrationGroupKey = registration.groupKey;
    manifest.integrationGroups.push_back(integration);

    auto executor = buildExecutor(true);
    QStringList logs;
    auto result = executeWithLogs(*executor, manifest, logs);

    // Should fail with clear error about missing file
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.errorMessage.contains(QStringLiteral("does_not_exist")) ||
                result.errorMessage.contains(QStringLiteral("not found")) ||
                result.errorMessage.contains(QStringLiteral("Failed to read")))
        << "Expected error about missing file, got: " << result.errorMessage.toStdString();
}
// ----------------------------------------------------------------------------
// In-Memory Calibration Mode Tests
// ----------------------------------------------------------------------------

TEST_F(FullWorkflowFixture, InMemoryModeDoesNotCreateCalibratedLightsDirectory)
{
    const QString output_dir = tempPath();

    // Create calibration masters
    const QString bias_a = writeConstantImage(output_dir, QStringLiteral("bias_a.fit"), 0.01f);
    const QString bias_b = writeConstantImage(output_dir, QStringLiteral("bias_b.fit"), 0.01f);
    const QString dark_a = writeConstantImage(output_dir, QStringLiteral("dark_a.fit"), 0.02f);
    const QString dark_b = writeConstantImage(output_dir, QStringLiteral("dark_b.fit"), 0.02f);
    const QString flat_a = writeConstantImage(output_dir, QStringLiteral("flat_a.fit"), 0.8f);
    const QString flat_b = writeConstantImage(output_dir, QStringLiteral("flat_b.fit"), 0.8f);

    const QString light_a = writeStarField(output_dir, QStringLiteral("light_a.fit"), 0);
    const QString light_b = writeStarField(output_dir, QStringLiteral("light_b.fit"), 1);

    WorkflowManifest manifest =
        buildManifest(output_dir, {FrameId(bias_a), FrameId(bias_b)}, {FrameId(dark_a), FrameId(dark_b)},
                      {FrameId(flat_a), FrameId(flat_b)}, {FrameId(light_a), FrameId(light_b)});
    // This fixture validates in-memory calibration storage, not drizzle. Keep it on
    // the ordinary materialized route so an unrelated registration failure cannot
    // rely on a drizzle-to-unaligned-stack fallback.
    manifest.integrationGroups.front().drizzle.reset();

    // Enable InMemory calibration mode
    manifest.calibrateLightsInMemory = true;

    // Remove LightsCalibration from stage sequence (as ManifestBuilder would)
    manifest.stageSequence.removeAll(QStringLiteral("LightsCalibration"));

    // Run calibration masters first
    {
        auto executor = buildExecutor(false);
        QStringList logs;
        auto result = executeWithLogs(*executor, manifest, logs);
        EXPECT_TRUE(result.success) << result.errorMessage.toStdString() << "\n"
                                    << formatLogMessages(logs).toStdString();
    }

    // Verify masters created
    EXPECT_TRUE(QFileInfo::exists(QDir(output_dir).filePath(QStringLiteral("master_bias.fit"))));
    EXPECT_TRUE(QFileInfo::exists(QDir(output_dir).filePath(QStringLiteral("master_dark.fit"))));
    EXPECT_TRUE(QFileInfo::exists(QDir(output_dir).filePath(QStringLiteral("master_flat.fit"))));

    // Skip calibration stages for registration run
    if (manifest.biasStage.has_value())
    {
        for (auto& group : manifest.biasStage->groups)
        {
            group.skipIfExists = true;
        }
    }
    if (manifest.darkStage.has_value())
    {
        for (auto& group : manifest.darkStage->groups)
        {
            group.skipIfExists = true;
        }
    }
    if (manifest.flatStage.has_value())
    {
        for (auto& group : manifest.flatStage->groups)
        {
            group.skipIfExists = true;
        }
    }

    // Run registration with InMemory mode
    {
        auto executor = buildExecutor(true);
        QStringList logs;
        auto result = executeWithLogs(*executor, manifest, logs);
        EXPECT_TRUE(result.success) << result.errorMessage.toStdString() << "\n"
                                    << formatLogMessages(logs).toStdString();
    }

    // Key assertion: calibrated_lights directory should NOT exist
    const QString calibrated_dir = QDir(output_dir).filePath(QStringLiteral("calibrated_lights"));
    EXPECT_FALSE(QFileInfo::exists(calibrated_dir))
        << "calibrated_lights directory should NOT be created in InMemory mode";

    // Registration outputs should still exist
    const QString metrics_path = QDir(output_dir).filePath(QStringLiteral("registration_metrics.db"));
    EXPECT_TRUE(QFileInfo::exists(metrics_path));

    // Final stacked output should exist
    const QString output_path = QDir(output_dir).filePath(QStringLiteral("stacked_TestObject_L.fit"));
    EXPECT_TRUE(QFileInfo::exists(output_path));
}

TEST_F(FullWorkflowFixture, InMemoryModeProducesValidStackedOutput)
{
    const QString output_dir = tempPath();

    // Create simple calibration + light frames
    const QString bias_a = writeConstantImage(output_dir, QStringLiteral("bias_a.fit"), 0.01f);
    const QString bias_b = writeConstantImage(output_dir, QStringLiteral("bias_b.fit"), 0.01f);
    const QString dark_a = writeConstantImage(output_dir, QStringLiteral("dark_a.fit"), 0.02f);
    const QString dark_b = writeConstantImage(output_dir, QStringLiteral("dark_b.fit"), 0.02f);
    const QString flat_a = writeConstantImage(output_dir, QStringLiteral("flat_a.fit"), 0.9f);
    const QString flat_b = writeConstantImage(output_dir, QStringLiteral("flat_b.fit"), 0.9f);

    const QString light_a = writeStarField(output_dir, QStringLiteral("light_a.fit"), 0);
    const QString light_b = writeStarField(output_dir, QStringLiteral("light_b.fit"), 1);

    // Build minimal manifest without drizzle
    WorkflowManifest manifest;
    manifest.outputDirectory = output_dir;
    manifest.calibrateLightsInMemory = true;

    // Bias stage
    WorkflowManifest::CalibrationStage bias_stage;
    bias_stage.frameType = QStringLiteral("BIAS");
    WorkflowManifest::CalibrationStage::Group bias_group;
    bias_group.groupKey = GroupKey{QStringLiteral("bias|%1").arg(kResolution)};
    bias_group.outputFilename = QStringLiteral("master_bias.fit");
    bias_group.inputFiles = {FrameId(bias_a), FrameId(bias_b)};
    bias_stage.groups.push_back(std::move(bias_group));
    manifest.biasStage = bias_stage;

    // Dark stage
    WorkflowManifest::CalibrationStage dark_stage;
    dark_stage.frameType = QStringLiteral("DARK");
    WorkflowManifest::CalibrationStage::Group dark_group;
    dark_group.groupKey = GroupKey{QStringLiteral("dark|%1").arg(kResolution)};
    dark_group.outputFilename = QStringLiteral("master_dark.fit");
    dark_group.inputFiles = {FrameId(dark_a), FrameId(dark_b)};
    dark_stage.groups.push_back(std::move(dark_group));
    manifest.darkStage = dark_stage;

    // Flat stage
    WorkflowManifest::CalibrationStage flat_stage;
    flat_stage.frameType = QStringLiteral("FLAT");
    WorkflowManifest::CalibrationStage::Group flat_group;
    flat_group.groupKey = GroupKey{QStringLiteral("flat|%1|L").arg(kResolution)};
    flat_group.outputFilename = QStringLiteral("master_flat.fit");
    flat_group.inputFiles = {FrameId(flat_a), FrameId(flat_b)};
    flat_stage.groups.push_back(std::move(flat_group));
    manifest.flatStage = flat_stage;

    // Lights calibration params (used by InMemory mode)
    const QString bias_path = QDir(output_dir).filePath(QStringLiteral("master_bias.fit"));
    const QString dark_path = QDir(output_dir).filePath(QStringLiteral("master_dark.fit"));
    const QString flat_path = QDir(output_dir).filePath(QStringLiteral("master_flat.fit"));

    for (const auto& light : {FrameId(light_a), FrameId(light_b)})
    {
        WorkflowManifest::LightsCalibrationParams params;
        params.lightPath = light;
        params.biasPath = FrameId(bias_path);
        params.darkPath = FrameId(dark_path);
        params.flatPath = FrameId(flat_path);
        manifest.lightsCalibration.push_back(std::move(params));
    }

    // Registration group
    const WorkflowManifest::RegistrationGroup registration =
        registrationOf(QStringLiteral("reg|%1|TestObject").arg(kResolution), {FrameId(light_a), FrameId(light_b)});
    manifest.registrationGroups.push_back(registration);

    // Integration group (no drizzle)
    WorkflowManifest::IntegrationGroup integration;
    integration.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|TestObject|L")}};
    integration.outputFilename = QStringLiteral("stacked_inmemory.fit");
    integration.resolution = kResolution;
    integration.lightFiles = {FrameId(light_a), FrameId(light_b)};
    integration.registrationGroupKey = registration.groupKey;
    manifest.integrationGroups.push_back(integration);

    // Execute full workflow
    auto executor = buildExecutor(true);
    QStringList logs;
    auto result = executeWithLogs(*executor, manifest, logs);
    EXPECT_TRUE(result.success) << result.errorMessage.toStdString() << "\n" << formatLogMessages(logs).toStdString();

    // Verify output exists and is valid
    const QString output_path = QDir(output_dir).filePath(QStringLiteral("stacked_inmemory.fit"));
    EXPECT_TRUE(QFileInfo::exists(output_path));

    // Read the stacked image back, independently of the engine that wrote it.
    const std::optional<an::tests::fixtures::FixtureFitsFile> stacked = readWrittenImage(output_path);
    ASSERT_TRUE(stacked.has_value());
    EXPECT_EQ(stacked->samples.width(), kWidth);
    EXPECT_EQ(stacked->samples.height(), kHeight);
}

// After a full run, changing only a group's stacking method stacks it again and leaves its registered lights, its
// normalization coefficients and its drizzle descriptors as the run left them, whether the group keeps its registered
// images, has its lights warped as the stack reads them or drizzles them; and a run with nothing changed plans nothing.
// A group whose registered images were discardable had them written by the second run, which measured its
// coefficients again on them, and every later run planned the group again because no plan recognised a master its
// stack had warped as it read. Each run is the product's: the model's manifest, planned through its snapshot and run
// with it.
TEST_F(FullWorkflowFixture, AChangedStackingMethodStacksAgainAndReusesTheAlignedAndNormalizedLights)
{
    enum class Route
    {
        Kept,
        WarpedAsRead,
        Drizzled
    };
    for (const Route route : {Route::Kept, Route::WarpedAsRead, Route::Drizzled})
    {
        const bool kept = route == Route::Kept;
        const QString name = kept                           ? QStringLiteral("kept")
                             : route == Route::WarpedAsRead ? QStringLiteral("warped_as_read")
                                                            : QStringLiteral("drizzled");
        SCOPED_TRACE(name.toStdString());
        const QString root = ensureDir(QDir(tempPath()).filePath(name));
        const QString frames_dir = ensureDir(QDir(root).filePath(QStringLiteral("frames")));
        const QString output_dir = ensureDir(QDir(root).filePath(QStringLiteral("output")));

        std::vector<FrameInfo> lights;
        for (int index = 0; index < 4; ++index)
        {
            const QString path =
                writeStarLight(frames_dir, QStringLiteral("light_%1.fit").arg(index), fieldStars(index % 2, 40u, 23u));
            const FrameMetadata metadata(FrameType::Light, QSize(static_cast<int>(kWidth), static_cast<int>(kHeight)),
                                         60.0, -10.0, 100, 10, QStringLiteral("L"), QStringLiteral("M42"),
                                         QDateTime::fromSecsSinceEpoch(1700000000 + 60 * index, QTimeZone::UTC),
                                         QStringLiteral("Default"));
            lights.push_back(FrameInfo(FrameId(path), path, metadata, QStringLiteral("2025-01-01"), {}));
        }
        IntegrationDataModel model;
        model.beginBatchUpdate();
        model.addFrames(FrameType::Light, lights);
        model.setOutputDirectory(output_dir);
        model.endBatchUpdate();
        ASSERT_EQ(model.integrationGroups().size(), 1u);
        const GroupKey key = model.integrationGroups().front();
        StackingSettings settings;
        settings.registeredOutputPolicy =
            route == Route::WarpedAsRead ? RegisteredOutputPolicy::Discardable : RegisteredOutputPolicy::Retained;
        settings.drizzle.enabled = route == Route::Drizzled;
        model.setStackingSettings(FrameType::Light, key, settings);

        const auto prepare = [&]() {
            return ImageIntegration::preparePlannedWorkflowExecution(
                [&model]() { return ImageIntegration::buildFullWorkflowManifest(model, {}); }, std::nullopt,
                []() { return false; }, {}, ImageIntegration::sessionFrameHeaderReader(*m_session));
        };
        const auto run = [&](const ImageIntegration::PreparedWorkflowExecution& prepared) {
            WorkflowExecutionOptions run_options = options();
            run_options.preparationSnapshot = prepared.preparationSnapshot;
            auto executor = buildExecutor(true);
            QStringList logs;
            executor->setLogCallback([&logs](WorkflowLogLevel, const QString& message) { logs.append(message); });
            const auto result = executor->execute(prepared.manifest, {}, run_options);
            EXPECT_TRUE(result.success) << result.errorMessage.toStdString() << "\n"
                                        << formatLogMessages(logs).toStdString();
            return logs.join(QLatin1Char('\n'));
        };
        // The action a plan states for `stage`, or nothing when it plans no such stage.
        const auto actionOf = [](const std::vector<ExecutionPlanDecision>& decisions,
                                 const QString& stage) -> std::optional<ExecutionPlanDecision::Action> {
            const auto it =
                std::find_if(decisions.cbegin(), decisions.cend(),
                             [&](const ExecutionPlanDecision& decision) { return decision.stage == stage; });
            return it != decisions.cend() ? std::optional(it->action) : std::nullopt;
        };

        const auto first = prepare();
        ASSERT_TRUE(first.success) << first.errorMessage.toStdString();
        ASSERT_EQ(first.plan.mode, ExecutionMode::Full);
        (void)run(first);
        ASSERT_FALSE(HasFailure());

        const QString registered_dir = QDir(output_dir).filePath(QStringLiteral("registered_lights"));
        QMap<QString, qint64> registered;
        QDirIterator registered_files(registered_dir, QDir::Files, QDirIterator::Subdirectories);
        while (registered_files.hasNext())
        {
            const QString file = registered_files.next();
            registered.insert(file, fileMtimeMs(file));
        }
        EXPECT_EQ(registered.size(), kept ? 4 : 0);
        const auto& group = first.manifest.integrationGroups.front();
        ASSERT_TRUE(group.normalizationCoefficientsArtifactPath.has_value());
        const QString coefficients = *group.normalizationCoefficientsArtifactPath;
        const QString master = QDir(output_dir).filePath(group.outputFilename);
        const qint64 coefficients_written = fileMtimeMs(coefficients);
        const qint64 master_written = fileMtimeMs(master);
        ASSERT_GT(coefficients_written, 0);
        ASSERT_GT(master_written, 0);
        const QString drizzle_sidecar = ImageIntegration::drizzleGroupSidecarPath(
            ImageIntegration::drizzleDescriptorDirectory(first.manifest, group));
        const qint64 drizzle_written = fileMtimeMs(drizzle_sidecar);
        EXPECT_EQ(drizzle_written > 0, route == Route::Drizzled);

        const auto unchanged = prepare();
        ASSERT_TRUE(unchanged.success) << unchanged.errorMessage.toStdString();
        EXPECT_EQ(unchanged.plan.mode, ExecutionMode::Incremental);
        EXPECT_TRUE(unchanged.plan.isEmpty()) << unchanged.plan.toSummaryText().toStdString();

        // The next master is written in a later millisecond than the first.
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        settings.stackMethod = QStringLiteral("Median");
        model.setStackingSettings(FrameType::Light, key, settings);
        const auto restack = prepare();
        ASSERT_TRUE(restack.success) << restack.errorMessage.toStdString();
        ASSERT_EQ(restack.plan.mode, ExecutionMode::Incremental);
        EXPECT_TRUE(restack.plan.registrationGroupsToProcess.empty());
        EXPECT_EQ(restack.plan.integrationGroupsToProcess.size(), 1u);
        // A group that only drizzles plans no alignment, and one whose stack warps its lights skips it.
        const std::optional<ExecutionPlanDecision::Action> alignment =
            route == Route::Drizzled
                ? std::nullopt
                : std::optional(kept ? ExecutionPlanDecision::Action::Reuse : ExecutionPlanDecision::Action::Skip);
        EXPECT_EQ(actionOf(restack.decisions, QStringLiteral("Lights Alignment")), alignment);
        EXPECT_EQ(actionOf(restack.decisions, QStringLiteral("Lights Normalization")),
                  ExecutionPlanDecision::Action::Reuse);
        EXPECT_EQ(actionOf(restack.decisions, QStringLiteral("Lights Drizzle")),
                  route == Route::Drizzled ? std::optional(ExecutionPlanDecision::Action::Reuse) : std::nullopt);
        EXPECT_EQ(actionOf(restack.decisions, QStringLiteral("Lights Stacking")),
                  ExecutionPlanDecision::Action::Recompute);

        const QString logs = run(restack);
        EXPECT_FALSE(logs.contains(QStringLiteral("missing or stale output"))) << logs.toStdString();
        EXPECT_TRUE(logs.contains(QStringLiteral("Reusing normalization coefficients"))) << logs.toStdString();
        QMap<QString, qint64> after;
        QDirIterator after_files(registered_dir, QDir::Files, QDirIterator::Subdirectories);
        while (after_files.hasNext())
        {
            const QString file = after_files.next();
            after.insert(file, fileMtimeMs(file));
        }
        EXPECT_EQ(after, registered);
        EXPECT_EQ(fileMtimeMs(coefficients), coefficients_written);
        EXPECT_EQ(fileMtimeMs(drizzle_sidecar), drizzle_written);
        EXPECT_GT(fileMtimeMs(master), master_written);
    }
}
