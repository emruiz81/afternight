// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The report model of a run: what `buildReportModel` states of the run as a whole, of each master, of the
/// calibration and of the registration, held to the runs the scenarios build. Each case reads the model, never a
/// document written from it.

#include <gtest/gtest.h>

#include "Test_ImageIntegration/reporting/ReportModelTestSupport.h"

#include "processes/ImageIntegration/reporting/ReportFormat.h"
#include "processes/ImageIntegration/workflow/WorkflowExecutionCounters.h"

#include <AfterNightEngine/Core/Version.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <numeric>
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

int countOfState(const ReportModel& model, const ReportMasterState state)
{
    return static_cast<int>(std::count_if(model.masters.begin(), model.masters.end(),
                                          [state](const ReportMaster& master) { return master.state == state; }));
}

const ReportCalibrationMaster* calibrationOfKind(const ReportModel& model, const ReportCalibrationKind kind)
{
    const auto found = std::find_if(model.calibrationMasters.begin(), model.calibrationMasters.end(),
                                    [kind](const ReportCalibrationMaster& master) { return master.kind == kind; });
    return found == model.calibrationMasters.end() ? nullptr : &*found;
}

/// A host that states a fixed time, zone and platform.
class FixedHost final : public IReportHost
{
public:
    [[nodiscard]] QDateTime now() override
    {
        return QDateTime(QDate(2026, 11, 10), QTime(11, 15), QTimeZone::UTC);
    }

    [[nodiscard]] QString timeZoneId() override
    {
        return QStringLiteral("Etc/UTC");
    }

    [[nodiscard]] QString platform() override
    {
        return QStringLiteral("A test host (x86_64)");
    }
};

} // namespace

// ---- The run: identity and provenance.

TEST(ReportModelBuilder, TheRunStatesItsIdentityAndWhereItCameFrom)
{
    const auto scenario = built(ReportScenarioId::FiveFilters);
    ASSERT_TRUE(scenario.has_value());

    const ReportModel model = buildScenarioModel(*scenario);
    const ReportRun& run = model.run;

    EXPECT_EQ(model.reportSchema, kReportSchema);
    EXPECT_EQ(run.executionId, scenario->manifest.executionId);
    EXPECT_EQ(run.applicationVersion, scenario->context.value(QStringLiteral("appVersion")).toString());
    EXPECT_EQ(run.timeZoneId, scenario->context.value(QStringLiteral("timeZoneId")).toString());
    EXPECT_EQ(run.utcOffsetSeconds, scenario->context.value(QStringLiteral("utcOffsetSeconds")).toInt());
    EXPECT_EQ(run.archiveFormat, scenario->context.value(QStringLiteral("schemaVersion")).toInt());
    EXPECT_EQ(run.manifestFormat, scenario->manifest.version);
    EXPECT_EQ(run.outputFolder, scenario->outputDirectory);
    EXPECT_EQ(run.process, QStringLiteral("Image Integration"));
    EXPECT_FALSE(run.title.isEmpty());
    EXPECT_FALSE(run.platform.isEmpty());
    EXPECT_FALSE(run.verdict.isEmpty());

    // The archive writes its local time without an offset and the offset beside it: the model holds the instant.
    ASSERT_TRUE(run.archivedAt.has_value());
    EXPECT_EQ(run.archivedAt->offsetFromUtc(), run.utcOffsetSeconds);
    EXPECT_EQ(run.archivedAt->toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss.zzz")),
              scenario->context.value(QStringLiteral("archivedAtLocal")).toString());

    static const QRegularExpression hash(QStringLiteral("^[0-9a-f]{64}$"));
    EXPECT_TRUE(hash.match(run.fingerprint).hasMatch()) << run.fingerprint.toStdString();
    EXPECT_TRUE(hash.match(run.settingsFingerprint).hasMatch());
    EXPECT_TRUE(hash.match(run.inputsFingerprint).hasMatch());
    EXPECT_NE(run.fingerprint, run.settingsFingerprint);
    EXPECT_NE(run.settingsFingerprint, run.inputsFingerprint);
}

TEST(ReportModelBuilder, TheVersionIsTheEnginesWhenTheArchiveStatesNone)
{
    auto scenario = built(ReportScenarioId::OneMonoMaster);
    ASSERT_TRUE(scenario.has_value());
    scenario->context.remove(QStringLiteral("appVersion"));

    EXPECT_EQ(buildScenarioModel(*scenario).run.applicationVersion, QString::fromUtf8(an::engineVersionString()));
}

TEST(ReportModelBuilder, TheFingerprintIsTheRunsOwnAndChangesWithItsInputs)
{
    auto scenario = built(ReportScenarioId::OneMonoMaster);
    ASSERT_TRUE(scenario.has_value());
    const ReportRun first = buildScenarioModel(*scenario).run;
    const ReportRun again = buildScenarioModel(*scenario).run;
    EXPECT_EQ(first.fingerprint, again.fingerprint);

    ASSERT_GT(scenario->manifest.integrationGroups.front().lightFiles.size(), 1u);
    scenario->manifest.integrationGroups.front().lightFiles.pop_back();
    scenario->manifest.integrationGroups.front().lightExposureSeconds.pop_back();
    const ReportRun fewer = buildScenarioModel(*scenario).run;

    EXPECT_NE(fewer.inputsFingerprint, first.inputsFingerprint);
    EXPECT_NE(fewer.fingerprint, first.fingerprint);
}

TEST(ReportModelBuilder, ARunOfTheLiteFrontEndStatesItsProcess)
{
    const auto scenario = built(ReportScenarioId::LiteFrontEnd);
    ASSERT_TRUE(scenario.has_value());

    const ReportRun run = buildScenarioModel(*scenario).run;

    EXPECT_EQ(run.process, QStringLiteral("Image Integration Lite"));
    EXPECT_TRUE(run.title.contains(QStringLiteral("Image Integration Lite")));
}

TEST(ReportModelBuilder, TheProfileIsStatedByItsLabelAndItsId)
{
    auto scenario = built(ReportScenarioId::OneMonoMaster);
    ASSERT_TRUE(scenario.has_value());
    scenario->manifest.processingProfileLabel = QStringLiteral(" Deep sky ");
    scenario->manifest.processingProfileId = QStringLiteral("deep_sky");

    const ReportRun run = buildScenarioModel(*scenario).run;

    EXPECT_EQ(run.profile, QStringLiteral("Deep sky"));
    EXPECT_EQ(run.profileId, QStringLiteral("deep_sky"));
}

TEST(ReportModelBuilder, TheSessionsNotesAreStatedWithWhenTheyWereLastChanged)
{
    const auto scenario = built(ReportScenarioId::FiveFilters);
    ASSERT_TRUE(scenario.has_value());
    const QString notesPath =
        QDir(scenario->outputDirectory).filePath(QStringLiteral(".afternight/image_integration/session_notes.txt"));
    QFile notes(notesPath);
    ASSERT_TRUE(notes.open(QIODevice::ReadOnly));
    const QString written = QString::fromUtf8(notes.readAll()).trimmed();
    ASSERT_FALSE(written.isEmpty());

    const ReportRun run = buildScenarioModel(*scenario).run;

    EXPECT_EQ(run.sessionNotes, written);
    ASSERT_TRUE(run.sessionNotesChangedAt.has_value());
    EXPECT_EQ(*run.sessionNotesChangedAt, QFileInfo(notesPath).lastModified());

    // A run with no notes states none, and no time for them.
    const auto plain = built(ReportScenarioId::OneMonoMaster);
    ASSERT_TRUE(plain.has_value());
    if (!QFileInfo::exists(
            QDir(plain->outputDirectory).filePath(QStringLiteral(".afternight/image_integration/session_notes.txt"))))
    {
        const ReportRun without = buildScenarioModel(*plain).run;
        EXPECT_TRUE(without.sessionNotes.isEmpty());
        EXPECT_FALSE(without.sessionNotesChangedAt.has_value());
    }
}

TEST(ReportModelBuilder, WhatTheHostStatesComesThroughItsSource)
{
    auto scenario = built(ReportScenarioId::OneMonoMaster);
    ASSERT_TRUE(scenario.has_value());
    scenario->context.remove(QStringLiteral("archivedAtLocal"));
    scenario->context.remove(QStringLiteral("timeZoneId"));
    scenario->context.remove(QStringLiteral("utcOffsetSeconds"));
    const ProductionReportSources production(scenario->session(), scenario->metricsDatabasePath);
    ReportSources sources = production.sources();
    FixedHost host;
    sources.host = &host;

    const ReportRun run = buildReportModel(ReportModelInputs{.manifest = scenario->manifest,
                                                             .result = scenario->result,
                                                             .runDirectory = {},
                                                             .context = scenario->context,
                                                             .counters = std::nullopt},
                                           sources)
                              .run;

    EXPECT_EQ(run.platform, QStringLiteral("A test host (x86_64)"));
    EXPECT_EQ(run.timeZoneId, QStringLiteral("Etc/UTC"));
    ASSERT_TRUE(run.archivedAt.has_value());
    EXPECT_EQ(*run.archivedAt, host.now());
}

