// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A run beside the earlier runs of its output folder.
///
/// A report model compares each master a run made with the latest earlier run that made it, the run as a whole with
/// the latest earlier run that completed, and lists the runs of the folder. Everything it states of an earlier run
/// is what that run's stored report states: an archive that keeps none is compared with nothing and listed by its
/// id, and no image, manifest or metrics database of another run is read.

#include <gtest/gtest.h>

#include "Test_ImageIntegration/reporting/ReportModelTestSupport.h"

#include "processes/ImageIntegration/reporting/ReportArchiveReader.h"
#include "processes/ImageIntegration/reporting/ReportModelJson.h"
#include "processes/ImageIntegration/reporting/WorkflowReport.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

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

/// A scenario with its run archived, as the executor archives it before the report is built.
std::optional<ReportScenario> archived(const ReportScenarioId id)
{
    std::optional<ReportScenario> scenario = built(id);
    if (scenario.has_value() && !archiveScenarioRun(*scenario))
    {
        return std::nullopt;
    }
    return scenario;
}

QString runIdOf(const QString& runDirectory)
{
    return QFileInfo(runDirectory).fileName();
}

/// The report an earlier run's archive keeps; fails the case when it keeps none.
ReportModel storedReport(const QString& runDirectory)
{
    ReportArchiveRead read = ReportArchiveReader::read(runDirectory);
    EXPECT_TRUE(read.isRead()) << runDirectory.toStdString() << ": " << read.why.toStdString();
    return read.model.value_or(ReportModel{});
}

/// Writes a model as the report a run's archive keeps.
void writeStoredReport(const QString& runDirectory, const ReportModel& model)
{
    ASSERT_TRUE(QDir().mkpath(runDirectory));
    QFile file(QDir(runDirectory).filePath(QLatin1String(kStoredReportFileName)));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QByteArray bytes = QJsonDocument(ReportModelJson::toJson(model)).toJson(QJsonDocument::Compact);
    ASSERT_EQ(file.write(bytes), bytes.size());
}

const ReportMaster* combinedOf(const ReportModel& model)
{
    const auto found = std::find_if(model.masters.begin(), model.masters.end(), [](const ReportMaster& master) {
        return master.kind == ReportMasterKind::CombinedRgb;
    });
    return found == model.masters.end() ? nullptr : &*found;
}

int usedOf(const ReportMaster& master)
{
    return static_cast<int>(std::count_if(master.frames.begin(), master.frames.end(), [](const ReportFrame& frame) {
        return frame.state == ReportFrameState::Used;
    }));
}

const ReportHistoryEntry* historyOf(const ReportModel& model, const QString& runId)
{
    const auto found = std::find_if(model.history.begin(), model.history.end(),
                                    [&runId](const ReportHistoryEntry& entry) { return entry.runId == runId; });
    return found == model.history.end() ? nullptr : &*found;
}

QByteArray bytesOf(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
} // namespace

// ---- Each master beside the latest earlier run that made it.

TEST(ReportComparison, EachMasterIsComparedWithTheLatestEarlierRunThatMadeIt)
{
    const auto scenario = archived(ReportScenarioId::FiveFiltersAfterTwoEarlierRuns);
    ASSERT_TRUE(scenario.has_value());
    ASSERT_EQ(scenario->earlierRunDirectories.size(), 3);
    const QString firstRun = runIdOf(scenario->earlierRunDirectories.at(0));
    const QString secondRun = runIdOf(scenario->earlierRunDirectories.at(1));
    const ReportModel first = storedReport(scenario->earlierRunDirectories.at(0));
    const ReportModel second = storedReport(scenario->earlierRunDirectories.at(1));

    const ReportModel model = buildScenarioModel(*scenario, scenario->runDirectory);

    // The luminance master was made again by the second run; the red, green and blue ones that run found still
    // valid, so the first run is the latest that made them.
    const ReportMaster* luminance = masterOfFilter(model, QStringLiteral("Luminance"));
    ASSERT_NE(luminance, nullptr);
    ASSERT_TRUE(luminance->baseline.has_value());
    EXPECT_TRUE(luminance->baseline->made);
    EXPECT_EQ(luminance->baseline->runId, secondRun);
    const ReportMaster* before = masterOfFilter(second, QStringLiteral("Luminance"));
    ASSERT_NE(before, nullptr);
    ASSERT_EQ(before->state, ReportMasterState::Made);
    EXPECT_EQ(luminance->baseline->framesTaken, 34);
    EXPECT_EQ(luminance->baseline->frames, usedOf(*before));
    EXPECT_EQ(luminance->baseline->backgroundNoise, before->snapshot.backgroundNoise);
    EXPECT_EQ(luminance->baseline->snr, before->snapshot.snr);
    ASSERT_TRUE(before->stack.has_value());
    EXPECT_EQ(luminance->baseline->effectiveFrames, before->stack->effectiveFrames);
    EXPECT_TRUE(luminance->baseline->fwhm.has_value());
    EXPECT_TRUE(luminance->baseline->integrationMs.has_value());
    EXPECT_EQ(luminance->baseline->archivedAt, second.run.archivedAt);

    for (const QString& filter : {QStringLiteral("Red"), QStringLiteral("Green"), QStringLiteral("Blue")})
    {
        SCOPED_TRACE(filter.toStdString());
        const ReportMaster* master = masterOfFilter(model, filter);
        ASSERT_NE(master, nullptr);
        ASSERT_TRUE(master->baseline.has_value());
        EXPECT_TRUE(master->baseline->made);
        EXPECT_EQ(master->baseline->runId, firstRun) << "a run that reused the master did not make it";
        const ReportMaster* made = masterOfFilter(first, filter);
        ASSERT_NE(made, nullptr);
        EXPECT_EQ(master->baseline->frames, usedOf(*made));
        EXPECT_EQ(master->baseline->framesTaken, static_cast<int>(made->frames.size()));
        EXPECT_EQ(master->baseline->backgroundNoise, made->snapshot.backgroundNoise);
        EXPECT_EQ(master->baseline->archivedAt, first.run.archivedAt);
    }

    // The colour image was combined by the first run alone.
    const ReportMaster* combined = combinedOf(model);
    ASSERT_NE(combined, nullptr);
    ASSERT_TRUE(combined->baseline.has_value());
    EXPECT_TRUE(combined->baseline->made);
    EXPECT_EQ(combined->baseline->runId, firstRun);
    EXPECT_FALSE(combined->baseline->frames.has_value()) << "a combined image has no lights of its own";
}

