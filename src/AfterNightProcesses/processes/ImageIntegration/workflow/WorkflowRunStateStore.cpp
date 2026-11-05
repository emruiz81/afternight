// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "WorkflowRunStateStore.h"

#include "processes/ImageIntegration/workflow/ArtifactSchema.h"

#include <AfterNightPlatform/Logging.h>

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace
{
constexpr char kLastManifestFile[] = "last_successful_manifest.json";
constexpr char kLastRunFile[] = "last_successful_run.json";

[[nodiscard]] bool writeAllAndCommit(QSaveFile& file, const QByteArray& payload)
{
    if (!file.open(QIODevice::WriteOnly))
    {
        return false;
    }

    if (file.write(payload) != payload.size())
    {
        file.cancelWriting();
        return false;
    }

    return file.commit();
}

QString normalizeScopeToken(QString scope)
{
    scope = scope.trimmed().toLower();
    if (scope.isEmpty())
    {
        scope = QStringLiteral("full");
    }

    QString normalized;
    normalized.reserve(scope.size());
    for (const QChar ch : scope)
    {
        if (ch.isLetterOrNumber())
        {
            normalized.append(ch);
        }
        else
        {
            normalized.append(QLatin1Char('_'));
        }
    }

    if (normalized.isEmpty())
    {
        normalized = QStringLiteral("full");
    }

    return normalized;
}

QString scopeKeyHashToken(QString scopeKey)
{
    scopeKey = scopeKey.trimmed();
    if (scopeKey.isEmpty())
    {
        return QStringLiteral("default");
    }

    return QString::fromLatin1(
        QCryptographicHash::hash(scopeKey.toUtf8(), QCryptographicHash::Sha256).toHex().left(16));
}
} // namespace

WorkflowRunStateStore::WorkflowRunStateStore(QString outputDirectory) : m_outputDirectory(std::move(outputDirectory)) {}

bool WorkflowRunStateStore::saveLastSuccessfulManifest(const WorkflowManifest& manifest)
{
    const QString dir = stateDirectory();
    if (!QDir().mkpath(dir))
        return false;

    const QString scope =
        manifest.executionScope.trimmed().isEmpty() ? QStringLiteral("full") : manifest.executionScope;
    const WorkflowManifest persistedManifest = manifest.redactedForPersistence();
    QJsonDocument doc(persistedManifest.toJson());
    QSaveFile file(manifestPath(scope, manifest.executionScopeKey));
    return writeAllAndCommit(file, doc.toJson());
}

std::optional<WorkflowManifest> WorkflowRunStateStore::loadLastSuccessfulManifest() const
{
    return loadLastSuccessfulManifest(QStringLiteral("full"), QString());
}

std::optional<WorkflowManifest>
WorkflowRunStateStore::loadLastSuccessfulManifest(const QString& executionScope, const QString& executionScopeKey) const
{
    QFile file(manifestPath(executionScope, executionScopeKey));
    if (!file.exists() || !file.open(QIODevice::ReadOnly))
        return std::nullopt;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (doc.isNull())
        return std::nullopt;

    // A manifest is reused only at the format the writer writes: one of another format, or one that states none, is
    // not read, and the run recomputes what it described.
    WorkflowManifest manifest = WorkflowManifest::fromJson(doc.object());
    if (!manifest.isCurrentFormat())
    {
        LOG_WARNING("WorkflowRunStateStore: the last manifest '{}' states format {}; this version of AfterNight reads "
                    "format {}, so it is not reused",
                    file.fileName().toStdString(), manifest.version, WorkflowManifest::kFormatVersion);
        return std::nullopt;
    }
    return manifest;
}

QString WorkflowRunStateStore::lastSuccessfulManifestPath(const QString& executionScope,
                                                          const QString& executionScopeKey) const
{
    return manifestPath(executionScope, executionScopeKey);
}

