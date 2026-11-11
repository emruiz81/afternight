// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The report a run's archive holds: the model the archiver builds once, as it commits the run, and the document
/// written from it; what the two final steps of a run state on that model; the report written again, and saved as
/// one file, from the model alone; and the bound the archiver puts on what it writes after it has decided whether a
/// run's payloads fit the history.

#include <gtest/gtest.h>

#include "Test_ImageIntegration/reporting/ReportScenarios.h"

#include "processes/ImageIntegration/ImageIntegrationConstants.h"
#include "processes/ImageIntegration/reporting/ReportArchiveReader.h"
#include "processes/ImageIntegration/reporting/ReportHtmlWriter.h"
#include "processes/ImageIntegration/reporting/ReportModel.h"
#include "processes/ImageIntegration/reporting/WorkflowReport.h"
#include "processes/ImageIntegration/workflow/WorkflowArchiver.h"
#include "processes/ImageIntegration/workflow/WorkflowExecutionCounters.h"
#include "processes/ImageIntegration/workflow/WorkflowRunArchive.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QThread>

#include <algorithm>
#include <iostream>
#include <memory>
#include <optional>

using namespace an::tests::integration::report;
using namespace ImageIntegration::Reporting;

namespace
{
const char kModelName[] = "workflow_report.json";
const char kDocumentName[] = "workflow_report.html";

/// A scenario, built and archived as the executor archives a run; nothing, with the case failed, when it could not
/// be.
[[nodiscard]] std::optional<ReportScenario> archived(const ReportScenarioId id)
{
    std::optional<ReportScenario> scenario = buildReportScenario(id);
    EXPECT_TRUE(scenario.has_value()) << reportScenarioName(id).toStdString();
    if (scenario.has_value() && !archiveScenarioRun(*scenario))
    {
        return std::nullopt;
    }
    return scenario;
}

/// The model a run's archive stores; nothing, with the case failed, when it gives none.
[[nodiscard]] std::optional<ReportModel> storedModel(const QString& runDirectory)
{
    ReportArchiveRead read = ReportArchiveReader::read(runDirectory);
    EXPECT_TRUE(read.model.has_value()) << read.why.toStdString();
    return std::move(read.model);
}

[[nodiscard]] QByteArray bytesOf(const QString& path)
{
    QFile file(path);
    EXPECT_TRUE(file.open(QIODevice::ReadOnly)) << path.toStdString();
    return file.readAll();
}

/// The document a model writes in the linked form, as the archive holds it.
[[nodiscard]] QString linkedDocumentOf(const ReportModel& model, const QString& runDirectory)
{
    return writeReportDocument(model, ReportDocumentOptions{.form = ReportForm::Linked, .runDirectory = runDirectory})
        .html;
}

[[nodiscard]] const ReportDiskUse* archiveOnDisk(const ReportModel& model)
{
    const auto found = std::find_if(model.run.disk.begin(), model.run.disk.end(),
                                    [](const ReportDiskUse& use) { return use.kind == QStringLiteral("The archive"); });
    return found == model.run.disk.end() ? nullptr : &*found;
}

[[nodiscard]] std::optional<std::uint64_t> counterOf(const ReportModel& model, const char* key)
{
    for (const ReportCounter& counter : model.run.counters)
    {
        if (counter.key == QLatin1String(key))
        {
            return counter.value;
        }
    }
    return std::nullopt;
}
} // namespace

