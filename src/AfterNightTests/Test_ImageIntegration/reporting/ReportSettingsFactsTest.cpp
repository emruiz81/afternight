// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The settings a report model states: each group's own, the ones stated once for the run, the ones that apply only
/// in a mode, and what was left to the application with what it resolved to and why.

#include <gtest/gtest.h>

#include "Test_ImageIntegration/reporting/ReportModelTestSupport.h"

#include <QFileInfo>
#include <QSet>

#include <algorithm>
#include <optional>

using namespace an::tests::integration::report;
using namespace ImageIntegration::Reporting;

namespace
{
std::optional<ReportScenario> built(const ReportScenarioId id)
{
    std::optional<ReportScenario> scenario = buildReportScenario(id);
    EXPECT_TRUE(scenario.has_value()) << scenarioName(id);
    return scenario;
}

const ReportDecision* decisionOf(const ReportModel& model, const QString& area)
{
    const auto found = std::find_if(model.decisions.begin(), model.decisions.end(),
                                    [&area](const ReportDecision& decision) { return decision.area == area; });
    return found == model.decisions.end() ? nullptr : &*found;
}

QString valueOf(const std::vector<ReportSettingsBlock>& blocks, const QString& title, const QString& name)
{
    const ReportSettingsRow* row = rowOf(blockOf(blocks, title), name);
    return row == nullptr ? QStringLiteral("<no such row>") : row->value;
}
} // namespace

TEST(ReportSettingsFacts, EveryGroupCarriesItsOwnSettings)
{
    const auto scenario = built(ReportScenarioId::FiveFilters);
    ASSERT_TRUE(scenario.has_value());

    const ReportModel model = buildScenarioModel(*scenario);

    for (const ReportMaster& master : model.masters)
    {
        SCOPED_TRACE(master.name.toStdString());
        if (master.kind == ReportMasterKind::CombinedRgb)
        {
            EXPECT_TRUE(master.settings.empty()) << "it is combined, not stacked";
            continue;
        }
        const ReportSettingsBlock* stacking = blockOf(master.settings, QStringLiteral("Stacking"));
        ASSERT_NE(stacking, nullptr);
        EXPECT_NE(rowOf(stacking, QStringLiteral("Stack Method")), nullptr);
        EXPECT_NE(rowOf(stacking, QStringLiteral("Rejection Method")), nullptr);
        EXPECT_NE(rowOf(stacking, QStringLiteral("Weighting")), nullptr);
        // A registration's settings are its group's, and are not repeated on the masters the group serves.
        EXPECT_EQ(blockOf(master.settings, QStringLiteral("Registration")), nullptr);
    }
    for (const ReportRegistrationGroup& group : model.registrationGroups)
    {
        ASSERT_EQ(group.settings.size(), 1u);
        EXPECT_EQ(group.settings.front().title, QStringLiteral("Registration"));
        EXPECT_NE(rowOf(&group.settings.front(), QStringLiteral("Transform Model")), nullptr);
        EXPECT_NE(rowOf(&group.settings.front(), QStringLiteral("Reference Frame")), nullptr);
    }
    for (const ReportCalibrationMaster& master : model.calibrationMasters)
    {
        ASSERT_EQ(master.settings.size(), 1u) << master.name.toStdString();
        EXPECT_EQ(master.settings.front().title, QStringLiteral("Stacking"));
    }
}

TEST(ReportSettingsFacts, AGroupsSettingsAreItsOwnAndNotAnothers)
{
    auto scenario = built(ReportScenarioId::FiveFilters);
    ASSERT_TRUE(scenario.has_value());
    ASSERT_GE(scenario->manifest.integrationGroups.size(), 2u);
    scenario->manifest.integrationGroups[0].stacking.sigmaHigh = 2.5;
    scenario->manifest.integrationGroups[1].stacking.sigmaHigh = 4.0;

    const ReportModel model = buildScenarioModel(*scenario);

    EXPECT_EQ(valueOf(model.masters[0].settings, QStringLiteral("Stacking"), QStringLiteral("Sigma High")),
              QStringLiteral("2.5"));
    EXPECT_EQ(valueOf(model.masters[1].settings, QStringLiteral("Stacking"), QStringLiteral("Sigma High")),
              QStringLiteral("4"));
}

