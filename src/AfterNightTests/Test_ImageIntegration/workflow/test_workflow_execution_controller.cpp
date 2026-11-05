// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QRegularExpression>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimeZone>

#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/model/RegistrationGroupEngine.h"
#include "processes/ImageIntegration/workflow/WorkflowExecutionController.h"

namespace
{
FrameInfo makeLightFrame(const QString& filename)
{
    const QString path = QDir::tempPath() + QDir::separator() + filename;
    FrameMetadata metadata(FrameType::Light, QSize(100, 100), 60.0, 0.0, 100, 10, QStringLiteral("L"),
                           QStringLiteral("M42"), QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC),
                           QStringLiteral("Default"));

    return FrameInfo(FrameId(path), path, metadata, QStringLiteral("2025-01-01"), {});
}

QString readRepoSourceFile(const QString& relativePath)
{
    QStringList seeds = {QDir::currentPath()};
    if (QCoreApplication::instance())
    {
        seeds.append(QCoreApplication::applicationDirPath());
    }
    seeds.append(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath());

    for (const QString& seed : seeds)
    {
        QDir dir(seed);
        while (true)
        {
            const QString candidate = dir.filePath(relativePath);
            if (QFileInfo::exists(candidate))
            {
                QFile file(candidate);
                if (file.open(QIODevice::ReadOnly | QIODevice::Text))
                {
                    return QString::fromUtf8(file.readAll());
                }
            }

            if (!dir.cdUp())
            {
                break;
            }
        }
    }

    return {};
}
} // namespace

TEST(WorkflowExecutionController, SourceGuardLaunchesProgressDialogAsynchronously)
{
    const QString source = readRepoSourceFile(
        QStringLiteral("src/AfterNightProcesses/processes/ImageIntegration/workflow/WorkflowExecutionController.cpp"));
    ASSERT_FALSE(source.isEmpty());

    EXPECT_FALSE(source.contains(QRegularExpression(QStringLiteral(R"(\.exec\s*\()"))));
    EXPECT_FALSE(source.contains(
        QRegularExpression(QStringLiteral(R"(QMessageBox::(warning|information|critical|question)\s*\()"))));
    EXPECT_FALSE(source.contains(QStringLiteral("runModalValidated")));
    EXPECT_TRUE(source.contains(QStringLiteral("&QDialog::finished")));
    EXPECT_TRUE(source.contains(QRegularExpression(QStringLiteral(R"(\->open\s*\()"))));
}

TEST(WorkflowExecutionController, ProcessPreparationUsesMonotonicExclusiveAsyncBoundaries)
{
    const QString source = readRepoSourceFile(
        QStringLiteral("src/AfterNightProcesses/processes/ImageIntegration/ImageIntegrationProcess.cpp"));
    ASSERT_FALSE(source.isEmpty());

    const qsizetype asyncPreparation = source.indexOf(QStringLiteral("void prepareExecutionPlanAsync"));
    ASSERT_GE(asyncPreparation, 0);
    const qsizetype concurrentRun = source.indexOf(QStringLiteral("QtConcurrent::run"), asyncPreparation);
    const qsizetype sharedPreparation =
        source.indexOf(QStringLiteral("ImageIntegration::preparePlannedWorkflowExecution("), asyncPreparation);
    ASSERT_GE(concurrentRun, 0);
    ASSERT_GE(sharedPreparation, 0);
    EXPECT_LT(concurrentRun, sharedPreparation);

    // The worker runs the shared preparation, whose boundaries are these, in this order.
    const QString shared = readRepoSourceFile(
        QStringLiteral("src/AfterNightProcesses/processes/ImageIntegration/workflow/WorkflowPreparation.cpp"));
    ASSERT_FALSE(shared.isEmpty());
    const qsizetype planned = shared.indexOf(QStringLiteral("preparePlannedWorkflowExecution(const std::function"));
    ASSERT_GE(planned, 0);
    const qsizetype userVisibleStart =
        shared.indexOf(QStringLiteral("const auto user_visible_start = std::chrono::steady_clock::now()"), planned);
    const qsizetype manifestBuild =
        shared.indexOf(QStringLiteral("WorkflowManifest manifest = buildManifest()"), planned);
    const qsizetype manifestNormalization =
        shared.indexOf(QStringLiteral("normalizeManifestForExecution(manifest)"), planned);
    const qsizetype planStart = shared.indexOf(
        QStringLiteral("const auto plan_and_validation_start = std::chrono::steady_clock::now()"), planned);

    ASSERT_GE(userVisibleStart, 0);
    ASSERT_GE(manifestBuild, 0);
    ASSERT_GE(manifestNormalization, 0);
    ASSERT_GE(planStart, 0);
    EXPECT_LT(userVisibleStart, manifestBuild);
    EXPECT_LT(manifestBuild, manifestNormalization);
    EXPECT_LT(manifestNormalization, planStart);
    EXPECT_LT(manifestBuild, planStart);
}

