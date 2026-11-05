// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The preparation between an accepted execution request and the executor: the full process's planned preparation,
// the direct front ends' preparation, the manifests they build and how a front end reads the result. A measurement
// driver runs these same functions, so what they do has to be pinned where both callers can see it.

#include <gtest/gtest.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTimeZone>

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "processes/ImageIntegration/lite/LiteIntegrationProfile.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/workflow/ExecutionPreparationSnapshot.h"
#include "processes/ImageIntegration/workflow/ManifestBuilder.h"
#include "processes/ImageIntegration/workflow/WorkflowExecutionController.h"
#include "processes/ImageIntegration/workflow/WorkflowPreparation.h"

namespace
{
using ImageIntegration::PreparedWorkflowDisposition;
using ImageIntegration::PreparedWorkflowExecution;

FrameInfo makeFrame(const QTemporaryDir& directory, FrameType type, const QString& filename)
{
    const QString path = directory.filePath(filename);
    QFile file(path);
    EXPECT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("x");
    file.close();
    FrameMetadata metadata(type, QSize(100, 100), type == FrameType::Bias ? 0.0 : 60.0, -10.0, 100, 10,
                           type == FrameType::Light || type == FrameType::Flat ? QStringLiteral("L") : QString(),
                           QStringLiteral("M42"), QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC),
                           QStringLiteral("Default"));
    return FrameInfo(FrameId(path), path, metadata, QStringLiteral("2025-01-01"), {});
}

// A model with lights, flats, darks and bias, and an output directory nothing has run in.
struct PreparationModel
{
    PreparationModel()
    {
        EXPECT_TRUE(directory.isValid());
        std::vector<FrameInfo> lights;
        std::vector<FrameInfo> flats;
        std::vector<FrameInfo> darks;
        std::vector<FrameInfo> bias;
        for (int index = 0; index < 3; ++index)
        {
            lights.push_back(makeFrame(directory, FrameType::Light, QStringLiteral("light_%1.fits").arg(index)));
            flats.push_back(makeFrame(directory, FrameType::Flat, QStringLiteral("flat_%1.fits").arg(index)));
            darks.push_back(makeFrame(directory, FrameType::Dark, QStringLiteral("dark_%1.fits").arg(index)));
            bias.push_back(makeFrame(directory, FrameType::Bias, QStringLiteral("bias_%1.fits").arg(index)));
        }
        model.beginBatchUpdate();
        model.addFrames(FrameType::Light, lights);
        model.addFrames(FrameType::Flat, flats);
        model.addFrames(FrameType::Dark, darks);
        model.addFrames(FrameType::Bias, bias);
        model.setOutputDirectory(directory.filePath(QStringLiteral("output")));
        model.endBatchUpdate();
    }

    QTemporaryDir directory;
    IntegrationDataModel model;
};

QByteArray canonical(const WorkflowManifest& manifest)
{
    return QJsonDocument(manifest.toJson()).toJson(QJsonDocument::Compact);
}

bool noIncrementalSelection(const WorkflowManifest& manifest)
{
    return manifest.incrementalBiasGroups.empty() && manifest.incrementalDarkGroups.empty() &&
           manifest.incrementalFlatGroups.empty() && manifest.incrementalRegistrationGroups.empty() &&
           manifest.incrementalIntegrationGroups.empty() && manifest.dirtyGroups.empty();
}

} // namespace

TEST(WorkflowPreparation, TheFullManifestIsTheBuildersWithTheStackingTabsOptions)
{
    PreparationModel prepared;
    const WorkflowManifest manifest = ImageIntegration::buildFullWorkflowManifest(prepared.model, {true, 6, true});

    ManifestBuilder builder(prepared.model);
    builder.setReprocessExisting(true);
    builder.setIncludeBiasFrames(true);
    builder.setIncludeDarkFrames(true);
    builder.setIncludeFlatFrames(true);
    builder.setIncludeLightFrames(true);
    WorkflowManifest expected = builder.build();
    expected.maxParallelCores = 6;
    expected.combineRgbImages = true;

    EXPECT_EQ(manifest.maxParallelCores, 6);
    EXPECT_TRUE(manifest.combineRgbImages);
    EXPECT_FALSE(manifest.integrationGroups.empty());
    EXPECT_EQ(canonical(manifest), canonical(expected));
}