TEST(ReportSettingsFacts, WhatIsStatedOnceForTheRunIsTheExecutionThePlateSolveAndTheColourCombination)
{
    auto scenario = built(ReportScenarioId::FiveFilters);
    ASSERT_TRUE(scenario.has_value());
    scenario->manifest.maxParallelCores = 0;

    const ReportRun run = buildScenarioModel(*scenario).run;

    ASSERT_EQ(run.settings.size(), 3u);
    EXPECT_EQ(run.settings[0].title, QStringLiteral("Execution"));
    EXPECT_EQ(run.settings[1].title, QStringLiteral("Plate Solve"));
    EXPECT_EQ(run.settings[2].title, QStringLiteral("Colour Combination"));
    EXPECT_TRUE(valueOf(run.settings, QStringLiteral("Execution"), QStringLiteral("Parallel CPU Threads"))
                    .startsWith(QStringLiteral("All available (")));
    EXPECT_EQ(valueOf(run.settings, QStringLiteral("Colour Combination"),
                      QStringLiteral("Combine Red, Green and Blue Masters")),
              QStringLiteral("Yes"));

    // A limit the user set is stated against what the host has.
    scenario->manifest.maxParallelCores = 1;
    const ReportRun limited = buildScenarioModel(*scenario).run;
    EXPECT_TRUE(valueOf(limited.settings, QStringLiteral("Execution"), QStringLiteral("Parallel CPU Threads"))
                    .startsWith(QStringLiteral("1 of ")));
}

TEST(ReportSettingsFacts, ThePlateSolvesSettingsNameNoPathAndNoKey)
{
    auto scenario = built(ReportScenarioId::OneMonoMaster);
    ASSERT_TRUE(scenario.has_value());
    scenario->manifest.plateSolve.astapPath = QStringLiteral("C:/secret/astap.exe");
    scenario->manifest.plateSolve.astrometryApiKey = QStringLiteral("a-secret-key");
    scenario->manifest.plateSolve.indexPath = QStringLiteral("C:/secret/index");
    scenario->manifest.plateSolve.focalLengthMm = 530.0;

    const ReportRun run = buildScenarioModel(*scenario).run;

    const ReportSettingsBlock* plateSolve = blockOf(run.settings, QStringLiteral("Plate Solve"));
    ASSERT_NE(plateSolve, nullptr);
    for (const ReportSettingsRow& row : plateSolve->rows)
    {
        EXPECT_FALSE(row.value.contains(QStringLiteral("secret"))) << row.name.toStdString();
    }
    EXPECT_EQ(valueOf(run.settings, QStringLiteral("Plate Solve"), QStringLiteral("Focal Length (mm)")),
              QStringLiteral("530"));
    // A hint that was not given is not stated as a zero.
    EXPECT_EQ(rowOf(plateSolve, QStringLiteral("Pixel Size (micrometres)")), nullptr);

    // One that was given is stated.
    scenario->manifest.plateSolve.pixelSizeUm = 3.76;
    EXPECT_EQ(valueOf(buildScenarioModel(*scenario).run.settings, QStringLiteral("Plate Solve"),
                      QStringLiteral("Pixel Size (micrometres)")),
              QStringLiteral("3.76"));
}

