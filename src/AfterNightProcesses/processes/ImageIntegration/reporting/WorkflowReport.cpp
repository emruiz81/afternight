// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "processes/ImageIntegration/reporting/WorkflowReport.h"

#include "processes/ImageIntegration/reporting/ReportArchiveReader.h"
#include "processes/ImageIntegration/reporting/ReportHtmlWriter.h"
#include "processes/ImageIntegration/reporting/ReportModel.h"
#include "processes/ImageIntegration/reporting/ReportModelJson.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QSaveFile>

#include <algorithm>
#include <optional>
#include <utility>

namespace ImageIntegration::Reporting
{
namespace
{
[[nodiscard]] QString storeText(const char* text)
{
    return QCoreApplication::translate("ImageIntegrationReport", text);
}

/// Copies one picture of an earlier run into the run's folder. A copy that is already there, of the source's size,
/// is left as it is: a report stored again copies nothing twice.
///
/// @return why the picture was not copied; empty when it is there.
[[nodiscard]] QString copyPicture(const QString& from, const QString& to)
{
    const QFileInfo source(from);
    if (!source.isFile())
    {
        return storeText("the earlier run's picture \"%1\" is no longer there").arg(QDir::toNativeSeparators(from));
    }
    const QFileInfo target(to);
    if (target.isFile() && target.size() == source.size())
    {
        return {};
    }
    if (!QDir().mkpath(target.absolutePath()))
    {
        return storeText("the folder \"%1\" could not be made").arg(QDir::toNativeSeparators(target.absolutePath()));
    }
    QFile::remove(to);
    if (!QFile::copy(from, to))
    {
        return storeText("\"%1\" could not be copied to \"%2\"")
            .arg(QDir::toNativeSeparators(from), QDir::toNativeSeparators(to));
    }
    return {};
}

/// The earlier runs the model's baselines name, each read once.
class BaselineArchives
{
public:
    explicit BaselineArchives(const QString& runDirectory) : m_history(QFileInfo(QDir::cleanPath(runDirectory)).dir())
    {
    }

    /// A master of an earlier run as that run's stored report states it; null when the archive gives no report or
    /// the report no such master.
    [[nodiscard]] const ReportMaster* master(const QString& runId, const QString& key)
    {
        auto known = m_models.find(runId);
        if (known == m_models.end())
        {
            known = m_models.insert(runId, ReportArchiveReader::read(m_history.filePath(runId)).model);
        }
        if (!known->has_value())
        {
            return nullptr;
        }
        const std::vector<ReportMaster>& masters = (*known)->masters;
        const auto found = std::find_if(masters.begin(), masters.end(),
                                        [&key](const ReportMaster& master) { return master.key == key; });
        return found == masters.end() ? nullptr : &*found;
    }

    /// A picture of an earlier run, as a file.
    [[nodiscard]] QString file(const QString& runId, const QString& picture) const
    {
        return QDir(m_history.filePath(runId)).filePath(picture);
    }

private:
    QDir m_history;
    QHash<QString, std::optional<ReportModel>> m_models;
};

/// Copies the pictures the model's baselines name into the run's folder.
///
/// @param model the model; a baseline's picture that could not be copied is taken out of it.
/// @return the pictures that could not be copied, each with why.
[[nodiscard]] QStringList copyBaselinePictures(ReportModel& model, const QString& runDirectory)
{
    QStringList failures;
    BaselineArchives archives(runDirectory);
    const QDir run(runDirectory);
    for (ReportMaster& master : model.masters)
    {
        if (!master.baseline.has_value() || !master.baseline->made)
        {
            continue;
        }
        ReportBaseline& baseline = *master.baseline;
        const ReportMaster* earlier = archives.master(baseline.runId, master.key);
        const auto copy = [&](QString& target, const QString ReportMaster::* source) {
            if (target.isEmpty())
            {
                return;
            }
            const QString why =
                earlier == nullptr || (earlier->*source).trimmed().isEmpty()
                    ? storeText("the run %1 no longer states the picture").arg(baseline.runId)
                    : copyPicture(archives.file(baseline.runId, earlier->*source), run.filePath(target));
            if (!why.isEmpty())
            {
                failures << QStringLiteral("%1: %2").arg(target, why);
                target.clear();
            }
        };
        copy(baseline.preview, &ReportMaster::preview);
        copy(baseline.detail, &ReportMaster::detail);
    }
    return failures;
}
} // namespace

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

WorkflowReportStored storeWorkflowReport(const ReportModel& model, const QString& runDirectory)
{
    WorkflowReportStored stored;
    if (runDirectory.trimmed().isEmpty() || !QDir(runDirectory).exists())
    {
        stored.error = storeText("The run's folder \"%1\" is not there.").arg(QDir::toNativeSeparators(runDirectory));
        return stored;
    }

    // The pictures first: what the model and the document name must be there when they are written.
    ReportModel kept = model;
    stored.picturesNotCopied = copyBaselinePictures(kept, runDirectory);

    const QString modelPath = QDir(runDirectory).filePath(QLatin1String(kStoredReportFileName));
    QSaveFile file(modelPath);
    // Written without indentation: a history is bounded in size, every run of it keeps one of these, and an
    // indented document is nearly twice as long.
    const QByteArray bytes = QJsonDocument(ReportModelJson::toJson(kept)).toJson(QJsonDocument::Compact);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
    {
        stored.error = storeText("The stored report \"%1\" could not be written: %2")
                           .arg(QDir::toNativeSeparators(modelPath), file.errorString());
        return stored;
    }
    stored.modelPath = modelPath;

    WorkflowReportWritten written = writeWorkflowReport(kept, runDirectory, ReportForm::Linked);
    stored.faults = std::move(written.faults);
    if (!written.isWritten())
    {
        stored.error = written.error;
        return stored;
    }
    stored.reportPath = written.path;
    return stored;
}
} // namespace ImageIntegration::Reporting