TEST(WorkflowPreparation, TheControllerNormalizesThroughTheSameRules)
{
    WorkflowManifest lowMemory;
    lowMemory.calibrateLightsInMemory = true;
    lowMemory.lightsCalibration.emplace_back();
    lowMemory.stageSequence = {QStringLiteral("Bias"), QStringLiteral("LightsRegistration")};
    WorkflowManifest throughController = lowMemory;

    ImageIntegration::normalizeManifestForExecution(lowMemory, 1024ull * 1024ull * 1024ull);
    WorkflowExecutionController::normalizeManifestForExecution(throughController, 1024ull * 1024ull * 1024ull);

    EXPECT_FALSE(lowMemory.calibrateLightsInMemory);
    EXPECT_EQ(lowMemory.stageSequence, (QStringList{QStringLiteral("Bias"), QStringLiteral("LightsCalibration"),
                                                    QStringLiteral("LightsRegistration")}));
    EXPECT_EQ(canonical(throughController), canonical(lowMemory));

    WorkflowManifest ample;
    ample.calibrateLightsInMemory = true;
    ample.lightsCalibration.emplace_back();
    ample.stageSequence = {QStringLiteral("LightsCalibration"), QStringLiteral("LightsRegistration")};
    ImageIntegration::normalizeManifestForExecution(ample, 64ull * 1024ull * 1024ull * 1024ull);
    EXPECT_TRUE(ample.calibrateLightsInMemory);
    EXPECT_EQ(ample.stageSequence, QStringList{QStringLiteral("LightsRegistration")});
}

TEST(WorkflowPreparation, AnIncrementalPlanSelectsExactlyItsGroupsAndAFullRunNone)
{
    ExecutionPlan plan;
    plan.biasGroupsToProcess = {GroupKey{QStringLiteral("bias")}};
    plan.darkGroupsToProcess = {GroupKey{QStringLiteral("dark")}};
    plan.flatGroupsToProcess = {GroupKey{QStringLiteral("flat")}};
    plan.registrationGroupsToProcess = {GroupKey{QStringLiteral("registration")}};
    plan.integrationGroupsToProcess = {IntegrationGroupKey{GroupKey{QStringLiteral("first")}},
                                       IntegrationGroupKey{GroupKey{QStringLiteral("second")}}};

    WorkflowManifest manifest;
    manifest.dirtyGroups = {GroupKey{QStringLiteral("stale")}};
    ImageIntegration::applyIncrementalPlanToManifest(manifest, plan);
    ASSERT_EQ(manifest.incrementalBiasGroups.size(), 1u);
    EXPECT_EQ(manifest.incrementalBiasGroups.front().value, QStringLiteral("bias"));
    EXPECT_EQ(manifest.incrementalDarkGroups.front().value, QStringLiteral("dark"));
    EXPECT_EQ(manifest.incrementalFlatGroups.front().value, QStringLiteral("flat"));
    EXPECT_EQ(manifest.incrementalRegistrationGroups.front().value, QStringLiteral("registration"));
    ASSERT_EQ(manifest.incrementalIntegrationGroups.size(), 2u);
    ASSERT_EQ(manifest.dirtyGroups.size(), 2u);
    EXPECT_EQ(manifest.dirtyGroups[0].value, QStringLiteral("first"));
    EXPECT_EQ(manifest.dirtyGroups[1].value, QStringLiteral("second"));

    ImageIntegration::clearIncrementalSelections(manifest);
    EXPECT_TRUE(noIncrementalSelection(manifest));
}