TEST(ReportSettingsFacts, ATransformLeftOnAutoSaysWhatItResolvedToAndWhy)
{
    auto scenario = built(ReportScenarioId::FiveFiltersWithoutMetrics);
    ASSERT_TRUE(scenario.has_value());
    RegistrationSettings& settings = scenario->manifest.registrationGroups.front().settings;
    settings.registrationMethod = QStringLiteral("star_matching");
    settings.transformMode = QStringLiteral("Auto");
    settings.transformation = QStringLiteral("Homography");

    const ReportModel model = buildScenarioModel(*scenario);

    const ReportRegistrationGroup& group = model.registrationGroups.front();
    const ReportSettingsRow* row =
        rowOf(blockOf(group.settings, QStringLiteral("Registration")), QStringLiteral("Transform Model"));
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->requested, QStringLiteral("Auto"));
    EXPECT_FALSE(row->reason.isEmpty());
    // With no record of what the registration used, the value says what Auto tries.
    EXPECT_TRUE(row->value.contains(QStringLiteral("Homography + adaptive Polynomial 2-4")))
        << row->value.toStdString();
    EXPECT_EQ(group.transform, row->value);

    const ReportDecision* decision = decisionOf(model, QStringLiteral("Registration: Transform Model"));
    ASSERT_NE(decision, nullptr);
    EXPECT_EQ(decision->groupKey, group.key);
    EXPECT_EQ(decision->group, group.name);
    EXPECT_EQ(decision->requested, QStringLiteral("Auto"));
    EXPECT_EQ(decision->resolved, row->value);
    EXPECT_EQ(decision->reason, row->reason);
}

TEST(ReportSettingsFacts, ATransformLeftOnAutoIsResolvedFromWhatTheRegistrationRecorded)
{
    auto scenario = built(ReportScenarioId::FiveFilters);
    ASSERT_TRUE(scenario.has_value());
    RegistrationSettings& settings = scenario->manifest.registrationGroups.front().settings;
    settings.registrationMethod = QStringLiteral("star_matching");
    settings.transformMode = QStringLiteral("Auto");

    const ReportModel model = buildScenarioModel(*scenario);

    const ReportRegistrationGroup& group = model.registrationGroups.front();
    ASSERT_FALSE(group.transformsUsed.empty()) << "the scenario's registration recorded its transforms";
    const ReportSettingsRow* row =
        rowOf(blockOf(group.settings, QStringLiteral("Registration")), QStringLiteral("Transform Model"));
    ASSERT_NE(row, nullptr);
    EXPECT_TRUE(row->value.startsWith(QStringLiteral("Auto (")));
    EXPECT_FALSE(row->value.contains(QStringLiteral("adaptive"))) << "the record says which transform was used";
    EXPECT_TRUE(row->reason.contains(QStringLiteral("metrics show"))) << row->reason.toStdString();
}

TEST(ReportSettingsFacts, ARejectionLeftOnAutoSaysWhatItResolvedToForTheGroupsFrames)
{
    auto scenario = built(ReportScenarioId::OneColourMaster);
    ASSERT_TRUE(scenario.has_value());
    WorkflowManifest::IntegrationGroup& group = scenario->manifest.integrationGroups.front();
    group.stacking.requestedRejectionMethod = QStringLiteral("Auto");
    group.stacking.rejectionMethod = QStringLiteral("Winsorized Sigma");
    const QString frames = QString::number(group.lightFiles.size());
    ASSERT_TRUE(scenario->manifest.biasStage.has_value());
    scenario->manifest.biasStage->groups.front().stacking.requestedRejectionMethod = QStringLiteral("Auto");
    scenario->manifest.biasStage->groups.front().stacking.rejectionMethod = QStringLiteral("Sigma Clipping");

    const ReportModel model = buildScenarioModel(*scenario);

    const ReportSettingsRow* row =
        rowOf(blockOf(model.masters.front().settings, QStringLiteral("Stacking")), QStringLiteral("Rejection Method"));
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->value, QStringLiteral("Winsorized Sigma"));
    EXPECT_EQ(row->requested, QStringLiteral("Auto"));
    EXPECT_TRUE(row->reason.contains(frames)) << row->reason.toStdString();

    const ReportDecision* stacking = decisionOf(model, QStringLiteral("Stacking: Rejection Method"));
    ASSERT_NE(stacking, nullptr);
    EXPECT_EQ(stacking->resolved, QStringLiteral("Winsorized Sigma"));
    const ReportDecision* calibration = decisionOf(model, QStringLiteral("Bias Stacking: Rejection Method"));
    ASSERT_NE(calibration, nullptr);
    EXPECT_EQ(calibration->resolved, QStringLiteral("Sigma Clipping"));

    // A method the user stated is no decision of the application.
    group.stacking.requestedRejectionMethod = QStringLiteral("Winsorized Sigma");
    scenario->manifest.biasStage->groups.front().stacking.requestedRejectionMethod.clear();
    const ReportModel stated = buildScenarioModel(*scenario);
    EXPECT_EQ(decisionOf(stated, QStringLiteral("Stacking: Rejection Method")), nullptr);
    EXPECT_EQ(decisionOf(stated, QStringLiteral("Bias Stacking: Rejection Method")), nullptr);
    const ReportSettingsRow* statedRow =
        rowOf(blockOf(stated.masters.front().settings, QStringLiteral("Stacking")), QStringLiteral("Rejection Method"));
    ASSERT_NE(statedRow, nullptr);
    EXPECT_TRUE(statedRow->requested.isEmpty());
    EXPECT_TRUE(statedRow->reason.isEmpty());
}

