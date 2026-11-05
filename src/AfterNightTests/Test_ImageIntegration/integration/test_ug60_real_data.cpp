// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The integration workflow over the maintainer's real acquisitions: a one-shot-colour session and a monochrome
/// luminance, red, green and blue session, and a monochrome narrowband session, each calibrated, registered and
/// stacked by the product's own stages in one engine session. The data are private and never part of the checkout, so
/// each case skips unless its environment names them; a run writes a sanitized summary and the captured log beside its
/// outputs.

#include "Test_ImageIntegration/ImportSession.h"

#include "processes/ImageIntegration/workflow/DefaultWorkflowStages.h"
#include "processes/ImageIntegration/workflow/FrameHeaderReader.h"
#include "processes/ImageIntegration/workflow/WorkflowExecutor.h"
#include "processes/ImageIntegration/workflow/WorkflowManifest.h"

#include <AfterNightConsumerRuntime/EngineSession.h>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <gtest/gtest.h>

#include <optional>
#include <utility>
#include <vector>

namespace
{

QJsonObject loadPrivateManifest()
{
    const QString path = qEnvironmentVariable("AFTERNIGHT_UG60_MANIFEST").trimmed();
    if (path.isEmpty())
    {
        return {};
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return {};
    }

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
    {
        return {};
    }
    return document.object();
}

std::vector<FrameId> frameIds(const QJsonObject& root, const QString& group_name)
{
    std::vector<FrameId> frames;
    const QJsonArray values = root.value(QStringLiteral("groups")).toObject().value(group_name).toArray();
    frames.reserve(static_cast<size_t>(values.size()));
    for (const QJsonValue& value : values)
    {
        const QString path = value.toObject().value(QStringLiteral("path")).toString();
        if (!path.isEmpty())
        {
            frames.emplace_back(path);
        }
    }
    return frames;
}

double firstExposure(const QJsonObject& root, const QString& group_name)
{
    const QJsonArray values = root.value(QStringLiteral("groups")).toObject().value(group_name).toArray();
    if (values.isEmpty())
    {
        return 0.0;
    }
    return values.first().toObject().value(QStringLiteral("exposure_seconds")).toDouble();
}

StackingSettings calibrationStackingSettings(NormalizationMode normalization)
{
    StackingSettings settings;
    settings.stackMethod = QStringLiteral("Average");
    settings.rejectionMethod = QStringLiteral("Sigma Clipping");
    settings.sigmaLow = 3.0;
    settings.sigmaHigh = 3.0;
    settings.weighting = QStringLiteral("None");
    settings.normalizationMode = normalization;
    settings.solveFinalMasterIfNeeded = false;
    return settings;
}

StackingSettings lightStackingSettings()
{
    StackingSettings settings;
    settings.stackMethod = QStringLiteral("Average");
    settings.rejectionMethod = QStringLiteral("Winsorized Sigma Clipping");
    settings.sigmaLow = 3.0;
    settings.sigmaHigh = 3.0;
    settings.weighting = QStringLiteral("None");
    settings.normalizationMode = NormalizationMode::Global;
    settings.globalNormalizationMethod = GlobalNormalizationMethod::AdditiveScaling;
    settings.scaleEstimator = ScaleEstimatorMethod::IKSS;
    settings.createQualityMaps = false;
    settings.createRejectionMaps = false;
    settings.drizzle.enabled = false;
    settings.solveFinalMasterIfNeeded = false;
    return settings;
}

WorkflowManifest::CalibrationStage::Group calibrationGroup(const QString& key, const QString& output_filename,
                                                           std::vector<FrameId> inputs, NormalizationMode normalization)
{
    WorkflowManifest::CalibrationStage::Group group;
    group.groupKey = GroupKey{key};
    group.outputFilename = output_filename;
    group.inputFiles = std::move(inputs);
    group.stacking = calibrationStackingSettings(normalization);
    return group;
}

WorkflowManifest::RegistrationGroup registrationGroup(const QString& key, const QString& resolution,
                                                      std::vector<FrameId> lights, const QString& cfa_pattern)
{
    WorkflowManifest::RegistrationGroup group;
    group.groupKey = GroupKey{key};
    group.resolution = resolution;
    group.object = QStringLiteral("UG60_VALIDATION_TARGET");
    group.lightFiles = std::move(lights);
    group.referenceFrame = group.lightFiles.front();
    group.settings.version = 1;
    group.settings.autoReferenceDuringRegistration = false;
    group.settings.referenceImage = group.referenceFrame->canonicalPath();
    group.settings.autoSolveUnsolvedFrames = false;
    group.settings.enrichWithGaiaCatalog = false;
    group.settings.registrationMethod = QStringLiteral("star_matching");
    group.settings.transformation = QStringLiteral("Homography");
    group.settings.transformMode = QStringLiteral("Auto");
    group.settings.alignmentMode = QStringLiteral("standard");
    group.settings.interpolation = QStringLiteral("Auto");
    group.settings.cfaPattern = cfa_pattern;
    group.settings.debayerMethod = QStringLiteral("RCD");
    return group;
}

WorkflowManifest::IntegrationGroup integrationGroup(const QString& key, const QString& output_filename,
                                                    const QString& resolution, const QString& filter,
                                                    const QString& exposure, std::vector<FrameId> lights,
                                                    const GroupKey& registration_key, const QString& output_dir)
{
    WorkflowManifest::IntegrationGroup group;
    group.groupKey = IntegrationGroupKey{GroupKey{key}};
    group.outputFilename = output_filename;
    group.resolution = resolution;
    group.object = QStringLiteral("UG60_VALIDATION_TARGET");
    group.filter = filter;
    group.exposure = exposure;
    group.lightFiles = std::move(lights);
    group.normalizationReferenceFrames = {group.lightFiles.front()};
    group.registrationGroupKey = registration_key;
    group.stacking = lightStackingSettings();
    group.normalizationCoefficientsArtifactPath =
        QDir(output_dir)
            .filePath(QStringLiteral(".afternight/image_integration/artifacts/normalization/%1.json")
                          .arg(QString(key).replace(QLatin1Char('|'), QLatin1Char('_'))));
    return group;
}

QJsonObject sanitizedRunSummary(const QString& branch, const WorkflowManifest& manifest,
                                const WorkflowExecutor::Result& result)
{
    QJsonObject root;
    root.insert(QStringLiteral("schema_version"), 1);
    root.insert(QStringLiteral("branch"), branch);
    root.insert(QStringLiteral("success"), result.success);
    root.insert(QStringLiteral("cancelled"), result.cancelled);
    root.insert(QStringLiteral("partial_success"), result.partialSuccess);
    root.insert(QStringLiteral("warning_count"), result.warningCount);
    root.insert(QStringLiteral("error_count"), result.errors.size());
    root.insert(QStringLiteral("duration_ms"), result.totalDurationMs);
    root.insert(QStringLiteral("calibrate_lights_in_memory"), manifest.calibrateLightsInMemory);
    root.insert(QStringLiteral("combine_rgb_images"), manifest.combineRgbImages);
    root.insert(QStringLiteral("external_tools_or_catalogs"), QStringLiteral("none"));

    QJsonArray stages;
    for (const auto& timing : result.stageResults)
    {
        QJsonObject stage;
        stage.insert(QStringLiteral("name"), timing.stageName);
        stage.insert(QStringLiteral("status"), timing.status);
        stage.insert(QStringLiteral("duration_ms"), timing.durationMs);
        stage.insert(QStringLiteral("groups_processed"), timing.groupsProcessed);
        stages.append(stage);
    }
    root.insert(QStringLiteral("stages"), stages);

    QJsonArray outputs;
    for (const auto& group : result.integrationGroupResults)
    {
        QJsonObject output;
        output.insert(QStringLiteral("group_key"), group.groupKey);
        output.insert(QStringLiteral("filename"), QFileInfo(group.outputPath).fileName());
        output.insert(QStringLiteral("bytes"), group.outputBytes);
        output.insert(QStringLiteral("light_frame_count"), group.lightFrameCount);
        output.insert(QStringLiteral("duration_ms"), group.durationMs);
        output.insert(QStringLiteral("success"), group.success);
        outputs.append(output);
    }
    root.insert(QStringLiteral("integration_outputs"), outputs);

    QJsonArray calibration_outputs;
    for (const auto& group : result.calibrationGroupResults)
    {
        QJsonObject output;
        output.insert(QStringLiteral("type"), group.type);
        output.insert(QStringLiteral("filename"), QFileInfo(group.masterPath).fileName());
        output.insert(QStringLiteral("input_count"), group.inputCount);
        output.insert(QStringLiteral("success"), group.success);
        calibration_outputs.append(output);
    }
    root.insert(QStringLiteral("calibration_outputs"), calibration_outputs);
    root.insert(QStringLiteral("report_filename"), QFileInfo(result.htmlReportPath).fileName());
    return root;
}

bool writeRunArtifacts(const QString& output_dir, const QString& branch, const WorkflowManifest& manifest,
                       const WorkflowExecutor::Result& result)
{
    QFile summary(QDir(output_dir).filePath(QStringLiteral("ug60-%1-run-summary.json").arg(branch)));
    if (!summary.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        return false;
    }
    summary.write(QJsonDocument(sanitizedRunSummary(branch, manifest, result)).toJson(QJsonDocument::Indented));
    summary.close();

    QFile log(QDir(output_dir).filePath(QStringLiteral("ug60-%1-native-private.log").arg(branch)));
    if (!log.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        return false;
    }
    log.write(result.capturedLog.toUtf8());
    return true;
}

QString ug61DataRoot()
{
    return qEnvironmentVariable("AFTERNIGHT_UG61_DATA_ROOT").trimmed();
}

QString ug61OutputRoot()
{
    return qEnvironmentVariable("AFTERNIGHT_UG61_OUTPUT_ROOT").trimmed();
}

std::vector<FrameId> ug61SelectFrames(const QString& data_root, const QString& frame_type, const QString& filter)
{
    QStringList candidates;
    QDirIterator iterator(QDir(data_root).filePath(frame_type), {QStringLiteral("*.fit"), QStringLiteral("*.fits")},
                          QDir::Files, QDirIterator::NoIteratorFlags);
    const QString filter_token = QStringLiteral("_%1_").arg(filter);
    while (iterator.hasNext())
    {
        const QString path = iterator.next();
        if (QFileInfo(path).fileName().contains(filter_token, Qt::CaseInsensitive))
        {
            candidates.push_back(QDir::cleanPath(path));
        }
    }
    candidates.sort(Qt::CaseInsensitive);

    constexpr qsizetype selection_size = 8;
    if (candidates.size() < selection_size)
    {
        return {};
    }

    std::vector<FrameId> selected;
    selected.reserve(static_cast<size_t>(selection_size));
    for (qsizetype i = 0; i < selection_size; ++i)
    {
        const qsizetype index = i * (candidates.size() - 1) / (selection_size - 1);
        selected.emplace_back(candidates.at(index));
    }
    return selected;
}

WorkflowManifest::RegistrationGroup ug61RegistrationGroup(std::vector<FrameId> lights)
{
    WorkflowManifest::RegistrationGroup group;
    group.groupKey = GroupKey{QStringLiteral("reg|UG61MONO|3008x3004")};
    group.resolution = QStringLiteral("3008x3004");
    group.object = QStringLiteral("UG61_VALIDATION_TARGET");
    group.lightFiles = std::move(lights);
    group.referenceFrame = group.lightFiles.front();
    group.settings.version = 1;
    group.settings.autoReferenceDuringRegistration = false;
    group.settings.referenceImage = group.referenceFrame->canonicalPath();
    group.settings.autoSolveUnsolvedFrames = false;
    group.settings.enrichWithGaiaCatalog = false;
    group.settings.registrationMethod = QStringLiteral("star_matching");
    group.settings.transformation = QStringLiteral("Homography");
    group.settings.transformMode = QStringLiteral("Auto");
    group.settings.alignmentMode = QStringLiteral("standard");
    group.settings.interpolation = QStringLiteral("Auto");
    group.settings.cfaPattern = QStringLiteral("Auto");
    group.settings.debayerMethod = QStringLiteral("RCD");
    return group;
}

WorkflowManifest::IntegrationGroup ug61IntegrationGroup(const QString& filter, const QString& suffix,
                                                        std::vector<FrameId> lights, const GroupKey& registration_key,
                                                        const QString& output_dir)
{
    WorkflowManifest::IntegrationGroup group;
    group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|UG61MONO|%1").arg(filter)}};
    group.outputFilename = QStringLiteral("ug61_mono_%1_master.fit").arg(suffix);
    group.resolution = QStringLiteral("3008x3004");
    group.object = QStringLiteral("UG61_VALIDATION_TARGET");
    group.filter = filter;
    group.exposure = QStringLiteral("300");
    group.lightFiles = std::move(lights);
    group.normalizationReferenceFrames = {group.lightFiles.front()};
    group.registrationGroupKey = registration_key;
    group.stacking = lightStackingSettings();
    group.normalizationCoefficientsArtifactPath =
        QDir(output_dir)
            .filePath(QStringLiteral(".afternight/image_integration/artifacts/normalization/ug61_%1.json").arg(suffix));
    return group;
}

bool writeUg61RunArtifacts(const QString& output_dir, const WorkflowManifest& manifest,
                           const WorkflowExecutor::Result& result)
{
    QFile summary(QDir(output_dir).filePath(QStringLiteral("ug61-mono-integration-run-summary.json")));
    if (!summary.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        return false;
    }
    summary.write(QJsonDocument(sanitizedRunSummary(QStringLiteral("mono-sho"), manifest, result))
                      .toJson(QJsonDocument::Indented));
    summary.close();

    const QString evidence_dir = QDir(output_dir).filePath(QStringLiteral("private-evidence"));
    if (!QDir().mkpath(evidence_dir))
    {
        return false;
    }
    QFile log(QDir(evidence_dir).filePath(QStringLiteral("ug61-native-private.log")));
    if (!log.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        return false;
    }
    log.write(result.capturedLog.toUtf8());
    return true;
}

} // namespace

