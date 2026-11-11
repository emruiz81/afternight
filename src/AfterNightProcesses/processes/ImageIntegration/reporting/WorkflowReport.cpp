// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "processes/ImageIntegration/reporting/WorkflowReport.h"

#include "processes/ImageIntegration/reporting/ReportHtmlWriter.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>

namespace ImageIntegration::Reporting
{
WorkflowReportWritten writeWorkflowReport(const ReportModel& model, const QString& runDirectory, const ReportForm form,
                                          const QString& destination)
{
    const auto failed = [](const QString& why) {
        return WorkflowReportWritten{.path = {}, .error = why, .faults = {}};
    };

    QString path;
    if (form == ReportForm::Linked)
    {
        if (runDirectory.trimmed().isEmpty() || !QDir(runDirectory).exists())
        {
            return failed(QCoreApplication::translate("ImageIntegrationReport", "The run's folder \"%1\" is not there.")
                              .arg(QDir::toNativeSeparators(runDirectory)));
        }
        path = QDir(runDirectory).filePath(QLatin1String(kWorkflowReportFileName));
    }
    else
    {
        if (destination.trimmed().isEmpty())
        {
            return failed(
                QCoreApplication::translate("ImageIntegrationReport", "No file was named to write the report to."));
        }
        path = destination;
        const QDir folder = QFileInfo(path).absoluteDir();
        if (!folder.exists() && !QDir().mkpath(folder.absolutePath()))
        {
            return failed(QCoreApplication::translate("ImageIntegrationReport", "The folder \"%1\" could not be made.")
                              .arg(QDir::toNativeSeparators(folder.absolutePath())));
        }
    }

    const ReportDocument document =
        writeReportDocument(model, ReportDocumentOptions{.form = form, .runDirectory = runDirectory});

    // Written beside the file and moved into its place, so a reader never opens half a report and a failure leaves
    // the report that was there.
    QSaveFile file(path);
    const QByteArray bytes = document.html.toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
    {
        return failed(
            QCoreApplication::translate("ImageIntegrationReport", "The report \"%1\" could not be written: %2")
                .arg(QDir::toNativeSeparators(path), file.errorString()));
    }
    return WorkflowReportWritten{.path = path, .error = {}, .faults = document.faults};
}
} // namespace ImageIntegration::Reporting
