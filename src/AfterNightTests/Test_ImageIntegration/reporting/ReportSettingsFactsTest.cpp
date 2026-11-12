// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The settings a report model states: each group's own, the ones stated once for the run, the ones that apply only
/// in a mode, and what was left to the application with what it resolved to and why.

#include <gtest/gtest.h>

#include <QDir>
#include <QRegularExpression>

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

// A registration that is told a linear mode is named by its base transform, whatever order its settings still
// hold from a polynomial mode chosen before.
TEST(ReportSettingsFacts, ALinearTransformModeIsNamedByItsBaseTransformWhateverOrderIsStored)
{
    auto scenario = built(ReportScenarioId::OneMonoMaster);
    ASSERT_TRUE(scenario.has_value());
    RegistrationSettings& settings = scenario->manifest.registrationGroups.front().settings;
    settings.registrationMethod = QStringLiteral("star_matching");
    settings.transformation = QStringLiteral("Affine");
    settings.transformMode = QStringLiteral("linear");
    settings.transformOrder = 3;
    settings.twoPassRefinementEnabled = true;

    const ReportModel model = buildScenarioModel(*scenario);
    const ReportRegistrationGroup& group = model.registrationGroups.front();

    EXPECT_EQ(valueOf(group.settings, QStringLiteral("Registration"), QStringLiteral("Transform Model")),
              QStringLiteral("Affine"));
    EXPECT_FALSE(group.transform.contains(QStringLiteral("Polynomial"))) << group.transform.toStdString();
    for (const ReportSettingsBlock& block : group.settings)
    {
        for (const ReportSettingsRow& row : block.rows)
        {
            EXPECT_FALSE(row.value.contains(QStringLiteral("Polynomial 3"))) << row.name.toStdString();
        }
    }
}

// The star matcher's cap is named for what it caps, the mode is stated as the settings hold it, and nothing is said
// of a refinement the mode already implies. The detection's own settings and the two left on Auto are rows too.
TEST(ReportSettingsFacts, TheRegistrationsSettingsNameTheStarCapTheModeAndTheDetection)
{
    auto scenario = built(ReportScenarioId::OneMonoMaster);
    ASSERT_TRUE(scenario.has_value());
    RegistrationSettings& settings = scenario->manifest.registrationGroups.front().settings;
    settings.registrationMethod = QStringLiteral("star_matching");
    settings.maxStarPairs = 1500;
    settings.transformation = QStringLiteral("Homography");
    settings.transformMode = QStringLiteral("polynomial");
    settings.transformOrder = 3;
    settings.twoPassRefinementEnabled = true;
    settings.radius = 7;
    settings.roundnessRangeEnabled = true;
    settings.profileType = QStringLiteral("Moffat");
    settings.interpolation = QStringLiteral("Auto");
    settings.cfaPattern = QStringLiteral("Auto");

    const ReportModel model = buildScenarioModel(*scenario);
    const std::vector<ReportSettingsBlock>& blocks = model.registrationGroups.front().settings;
    const ReportSettingsBlock* registration = blockOf(blocks, QStringLiteral("Registration"));
    ASSERT_NE(registration, nullptr);
    const auto digits = [](QString text) { return text.remove(QRegularExpression(QStringLiteral("[^0-9]"))); };

    const ReportSettingsRow* cap = rowOf(registration, QStringLiteral("Max Stars per Frame"));
    ASSERT_NE(cap, nullptr);
    EXPECT_EQ(digits(cap->value), QStringLiteral("1500"));
    EXPECT_EQ(rowOf(registration, QStringLiteral("Max Star Pairs")), nullptr);
    EXPECT_EQ(valueOf(blocks, QStringLiteral("Registration"), QStringLiteral("Transform Mode")),
              QStringLiteral("polynomial"));
    for (const ReportSettingsRow& row : registration->rows)
    {
        EXPECT_FALSE(row.name.contains(QStringLiteral("Two-pass"), Qt::CaseInsensitive)) << row.name.toStdString();
    }

    EXPECT_EQ(digits(valueOf(blocks, QStringLiteral("Registration"), QStringLiteral("Detection Radius"))),
              QStringLiteral("7"));
    EXPECT_EQ(valueOf(blocks, QStringLiteral("Registration"), QStringLiteral("Roundness Range Enabled")),
              QStringLiteral("Yes"));
    EXPECT_EQ(valueOf(blocks, QStringLiteral("Registration"), QStringLiteral("Profile Type")),
              QStringLiteral("Moffat"));

    // What was left on Auto says what it resolved to, what was asked for and why.
    const ReportSettingsRow* interpolation = rowOf(registration, QStringLiteral("Interpolation"));
    ASSERT_NE(interpolation, nullptr);
    EXPECT_EQ(interpolation->value, QStringLiteral("Lanczos4"));
    EXPECT_EQ(interpolation->requested, QStringLiteral("Auto"));
    EXPECT_FALSE(interpolation->reason.isEmpty());
    const ReportSettingsRow* pattern = rowOf(registration, QStringLiteral("CFA Pattern"));
    ASSERT_NE(pattern, nullptr);
    EXPECT_EQ(pattern->value, QStringLiteral("from metadata"));
    EXPECT_EQ(pattern->requested, QStringLiteral("Auto"));
}

