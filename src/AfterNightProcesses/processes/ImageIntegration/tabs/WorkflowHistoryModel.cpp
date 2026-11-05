// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "WorkflowHistoryModel.h"

#include "processes/ImageIntegration/core/FilenameUtils.h"
#include "processes/ImageIntegration/model/GroupingKeys.h"

#include <verdigris/wobjectimpl.h>

#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QLocale>
#include <QSet>
#include <algorithm>
#include <cmath>

W_OBJECT_IMPL(WorkflowHistoryModel)

namespace
{
[[nodiscard]] QString compactGroupKeyForDisplay(const QString& rawKey)
{
    const QString trimmed = rawKey.trimmed();
    if (trimmed.isEmpty())
    {
        return {};
    }

    const auto parsed = GroupKey::fromString(trimmed);
    if (!parsed.has_value())
    {
        return trimmed;
    }

    const QString compact = parsed->toCompactString().trimmed();
    return compact.isEmpty() ? trimmed : compact;
}

[[nodiscard]] QString formatDurationHuman(int ms)
{
    if (ms < 1000)
    {
        return WorkflowHistoryModel::tr("%1 ms").arg(ms);
    }
    const int totalSec = ms / 1000;
    if (totalSec < 60)
    {
        return WorkflowHistoryModel::tr("%1 sec").arg(totalSec);
    }
    const int min = totalSec / 60;
    const int sec = totalSec % 60;
    if (min < 60)
    {
        return sec > 0 ? WorkflowHistoryModel::tr("%1 min %2 sec").arg(min).arg(sec)
                       : WorkflowHistoryModel::tr("%1 min").arg(min);
    }
    const int hr = min / 60;
    const int remainMin = min % 60;
    return remainMin > 0 ? WorkflowHistoryModel::tr("%1 h %2 min").arg(hr).arg(remainMin)
                         : WorkflowHistoryModel::tr("%1 h").arg(hr);
}

[[nodiscard]] QString formatFileSizeHuman(qint64 bytes)
{
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

[[nodiscard]] QString statusLabel(const QString& status)
{
    if (status == QStringLiteral("success"))
        return WorkflowHistoryModel::tr("Completed successfully");
    if (status == QStringLiteral("partial"))
        return WorkflowHistoryModel::tr("Partially completed");
    if (status == QStringLiteral("failed"))
        return WorkflowHistoryModel::tr("Failed");
    if (status == QStringLiteral("cancelled"))
        return WorkflowHistoryModel::tr("Cancelled");
    return status;
}

[[nodiscard]] QString modeLabel(const QString& mode)
{
    if (mode == QStringLiteral("full"))
        return WorkflowHistoryModel::tr("Full pipeline");
    if (mode == QStringLiteral("quick"))
        return WorkflowHistoryModel::tr("Quick run");
    return mode;
}

[[nodiscard]] QString normalizedTraitValue(const QString& value)
{
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty() || trimmed == QStringLiteral("-"))
    {
        return {};
    }

    return trimmed;
}

[[nodiscard]] QString normalizedCompareToken(const QString& value)
{
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty() || trimmed == QStringLiteral("-"))
    {
        return {};
    }

    return trimmed.simplified().toCaseFolded();
}

[[nodiscard]] QString normalizedCompareFilename(const QString& filename)
{
    const QString trimmed = filename.trimmed();
    if (trimmed.isEmpty())
    {
        return {};
    }

    return QFileInfo(trimmed).completeBaseName().trimmed().toCaseFolded();
}

[[nodiscard]] bool isCombinedRgbMasterKey(const QString& rawGroupKey)
{
    const QString key = normalizedCompareToken(rawGroupKey);
    return key == QStringLiteral("rgb") || key.startsWith(QStringLiteral("rgb|"));
}

[[nodiscard]] bool sameNonEmptyCompareValue(const QString& lhs, const QString& rhs)
{
    const QString left = normalizedCompareToken(lhs);
    const QString right = normalizedCompareToken(rhs);
    return !left.isEmpty() && left == right;
}

[[nodiscard]] bool conflictingCompareValue(const QString& lhs, const QString& rhs)
{
    const QString left = normalizedCompareToken(lhs);
    const QString right = normalizedCompareToken(rhs);
    return !left.isEmpty() && !right.isEmpty() && left != right;
}

[[nodiscard]] bool existingFilePath(const QString& path)
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty())
    {
        return false;
    }

    const QFileInfo info(trimmed);
    return info.exists() && info.isFile();
}

[[nodiscard]] QString resolveHistoryPath(const QDir& runDir, const QString& path)
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty())
    {
        return {};
    }

    const QFileInfo info(trimmed);
    if (info.isRelative())
    {
        return QFileInfo(runDir.filePath(trimmed)).absoluteFilePath();
    }

    return info.absoluteFilePath();
}

[[nodiscard]] QString dedupeFilePath(const QString& path)
{
    const QFileInfo info(path.trimmed());
    QString key = info.canonicalFilePath();
    if (key.isEmpty())
    {
        key = info.absoluteFilePath();
    }
    return key;
}

struct ParsedLightTraits
{
    QString resolution;
    QString exposure;
    QString filter;
    QString object;
};

