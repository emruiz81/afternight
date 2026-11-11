// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Writes every scenario's report where a person, or the tool that opens reports in real browsers, can look at it.
///
/// The case is skipped unless `AFTERNIGHT_WRITE_REPORT_SAMPLES` names a folder. Each scenario then gets a folder of
/// its own there, laid out as an output folder's history is: under `history/`, a folder per run of the scenario -
/// the run reported and the earlier runs it is compared with - each as its archive keeps its report: the linked
/// report beside the `report_previews/` it names, and the stored model. The run reported also holds the embedded
/// report under a name of its own, which the render tool copies alone into an empty folder before it opens it.

#include <gtest/gtest.h>

#include "ReportModelTestSupport.h"

#include "processes/ImageIntegration/reporting/ReportArchiveReader.h"
#include "processes/ImageIntegration/reporting/WorkflowReport.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

#include <iostream>

using namespace ImageIntegration::Reporting;
using namespace an::tests::integration::report;

namespace
{
/// Copies a folder's files, and its folders' files, into another, which is made as needed.
[[nodiscard]] bool copyFolder(const QString& from, const QString& to)
{
    if (!QDir().mkpath(to))
    {
        return false;
    }
    const QDir source(from);
    QDirIterator files(from, QDir::Files, QDirIterator::Subdirectories);
    while (files.hasNext())
    {
        const QString file = files.next();
        const QString target = QDir(to).filePath(source.relativeFilePath(file));
        if (!QDir().mkpath(QFileInfo(target).absolutePath()))
        {
            return false;
        }
        QFile::remove(target);
        if (!QFile::copy(file, target))
        {
            return false;
        }
    }
    return true;
}

/// Copies what a run's archive keeps of its report - the stored model, the linked document and the pictures - into
/// a folder of the run's name under a sample's `history/`.
[[nodiscard]] bool copyReportOf(const QString& runDirectory, const QDir& history)
{
    const QDir run(runDirectory);
    const QDir sample(history.filePath(run.dirName()));
    if (!QDir().mkpath(sample.absolutePath()))
    {
        return false;
    }
    for (const char* name : {kWorkflowReportFileName, kStoredReportFileName})
    {
        const QString file = run.filePath(QLatin1String(name));
        if (QFileInfo::exists(file) && !QFile::copy(file, sample.filePath(QLatin1String(name))))
        {
            return false;
        }
    }
    const QString previews = run.filePath(QStringLiteral("report_previews"));
    return !QDir(previews).exists() || copyFolder(previews, sample.filePath(QStringLiteral("report_previews")));
}
} // namespace

TEST(ReportSamples, WritesTheReportOfEveryScenarioInBothForms)
{
    const QString folder = qEnvironmentVariable("AFTERNIGHT_WRITE_REPORT_SAMPLES").trimmed();
    if (folder.isEmpty())
    {
        GTEST_SKIP() << "set AFTERNIGHT_WRITE_REPORT_SAMPLES to a folder to write every scenario's report into";
    }
    ASSERT_TRUE(QDir().mkpath(folder)) << folder.toStdString();

    for (const ReportScenarioId id : allReportScenarios())
    {
        SCOPED_TRACE(scenarioName(id));
        std::optional<ReportScenario> scenario = buildReportScenario(id);
        ASSERT_TRUE(scenario.has_value());
        if (!scenario->archivedByExecutor)
        {
            // A run that stopped before any master was made is archived by nothing, and has no report.
            continue;
        }
        ASSERT_TRUE(archiveScenarioRun(*scenario));
        const ReportModel model = buildScenarioModel(*scenario, scenario->runDirectory);

        const QDir sample(QDir(folder).filePath(scenario->name));
        if (sample.exists())
        {
            ASSERT_TRUE(QDir(sample.absolutePath()).removeRecursively());
        }
        const QDir history(sample.filePath(QStringLiteral("history")));
        ASSERT_TRUE(QDir().mkpath(history.absolutePath()));

        // The report is stored in the run's folder as a run stores it - the model, the linked document and the
        // earlier runs' pictures - and the sample is what the folder's history keeps of each run's report.
        const WorkflowReportStored stored = storeWorkflowReport(model, scenario->runDirectory);
        ASSERT_TRUE(stored.isStored()) << stored.error.toStdString();
        EXPECT_TRUE(stored.faults.isEmpty()) << stored.faults.join(QLatin1Char('\n')).toStdString();
        EXPECT_TRUE(stored.picturesNotCopied.isEmpty())
            << stored.picturesNotCopied.join(QLatin1Char('\n')).toStdString();
        for (const QString& earlier : scenario->earlierRunDirectories)
        {
            ASSERT_TRUE(copyReportOf(earlier, history)) << earlier.toStdString();
        }
        ASSERT_TRUE(copyReportOf(scenario->runDirectory, history));

        const QDir run(history.filePath(QDir(scenario->runDirectory).dirName()));
        const WorkflowReportWritten embedded =
            writeWorkflowReport(model, scenario->runDirectory, ReportForm::Embedded,
                                run.filePath(QStringLiteral("workflow_report.single.html")));
        ASSERT_TRUE(embedded.isWritten()) << embedded.error.toStdString();
        EXPECT_TRUE(embedded.faults.isEmpty()) << embedded.faults.join(QLatin1Char('\n')).toStdString();

        std::cout << "[report-samples] " << QDir::toNativeSeparators(run.absolutePath()).toStdString() << '\n';
    }
}
