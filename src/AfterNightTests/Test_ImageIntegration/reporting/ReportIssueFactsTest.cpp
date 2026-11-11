// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The issues a report model states: each kind of the catalog computed from the run's records and raised by the
/// runs that call for it and by no other, then every warning and error of the run in the run's own words.

#include <gtest/gtest.h>

#include "Test_ImageIntegration/reporting/ReportModelTestSupport.h"

#include <QSet>

#include <algorithm>
#include <map>
#include <optional>
#include <set>

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

int countOfKind(const ReportModel& model, const ReportIssueKind kind)
{
    return static_cast<int>(std::count_if(model.issues.begin(), model.issues.end(),
                                          [kind](const ReportIssue& issue) { return issue.kind == kind; }));
}

const ReportIssue* firstOfKind(const ReportModel& model, const ReportIssueKind kind)
{
    const auto found = std::find_if(model.issues.begin(), model.issues.end(),
                                    [kind](const ReportIssue& issue) { return issue.kind == kind; });
    return found == model.issues.end() ? nullptr : &*found;
}

/// The kinds a model raises, the run's own messages left out.
std::set<ReportIssueKind> computedKinds(const ReportModel& model)
{
    std::set<ReportIssueKind> kinds;
    for (const ReportIssue& issue : model.issues)
    {
        if (issue.kind != ReportIssueKind::RunMessage)
        {
            kinds.insert(issue.kind);
        }
    }
    return kinds;
}
} // namespace

TEST(ReportIssueFacts, EveryScenarioRaisesWhatItsRecordsCallFor)
{
    for (const ReportScenarioId id : allReportScenarios())
    {
        SCOPED_TRACE(scenarioName(id));
        const auto scenario = built(id);
        ASSERT_TRUE(scenario.has_value());
        const ReportScenarioFacts& facts = scenario->facts;

        const ReportModel model = buildScenarioModel(*scenario);

        EXPECT_EQ(countOfKind(model, ReportIssueKind::RunFailed),
                  !scenario->result.success && !scenario->result.cancelled ? 1 : 0);
        EXPECT_EQ(countOfKind(model, ReportIssueKind::RunCancelled), scenario->result.cancelled ? 1 : 0);
        EXPECT_EQ(countOfKind(model, ReportIssueKind::MasterFailed), facts.mastersFailed);
        EXPECT_EQ(countOfKind(model, ReportIssueKind::MasterNotReached), facts.mastersNotReached);
        EXPECT_EQ(countOfKind(model, ReportIssueKind::MasterReused), facts.mastersReused);
        EXPECT_EQ(countOfKind(model, ReportIssueKind::PreCalibratedLights),
                  static_cast<int>(scenario->manifest.preCalibratedLightGroups.size()));
        EXPECT_EQ(countOfKind(model, ReportIssueKind::RegistrationFailures) > 0, facts.registrationFailures > 0);
        EXPECT_EQ(countOfKind(model, ReportIssueKind::LightsLeftOutByStack) > 0, facts.lightsDroppedByStack > 0);
        EXPECT_EQ(countOfKind(model, ReportIssueKind::StackedTwice) > 0, facts.lightsExcludedOnRestack > 0);
        EXPECT_EQ(countOfKind(model, ReportIssueKind::RunMessage),
                  static_cast<int>(scenario->result.warnings.size() + scenario->result.errors.size()));

        for (const ReportIssue& issue : model.issues)
        {
            EXPECT_FALSE(issue.title.isEmpty());
            EXPECT_FALSE(issue.title.contains(QStringLiteral("%1"))) << issue.title.toStdString();
            EXPECT_FALSE(issue.detail.contains(QStringLiteral("%1"))) << issue.detail.toStdString();
        }
    }
}