// A run that is archived is given its report whatever way it ended, once it made a master: the model and the
// document, and no Markdown.
TEST(WorkflowArchiverReport, ARunThatMadeAMasterIsGivenItsModelAndItsDocumentHoweverItEnded)
{
    struct Ending
    {
        ReportScenarioId id;
        std::vector<ReportOutcome> outcomes;
    };
    for (const Ending& ending :
         {Ending{ReportScenarioId::OneMonoMaster, {ReportOutcome::Completed, ReportOutcome::CompletedWithWarnings}},
          Ending{ReportScenarioId::FailedInStacking, {ReportOutcome::Partial, ReportOutcome::Failed}},
          Ending{ReportScenarioId::CancelledAfterTwoMasters, {ReportOutcome::Cancelled}}})
    {
        SCOPED_TRACE(reportScenarioName(ending.id).toStdString());
        const std::optional<ReportScenario> scenario = archived(ending.id);
        ASSERT_TRUE(scenario.has_value());
        const QDir run(scenario->runDirectory);
        EXPECT_TRUE(run.exists(QLatin1String(kModelName)));
        EXPECT_TRUE(run.exists(QLatin1String(kDocumentName)));
        EXPECT_FALSE(run.exists(QStringLiteral("workflow_report.md")));

        const std::optional<ReportModel> model = storedModel(scenario->runDirectory);
        ASSERT_TRUE(model.has_value());
        EXPECT_NE(std::find(ending.outcomes.begin(), ending.outcomes.end(), model->run.outcome), ending.outcomes.end())
            << static_cast<int>(model->run.outcome);
        EXPECT_EQ(static_cast<int>(model->masters.size()), scenario->facts.mastersPlanned);
        // The document is the one the stored model writes.
        EXPECT_TRUE(QString::fromUtf8(bytesOf(run.filePath(QLatin1String(kDocumentName)))) ==
                    linkedDocumentOf(*model, scenario->runDirectory));
    }
}

// What a run knows only after its commit - its own phases and total, the diagnostics at the end, the engine's
// memory, what its folder in the history holds - is stated on the model the archive keeps, and both files are
// written again; then the log, once it is whole.
TEST(WorkflowArchiverReport, TheFinalStepsStateTheRunsLastFiguresAndItsWholeLogInBothFiles)
{
    std::optional<ReportScenario> scenario = buildReportScenario(ReportScenarioId::OneMonoMaster);
    ASSERT_TRUE(scenario.has_value());
    WorkflowExecutionCounterAccumulator counters;
    const std::unique_ptr<IWorkflowRunArchive> archive =
        makeWorkflowArchiverRunArchive(scenario->outputDirectory, &counters, scenario->session());
    scenario->result.capturedLog = QStringLiteral("the log as it stood when the run was committed");

    const IWorkflowRunArchive::CommitOutcome committed =
        archive->commit(scenario->manifest, scenario->result, []() { return qint64{7}; });
    ASSERT_TRUE(committed.success) << committed.error.toStdString();
    const QDir run(scenario->runDirectory);
    EXPECT_EQ(QDir::cleanPath(committed.reportPath), QDir::cleanPath(run.filePath(QLatin1String(kDocumentName))));
    EXPECT_GT(committed.reportMs, 0);
    {
        const std::optional<ReportModel> atCommit = storedModel(scenario->runDirectory);
        ASSERT_TRUE(atCommit.has_value());
        EXPECT_FALSE(atCommit->run.engineMemory.has_value());
        EXPECT_EQ(archiveOnDisk(*atCommit), nullptr) << "the archive is measured once it is whole";
        EXPECT_TRUE(atCommit->run.log.contains(QStringLiteral("when the run was committed")));
    }

    // The executor's last figures.
    counters.addBytesCopied(123456);
    WorkflowExecutor::Result final = scenario->result;
    final.executorTiming.captured = true;
    final.executorTiming.stageExecutionMs = 61000;
    final.executorTiming.stateCommitMs = 210;
    final.executorTiming.archiveMs = 1900;
    final.executorTiming.reportMs = 870;
    final.totalDurationMs = static_cast<int>(final.executorTiming.exclusivePhasesTotalMs());
    final.executionCounters = counters.snapshot();
    final.engineMemory = WorkflowExecutor::Result::EngineMemory{.peakBytes = 3ull << 30,
                                                                .peakBytesBeforeRun = 1ull << 20,
                                                                .liveBytesAtEnd = 4096,
                                                                .allocations = 9001,
                                                                .bytesAllocated = 7ull << 30,
                                                                .deniedAllocations = 2};
    archive->finalizeTimingArtifacts(final);

    const std::optional<ReportModel> timed = storedModel(scenario->runDirectory);
    ASSERT_TRUE(timed.has_value());
    ASSERT_TRUE(timed->run.executor.has_value());
    EXPECT_EQ(timed->run.executor->stageExecutionMs, 61000);
    EXPECT_EQ(timed->run.executor->stateCommitMs, 210);
    EXPECT_EQ(timed->run.executor->archiveMs, 1900);
    EXPECT_EQ(timed->run.executor->reportMs, 870);
    EXPECT_EQ(timed->run.processingMs, final.totalDurationMs);
    EXPECT_GE(counterOf(*timed, "bytesCopied").value_or(0), 123456u);
    ASSERT_TRUE(timed->run.engineMemory.has_value());
    EXPECT_EQ(timed->run.engineMemory->peakBytes, 3ull << 30);
    EXPECT_EQ(timed->run.engineMemory->allocations, 9001u);
    EXPECT_EQ(timed->run.engineMemory->deniedAllocations, 2u);
    const ReportDiskUse* onDisk = archiveOnDisk(*timed);
    ASSERT_NE(onDisk, nullptr);
    EXPECT_GT(onDisk->files, 2);
    EXPECT_GT(onDisk->bytes, 0);
    const QString timedDocument = QString::fromUtf8(bytesOf(run.filePath(QLatin1String(kDocumentName))));
    EXPECT_TRUE(timedDocument == linkedDocumentOf(*timed, scenario->runDirectory));
    EXPECT_FALSE(timedDocument.contains(QStringLiteral("AFTERNIGHT_")));
    EXPECT_FALSE(timedDocument.contains(QStringLiteral("{{")));
    // Stated a second time, the archive is still one row.
    archive->finalizeTimingArtifacts(final);
    const std::optional<ReportModel> again = storedModel(scenario->runDirectory);
    ASSERT_TRUE(again.has_value());
    EXPECT_EQ(std::count_if(again->run.disk.begin(), again->run.disk.end(),
                            [](const ReportDiskUse& use) { return use.kind == QStringLiteral("The archive"); }),
              1);

    // The log, whole.
    final.capturedLog = scenario->result.capturedLog + QStringLiteral("\nWORKFLOW COMPLETED: the last line of the log");
    archive->finalizeLogArtifact(final);
    const std::optional<ReportModel> logged = storedModel(scenario->runDirectory);
    ASSERT_TRUE(logged.has_value());
    EXPECT_TRUE(logged->run.log.endsWith(QStringLiteral("the last line of the log")));
    const QString loggedDocument = QString::fromUtf8(bytesOf(run.filePath(QLatin1String(kDocumentName))));
    EXPECT_TRUE(loggedDocument.contains(QStringLiteral("the last line of the log")));
    EXPECT_TRUE(loggedDocument == linkedDocumentOf(*logged, scenario->runDirectory));
    EXPECT_TRUE(QString::fromUtf8(bytesOf(run.filePath(QStringLiteral("workflow_log.txt"))))
                    .contains(QStringLiteral("the last line of the log")));
    // The final timing is still the one stated.
    ASSERT_TRUE(logged->run.executor.has_value());
    EXPECT_EQ(logged->run.executor->reportMs, 870);
}