[[nodiscard]] ParsedLightTraits parseLightTraitsFromGroupKey(const QString& rawGroupKey)
{
    ParsedLightTraits traits;
    const QStringList parts = rawGroupKey.split(QLatin1Char('|'), Qt::KeepEmptyParts);
    if (parts.size() > 0)
    {
        traits.resolution = normalizedTraitValue(parts.at(0));
    }
    if (parts.size() > 3)
    {
        traits.exposure = normalizedTraitValue(parts.at(3));
    }
    if (parts.size() > 4)
    {
        traits.filter = normalizedTraitValue(parts.at(4));
    }
    if (parts.size() > 5)
    {
        traits.object = normalizedTraitValue(parts.at(5));
    }
    return traits;
}

[[nodiscard]] QHash<QString, QJsonObject> integrationGroupsByKey(const QJsonObject& root)
{
    QHash<QString, QJsonObject> groups;
    const QJsonArray integrationGroups =
        root.value(QStringLiteral("manifest")).toObject().value(QStringLiteral("integrationGroups")).toArray();
    groups.reserve(integrationGroups.size());

    for (const auto& value : integrationGroups)
    {
        const QJsonObject group = value.toObject();
        const QString groupKey = group.value(QStringLiteral("groupKey")).toString().trimmed();
        if (!groupKey.isEmpty())
        {
            groups.insert(groupKey, group);
        }
    }

    return groups;
}

[[nodiscard]] QHash<QString, QJsonObject> registrationGroupsByKey(const QJsonObject& root)
{
    QHash<QString, QJsonObject> groups;
    const QJsonArray registrationGroups =
        root.value(QStringLiteral("manifest")).toObject().value(QStringLiteral("registrationGroups")).toArray();
    groups.reserve(registrationGroups.size());

    for (const auto& value : registrationGroups)
    {
        const QJsonObject group = value.toObject();
        const QString groupKey = group.value(QStringLiteral("groupKey")).toString().trimmed();
        if (!groupKey.isEmpty())
        {
            groups.insert(groupKey, group);
        }
    }

    return groups;
}

[[nodiscard]] QString displaySettingLabel(const QString& value)
{
    QString label = value.trimmed();
    if (label.isEmpty())
    {
        return {};
    }

    label.replace(QLatin1Char('_'), QLatin1Char(' '));
    return label.simplified();
}

[[nodiscard]] bool isDisabledSetting(const QString& value)
{
    const QString normalized = value.trimmed().toCaseFolded();
    return normalized.isEmpty() || normalized == QStringLiteral("none") || normalized == QStringLiteral("off") ||
           normalized == QStringLiteral("disabled") || normalized == QStringLiteral("false");
}

[[nodiscard]] QString formatNumberCompact(double value, int decimals = 2)
{
    if (!std::isfinite(value))
    {
        return {};
    }

    QString text = QString::number(value, 'f', decimals);
    while (text.contains(QLatin1Char('.')) && (text.endsWith(QLatin1Char('0')) || text.endsWith(QLatin1Char('.'))))
    {
        text.chop(1);
    }
    return text;
}

[[nodiscard]] QString normalizationModeLabel(int mode)
{
    switch (mode)
    {
    case 0:
        return QStringLiteral("None");
    case 1:
        return QStringLiteral("Global");
    case 2:
        return QStringLiteral("Local");
    default:
        return WorkflowHistoryModel::tr("Mode %1").arg(mode);
    }
}

[[nodiscard]] QString globalNormalizationMethodLabel(int method)
{
    switch (method)
    {
    case 0:
        return QStringLiteral("Additive");
    case 1:
        return QStringLiteral("Multiplicative");
    case 2:
        return QStringLiteral("Additive+Scale");
    case 3:
        return QStringLiteral("Multiplicative+Scale");
    default:
        return WorkflowHistoryModel::tr("Method %1").arg(method);
    }
}

[[nodiscard]] QString scaleEstimatorLabel(int method)
{
    switch (method)
    {
    case 0:
        return QStringLiteral("MAD");
    case 1:
        return QStringLiteral("IKSS");
    default:
        return WorkflowHistoryModel::tr("Estimator %1").arg(method);
    }
}

[[nodiscard]] bool equalsSettingText(const QString& lhs, const QString& rhs)
{
    return lhs.trimmed().compare(rhs.trimmed(), Qt::CaseInsensitive) == 0;
}

[[nodiscard]] QString localNormalizationPresetLabel(const QJsonObject& localNormalization)
{
    const int gridSize = localNormalization.value(QStringLiteral("gridSize")).toInt(4);
    const QString referenceStrategy =
        displaySettingLabel(localNormalization.value(QStringLiteral("referenceStrategy")).toString());

    if (gridSize == 2 && equalsSettingText(referenceStrategy, QStringLiteral("Single Best Frame")))
    {
        return WorkflowHistoryModel::tr("Quick");
    }
    if (gridSize == 4 && equalsSettingText(referenceStrategy, QStringLiteral("Integrate Best Frames")))
    {
        return WorkflowHistoryModel::tr("Standard");
    }
    if (gridSize == 8 && equalsSettingText(referenceStrategy, QStringLiteral("Integrate Best Frames")))
    {
        return WorkflowHistoryModel::tr("Fine");
    }
    return WorkflowHistoryModel::tr("Custom");
}