/// A run over real data: the product's stages and archiver, in one engine session, the stage companions answering as
/// the stages do. The session is opened only once a case knows its data are there.
class RealDataWorkflowRun : public ::testing::Test
{
protected:
    /// Opens the case's session; fails the case when it cannot.
    [[nodiscard]] bool openSession()
    {
        an::Result<an::consumer::EngineSession> session = an::tests::integration::openImportSession(8u);
        EXPECT_TRUE(session.hasValue()) << session.status().message();
        if (!session.hasValue())
        {
            return false;
        }
        m_session.emplace(std::move(session).value());
        return true;
    }

    /// The manifest's checks, its frames' sizes read in the session.
    [[nodiscard]] WorkflowValidationResult validate(const WorkflowManifest& manifest) const
    {
        return manifest.validate(ImageIntegration::sessionFrameHeaderReader(*m_session));
    }

    [[nodiscard]] WorkflowExecutor::Result executeManifest(const WorkflowManifest& manifest) const
    {
        WorkflowExecutor executor;
        ImageIntegration::composeDefaultWorkflowStages(executor);
        WorkflowExecutionOptions options;
        options.session = &*m_session;
        return executor.execute(manifest, {}, options);
    }

    std::optional<an::consumer::EngineSession> m_session;
};

class UG60RealDataWorkflow : public RealDataWorkflowRun
{
};