TEST(ReportSettingsFacts, EveryDecisionIsARowOfItsGroupsSettings)
{
    for (const ReportScenarioId id : allReportScenarios())
    {
        SCOPED_TRACE(scenarioName(id));
        const auto scenario = built(id);
        ASSERT_TRUE(scenario.has_value());
        const ReportModel model = buildScenarioModel(*scenario);

        int decided = 0;
        const auto count = [&decided](const std::vector<ReportSettingsBlock>& blocks) {
            for (const ReportSettingsBlock& block : blocks)
            {
                for (const ReportSettingsRow& row : block.rows)
                {
                    EXPECT_FALSE(row.name.isEmpty());
                    // A setting is either stated, or decided with what was asked and why.
                    EXPECT_EQ(row.requested.isEmpty(), row.reason.isEmpty()) << row.name.toStdString();
                    decided += row.requested.isEmpty() ? 0 : 1;
                }
            }
        };
        for (const ReportMaster& master : model.masters)
        {
            count(master.settings);
        }
        for (const ReportCalibrationMaster& master : model.calibrationMasters)
        {
            count(master.settings);
        }
        for (const ReportRegistrationGroup& group : model.registrationGroups)
        {
            count(group.settings);
        }
        EXPECT_EQ(static_cast<int>(model.decisions.size()), decided);
        for (const ReportDecision& decision : model.decisions)
        {
            EXPECT_FALSE(decision.area.isEmpty());
            EXPECT_FALSE(decision.groupKey.isEmpty());
            EXPECT_FALSE(decision.resolved.isEmpty());
            EXPECT_FALSE(decision.reason.contains(QStringLiteral("%1")));
        }
    }
}