TEST(ReportModelBuilder, NoFigureIsTakenFromTheLog)
{
    for (const ReportScenarioId id : allReportScenarios())
    {
        SCOPED_TRACE(scenarioName(id));
        auto scenario = built(id);
        ASSERT_TRUE(scenario.has_value());
        ReportModel withLog = buildScenarioModel(*scenario);
        EXPECT_EQ(withLog.run.log, scenario->result.capturedLog);

        // A log that says something else about every figure the old report read from it.
        scenario->result.capturedLog =
            QStringLiteral("[2026-11-10T11:15:00.000] [HEADER] \u2550\u2550\u2550 STAGE: Lights Stacking (99 groups) "
                           "\u2550\u2550\u2550\nInputs: Lights: 999 | Effective: 998 | Registered: 997\n");
        ReportModel withAnotherLog = buildScenarioModel(*scenario);
        scenario->result.capturedLog.clear();
        ReportModel withoutLog = buildScenarioModel(*scenario);

        withLog.run.log.clear();
        withAnotherLog.run.log.clear();
        EXPECT_TRUE(withLog == withoutLog) << "the model changes with the run's log";
        EXPECT_TRUE(withAnotherLog == withoutLog) << "the model reads a figure out of the run's log";
    }
}

// ---- The run: how it ended.

TEST(ReportModelBuilder, ACleanRunCompletedAndARunThatWarnedSaysSo)
{
    const auto clean = built(ReportScenarioId::OneColourMaster);
    ASSERT_TRUE(clean.has_value());
    const ReportRun cleanRun = buildScenarioModel(*clean).run;
    EXPECT_EQ(cleanRun.outcome, ReportOutcome::Completed);
    EXPECT_EQ(cleanRun.warnings, 0);
    EXPECT_EQ(cleanRun.errors, 0);

    // The same run with a warning completed all the same, and says it warned.
    auto warned = clean;
    warned->result.warnings.append(QStringLiteral("Flat group has no bias or dark master assigned."));
    warned->result.warningCount = 1;
    const ReportRun warnedRun = buildScenarioModel(*warned).run;
    EXPECT_EQ(warnedRun.outcome, ReportOutcome::CompletedWithWarnings);
    EXPECT_EQ(warnedRun.warnings, 1);

    // A run counts its own warnings; where it counted none, the ones it lists are counted.
    warned->result.warningCount = 0;
    warned->result.warnings.append(QStringLiteral("A second warning."));
    EXPECT_EQ(buildScenarioModel(*warned).run.warnings, 2);
}

TEST(ReportModelBuilder, ARunThatFailedAfterSomeMastersIsPartialAndKeepsWhatItMade)
{
    const auto scenario = built(ReportScenarioId::FailedInStacking);
    ASSERT_TRUE(scenario.has_value());

    const ReportModel model = buildScenarioModel(*scenario);

    EXPECT_EQ(model.run.outcome, ReportOutcome::Partial);
    EXPECT_EQ(model.run.groups.made, 3);
    EXPECT_EQ(model.run.groups.failed, 1);
    EXPECT_EQ(model.run.groups.notReached, 1);
    EXPECT_EQ(model.run.groups.reused, 0);
    EXPECT_EQ(countOfState(model, ReportMasterState::NotReached), 1);
    EXPECT_EQ(countOfState(model, ReportMasterState::Cancelled), 0);
    EXPECT_EQ(model.run.errors, scenario->result.errors.size());
    EXPECT_GT(model.run.errors, 0);

    for (const ReportMaster& master : model.masters)
    {
        if (master.state == ReportMasterState::Made)
        {
            EXPECT_TRUE(master.statistics.has_value()) << master.name.toStdString();
        }
        else
        {
            // A master that was not made states no figure of a file it does not have.
            EXPECT_FALSE(master.statistics.has_value()) << master.name.toStdString();
            EXPECT_FALSE(master.stack.has_value());
            EXPECT_TRUE(master.preview.isEmpty());
        }
    }
}

TEST(ReportModelBuilder, ACancelledRunSaysWhichMastersItNeverReached)
{
    const auto scenario = built(ReportScenarioId::CancelledAfterTwoMasters);
    ASSERT_TRUE(scenario.has_value());

    const ReportModel model = buildScenarioModel(*scenario);

    EXPECT_EQ(model.run.outcome, ReportOutcome::Cancelled);
    EXPECT_EQ(model.run.groups.total, 5);
    EXPECT_EQ(model.run.groups.made, 2);
    EXPECT_EQ(model.run.groups.notReached, 3);
    EXPECT_EQ(countOfState(model, ReportMasterState::Cancelled), 3);
    EXPECT_EQ(countOfState(model, ReportMasterState::Reused), 0) << "a run that stopped reused nothing it can name";
}

TEST(ReportModelBuilder, ARunThatStoppedBeforeAnyMasterMadeNone)
{
    const auto failed = built(ReportScenarioId::FailedBeforeAnyMaster);
    ASSERT_TRUE(failed.has_value());
    const ReportModel failedModel = buildScenarioModel(*failed);
    EXPECT_EQ(failedModel.run.outcome, ReportOutcome::Failed);
    EXPECT_EQ(failedModel.run.groups.made, 0);

    const auto cancelled = built(ReportScenarioId::CancelledBeforeAnyMaster);
    ASSERT_TRUE(cancelled.has_value());
    const ReportModel cancelledModel = buildScenarioModel(*cancelled);
    EXPECT_EQ(cancelledModel.run.outcome, ReportOutcome::Cancelled);
    EXPECT_EQ(cancelledModel.run.groups.made, 0);
}

TEST(ReportModelBuilder, AMasterTheHistoryHadNoRoomForIsMadeAndSaysItsCopyWasNotKept)
{
    auto scenario = built(ReportScenarioId::FiveFiltersWithoutPayloads);
    ASSERT_TRUE(scenario.has_value());
    ASSERT_TRUE(archiveScenarioRun(*scenario));

    const ReportModel model = buildScenarioModel(*scenario, scenario->runDirectory);

    EXPECT_NE(model.run.outcome, ReportOutcome::Failed);
    EXPECT_NE(model.run.outcome, ReportOutcome::Partial);
    int notKept = 0;
    for (const ReportMaster& master : model.masters)
    {
        EXPECT_EQ(master.state, ReportMasterState::Made) << master.name.toStdString();
        if (master.retention == ReportRetention::NotRetainedByPolicy)
        {
            ++notKept;
            EXPECT_FALSE(master.retentionReason.isEmpty());
        }
    }
    EXPECT_GT(notKept, 0);
}

// ---- The run: its masters by what became of them.

TEST(ReportModelBuilder, EveryPlannedMasterMeetsTheFateItsScenarioStates)
{
    for (const ReportScenarioId id : allReportScenarios())
    {
        SCOPED_TRACE(scenarioName(id));
        const auto scenario = built(id);
        ASSERT_TRUE(scenario.has_value());
        const ReportScenarioFacts& facts = scenario->facts;

        const ReportModel model = buildScenarioModel(*scenario);

        EXPECT_EQ(model.run.groups.total, static_cast<int>(model.masters.size()));
        EXPECT_EQ(model.run.groups.total, facts.mastersPlanned);
        EXPECT_EQ(model.run.groups.made, facts.mastersMade);
        EXPECT_EQ(model.run.groups.failed, facts.mastersFailed);
        EXPECT_EQ(model.run.groups.reused, facts.mastersReused);
        EXPECT_EQ(model.run.groups.notReached, facts.mastersNotReached);
        EXPECT_EQ(model.registrationGroups.size(), static_cast<std::size_t>(facts.registrationGroups));
        EXPECT_EQ(model.calibrationMasters.size(), static_cast<std::size_t>(facts.calibrationMasters));
        EXPECT_EQ(model.run.frames + facts.lightsLeftOutByUser, facts.lightsTaken);
        EXPECT_EQ(static_cast<int>(model.run.nights.size()), facts.nights);

        // Every master has an anchor of its own.
        QSet<QString> slugs;
        for (const ReportMaster& master : model.masters)
        {
            EXPECT_FALSE(master.slug.isEmpty());
            EXPECT_FALSE(slugs.contains(master.slug)) << master.slug.toStdString();
            slugs.insert(master.slug);
            // A master is named as a person names it, never by the key's own spelling.
            EXPECT_FALSE(master.name.isEmpty());
            EXPECT_EQ(master.name, friendlyGroupKey(master.key));
            EXPECT_FALSE(master.name.contains(QLatin1Char('|'))) << master.name.toStdString();
        }
    }
}

TEST(ReportModelBuilder, AnIncrementalRunSaysWhatItMadeAndWhatWasStillValid)
{
    const auto scenario = built(ReportScenarioId::IncrementalRun);
    ASSERT_TRUE(scenario.has_value());

    const ReportModel model = buildScenarioModel(*scenario);

    EXPECT_TRUE(model.run.incremental);
    EXPECT_EQ(model.run.mode, QStringLiteral("Incremental"));
    EXPECT_EQ(model.run.changedGroups.size(), scenario->manifest.incrementalIntegrationGroups.size());
    EXPECT_EQ(model.run.groups.made, 2);
    EXPECT_EQ(model.run.groups.reused, 3);
    for (const ReportMaster& master : model.masters)
    {
        if (master.state != ReportMasterState::Reused)
        {
            continue;
        }
        // A reused master is a file that is there, and the run has no result to state a figure from.
        EXPECT_TRUE(QFileInfo::exists(master.file)) << master.file.toStdString();
        ASSERT_TRUE(master.bytes.has_value());
        EXPECT_EQ(*master.bytes, QFileInfo(master.file).size());
        EXPECT_FALSE(master.lightsCombined.has_value());
        EXPECT_FALSE(master.durationMs.has_value());
        EXPECT_FALSE(master.stack.has_value());
        EXPECT_GT(master.exposure.frames, 0) << "its lights are the manifest's";
    }
}