// A drizzled group normalized locally states its local normalization as any group does, with the artifact its
// coefficients are kept in.
TEST(ReportSettingsFacts, ADrizzledGroupNormalizedLocallyListsItsLocalNormalizationAsAnyGroupDoes)
{
    auto scenario = built(ReportScenarioId::OneMonoMaster);
    ASSERT_TRUE(scenario.has_value());
    WorkflowManifest::IntegrationGroup& group = scenario->manifest.integrationGroups.front();
    group.stacking.normalizationMode = NormalizationMode::Local;
    group.stacking.localNormalization.enabled = true;
    group.stacking.localNormalization.referenceStrategy = QStringLiteral("Single Best Frame");
    group.stacking.localNormalization.minDetectionSnr = 55.0;
    group.drizzle = WorkflowManifest::IntegrationGroup::DrizzleSettings{};
    group.drizzle->enabled = true;
    group.normalizationCoefficientsArtifactPath =
        QDir(scenario->outputDirectory).filePath(QStringLiteral("normalization/coefficients.json"));

    const ReportModel model = buildScenarioModel(*scenario);
    const std::vector<ReportSettingsBlock>& blocks = model.masters.front().settings;

    ASSERT_NE(blockOf(blocks, QStringLiteral("Drizzle")), nullptr);
    EXPECT_EQ(valueOf(blocks, QStringLiteral("Local Normalization"), QStringLiteral("Reference Strategy")),
              QStringLiteral("Single Best Frame"));
    EXPECT_EQ(valueOf(blocks, QStringLiteral("Local Normalization"), QStringLiteral("Min SNR (flux scale)")),
              QStringLiteral("55"));
    const QString artifact =
        valueOf(blocks, QStringLiteral("Normalization Reference"), QStringLiteral("Coefficients Artifact"));
    EXPECT_TRUE(artifact.endsWith(QStringLiteral("coefficients.json"))) << artifact.toStdString();
}

// An exposure merge names its stack method as a merge of the method it averages by.
TEST(ReportSettingsFacts, AnExposureMergeNamesItsStackMethodAsAMerge)
{
    auto scenario = built(ReportScenarioId::OneMonoMaster);
    ASSERT_TRUE(scenario.has_value());
    WorkflowManifest::IntegrationGroup& group = scenario->manifest.integrationGroups.front();
    group.stacking.stackMethod = QStringLiteral("Average");
    EXPECT_EQ(valueOf(buildScenarioModel(*scenario).masters.front().settings, QStringLiteral("Stacking"),
                      QStringLiteral("Stack Method")),
              QStringLiteral("Average"));

    group.stacking.hdr.enabled = true;
    EXPECT_EQ(valueOf(buildScenarioModel(*scenario).masters.front().settings, QStringLiteral("Stacking"),
                      QStringLiteral("Stack Method")),
              QStringLiteral("HDR Merge (Average)"));
}