TEST(ReportComparison, AMasterNoEarlierRunMadeIsNewInThisRun)
{
    const auto scenario = archived(ReportScenarioId::FiveFiltersAfterTwoEarlierRuns);
    ASSERT_TRUE(scenario.has_value());

    const ReportModel model = buildScenarioModel(*scenario, scenario->runDirectory);

    const ReportMaster* hydrogenAlpha = masterOfFilter(model, QStringLiteral("Ha"));
    ASSERT_NE(hydrogenAlpha, nullptr);
    ASSERT_TRUE(hydrogenAlpha->baseline.has_value()) << "the folder has earlier runs, and none made it";
    const ReportBaseline& baseline = *hydrogenAlpha->baseline;
    EXPECT_FALSE(baseline.made);
    EXPECT_TRUE(baseline.runId.isEmpty());
    EXPECT_FALSE(baseline.frames.has_value());
    EXPECT_FALSE(baseline.backgroundNoise.has_value());
    EXPECT_TRUE(baseline.preview.isEmpty());
    EXPECT_TRUE(baseline.detail.isEmpty());
}

TEST(ReportComparison, AMasterTheRunReusedIsComparedWithNothing)
{
    const auto scenario = archived(ReportScenarioId::IncrementalRun);
    ASSERT_TRUE(scenario.has_value());
    ASSERT_EQ(scenario->earlierRunDirectories.size(), 1);
    const QString earlierRun = runIdOf(scenario->earlierRunDirectories.front());

    const ReportModel model = buildScenarioModel(*scenario, scenario->runDirectory);

    int reused = 0;
    int made = 0;
    for (const ReportMaster& master : model.masters)
    {
        SCOPED_TRACE(master.name.toStdString());
        if (master.state == ReportMasterState::Reused)
        {
            ++reused;
            EXPECT_FALSE(master.baseline.has_value()) << "it is the earlier run's own master";
            EXPECT_EQ(master.madeByRun, earlierRun);
        }
        else if (master.state == ReportMasterState::Made)
        {
            ++made;
            ASSERT_TRUE(master.baseline.has_value());
            EXPECT_TRUE(master.baseline->made);
            EXPECT_EQ(master.baseline->runId, earlierRun);
        }
    }
    EXPECT_EQ(reused, 3);
    EXPECT_EQ(made, 2);
}

TEST(ReportComparison, AnEarlierRunWhoseArchiveKeepsNoReportIsNoBaseline)
{
    const auto scenario = archived(ReportScenarioId::FiveFiltersAfterTwoEarlierRuns);
    ASSERT_TRUE(scenario.has_value());
    // The latest earlier run is the one whose archive keeps no report.
    const QString unreported = scenario->earlierRunDirectories.at(2);
    ASSERT_TRUE(QFileInfo::exists(QDir(unreported).filePath(QStringLiteral("workflow_manifest.json"))));
    ASSERT_FALSE(QFileInfo::exists(QDir(unreported).filePath(QLatin1String(kStoredReportFileName))));

    const ReportModel model = buildScenarioModel(*scenario, scenario->runDirectory);

    ASSERT_TRUE(model.comparison.has_value());
    EXPECT_EQ(model.comparison->baselineRunId, runIdOf(scenario->earlierRunDirectories.at(1)));
    for (const ReportMaster& master : model.masters)
    {
        if (master.baseline.has_value())
        {
            EXPECT_NE(master.baseline->runId, runIdOf(unreported)) << master.name.toStdString();
        }
    }
    // It is listed, by its id, and says why it states nothing.
    const ReportHistoryEntry* entry = historyOf(model, runIdOf(unreported));
    ASSERT_NE(entry, nullptr);
    EXPECT_FALSE(entry->refusal.isEmpty());
    EXPECT_FALSE(entry->outcome.has_value());
    EXPECT_FALSE(entry->frames.has_value());
    EXPECT_FALSE(entry->backgroundNoise.has_value());
}