TEST(WorkflowPreparation, APlannedPreparationOfAFreshOutputPlansAFullRun)
{
    PreparationModel prepared;
    std::vector<int> percents;
    const PreparedWorkflowExecution execution = ImageIntegration::preparePlannedWorkflowExecution(
        [&prepared]() { return ImageIntegration::buildFullWorkflowManifest(prepared.model, {}); }, std::nullopt,
        []() { return false; },
        [&percents](int percent, const QString& text) {
            EXPECT_FALSE(text.isEmpty());
            percents.push_back(percent);
        });

    ASSERT_TRUE(execution.success) << execution.errorMessage.toStdString();
    EXPECT_EQ(execution.plan.mode, ExecutionMode::Full);
    EXPECT_EQ(execution.manifest.executionMode, ExecutionMode::Full);
    EXPECT_TRUE(noIncrementalSelection(execution.manifest));
    EXPECT_NE(execution.preparationSnapshot, nullptr);
    EXPECT_TRUE(execution.preparationTiming.captured);
    EXPECT_FALSE(execution.decisions.empty());
    EXPECT_EQ(percents, (std::vector<int>{0, 25, 60, 75, 90, 100}));
    // The planted frames are one byte long, so what validation says of them is the manifest's business; the
    // disposition follows it.
    EXPECT_EQ(ImageIntegration::plannedDisposition(execution), execution.validation.hasErrors()
                                                                   ? PreparedWorkflowDisposition::Invalid
                                                                   : PreparedWorkflowDisposition::Ready);
}

TEST(WorkflowPreparation, APlannedPreparationReturnsABuildFailure)
{
    std::vector<int> percents;
    const PreparedWorkflowExecution execution = ImageIntegration::preparePlannedWorkflowExecution(
        []() -> WorkflowManifest { throw std::runtime_error("no frames"); }, std::nullopt, []() { return false; },
        [&percents](int percent, const QString&) { percents.push_back(percent); });

    EXPECT_FALSE(execution.success);
    EXPECT_EQ(execution.errorMessage, QStringLiteral("no frames"));
    EXPECT_TRUE(execution.preparationTiming.captured);
    EXPECT_EQ(execution.preparationTiming.planAndValidationMs, 0);
    EXPECT_EQ(percents, (std::vector<int>{0, 100}));
    EXPECT_EQ(ImageIntegration::plannedDisposition(execution), PreparedWorkflowDisposition::Failed);
}

TEST(WorkflowPreparation, APlannedPreparationStopsBeforeBuildingWhenCancelled)
{
    int built = 0;
    const PreparedWorkflowExecution execution = ImageIntegration::preparePlannedWorkflowExecution(
        [&built]() {
            ++built;
            return WorkflowManifest{};
        },
        std::nullopt, []() { return true; });

    EXPECT_FALSE(execution.success);
    EXPECT_EQ(built, 0);
    EXPECT_EQ(execution.errorMessage, QStringLiteral("Workflow preparation cancelled"));
}

TEST(WorkflowPreparation, ADirectPreparationValidatesWithoutAPlan)
{
    PreparationModel prepared;
    const PreparedWorkflowExecution execution = ImageIntegration::prepareDirectWorkflowExecution(
        [&prepared]() { return ImageIntegration::buildFullWorkflowManifest(prepared.model, {}); },
        []() { return false; });

    ASSERT_TRUE(execution.success) << execution.errorMessage.toStdString();
    EXPECT_NE(execution.preparationSnapshot, nullptr);
    EXPECT_TRUE(execution.decisions.empty());
    EXPECT_TRUE(execution.preparationTiming.captured);
    EXPECT_EQ(ImageIntegration::directDisposition(execution), execution.validation.hasErrors()
                                                                  ? PreparedWorkflowDisposition::Invalid
                                                                  : PreparedWorkflowDisposition::Ready);
}

TEST(WorkflowPreparation, ADirectPreparationStopsAfterBuildingWhenCancelled)
{
    int built = 0;
    const PreparedWorkflowExecution execution = ImageIntegration::prepareDirectWorkflowExecution(
        [&built]() {
            ++built;
            return WorkflowManifest{};
        },
        []() { return true; });

    EXPECT_FALSE(execution.success);
    EXPECT_EQ(built, 1);
    EXPECT_EQ(execution.errorMessage, QStringLiteral("Workflow preparation cancelled"));
    EXPECT_EQ(ImageIntegration::directDisposition(execution), PreparedWorkflowDisposition::Failed);
}

