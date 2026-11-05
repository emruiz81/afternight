// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "processes/ImageIntegration/workflow/WorkflowRunStateStore.h"

TEST(WorkflowRunStateStore, SavesAndLoadsRichRunMetadata)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    WorkflowRunStateStore store(tempDir.path());

    WorkflowRunStateStore::RunMetadata saved;
    saved.timestamp = 1700000000;
    saved.appVersion = QStringLiteral("1.2.3");
    saved.success = true;
    saved.executionId = QStringLiteral("20260210_143022_001");
    saved.executionScope = QStringLiteral("incremental");
    saved.executionScopeKey = QStringLiteral("Ha_120s_G100");
    saved.executionMode = QStringLiteral("Incremental");
    saved.overallResult = QStringLiteral("success");
    saved.totalDurationMs = 654321;
    saved.stageExecutionMs = 600000;
    saved.stateCommitMs = 4321;
    saved.archiveMs = 40000;
    saved.reportMs = 10000;
    saved.warningCount = 2;
    saved.errorCount = 1;
    saved.integrationGroupCount = 3;
    saved.successfulGroupCount = 2;
    saved.cancelled = false;

    ASSERT_TRUE(store.saveRunMetadata(saved));

    const auto loadedOpt = store.loadRunMetadata();
    ASSERT_TRUE(loadedOpt.has_value());
    const auto& loaded = loadedOpt.value();

    EXPECT_EQ(loaded.timestamp, saved.timestamp);
    EXPECT_EQ(loaded.appVersion, saved.appVersion);
    EXPECT_EQ(loaded.success, saved.success);
    EXPECT_EQ(loaded.executionId, saved.executionId);
    EXPECT_EQ(loaded.executionScope, saved.executionScope);
    EXPECT_EQ(loaded.executionScopeKey, saved.executionScopeKey);
    EXPECT_EQ(loaded.executionMode, saved.executionMode);
    EXPECT_EQ(loaded.overallResult, saved.overallResult);
    EXPECT_EQ(loaded.totalDurationMs, saved.totalDurationMs);
    EXPECT_EQ(loaded.stageExecutionMs, saved.stageExecutionMs);
    EXPECT_EQ(loaded.stateCommitMs, saved.stateCommitMs);
    EXPECT_EQ(loaded.archiveMs, saved.archiveMs);
    EXPECT_EQ(loaded.reportMs, saved.reportMs);
    EXPECT_EQ(loaded.warningCount, saved.warningCount);
    EXPECT_EQ(loaded.errorCount, saved.errorCount);
    EXPECT_EQ(loaded.integrationGroupCount, saved.integrationGroupCount);
    EXPECT_EQ(loaded.successfulGroupCount, saved.successfulGroupCount);
    EXPECT_EQ(loaded.cancelled, saved.cancelled);
}

TEST(WorkflowRunStateStore, RedactsAstrometryApiKeyFromPersistedManifest)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    WorkflowManifest manifest;
    manifest.outputDirectory = tempDir.path();
    manifest.plateSolve.defaultSolver = QStringLiteral("astrometry_web");
    manifest.plateSolve.astrometryApiKey = QStringLiteral("run-state-api-key");

    WorkflowRunStateStore store(tempDir.path());
    ASSERT_TRUE(store.saveLastSuccessfulManifest(manifest));

    const QString manifestPath =
        QDir(tempDir.path()).filePath(QStringLiteral(".afternight/image_integration/last_successful_manifest.json"));
    QFile file(manifestPath);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const QByteArray payload = file.readAll();
    EXPECT_FALSE(QString::fromUtf8(payload).contains(QStringLiteral("run-state-api-key")));

    const QJsonObject root = QJsonDocument::fromJson(payload).object();
    const QJsonObject plateSolve = root.value(QStringLiteral("plateSolve")).toObject();
    EXPECT_FALSE(plateSolve.contains(QStringLiteral("astrometryApiKey")));
    EXPECT_TRUE(plateSolve.value(QStringLiteral("astrometryApiKeyRedacted")).toBool());
    EXPECT_FALSE(plateSolve.value(QStringLiteral("astrometryApiKeyHash")).toString().isEmpty());

    const auto loaded = store.loadLastSuccessfulManifest();
    ASSERT_TRUE(loaded.has_value());
    EXPECT_TRUE(loaded->plateSolve.astrometryApiKey.isEmpty());
    EXPECT_EQ(loaded->astrometryApiKeyHash, plateSolve.value(QStringLiteral("astrometryApiKeyHash")).toString());
}

// The last manifest is reused only at the format its writer writes: one that states another format, or none, is not
// read, so the run recomputes what that manifest described rather than reading it as this version's.
TEST(WorkflowRunStateStore, ALastManifestOfAnotherFormatIsNotReused)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    WorkflowManifest manifest;
    manifest.outputDirectory = tempDir.path();
    WorkflowRunStateStore store(tempDir.path());
    ASSERT_TRUE(store.saveLastSuccessfulManifest(manifest));
    ASSERT_TRUE(store.loadLastSuccessfulManifest().has_value());

    const QString manifestPath =
        QDir(tempDir.path()).filePath(QStringLiteral(".afternight/image_integration/last_successful_manifest.json"));
    QFile file(manifestPath);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const QJsonObject written = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    EXPECT_EQ(written.value(QStringLiteral("version")).toInt(), 1);

    for (const QJsonValue& version : {QJsonValue(2), QJsonValue(0), QJsonValue(QJsonValue::Undefined)})
    {
        QJsonObject other = written;
        if (version.isUndefined())
        {
            other.remove(QStringLiteral("version"));
        }
        else
        {
            other.insert(QStringLiteral("version"), version);
        }
        ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(QJsonDocument(other).toJson(QJsonDocument::Compact));
        file.close();
        EXPECT_FALSE(store.loadLastSuccessfulManifest().has_value()) << version.toInt(-1);
    }
}