// With no session the archiver reads no image: the report states what the run recorded, measures nothing, and says
// what it could not state and why.
TEST(WorkflowArchiverReport, ARunArchivedWithNoSessionIsGivenAReportThatMeasuresNothingAndSaysSo)
{
    const std::optional<ReportScenario> scenario = archived(ReportScenarioId::FiveFiltersWithoutSession);
    ASSERT_TRUE(scenario.has_value());
    ASSERT_EQ(scenario->session(), nullptr);
    const std::optional<ReportModel> model = storedModel(scenario->runDirectory);
    ASSERT_TRUE(model.has_value());
    EXPECT_EQ(static_cast<int>(model->masters.size()), scenario->facts.mastersPlanned);
    for (const ReportMaster& master : model->masters)
    {
        EXPECT_FALSE(master.statistics.has_value()) << master.name.toStdString();
        EXPECT_TRUE(master.distribution.empty()) << master.name.toStdString();
    }
    EXPECT_TRUE(std::any_of(model->notStated.begin(), model->notStated.end(), [](const ReportNotStated& entry) {
        return entry.why.contains(QStringLiteral("without an engine session"));
    }));
}

// A report that cannot be written does not undo a run's archive: the commit succeeds, names no report, and the final
// steps write what they can.
TEST(WorkflowArchiverReport, AReportThatCannotBeWrittenLeavesTheRunCommittedAndNamesNoReport)
{
    std::optional<ReportScenario> scenario = buildReportScenario(ReportScenarioId::OneMonoMaster);
    ASSERT_TRUE(scenario.has_value());
    scenario->result.capturedLog = QStringLiteral("a log, so that the log's writer is called");
    // The log is written before the report: a folder where the stored model belongs makes the report unwritable.
    WorkflowArchiver archiver(scenario->outputDirectory, nullptr, [](const QString& logPath, const QByteArray& bytes) {
        QFile log(logPath);
        if (!log.open(QIODevice::WriteOnly | QIODevice::Truncate) || log.write(bytes) != bytes.size())
        {
            return false;
        }
        log.close();
        return QDir().mkpath(QFileInfo(logPath).dir().filePath(QLatin1String(kModelName)));
    });
    archiver.setEngineSession(scenario->session());
    const WorkflowArchiver::ArchiveResult committed = archiver.commit(scenario->manifest, scenario->result);

    EXPECT_TRUE(committed.success) << committed.error.toStdString();
    EXPECT_TRUE(committed.reportPath.isEmpty());
    const QDir run(committed.archivePath);
    EXPECT_TRUE(run.exists(QStringLiteral("workflow_manifest.json")));
    EXPECT_FALSE(run.exists(QLatin1String(kDocumentName)));
    EXPECT_TRUE(QFileInfo(run.filePath(QLatin1String(kModelName))).isDir());
    EXPECT_EQ(scenario->result.integrationGroupResults.front().outputRetentionStatus,
              OutputRetentionStatus::RetainedInHistory);
}