TEST(WorkflowExecutionController, PreparedExecutionLaunchesWithoutSecondUiThreadValidation)
{
    const QString process_source = readRepoSourceFile(
        QStringLiteral("src/AfterNightProcesses/processes/ImageIntegration/ImageIntegrationProcess.cpp"));
    const QString controller_source = readRepoSourceFile(
        QStringLiteral("src/AfterNightProcesses/processes/ImageIntegration/workflow/WorkflowExecutionController.cpp"));
    ASSERT_FALSE(process_source.isEmpty());
    ASSERT_FALSE(controller_source.isEmpty());

    EXPECT_GE(process_source.count(QStringLiteral("runAsyncPrevalidated(")), 2);
    const qsizetype function_start =
        controller_source.indexOf(QStringLiteral("bool WorkflowExecutionController::runAsyncPrevalidated"));
    ASSERT_GE(function_start, 0);
    const qsizetype function_end =
        controller_source.indexOf(QStringLiteral("bool WorkflowExecutionController::runAsyncPrepared"), function_start);
    ASSERT_GT(function_end, function_start);
    const QString function_source = controller_source.mid(function_start, function_end - function_start);
    EXPECT_FALSE(function_source.contains(QStringLiteral("normalizeManifestForExecution")));
    EXPECT_FALSE(function_source.contains(QStringLiteral("validateManifestAndWarn")));

    const QString shared_source = readRepoSourceFile(
        QStringLiteral("src/AfterNightProcesses/processes/ImageIntegration/workflow/WorkflowPreparation.cpp"));
    ASSERT_FALSE(shared_source.isEmpty());
    const qsizetype preparation_start =
        shared_source.indexOf(QStringLiteral("preparePlannedWorkflowExecution(const std::function"));
    ASSERT_GE(preparation_start, 0);
    const qsizetype normalization =
        shared_source.indexOf(QStringLiteral("normalizeManifestForExecution(manifest)"), preparation_start);
    const qsizetype plan_build = shared_source.indexOf(QStringLiteral("planner.buildPlan(manifest"), preparation_start);
    ASSERT_GE(normalization, 0);
    ASSERT_GE(plan_build, 0);
    EXPECT_LT(normalization, plan_build);
}

TEST(WorkflowExecutionController, LowMemoryPreparedManifestNormalizesBeforePlanning)
{
    WorkflowManifest manifest;
    manifest.calibrateLightsInMemory = true;
    manifest.lightsCalibration.emplace_back();
    manifest.stageSequence = {
        QStringLiteral("LightsRegistration"),
        QStringLiteral("LightsStacking"),
    };
    WorkflowManifest::IntegrationGroup integration;
    integration.stacking.cometStackingMode = QStringLiteral("Comet");
    manifest.integrationGroups.push_back(integration);

    constexpr std::uint64_t low_memory_bytes = 1024ull * 1024ull * 1024ull;
    WorkflowExecutionController::normalizeManifestForExecution(manifest, low_memory_bytes);

    EXPECT_FALSE(manifest.calibrateLightsInMemory);
    const int calibration_index = manifest.stageSequence.indexOf(QStringLiteral("LightsCalibration"));
    const int registration_index = manifest.stageSequence.indexOf(QStringLiteral("LightsRegistration"));
    const int alignment_index = manifest.stageSequence.indexOf(QStringLiteral("LightsAlignment"));
    const int stacking_index = manifest.stageSequence.indexOf(QStringLiteral("LightsStacking"));
    ASSERT_GE(calibration_index, 0);
    ASSERT_GE(registration_index, 0);
    ASSERT_GE(alignment_index, 0);
    ASSERT_GE(stacking_index, 0);
    EXPECT_LT(calibration_index, registration_index);
    EXPECT_LT(alignment_index, stacking_index);
}

