// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Writes every scenario's report where a person, or the tool that opens reports in real browsers, can look at it.
///
/// The case is skipped unless `AFTERNIGHT_WRITE_REPORT_SAMPLES` names a folder. Each scenario then gets a folder of
/// its own there, laid out as a run's archive is: the linked report beside the `report_previews/` it names, the
/// stored model, and the embedded report under a name of its own, which the render tool copies alone into an empty
/// folder before it opens it.

#include <gtest/gtest.h>

#include "ReportModelTestSupport.h"

#include "processes/ImageIntegration/reporting/ReportModelJson.h"
#include "processes/ImageIntegration/reporting/WorkflowReport.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>

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
        ASSERT_TRUE(QDir().mkpath(sample.absolutePath()));

        // The linked report is written into the run's folder, as a run will write it, and the sample is that
        // folder's report and its pictures.
        const WorkflowReportWritten linked = writeWorkflowReport(model, scenario->runDirectory, ReportForm::Linked);
        ASSERT_TRUE(linked.isWritten()) << linked.error.toStdString();
        EXPECT_TRUE(linked.faults.isEmpty()) << linked.faults.join(QLatin1Char('\n')).toStdString();
        ASSERT_TRUE(QFile::copy(linked.path, sample.filePath(QLatin1String(kWorkflowReportFileName))));
        const QString previews = QDir(scenario->runDirectory).filePath(QStringLiteral("report_previews"));
        if (QDir(previews).exists())
        {
            ASSERT_TRUE(copyFolder(previews, sample.filePath(QStringLiteral("report_previews"))));
        }

        const WorkflowReportWritten embedded =
            writeWorkflowReport(model, scenario->runDirectory, ReportForm::Embedded,
                                sample.filePath(QStringLiteral("workflow_report.single.html")));
        ASSERT_TRUE(embedded.isWritten()) << embedded.error.toStdString();
        EXPECT_TRUE(embedded.faults.isEmpty()) << embedded.faults.join(QLatin1Char('\n')).toStdString();

        QFile stored(sample.filePath(QStringLiteral("workflow_report.json")));
        ASSERT_TRUE(stored.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const QByteArray json = QJsonDocument(ReportModelJson::toJson(model)).toJson(QJsonDocument::Indented);
        ASSERT_EQ(stored.write(json), json.size());
        stored.close();

        std::cout << "[report-samples] " << QDir::toNativeSeparators(sample.absolutePath()).toStdString() << '\n';
    }
}