// The history row's report action: the document written again from the model the archive keeps, and from nothing
// else.
TEST(WorkflowArchiverReport, AReportIsWrittenAgainFromTheStoredModelAndRefusedByNameWithoutOne)
{
    const std::optional<ReportScenario> scenario = archived(ReportScenarioId::OneMonoMaster);
    ASSERT_TRUE(scenario.has_value());
    const QDir run(scenario->runDirectory);
    const QString documentPath = run.filePath(QLatin1String(kDocumentName));
    const QByteArray asArchived = bytesOf(documentPath);
    ASSERT_FALSE(asArchived.isEmpty());
    const QByteArray storedBytes = bytesOf(run.filePath(QLatin1String(kModelName)));

    // Written again, it is the same document, and the stored model is not touched.
    ASSERT_TRUE(QFile::remove(documentPath));
    const WorkflowReportWritten again = rerenderWorkflowReport(scenario->runDirectory);
    ASSERT_TRUE(again.isWritten()) << again.error.toStdString();
    EXPECT_EQ(QDir::cleanPath(again.path), QDir::cleanPath(documentPath));
    EXPECT_TRUE(again.faults.isEmpty()) << again.faults.join(QLatin1Char('\n')).toStdString();
    EXPECT_EQ(bytesOf(documentPath), asArchived);
    EXPECT_EQ(bytesOf(run.filePath(QLatin1String(kModelName))), storedBytes);

    // A stored report of another format is refused by name, and the document is left as it was.
    QJsonObject other = QJsonDocument::fromJson(storedBytes).object();
    other.insert(QStringLiteral("reportSchema"), 999);
    {
        QFile file(run.filePath(QLatin1String(kModelName)));
        ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(QJsonDocument(other).toJson(QJsonDocument::Compact));
    }
    const WorkflowReportWritten otherFormat = rerenderWorkflowReport(scenario->runDirectory);
    EXPECT_FALSE(otherFormat.isWritten());
    EXPECT_TRUE(otherFormat.path.isEmpty());
    EXPECT_TRUE(otherFormat.error.contains(QStringLiteral("999"))) << otherFormat.error.toStdString();
    EXPECT_EQ(bytesOf(documentPath), asArchived);

    // An archive that keeps no stored report: refused by name, and nothing is written - no document, no model, no
    // Markdown. The run's manifest is there and is not read for one.
    ASSERT_TRUE(QFile::remove(run.filePath(QLatin1String(kModelName))));
    ASSERT_TRUE(QFile::remove(documentPath));
    ASSERT_TRUE(run.exists(QStringLiteral("workflow_manifest.json")));
    const WorkflowReportWritten none = rerenderWorkflowReport(scenario->runDirectory);
    EXPECT_FALSE(none.isWritten());
    EXPECT_TRUE(none.error.contains(QStringLiteral("keeps no stored report"))) << none.error.toStdString();
    EXPECT_FALSE(run.exists(QLatin1String(kDocumentName)));
    EXPECT_FALSE(run.exists(QLatin1String(kModelName)));
    EXPECT_FALSE(run.exists(QStringLiteral("workflow_report.md")));

    // A run whose folder is gone is named too.
    const WorkflowReportWritten gone = rerenderWorkflowReport(run.filePath(QStringLiteral("../no_such_run")));
    EXPECT_FALSE(gone.isWritten());
    EXPECT_TRUE(gone.error.contains(QStringLiteral("is not there"))) << gone.error.toStdString();
}

