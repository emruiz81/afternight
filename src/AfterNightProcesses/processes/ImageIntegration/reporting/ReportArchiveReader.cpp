// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ReportArchiveReader.h"

#include "ReportModelJson.h"
#include "ReportSources.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>

#include <utility>

namespace ImageIntegration::Reporting
{
namespace
{
[[nodiscard]] QString readerText(const char* text)
{
    return QCoreApplication::translate("ImageIntegrationReport", text);
}

[[nodiscard]] ReportArchiveRead refused(const ReportArchiveRefusal refusal, const QString& why)
{
    return ReportArchiveRead{.model = std::nullopt, .refusal = refusal, .why = why};
}

[[nodiscard]] ReportArchiveRead noStoredReport()
{
    return refused(ReportArchiveRefusal::NoStoredReport,
                   readerText("The archive keeps no stored report (%1).").arg(QLatin1String(kStoredReportFileName)));
}

[[nodiscard]] QString storedReportPath(const QString& runDirectory)
{
    return QDir(runDirectory).filePath(QLatin1String(kStoredReportFileName));
}
} // namespace

ReportArchiveRead ReportArchiveReader::parse(const QByteArray& stored)
{
    if (stored.trimmed().isEmpty())
    {
        return refused(ReportArchiveRefusal::NotReadable, readerText("The stored report is empty."));
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(stored, &error);
    if (error.error != QJsonParseError::NoError)
    {
        return refused(ReportArchiveRefusal::NotReadable,
                       readerText("The stored report is cut short or is not a JSON document (%1, at byte %2).")
                           .arg(error.errorString())
                           .arg(error.offset));
    }
    if (!document.isObject())
    {
        return refused(ReportArchiveRefusal::NotReadable, readerText("The stored report is not a JSON object."));
    }

    ReportModelRead read = ReportModelJson::fromJson(document.object());
    if (!read.model.has_value())
    {
        return refused(ReportArchiveRefusal::OtherFormat,
                       read.refusal.has_value() && !read.refusal->why.isEmpty()
                           ? read.refusal->why
                           : readerText("The stored report is not of this version's format."));
    }
    return ReportArchiveRead{.model = std::move(read.model), .refusal = std::nullopt, .why = {}};
}

ReportArchiveRead ReportArchiveReader::read(const QString& runDirectory)
{
    if (runDirectory.trimmed().isEmpty())
    {
        return noStoredReport();
    }
    QFile file(storedReportPath(runDirectory));
    if (!file.exists())
    {
        return noStoredReport();
    }
    if (!file.open(QIODevice::ReadOnly))
    {
        return refused(ReportArchiveRefusal::NotReadable,
                       readerText("The stored report could not be opened: %1").arg(file.errorString()));
    }
    return parse(file.readAll());
}

ReportArchiveRead ReportArchiveReader::read(const QString& runDirectory, IReportFiles& files)
{
    if (runDirectory.trimmed().isEmpty())
    {
        return noStoredReport();
    }
    const QString path = storedReportPath(runDirectory);
    if (!files.exists(path))
    {
        return noStoredReport();
    }
    const std::optional<QString> stored = files.readText(path);
    if (!stored.has_value())
    {
        return refused(ReportArchiveRefusal::NotReadable, readerText("The stored report could not be read."));
    }
    return parse(stored->toUtf8());
}
} // namespace ImageIntegration::Reporting