TEST(ReportIssueFacts, TheKindsOfARunAreTheOnesItsRecordsCallForAndNoOther)
{
    using Kind = ReportIssueKind;
    const std::map<ReportScenarioId, std::set<Kind>> expected = {
        // Nothing to calibrate with, and a master saved without a solution.
        {ReportScenarioId::OneMonoMaster, {Kind::MasterWithoutAstrometry, Kind::NoFlats, Kind::NoDarks}},
        // A clean run.
        {ReportScenarioId::OneColourMaster, {}},
        // The bias is an earlier session's, and the hydrogen-alpha lights take no flat and are not solved.
        {ReportScenarioId::FiveFilters,
         {Kind::CalibrationMasterReused, Kind::LightsWithoutFlat, Kind::MasterWithoutAstrometry}},
        {ReportScenarioId::FailedInStacking,
         {Kind::RunFailed, Kind::MasterFailed, Kind::MasterNotReached, Kind::CalibrationMasterReused,
          Kind::LightsWithoutFlat}},
        {ReportScenarioId::CancelledAfterTwoMasters,
         {Kind::RunCancelled, Kind::MasterNotReached, Kind::CalibrationMasterReused, Kind::LightsWithoutFlat}},
        {ReportScenarioId::IncrementalRun,
         {Kind::MasterReused, Kind::CalibrationMasterReused, Kind::LightsWithoutFlat, Kind::MasterWithoutAstrometry}},
        {ReportScenarioId::PreCalibratedAndSuppliedDark,
         {Kind::PreCalibratedLights, Kind::NoFlats, Kind::MasterWithoutAstrometry}},
        {ReportScenarioId::TwoThousandLights,
         {Kind::RegistrationFailures, Kind::MasterWithoutAstrometry, Kind::NoFlats, Kind::NoDarks}},
        // Four of thirty-seven lights failed registration, one was dropped and two were flagged on a restack.
        {ReportScenarioId::EveryLightState,
         {Kind::RegistrationFailures, Kind::RegistrationSuccessLow, Kind::LightsLeftOutByStack, Kind::StackedTwice,
          Kind::MasterWithoutAstrometry, Kind::NoFlats, Kind::NoDarks}},
    };
    for (const auto& [id, kinds] : expected)
    {
        SCOPED_TRACE(scenarioName(id));
        const auto scenario = built(id);
        ASSERT_TRUE(scenario.has_value());

        const std::set<Kind> raised = computedKinds(buildScenarioModel(*scenario));

        for (const Kind kind : kinds)
        {
            EXPECT_TRUE(raised.contains(kind)) << "kind " << static_cast<int>(kind) << " is not raised";
        }
        for (const Kind kind : raised)
        {
            EXPECT_TRUE(kinds.contains(kind)) << "kind " << static_cast<int>(kind) << " is raised";
        }
    }
}

TEST(ReportIssueFacts, EveryMessageOfTheRunIsListedOnceAsWrittenAfterTheKinds)
{
    for (const ReportScenarioId id :
         {ReportScenarioId::EveryLightState, ReportScenarioId::FailedInStacking, ReportScenarioId::OneColourMaster})
    {
        SCOPED_TRACE(scenarioName(id));
        auto scenario = built(id);
        ASSERT_TRUE(scenario.has_value());
        scenario->result.warnings.append(QStringLiteral("A sentence of the run's own, with <markup> & a %1."));

        const ReportModel model = buildScenarioModel(*scenario);

        QStringList messages;
        bool messagesBegan = false;
        for (const ReportIssue& issue : model.issues)
        {
            if (issue.kind == ReportIssueKind::RunMessage)
            {
                messagesBegan = true;
                messages << issue.title;
                continue;
            }
            EXPECT_FALSE(messagesBegan) << "a computed kind follows the run's messages";
        }
        QStringList written = scenario->result.errors + scenario->result.warnings;
        EXPECT_EQ(messages, written) << "each message once, as written: the errors, then the warnings";
        for (const ReportIssue& issue : model.issues)
        {
            if (issue.kind == ReportIssueKind::RunMessage)
            {
                EXPECT_EQ(issue.severity, scenario->result.errors.contains(issue.title) ? ReportIssueSeverity::Critical
                                                                                        : ReportIssueSeverity::Warning);
            }
        }
    }
}

TEST(ReportIssueFacts, TheComputedKindsAreInOrderOfSeverity)
{
    for (const ReportScenarioId id : allReportScenarios())
    {
        SCOPED_TRACE(scenarioName(id));
        const auto scenario = built(id);
        ASSERT_TRUE(scenario.has_value());

        const ReportModel model = buildScenarioModel(*scenario);

        int last = 0;
        for (const ReportIssue& issue : model.issues)
        {
            if (issue.kind == ReportIssueKind::RunMessage)
            {
                break;
            }
            EXPECT_GE(static_cast<int>(issue.severity), last) << issue.title.toStdString();
            last = static_cast<int>(issue.severity);
        }
    }
}