class UG61RealDataWorkflow : public RealDataWorkflowRun
{
};

TEST_F(UG60RealDataWorkflow, Osc)
{
    const QJsonObject root = loadPrivateManifest();
    if (root.isEmpty())
    {
        GTEST_SKIP() << "AFTERNIGHT_UG60_MANIFEST is not set to a readable private manifest.";
    }
    ASSERT_TRUE(openSession());

    const QString output_dir = QDir(root.value(QStringLiteral("output_root")).toString()).filePath("osc");
    ASSERT_TRUE(QDir().mkpath(output_dir));

    const auto bias = frameIds(root, QStringLiteral("osc_bias"));
    const auto dark = frameIds(root, QStringLiteral("osc_dark"));
    const auto dark_flat = frameIds(root, QStringLiteral("osc_darkflat"));
    const auto flat = frameIds(root, QStringLiteral("osc_flat"));
    const auto lights = frameIds(root, QStringLiteral("osc_light"));
    ASSERT_EQ(bias.size(), 8u);
    ASSERT_EQ(dark.size(), 8u);
    ASSERT_EQ(dark_flat.size(), 8u);
    ASSERT_EQ(flat.size(), 8u);
    ASSERT_EQ(lights.size(), 8u);

    WorkflowManifest manifest;
    manifest.outputDirectory = output_dir;
    manifest.processLabel = QStringLiteral("UG-60 OSC validation");
    manifest.processingProfileId = QStringLiteral("ug60-native-conservative");
    manifest.processingProfileLabel = QStringLiteral("UG-60 native conservative");
    manifest.maxParallelCores = 8;
    manifest.calibrateLightsInMemory = true;

    WorkflowManifest::CalibrationStage bias_stage;
    bias_stage.frameType = QStringLiteral("BIAS");
    bias_stage.groups.push_back(calibrationGroup(QStringLiteral("bias|UG60OSC"), QStringLiteral("master_bias.fit"),
                                                 bias, NormalizationMode::None));
    manifest.biasStage = std::move(bias_stage);

    const QString master_dark = QDir(output_dir).filePath(QStringLiteral("master_dark.fit"));
    const QString master_dark_flat = QDir(output_dir).filePath(QStringLiteral("master_darkflat.fit"));
    WorkflowManifest::CalibrationStage dark_stage;
    dark_stage.frameType = QStringLiteral("DARK");
    dark_stage.groups.push_back(calibrationGroup(QStringLiteral("dark|UG60OSC|180s"), QFileInfo(master_dark).fileName(),
                                                 dark, NormalizationMode::None));
    dark_stage.groups.push_back(calibrationGroup(QStringLiteral("dark|UG60OSC|flat"),
                                                 QFileInfo(master_dark_flat).fileName(), dark_flat,
                                                 NormalizationMode::None));
    manifest.darkStage = std::move(dark_stage);

    const QString master_flat = QDir(output_dir).filePath(QStringLiteral("master_flat.fit"));
    WorkflowManifest::CalibrationStage flat_stage;
    flat_stage.frameType = QStringLiteral("FLAT");
    auto flat_group = calibrationGroup(QStringLiteral("flat|UG60OSC"), QFileInfo(master_flat).fileName(), flat,
                                       NormalizationMode::Global);
    flat_group.darkMaster = FrameId(master_dark_flat);
    manifest.flatStage = std::move(flat_stage);
    manifest.flatStage->groups.push_back(std::move(flat_group));

    for (const FrameId& light : lights)
    {
        WorkflowManifest::LightsCalibrationParams calibration;
        calibration.lightPath = light;
        calibration.darkPath = FrameId(master_dark);
        calibration.flatPath = FrameId(master_flat);
        calibration.settings.cosmeticCorrection = false;
        calibration.settings.flatAutoLevel = true;
        manifest.lightsCalibration.push_back(std::move(calibration));
    }

    auto registration = registrationGroup(QStringLiteral("reg|UG60OSC|5544x3684"), QStringLiteral("5544x3684"), lights,
                                          QStringLiteral("RGGB"));
    const GroupKey registration_key = registration.groupKey;
    manifest.registrationGroups.push_back(std::move(registration));
    manifest.integrationGroups.push_back(integrationGroup(
        QStringLiteral("int|UG60OSC|OSC"), QStringLiteral("ug60_osc_master.fit"), QStringLiteral("5544x3684"),
        QStringLiteral("OSC"), QString::number(firstExposure(root, QStringLiteral("osc_light"))), lights,
        registration_key, output_dir));

    const WorkflowValidationResult validation = validate(manifest);
    ASSERT_FALSE(validation.hasErrors());
    const WorkflowExecutor::Result result = executeManifest(manifest);
    ASSERT_TRUE(writeRunArtifacts(output_dir, QStringLiteral("osc"), manifest, result));
    EXPECT_TRUE(result.success) << result.errorMessage.toStdString();
    EXPECT_FALSE(result.generatedLightMasters.isEmpty());
    EXPECT_FALSE(result.htmlReportPath.isEmpty());
}

