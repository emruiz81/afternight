// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/ImageIntegration/workflow/ArchiveFileTransport.h"
#include "processes/ImageIntegration/workflow/WorkflowExecutor.h"
#include "processes/ImageIntegration/workflow/WorkflowManifest.h"
#include "processes/ImageIntegration/workflow/WorkflowRunArchive.h"

#include <QByteArray>
#include <QObject>
#include <QString>

#include <functional>
#include <memory>
#include <vector>

/**
 * @brief Manages archiving of workflow execution results.
 *
 * Handles creation of the 'history/<execution_id>' directory, snapshotting
 * current outputs, preserving staged previous outputs, and generation of the
 * consolidated 'workflow_manifest.json' and 'workflow_report.md'.
 */
class AFTERNIGHTPROCESSES_API WorkflowArchiver
{
public:
    using WorkflowLogWriter = std::function<bool(const QString&, const QByteArray&)>;

    struct ArchiveResult
    {
        bool success = false;
        QString archivePath;
        QString manifestPath;
        QString reportPath;
        QString error;
        int itemsArchived = 0;
        qint64 archiveMs = 0;
        qint64 reportMs = 0;
        ImageIntegration::ArchiveTransportDiagnostics transportDiagnostics;
        std::vector<ImageIntegration::ArchiveFileTransportResult> payloadTransports;
    };

    explicit WorkflowArchiver(const QString& outputDir, WorkflowExecutionCounterAccumulator* counters = nullptr);
    WorkflowArchiver(const QString& outputDir, WorkflowExecutionCounterAccumulator* counters,
                     ImageIntegration::ArchiveFileTransport archiveTransport);
    WorkflowArchiver(const QString& outputDir, int maxHistoryEntries, qint64 maxHistoryBytes,
                     WorkflowExecutionCounterAccumulator* counters = nullptr);
    WorkflowArchiver(const QString& outputDir, int maxHistoryEntries, qint64 maxHistoryBytes,
                     WorkflowExecutionCounterAccumulator* counters,
                     ImageIntegration::ArchiveFileTransport archiveTransport);
    WorkflowArchiver(const QString& outputDir, WorkflowExecutionCounterAccumulator* counters,
                     WorkflowLogWriter workflowLogWriter);

    /**
     * @brief Repairs/removes orphaned staged files left after interrupted runs.
     *
     * A staged file has the suffix ".tmp_archive_<uuid>".
     * If its original file is missing, it is restored in-place; otherwise the
     * orphan is removed.
     *
     * @return Number of orphaned staged files processed.
     */
    [[nodiscard]] static int cleanupOrphanedStagedFiles(const QString& outputDir);

    /**
     * @brief Commit the workflow execution to the archive.
     *
     * Copies current outputs into the archive folder, moves any staged files
     * renamed by stages into the archive, and writes the workflow manifest,
     * report, and log.
     *
     * @param manifest The executed workflow manifest.
     * @param result The execution results.
     * @return Result of the archive operation.
     */
    ArchiveResult commit(const WorkflowManifest& manifest, WorkflowExecutor::Result& result);
    ArchiveResult commit(const WorkflowManifest& manifest, const WorkflowExecutor::Result& result);

    /**
     * @brief Revert staged files if the workflow failed/cancelled.
     *
     * Moves any staged files (e.g. 'foo.fits.tmp_archive_<uuid>') back to
     * their original locations ('foo.fits'), restoring the previous state.
     *
     * @param result The execution results containing staged files.
     */
    void revertStagedFiles(const WorkflowExecutor::Result& result);

    /// Names the session the report's previews, images and headers are read in.
    ///
    /// @param session the session, borrowed for as long as this archiver commits; null, the default, writes no preview
    ///        the run did not prepare and a report that states no figure needing a file read.
    void setEngineSession(const an::consumer::EngineSession* session);

private:
    friend class WorkflowArchiverRunArchive;

    using CapturePhaseDuration = std::function<qint64()>;

    QString m_outputDir;
    int m_maxHistoryEntries = 0;
    qint64 m_maxHistoryBytes = 0;
    QString m_reportMarkdownPath;
    QString m_reportHtmlPath;
    QString m_reportMarkdownTemplate;
    QString m_reportHtmlTemplate;
    QString m_manifestPath;
    QString m_logPath;
    WorkflowExecutionCounterAccumulator* m_counters = nullptr;
    const an::consumer::EngineSession* m_session = nullptr;
    ImageIntegration::ArchiveFileTransport m_archiveTransport;
    WorkflowLogWriter m_workflowLogWriter;

    ArchiveResult commit(const WorkflowManifest& manifest, WorkflowExecutor::Result& result,
                         const CapturePhaseDuration& capturePhaseDuration);
    void finalizeTimingArtifacts(const WorkflowExecutor::Result& result) const;
    void finalizeLogArtifact(const WorkflowExecutor::Result& result) const;
    void cleanupOldArchives(const QString& protectedRunPath = {});
};

/// The archive a run commits through: this archiver over the run's output directory.
///
/// @param outputDir the run's output directory.
/// @param counters the run's diagnostics, if collected.
/// @param session the session the report's previews and figures are read in; without one the report has none.
/// @return the archive.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::unique_ptr<IWorkflowRunArchive>
makeWorkflowArchiverRunArchive(const QString& outputDir, WorkflowExecutionCounterAccumulator* counters,
                               const an::consumer::EngineSession* session);