[[nodiscard]] QString normalizationModeContextLabel(int mode, const QJsonObject& stacking)
{
    if (mode != 2)
    {
        return normalizationModeLabel(mode);
    }

    const QJsonObject localNormalization = stacking.value(QStringLiteral("localNormalization")).toObject();
    if (!localNormalization.value(QStringLiteral("enabled")).toBool(false))
    {
        return normalizationModeLabel(mode);
    }

    return WorkflowHistoryModel::tr("Local (%1)").arg(localNormalizationPresetLabel(localNormalization));
}

[[nodiscard]] QString stackingContextLine(const QJsonObject& stacking)
{
    const QString stackMethod = displaySettingLabel(stacking.value(QStringLiteral("stackMethod")).toString());
    QString rejection = displaySettingLabel(stacking.value(QStringLiteral("rejectionMethod")).toString());
    if (rejection.isEmpty())
    {
        rejection = displaySettingLabel(stacking.value(QStringLiteral("requestedRejectionMethod")).toString());
    }

    if (stackMethod.isEmpty() && rejection.isEmpty())
    {
        return {};
    }

    QString line =
        WorkflowHistoryModel::tr("Stacking: %1").arg(stackMethod.isEmpty() ? QStringLiteral("-") : stackMethod);
    if (!rejection.isEmpty())
    {
        line += WorkflowHistoryModel::tr(" / %1").arg(rejection);
    }

    if (stacking.contains(QStringLiteral("sigmaLow")) || stacking.contains(QStringLiteral("sigmaHigh")))
    {
        const QString sigmaLow = formatNumberCompact(stacking.value(QStringLiteral("sigmaLow")).toDouble());
        const QString sigmaHigh = formatNumberCompact(stacking.value(QStringLiteral("sigmaHigh")).toDouble());
        if (!sigmaLow.isEmpty() || !sigmaHigh.isEmpty())
        {
            line += WorkflowHistoryModel::tr(" (sigma %1 / %2)")
                        .arg(sigmaLow.isEmpty() ? QStringLiteral("-") : sigmaLow,
                             sigmaHigh.isEmpty() ? QStringLiteral("-") : sigmaHigh);
        }
    }

    return line;
}

[[nodiscard]] QString weightingContextLine(const QJsonObject& stacking)
{
    const QString weighting = displaySettingLabel(stacking.value(QStringLiteral("weighting")).toString());
    if (isDisabledSetting(weighting))
    {
        return WorkflowHistoryModel::tr("Weighting: Disabled");
    }

    QString line = WorkflowHistoryModel::tr("Weighting: %1").arg(weighting);
    const QJsonObject localWeighting = stacking.value(QStringLiteral("localWeighting")).toObject();
    if (localWeighting.value(QStringLiteral("enabled")).toBool(false))
    {
        line += WorkflowHistoryModel::tr(" + local");
    }
    return line;
}

[[nodiscard]] QJsonObject drizzleContextObject(const QJsonObject& manifestGroup, const QJsonObject& stacking)
{
    const QJsonObject groupDrizzle = manifestGroup.value(QStringLiteral("drizzle")).toObject();
    if (!groupDrizzle.isEmpty())
    {
        return groupDrizzle;
    }
    return stacking.value(QStringLiteral("drizzle")).toObject();
}

[[nodiscard]] QString drizzleContextLine(const QJsonObject& manifestGroup, const QJsonObject& stacking)
{
    const QJsonObject drizzle = drizzleContextObject(manifestGroup, stacking);
    if (drizzle.isEmpty())
    {
        return {};
    }

    const bool enabled = drizzle.value(QStringLiteral("enabled")).toBool(false);
    if (!enabled)
    {
        return WorkflowHistoryModel::tr("Drizzle: Disabled");
    }

    QString line = WorkflowHistoryModel::tr("Drizzle: Enabled");
    if (drizzle.contains(QStringLiteral("scale")))
    {
        const QString scale = formatNumberCompact(drizzle.value(QStringLiteral("scale")).toDouble());
        if (!scale.isEmpty())
        {
            line += WorkflowHistoryModel::tr(" x%1").arg(scale);
        }
    }

    QStringList details;
    const QString kernel = displaySettingLabel(drizzle.value(QStringLiteral("kernel")).toString());
    if (!kernel.isEmpty())
    {
        details << kernel;
    }
    if (drizzle.contains(QStringLiteral("pixelFraction")))
    {
        const QString pixfrac = formatNumberCompact(drizzle.value(QStringLiteral("pixelFraction")).toDouble());
        if (!pixfrac.isEmpty())
        {
            details << WorkflowHistoryModel::tr("pixfrac %1").arg(pixfrac);
        }
    }
    if (drizzle.value(QStringLiteral("cfaAware")).toBool(false))
    {
        const QString cfaPattern = displaySettingLabel(drizzle.value(QStringLiteral("cfaPattern")).toString());
        details << (cfaPattern.isEmpty() ? WorkflowHistoryModel::tr("CFA-aware")
                                         : WorkflowHistoryModel::tr("CFA-aware %1").arg(cfaPattern));
    }
    if (!details.isEmpty())
    {
        line += QStringLiteral(" (");
        line += details.join(QStringLiteral(", "));
        line += QStringLiteral(")");
    }

    return line;
}

[[nodiscard]] QString resolutionContextLine(const QJsonObject& manifestGroup)
{
    const QString resolution = normalizedTraitValue(manifestGroup.value(QStringLiteral("resolution")).toString());
    if (resolution.isEmpty())
    {
        return {};
    }

    return WorkflowHistoryModel::tr("Resolution: %1").arg(resolution);
}

