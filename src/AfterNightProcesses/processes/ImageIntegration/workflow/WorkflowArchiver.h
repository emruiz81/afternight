// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/ImageIntegration/workflow/ArchiveFileTransport.h"
#include "processes/ImageIntegration/workflow/WorkflowExecutor.h"
#include "processes/ImageIntegration/workflow/WorkflowManifest.h"
#include "processes/ImageIntegration/workflow/WorkflowRunArchive.h"

#include <QByteArray>
#include <QDateTime>
#include <QObject>
#include <QString>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace ImageIntegration::Reporting
{
struct ReportModel;
} // namespace ImageIntegration::Reporting

/**
 * @brief Manages archiving of workflow execution results.
 *
 * Handles creation of the 'history/<execution_id>' directory, snapshotting
 * current outputs, preserving staged previous outputs, and generation of the
 * consolidated 'workflow_manifest.json' and of the run's report: its model as
 * 'workflow_report.json' and the document written from it, 'workflow_report.html'.
 *
 * The report's model is built once, as the run is committed, and kept. The run's
 * final timings, its diagnostics and what the engine counted of its memory are
 * known only after the commit, and its log is complete last, so the two final
 * steps state them on the kept model and write both files again. A report is
 * therefore never a text with figures put into it afterwards.
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

    /// When, where and by which version of the application an archive says it was written.
    struct ArchiveStamp
    {
        /// The instant, with the zone or the offset the archive states its local time in.
        QDateTime archivedAt;

        /// The zone's IANA name.
        QString timeZoneId;

        /// The application's version; empty when it states none.
        QString applicationVersion;
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

    /// States what the archive says of when, where and by which version it was written, in place of the host's
    /// clock and zone and the running application's version: what a run of another time and place is archived with.
    ///
    /// @param stamp what the archives this archiver commits state.
    void setArchiveStamp(ArchiveStamp stamp);

private:
    friend class WorkflowArchiverRunArchive;

    using CapturePhaseDuration = std::function<qint64()>;

    QString m_outputDir;
    int m_maxHistoryEntries = 0;
    qint64 m_maxHistoryBytes = 0;
    QString m_runDirectory;
    QString m_manifestPath;
    QString m_logPath;
    /// The model of the run last committed, kept so the final steps state the run's last figures on it; null for a
    /// run that has no report.
    std::shared_ptr<ImageIntegration::Reporting::ReportModel> m_reportModel;
    std::optional<ArchiveStamp> m_archiveStamp;
    WorkflowExecutionCounterAccumulator* m_counters = nullptr;
    const an::consumer::EngineSession* m_session = nullptr;
    ImageIntegration::ArchiveFileTransport m_archiveTransport;
    WorkflowLogWriter m_workflowLogWriter;

    ArchiveResult commit(const WorkflowManifest& manifest, WorkflowExecutor::Result& result,
                         const CapturePhaseDuration& capturePhaseDuration);
    void finalizeTimingArtifacts(const WorkflowExecutor::Result& result);
    void finalizeLogArtifact(const WorkflowExecutor::Result& result);
    void cleanupOldArchives(const QString& protectedRunPath = {});

    /// Writes the kept model and its document into the run's folder, and logs why when it cannot.
    ///
    /// @return the document written; empty when the report was not stored.
    QString storeKeptReport();
};

namespace ImageIntegration
{
/// The folder each group's pictures and payloads are archived in, by the group's place in the list: the group's key
/// made a file name, with a hash of the key appended where two keys of the list give the same name. An archive lists
/// the groups it has results for first, then the groups the run reused.
///
/// @param groupKeys the groups' keys.
/// @return one folder name per key, in the same order.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::vector<QString>
archiveGroupFolderNames(const std::vector<QString>& groupKeys);

/// A calibration master of a run and the preview its archive keeps of it.
struct ArchivedCalibrationPreview
{
    /// The master's kind - "Bias", "Dark" or "Flat" - and its group.
    QString type;
    QString groupKey;

    /// The master's file: the one the user supplied, else the run's own under its output directory.
    QString masterPath;

    /// The preview's file name under `report_previews/calibration/`: the master's own name, with a hash of its kind
    /// and group appended where two masters of the run share a name.
    QString fileName;
};

/// The calibration masters of a run with their previews' names: its bias, dark and flat groups in the manifest's
/// order.
///
/// @param manifest the run's manifest.
/// @return one entry per calibration group.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::vector<ArchivedCalibrationPreview>
archivedCalibrationPreviews(const WorkflowManifest& manifest);

/// Whether a run is given a report: one that stacks, and not one that only registers.
///
/// @param manifest the run's manifest.
/// @param result the run's result.
[[nodiscard]] AFTERNIGHTPROCESSES_API bool runHasWorkflowReport(const WorkflowManifest& manifest,
                                                                const WorkflowExecutor::Result& result);

/// The id of a run that starts now in an output folder: the stamp of the wall clock, `yyyyMMdd_HHmmss_zzz`, which
/// names the run's folder under `history/`, made to sort after every run the history already holds.
///
/// The history's retention and a report's earlier runs both take the folder whose name sorts first for the run
/// that started first. A wall clock goes back - at the end of summer time, in another time zone, when it is set -
/// and a run named by it alone would then sort before the runs made earlier: the history would drop it as its
/// oldest, and its report would find no earlier run to set its masters beside. So a run that starts at or before
/// the latest stamp of the history is named one millisecond after that stamp.
///
/// @param outputDirectory the run's output folder, whose `history/` holds the earlier runs; one with no history
///        gives the clock's own stamp.
/// @param now the wall clock's time, whose date and time are stamped as they read.
/// @return the stamp of `now`, or the latest stamp among the history's folders plus one millisecond when that
///         folder is at or after `now`. A folder whose name is no such stamp is not counted.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString executionIdAfterHistory(const QString& outputDirectory,
                                                                      const QDateTime& now);

/// The most the files an archive writes after it decides whether the run's payloads fit the history can weigh: the
/// manifest, the log, and the report's model and document.
///
/// The decision is taken before those files exist, so that a payload is never copied into the history and then
/// found not to fit. The report grows with the run - with its lights, its masters, its log and its messages - so
/// the bound is the manifest's, the results' and the log's own bytes, each counted for the three files that carry
/// it, and a base, an amount a light and an amount a master for the report (`Config::kReportArchiveBaseBytes` and
/// the two beside it).
///
/// @param manifest the run's manifest, as it is persisted.
/// @param result the run's result.
/// @return the bound, in bytes.
[[nodiscard]] AFTERNIGHTPROCESSES_API qint64 estimatedArchiveMetadataBytes(const WorkflowManifest& manifest,
                                                                           const WorkflowExecutor::Result& result);
} // namespace ImageIntegration

/// The archive a run commits through: this archiver over the run's output directory.
///
/// @param outputDir the run's output directory.
/// @param counters the run's diagnostics, if collected.
/// @param session the session the report's previews and figures are read in; without one the report has none.
/// @return the archive.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::unique_ptr<IWorkflowRunArchive>
makeWorkflowArchiverRunArchive(const QString& outputDir, WorkflowExecutionCounterAccumulator* counters,
                               const an::consumer::EngineSession* session);