// What only a stack of lights reads changes its master or what is written beside it, so each is a row of the group's
// stacking with the value the group holds.
TEST(ReportSettingsFacts, WhatOnlyAStackOfLightsReadsIsStatedAmongItsStackingSettings)
{
    auto scenario = built(ReportScenarioId::OneMonoMaster);
    ASSERT_TRUE(scenario.has_value());
    StackingSettings& stacking = scenario->manifest.integrationGroups.front().stacking;
    stacking.stackMethod = QStringLiteral("Average");
    stacking.rejectionMethod = QStringLiteral("Winsorized Sigma Clipping");
    stacking.weighting = QStringLiteral("Weighted FWHM");
    stacking.normalizationMode = NormalizationMode::Global;
    stacking.createQualityMaps = true;
    stacking.spatialRejectionClustering = true;
    stacking.restackExcludingFlaggedFrames = true;
    stacking.localWeighting.enabled = true;
    stacking.localWeighting.windowSize = 7;
    stacking.localWeighting.gridSpacing = 48;
    stacking.localWeighting.snrWeightExponent = 1.5;
    stacking.overlapNormalization.enabled = true;
    stacking.overlapNormalization.minOverlapPixels = 750;
    stacking.overlapNormalization.minOverlapFraction = 0.05;
    stacking.solveFinalMasterIfNeeded = true;

    const std::vector<ReportSettingsBlock> settings = buildScenarioModel(*scenario).masters.front().settings;
    const auto stated = [&settings](const QString& name) {
        return valueOf(settings, QStringLiteral("Stacking"), name);
    };

    EXPECT_EQ(stated(QStringLiteral("Per-Pixel Weighting")), QStringLiteral("Yes"));
    EXPECT_EQ(stated(QStringLiteral("Per-Pixel Weighting Window Size")), QStringLiteral("7"));
    EXPECT_EQ(stated(QStringLiteral("Per-Pixel Weighting Grid Spacing")), QStringLiteral("48"));
    EXPECT_EQ(stated(QStringLiteral("Per-Pixel Weighting SNR Exponent")), QStringLiteral("1.5"));
    EXPECT_EQ(stated(QStringLiteral("Analyze Spatial Rejection Clusters")), QStringLiteral("Yes"));
    EXPECT_EQ(stated(QStringLiteral("Re-Stack Excluding Flagged Frames")), QStringLiteral("Yes"));
    EXPECT_EQ(stated(QStringLiteral("Create Quality Maps")), QStringLiteral("Yes"));
    EXPECT_EQ(stated(QStringLiteral("Overlap Normalization")), QStringLiteral("Yes"));
    EXPECT_EQ(stated(QStringLiteral("Min Overlap Pixels")), QStringLiteral("750"));
    EXPECT_EQ(stated(QStringLiteral("Min Overlap Fraction")), QStringLiteral("0.05"));
    EXPECT_EQ(stated(QStringLiteral("Plate-Solve Final Master If Needed")), QStringLiteral("Yes"));
}

// A feature that is off is stated as off, and nothing is said of the settings only it would read.
TEST(ReportSettingsFacts, AFeaturesOwnSettingsAreStatedOnlyWhileItIsOn)
{
    auto scenario = built(ReportScenarioId::OneMonoMaster);
    ASSERT_TRUE(scenario.has_value());
    StackingSettings& stacking = scenario->manifest.integrationGroups.front().stacking;
    stacking.stackMethod = QStringLiteral("Average");
    stacking.weighting = QStringLiteral("Weighted FWHM");
    stacking.normalizationMode = NormalizationMode::Global;
    stacking.localWeighting.enabled = false;
    stacking.localWeighting.windowSize = 9;
    stacking.overlapNormalization.enabled = false;
    stacking.overlapNormalization.minOverlapPixels = 750;
    RegistrationSettings& registration = scenario->manifest.registrationGroups.front().settings;
    registration.chromaticAberration.enabled = false;
    registration.chromaticAberration.minMatchedStars = 9;

    const ReportModel model = buildScenarioModel(*scenario);

    const std::vector<ReportSettingsBlock>& ofTheStack = model.masters.front().settings;
    const ReportSettingsBlock* stackingRows = blockOf(ofTheStack, QStringLiteral("Stacking"));
    EXPECT_EQ(valueOf(ofTheStack, QStringLiteral("Stacking"), QStringLiteral("Per-Pixel Weighting")),
              QStringLiteral("No"));
    EXPECT_EQ(valueOf(ofTheStack, QStringLiteral("Stacking"), QStringLiteral("Overlap Normalization")),
              QStringLiteral("No"));
    for (const QString& name :
         {QStringLiteral("Per-Pixel Weighting Window Size"), QStringLiteral("Per-Pixel Weighting Grid Spacing"),
          QStringLiteral("Per-Pixel Weighting SNR Exponent"), QStringLiteral("Min Overlap Pixels"),
          QStringLiteral("Min Overlap Fraction")})
    {
        EXPECT_EQ(rowOf(stackingRows, name), nullptr) << name.toStdString();
    }

    const std::vector<ReportSettingsBlock>& ofTheRegistration = model.registrationGroups.front().settings;
    const ReportSettingsBlock* registrationRows = blockOf(ofTheRegistration, QStringLiteral("Registration"));
    EXPECT_EQ(valueOf(ofTheRegistration, QStringLiteral("Registration"),
                      QStringLiteral("Correct Chromatic Aberration Before Registration")),
              QStringLiteral("No"));
    for (const QString& name : {QStringLiteral("CA Edge Fraction"), QStringLiteral("CA Fit Type"),
                                QStringLiteral("CA Min Matched Stars"), QStringLiteral("CA Interpolation")})
    {
        EXPECT_EQ(rowOf(registrationRows, name), nullptr) << name.toStdString();
    }
}