[[nodiscard]] QString registrationContextLine(const QJsonObject& registrationGroup)
{
    const QJsonObject settings = registrationGroup.value(QStringLiteral("settings")).toObject();
    const QString method = displaySettingLabel(settings.value(QStringLiteral("registrationMethod")).toString());
    const QString transformation = displaySettingLabel(settings.value(QStringLiteral("transformation")).toString());
    const QString interpolation = displaySettingLabel(settings.value(QStringLiteral("interpolation")).toString());
    if (method.isEmpty() && transformation.isEmpty() && interpolation.isEmpty())
    {
        return {};
    }

    QStringList parts;
    if (!method.isEmpty())
    {
        parts << method;
    }
    if (!transformation.isEmpty())
    {
        parts << transformation;
    }

    QString line = WorkflowHistoryModel::tr("Registration: %1")
                       .arg(parts.isEmpty() ? QStringLiteral("-") : parts.join(QStringLiteral(" / ")));
    if (!interpolation.isEmpty())
    {
        line += WorkflowHistoryModel::tr(" (%1)").arg(interpolation);
    }
    return line;
}

[[nodiscard]] QString normalizationContextLine(const QJsonObject& stacking)
{
    if (!stacking.contains(QStringLiteral("normalizationMode")))
    {
        return {};
    }

    QStringList parts;
    const int mode = stacking.value(QStringLiteral("normalizationMode")).toInt(0);
    parts << normalizationModeContextLabel(mode, stacking);
    if (mode != 0 && stacking.contains(QStringLiteral("globalNormalizationMethod")))
    {
        parts << globalNormalizationMethodLabel(stacking.value(QStringLiteral("globalNormalizationMethod")).toInt(0));
    }
    if (mode != 0 && stacking.contains(QStringLiteral("scaleEstimator")))
    {
        parts << scaleEstimatorLabel(stacking.value(QStringLiteral("scaleEstimator")).toInt(0));
    }
    return WorkflowHistoryModel::tr("Normalization: %1").arg(parts.join(QStringLiteral(", ")));
}

[[nodiscard]] QString buildMasterContextSummary(const QJsonObject& manifestGroup, const QJsonObject& registrationGroup)
{
    if (manifestGroup.isEmpty())
    {
        return {};
    }

    const QJsonObject stacking = manifestGroup.value(QStringLiteral("stacking")).toObject();
    QStringList lines;

    const QString stackingLine = stackingContextLine(stacking);
    if (!stackingLine.isEmpty())
    {
        lines << stackingLine;
    }
    if (!stacking.isEmpty())
    {
        lines << weightingContextLine(stacking);
    }

    const QString drizzleLine = drizzleContextLine(manifestGroup, stacking);
    if (!drizzleLine.isEmpty())
    {
        lines << drizzleLine;
    }

    const QString resolutionLine = resolutionContextLine(manifestGroup);
    if (!resolutionLine.isEmpty())
    {
        lines << resolutionLine;
    }

    const QString registrationLine = registrationContextLine(registrationGroup);
    if (!registrationLine.isEmpty())
    {
        lines << registrationLine;
    }

    const QString normalizationLine = normalizationContextLine(stacking);
    if (!normalizationLine.isEmpty())
    {
        lines << normalizationLine;
    }

    return lines.join(QStringLiteral("\n"));
}

[[nodiscard]] QString resolveArchivedGroupFileBySummary(const QDir& runDir, const QString& rawGroupKey,
                                                        const QString& filename)
{
    if (rawGroupKey.trimmed().isEmpty() || filename.trimmed().isEmpty())
    {
        return {};
    }

    const QDir groupsDir(runDir.filePath(QStringLiteral("groups")));
    if (!groupsDir.exists())
    {
        return {};
    }

    QDirIterator it(groupsDir.absolutePath(), QStringList{QStringLiteral("group_summary.json")}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        const QString summaryPath = it.next();
        QFile file(summaryPath);
        if (!file.open(QIODevice::ReadOnly))
        {
            continue;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        const QJsonObject summary = doc.object();
        if (summary.value(QStringLiteral("groupKey")).toString().trimmed() != rawGroupKey.trimmed())
        {
            continue;
        }

        const QString candidatePath = QFileInfo(summaryPath).dir().filePath(filename);
        if (QFileInfo::exists(candidatePath))
        {
            return QFileInfo(candidatePath).absoluteFilePath();
        }
    }

    return {};
}

[[nodiscard]] QString resolveArchivedGroupFile(const QDir& runDir, const QString& rawGroupKey, const QString& filename)
{
    QString path = resolveArchivedGroupFileBySummary(runDir, rawGroupKey, filename);
    if (!path.isEmpty())
    {
        return path;
    }

    const QString groupDirName = ImageIntegration::sanitizeFilename(rawGroupKey);
    path = runDir.filePath(QStringLiteral("groups/%1/%2").arg(groupDirName, filename));
    return QFileInfo::exists(path) ? QFileInfo(path).absoluteFilePath() : QString();
}

[[nodiscard]] QStringList resolveArchivedOutputPaths(const QDir& runDir, const QJsonObject& groupJson,
                                                     const QString& rawGroupKey, const QString& outputFilename)
{
    const QString expectedFilename = outputFilename.trimmed();
    if (expectedFilename.isEmpty())
    {
        return {};
    }

    QStringList paths;
    QSet<QString> seen;
    const QJsonArray archivedFiles = groupJson.value(QStringLiteral("archivedFiles")).toArray();
    for (const auto& value : archivedFiles)
    {
        const QJsonObject archived = value.toObject();
        const QString filename = archived.value(QStringLiteral("filename")).toString().trimmed();
        if (filename.isEmpty() || filename != expectedFilename)
        {
            continue;
        }

        const QString archivedPath = resolveArchivedGroupFile(runDir, rawGroupKey, filename);
        if (archivedPath.isEmpty() || seen.contains(archivedPath))
        {
            continue;
        }

        seen.insert(archivedPath);
        paths.append(archivedPath);
    }

    return paths;
}
} // namespace