TEST(ReportSettingsFacts, ASettingOfAModeIsStatedOnlyInThatMode)
{
    // Comet: the stacking's mode and the registration's observations.
    const auto comet = built(ReportScenarioId::CometRun);
    ASSERT_TRUE(comet.has_value());
    const ReportModel cometModel = buildScenarioModel(*comet);
    EXPECT_NE(rowOf(blockOf(cometModel.masters.front().settings, QStringLiteral("Stacking")),
                    QStringLiteral("Comet Stacking Mode")),
              nullptr);
    EXPECT_NE(rowOf(blockOf(cometModel.registrationGroups.front().settings, QStringLiteral("Registration")),
                    QStringLiteral("Comet Observations")),
              nullptr);
    EXPECT_EQ(cometModel.registrationGroups.front().transform, QStringLiteral("Comet Tracking"));

    // An exposure merge: the exposures it merges.
    const auto merge = built(ReportScenarioId::HdrExposureMerge);
    ASSERT_TRUE(merge.has_value());
    const ReportModel mergeModel = buildScenarioModel(*merge);
    const ReportSettingsBlock* mergeStacking = blockOf(mergeModel.masters.front().settings, QStringLiteral("Stacking"));
    EXPECT_EQ(
        valueOf(mergeModel.masters.front().settings, QStringLiteral("Stacking"), QStringLiteral("HDR Exposure Merge")),
        QStringLiteral("Yes"));
    ASSERT_NE(rowOf(mergeStacking, QStringLiteral("HDR Exposure Buckets")), nullptr);
    EXPECT_TRUE(rowOf(mergeStacking, QStringLiteral("HDR Exposure Buckets"))->value.contains(QStringLiteral("180 s")));

    // Drizzle: a block of its own, on the drizzled group alone.
    const auto drizzle = built(ReportScenarioId::DrizzledGroup);
    ASSERT_TRUE(drizzle.has_value());
    const ReportModel drizzleModel = buildScenarioModel(*drizzle);
    EXPECT_NE(blockOf(drizzleModel.masters.front().settings, QStringLiteral("Drizzle")), nullptr);
    EXPECT_EQ(blockOf(drizzleModel.masters.back().settings, QStringLiteral("Drizzle")), nullptr);

    // Manual control points: their counts, in the group registered with them alone.
    const auto manual = built(ReportScenarioId::DistortionCorrectionsAndManualPoints);
    ASSERT_TRUE(manual.has_value());
    const ReportModel manualModel = buildScenarioModel(*manual);
    EXPECT_EQ(rowOf(blockOf(manualModel.registrationGroups.front().settings, QStringLiteral("Registration")),
                    QStringLiteral("Manual Control-Point Frames")),
              nullptr);
    EXPECT_EQ(valueOf(manualModel.registrationGroups.back().settings, QStringLiteral("Registration"),
                      QStringLiteral("Manual Control-Point Frames")),
              QStringLiteral("11"));

    // A plain run states none of them.
    const auto plain = built(ReportScenarioId::OneMonoMaster);
    ASSERT_TRUE(plain.has_value());
    const ReportModel plainModel = buildScenarioModel(*plain);
    const ReportSettingsBlock* plainStacking = blockOf(plainModel.masters.front().settings, QStringLiteral("Stacking"));
    ASSERT_NE(plainStacking, nullptr);
    EXPECT_EQ(rowOf(plainStacking, QStringLiteral("Comet Stacking Mode")), nullptr);
    EXPECT_EQ(rowOf(plainStacking, QStringLiteral("HDR Exposure Buckets")), nullptr);
    EXPECT_EQ(blockOf(plainModel.masters.front().settings, QStringLiteral("Drizzle")), nullptr);
    EXPECT_EQ(blockOf(plainModel.masters.front().settings, QStringLiteral("Local Normalization")), nullptr);
}