TEST_F(UG60RealDataWorkflow, MonoRgbLrgb)
{
    const QJsonObject root = loadPrivateManifest();
    if (root.isEmpty())
    {
        GTEST_SKIP() << "AFTERNIGHT_UG60_MANIFEST is not set to a readable private manifest.";
    }
    ASSERT_TRUE(openSession());

    const QString output_dir = QDir(root.value(QStringLiteral("output_root")).toString()).filePath("lrgb");
    ASSERT_TRUE(QDir().mkpath(output_dir));

    struct FilterInputs
    {
        QString key;
        QString label;
        QString suffix;
        std::vector<FrameId> lights;
        std::vector<FrameId> flats;
    };

    std::vector<FilterInputs> filters = {
        {QStringLiteral("luminance"), QStringLiteral("Luminance"), QStringLiteral("L"),
         frameIds(root, QStringLiteral("mono_light_luminance")), frameIds(root, QStringLiteral("mono_flat_luminance"))},
        {QStringLiteral("red"), QStringLiteral("Red"), QStringLiteral("R"),
         frameIds(root, QStringLiteral("mono_light_red")), frameIds(root, QStringLiteral("mono_flat_red"))},
        {QStringLiteral("green"), QStringLiteral("Green"), QStringLiteral("G"),
         frameIds(root, QStringLiteral("mono_light_green")), frameIds(root, QStringLiteral("mono_flat_green"))},
        {QStringLiteral("blue"), QStringLiteral("Blue"), QStringLiteral("B"),
         frameIds(root, QStringLiteral("mono_light_blue")), frameIds(root, QStringLiteral("mono_flat_blue"))}};

    std::vector<FrameId> registration_lights;
    for (const auto& filter : filters)
    {
        ASSERT_EQ(filter.lights.size(), 8u);
        ASSERT_EQ(filter.flats.size(), 8u);
        registration_lights.insert(registration_lights.end(), filter.lights.begin(), filter.lights.end());
    }

    WorkflowManifest manifest;
    manifest.outputDirectory = output_dir;
    manifest.processLabel = QStringLiteral("UG-60 mono RGB/LRGB validation");
    manifest.processingProfileId = QStringLiteral("ug60-native-conservative");
    manifest.processingProfileLabel = QStringLiteral("UG-60 native conservative");
    manifest.maxParallelCores = 8;
    manifest.calibrateLightsInMemory = true;
    manifest.combineRgbImages = true;

    WorkflowManifest::CalibrationStage flat_stage;
    flat_stage.frameType = QStringLiteral("FLAT");
    for (const auto& filter : filters)
    {
        const QString filename = QStringLiteral("master_flat_%1.fit").arg(filter.suffix);
        flat_stage.groups.push_back(calibrationGroup(QStringLiteral("flat|UG60MONO|%1").arg(filter.label), filename,
                                                     filter.flats, NormalizationMode::Global));
    }
    manifest.flatStage = std::move(flat_stage);

    for (const auto& filter : filters)
    {
        const FrameId master_flat(QDir(output_dir).filePath(QStringLiteral("master_flat_%1.fit").arg(filter.suffix)));
        for (const FrameId& light : filter.lights)
        {
            WorkflowManifest::LightsCalibrationParams calibration;
            calibration.lightPath = light;
            calibration.flatPath = master_flat;
            calibration.settings.cosmeticCorrection = false;
            calibration.settings.flatAutoLevel = true;
            manifest.lightsCalibration.push_back(std::move(calibration));
        }
    }

    auto registration = registrationGroup(QStringLiteral("reg|UG60MONO|3008x3004"), QStringLiteral("3008x3004"),
                                          registration_lights, QStringLiteral("Auto"));
    const GroupKey registration_key = registration.groupKey;
    manifest.registrationGroups.push_back(std::move(registration));

    for (const auto& filter : filters)
    {
        const QString group_name = QStringLiteral("mono_light_%1").arg(filter.key);
        manifest.integrationGroups.push_back(integrationGroup(
            QStringLiteral("int|UG60MONO|%1").arg(filter.label),
            QStringLiteral("ug60_mono_%1_master.fit").arg(filter.suffix), QStringLiteral("3008x3004"), filter.label,
            QString::number(firstExposure(root, group_name)), filter.lights, registration_key, output_dir));
    }

    const WorkflowValidationResult validation = validate(manifest);
    ASSERT_FALSE(validation.hasErrors());
    const WorkflowExecutor::Result result = executeManifest(manifest);
    ASSERT_TRUE(writeRunArtifacts(output_dir, QStringLiteral("mono-rgb-lrgb"), manifest, result));
    EXPECT_TRUE(result.success) << result.errorMessage.toStdString();
    EXPECT_GE(result.generatedLightMasters.size(), 5);
    EXPECT_FALSE(result.htmlReportPath.isEmpty());
}