WorkflowHistoryModel::WorkflowHistoryModel(QObject* parent) : QAbstractItemModel(parent) {}

WorkflowHistoryModel::~WorkflowHistoryModel() = default;

void WorkflowHistoryModel::setHistoryDirectory(const QString& path)
{
    if (m_historyDir != path)
    {
        m_historyDir = path;
        m_expandedStateByExecutionId.clear();
        refresh();
    }
}

void WorkflowHistoryModel::refresh()
{
    beginResetModel();
    loadWorkflows();
    endResetModel();
}

void WorkflowHistoryModel::loadWorkflows()
{
    m_workflows.clear();

    if (m_historyDir.isEmpty())
        return;

    QDir historyDir(m_historyDir);
    if (!historyDir.exists())
        return;

    // List all directories in history/ (each is an execution)
    const QStringList subdirs = historyDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time); // Newest first

    for (const QString& subdirName : subdirs)
    {
        QDir runDir(historyDir.filePath(subdirName));
        QString manifestPath = runDir.filePath("workflow_manifest.json");

        if (!QFile::exists(manifestPath))
            continue;

        QFile file(manifestPath);
        if (!file.open(QIODevice::ReadOnly))
            continue;

        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();

        if (!doc.isObject())
            continue;
        QJsonObject root = doc.object();

        // A run directory is one that holds a workflow_manifest.json; any other directory is not listed.

        WorkflowItem item;
        item.executionId = root["executionId"].toString();
        // Fallback if ID missing
        if (item.executionId.isEmpty())
            item.executionId = subdirName;

        item.runDirPath = runDir.absolutePath();
        item.manifestPath = manifestPath;

        // Report path: prefer HTML report when available, fallback to Markdown.
        const QString htmlReportPath = runDir.filePath(QStringLiteral("workflow_report.html"));
        if (QFile::exists(htmlReportPath))
        {
            item.reportPath = htmlReportPath;
        }
        else
        {
            const QString markdownReportPath = runDir.filePath(QStringLiteral("workflow_report.md"));
            if (QFile::exists(markdownReportPath))
            {
                item.reportPath = markdownReportPath;
            }
        }

        // Log path
        if (QFile::exists(runDir.filePath("workflow_log.txt")))
            item.logPath = runDir.filePath("workflow_log.txt");

        item.timestamp = QDateTime::fromString(root["archivedAtLocal"].toString(), Qt::ISODateWithMs);
        if (!item.timestamp.isValid())
            item.timestamp = QDateTime::fromString(root["archivedAtLocal"].toString(), Qt::ISODate);

        item.mode = root["executionMode"].toString();
        item.status = root["overallResult"].toString();
        item.totalDurationMs = root["totalDurationMs"].toInt();
        item.warningCount = root["warningCount"].toInt();

        const QHash<QString, QJsonObject> manifestIntegrationGroups = integrationGroupsByKey(root);
        const QHash<QString, QJsonObject> manifestRegistrationGroups = registrationGroupsByKey(root);

        bool hasProducedMasterStack = false;
        QJsonArray groups = root["integrationGroupResults"].toArray();
        for (const auto& gVal : groups)
        {
            QJsonObject g = gVal.toObject();
            GroupItem group;
            group.rawGroupKey = g["groupKey"].toString();
            group.groupKey = compactGroupKeyForDisplay(group.rawGroupKey);
            group.outputPath = resolveHistoryPath(runDir, g["outputPath"].toString());
            group.outputFilename = g["outputFilename"].toString();
            group.outputBytes = g["outputBytes"].toVariant().toLongLong();

            QJsonObject manifestGroup = manifestIntegrationGroups.value(group.rawGroupKey);
            if (manifestGroup.isEmpty() && !group.outputFilename.trimmed().isEmpty())
            {
                for (const auto& manifestValue : manifestIntegrationGroups)
                {
                    const QJsonObject candidate = manifestValue;
                    if (candidate.value(QStringLiteral("outputFilename")).toString().trimmed() ==
                        group.outputFilename.trimmed())
                    {
                        manifestGroup = candidate;
                        break;
                    }
                }
            }

            if (!manifestGroup.isEmpty())
            {
                if (group.outputFilename.trimmed().isEmpty())
                {
                    group.outputFilename = manifestGroup.value(QStringLiteral("outputFilename")).toString();
                }
                group.resolution = normalizedTraitValue(manifestGroup.value(QStringLiteral("resolution")).toString());
                group.object = normalizedTraitValue(manifestGroup.value(QStringLiteral("object")).toString());
                group.filter = normalizedTraitValue(manifestGroup.value(QStringLiteral("filter")).toString());
                group.exposure = normalizedTraitValue(manifestGroup.value(QStringLiteral("exposure")).toString());
                group.registrationGroupKey =
                    normalizedTraitValue(manifestGroup.value(QStringLiteral("registrationGroupKey")).toString());
            }

            const ParsedLightTraits parsedTraits = parseLightTraitsFromGroupKey(group.rawGroupKey);
            if (group.resolution.isEmpty())
            {
                group.resolution = parsedTraits.resolution;
            }
            if (group.object.isEmpty())
            {
                group.object = parsedTraits.object;
            }
            if (group.filter.isEmpty())
            {
                group.filter = parsedTraits.filter;
            }
            if (group.exposure.isEmpty())
            {
                group.exposure = parsedTraits.exposure;
            }

            // Resolve preview path relative to run dir if needed, or absolute
            QString rawPreview = g["previewPath"].toString();
            if (!rawPreview.isEmpty())
            {
                group.previewPath = resolveHistoryPath(runDir, rawPreview);
            }

            group.lightFrameCount = g["lightFrameCount"].toInt();
            group.durationMs = g["durationMs"].toInt();
            if (group.registrationGroupKey.isEmpty())
            {
                group.registrationGroupKey = normalizedTraitValue(g["registrationGroupKey"].toString());
            }
            group.masterContext =
                buildMasterContextSummary(manifestGroup, manifestRegistrationGroups.value(group.registrationGroupKey));
            group.success = (g["result"].toString() == "success");
            group.archivedOutputPaths = resolveArchivedOutputPaths(runDir, g, group.rawGroupKey, group.outputFilename);
            hasProducedMasterStack = hasProducedMasterStack || group.success;

            item.groups.push_back(group);
        }

        // Ignore runs that did not produce any successful master stack.
        if (!hasProducedMasterStack)
        {
            continue;
        }

        m_workflows.push_back(item);
    }

    // Sort newest first
    std::sort(m_workflows.begin(), m_workflows.end(),
              [](const WorkflowItem& a, const WorkflowItem& b) { return a.timestamp > b.timestamp; });

    // Restore explicit expand/collapse state per execution, defaulting newest run to expanded.
    for (int i = 0; i < static_cast<int>(m_workflows.size()); ++i)
    {
        auto& workflow = m_workflows[static_cast<size_t>(i)];
        const auto found = m_expandedStateByExecutionId.constFind(workflow.executionId);
        if (found != m_expandedStateByExecutionId.cend())
        {
            workflow.expanded = found.value();
        }
        else
        {
            workflow.expanded = (i == 0);
            m_expandedStateByExecutionId.insert(workflow.executionId, workflow.expanded);
        }
    }
}