TEST(ReportSettingsFacts, TheNormalizationsReferenceSaysWhoChoseIt)
{
    auto scenario = built(ReportScenarioId::OneMonoMaster);
    ASSERT_TRUE(scenario.has_value());
    WorkflowManifest::IntegrationGroup& group = scenario->manifest.integrationGroups.front();
    const QString light = QFileInfo(group.lightFiles.front().canonicalPath()).fileName();

    // No normalization: nothing to state about a reference.
    group.stacking.normalizationMode = NormalizationMode::None;
    const ReportModel none = buildScenarioModel(*scenario);
    EXPECT_EQ(blockOf(none.masters.front().settings, QStringLiteral("Normalization Reference")), nullptr);
    EXPECT_EQ(rowOf(blockOf(none.masters.front().settings, QStringLiteral("Stacking")),
                    QStringLiteral("Global Normalization Method")),
              nullptr);

    // Normalized to a light the application chose.
    group.stacking.normalizationMode = NormalizationMode::Global;
    group.stacking.normalizationReferenceMode = NormalizationReferenceMode::Automatic;
    group.stacking.manualNormalizationReference.clear();
    group.normalizationReferenceFrames = {group.lightFiles.front()};
    const ReportModel automatic = buildScenarioModel(*scenario);
    const ReportSettingsRow* chosen =
        rowOf(blockOf(automatic.masters.front().settings, QStringLiteral("Normalization Reference")),
              QStringLiteral("Reference Frames"));
    ASSERT_NE(chosen, nullptr);
    EXPECT_EQ(chosen->value, light);
    EXPECT_FALSE(chosen->requested.isEmpty());
    EXPECT_NE(decisionOf(automatic, QStringLiteral("Normalization Reference: Reference Frames")), nullptr);

    // Normalized to a light the user named.
    group.stacking.normalizationReferenceMode = NormalizationReferenceMode::Manual;
    group.stacking.manualNormalizationReference = group.lightFiles.front().canonicalPath();
    const ReportModel manual = buildScenarioModel(*scenario);
    const ReportSettingsBlock* block =
        blockOf(manual.masters.front().settings, QStringLiteral("Normalization Reference"));
    ASSERT_NE(rowOf(block, QStringLiteral("Reference Frames")), nullptr);
    EXPECT_TRUE(rowOf(block, QStringLiteral("Reference Frames"))->requested.isEmpty());
    EXPECT_EQ(valueOf(manual.masters.front().settings, QStringLiteral("Normalization Reference"),
                      QStringLiteral("Manual Reference Requested")),
              light);
    EXPECT_EQ(decisionOf(manual, QStringLiteral("Normalization Reference: Reference Frames")), nullptr);
}

TEST(ReportSettingsFacts, LocalNormalizationStatesItsOwnSettingsAndTheGlobalMethodItSets)
{
    auto scenario = built(ReportScenarioId::OneMonoMaster);
    ASSERT_TRUE(scenario.has_value());
    WorkflowManifest::IntegrationGroup& group = scenario->manifest.integrationGroups.front();
    group.stacking.normalizationMode = NormalizationMode::Local;
    group.stacking.localNormalization.enabled = true;
    group.stacking.localNormalization.gridSize = 96;
    group.stacking.globalNormalizationMethod = GlobalNormalizationMethod::Multiplicative;

    const ReportModel model = buildScenarioModel(*scenario);

    EXPECT_EQ(
        valueOf(model.masters.front().settings, QStringLiteral("Local Normalization"), QStringLiteral("Grid Size")),
        QStringLiteral("96"));
    const QString method = valueOf(model.masters.front().settings, QStringLiteral("Stacking"),
                                   QStringLiteral("Global Normalization Method"));
    EXPECT_TRUE(method.startsWith(QStringLiteral("Additive + Scaling"))) << method.toStdString();
    EXPECT_FALSE(method.contains(QStringLiteral("Multiplicative")));
}

TEST(ReportSettingsFacts, AFigureOfASettingIsPrintedForAPerson)
{
    auto scenario = built(ReportScenarioId::OneMonoMaster);
    ASSERT_TRUE(scenario.has_value());
    WorkflowManifest::IntegrationGroup& group = scenario->manifest.integrationGroups.front();
    group.stacking.sigmaLow = 3.0;
    group.stacking.minWeightFloor = 0.05;
    group.stacking.exponentialAlpha = 1.0 / 3.0;

    const std::vector<ReportSettingsBlock> settings = buildScenarioModel(*scenario).masters.front().settings;

    EXPECT_EQ(valueOf(settings, QStringLiteral("Stacking"), QStringLiteral("Sigma Low")), QStringLiteral("3"));
    EXPECT_EQ(valueOf(settings, QStringLiteral("Stacking"), QStringLiteral("Minimum Weight Floor")),
              QStringLiteral("0.05"));
    EXPECT_EQ(valueOf(settings, QStringLiteral("Stacking"), QStringLiteral("Exponential Alpha")),
              QStringLiteral("0.3333"));
}