// A stack reads none of them outside the mode each belongs to, and a calibration master's stack reads none at all.
TEST(ReportSettingsFacts, WhatAStackDoesNotReadInItsModeIsNotStated)
{
    auto scenario = built(ReportScenarioId::OneMonoMaster);
    ASSERT_TRUE(scenario.has_value());
    WorkflowManifest::IntegrationGroup& group = scenario->manifest.integrationGroups.front();
    StackingSettings& stacking = group.stacking;
    stacking.stackMethod = QStringLiteral("Average");
    stacking.rejectionMethod = QStringLiteral("Winsorized Sigma Clipping");
    stacking.weighting = QStringLiteral("Weighted FWHM");
    stacking.normalizationMode = NormalizationMode::Global;
    stacking.createQualityMaps = true;
    stacking.spatialRejectionClustering = true;
    stacking.restackExcludingFlaggedFrames = true;
    stacking.localWeighting.enabled = true;
    stacking.overlapNormalization.enabled = true;
    stacking.solveFinalMasterIfNeeded = true;

    const QString perPixel = QStringLiteral("Per-Pixel Weighting");
    const QString clusters = QStringLiteral("Analyze Spatial Rejection Clusters");
    const QString restack = QStringLiteral("Re-Stack Excluding Flagged Frames");
    const QString qualityMaps = QStringLiteral("Create Quality Maps");
    const QString overlap = QStringLiteral("Overlap Normalization");
    const QString finalSolve = QStringLiteral("Plate-Solve Final Master If Needed");
    // The names of the rows the group's stacking states, as the manifest stands.
    const auto stackingRows = [&scenario]() {
        const ReportModel model = buildScenarioModel(*scenario);
        const ReportSettingsBlock* block = blockOf(model.masters.front().settings, QStringLiteral("Stacking"));
        QStringList names;
        if (block != nullptr)
        {
            for (const ReportSettingsRow& row : block->rows)
            {
                names << row.name;
            }
        }
        return names;
    };

    // An average that rejects, weighs its lights and matches them globally reads every one of them.
    const QStringList all = stackingRows();
    for (const QString& name : {perPixel, clusters, restack, qualityMaps, overlap, finalSolve})
    {
        EXPECT_TRUE(all.contains(name)) << name.toStdString();
    }

    // With no frame weighting, no light's weight varies over its frame.
    stacking.weighting = QStringLiteral("None");
    EXPECT_FALSE(stackingRows().contains(perPixel));
    stacking.weighting = QStringLiteral("Weighted FWHM");

    // A stack that rejects nothing has nothing to analyse.
    stacking.rejectionMethod = QStringLiteral("None");
    const QStringList unrejected = stackingRows();
    EXPECT_FALSE(unrejected.contains(clusters));
    EXPECT_FALSE(unrejected.contains(restack));
    EXPECT_TRUE(unrejected.contains(qualityMaps));
    stacking.rejectionMethod = QStringLiteral("Winsorized Sigma Clipping");

    // A drizzled stack is analysed, and is not stacked again.
    group.drizzle = WorkflowManifest::IntegrationGroup::DrizzleSettings{};
    group.drizzle->enabled = true;
    const QStringList drizzled = stackingRows();
    EXPECT_TRUE(drizzled.contains(clusters));
    EXPECT_FALSE(drizzled.contains(restack));
    group.drizzle.reset();

    // The lights are matched over the areas they share in a global normalization alone.
    stacking.normalizationMode = NormalizationMode::Local;
    stacking.localNormalization.enabled = true;
    EXPECT_FALSE(stackingRows().contains(overlap));
    stacking.localNormalization.enabled = false;
    stacking.normalizationMode = NormalizationMode::None;
    EXPECT_FALSE(stackingRows().contains(overlap));
    stacking.normalizationMode = NormalizationMode::Global;

    // A merge of exposures stacks each exposure on its own, and its master is solved as any other.
    stacking.hdr.enabled = true;
    const QStringList merged = stackingRows();
    for (const QString& name : {perPixel, clusters, restack, qualityMaps, overlap})
    {
        EXPECT_FALSE(merged.contains(name)) << name.toStdString();
    }
    EXPECT_TRUE(merged.contains(finalSolve));

    // A calibration master's stack reads none of them, whatever its settings hold.
    auto calibrated = built(ReportScenarioId::OneColourMaster);
    ASSERT_TRUE(calibrated.has_value());
    for (std::optional<WorkflowManifest::CalibrationStage>* stage :
         {&calibrated->manifest.biasStage, &calibrated->manifest.darkStage, &calibrated->manifest.flatStage})
    {
        ASSERT_TRUE(stage->has_value());
        for (WorkflowManifest::CalibrationStage::Group& calibration : (*stage)->groups)
        {
            calibration.stacking.weighting = QStringLiteral("Weighted FWHM");
            calibration.stacking.localWeighting.enabled = true;
            calibration.stacking.spatialRejectionClustering = true;
            calibration.stacking.restackExcludingFlaggedFrames = true;
            calibration.stacking.createQualityMaps = true;
            calibration.stacking.overlapNormalization.enabled = true;
            calibration.stacking.solveFinalMasterIfNeeded = true;
        }
    }
    const ReportModel calibratedModel = buildScenarioModel(*calibrated);
    ASSERT_FALSE(calibratedModel.calibrationMasters.empty());
    for (const ReportCalibrationMaster& master : calibratedModel.calibrationMasters)
    {
        SCOPED_TRACE(master.name.toStdString());
        const ReportSettingsBlock* block = blockOf(master.settings, QStringLiteral("Stacking"));
        ASSERT_NE(block, nullptr);
        for (const QString& name : {perPixel, clusters, restack, qualityMaps, overlap, finalSolve})
        {
            EXPECT_EQ(rowOf(block, name), nullptr) << name.toStdString();
        }
    }
}

