// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The integration jobs: every session of the session databases Image Integration keeps - the one outside any project
/// and the open project's - each with its frames, and with what its last run left in its output folder.

#include "AfterNightProcessesExport.h"

#include <QAbstractTableModel>
#include <QDateTime>
#include <QString>
#include <QVariant>
#include <verdigris/wobjectdefs.h>

#include <memory>
#include <optional>
#include <vector>

namespace ImageIntegration
{

/// The name of the session database Image Integration keeps in a folder.
inline constexpr char kSessionDatabaseFileName[] = "image_integration_sessions.db";

/// The session database Image Integration keeps outside any project, in the application's local data.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString applicationSessionDatabasePath();

/// Where jobs are read from: a session database, and the project it is the database of.
struct IntegrationJobSource
{
    QString databasePath;

    /// The project's name; empty for the database outside any project.
    QString project;

    [[nodiscard]] bool operator==(const IntegrationJobSource&) const = default;
};

/// Where a job stands: what its last run came to, or that it runs now.
enum class IntegrationJobStatus
{
    /// No run of it left a record.
    NotRun,

    /// An open Image Integration runs it.
    Running,

    Completed,
    CompletedWithWarnings,

    /// Its last run went to its end and left at least one planned master unmade.
    Partial,

    Failed,
    Cancelled
};

/// A job: a session of a session database, and what its last run left.
struct IntegrationJob
{
    /// The session's name.
    QString name;

    /// The database the session is in.
    QString databasePath;

    /// The project the database is of; empty for none.
    QString project;

    /// The folder the job's runs write to; empty when its session does not say.
    QString outputDirectory;

    /// The frames of every kind the session holds.
    int frames = 0;

    /// The masters of the lights the last run left: the ones it made and the ones it found still valid.
    int masters = 0;

    /// When the last run ended, in UTC; null for a job no run left a record of.
    QDateTime lastRun;

    /// How long the last run took; negative when it is not known.
    qint64 durationMs = -1;

    IntegrationJobStatus status = IntegrationJobStatus::NotRun;

    /// The stage a failed or a cancelled last run stopped at, by its stage's name (`LightsRegistration`); empty when it
    /// stopped before its first stage or between two, and for every other status.
    QString stage;

    /// While the job runs: what its run says it does, and how far it has come from 0 to 1 when it can tell.
    QString runText;
    std::optional<double> progress;

    /// Whether the session could not be read: then only its name and its database are known.
    bool unreadable = false;

    [[nodiscard]] bool operator==(const IntegrationJob&) const = default;
};

/// A job an open Image Integration runs now, and what its run reports.
struct RunningIntegrationJob
{
    QString databasePath;
    QString name;

    /// What the run says it does - a stage, a step - empty when it says nothing.
    QString text;

    /// How far the run has come, from 0 to 1, when it can tell.
    std::optional<double> progress;

    [[nodiscard]] bool operator==(const RunningIntegrationJob&) const = default;
};

/// Whether two paths name the same session database.
[[nodiscard]] AFTERNIGHTPROCESSES_API bool sameSessionDatabase(const QString& lhs, const QString& rhs);

/// Reads the jobs of the sources, in their order: each database's sessions, each with its frames and its output folder
/// from its document and its last run from the record the run left in that folder. A database that is not there, or
/// is of another format, gives none and is neither made nor changed; a session whose document does not read is listed
/// by its name alone. It reads files and databases: it is called off the GUI thread.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::vector<IntegrationJob>
readIntegrationJobs(const std::vector<IntegrationJobSource>& sources);

/// Puts jobs in the order they are listed in: the running ones first, then the others by their last run, the most
/// recent first, then the ones never run, by name.
AFTERNIGHTPROCESSES_API void orderIntegrationJobs(std::vector<IntegrationJob>& jobs);

/// A stage of the workflow as a person reads it, in the progress window's words: "Registration" for
/// `LightsRegistration`, "Lights calibration" for `LightsCalibration`; a name it does not know as it is.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString integrationStageTitle(const QString& stage);

/// A job's status as its tag says it: "Not run", "Running", "Running · Registration 58 %", "Completed", "Completed with
/// warnings", "Partly completed", "Failed", "Failed at Stacking", "Cancelled", "Cancelled at Registration".
[[nodiscard]] AFTERNIGHTPROCESSES_API QString integrationJobStatusText(const IntegrationJob& job);

/// How long a run took as a job's row says it: "51m 0s", "1h 2m 5s", "14s"; a dash when it is not known.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString integrationJobDurationText(qint64 durationMs);

/// When a job's last run ended as its row says it, in local time to the minute: "2026-07-27 23:54"; a dash for none.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString integrationJobLastRunText(const QDateTime& lastRun);

/// The integration jobs as a table: a row each - name, project, frames, masters, last run, how long it took and its
/// status - the running ones first, then by their last run.
///
/// The jobs are read on the thread pool (`readIntegrationJobs`) and the rows change on the GUI thread when the read
/// lands, so the GUI thread waits for no database and no file; a read that lands after a later one was asked for is
/// dropped. What an open Image Integration runs is told to it (`setRunningJobs`) and shown at once, without reading
/// again. Used on the GUI thread.
class AFTERNIGHTPROCESSES_API IntegrationJobsModel final : public QAbstractTableModel
{
    W_OBJECT(IntegrationJobsModel)

public:
    enum Column
    {
        NameColumn,
        ProjectColumn,
        FramesColumn,
        MastersColumn,
        LastRunColumn,
        TookColumn,
        StatusColumn,
        ColumnCount
    };

    enum Role
    {
        /// The job's `IntegrationJobStatus`, as an int.
        StatusRole = Qt::UserRole + 1,

        /// A running job's progress from 0 to 1; null when it cannot tell, or the job does not run.
        ProgressRole,

        DatabaseRole,
        SessionRole,
        ProjectRole
    };

    explicit IntegrationJobsModel(QObject* parent = nullptr);
    ~IntegrationJobsModel() override;

    IntegrationJobsModel(const IntegrationJobsModel&) = delete;
    IntegrationJobsModel& operator=(const IntegrationJobsModel&) = delete;

    /// Where the jobs are read from; they are read again at once.
    void setSources(std::vector<IntegrationJobSource> sources);
    [[nodiscard]] const std::vector<IntegrationJobSource>& sources() const noexcept;

    /// Reads the jobs again, on the thread pool.
    void refresh();

    /// Whether a read is on its way.
    [[nodiscard]] bool isReading() const noexcept;

    /// The jobs an open Image Integration runs now; each is shown running, and first.
    void setRunningJobs(std::vector<RunningIntegrationJob> running);

    /// The jobs, in their order.
    [[nodiscard]] const std::vector<IntegrationJob>& jobs() const noexcept;

    /// How many jobs run now.
    [[nodiscard]] int runningCount() const;

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                      int role = Qt::DisplayRole) const override;

    /// The jobs were read again, or one began or stopped running.
    void jobsChanged() W_SIGNAL(jobsChanged);

private:
    void publish(std::vector<IntegrationJob> read);
    void applyRunning();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace ImageIntegration
