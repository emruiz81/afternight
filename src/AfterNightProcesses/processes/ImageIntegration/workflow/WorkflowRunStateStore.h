// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "WorkflowManifest.h"

#include <QDateTime>
#include <QString>

#include <optional>

/// How a run ended, as its record states it.
enum class WorkflowRunEnd
{
    /// Every master was made or was still valid, and the run stated no warning.
    Completed,

    /// The run completed and stated at least one warning.
    CompletedWithWarnings,

    /// The run went to its end, and at least one master it planned was not made.
    Partial,

    /// The run failed.
    Failed,

    /// The person stopped the run.
    Cancelled
};

/// What a run leaves in its output folder as it ends, whatever its end: when it ended, how long it took, how it ended
/// and where it stopped, and the masters it left. The integration jobs' list reads a job's state from it.
struct WorkflowRunRecord
{
    /// When the run ended, in UTC to the millisecond.
    QDateTime ended;

    /// How long the run took.
    qint64 durationMs = 0;

    WorkflowRunEnd end = WorkflowRunEnd::Completed;

    /// The stage a failed or a cancelled run stopped at, by its stage's name (`LightsRegistration`); empty for a run
    /// that completed, and for one that stopped before its first stage or between two.
    QString stage;

    /// The masters of the lights the run left: the ones it made and the ones it found still valid.
    int masters = 0;

    /// The warnings the run stated.
    int warnings = 0;

    [[nodiscard]] bool operator==(const WorkflowRunRecord&) const = default;
};

/// The format a run's record states, and the version this application writes and reads.
inline constexpr char kWorkflowRunRecordFormat[] = "afternight-integration-run";
inline constexpr int kWorkflowRunRecordVersion = 1;

/**
 * @class WorkflowRunStateStore
 * @brief Manages persistence of workflow state (manifests, run metadata).
 *
 * State is stored in:
 * [OutputDir]/.afternight/image_integration/
 *   - last_successful_manifest.json (full scope)
 *   - last_successful_manifest_<scope>_<keyhash>.json (scoped runs)
 *   - last_successful_run.json
 *   - last_run.json: the record of the last run, whatever its end (`WorkflowRunRecord`)
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

    /// Writes the record of the run that just ended in place of the last one, whole: to a file beside it that takes
    /// its place once complete. False when the folder or the file could not be written.
    bool saveRunRecord(const WorkflowRunRecord& record);

    /// The record of the last run; nothing when no run left one, or when the file is not a record of this format and
    /// version: a record is read strictly and never guessed at.
    [[nodiscard]] std::optional<WorkflowRunRecord> loadRunRecord() const;

    /// Where the record of the last run is kept.
    [[nodiscard]] QString runRecordPath() const;

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