TEST(WorkflowPreparation, AFrontEndReadsAPreparationInTheProcessesOrder)
{
    PreparedWorkflowExecution execution;
    EXPECT_EQ(ImageIntegration::plannedDisposition(execution), PreparedWorkflowDisposition::Failed);

    execution.success = true;
    EXPECT_EQ(ImageIntegration::plannedDisposition(execution), PreparedWorkflowDisposition::NoGroups);
    EXPECT_EQ(ImageIntegration::directDisposition(execution), PreparedWorkflowDisposition::Ready);

    execution.validation.issues.push_back(ValidationIssue(ValidationIssue::Severity::Error, QStringLiteral("broken")));
    EXPECT_EQ(ImageIntegration::plannedDisposition(execution), PreparedWorkflowDisposition::Invalid);
    EXPECT_EQ(ImageIntegration::directDisposition(execution), PreparedWorkflowDisposition::Invalid);

    execution.plan.mode = ExecutionMode::Incremental;
    EXPECT_EQ(ImageIntegration::plannedDisposition(execution), PreparedWorkflowDisposition::NothingToProcess);

    execution.plan.integrationGroupsToProcess = {IntegrationGroupKey{GroupKey{QStringLiteral("group")}}};
    EXPECT_EQ(ImageIntegration::plannedDisposition(execution), PreparedWorkflowDisposition::Invalid);
}

TEST(WorkflowPreparation, LitePreparationRefusesAModelThatIsNotReady)
{
    IntegrationDataModel empty;
    try
    {
        (void)ImageIntegrationLite::prepareManifest(empty.toJson(), ImageIntegrationLite::defaultProcessingOptions(),
                                                    false);
        FAIL() << "an empty model was prepared";
    }
    catch (const std::runtime_error& error)
    {
        const WorkflowValidationResult readiness = ImageIntegrationLite::validateReadiness(empty);
        ASSERT_TRUE(readiness.hasErrors());
        EXPECT_EQ(QString::fromUtf8(error.what()), ImageIntegrationLite::firstReadinessErrors(readiness));
    }
}

TEST(WorkflowPreparation, TheFirstReadinessErrorsAreTheFirstThreeErrors)
{
    WorkflowValidationResult validation;
    validation.issues.push_back(ValidationIssue(ValidationIssue::Severity::Warning, QStringLiteral("w")));
    for (const char* message : {"a", "b", "c", "d"})
    {
        validation.issues.push_back(ValidationIssue(ValidationIssue::Severity::Error, QString::fromLatin1(message)));
    }
    EXPECT_EQ(ImageIntegrationLite::firstReadinessErrors(validation), QStringLiteral("a b c"));
}

TEST(WorkflowPreparation, TheLiteManifestIsTheBuildersWithLitesAdjustments)
{
    PreparationModel prepared;
    const ImageIntegrationLite::ProcessingOptions options = ImageIntegrationLite::defaultProcessingOptions();
    ImageIntegrationLite::applyDefaults(prepared.model, options);

    WorkflowManifest expected =
        ManifestBuilder(prepared.model).setReprocessExisting(false).setIncludeRejectionMaps(false).build();
    ImageIntegrationLite::applyManifestMetadata(expected);
    ImageIntegrationLite::applyExecutionOptions(expected, options);
    ImageIntegrationLite::applyDarkFlatCalibration(expected, prepared.model, false);

    const WorkflowManifest manifest = ImageIntegrationLite::buildManifest(prepared.model, options, false);
    EXPECT_FALSE(manifest.integrationGroups.empty());
    EXPECT_EQ(canonical(manifest), canonical(expected));

    const WorkflowManifest fromSnapshot =
        ImageIntegrationLite::prepareManifest(prepared.model.toJson(), options, false);
    EXPECT_EQ(fromSnapshot.integrationGroups.size(), manifest.integrationGroups.size());
}