TEST(WorkflowExecutionController, RegistrationOnlyManifestPreservesDiskCalibrationMode)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    IntegrationDataModel model;
    model.setOutputDirectory(tempDir.path());
    model.addFrames(FrameType::Light, {makeLightFrame(QStringLiteral("registration_only_disk.fit"))});
    model.setLightCalibrationCacheMode(LightCalibrationCacheMode::Disk);

    WorkflowExecutionController controller(&model, nullptr);
    const WorkflowManifest manifest = controller.buildRegistrationOnlyManifestAll(false);

    EXPECT_FALSE(manifest.calibrateLightsInMemory);
    EXPECT_FALSE(manifest.lightsCalibration.empty());
    EXPECT_TRUE(manifest.stageSequence.contains(QStringLiteral("LightsCalibration")));
    EXPECT_TRUE(manifest.stageSequence.contains(QStringLiteral("LightsRegistration")));
    EXPECT_FALSE(manifest.stageSequence.contains(QStringLiteral("LightsAlignment")));
    EXPECT_TRUE(manifest.integrationGroups.empty());
}

TEST(WorkflowExecutionController, RegistrationOnlyManifestOmitsDiskCalibrationStageWhenUsingInMemoryMode)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    IntegrationDataModel model;
    model.setOutputDirectory(tempDir.path());
    model.addFrames(FrameType::Light, {makeLightFrame(QStringLiteral("registration_only_memory.fit"))});
    model.setLightCalibrationCacheMode(LightCalibrationCacheMode::InMemory);

    WorkflowExecutionController controller(&model, nullptr);
    const WorkflowManifest manifest = controller.buildRegistrationOnlyManifestAll(false);

    EXPECT_TRUE(manifest.calibrateLightsInMemory);
    EXPECT_FALSE(manifest.lightsCalibration.empty());
    EXPECT_FALSE(manifest.stageSequence.contains(QStringLiteral("LightsCalibration")));
    EXPECT_TRUE(manifest.stageSequence.contains(QStringLiteral("LightsRegistration")));
    EXPECT_FALSE(manifest.stageSequence.contains(QStringLiteral("LightsAlignment")));
    EXPECT_TRUE(manifest.integrationGroups.empty());
}

TEST(WorkflowExecutionController, RegistrationOnlyManifestsSkipExcludedLights)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    FrameInfo included = makeLightFrame(QStringLiteral("registration_included.fit"));
    FrameInfo excluded = makeLightFrame(QStringLiteral("registration_excluded.fit"));
    excluded.excluded = true;

    IntegrationDataModel model;
    model.setOutputDirectory(tempDir.path());
    model.addFrames(FrameType::Light, {included, excluded});

    WorkflowExecutionController controller(&model, nullptr);
    const WorkflowManifest allManifest = controller.buildRegistrationOnlyManifestAll(false);

    ASSERT_EQ(allManifest.registrationGroups.size(), 1u);
    ASSERT_EQ(allManifest.registrationGroups.front().lightFiles.size(), 1u);
    EXPECT_EQ(allManifest.registrationGroups.front().lightFiles.front().canonicalPath(), included.id.canonicalPath());

    for (const auto& calibration : allManifest.lightsCalibration)
    {
        EXPECT_NE(calibration.lightPath.canonicalPath(), excluded.id.canonicalPath());
    }

    const auto& groups = model.registrationGroups();
    ASSERT_FALSE(groups.empty());
    const QString machineKey = RegistrationGroupEngine::toMachineGroupKey(groups.begin()->first).toString();

    WorkflowManifest groupManifest;
    ASSERT_TRUE(controller.buildRegistrationOnlyManifestForGroup(machineKey, false, groupManifest));

    ASSERT_EQ(groupManifest.registrationGroups.size(), 1u);
    ASSERT_EQ(groupManifest.registrationGroups.front().lightFiles.size(), 1u);
    EXPECT_EQ(groupManifest.registrationGroups.front().lightFiles.front().canonicalPath(), included.id.canonicalPath());
}