TEST(ReportComparison, AFolderWhoseEarlierRunsKeepNoReportComparesNothing)
{
    auto scenario = archived(ReportScenarioId::OneMonoMaster);
    ASSERT_TRUE(scenario.has_value());
    const QDir history = QFileInfo(scenario->runDirectory).dir();
    // An earlier run that completed, as its manifest says, and one whose folder is empty: neither keeps a report.
    ASSERT_TRUE(writeArchivedRunManifest(history.filePath(QStringLiteral("20200101_000000_000")), scenario->manifest,
                                         QStringLiteral("success"), QJsonArray{}));
    ASSERT_TRUE(QDir().mkpath(history.filePath(QStringLiteral("20200102_000000_000"))));

    const ReportModel model = buildScenarioModel(*scenario, scenario->runDirectory);

    EXPECT_FALSE(model.comparison.has_value()) << "nothing is built from an archive's manifest";
    for (const ReportMaster& master : model.masters)
    {
        EXPECT_FALSE(master.baseline.has_value()) << master.name.toStdString();
    }
    ASSERT_EQ(model.history.size(), 2u);
    EXPECT_EQ(model.history.front().runId, QStringLiteral("20200101_000000_000")) << "oldest first";
    for (const ReportHistoryEntry& entry : model.history)
    {
        EXPECT_FALSE(entry.refusal.isEmpty()) << entry.runId.toStdString();
    }
}

TEST(ReportComparison, ABaselineOfAnotherShapeIsSetSideBySide)
{
    const auto scenario = archived(ReportScenarioId::FiveFiltersAfterEarlierRun);
    ASSERT_TRUE(scenario.has_value());
    const QString earlier = scenario->earlierRunDirectories.front();

    const ReportModel same = buildScenarioModel(*scenario, scenario->runDirectory);
    for (const ReportMaster& master : same.masters)
    {
        ASSERT_TRUE(master.baseline.has_value()) << master.name.toStdString();
        EXPECT_FALSE(master.baseline->sideBySide) << master.name.toStdString();
    }

    // The earlier run's luminance master was of another shape: twice as wide, as a mosaic's would be.
    ReportModel stored = storedReport(earlier);
    for (ReportMaster& master : stored.masters)
    {
        if (master.filter == QStringLiteral("Luminance") && master.kind == ReportMasterKind::Stack)
        {
            ASSERT_TRUE(master.statistics.has_value());
            master.statistics->width *= 2u;
        }
        else if (master.filter == QStringLiteral("Red"))
        {
            // Half a percent wider is the same shape.
            ASSERT_TRUE(master.statistics.has_value());
            master.statistics->width = 1005u;
            master.statistics->height = 750u;
        }
    }
    writeStoredReport(earlier, stored);

    const ReportModel model = buildScenarioModel(*scenario, scenario->runDirectory);
    for (const ReportMaster& master : model.masters)
    {
        SCOPED_TRACE(master.name.toStdString());
        ASSERT_TRUE(master.baseline.has_value());
        EXPECT_EQ(master.baseline->sideBySide,
                  master.filter == QStringLiteral("Luminance") && master.kind == ReportMasterKind::Stack);
    }
}

// ---- The run as a whole beside the latest earlier run that completed.

TEST(ReportComparison, TheRunIsComparedWithTheLatestEarlierRunThatCompleted)
{
    const auto scenario = archived(ReportScenarioId::FiveFiltersAfterEarlierRun);
    ASSERT_TRUE(scenario.has_value());
    ASSERT_EQ(scenario->earlierRunDirectories.size(), 1);
    const ReportModel earlier = storedReport(scenario->earlierRunDirectories.front());

    const ReportModel model = buildScenarioModel(*scenario, scenario->runDirectory);

    ASSERT_TRUE(model.comparison.has_value());
    const ReportComparison& comparison = *model.comparison;
    EXPECT_EQ(comparison.baselineRunId, runIdOf(scenario->earlierRunDirectories.front()));
    EXPECT_NE(comparison.baselineRunId, runIdOf(scenario->runDirectory)) << "a run is not its own baseline";
    EXPECT_EQ(comparison.baselineArchivedAt, earlier.run.archivedAt);
    EXPECT_TRUE(comparison.baselineArchivedAt.has_value());

    // This run's totals are its model's own figures.
    EXPECT_EQ(comparison.current.frames, model.run.frames);
    EXPECT_EQ(comparison.current.frames, 174);
    EXPECT_EQ(comparison.current.integrationMs, model.run.capturedMs);
    EXPECT_EQ(comparison.current.framesUsed, model.run.framesUsed);
    EXPECT_EQ(comparison.current.integratedMs, model.run.integratedMs);
    EXPECT_EQ(comparison.current.masters, 6);
    qint64 bytes = 0;
    for (const ReportMaster& master : model.masters)
    {
        bytes += master.bytes.value_or(0);
    }
    EXPECT_EQ(comparison.current.outputBytes, bytes);
    ASSERT_TRUE(comparison.current.snr.has_value());
    EXPECT_GT(*comparison.current.snr, 0.0);
    ASSERT_TRUE(comparison.current.overlapRetained.has_value());
    EXPECT_LE(*comparison.current.overlapRetained, 1.0);

    // The earlier run was of the first night alone; its totals are the ones its report states.
    EXPECT_EQ(comparison.previous.frames, 87);
    EXPECT_EQ(comparison.previous.frames, earlier.run.frames);
    EXPECT_EQ(comparison.previous.integrationMs, earlier.run.capturedMs);
    EXPECT_EQ(comparison.previous.framesUsed, earlier.run.framesUsed);
    EXPECT_EQ(comparison.previous.integratedMs, earlier.run.integratedMs);
    EXPECT_LT(comparison.previous.integrationMs, comparison.current.integrationMs);
    EXPECT_EQ(comparison.previous.masters, 6);
    EXPECT_GT(comparison.previous.outputBytes, 0);
    EXPECT_TRUE(comparison.previous.snr.has_value());

    EXPECT_EQ(comparison.previousApplicationVersion, earlier.run.applicationVersion);
    EXPECT_EQ(comparison.previousFingerprint, earlier.run.fingerprint);
    EXPECT_EQ(comparison.previousSettingsFingerprint, earlier.run.settingsFingerprint);
    EXPECT_EQ(comparison.previousInputsFingerprint, earlier.run.inputsFingerprint);
    EXPECT_NE(comparison.previousInputsFingerprint, model.run.inputsFingerprint) << "the lights differ";
}