TEST_F(UG61RealDataWorkflow, MonoIntegration)
{
    const QString data_root = ug61DataRoot();
    const QString output_root = ug61OutputRoot();
    if (data_root.isEmpty() || output_root.isEmpty())
    {
        GTEST_SKIP() << "AFTERNIGHT_UG61_DATA_ROOT and AFTERNIGHT_UG61_OUTPUT_ROOT are required.";
    }
    ASSERT_TRUE(openSession());

    struct FilterInputs
    {
        QString label;
        QString suffix;
        std::vector<FrameId> lights;
        std::vector<FrameId> flats;
    };

    std::vector<FilterInputs> filters = {
        {QStringLiteral("Ha"), QStringLiteral("Ha"),
         ug61SelectFrames(data_root, QStringLiteral("LIGHT"), QStringLiteral("Ha")),
         ug61SelectFrames(data_root, QStringLiteral("FLAT"), QStringLiteral("Ha"))},
        {QStringLiteral("OIII"), QStringLiteral("OIII"),
         ug61SelectFrames(data_root, QStringLiteral("LIGHT"), QStringLiteral("Oiii")),
         ug61SelectFrames(data_root, QStringLiteral("FLAT"), QStringLiteral("Oiii"))},
        {QStringLiteral("SII"), QStringLiteral("SII"),
         ug61SelectFrames(data_root, QStringLiteral("LIGHT"), QStringLiteral("Sii")),
         ug61SelectFrames(data_root, QStringLiteral("FLAT"), QStringLiteral("Sii"))},
    };

    std::vector<FrameId> registration_lights;
    for (const auto& filter : filters)
    {
        ASSERT_EQ(filter.lights.size(), 8u) << filter.label.toStdString();
        ASSERT_EQ(filter.flats.size(), 8u) << filter.label.toStdString();
        registration_lights.insert(registration_lights.end(), filter.lights.begin(), filter.lights.end());
    }

    const QString output_dir = QDir(output_root).filePath(QStringLiteral("mono-sho"));
    ASSERT_TRUE(QDir().mkpath(output_dir));

    WorkflowManifest manifest;
    manifest.outputDirectory = output_dir;
    manifest.processLabel = QStringLiteral("UG-61 mono narrowband validation");
    manifest.processingProfileId = QStringLiteral("ug61-native-conservative");
    manifest.processingProfileLabel = QStringLiteral("UG-61 native conservative");
    manifest.maxParallelCores = 8;
    manifest.calibrateLightsInMemory = true;

    WorkflowManifest::CalibrationStage flat_stage;
    flat_stage.frameType = QStringLiteral("FLAT");
    for (const auto& filter : filters)
    {
        flat_stage.groups.push_back(calibrationGroup(QStringLiteral("flat|UG61MONO|%1").arg(filter.label),
                                                     QStringLiteral("master_flat_%1.fit").arg(filter.suffix),
                                                     filter.flats, NormalizationMode::Global));
    }
    manifest.flatStage = std::move(flat_stage);

    for (const auto& filter : filters)
    {
        const FrameId master_flat(QDir(output_dir).filePath(QStringLiteral("master_flat_%1.fit").arg(filter.suffix)));
        for (const FrameId& light : filter.lights)
        {
            WorkflowManifest::LightsCalibrationParams calibration;
            calibration.lightPath = light;
            calibration.flatPath = master_flat;
            calibration.settings.cosmeticCorrection = false;
            calibration.settings.flatAutoLevel = true;
            manifest.lightsCalibration.push_back(std::move(calibration));
        }
    }

    auto registration = ug61RegistrationGroup(registration_lights);
    const GroupKey registration_key = registration.groupKey;
    manifest.registrationGroups.push_back(std::move(registration));
    for (const auto& filter : filters)
    {
        manifest.integrationGroups.push_back(
            ug61IntegrationGroup(filter.label, filter.suffix, filter.lights, registration_key, output_dir));
    }

    const WorkflowValidationResult validation = validate(manifest);
    ASSERT_FALSE(validation.hasErrors());
    const WorkflowExecutor::Result result = executeManifest(manifest);
    ASSERT_TRUE(writeUg61RunArtifacts(output_dir, manifest, result));
    EXPECT_TRUE(result.success) << result.errorMessage.toStdString();
    EXPECT_EQ(result.generatedLightMasters.size(), 3);
    EXPECT_FALSE(result.htmlReportPath.isEmpty());
}