// The single file: written from the stored model to a path of the person's choosing, with nothing of the host's
// folders in it; a failure is named.
TEST(WorkflowArchiverReport, AReportIsSavedAsOneFileFromTheStoredModelAndAFailureIsNamed)
{
    const std::optional<ReportScenario> scenario = archived(ReportScenarioId::FiveFiltersAfterEarlierRun);
    ASSERT_TRUE(scenario.has_value());
    QTemporaryDir elsewhere;
    ASSERT_TRUE(elsewhere.isValid());
    const QString destination = QDir(elsewhere.path()).filePath(QStringLiteral("to share/five filters.html"));

    const WorkflowReportWritten saved = saveWorkflowReportAsOneFile(scenario->runDirectory, destination);
    ASSERT_TRUE(saved.isWritten()) << saved.error.toStdString();
    EXPECT_TRUE(saved.faults.isEmpty()) << saved.faults.join(QLatin1Char('\n')).toStdString();
    const QString document = QString::fromUtf8(bytesOf(destination));
    EXPECT_TRUE(document.contains(QStringLiteral("data:image/jpeg;base64,")));
    EXPECT_FALSE(document.contains(QStringLiteral("report_previews/")));
    EXPECT_FALSE(document.contains(QStringLiteral("workflow_report.html")))
        << "a single file links no other run's report";
    EXPECT_FALSE(document.contains(scenario->outputDirectory));
    EXPECT_FALSE(document.contains(QDir::toNativeSeparators(scenario->outputDirectory)));
    // The run's own folder holds what it held: the single file is never written into the history.
    EXPECT_FALSE(QDir(scenario->runDirectory).exists(QFileInfo(destination).fileName()));

    const WorkflowReportWritten nowhere = saveWorkflowReportAsOneFile(scenario->runDirectory, QString());
    EXPECT_FALSE(nowhere.isWritten());
    EXPECT_FALSE(nowhere.error.isEmpty());

    const QString other = QDir(elsewhere.path()).filePath(QStringLiteral("other.html"));
    ASSERT_TRUE(QFile::remove(QDir(scenario->runDirectory).filePath(QLatin1String(kModelName))));
    const WorkflowReportWritten refused = saveWorkflowReportAsOneFile(scenario->runDirectory, other);
    EXPECT_FALSE(refused.isWritten());
    EXPECT_TRUE(refused.error.contains(QStringLiteral("keeps no stored report"))) << refused.error.toStdString();
    EXPECT_FALSE(QFileInfo::exists(other));
}

// A report links the report of each earlier run of its folder. When the history drops a run, the report written
// next no longer links it: the run is still listed, and its report is no longer a file to open.
TEST(WorkflowArchiverReport, ARunTheHistoryDroppedIsListedWithoutALinkOnceTheReportIsWrittenAgain)
{
    const auto linksOf = [](const ReportModel& model) {
        QStringList links;
        for (const ReportHistoryEntry& entry : model.history)
        {
            if (!entry.report.isEmpty())
            {
                links.append(entry.report);
            }
        }
        return links;
    };

    const std::optional<ReportScenario> scenario = archived(ReportScenarioId::FiveFiltersAfterEarlierRun);
    ASSERT_TRUE(scenario.has_value());
    ASSERT_EQ(scenario->earlierRunDirectories.size(), 1);
    const QString earlier = scenario->earlierRunDirectories.front();
    const QString link = QStringLiteral("../%1/%2").arg(QFileInfo(earlier).fileName(), QLatin1String(kDocumentName));
    const QDir run(scenario->runDirectory);
    {
        const std::optional<ReportModel> model = storedModel(scenario->runDirectory);
        ASSERT_TRUE(model.has_value());
        EXPECT_TRUE(linksOf(*model).contains(link));
        EXPECT_TRUE(QString::fromUtf8(bytesOf(run.filePath(QLatin1String(kDocumentName)))).contains(link));
    }

    // The history drops the earlier run.
    ASSERT_TRUE(QDir(earlier).removeRecursively());
    const WorkflowReportWritten again = rerenderWorkflowReport(scenario->runDirectory);
    ASSERT_TRUE(again.isWritten()) << again.error.toStdString();
    const QString document = QString::fromUtf8(bytesOf(again.path));
    EXPECT_FALSE(document.contains(link));
    EXPECT_TRUE(document.contains(QFileInfo(earlier).fileName())) << "the run is still one of the folder's runs";
    // The pictures of the earlier run were copied when the report was stored, so the comparison is whole.
    const std::optional<ReportModel> model = storedModel(scenario->runDirectory);
    ASSERT_TRUE(model.has_value());
    int compared = 0;
    for (const ReportMaster& master : model->masters)
    {
        if (master.baseline.has_value() && master.baseline->made && !master.baseline->preview.isEmpty())
        {
            ++compared;
            EXPECT_TRUE(run.exists(master.baseline->preview)) << master.baseline->preview.toStdString();
        }
    }
    EXPECT_GT(compared, 0);
}