bool WorkflowRunStateStore::saveRunMetadata(const RunMetadata& metadata)
{
    const QString dir = stateDirectory();
    if (!QDir().mkpath(dir))
        return false;

    QJsonObject obj;
    obj.insert(QStringLiteral("timestamp"), metadata.timestamp);
    obj.insert(QStringLiteral("appVersion"), metadata.appVersion);
    obj.insert(QStringLiteral("success"), metadata.success);
    obj.insert(QStringLiteral("executionId"), metadata.executionId);
    obj.insert(QStringLiteral("executionScope"), metadata.executionScope);
    obj.insert(QStringLiteral("executionScopeKey"), metadata.executionScopeKey);
    obj.insert(QStringLiteral("executionMode"), metadata.executionMode);
    obj.insert(QStringLiteral("overallResult"), metadata.overallResult);
    obj.insert(QStringLiteral("totalDurationMs"), metadata.totalDurationMs);
    obj.insert(QStringLiteral("stageExecutionMs"), metadata.stageExecutionMs);
    obj.insert(QStringLiteral("stateCommitMs"), metadata.stateCommitMs);
    obj.insert(QStringLiteral("archiveMs"), metadata.archiveMs);
    obj.insert(QStringLiteral("reportMs"), metadata.reportMs);
    obj.insert(QStringLiteral("warningCount"), metadata.warningCount);
    obj.insert(QStringLiteral("errorCount"), metadata.errorCount);
    obj.insert(QStringLiteral("integrationGroupCount"), metadata.integrationGroupCount);
    obj.insert(QStringLiteral("successfulGroupCount"), metadata.successfulGroupCount);
    obj.insert(QStringLiteral("cancelled"), metadata.cancelled);
    obj.insert(QStringLiteral("engineGeneration"),
               QString::fromLatin1(ImageIntegration::schema::kWorkflowEngineGeneration));

    QJsonDocument doc(obj);
    QSaveFile file(metadataPath());
    return writeAllAndCommit(file, doc.toJson());
}

std::optional<WorkflowRunStateStore::RunMetadata> WorkflowRunStateStore::loadRunMetadata() const
{
    QFile file(metadataPath());
    if (!file.exists() || !file.open(QIODevice::ReadOnly))
        return std::nullopt;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (doc.isNull())
        return std::nullopt;

    QJsonObject obj = doc.object();
    RunMetadata meta;
    meta.timestamp = obj.value(QStringLiteral("timestamp")).toVariant().toLongLong();
    meta.appVersion = obj.value(QStringLiteral("appVersion")).toString();
    meta.success = obj.value(QStringLiteral("success")).toBool();
    meta.executionId = obj.value(QStringLiteral("executionId")).toString();
    meta.executionScope = obj.value(QStringLiteral("executionScope")).toString();
    meta.executionScopeKey = obj.value(QStringLiteral("executionScopeKey")).toString();
    meta.executionMode = obj.value(QStringLiteral("executionMode")).toString();
    meta.overallResult = obj.value(QStringLiteral("overallResult")).toString();
    meta.totalDurationMs = obj.value(QStringLiteral("totalDurationMs")).toInt();
    meta.stageExecutionMs = obj.value(QStringLiteral("stageExecutionMs")).toInteger();
    meta.stateCommitMs = obj.value(QStringLiteral("stateCommitMs")).toInteger();
    meta.archiveMs = obj.value(QStringLiteral("archiveMs")).toInteger();
    meta.reportMs = obj.value(QStringLiteral("reportMs")).toInteger();
    meta.warningCount = obj.value(QStringLiteral("warningCount")).toInt();
    meta.errorCount = obj.value(QStringLiteral("errorCount")).toInt();
    meta.integrationGroupCount = obj.value(QStringLiteral("integrationGroupCount")).toInt();
    meta.successfulGroupCount = obj.value(QStringLiteral("successfulGroupCount")).toInt();
    meta.cancelled = obj.value(QStringLiteral("cancelled")).toBool();
    meta.engineGeneration = obj.value(QStringLiteral("engineGeneration")).toString();
    return meta;
}

void WorkflowRunStateStore::clearHistory()
{
    QDir(stateDirectory()).removeRecursively();
}

bool WorkflowRunStateStore::hasHistory() const
{
    return QFile::exists(manifestPath());
}

QString WorkflowRunStateStore::stateDirectory() const
{
    return QDir(m_outputDirectory).filePath(QStringLiteral(".afternight/image_integration"));
}

QString WorkflowRunStateStore::manifestPath() const
{
    return manifestPath(QStringLiteral("full"), QString());
}

QString WorkflowRunStateStore::manifestPath(const QString& executionScope, const QString& executionScopeKey) const
{
    const QString normalized_scope = normalizeScopeToken(executionScope);
    const QString normalized_key = executionScopeKey.trimmed();

    if (normalized_scope == QStringLiteral("full") && normalized_key.isEmpty())
    {
        return QDir(stateDirectory()).filePath(kLastManifestFile);
    }

    const QString scoped_file = QStringLiteral("last_successful_manifest_%1_%2.json")
                                    .arg(normalized_scope)
                                    .arg(scopeKeyHashToken(normalized_key));
    return QDir(stateDirectory()).filePath(scoped_file);
}

QString WorkflowRunStateStore::metadataPath() const
{
    return QDir(stateDirectory()).filePath(kLastRunFile);
}