bool WorkflowHistoryModel::groupsCanCompare(const GroupItem& source, const GroupItem& candidate)
{
    if (!source.success || !candidate.success)
    {
        return false;
    }

    const bool source_is_combined_rgb = isCombinedRgbMasterKey(source.rawGroupKey);
    const bool candidate_is_combined_rgb = isCombinedRgbMasterKey(candidate.rawGroupKey);
    if (source_is_combined_rgb || candidate_is_combined_rgb)
    {
        if (source_is_combined_rgb != candidate_is_combined_rgb)
        {
            return false;
        }

        return sameNonEmptyCompareValue(source.rawGroupKey, candidate.rawGroupKey) ||
               sameNonEmptyCompareValue(source.groupKey, candidate.groupKey) ||
               sameNonEmptyCompareValue(source.registrationGroupKey, candidate.registrationGroupKey) ||
               (!normalizedCompareFilename(source.outputFilename).isEmpty() &&
                normalizedCompareFilename(source.outputFilename) ==
                    normalizedCompareFilename(candidate.outputFilename));
    }

    if (sameNonEmptyCompareValue(source.rawGroupKey, candidate.rawGroupKey) ||
        sameNonEmptyCompareValue(source.groupKey, candidate.groupKey))
    {
        return true;
    }

    const QString sourceFilename = normalizedCompareFilename(source.outputFilename);
    const QString candidateFilename = normalizedCompareFilename(candidate.outputFilename);
    if (!sourceFilename.isEmpty() && sourceFilename == candidateFilename)
    {
        return true;
    }

    if (conflictingCompareValue(source.filter, candidate.filter) ||
        conflictingCompareValue(source.object, candidate.object) ||
        conflictingCompareValue(source.exposure, candidate.exposure))
    {
        return false;
    }

    return sameNonEmptyCompareValue(source.filter, candidate.filter) ||
           sameNonEmptyCompareValue(source.object, candidate.object) ||
           sameNonEmptyCompareValue(source.exposure, candidate.exposure) ||
           sameNonEmptyCompareValue(source.registrationGroupKey, candidate.registrationGroupKey);
}