// What a group asks of the plate solver and of the catalog, and the correction of its lights' colour fringes with the
// settings of that correction, are rows of its registration whatever its method.
TEST(ReportSettingsFacts, TheRegistrationsSettingsStateThePlateSolveTheCatalogAndTheChromaticCorrection)
{
    auto scenario = built(ReportScenarioId::OneMonoMaster);
    ASSERT_TRUE(scenario.has_value());
    RegistrationSettings& settings = scenario->manifest.registrationGroups.front().settings;
    settings.autoSolveUnsolvedFrames = true;
    settings.enrichWithGaiaCatalog = false;
    settings.chromaticAberration.enabled = true;
    settings.chromaticAberration.edgeFraction = 0.75;
    settings.chromaticAberration.transformType = QStringLiteral("Affine");
    settings.chromaticAberration.minMatchedStars = 9;
    settings.chromaticAberration.interpolation = QStringLiteral("Bicubic");

    const ReportModel model = buildScenarioModel(*scenario);
    const std::vector<ReportSettingsBlock>& blocks = model.registrationGroups.front().settings;
    const auto stated = [&blocks](const QString& name) {
        return valueOf(blocks, QStringLiteral("Registration"), name);
    };

    EXPECT_EQ(stated(QStringLiteral("Auto-Solve Unsolved Frames During Registration")), QStringLiteral("Yes"));
    EXPECT_EQ(stated(QStringLiteral("Enrich With Gaia Catalog")), QStringLiteral("No"));
    EXPECT_EQ(stated(QStringLiteral("Correct Chromatic Aberration Before Registration")), QStringLiteral("Yes"));
    EXPECT_EQ(stated(QStringLiteral("CA Edge Fraction")), QStringLiteral("0.75"));
    EXPECT_EQ(stated(QStringLiteral("CA Fit Type")), QStringLiteral("Affine"));
    EXPECT_EQ(stated(QStringLiteral("CA Min Matched Stars")), QStringLiteral("9"));
    EXPECT_EQ(stated(QStringLiteral("CA Interpolation")), QStringLiteral("Bicubic"));

    // A group tracked on a comet states them as a group matched by its stars does.
    const auto comet = built(ReportScenarioId::CometRun);
    ASSERT_TRUE(comet.has_value());
    const ReportModel cometModel = buildScenarioModel(*comet);
    const std::vector<ReportSettingsBlock>& cometBlocks = cometModel.registrationGroups.front().settings;
    EXPECT_EQ(valueOf(cometBlocks, QStringLiteral("Registration"),
                      QStringLiteral("Auto-Solve Unsolved Frames During Registration")),
              QStringLiteral("No"));
    EXPECT_EQ(valueOf(cometBlocks, QStringLiteral("Registration"), QStringLiteral("Enrich With Gaia Catalog")),
              QStringLiteral("Yes"));
    EXPECT_EQ(valueOf(cometBlocks, QStringLiteral("Registration"),
                      QStringLiteral("Correct Chromatic Aberration Before Registration")),
              QStringLiteral("No"));
}