TEST(ReportComparison, AnEarlierRunThatDidNotCompleteIsNoBaselineForTheRun)
{
    const auto scenario = archived(ReportScenarioId::FiveFiltersAfterEarlierRun);
    ASSERT_TRUE(scenario.has_value());
    const QString earlier = scenario->earlierRunDirectories.front();
    ReportModel stored = storedReport(earlier);
    stored.run.outcome = ReportOutcome::Partial;
    writeStoredReport(earlier, stored);

    const ReportModel model = buildScenarioModel(*scenario, scenario->runDirectory);

    EXPECT_FALSE(model.comparison.has_value());
    // The masters it did make are still the latest of their kind.
    const ReportMaster* luminance = masterOfFilter(model, QStringLiteral("Luminance"));
    ASSERT_NE(luminance, nullptr);
    ASSERT_TRUE(luminance->baseline.has_value());
    EXPECT_EQ(luminance->baseline->runId, runIdOf(earlier));
    ASSERT_EQ(model.history.size(), 1u);
    EXPECT_EQ(model.history.front().outcome, ReportOutcome::Partial);
}

TEST(ReportComparison, ARunWithNoEarlierRunComparesNothing)
{
    const auto scenario = archived(ReportScenarioId::FiveFilters);
    ASSERT_TRUE(scenario.has_value());
    ASSERT_TRUE(scenario->earlierRunDirectories.isEmpty());

    const ReportModel model = buildScenarioModel(*scenario, scenario->runDirectory);

    EXPECT_FALSE(model.comparison.has_value());
    EXPECT_TRUE(model.history.empty());
    for (const ReportMaster& master : model.masters)
    {
        EXPECT_FALSE(master.baseline.has_value()) << master.name.toStdString();
    }
}

TEST(ReportComparison, ARunThatHasNoFolderOfItsOwnIsComparedWithNothing)
{
    const auto scenario = built(ReportScenarioId::FiveFiltersAfterEarlierRun);
    ASSERT_TRUE(scenario.has_value());

    const ReportModel model = buildScenarioModel(*scenario);

    EXPECT_FALSE(model.comparison.has_value());
    EXPECT_TRUE(model.history.empty());
    for (const ReportMaster& master : model.masters)
    {
        EXPECT_FALSE(master.baseline.has_value()) << master.name.toStdString();
    }
}

TEST(ReportComparison, TheRunBeingWrittenAndTheRunsAfterItAreNeverRead)
{
    const auto scenario = archived(ReportScenarioId::FiveFiltersAfterEarlierRun);
    ASSERT_TRUE(scenario.has_value());
    const ReportModel before = buildScenarioModel(*scenario, scenario->runDirectory);

    // The run's own folder keeps a report - the one written of it a moment ago - and a later run keeps one too.
    ASSERT_TRUE(storeWorkflowReport(before, scenario->runDirectory).isStored());
    const QString later = QFileInfo(scenario->runDirectory).dir().filePath(QStringLiteral("20991231_235959_999"));
    writeStoredReport(later, before);

    const ReportModel model = buildScenarioModel(*scenario, scenario->runDirectory);

    ASSERT_TRUE(model.comparison.has_value());
    EXPECT_EQ(model.comparison->baselineRunId, runIdOf(scenario->earlierRunDirectories.front()));
    ASSERT_EQ(model.history.size(), 1u);
    EXPECT_EQ(model.history.front().runId, runIdOf(scenario->earlierRunDirectories.front()));
    EXPECT_EQ(model.comparison, before.comparison);
}

TEST(ReportComparison, AnEarlierRunsFiguresAreTheOnesItsReportStatesWhateverTheOutputFolderHoldsNow)
{
    const auto scenario = archived(ReportScenarioId::FiveFiltersAfterTwoEarlierRuns);
    ASSERT_TRUE(scenario.has_value());
    const ReportModel model = buildScenarioModel(*scenario, scenario->runDirectory);

    // The same run reported with no metrics database: the output folder's database is this run's, and no figure
    // of an earlier run is read from it.
    const ProductionReportSources production(scenario->session(), QString());
    const ReportModel withoutMetrics = buildReportModel(ReportModelInputs{.manifest = scenario->manifest,
                                                                          .result = scenario->result,
                                                                          .runDirectory = scenario->runDirectory,
                                                                          .context = scenario->context,
                                                                          .counters = std::nullopt},
                                                        production.sources());

    ASSERT_EQ(withoutMetrics.masters.size(), model.masters.size());
    for (std::size_t index = 0; index < model.masters.size(); ++index)
    {
        SCOPED_TRACE(model.masters[index].name.toStdString());
        EXPECT_EQ(withoutMetrics.masters[index].baseline, model.masters[index].baseline);
    }
    ASSERT_TRUE(withoutMetrics.comparison.has_value());
    EXPECT_EQ(withoutMetrics.comparison->previous, model.comparison->previous);
    EXPECT_EQ(withoutMetrics.history, model.history);
}