// The same, as an archiver whose history keeps one run does it: the commit of the second run drops the first, and
// the report the final step writes links no report that is gone.
TEST(WorkflowArchiverReport, TheReportOfARunWhoseCommitDroppedAnEarlierRunDoesNotLinkIt)
{
    std::optional<ReportScenario> scenario = buildReportScenario(ReportScenarioId::FiveFiltersAfterEarlierRun);
    ASSERT_TRUE(scenario.has_value());
    ASSERT_EQ(scenario->earlierRunDirectories.size(), 1);
    const QString earlier = scenario->earlierRunDirectories.front();
    ASSERT_TRUE(QFileInfo::exists(earlier));

    WorkflowArchiver archiver(scenario->outputDirectory, 1, qint64{4} * 1024 * 1024 * 1024);
    archiver.setEngineSession(scenario->session());
    const WorkflowArchiver::ArchiveResult committed = archiver.commit(scenario->manifest, scenario->result);
    ASSERT_TRUE(committed.success) << committed.error.toStdString();
    EXPECT_FALSE(QFileInfo::exists(earlier)) << "a history of one run keeps the run just committed";

    const std::optional<ReportModel> model = storedModel(committed.archivePath);
    ASSERT_TRUE(model.has_value());
    // The earlier run was read before it was dropped: it is among the folder's runs, and the masters are compared
    // with it.
    const QString earlierId = QFileInfo(earlier).fileName();
    const auto listed =
        std::find_if(model->history.begin(), model->history.end(),
                     [&earlierId](const ReportHistoryEntry& entry) { return entry.runId == earlierId; });
    ASSERT_NE(listed, model->history.end());
    EXPECT_TRUE(listed->report.isEmpty());
    EXPECT_FALSE(QString::fromUtf8(bytesOf(committed.reportPath)).contains(QStringLiteral("../%1/").arg(earlierId)));
    EXPECT_TRUE(std::any_of(model->masters.begin(), model->masters.end(), [&](const ReportMaster& master) {
        return master.baseline.has_value() && master.baseline->made && master.baseline->runId == earlierId &&
               QDir(committed.archivePath).exists(master.baseline->preview);
    }));
}

