// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "WorkflowManifest.h"

#include <optional>

/**
 * @class WorkflowRunStateStore
 * @brief Manages persistence of workflow state (manifests, run metadata).
 *
 * State is stored in:
 * [OutputDir]/.afternight/image_integration/
 *   - last_successful_manifest.json (full scope)
 *   - last_successful_manifest_<scope>_<keyhash>.json (scoped runs)
 *   - last_successful_run.json
 */
class AFTERNIGHTPROCESSES_API WorkflowRunStateStore
{
public:
    explicit WorkflowRunStateStore(QString outputDirectory);

    // Persist manifest after successful run
    bool saveLastSuccessfulManifest(const WorkflowManifest& manifest);

    // Load manifest from previous run
    [[nodiscard]] std::optional<WorkflowManifest> loadLastSuccessfulManifest() const;
    [[nodiscard]] std::optional<WorkflowManifest>
    loadLastSuccessfulManifest(const QString& executionScope, const QString& executionScopeKey = QString()) const;
    [[nodiscard]] QString lastSuccessfulManifestPath(const QString& executionScope,
                                                     const QString& executionScopeKey = QString()) const;

    // Run metadata
    struct RunMetadata
    {
        qint64 timestamp = 0;
        QString appVersion;
        bool success = false;
        QString executionId;
        QString executionScope;
        QString executionScopeKey;
        QString executionMode;
        QString overallResult;
        int totalDurationMs = 0;
        qint64 stageExecutionMs = 0;
        qint64 stateCommitMs = 0;
        qint64 archiveMs = 0;
        qint64 reportMs = 0;
        int warningCount = 0;
        int errorCount = 0;
        int integrationGroupCount = 0;
        int successfulGroupCount = 0;
        bool cancelled = false;
        /// The engine generation the run's results came from. Every record this version saves states
        /// `kWorkflowEngineGeneration`; a record of another generation, or one that states none, tells the first run
        /// on this engine apart from the runs after it.
        QString engineGeneration;
    };
    bool saveRunMetadata(const RunMetadata& metadata);
    [[nodiscard]] std::optional<RunMetadata> loadRunMetadata() const;

    // Helpers
    void clearHistory();
    [[nodiscard]] bool hasHistory() const;

private:
    [[nodiscard]] QString stateDirectory() const;
    [[nodiscard]] QString manifestPath() const;
    [[nodiscard]] QString manifestPath(const QString& executionScope, const QString& executionScopeKey) const;
    [[nodiscard]] QString metadataPath() const;

    QString m_outputDirectory;
};