// ---- What changed.

TEST(ReportComparison, AChangedSettingIsFoundByStageGroupAndNameAndAnUnchangedOneIsNotListed)
{
    const auto scenario = archived(ReportScenarioId::FiveFiltersAfterTwoEarlierRuns);
    ASSERT_TRUE(scenario.has_value());

    const ReportModel model = buildScenarioModel(*scenario, scenario->runDirectory);

    ASSERT_TRUE(model.comparison.has_value());
    const ReportComparison& comparison = *model.comparison;
    // The broadband stacks were weighed by noise in the earlier run and are weighed by star width in this one.
    for (const QString& filter :
         {QStringLiteral("Luminance"), QStringLiteral("Red"), QStringLiteral("Green"), QStringLiteral("Blue")})
    {
        SCOPED_TRACE(filter.toStdString());
        const ReportMaster* master = masterOfFilter(model, filter);
        ASSERT_NE(master, nullptr);
        const auto change =
            std::find_if(comparison.settingsChanged.begin(), comparison.settingsChanged.end(),
                         [&](const ReportSettingChange& candidate) {
                             return candidate.groupKey == master->key && candidate.name == QStringLiteral("Weighting");
                         });
        ASSERT_NE(change, comparison.settingsChanged.end());
        EXPECT_EQ(change->stage, QStringLiteral("Stacking"));
        EXPECT_EQ(change->block, QStringLiteral("Stacking"));
        EXPECT_EQ(change->group, master->name);
        EXPECT_EQ(change->before, QStringLiteral("Noise"));
        EXPECT_EQ(change->after, QStringLiteral("Weighted FWHM"));
    }

    // A setting both runs state the same is counted and not listed.
    EXPECT_GT(comparison.settingsCompared, static_cast<int>(comparison.settingsChanged.size()));
    QStringList ofTheRun;
    for (const ReportSettingChange& change : comparison.settingsChanged)
    {
        EXPECT_NE(change.before, change.after)
            << change.stage.toStdString() << " / " << change.group.toStdString() << " / " << change.name.toStdString();
        EXPECT_NE(change.stage, QStringLiteral("Registration")) << "the registration's settings did not change";
        if (change.stage == QStringLiteral("The run"))
        {
            EXPECT_TRUE(change.groupKey.isEmpty());
            EXPECT_TRUE(change.group.isEmpty());
            ofTheRun << change.name;
        }
    }
    // Of the settings stated once for the run, one differs: the earlier run combined no colour image.
    EXPECT_EQ(ofTheRun, QStringList{QStringLiteral("Combine Red, Green and Blue Masters")});

    // The hydrogen-alpha master, its registration group and its dark are new: the earlier run had none of them,
    // so their settings are no change.
    const ReportMaster* hydrogenAlpha = masterOfFilter(model, QStringLiteral("Ha"));
    ASSERT_NE(hydrogenAlpha, nullptr);
    const auto isNew = [&comparison](const QString& stage, const QString& key) {
        return std::any_of(comparison.groupsChanged.begin(), comparison.groupsChanged.end(),
                           [&](const ReportGroupChange& change) {
                               return change.added && change.stage == stage && change.groupKey == key;
                           });
    };
    EXPECT_TRUE(isNew(QStringLiteral("Stacking"), hydrogenAlpha->key));
    EXPECT_TRUE(isNew(QStringLiteral("Registration"), hydrogenAlpha->registrationGroupKey));
    EXPECT_TRUE(std::none_of(comparison.settingsChanged.begin(), comparison.settingsChanged.end(),
                             [&](const ReportSettingChange& change) { return change.groupKey == hydrogenAlpha->key; }));
    EXPECT_TRUE(std::none_of(comparison.groupsChanged.begin(), comparison.groupsChanged.end(),
                             [](const ReportGroupChange& change) { return !change.added; }))
        << "this run has every group the earlier one had";
}

TEST(ReportComparison, AGroupTheEarlierRunHadAndThisOneHasNotIsNamed)
{
    const auto scenario = archived(ReportScenarioId::FiveFiltersAfterEarlierRun);
    ASSERT_TRUE(scenario.has_value());
    const QString earlier = scenario->earlierRunDirectories.front();
    ReportModel stored = storedReport(earlier);
    ASSERT_FALSE(stored.masters.empty());
    ReportMaster gone = stored.masters.front();
    gone.key = QStringLiteral("64x48|-|-|-|Oiii|M 20|-");
    gone.name = QStringLiteral("Oiii");
    stored.masters.push_back(gone);
    writeStoredReport(earlier, stored);

    const ReportModel model = buildScenarioModel(*scenario, scenario->runDirectory);

    ASSERT_TRUE(model.comparison.has_value());
    const auto& changes = model.comparison->groupsChanged;
    const auto named = std::find_if(changes.begin(), changes.end(),
                                    [&gone](const ReportGroupChange& change) { return change.groupKey == gone.key; });
    ASSERT_NE(named, changes.end());
    EXPECT_FALSE(named->added);
    EXPECT_EQ(named->stage, QStringLiteral("Stacking"));
    EXPECT_EQ(named->group, QStringLiteral("Oiii"));
}