// A reference the application chose is stated as the light the registration's own records name. Where the run kept
// no such record, the row can only say that the choice was left to the application.
TEST(ReportSettingsFacts, AReferenceTheApplicationChoseIsStatedAsTheLightTheRegistrationRecorded)
{
    const auto scenario = built(ReportScenarioId::FiveFilters);
    ASSERT_TRUE(scenario.has_value());

    const ReportModel model = buildScenarioModel(*scenario);

    ASSERT_EQ(model.registrationGroups.size(), scenario->manifest.registrationGroups.size());
    for (std::size_t i = 0; i < model.registrationGroups.size(); ++i)
    {
        const ReportRegistrationGroup& group = model.registrationGroups[i];
        const WorkflowManifest::RegistrationGroup& planned = scenario->manifest.registrationGroups[i];
        SCOPED_TRACE(group.name.toStdString());
        ASSERT_FALSE(planned.referenceFrame.has_value()) << "the manifest names no reference";
        ASSERT_FALSE(planned.lightFiles.empty());
        const ReportSettingsRow* row =
            rowOf(blockOf(group.settings, QStringLiteral("Registration")), QStringLiteral("Reference Frame"));
        ASSERT_NE(row, nullptr);
        // The registration's own record names it: the group's first light.
        EXPECT_EQ(row->value, QFileInfo(planned.lightFiles.front().canonicalPath()).fileName());
        EXPECT_EQ(row->value, group.reference) << "the group states the same light as its reference";
        EXPECT_EQ(row->requested, QStringLiteral("Automatic"));
        EXPECT_FALSE(row->reason.isEmpty());
    }
    // The decision says what the choice resolved to.
    const ReportDecision* decision = decisionOf(model, QStringLiteral("Registration: Reference Frame"));
    ASSERT_NE(decision, nullptr);
    EXPECT_EQ(decision->groupKey, model.registrationGroups.front().key);
    EXPECT_EQ(decision->resolved, model.registrationGroups.front().reference);

    // Without the registration's records no light is known.
    const auto unrecorded = built(ReportScenarioId::FiveFiltersWithoutMetrics);
    ASSERT_TRUE(unrecorded.has_value());
    const ReportModel unknown = buildScenarioModel(*unrecorded);
    ASSERT_FALSE(unknown.registrationGroups.empty());
    const ReportSettingsRow* left =
        rowOf(blockOf(unknown.registrationGroups.front().settings, QStringLiteral("Registration")),
              QStringLiteral("Reference Frame"));
    ASSERT_NE(left, nullptr);
    EXPECT_EQ(left->value, QStringLiteral("Automatic"));
    EXPECT_EQ(left->requested, QStringLiteral("Automatic"));
}