TEST(ReportModelBuilder, ARunInWhichEveryMasterWasStillValidMadeNone)
{
    const auto scenario = built(ReportScenarioId::EveryGroupReused);
    ASSERT_TRUE(scenario.has_value());

    const ReportModel model = buildScenarioModel(*scenario);

    EXPECT_EQ(model.run.groups.made, 0);
    EXPECT_EQ(model.run.groups.reused, model.run.groups.total);
    EXPECT_GT(model.run.groups.total, 0);
    // No stage ran: each is stated as the manifest lists it, with no time of its own.
    EXPECT_FALSE(model.stages.empty());
    for (const ReportStage& stage : model.stages)
    {
        EXPECT_EQ(stage.state, ReportStageState::Reused) << stage.name.toStdString();
        EXPECT_FALSE(stage.durationMs.has_value());
    }
    EXPECT_NE(model.run.outcome, ReportOutcome::Failed);
}

TEST(ReportModelBuilder, ARunOfAFullScopeSaysSoAndARunOfOneGroupNamesIt)
{
    auto scenario = built(ReportScenarioId::OneMonoMaster);
    ASSERT_TRUE(scenario.has_value());
    const ReportRun full = buildScenarioModel(*scenario).run;
    EXPECT_EQ(full.mode, QStringLiteral("Full"));
    EXPECT_EQ(full.scope, QStringLiteral("full"));
    EXPECT_TRUE(full.scopeKey.isEmpty());
    EXPECT_FALSE(full.incremental);

    const QString key = scenario->manifest.integrationGroups.front().groupKey.toString();
    scenario->manifest.executionScope = QStringLiteral("group");
    scenario->manifest.executionScopeKey = key;
    const ReportRun scoped = buildScenarioModel(*scenario).run;
    EXPECT_EQ(scoped.scope, QStringLiteral("group"));
    EXPECT_EQ(scoped.scopeKey, key);
    EXPECT_EQ(scoped.scopeName, friendlyGroupKey(key));
}

// ---- The run: stages, timings, counters.

TEST(ReportModelBuilder, TheStagesAreTheOnesTheRunTimed)
{
    const auto scenario = built(ReportScenarioId::FailedInStacking);
    ASSERT_TRUE(scenario.has_value());

    const ReportModel model = buildScenarioModel(*scenario);

    ASSERT_EQ(model.stages.size(), scenario->result.stageResults.size());
    bool anyFailed = false;
    for (std::size_t i = 0; i < model.stages.size(); ++i)
    {
        const auto& timed = scenario->result.stageResults[i];
        const ReportStage& stage = model.stages[i];
        EXPECT_EQ(stage.key, timed.stageName);
        EXPECT_FALSE(stage.name.isEmpty());
        ASSERT_TRUE(stage.durationMs.has_value());
        EXPECT_EQ(*stage.durationMs, timed.durationMs);
        EXPECT_EQ(stage.status, timed.status.trimmed().toLower());
        if (timed.status == QStringLiteral("failed"))
        {
            EXPECT_EQ(stage.state, ReportStageState::Failed);
            anyFailed = true;
        }
        else if (timed.status == QStringLiteral("completed"))
        {
            EXPECT_EQ(stage.state, ReportStageState::Ran);
        }
    }
    EXPECT_TRUE(anyFailed) << "the run failed in a stage";
}

TEST(ReportModelBuilder, AStageWorksOnTheGroupsOfItsKind)
{
    auto scenario = built(ReportScenarioId::FiveFilters);
    ASSERT_TRUE(scenario.has_value());
    scenario->result.stageResults.clear();
    // Only the stages the run timed: a stage the manifest lists and the run has no result for is stated too.
    scenario->manifest.stageSequence.clear();
    for (const QString& name :
         {QStringLiteral("BiasStacking"), QStringLiteral("DarkStacking"), QStringLiteral("FlatStacking"),
          QStringLiteral("LightsRegistration"), QStringLiteral("LightsStacking")})
    {
        WorkflowExecutor::Result::StageTiming timing;
        timing.stageName = name;
        timing.status = QStringLiteral("completed");
        scenario->result.stageResults.push_back(timing);
    }

    const ReportModel model = buildScenarioModel(*scenario);

    ASSERT_EQ(model.stages.size(), 5u);
    EXPECT_EQ(model.stages[0].groups, static_cast<int>(scenario->manifest.biasStage->groups.size()));
    EXPECT_EQ(model.stages[1].groups, static_cast<int>(scenario->manifest.darkStage->groups.size()));
    EXPECT_EQ(model.stages[2].groups, static_cast<int>(scenario->manifest.flatStage->groups.size()));
    EXPECT_EQ(model.stages[3].groups, static_cast<int>(scenario->manifest.registrationGroups.size()));
    EXPECT_EQ(model.stages[4].groups, static_cast<int>(scenario->manifest.integrationGroups.size()));
    EXPECT_EQ(model.stages[4].name, QStringLiteral("Lights Stacking"));
}