TEST(ReportComparison, TheLightsAGroupGainedAndLostAreCountedPerGroup)
{
    const auto scenario = archived(ReportScenarioId::FiveFiltersAfterTwoEarlierRuns);
    ASSERT_TRUE(scenario.has_value());

    const ReportModel model = buildScenarioModel(*scenario, scenario->runDirectory);

    ASSERT_TRUE(model.comparison.has_value());
    const auto changeOf = [&model](const QString& filter) -> const ReportInputChange* {
        const ReportMaster* master = masterOfFilter(model, filter);
        if (master == nullptr)
        {
            return nullptr;
        }
        const auto& changes = model.comparison->inputsChanged;
        const auto found = std::find_if(changes.begin(), changes.end(), [master](const ReportInputChange& change) {
            return change.groupKey == master->key && change.stage == QStringLiteral("Stacking");
        });
        return found == changes.end() ? nullptr : &*found;
    };

    // The earlier run took 34 luminance lights of the first night; this one takes 32 of them and 28 of the second.
    const ReportInputChange* luminance = changeOf(QStringLiteral("Luminance"));
    ASSERT_NE(luminance, nullptr);
    EXPECT_EQ(luminance->added.size(), 28);
    EXPECT_EQ(luminance->removed.size(), 2);
    EXPECT_FALSE(luminance->group.isEmpty());
    for (const QString& name : luminance->added)
    {
        EXPECT_FALSE(name.contains(QLatin1Char('/'))) << "a frame is named by its file";
    }

    // The red master's lights of the first night are the ones the earlier run had; the second night's are new.
    const ReportInputChange* red = changeOf(QStringLiteral("Red"));
    ASSERT_NE(red, nullptr);
    EXPECT_EQ(red->added.size(), 16);
    EXPECT_TRUE(red->removed.isEmpty());

    // A group new in this run gained nothing: it is named among the groups, not among the frames.
    EXPECT_EQ(changeOf(QStringLiteral("Ha")), nullptr);
}

// ---- The runs of the folder.

TEST(ReportComparison, TheRunsOfTheFolderAreListedOldestFirstAsTheirReportsStateThem)
{
    const auto scenario = archived(ReportScenarioId::FiveFiltersAfterTwoEarlierRuns);
    ASSERT_TRUE(scenario.has_value());

    const ReportModel model = buildScenarioModel(*scenario, scenario->runDirectory);

    ASSERT_EQ(model.history.size(), 3u);
    const ReportMaster* luminance = masterOfFilter(model, QStringLiteral("Luminance"));
    ASSERT_NE(luminance, nullptr);
    for (int index = 0; index < 3; ++index)
    {
        const QString directory = scenario->earlierRunDirectories.at(index);
        const ReportHistoryEntry& entry = model.history[static_cast<std::size_t>(index)];
        SCOPED_TRACE(entry.runId.toStdString());
        EXPECT_EQ(entry.runId, runIdOf(directory));
        // The run's own report, as a link from this run's folder. A report an archive holds is a file like any
        // other and opens, whether or not the archive keeps the model it was written from.
        EXPECT_EQ(entry.report, QStringLiteral("../%1/workflow_report.html").arg(entry.runId));
        EXPECT_TRUE(QFileInfo::exists(QDir(scenario->runDirectory).filePath(entry.report)));
        if (index == 2)
        {
            EXPECT_FALSE(entry.refusal.isEmpty());
            EXPECT_FALSE(entry.frames.has_value());
            continue;
        }
        const ReportModel stored = storedReport(directory);
        EXPECT_TRUE(entry.refusal.isEmpty());
        EXPECT_EQ(entry.archivedAt, stored.run.archivedAt);
        EXPECT_EQ(entry.outcome, stored.run.outcome);
        EXPECT_EQ(entry.frames, stored.run.frames);
        EXPECT_EQ(entry.framesUsed, stored.run.framesUsed);
        EXPECT_EQ(entry.integrationMs, stored.run.integratedMs);
        // The noise is of one master over the runs: the first this run lists.
        const ReportMaster* same = masterOfFilter(stored, QStringLiteral("Luminance"));
        ASSERT_NE(same, nullptr);
        EXPECT_EQ(entry.backgroundNoise, same->snapshot.backgroundNoise);
    }
    // The first run made five masters, its colour image among them; the second had four, three of them reused.
    EXPECT_EQ(model.history[0].masters, 5);
    EXPECT_EQ(model.history[1].masters, 4);
    ASSERT_TRUE(model.history[0].archivedAt.has_value());
    ASSERT_TRUE(model.history[1].archivedAt.has_value());
    EXPECT_LT(*model.history[0].archivedAt, *model.history[1].archivedAt);
}

// ---- The earlier run's pictures.