TEST(ReportIssueFacts, ARunIsHeldToItsIssuesANoteAloneLeavesItClean)
{
    const auto clean = built(ReportScenarioId::OneColourMaster);
    ASSERT_TRUE(clean.has_value());
    EXPECT_EQ(buildScenarioModel(*clean).run.outcome, ReportOutcome::Completed);

    // A master with no solution is a warning the run itself never wrote.
    const auto unsolved = built(ReportScenarioId::OneMonoMaster);
    ASSERT_TRUE(unsolved.has_value());
    ASSERT_TRUE(unsolved->result.warnings.isEmpty());
    EXPECT_EQ(buildScenarioModel(*unsolved).run.outcome, ReportOutcome::CompletedWithWarnings);

    // Notes alone: nothing to calibrate with is a choice, not a warning.
    auto solved = built(ReportScenarioId::OneMonoMaster);
    ASSERT_TRUE(solved.has_value());
    solved->result.integrationGroupResults.front().masterSolution = MasterSolutionOutcome::Solved;
    const ReportModel noted = buildScenarioModel(*solved);
    EXPECT_GT(countOfKind(noted, ReportIssueKind::NoFlats), 0);
    EXPECT_EQ(noted.run.outcome, ReportOutcome::Completed);

    // An error the run wrote makes a run that completed partial.
    solved->result.errors.append(QStringLiteral("The preview of the master could not be written."));
    const ReportModel withError = buildScenarioModel(*solved);
    EXPECT_EQ(withError.run.outcome, ReportOutcome::Partial);
    EXPECT_FALSE(withError.run.verdict.isEmpty());
}

TEST(ReportIssueFacts, ARunThatStoppedSaysWhatWasKeptAndWhatANewRunMakes)
{
    const auto failed = built(ReportScenarioId::FailedInStacking);
    ASSERT_TRUE(failed.has_value());
    const ReportModel failedModel = buildScenarioModel(*failed);
    const ReportIssue* runFailed = firstOfKind(failedModel, ReportIssueKind::RunFailed);
    ASSERT_NE(runFailed, nullptr);
    EXPECT_EQ(runFailed, &failedModel.issues.front()) << "how the run ended leads";
    EXPECT_EQ(runFailed->severity, ReportIssueSeverity::Critical);
    EXPECT_EQ(runFailed->detail, failed->result.errorMessage);
    EXPECT_TRUE(runFailed->action.contains(QString::number(failed->facts.mastersMade)));
    EXPECT_TRUE(
        runFailed->action.contains(QString::number(failed->facts.mastersFailed + failed->facts.mastersNotReached)));

    const ReportIssue* masterFailed = firstOfKind(failedModel, ReportIssueKind::MasterFailed);
    ASSERT_NE(masterFailed, nullptr);
    EXPECT_TRUE(masterFailed->detail.contains(QStringLiteral("Final Master Save")))
        << "the phase the stack failed in: " << masterFailed->detail.toStdString();
    EXPECT_TRUE(masterFailed->anchor.startsWith(QStringLiteral("file-")));
    EXPECT_FALSE(masterFailed->groupKey.isEmpty());

    const auto cancelled = built(ReportScenarioId::CancelledAfterTwoMasters);
    ASSERT_TRUE(cancelled.has_value());
    const ReportModel cancelledModel = buildScenarioModel(*cancelled);
    ASSERT_NE(firstOfKind(cancelledModel, ReportIssueKind::RunCancelled), nullptr);
    EXPECT_EQ(firstOfKind(cancelledModel, ReportIssueKind::RunFailed), nullptr);
    EXPECT_EQ(cancelledModel.run.outcome, ReportOutcome::Cancelled);
}

TEST(ReportIssueFacts, FewLightsCombinedIsWarnedAboutAtTwoThresholds)
{
    auto scenario = built(ReportScenarioId::OneMonoMaster);
    ASSERT_TRUE(scenario.has_value());
    IntegrationGroupResult& result = scenario->result.integrationGroupResults.front();
    StackQualityRecord& quality = result.qualityReport;
    ASSERT_EQ(quality.lightWeights.size(), 18u);
    EXPECT_EQ(countOfKind(buildScenarioModel(*scenario), ReportIssueKind::LowFrameUse), 0);

    // Thirteen of eighteen: under four in five.
    const auto keep = [&](const std::size_t lights) {
        quality.lightWeights.resize(lights);
        if (!quality.frameRejectionRates.empty())
        {
            quality.frameRejectionRates.resize(lights);
        }
        result.lightFrameCount = static_cast<int>(lights);
    };
    keep(13u);
    const ReportModel some = buildScenarioModel(*scenario);
    const ReportIssue* low = firstOfKind(some, ReportIssueKind::LowFrameUse);
    ASSERT_NE(low, nullptr);
    EXPECT_EQ(low->severity, ReportIssueSeverity::Warning);
    EXPECT_TRUE(low->title.contains(QStringLiteral("four in five"))) << low->title.toStdString();
    EXPECT_TRUE(low->detail.contains(QStringLiteral("13")) && low->detail.contains(QStringLiteral("18")));

    // Ten of eighteen: under three in five.
    keep(10u);
    const ReportModel few = buildScenarioModel(*scenario);
    low = firstOfKind(few, ReportIssueKind::LowFrameUse);
    ASSERT_NE(low, nullptr);
    EXPECT_TRUE(low->title.contains(QStringLiteral("three in five"))) << low->title.toStdString();
}