TEST(ReportModelBuilder, ThePhasesOfARunAreStatedOnlyWhenTheyWereTimed)
{
    auto scenario = built(ReportScenarioId::OneMonoMaster);
    ASSERT_TRUE(scenario.has_value());
    scenario->result.totalDurationMs = 5'000;
    scenario->result.preparationTiming = WorkflowPreparationTiming{};
    scenario->result.executorTiming = WorkflowExecutorTiming{};

    const ReportRun untimed = buildScenarioModel(*scenario).run;
    EXPECT_EQ(untimed.processingMs, 5'000);
    EXPECT_FALSE(untimed.preparation.has_value());
    EXPECT_FALSE(untimed.executor.has_value());

    // A phase that was timed and took no time is stated as zero, which is not the same as not timed.
    scenario->result.preparationTiming =
        WorkflowPreparationTiming{.captured = true, .manifestBuildMs = 0, .planAndValidationMs = 40};
    scenario->result.executorTiming = WorkflowExecutorTiming{
        .captured = true, .stageExecutionMs = 4'000, .stateCommitMs = 100, .archiveMs = 600, .reportMs = 300};
    const ReportRun timed = buildScenarioModel(*scenario).run;
    ASSERT_TRUE(timed.preparation.has_value());
    EXPECT_EQ(timed.preparation->manifestBuildMs, 0);
    EXPECT_EQ(timed.preparation->planAndValidationMs, 40);
    ASSERT_TRUE(timed.executor.has_value());
    EXPECT_EQ(timed.executor->stageExecutionMs, 4'000);
    EXPECT_EQ(timed.executor->stateCommitMs, 100);
    EXPECT_EQ(timed.executor->archiveMs, 600);
    EXPECT_EQ(timed.executor->reportMs, 300);
    // The executor's phases are inside its interval and are not added to it.
    EXPECT_EQ(timed.processingMs, 5'000);
}

TEST(ReportModelBuilder, CountersAreStatedOnlyWhenTheRunCollectedThem)
{
    auto scenario = built(ReportScenarioId::OneMonoMaster);
    ASSERT_TRUE(scenario.has_value());
    scenario->result.executionCounters.reset();
    const ProductionReportSources production(scenario->session(), scenario->metricsDatabasePath);
    const auto modelWith = [&](const std::optional<WorkflowExecutionCounters>& counters) {
        return buildReportModel(ReportModelInputs{.manifest = scenario->manifest,
                                                  .result = scenario->result,
                                                  .runDirectory = {},
                                                  .context = scenario->context,
                                                  .counters = counters},
                                production.sources());
    };

    EXPECT_TRUE(modelWith(std::nullopt).run.counters.empty());

    WorkflowExecutionCounterAccumulator running;
    running.addBytesHashed(1'234);
    running.addBytesCopied(5);
    const ReportRun whileRunning = modelWith(running.snapshot()).run;
    ASSERT_EQ(whileRunning.counters.size(), 25u);
    EXPECT_EQ(whileRunning.counters[0].key, QStringLiteral("bytesHashed"));
    EXPECT_EQ(whileRunning.counters[0].value, 1'234u);
    EXPECT_EQ(whileRunning.counters[1].value, 5u);
    QSet<QString> keys;
    for (const ReportCounter& counter : whileRunning.counters)
    {
        EXPECT_FALSE(counter.name.isEmpty());
        keys.insert(counter.key);
    }
    EXPECT_EQ(keys.size(), 25) << "every counter has a key of its own";

    // The result's own counters are the final ones and win over a snapshot taken while the run was ending.
    WorkflowExecutionCounterAccumulator final;
    final.addBytesHashed(9'999);
    scenario->result.executionCounters = final.snapshot();
    EXPECT_EQ(modelWith(running.snapshot()).run.counters[0].value, 9'999u);
}

// ---- The run: lights, exposures, nights, targets.

TEST(ReportModelBuilder, TheRunsLightsAreCountedByFilterAndByNight)
{
    const auto scenario = built(ReportScenarioId::FiveFilters);
    ASSERT_TRUE(scenario.has_value());

    const ReportRun run = buildScenarioModel(*scenario).run;

    EXPECT_EQ(run.frames, 174);
    ASSERT_EQ(run.acquisition.size(), 5u);
    int frames = 0;
    qint64 integration = 0;
    double share = 0.0;
    QStringList filters;
    for (const ReportAcquisitionRow& row : run.acquisition)
    {
        frames += row.exposure.frames;
        integration += row.exposure.integrationMs;
        share += row.share;
        filters << row.filter;
        EXPECT_EQ(row.exposure.framesWithExposure, row.exposure.frames);
        EXPECT_FALSE(row.exposure.bins.empty());
    }
    EXPECT_EQ(frames, 174);
    EXPECT_EQ(integration, run.capturedMs);
    EXPECT_GT(run.capturedMs, 0);
    EXPECT_NEAR(share, 1.0, 1e-9);
    EXPECT_EQ(filters, (QStringList{QStringLiteral("Luminance"), QStringLiteral("Red"), QStringLiteral("Green"),
                                    QStringLiteral("Blue"), QStringLiteral("Ha")}))
        << "a row per group, in the order of the run's masters";

    ASSERT_EQ(run.nights.size(), 2u);
    EXPECT_LT(run.nights[0].date, run.nights[1].date);
    int nightFrames = 0;
    qint64 nightIntegration = 0;
    for (const ReportNight& night : run.nights)
    {
        nightFrames += night.frames;
        nightIntegration += night.integrationMs;
        int ofFilters = 0;
        for (const ReportNightFilter& filter : night.filters)
        {
            ofFilters += filter.frames;
        }
        EXPECT_EQ(ofFilters, night.frames);
    }
    EXPECT_EQ(nightFrames, 174);
    EXPECT_EQ(nightIntegration, run.capturedMs);

    // The sensor's temperature is the one the lights' headers state.
    ASSERT_TRUE(run.lowestTemperature.has_value());
    ASSERT_TRUE(run.highestTemperature.has_value());
    EXPECT_DOUBLE_EQ(*run.lowestTemperature, -10.0);
    EXPECT_DOUBLE_EQ(*run.highestTemperature, -10.0);
    EXPECT_EQ(run.captureResolution, scenario->manifest.integrationGroups.front().resolution);
}

TEST(ReportModelBuilder, LightsOfSeveralExposuresAreCountedLightByLight)
{
    const auto scenario = built(ReportScenarioId::HdrExposureMerge);
    ASSERT_TRUE(scenario.has_value());
    const WorkflowManifest::IntegrationGroup& group = scenario->manifest.integrationGroups.front();
    const double seconds = std::accumulate(group.lightExposureSeconds.begin(), group.lightExposureSeconds.end(), 0.0);

    const ReportModel model = buildScenarioModel(*scenario);

    ASSERT_FALSE(model.masters.empty());
    const ReportExposure& exposure = model.masters.front().exposure;
    EXPECT_EQ(exposure.frames, static_cast<int>(group.lightFiles.size()));
    EXPECT_EQ(exposure.framesWithExposure, exposure.frames);
    EXPECT_EQ(exposure.integrationMs, std::llround(seconds * 1000.0));
    ASSERT_EQ(exposure.bins.size(), 3u);
    EXPECT_DOUBLE_EQ(exposure.bins[0].seconds, 5.0);
    EXPECT_DOUBLE_EQ(exposure.bins[1].seconds, 30.0);
    EXPECT_DOUBLE_EQ(exposure.bins[2].seconds, 180.0);
    EXPECT_EQ(exposure.bins[0].frames + exposure.bins[1].frames + exposure.bins[2].frames, exposure.frames);
    EXPECT_EQ(model.run.capturedMs, exposure.integrationMs);
}

TEST(ReportModelBuilder, ALightThatStatesNoExposureIsCountedAndAddsNoTime)
{
    auto scenario = built(ReportScenarioId::OneMonoMaster);
    ASSERT_TRUE(scenario.has_value());
    WorkflowManifest::IntegrationGroup& group = scenario->manifest.integrationGroups.front();
    // A light whose exposure neither the manifest, its name nor its group states.
    group.exposure = QStringLiteral("-");
    group.lightExposureSeconds.assign(group.lightFiles.size(), 0.0);
    group.lightFiles.front() = FrameId(QStringLiteral("D:/lights/unnamed_light.fits"));
    const std::size_t lights = group.lightFiles.size();

    const ReportExposure exposure = buildScenarioModel(*scenario).masters.front().exposure;

    EXPECT_EQ(exposure.frames, static_cast<int>(lights));
    EXPECT_EQ(exposure.framesWithExposure, static_cast<int>(lights) - 1) << "the others state it in their names";
}

TEST(ReportModelBuilder, TwoTargetsAreStatedApart)
{
    const auto scenario = built(ReportScenarioId::TwoTargets);
    ASSERT_TRUE(scenario.has_value());

    const ReportModel model = buildScenarioModel(*scenario);

    ASSERT_EQ(model.run.targets.size(), 2);
    ASSERT_EQ(model.targets.size(), 2u);
    int frames = 0;
    qint64 integration = 0;
    for (const ReportTarget& target : model.targets)
    {
        EXPECT_TRUE(model.run.targets.contains(target.name));
        EXPECT_EQ(target.filters.size(), 2);
        EXPECT_EQ(target.masters.size(), 2);
        EXPECT_GT(target.frames, 0);
        frames += target.frames;
        integration += target.integrationMs;
        // The masters were measured, so the target states what they measured.
        EXPECT_TRUE(target.snr.has_value());
        ASSERT_TRUE(target.overlapRetained.has_value());
        EXPECT_LE(*target.overlapRetained, 1.0);
    }
    EXPECT_EQ(frames, model.run.frames);
    EXPECT_EQ(integration, model.run.capturedMs);
    for (const ReportMaster& master : model.masters)
    {
        EXPECT_TRUE(model.run.targets.contains(master.target)) << master.name.toStdString();
    }
}

// ---- The masters.

TEST(ReportModelBuilder, AMasterStatesItsFileItsLightsAndWhatItsStackRecorded)
{
    const auto scenario = built(ReportScenarioId::FiveFilters);
    ASSERT_TRUE(scenario.has_value());
    const IntegrationGroupResult* result = nullptr;
    for (const IntegrationGroupResult& candidate : scenario->result.integrationGroupResults)
    {
        if (candidate.groupKey.contains(QStringLiteral("|Luminance|")) &&
            !candidate.groupKey.startsWith(QStringLiteral("rgb")))
        {
            result = &candidate;
        }
    }
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->qualityReport.valid);

    const ReportModel model = buildScenarioModel(*scenario);
    const ReportMaster* master = masterOfFilter(model, QStringLiteral("Luminance"));
    ASSERT_NE(master, nullptr);

    EXPECT_EQ(master->key, result->groupKey);
    EXPECT_EQ(master->kind, ReportMasterKind::Stack);
    EXPECT_EQ(master->state, ReportMasterState::Made);
    EXPECT_EQ(master->fileName, result->outputFilename);
    EXPECT_EQ(master->file, result->outputPath);
    ASSERT_TRUE(master->bytes.has_value());
    EXPECT_EQ(*master->bytes, result->outputBytes);
    ASSERT_TRUE(master->lightsCombined.has_value());
    EXPECT_EQ(*master->lightsCombined, 60);
    EXPECT_EQ(master->exposure.frames, 60);
    ASSERT_TRUE(master->durationMs.has_value());
    EXPECT_EQ(*master->durationMs, result->durationMs);
    EXPECT_EQ(master->registrationGroupKey, result->registrationGroupKey);
    EXPECT_EQ(master->registrationInputPolicy, result->registrationInputPolicy.trimmed());
    EXPECT_FALSE(master->stackingSummary.isEmpty());
    EXPECT_TRUE(master->drizzleSummary.isEmpty());
    EXPECT_FALSE(master->target.isEmpty());

    // The lights were calibrated with a dark and a flat, named by their files.
    EXPECT_FALSE(master->calibration.dark.isEmpty());
    EXPECT_FALSE(master->calibration.flat.isEmpty());

    ASSERT_TRUE(master->stack.has_value());
    const StackQualityRecord& quality = result->qualityReport;
    EXPECT_EQ(master->stack->inputFrames, quality.inputFrameCount);
    EXPECT_DOUBLE_EQ(master->stack->effectiveFrames, quality.effectiveFrameCount);
    EXPECT_DOUBLE_EQ(master->stack->measuredSnrGain, quality.actualSnrGain);
    EXPECT_DOUBLE_EQ(master->stack->theoreticalSnrGain, quality.theoreticalSnrGain);
    EXPECT_DOUBLE_EQ(master->stack->efficiency, quality.integrationEfficiency);
    EXPECT_DOUBLE_EQ(master->stack->lowestCoverage, quality.minCoverage);
    EXPECT_DOUBLE_EQ(master->stack->medianCoverage, quality.medianCoverage);
    EXPECT_DOUBLE_EQ(master->stack->highestCoverage, quality.maxCoverage);
    EXPECT_EQ(master->stack->rejection.has_value(), quality.hasRejectionStats);
    if (quality.hasRejectionStats)
    {
        EXPECT_DOUBLE_EQ(master->stack->rejection->global, quality.globalRejectionRate);
        EXPECT_DOUBLE_EQ(master->stack->rejection->highestFrame, quality.maxFrameRejectionRate);
    }
    EXPECT_EQ(master->stack->weights.has_value(), quality.hasWeightingStats);
    EXPECT_EQ(master->stack->localWeights.has_value(), quality.hasLocalWeightingStats);
    EXPECT_EQ(master->stack->clusters.has_value(), quality.hasSpatialRejectionClusters);
    EXPECT_EQ(master->stack->restack.has_value(), quality.spatialRestackRequested);
}

TEST(ReportModelBuilder, AMastersFiguresAreMeasuredOnItsOwnSamples)
{
    const auto scenario = built(ReportScenarioId::FiveFilters);
    ASSERT_TRUE(scenario.has_value());

    const ReportModel model = buildScenarioModel(*scenario);

    for (const ReportMaster& master : model.masters)
    {
        SCOPED_TRACE(master.name.toStdString());
        ASSERT_TRUE(master.statistics.has_value());
        const ReportImageStatistics& statistics = *master.statistics;
        EXPECT_GT(statistics.width, 0u);
        EXPECT_GT(statistics.height, 0u);
        ASSERT_EQ(statistics.channelStats.size(), statistics.channels);
        EXPECT_EQ(statistics.channels, master.kind == ReportMasterKind::CombinedRgb ? 3u : 1u);
        for (const ReportChannelStatistics& channel : statistics.channelStats)
        {
            EXPECT_EQ(channel.samples, statistics.width * statistics.height);
            EXPECT_LE(channel.unclippedSamples, channel.samples);
            EXPECT_LE(channel.minimum, channel.median);
            EXPECT_LE(channel.median, channel.maximum);
            // A master is linear data well under full scale: a figure read from a stretched preview would not be.
            EXPECT_LT(channel.maximum, 1.0);
            EXPECT_GT(channel.mean, 0.0);
        }
        ASSERT_TRUE(master.snapshot.mean.has_value());
        ASSERT_TRUE(master.snapshot.dynamicRange.has_value());
        EXPECT_GE(*master.snapshot.dynamicRange, 0.0);
        if (master.snapshot.backgroundNoise.has_value() && *master.snapshot.backgroundNoise > 0.0)
        {
            ASSERT_TRUE(master.snapshot.snr.has_value());
            EXPECT_DOUBLE_EQ(*master.snapshot.snr, *master.snapshot.mean / *master.snapshot.backgroundNoise);
        }
    }
    const ReportMaster* luminance = masterOfFilter(model, QStringLiteral("Luminance"));
    ASSERT_NE(luminance, nullptr);
    // A master of the frames' own size kept all of the capture's area.
    ASSERT_TRUE(luminance->overlapRetained.has_value());
    EXPECT_DOUBLE_EQ(*luminance->overlapRetained, 1.0);
}

TEST(ReportModelBuilder, WithoutASessionNoImageIsReadAndNoFigureOfOneIsStated)
{
    const auto scenario = built(ReportScenarioId::FiveFiltersWithoutSession);
    ASSERT_TRUE(scenario.has_value());
    ASSERT_EQ(scenario->session(), nullptr);

    const ReportModel model = buildScenarioModel(*scenario);

    EXPECT_EQ(model.run.groups.made, 6);
    for (const ReportMaster& master : model.masters)
    {
        EXPECT_FALSE(master.statistics.has_value()) << master.name.toStdString();
        EXPECT_FALSE(master.snapshot.mean.has_value());
        EXPECT_FALSE(master.snapshot.snr.has_value());
        EXPECT_FALSE(master.overlapRetained.has_value());
        // What the run recorded needs no session.
        ASSERT_TRUE(master.bytes.has_value());
        EXPECT_GT(*master.bytes, 0);
    }
    EXPECT_TRUE(model.run.equipment.empty());
    EXPECT_FALSE(model.run.headers.has_value()) << "no header was read, which is not zero of them stating a field";
    for (const ReportTarget& target : model.targets)
    {
        EXPECT_FALSE(target.snr.has_value());
    }
}

TEST(ReportModelBuilder, EachMasterIsReadOnceHoweverManyFactsNeedIt)
{
    const auto scenario = built(ReportScenarioId::FiveFilters);
    ASSERT_TRUE(scenario.has_value());
    const ProductionReportSources production(scenario->session(), scenario->metricsDatabasePath);
    ReportSources sources = production.sources();
    CountingImages counting(sources.images);
    sources.images = &counting;

    const ReportModel model = buildReportModel(ReportModelInputs{.manifest = scenario->manifest,
                                                                 .result = scenario->result,
                                                                 .runDirectory = {},
                                                                 .context = scenario->context,
                                                                 .counters = std::nullopt},
                                               sources);

    EXPECT_EQ(counting.reads.size(), model.run.groups.made + static_cast<int>(model.calibrationMasters.size()));
    for (auto it = counting.reads.cbegin(); it != counting.reads.cend(); ++it)
    {
        EXPECT_EQ(it.value(), 1) << it.key().toStdString();
    }
}

TEST(ReportModelBuilder, AMasterIsMeasuredWithinTheThreadsTheRunWasAllowed)
{
    auto scenario = built(ReportScenarioId::OneMonoMaster);
    ASSERT_TRUE(scenario.has_value());
    scenario->manifest.maxParallelCores = 1;
    const ProductionReportSources production(scenario->session(), scenario->metricsDatabasePath);
    ReportSources sources = production.sources();
    CountingImages counting(sources.images);
    sources.images = &counting;

    const ReportModel model = buildReportModel(ReportModelInputs{.manifest = scenario->manifest,
                                                                 .result = scenario->result,
                                                                 .runDirectory = {},
                                                                 .context = scenario->context,
                                                                 .counters = std::nullopt},
                                               sources);

    ASSERT_FALSE(counting.widths.isEmpty());
    for (const std::uint32_t width : counting.widths)
    {
        EXPECT_EQ(width, 1u);
    }
    // The figures do not depend on the width.
    EXPECT_TRUE(model.masters.front().statistics == buildScenarioModel(*scenario).masters.front().statistics);
}

TEST(ReportModelBuilder, TheCombinedColourImageIsAMasterOfItsOwnKind)
{
    const auto scenario = built(ReportScenarioId::FiveFilters);
    ASSERT_TRUE(scenario.has_value());

    const ReportModel model = buildScenarioModel(*scenario);

    ASSERT_EQ(model.masters.size(), 6u);
    int combined = 0;
    for (const ReportMaster& master : model.masters)
    {
        if (master.kind != ReportMasterKind::CombinedRgb)
        {
            continue;
        }
        ++combined;
        EXPECT_EQ(master.state, ReportMasterState::Made);
        ASSERT_TRUE(master.lightsCombined.has_value());
        EXPECT_EQ(*master.lightsCombined, 90);
        EXPECT_FALSE(master.registrationGroupKey.isEmpty());
        EXPECT_FALSE(master.target.isEmpty());
        // It is combined from masters, so it has no lights of its own.
        EXPECT_EQ(master.exposure.frames, 0);
        EXPECT_TRUE(master.frames.empty());
    }
    EXPECT_EQ(combined, 1);
}

TEST(ReportModelBuilder, AMastersMapsAndPicturesAreTheOnesItsArchiveHolds)
{
    auto scenario = built(ReportScenarioId::FiveFilters);
    ASSERT_TRUE(scenario.has_value());

    // Before the run is archived there is no picture to name.
    const ReportModel before = buildScenarioModel(*scenario);
    const ReportMaster* unarchived = masterOfFilter(before, QStringLiteral("Luminance"));
    ASSERT_NE(unarchived, nullptr);
    EXPECT_FALSE(unarchived->preview.startsWith(QStringLiteral("report_previews/")));

    ASSERT_TRUE(archiveScenarioRun(*scenario));
    const ReportModel model = buildScenarioModel(*scenario, scenario->runDirectory);
    const ReportMaster* master = masterOfFilter(model, QStringLiteral("Luminance"));
    ASSERT_NE(master, nullptr);

    EXPECT_EQ(master->preview, QStringLiteral("report_previews/%1/master.jpg").arg(master->previewFolder));
    EXPECT_TRUE(QFileInfo::exists(QDir(scenario->runDirectory).filePath(master->preview)));

    ASSERT_EQ(master->maps.size(), 3u);
    ASSERT_TRUE(master->rejectionMapsExpected.has_value());
    EXPECT_EQ(*master->rejectionMapsExpected, 2);
    int rejection = 0;
    int quality = 0;
    for (const ReportMap& map : master->maps)
    {
        SCOPED_TRACE(map.title.toStdString());
        (map.kind == ReportMapKind::Rejection ? rejection : quality) += 1;
        EXPECT_FALSE(map.title.isEmpty());
        ASSERT_TRUE(map.bytes.has_value());
        EXPECT_EQ(*map.bytes, QFileInfo(map.file).size());
        if (!map.preview.isEmpty())
        {
            EXPECT_TRUE(map.preview.startsWith(QStringLiteral("report_previews/%1/").arg(master->previewFolder)));
            EXPECT_TRUE(QFileInfo::exists(QDir(scenario->runDirectory).filePath(map.preview)));
        }
    }
    EXPECT_EQ(rejection, 2);
    EXPECT_EQ(quality, 1);

    // A master with no maps states none, and no count of them.
    const ReportMaster* red = masterOfFilter(model, QStringLiteral("Red"));
    ASSERT_NE(red, nullptr);
    EXPECT_TRUE(red->maps.empty());
    EXPECT_FALSE(red->rejectionMapsExpected.has_value());
}

TEST(ReportModelBuilder, TwoMastersWhoseKeysMakeTheSameFolderNameKeepFoldersOfTheirOwn)
{
    auto scenario = built(ReportScenarioId::TwoTargets);
    ASSERT_TRUE(scenario.has_value());
    ASSERT_GE(scenario->result.integrationGroupResults.size(), 2u);
    // Two keys that differ only in a character a file name cannot hold.
    const QString first = QStringLiteral("64x48|-|-|-|Red|M:1|-");
    const QString second = QStringLiteral("64x48|-|-|-|Red|M?1|-");
    scenario->result.integrationGroupResults[0].groupKey = first;
    scenario->result.integrationGroupResults[1].groupKey = second;

    const ReportModel model = buildScenarioModel(*scenario);

    const auto masterOfKey = [&model](const QString& key) -> const ReportMaster* {
        const auto found = std::find_if(model.masters.begin(), model.masters.end(),
                                        [&key](const ReportMaster& master) { return master.key == key; });
        return found == model.masters.end() ? nullptr : &*found;
    };
    const ReportMaster* one = masterOfKey(first);
    const ReportMaster* other = masterOfKey(second);
    ASSERT_NE(one, nullptr);
    ASSERT_NE(other, nullptr);
    EXPECT_FALSE(one->previewFolder.isEmpty());
    EXPECT_NE(one->previewFolder, other->previewFolder);
    EXPECT_NE(one->slug, other->slug);
}

TEST(ReportModelBuilder, AMastersSolutionSaysWhereItCameFrom)
{
    auto scenario = built(ReportScenarioId::OneColourMaster);
    ASSERT_TRUE(scenario.has_value());
    ASSERT_EQ(scenario->result.integrationGroupResults.front().masterSolution, MasterSolutionOutcome::Solved);
    EXPECT_EQ(buildScenarioModel(*scenario).masters.front().astrometry, ReportAstrometryOutcome::Solved);

    scenario->result.integrationGroupResults.front().masterSolution = MasterSolutionOutcome::Propagated;
    scenario->result.integrationGroupResults.front().masterSolutionDetail =
        QStringLiteral("carried from the registration reference");
    const ReportMaster inherited = buildScenarioModel(*scenario).masters.front();
    EXPECT_EQ(inherited.astrometry, ReportAstrometryOutcome::Inherited);
    EXPECT_EQ(inherited.astrometryDetail, QStringLiteral("carried from the registration reference"));

    scenario->result.integrationGroupResults.front().masterSolution = MasterSolutionOutcome::None;
    EXPECT_EQ(buildScenarioModel(*scenario).masters.front().astrometry, ReportAstrometryOutcome::None);

    scenario->result.integrationGroupResults.front().masterSolution = MasterSolutionOutcome::NotRecorded;
    EXPECT_EQ(buildScenarioModel(*scenario).masters.front().astrometry, ReportAstrometryOutcome::NotRecorded);
}

TEST(ReportModelBuilder, ADrizzledMasterSaysHowItWasDrizzled)
{
    const auto scenario = built(ReportScenarioId::DrizzledGroup);
    ASSERT_TRUE(scenario.has_value());

    const ReportModel model = buildScenarioModel(*scenario);

    ASSERT_EQ(model.masters.size(), 2u);
    EXPECT_FALSE(model.masters.front().drizzleSummary.isEmpty());
    EXPECT_TRUE(model.masters.back().drizzleSummary.isEmpty());
    // The drizzled master is larger than its frames: the share of the capture it covers is not more than all of it.
    ASSERT_TRUE(model.masters.front().overlapRetained.has_value());
    EXPECT_DOUBLE_EQ(*model.masters.front().overlapRetained, 1.0);
}

TEST(ReportModelBuilder, AMastersFinalizationPhasesAreStatedWhenTheyWereTimed)
{
    auto scenario = built(ReportScenarioId::OneMonoMaster);
    ASSERT_TRUE(scenario.has_value());
    IntegrationGroupResult& result = scenario->result.integrationGroupResults.front();
    result.finalizationTiming = StackGroupFinalizationTiming{};
    EXPECT_TRUE(buildScenarioModel(*scenario).masters.front().finalization.empty());

    result.finalizationTiming.captured = true;
    result.finalizationTiming.phases = {
        StagePhaseTiming{QStringLiteral("Master write"), QStringLiteral("completed"), 120, 0},
        StagePhaseTiming{QStringLiteral("Plate solve"), QStringLiteral("skipped"), 0, 0}};
    const ReportMaster master = buildScenarioModel(*scenario).masters.front();
    ASSERT_EQ(master.finalization.size(), 2u);
    EXPECT_EQ(master.finalization[0].name, QStringLiteral("Master write"));
    EXPECT_EQ(master.finalization[0].durationMs, 120);
    EXPECT_EQ(master.finalization[1].status, QStringLiteral("skipped"));
}

TEST(ReportModelBuilder, AMasterThatFailedKeepsThePhasesItReached)
{
    auto scenario = built(ReportScenarioId::FailedInStacking);
    ASSERT_TRUE(scenario.has_value());
    const auto failed =
        std::find_if(scenario->result.integrationGroupResults.begin(), scenario->result.integrationGroupResults.end(),
                     [](const IntegrationGroupResult& group) { return !group.success; });
    ASSERT_NE(failed, scenario->result.integrationGroupResults.end());
    failed->durationMs = 4'321;
    failed->finalizationTiming.captured = true;
    failed->finalizationTiming.phases = {
        StagePhaseTiming{QStringLiteral("Stack"), QStringLiteral("failed"), 4'000, 0},
        StagePhaseTiming{QStringLiteral("Master write"), QStringLiteral("skipped"), 0, 0}};
    const QString key = failed->groupKey;

    const ReportModel model = buildScenarioModel(*scenario);

    const auto master = std::find_if(model.masters.begin(), model.masters.end(),
                                     [&key](const ReportMaster& candidate) { return candidate.key == key; });
    ASSERT_NE(master, model.masters.end());
    EXPECT_EQ(master->state, ReportMasterState::Failed);
    ASSERT_TRUE(master->durationMs.has_value());
    EXPECT_EQ(*master->durationMs, 4'321);
    ASSERT_EQ(master->finalization.size(), 2u);
    EXPECT_EQ(master->finalization[0].status, QStringLiteral("failed"));
    EXPECT_EQ(master->finalization[0].durationMs, 4'000);
    // A phase the attempt never reached is there, skipped, with no time.
    EXPECT_EQ(master->finalization[1].status, QStringLiteral("skipped"));
    EXPECT_EQ(master->finalization[1].durationMs, 0);
}

TEST(ReportModelBuilder, MapsTheSettingsNoLongerAskForAreNotStated)
{
    auto scenario = built(ReportScenarioId::FiveFilters);
    ASSERT_TRUE(scenario.has_value());
    for (WorkflowManifest::IntegrationGroup& group : scenario->manifest.integrationGroups)
    {
        if (group.filter == QStringLiteral("Luminance"))
        {
            group.stacking.createQualityMaps = false;
        }
    }

    const ReportModel model = buildScenarioModel(*scenario);
    const ReportMaster* master = masterOfFilter(model, QStringLiteral("Luminance"));
    ASSERT_NE(master, nullptr);

    // The result still names a quality map left by an earlier run of other settings; the rejection maps stay.
    int rejection = 0;
    for (const ReportMap& map : master->maps)
    {
        EXPECT_EQ(map.kind, ReportMapKind::Rejection) << map.title.toStdString();
        ++rejection;
    }
    EXPECT_EQ(rejection, 2);
    EXPECT_TRUE(master->statistics.has_value());
}

TEST(ReportModelBuilder, ARestackNamesTheLightsItLeftOutByTheirPlaceAmongTheGroupsLights)
{
    const auto scenario = built(ReportScenarioId::EveryLightState);
    ASSERT_TRUE(scenario.has_value());
    ASSERT_GT(scenario->facts.lightsExcludedOnRestack, 0);

    const ReportModel model = buildScenarioModel(*scenario);

    int excluded = 0;
    for (const ReportMaster& master : model.masters)
    {
        if (!master.stack.has_value() || !master.stack->restack.has_value())
        {
            continue;
        }
        for (const ReportRestackExclusion& light : master.stack->restack->excluded)
        {
            ++excluded;
            ASSERT_LT(light.index, master.frames.size());
            EXPECT_EQ(light.fileName, master.frames[light.index].fileName);
            EXPECT_TRUE(light.rejectedFraction.has_value());
        }
    }
    EXPECT_EQ(excluded, scenario->facts.lightsExcludedOnRestack);
}

// ---- The calibration.

TEST(ReportModelBuilder, ACalibrationMasterSaysWhatBecameOfItAndWhatItWasStackedFrom)
{
    const auto scenario = built(ReportScenarioId::FiveFilters);
    ASSERT_TRUE(scenario.has_value());

    const ReportModel model = buildScenarioModel(*scenario);

    ASSERT_EQ(model.calibrationMasters.size(), 7u);
    const ReportCalibrationMaster* bias = calibrationOfKind(model, ReportCalibrationKind::Bias);
    ASSERT_NE(bias, nullptr);
    EXPECT_EQ(bias->state, ReportCalibrationState::Reused) << "a master the run found still valid leaves no result";
    QSet<QString> slugs;
    for (const ReportCalibrationMaster& master : model.calibrationMasters)
    {
        SCOPED_TRACE(master.name.toStdString());
        if (master.kind != ReportCalibrationKind::Bias)
        {
            EXPECT_EQ(master.state, ReportCalibrationState::Made);
        }
        EXPECT_GT(master.frames, 0);
        EXPECT_EQ(master.inputFiles.size(), master.frames);
        EXPECT_FALSE(master.fileName.isEmpty());
        EXPECT_FALSE(master.file.isEmpty());
        EXPECT_FALSE(master.stackingSummary.isEmpty());
        EXPECT_FALSE(slugs.contains(master.slug));
        slugs.insert(master.slug);
    }
    for (const ReportMaster& master : model.masters)
    {
        EXPECT_FALSE(slugs.contains(master.slug)) << "a calibration master and a master share an anchor";
    }
}

TEST(ReportModelBuilder, ASuppliedMasterAndAPreCalibratedGroupAreToldApart)
{
    const auto scenario = built(ReportScenarioId::PreCalibratedAndSuppliedDark);
    ASSERT_TRUE(scenario.has_value());

    const ReportModel model = buildScenarioModel(*scenario);

    const ReportCalibrationMaster* dark = calibrationOfKind(model, ReportCalibrationKind::Dark);
    ASSERT_NE(dark, nullptr);
    EXPECT_EQ(dark->state, ReportCalibrationState::Supplied);
    EXPECT_EQ(FrameId(dark->file), *scenario->manifest.darkStage->groups.front().existingMaster);

    ASSERT_EQ(model.lightGroups.size(), 2u);
    const auto preCalibrated = std::find_if(model.lightGroups.begin(), model.lightGroups.end(),
                                            [](const ReportLightGroup& group) { return group.preCalibrated; });
    ASSERT_NE(preCalibrated, model.lightGroups.end());
    EXPECT_EQ(preCalibrated->lights, 12);
    EXPECT_FALSE(preCalibrated->name.isEmpty());
    EXPECT_TRUE(preCalibrated->settings.empty()) << "no master is applied, so no setting of one is stated";
    EXPECT_EQ(std::count_if(model.lightGroups.begin(), model.lightGroups.end(),
                            [](const ReportLightGroup& group) { return group.preCalibrated; }),
              1);

    // The pre-calibrated lights take no master; the others take the supplied dark.
    int withDark = 0;
    int withNone = 0;
    for (const ReportMaster& master : model.masters)
    {
        (master.calibration.dark.isEmpty() ? withNone : withDark) += 1;
        EXPECT_TRUE(master.calibration.bias.isEmpty());
    }
    EXPECT_EQ(withDark, 1);
    EXPECT_EQ(withNone, 1);
}

TEST(ReportModelBuilder, ARunWithNoCalibrationFramesStatesNoCalibration)
{
    const auto scenario = built(ReportScenarioId::OneMonoMaster);
    ASSERT_TRUE(scenario.has_value());

    const ReportModel model = buildScenarioModel(*scenario);

    EXPECT_TRUE(model.calibrationMasters.empty());
    // The lights are one group that takes no master, which is not the same as lights marked calibrated already.
    ASSERT_EQ(model.lightGroups.size(), 1u);
    EXPECT_FALSE(model.lightGroups.front().preCalibrated);
    EXPECT_TRUE(model.lightGroups.front().settings.empty());
    EXPECT_TRUE(model.lightGroups.front().darkMaster.isEmpty());
    EXPECT_TRUE(model.lightGroups.front().flatMaster.isEmpty());
    EXPECT_TRUE(model.masters.front().calibration.bias.isEmpty());
    EXPECT_TRUE(model.masters.front().calibration.dark.isEmpty());
    EXPECT_TRUE(model.masters.front().calibration.flat.isEmpty());
}

// ---- The registration.

TEST(ReportModelBuilder, ARegistrationGroupStatesItsReferenceItsLightsAndTheMastersItServes)
{
    auto scenario = built(ReportScenarioId::FiveFilters);
    ASSERT_TRUE(scenario.has_value());
    // The first group's reference is one the manifest names; the second's was left to the registration.
    ASSERT_EQ(scenario->manifest.registrationGroups.size(), 2u);
    scenario->manifest.registrationGroups.front().referenceFrame =
        scenario->manifest.registrationGroups.front().lightFiles.front();
    scenario->manifest.registrationGroups.back().referenceFrame.reset();

    const ReportModel model = buildScenarioModel(*scenario);

    ASSERT_EQ(model.registrationGroups.size(), 2u);
    // A reference the manifest does not name is the one the registration's own records state.
    EXPECT_EQ(model.registrationGroups.back().reference,
              QFileInfo(scenario->manifest.registrationGroups.back().lightFiles.front().canonicalPath()).fileName());
    QStringList served;
    for (std::size_t i = 0; i < model.registrationGroups.size(); ++i)
    {
        const ReportRegistrationGroup& group = model.registrationGroups[i];
        const WorkflowManifest::RegistrationGroup& planned = scenario->manifest.registrationGroups[i];
        SCOPED_TRACE(group.name.toStdString());
        EXPECT_EQ(group.key, planned.groupKey.toString());
        EXPECT_EQ(group.lights, static_cast<int>(planned.lightFiles.size()));
        if (planned.referenceFrame.has_value())
        {
            EXPECT_EQ(group.reference, QFileInfo(planned.referenceFrame->canonicalPath()).fileName());
        }
        EXPECT_FALSE(group.transform.isEmpty());
        EXPECT_FALSE(group.masters.isEmpty());
        served += group.masters;
        // The registration recorded every light, and none failed.
        ASSERT_TRUE(group.registered.has_value());
        EXPECT_EQ(*group.registered, group.lights);
        int byKind = 0;
        for (const ReportTransformUse& use : group.transformsUsed)
        {
            ASSERT_TRUE(use.frames.has_value());
            byKind += *use.frames;
        }
        EXPECT_EQ(byKind, *group.registered);
    }
    EXPECT_EQ(served.size(), 5) << "every group of lights is served by one registration group";
    for (const ReportMaster& master : model.masters)
    {
        if (master.kind == ReportMasterKind::Stack)
        {
            EXPECT_TRUE(served.contains(master.key));
        }
    }
}

TEST(ReportModelBuilder, TheCorrectionsOfAnAlignmentAreCountedPerGroup)
{
    const auto scenario = built(ReportScenarioId::DistortionCorrectionsAndManualPoints);
    ASSERT_TRUE(scenario.has_value());

    const ReportModel model = buildScenarioModel(*scenario);

    ASSERT_EQ(model.registrationGroups.size(), 2u);
    const ReportRegistrationGroup& corrected = model.registrationGroups.front();
    ASSERT_TRUE(corrected.distortionCorrected.has_value());
    EXPECT_EQ(*corrected.distortionCorrected, 20);
    EXPECT_EQ(*corrected.chromaticAberrationCorrected, 17);
    EXPECT_EQ(*corrected.chromaticAberrationPassedThrough, 3);
    EXPECT_EQ(*corrected.aligned, scenario->result.alignmentGroupResults.front().totalAligned);

    const ReportRegistrationGroup& manual = model.registrationGroups.back();
    ASSERT_TRUE(manual.distortionCorrected.has_value());
    EXPECT_EQ(*manual.distortionCorrected, 0) << "counted as none, which is not the same as not counted";
    EXPECT_EQ(manual.transform, QStringLiteral("Manual Points"));
}

TEST(ReportModelBuilder, AGroupTheAlignmentDidNotReportStatesNoCorrectionCount)
{
    auto scenario = built(ReportScenarioId::OneMonoMaster);
    ASSERT_TRUE(scenario.has_value());
    scenario->result.alignmentGroupResults.clear();

    const ReportModel model = buildScenarioModel(*scenario);

    ASSERT_FALSE(model.registrationGroups.empty());
    EXPECT_FALSE(model.registrationGroups.front().aligned.has_value());
    EXPECT_FALSE(model.registrationGroups.front().distortionCorrected.has_value());
}

TEST(ReportModelBuilder, WithoutAMetricsDatabaseNoLightIsCountedAsRegistered)
{
    const auto scenario = built(ReportScenarioId::FiveFiltersWithoutMetrics);
    ASSERT_TRUE(scenario.has_value());

    const ReportModel model = buildScenarioModel(*scenario);

    for (const ReportRegistrationGroup& group : model.registrationGroups)
    {
        EXPECT_FALSE(group.registered.has_value());
        EXPECT_TRUE(group.transformsUsed.empty());
        EXPECT_TRUE(group.cometRuns.empty());
        EXPECT_FALSE(group.catalogHandoff.has_value());
        EXPECT_GT(group.lights, 0) << "the manifest still says how many lights the group has";
    }
    EXPECT_EQ(model.run.groups.made, 6) << "what the run recorded is stated all the same";
}

TEST(ReportModelBuilder, AReferenceTheUserChoseIsNotTheApplicationsChoice)
{
    auto scenario = built(ReportScenarioId::OneMonoMaster);
    ASSERT_TRUE(scenario.has_value());
    WorkflowManifest::RegistrationGroup& group = scenario->manifest.registrationGroups.front();
    group.settings.autoReferenceDuringRegistration = true;
    group.settings.referenceImage.clear();
    EXPECT_TRUE(buildScenarioModel(*scenario).registrationGroups.front().referenceChosenAutomatically);

    group.settings.autoReferenceDuringRegistration = false;
    group.settings.referenceImage = group.lightFiles.front().canonicalPath();
    EXPECT_FALSE(buildScenarioModel(*scenario).registrationGroups.front().referenceChosenAutomatically);
}

TEST(ReportModelBuilder, ACometFitIsStatedOnTheGroupItWasMadeFor)
{
    const auto scenario = built(ReportScenarioId::CometRun);
    ASSERT_TRUE(scenario.has_value());
    const QString key = scenario->manifest.registrationGroups.front().groupKey.toString();
    const ProductionReportSources production(scenario->session(), scenario->metricsDatabasePath);
    ReportSources sources = production.sources();
    StubbedMetrics metrics(sources.metrics);
    sources.metrics = &metrics;

    CometReportRun fit;
    fit.run.cometRunId = QStringLiteral("run-1");
    fit.run.groupKey = key;
    fit.run.status = QStringLiteral(" Ready ");
    fit.run.mode = QStringLiteral("comet_aligned");
    an::ui::CometTrackingRunSummaryRecord summary;
    summary.fitModel = QStringLiteral("Linear");
    summary.rmsPixels = 0.42;
    summary.maxResidualPixels = 1.3;
    summary.acceptedInputCount = 18;
    summary.rejectedInputCount = 2;
    summary.predictedOnlyFrameCount = 1;
    summary.warningFlags = QStringLiteral("short-arc");
    fit.summary = summary;
    const auto point = [](const QString& origin, const QString& fitState) {
        an::ui::CometTrackingPointRecord record;
        record.origin = origin;
        record.fitState = fitState;
        return record;
    };
    fit.points = {point(QStringLiteral("Manual"), QStringLiteral("Accepted")),
                  point(QStringLiteral("Manual"), QStringLiteral("Outlier")),
                  point(QStringLiteral("AcceptedAutomatic"), QStringLiteral("Accepted")),
                  point(QStringLiteral("AutoCandidate"), QStringLiteral("PredictedOnly"))};
    an::ui::CometTransformRoleProduct product;
    product.readiness.readinessStatus = QStringLiteral("Ready");
    product.readiness.selectedFrameCount = 20;
    product.readiness.successfulTransformCount = 19;
    product.readiness.failedTransformCount = 1;
    product.readiness.missingTransformCount = 0;
    product.outputPlane.coordinateFrameKind = QStringLiteral("comet");
    product.outputPlane.canvasWidth = 64;
    product.outputPlane.canvasHeight = 48;
    product.outputPlane.skyWcsValid = false;
    fit.cometAlignedProduct = product;

    CometReportRun ofAnotherGroup = fit;
    ofAnotherGroup.run.groupKey = QStringLiteral("no such group");
    CometReportRun bare;
    bare.run.groupKey = key;
    bare.run.status = QStringLiteral("Candidate");
    metrics.runs = {fit, ofAnotherGroup, bare};

    const ReportModel model = buildReportModel(ReportModelInputs{.manifest = scenario->manifest,
                                                                 .result = scenario->result,
                                                                 .runDirectory = {},
                                                                 .context = scenario->context,
                                                                 .counters = std::nullopt},
                                               sources);

    const ReportRegistrationGroup& group = model.registrationGroups.front();
    ASSERT_EQ(group.cometRuns.size(), 2u) << "a fit of a group the run does not have is not stated";
    const ReportCometRun& stated = group.cometRuns.front();
    EXPECT_EQ(stated.status, QStringLiteral("Ready"));
    EXPECT_EQ(stated.mode, QStringLiteral("comet_aligned"));
    EXPECT_EQ(stated.fitModel, QStringLiteral("Linear"));
    EXPECT_DOUBLE_EQ(*stated.rmsPixels, 0.42);
    EXPECT_DOUBLE_EQ(*stated.largestResidualPixels, 1.3);
    EXPECT_EQ(stated.warnings, QStringLiteral("short-arc"));
    EXPECT_EQ(*stated.inputsAccepted, 18);
    EXPECT_EQ(*stated.inputsRejected, 2);
    EXPECT_EQ(*stated.inputsPredictedOnly, 1);
    EXPECT_EQ(stated.pointsManual, 2);
    EXPECT_EQ(stated.pointsAutomatic, 1);
    EXPECT_EQ(stated.pointsPredictedOnly, 1);
    EXPECT_EQ(stated.pointsOutliers, 1);
    EXPECT_EQ(stated.readiness, QStringLiteral("Ready"));
    EXPECT_EQ(*stated.transformsSucceeded, 19);
    EXPECT_EQ(*stated.transformsFailed, 1);
    EXPECT_EQ(*stated.transformsMissing, 0);
    EXPECT_TRUE(stated.outputPlane.contains(QStringLiteral("comet")));
    EXPECT_TRUE(stated.outputPlane.contains(QStringLiteral("64 \u00D7 48")));

    // A fit with no summary and no aligned frames states neither a residual nor a transform count.
    const ReportCometRun& candidate = group.cometRuns.back();
    EXPECT_EQ(candidate.status, QStringLiteral("Candidate"));
    EXPECT_FALSE(candidate.rmsPixels.has_value());
    EXPECT_FALSE(candidate.inputsAccepted.has_value());
    EXPECT_FALSE(candidate.transformsSucceeded.has_value());
    EXPECT_TRUE(candidate.outputPlane.isEmpty());
}

TEST(ReportModelBuilder, TheCatalogSourcesKeptForTheColourCalibrationAreStatedOnTheirGroup)
{
    const auto scenario = built(ReportScenarioId::FiveFilters);
    ASSERT_TRUE(scenario.has_value());
    const QString key = scenario->manifest.registrationGroups.back().groupKey.toString();
    const ProductionReportSources production(scenario->session(), scenario->metricsDatabasePath);
    ReportSources sources = production.sources();
    StubbedMetrics metrics(sources.metrics);
    sources.metrics = &metrics;

    SpccHandoffGroupSummary summary;
    summary.groupKey = key;
    summary.settingsVersion = 4;
    summary.frameCount = 24;
    summary.referenceCount = 300;
    summary.sourceCount = 120;
    summary.satisfiedCount = 280;
    summary.missingSourceCount = 9;
    summary.staleMatchCount = 11;
    summary.astrometricManifestId = QStringLiteral("gaia_dr3_g14");
    summary.spccManifestId = QStringLiteral(" gaia_xp ");
    summary.residualsPx = {0.6, 0.2, 0.4};
    metrics.handoffs = {summary};

    const ReportModel model = buildReportModel(ReportModelInputs{.manifest = scenario->manifest,
                                                                 .result = scenario->result,
                                                                 .runDirectory = {},
                                                                 .context = scenario->context,
                                                                 .counters = std::nullopt},
                                               sources);

    EXPECT_FALSE(model.registrationGroups.front().catalogHandoff.has_value());
    ASSERT_TRUE(model.registrationGroups.back().catalogHandoff.has_value());
    const ReportCatalogHandoff& handoff = *model.registrationGroups.back().catalogHandoff;
    EXPECT_EQ(handoff.settingsVersion, 4);
    EXPECT_EQ(handoff.frames, 24);
    EXPECT_EQ(handoff.references, 300);
    EXPECT_EQ(handoff.sources, 120);
    EXPECT_EQ(handoff.satisfied, 280);
    EXPECT_EQ(handoff.missingSource, 9);
    EXPECT_EQ(handoff.staleMatch, 11);
    EXPECT_EQ(handoff.untested, 0);
    EXPECT_EQ(handoff.astrometricCatalog, QStringLiteral("gaia_dr3_g14"));
    EXPECT_EQ(handoff.photometricCatalog, QStringLiteral("gaia_xp"));
    ASSERT_TRUE(handoff.medianResidualPixels.has_value());
    EXPECT_DOUBLE_EQ(*handoff.medianResidualPixels, 0.4);
    EXPECT_FALSE(handoff.medianSeparationArcsec.has_value()) << "no separation was recorded";
}

// ---- The equipment and the headers.

TEST(ReportModelBuilder, EquipmentIsStatedOncePerSetAndNamesItsMasters)
{
    const auto scenario = built(ReportScenarioId::FiveFilters);
    ASSERT_TRUE(scenario.has_value());

    const ReportModel model = buildScenarioModel(*scenario);

    ASSERT_FALSE(model.run.equipment.empty());
    QSet<QString> sets;
    int named = 0;
    for (const ReportEquipmentRow& row : model.run.equipment)
    {
        const QString set = QStringList{row.target, row.camera, row.captureResolution, row.outputResolution,
                                        row.gain,   row.offset, row.telescope,         row.site}
                                .join(QLatin1Char('|'));
        EXPECT_FALSE(sets.contains(set)) << "a set of equipment is stated twice: " << set.toStdString();
        sets.insert(set);
        EXPECT_FALSE(row.masters.isEmpty());
        named += static_cast<int>(row.masters.size());
        EXPECT_FALSE(row.outputResolution.isEmpty());
        QStringList sorted = row.masters;
        sorted.sort(Qt::CaseInsensitive);
        EXPECT_EQ(row.masters, sorted);
    }
    EXPECT_LE(named, model.run.groups.made);
    EXPECT_LT(model.run.equipment.size(), static_cast<std::size_t>(named)) << "masters of one camera share a row";
}

TEST(ReportModelBuilder, WhatTheLightsHeadersStateIsCountedFieldByField)
{
    const auto scenario = built(ReportScenarioId::OneMonoMaster);
    ASSERT_TRUE(scenario.has_value());

    const ReportModel model = buildScenarioModel(*scenario);

    ASSERT_TRUE(model.run.headers.has_value());
    EXPECT_EQ(model.run.headers->lights, model.run.frames);
    ASSERT_EQ(model.run.headers->fields.size(), 8u);
    bool anyStated = false;
    for (const ReportHeaderField& field : model.run.headers->fields)
    {
        EXPECT_FALSE(field.name.isEmpty());
        EXPECT_GE(field.present, 0);
        EXPECT_LE(field.present, model.run.headers->lights);
        anyStated = anyStated || field.present > 0;
    }
    EXPECT_TRUE(anyStated);
    // Every light states its exposure.
    EXPECT_EQ(model.run.headers->fields.front().present, model.run.headers->lights);
}

TEST(ReportModelBuilder, LightsThatAreNotOnDiskStateNothingOfTheirHeaders)
{
    const auto scenario = built(ReportScenarioId::TwoThousandLights);
    ASSERT_TRUE(scenario.has_value());

    const ReportModel model = buildScenarioModel(*scenario);

    EXPECT_EQ(model.run.frames, 2000);
    EXPECT_FALSE(model.run.headers.has_value());
    ASSERT_FALSE(model.masters.empty());
    EXPECT_EQ(model.masters.front().frames.size(), 2000u);
}