TEST(ReportComparison, TheEarlierRunsPicturesAreCopiedIntoTheRunsFolderWhenItsReportIsStored)
{
    const auto scenario = archived(ReportScenarioId::FiveFiltersAfterTwoEarlierRuns);
    ASSERT_TRUE(scenario.has_value());
    const QDir run(scenario->runDirectory);
    const QDir history = QFileInfo(scenario->runDirectory).dir();

    // The archiver stored the run's report as it committed the run, the copies with it. They are taken away, so
    // that what building a model does and what storing a report does are seen apart.
    for (const ReportMaster& master : storedReport(scenario->runDirectory).masters)
    {
        if (master.baseline.has_value() && master.baseline->made)
        {
            ASSERT_TRUE(QFile::remove(run.filePath(master.baseline->preview))) << master.name.toStdString();
            ASSERT_TRUE(QFile::remove(run.filePath(master.baseline->detail))) << master.name.toStdString();
        }
    }
    const ReportModel model = buildScenarioModel(*scenario, scenario->runDirectory);
    int pictured = 0;
    for (const ReportMaster& master : model.masters)
    {
        if (!master.baseline.has_value() || !master.baseline->made)
        {
            continue;
        }
        SCOPED_TRACE(master.name.toStdString());
        ++pictured;
        EXPECT_EQ(master.baseline->preview,
                  QStringLiteral("report_previews/%1/previous_master.jpg").arg(master.previewFolder));
        EXPECT_EQ(master.baseline->detail,
                  QStringLiteral("report_previews/%1/previous_detail.jpg").arg(master.previewFolder));
        EXPECT_FALSE(QFileInfo::exists(run.filePath(master.baseline->preview))) << "building a model writes nothing";
    }
    EXPECT_EQ(pictured, 5);

    const WorkflowReportStored stored = storeWorkflowReport(model, scenario->runDirectory);
    ASSERT_TRUE(stored.isStored()) << stored.error.toStdString();
    EXPECT_TRUE(stored.picturesNotCopied.isEmpty());
    EXPECT_TRUE(stored.faults.isEmpty()) << stored.faults.join(QLatin1Char('\n')).toStdString();
    EXPECT_TRUE(QFileInfo::exists(stored.modelPath));
    EXPECT_TRUE(QFileInfo::exists(stored.reportPath));

    for (const ReportMaster& master : model.masters)
    {
        if (!master.baseline.has_value() || !master.baseline->made)
        {
            continue;
        }
        SCOPED_TRACE(master.name.toStdString());
        const ReportModel earlier = storedReport(history.filePath(master.baseline->runId));
        const auto made = std::find_if(earlier.masters.begin(), earlier.masters.end(),
                                       [&master](const ReportMaster& other) { return other.key == master.key; });
        ASSERT_NE(made, earlier.masters.end());
        const QDir archive(history.filePath(master.baseline->runId));
        const QByteArray whole = bytesOf(run.filePath(master.baseline->preview));
        const QByteArray detail = bytesOf(run.filePath(master.baseline->detail));
        ASSERT_FALSE(whole.isEmpty());
        ASSERT_FALSE(detail.isEmpty());
        // The copies are that run's own pictures, which keep this report whole once the history drops that run.
        EXPECT_EQ(whole, bytesOf(archive.filePath(made->preview)));
        EXPECT_EQ(detail, bytesOf(archive.filePath(made->detail)));
    }
    // The model the archive keeps states the copies.
    EXPECT_EQ(storedReport(scenario->runDirectory), model);
}

TEST(ReportComparison, APictureThatCannotBeCopiedIsNotStatedInTheReportStored)
{
    const auto scenario = archived(ReportScenarioId::FiveFiltersAfterEarlierRun);
    ASSERT_TRUE(scenario.has_value());
    const ReportModel model = buildScenarioModel(*scenario, scenario->runDirectory);
    const ReportMaster* luminance = masterOfFilter(model, QStringLiteral("Luminance"));
    ASSERT_NE(luminance, nullptr);
    ASSERT_TRUE(luminance->baseline.has_value());
    ASSERT_FALSE(luminance->baseline->detail.isEmpty());

    // The earlier run's detail of the luminance master goes away, and the run holds no copy of it yet.
    const ReportModel earlier = storedReport(scenario->earlierRunDirectories.front());
    const ReportMaster* made = masterOfFilter(earlier, QStringLiteral("Luminance"));
    ASSERT_NE(made, nullptr);
    ASSERT_TRUE(QFile::remove(QDir(scenario->earlierRunDirectories.front()).filePath(made->detail)));
    ASSERT_TRUE(QFile::remove(QDir(scenario->runDirectory).filePath(luminance->baseline->detail)));

    const WorkflowReportStored stored = storeWorkflowReport(model, scenario->runDirectory);

    ASSERT_TRUE(stored.isStored()) << "a picture that is gone does not keep a report from being stored";
    ASSERT_EQ(stored.picturesNotCopied.size(), 1);
    EXPECT_TRUE(stored.picturesNotCopied.front().contains(QStringLiteral("previous_detail.jpg")));
    const ReportModel kept = storedReport(scenario->runDirectory);
    const ReportMaster* keptLuminance = masterOfFilter(kept, QStringLiteral("Luminance"));
    ASSERT_NE(keptLuminance, nullptr);
    ASSERT_TRUE(keptLuminance->baseline.has_value());
    EXPECT_TRUE(keptLuminance->baseline->detail.isEmpty());
    EXPECT_EQ(keptLuminance->baseline->preview, luminance->baseline->preview);
    EXPECT_FALSE(QFileInfo::exists(QDir(scenario->runDirectory).filePath(luminance->baseline->detail)));
    EXPECT_TRUE(QFileInfo::exists(QDir(scenario->runDirectory).filePath(luminance->baseline->preview)));

    // A copy the run already holds stands when the earlier run goes away altogether: a report is stored more than
    // once, and the history may drop a run in between.
    ASSERT_TRUE(QDir(scenario->earlierRunDirectories.front()).removeRecursively());
    const WorkflowReportStored again = storeWorkflowReport(model, scenario->runDirectory);
    ASSERT_TRUE(again.isStored());
    ASSERT_EQ(again.picturesNotCopied.size(), 1) << again.picturesNotCopied.join(QLatin1Char('\n')).toStdString();
    const ReportModel keptAgain = storedReport(scenario->runDirectory);
    const ReportMaster* luminanceAgain = masterOfFilter(keptAgain, QStringLiteral("Luminance"));
    ASSERT_NE(luminanceAgain, nullptr);
    ASSERT_TRUE(luminanceAgain->baseline.has_value());
    EXPECT_EQ(luminanceAgain->baseline->preview, luminance->baseline->preview);
    // And the run that is gone is listed with no report to open.
    for (const ReportHistoryEntry& entry : keptAgain.history)
    {
        EXPECT_TRUE(entry.report.isEmpty()) << entry.runId.toStdString();
    }
}