TEST(ReportIssueFacts, LightsThatStateNoExposureAreWarnedAbout)
{
    auto scenario = built(ReportScenarioId::OneMonoMaster);
    ASSERT_TRUE(scenario.has_value());
    WorkflowManifest::IntegrationGroup& group = scenario->manifest.integrationGroups.front();
    EXPECT_EQ(countOfKind(buildScenarioModel(*scenario), ReportIssueKind::ExposureMetadataIncomplete), 0);

    // Two lights whose names state no exposure, in a group that states none either.
    group.exposure = QStringLiteral("-");
    for (std::size_t light = 0; light < 2u; ++light)
    {
        group.lightFiles[light] = FrameId(QStringLiteral("C:/lights/unnamed_%1.fits").arg(light));
        group.lightExposureSeconds[light] = 0.0;
    }

    const ReportModel model = buildScenarioModel(*scenario);

    const ReportIssue* incomplete = firstOfKind(model, ReportIssueKind::ExposureMetadataIncomplete);
    ASSERT_NE(incomplete, nullptr);
    EXPECT_TRUE(incomplete->detail.contains(QStringLiteral("16")) && incomplete->detail.contains(QStringLiteral("18")))
        << incomplete->detail.toStdString();
}

TEST(ReportIssueFacts, AMasterTheArchiveKeptNoCopyOfIsAnIssueOfItsReason)
{
    // The history had no room: a note.
    auto withoutPayloads = built(ReportScenarioId::FiveFiltersWithoutPayloads);
    ASSERT_TRUE(withoutPayloads.has_value());
    ASSERT_TRUE(archiveScenarioRun(*withoutPayloads));
    const ReportModel byPolicy = buildScenarioModel(*withoutPayloads, withoutPayloads->runDirectory);
    EXPECT_EQ(countOfKind(byPolicy, ReportIssueKind::OutputNotRetainedByPolicy), withoutPayloads->facts.mastersMade);
    EXPECT_EQ(countOfKind(byPolicy, ReportIssueKind::OutputNotRetained), 0);
    EXPECT_EQ(firstOfKind(byPolicy, ReportIssueKind::OutputNotRetainedByPolicy)->severity, ReportIssueSeverity::Note);

    // The copy could not be written: a warning, with the reason.
    auto failedCopy = built(ReportScenarioId::OneColourMaster);
    ASSERT_TRUE(failedCopy.has_value());
    IntegrationGroupResult& result = failedCopy->result.integrationGroupResults.front();
    result.outputRetentionStatus = OutputRetentionStatus::NotRetainedByArchiveFailure;
    result.outputRetentionReason = QStringLiteral("The disk is full.");
    const ReportModel byFailure = buildScenarioModel(*failedCopy);
    const ReportIssue* notRetained = firstOfKind(byFailure, ReportIssueKind::OutputNotRetained);
    ASSERT_NE(notRetained, nullptr);
    EXPECT_EQ(notRetained->severity, ReportIssueSeverity::Warning);
    EXPECT_EQ(notRetained->detail, QStringLiteral("The disk is full."));
    EXPECT_EQ(byFailure.run.outcome, ReportOutcome::CompletedWithWarnings);
}

TEST(ReportIssueFacts, TheProductsTwoCalibrationWarningsAreKindsOfTheirOwn)
{
    // A flat stacked from frames that take neither a bias nor a dark, and cosmetic correction with no dark to find
    // defects in: the run writes a sentence for each, and the model raises each as a kind from the manifest.
    auto scenario = built(ReportScenarioId::OneColourMaster);
    ASSERT_TRUE(scenario.has_value());
    ASSERT_TRUE(scenario->manifest.flatStage.has_value());
    scenario->manifest.flatStage->groups.front().biasMaster.reset();
    for (WorkflowManifest::LightsCalibrationParams& calibration : scenario->manifest.lightsCalibration)
    {
        calibration.darkPath.reset();
    }

    const ReportModel model = buildScenarioModel(*scenario);

    const ReportIssue* flat = firstOfKind(model, ReportIssueKind::FlatMasterUncalibrated);
    ASSERT_NE(flat, nullptr);
    EXPECT_EQ(flat->severity, ReportIssueSeverity::Warning);
    EXPECT_EQ(countOfKind(model, ReportIssueKind::CosmeticCorrectionWithoutDark), 1) << "once for the group";
    const ReportIssue* cosmetic = firstOfKind(model, ReportIssueKind::CosmeticCorrectionWithoutDark);
    EXPECT_EQ(cosmetic->groupKey, model.masters.front().key);
}