bool WorkflowHistoryModel::groupHasComparableMaster(int workflowRow, int groupRow) const
{
    if (workflowRow < 0 || workflowRow >= static_cast<int>(m_workflows.size()))
    {
        return false;
    }

    const auto& workflow = m_workflows[static_cast<size_t>(workflowRow)];
    if (groupRow < 0 || groupRow >= static_cast<int>(workflow.groups.size()))
    {
        return false;
    }

    const GroupItem& source = workflow.groups[static_cast<size_t>(groupRow)];
    QSet<QString> seenPaths;
    const auto addComparablePath = [&seenPaths](const QString& path) {
        if (!existingFilePath(path))
        {
            return false;
        }

        const QString key = dedupeFilePath(path);
        if (key.isEmpty() || seenPaths.contains(key))
        {
            return false;
        }

        seenPaths.insert(key);
        return true;
    };

    if (!source.success || !addComparablePath(source.outputPath))
    {
        return false;
    }

    for (const QString& archivedPath : source.archivedOutputPaths)
    {
        if (addComparablePath(archivedPath))
        {
            return true;
        }
    }

    for (int candidateWorkflowRow = 0; candidateWorkflowRow < static_cast<int>(m_workflows.size());
         ++candidateWorkflowRow)
    {
        const auto& candidateWorkflow = m_workflows[static_cast<size_t>(candidateWorkflowRow)];
        for (int candidateGroupRow = 0; candidateGroupRow < static_cast<int>(candidateWorkflow.groups.size());
             ++candidateGroupRow)
        {
            if (candidateWorkflowRow == workflowRow && candidateGroupRow == groupRow)
            {
                continue;
            }

            const GroupItem& candidate = candidateWorkflow.groups[static_cast<size_t>(candidateGroupRow)];
            if (!groupsCanCompare(source, candidate))
            {
                continue;
            }

            if (addComparablePath(candidate.outputPath))
            {
                return true;
            }
            for (const QString& archivedPath : candidate.archivedOutputPaths)
            {
                if (addComparablePath(archivedPath))
                {
                    return true;
                }
            }
        }
    }

    return false;
}

QModelIndex WorkflowHistoryModel::index(int row, int column, const QModelIndex& parent) const
{
    if (!hasIndex(row, column, parent))
        return QModelIndex();

    if (!parent.isValid())
    {
        // Root items (Workflows)
        if (row >= 0 && row < static_cast<int>(m_workflows.size()))
            return createIndex(row, column, quintptr(row)); // Use ID as simple index
    }
    else
    {
        // Child items (Groups)
        quintptr parentId = parent.internalId();
        if (parentId < m_workflows.size())
        {
            const auto& workflow = m_workflows[parentId];
            if (row >= 0 && row < static_cast<int>(workflow.groups.size()))
            {
                // We need a unique ID for the child.
                // A common trick is (parentRow << 16) | childRow.
                // Or pointer.
                // Let's use pointer to the group item? No, vector resize invalidates pointers.
                // Let's use (parentRow << 32) | childRow? internalId is quintptr (64-bit usually).

                // Assuming max 2B workflows and 2B groups per workflow.
                quintptr childId = (parentId << 32) | (static_cast<quintptr>(row) & 0xFFFFFFFF);
                // Wait, to disambiguate from root items (where high 32 bits are 0),
                // root items are just 0..N.
                // If I shift parentId by 32, it becomes large.
                // But row 0 child of row 0 would be 0 | 0 = 0. Same as root item 0.
                // I need a bit to indicate "is child".
                // Let's set the high bit?

                // Let's simply say: internalId for Root items is (row).
                // internalId for Child items is (parentRow << 20) | childRow | (1ULL << 63).
                // Max 1 million workflows, 1 million groups.

                quintptr combined = (parentId << 20) | row;
                combined |= (1ULL << 60); // Mark as child
                return createIndex(row, column, combined);
            }
        }
    }

    return QModelIndex();
}

QModelIndex WorkflowHistoryModel::parent(const QModelIndex& index) const
{
    if (!index.isValid())
        return QModelIndex();

    quintptr id = index.internalId();
    bool isChild = (id & (1ULL << 60));

    if (!isChild)
        return QModelIndex(); // Root item has no parent

    // Extract parent row
    quintptr parentRow = (id & ~(1ULL << 60)) >> 20;
    if (parentRow < m_workflows.size())
        return createIndex(static_cast<int>(parentRow), 0, parentRow);

    return QModelIndex();
}

int WorkflowHistoryModel::rowCount(const QModelIndex& parent) const
{
    if (!parent.isValid())
        return static_cast<int>(m_workflows.size());

    quintptr id = parent.internalId();
    bool isChild = (id & (1ULL << 60));

    if (isChild)
        return 0; // Groups don't have children

    // Root item: return number of groups
    if (id < m_workflows.size())
        return static_cast<int>(m_workflows[id].groups.size());

    return 0;
}

int WorkflowHistoryModel::columnCount(const QModelIndex& parent) const
{
    return 1;
}