// The archiver decides whether a run's payloads fit the history before it writes the manifest, the log and the
// report, so it bounds them first. The bound holds what every scenario's archive then holds of them, the largest
// reports among them with room to spare.
TEST(WorkflowArchiverReport, TheBoundOnWhatAnArchiveWritesLastHoldsWhatEveryScenarioWrites)
{
    for (const ReportScenarioId id : allReportScenarios())
    {
        SCOPED_TRACE(reportScenarioName(id).toStdString());
        std::optional<ReportScenario> scenario = buildReportScenario(id);
        ASSERT_TRUE(scenario.has_value());
        if (!scenario->archivedByExecutor)
        {
            continue;
        }
        // The bound is taken of the run as the archiver sees it when it decides: before the commit.
        const qint64 bound = ImageIntegration::estimatedArchiveMetadataBytes(
            scenario->manifest.redactedForPersistence(), scenario->result);
        ASSERT_TRUE(archiveScenarioRun(*scenario));

        const QDir run(scenario->runDirectory);
        qint64 written = 0;
        for (const QString& name : {QStringLiteral("workflow_manifest.json"), QStringLiteral("workflow_log.txt"),
                                    QString::fromLatin1(kModelName), QString::fromLatin1(kDocumentName)})
        {
            written += QFileInfo(run.filePath(name)).size();
        }
        const qint64 report = QFileInfo(run.filePath(QLatin1String(kModelName))).size() +
                              QFileInfo(run.filePath(QLatin1String(kDocumentName))).size();
        ASSERT_GT(report, 0);
        EXPECT_GE(bound, written);
        int lights = static_cast<int>(scenario->manifest.leftOutLights.size());
        for (const auto& group : scenario->manifest.integrationGroups)
        {
            lights += static_cast<int>(group.lightFiles.size());
        }
        std::cout << "[archive-bound] " << reportScenarioName(id).toStdString() << ": " << lights << " lights, "
                  << scenario->manifest.integrationGroups.size() << " masters, report " << report / 1024
                  << " KiB, written " << written / 1024 << " KiB, bound " << bound / 1024 << " KiB ("
                  << (written > 0 ? static_cast<double>(bound) / static_cast<double>(written) : 0.0) << "x)\n";

        // The two runs the report's amounts are set by: twice what they write.
        if (id == ReportScenarioId::FiveFilters || id == ReportScenarioId::TwoThousandLights)
        {
            const qint64 amounts = ImageIntegration::Config::kReportArchiveBaseBytes +
                                   lights * ImageIntegration::Config::kReportArchiveBytesPerLight +
                                   static_cast<qint64>(scenario->manifest.integrationGroups.size()) *
                                       ImageIntegration::Config::kReportArchiveBytesPerMaster;
            EXPECT_GE(amounts, 2 * report) << "the report's amounts are less than twice what this run's report weighs";
        }
    }
}

// The history drops the run that started first, which is what a run's folder is named for, whatever was written
// into a folder since. Writing a run's report again replaces a file in its folder; that must not make the oldest
// run the newest, or the history would drop another run in its place.
TEST(WorkflowArchiverReport, TheHistoryDropsTheRunThatStartedFirstWhicheverFolderWasWrittenLast)
{
    std::optional<ReportScenario> scenario = buildReportScenario(ReportScenarioId::FiveFiltersAfterTwoEarlierRuns);
    ASSERT_TRUE(scenario.has_value());
    QStringList earlier = scenario->earlierRunDirectories;
    ASSERT_GE(earlier.size(), 2);
    std::sort(earlier.begin(), earlier.end(), [](const QString& left, const QString& right) {
        return QFileInfo(left).fileName() < QFileInfo(right).fileName();
    });
    const QString oldest = earlier.front();

    // Something is written into the oldest run's folder after every other run's folder was last changed.
    QDateTime latest;
    for (const QString& run : earlier)
    {
        const QDateTime changed = QFileInfo(run).lastModified();
        latest = latest.isValid() ? (std::max)(latest, changed) : changed;
    }
    bool writtenLast = false;
    for (int attempt = 0; attempt < 250 && !writtenLast; ++attempt)
    {
        QFile marker(QDir(oldest).filePath(QStringLiteral("written_again.tmp")));
        ASSERT_TRUE(marker.open(QIODevice::WriteOnly));
        marker.close();
        ASSERT_TRUE(marker.remove());
        writtenLast = QFileInfo(oldest).lastModified() > latest;
        if (!writtenLast)
        {
            QThread::msleep(20);
        }
    }
    ASSERT_TRUE(writtenLast) << "the folder's modification time did not move";

    // A history of as many runs as the folder already holds: committing one more drops exactly one.
    WorkflowArchiver archiver(scenario->outputDirectory, static_cast<int>(earlier.size()),
                              qint64{4} * 1024 * 1024 * 1024);
    archiver.setEngineSession(scenario->session());
    const WorkflowArchiver::ArchiveResult committed = archiver.commit(scenario->manifest, scenario->result);
    ASSERT_TRUE(committed.success) << committed.error.toStdString();

    EXPECT_FALSE(QFileInfo::exists(oldest)) << "the run that started first is the one the history drops";
    for (qsizetype run = 1; run < earlier.size(); ++run)
    {
        EXPECT_TRUE(QFileInfo::exists(earlier[run])) << QFileInfo(earlier[run]).fileName().toStdString();
    }
}