// ---- Why a light's registration failed.

namespace
{
/// The lights of a model whose registration failed.
std::vector<const ReportFrame*> failedRegistrations(const ReportModel& model)
{
    std::vector<const ReportFrame*> failed;
    for (const ReportMaster& master : model.masters)
    {
        for (const ReportFrame& frame : master.frames)
        {
            if (frame.state == ReportFrameState::RegistrationFailed)
            {
                failed.push_back(&frame);
            }
        }
    }
    return failed;
}

bool saysWhyNot(const ReportModel& model)
{
    return std::any_of(model.notStated.begin(), model.notStated.end(), [](const ReportNotStated& entry) {
        return entry.what.contains(QStringLiteral("failed registration"));
    });
}
} // namespace

// A run records why its registration found no transform for a light, and the report states it. A run registers a
// light once: a later run that finds the registration still valid records nothing of it again, so the reason is
// then taken from the latest earlier run whose report states it; and with no such report it is not stated, and
// the model says so.
TEST(ReportComparison, WhyALightFailedRegistrationIsStatedByTheRunThatRegisteredIt)
{
    auto scenario = archived(ReportScenarioId::EveryLightState);
    ASSERT_TRUE(scenario.has_value());
    ASSERT_FALSE(scenario->result.registrationFailures.empty());
    const QString reason = QString::fromLatin1(kScenarioRegistrationFailure);

    // As the run recorded them.
    const ReportModel recorded = buildScenarioModel(*scenario, scenario->runDirectory);
    const std::vector<const ReportFrame*> failed = failedRegistrations(recorded);
    ASSERT_FALSE(failed.empty());
    for (const ReportFrame* frame : failed)
    {
        EXPECT_EQ(frame->registrationFailure, reason) << frame->fileName.toStdString();
        EXPECT_TRUE(frame->stateDetail.contains(reason)) << frame->stateDetail.toStdString();
    }
    EXPECT_FALSE(saysWhyNot(recorded));
    const auto issue = std::find_if(recorded.issues.begin(), recorded.issues.end(), [](const ReportIssue& candidate) {
        return candidate.kind == ReportIssueKind::RegistrationFailures;
    });
    ASSERT_NE(issue, recorded.issues.end());
    EXPECT_TRUE(issue->detail.contains(reason)) << issue->detail.toStdString();

    // A later run that registers nothing again: the reasons are the earlier run's.
    const QDir history = QFileInfo(scenario->runDirectory).dir();
    const QString earlier = history.filePath(QStringLiteral("20200101_000000_000"));
    writeStoredReport(earlier, recorded);
    scenario->result.registrationFailures.clear();
    const ReportModel later = buildScenarioModel(*scenario, scenario->runDirectory);
    ASSERT_EQ(failedRegistrations(later).size(), failed.size());
    for (const ReportFrame* frame : failedRegistrations(later))
    {
        EXPECT_EQ(frame->registrationFailure, reason) << frame->fileName.toStdString();
    }
    EXPECT_FALSE(saysWhyNot(later));

    // With no report that states them, the reasons are not stated, and nothing is put in their place.
    ASSERT_TRUE(QDir(earlier).removeRecursively());
    const ReportModel alone = buildScenarioModel(*scenario, scenario->runDirectory);
    ASSERT_EQ(failedRegistrations(alone).size(), failed.size());
    for (const ReportFrame* frame : failedRegistrations(alone))
    {
        EXPECT_TRUE(frame->registrationFailure.isEmpty()) << frame->fileName.toStdString();
        EXPECT_FALSE(frame->stateDetail.contains(reason));
    }
    EXPECT_TRUE(saysWhyNot(alone));
}