QVariant WorkflowHistoryModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
        return QVariant();

    quintptr id = index.internalId();
    bool isChild = (id & (1ULL << 60));

    if (!isChild)
    {
        // Root item (Workflow)
        if (id >= m_workflows.size())
            return QVariant();
        const auto& wf = m_workflows[id];

        if (role == Qt::ToolTipRole)
        {
            QStringList lines;
            lines << QStringLiteral("<b>%1</b>").arg(statusLabel(wf.status));
            lines << QStringLiteral("");
            lines << tr("Date: %1").arg(wf.timestamp.toString(QStringLiteral("dddd, MMMM d yyyy — h:mm AP")));
            lines << tr("Mode: %1").arg(modeLabel(wf.mode));
            lines << tr("Duration: %1").arg(formatDurationHuman(wf.totalDurationMs));

            const int groupCount = static_cast<int>(wf.groups.size());
            int successCount = 0;
            for (const auto& g : wf.groups)
            {
                if (g.success)
                    ++successCount;
            }
            lines << tr("Masters: %1 of %2 groups").arg(successCount).arg(groupCount);

            if (wf.warningCount > 0)
            {
                lines << tr("Warnings: %1").arg(wf.warningCount);
            }

            if (!wf.runDirPath.isEmpty())
            {
                lines << QStringLiteral("");
                lines << tr("Output folder: %1").arg(wf.runDirPath);
            }

            return lines.join(QStringLiteral("<br>"));
        }

        switch (role)
        {
        case ExecutionIdRole:
            return wf.executionId;
        case TimestampRole:
            return wf.timestamp;
        case ModeRole:
            return wf.mode;
        case StatusRole:
            return wf.status;
        case DurationRole:
            return wf.totalDurationMs;
        case GroupCountRole:
            return static_cast<int>(wf.groups.size());
        case MasterCountRole: {
            int masters = 0;
            for (const auto& g : wf.groups)
                if (g.success)
                    masters++;
            return masters;
        }
        case ManifestPathRole:
            return wf.manifestPath;
        case ReportPathRole:
            return wf.reportPath;
        case LogPathRole:
            return wf.logPath;
        case RunDirPathRole:
            return wf.runDirPath;
        case IsExpandedRole:
            return wf.expanded;
        case CanExpandRole:
            return !wf.groups.empty();
        case IsGroupItemRole:
            return false;
        default:
            return QVariant();
        }
    }
    else
    {
        // Child item (Group)
        quintptr parentRow = (id & ~(1ULL << 60)) >> 20;
        quintptr childRow = (id & 0xFFFFF); // mask out parent and flag

        if (parentRow >= m_workflows.size())
            return QVariant();
        const auto& wf = m_workflows[parentRow];

        if (childRow >= wf.groups.size())
            return QVariant();
        const auto& gr = wf.groups[childRow];

        if (role == Qt::ToolTipRole)
        {
            QStringList lines;
            lines << QStringLiteral("<b>%1</b>").arg(gr.groupKey.toHtmlEscaped());
            lines << QStringLiteral("");
            lines << tr("Status: %1").arg(gr.success ? tr("Success") : tr("Failed"));
            lines << tr("Light frames: %1").arg(gr.lightFrameCount);
            lines << tr("Duration: %1").arg(formatDurationHuman(gr.durationMs));

            if (gr.outputBytes > 0)
            {
                lines << tr("File size: %1").arg(formatFileSizeHuman(gr.outputBytes));
            }

            if (!gr.outputFilename.isEmpty())
            {
                lines << QStringLiteral("");
                lines << tr("Output: %1").arg(gr.outputFilename.toHtmlEscaped());
            }

            if (!gr.outputPath.isEmpty())
            {
                lines << tr("Path: %1").arg(gr.outputPath.toHtmlEscaped());
            }

            if (!gr.masterContext.isEmpty())
            {
                lines << QStringLiteral("");
                const QStringList contextLines = gr.masterContext.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
                for (const QString& contextLine : contextLines)
                {
                    lines << contextLine.toHtmlEscaped();
                }
            }

            return lines.join(QStringLiteral("<br>"));
        }

        switch (role)
        {
        case GroupKeyRole:
            return gr.groupKey;
        case RawGroupKeyRole:
            return gr.rawGroupKey;
        case OutputPathRole:
            return gr.outputPath;
        case OutputFilenameRole:
            return gr.outputFilename;
        case OutputSizeRole:
            return gr.outputBytes;
        case ThumbnailPathRole:
            return gr.previewPath;
        case FrameCountRole:
            return gr.lightFrameCount;
        case ResolutionRole:
            return gr.resolution;
        case ObjectRole:
            return gr.object;
        case FilterRole:
            return gr.filter;
        case ExposureRole:
            return gr.exposure;
        case RegistrationGroupKeyRole:
            return gr.registrationGroupKey;
        case ArchivedOutputPathsRole:
            return gr.archivedOutputPaths;
        case MasterContextRole:
            return gr.masterContext;
        case CanCompareMasterRole:
            return groupHasComparableMaster(static_cast<int>(parentRow), static_cast<int>(childRow));
        case DurationRole:
            return gr.durationMs;
        case StatusRole:
            return gr.success ? QStringLiteral("success") : QStringLiteral("failed");
        case IsExpandedRole:
            return false;
        case CanExpandRole:
            return false;
        case IsGroupItemRole:
            return true;
        default:
            return QVariant();
        }
    }
}

bool WorkflowHistoryModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (!index.isValid() || role != IsExpandedRole)
    {
        return false;
    }

    const quintptr id = index.internalId();
    const bool isChild = (id & (1ULL << 60));
    if (isChild || id >= m_workflows.size())
    {
        return false;
    }

    const bool expanded = value.toBool();
    auto& workflow = m_workflows[static_cast<size_t>(id)];
    if (workflow.expanded == expanded)
    {
        return true;
    }

    workflow.expanded = expanded;
    m_expandedStateByExecutionId.insert(workflow.executionId, expanded);
    emit dataChanged(index, index, {IsExpandedRole});
    return true;
}

Qt::ItemFlags WorkflowHistoryModel::flags(const QModelIndex& index) const
{
    if (!index.isValid())
    {
        return Qt::NoItemFlags;
    }

    Qt::ItemFlags itemFlags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    const quintptr id = index.internalId();
    const bool isChild = (id & (1ULL << 60));
    if (!isChild)
    {
        itemFlags |= Qt::ItemIsEditable;
    }
    return itemFlags;
}
