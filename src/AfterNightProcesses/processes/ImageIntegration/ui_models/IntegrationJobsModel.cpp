// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "IntegrationJobsModel.h"

#include "app/ApplicationStorage.h"
#include "processes/ImageIntegration/persistence/DatabaseSchema.h"
#include "processes/ImageIntegration/persistence/IntegrationDatabase.h"
#include "processes/ImageIntegration/workflow/WorkflowRunStateStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QLocale>
#include <QPointer>
#include <QThreadPool>
#include <verdigris/wobjectimpl.h>

#include <algorithm>
#include <cmath>
#include <utility>

W_OBJECT_IMPL(ImageIntegration::IntegrationJobsModel)

namespace ImageIntegration
{

namespace
{

constexpr char kContext[] = "ImageIntegration::IntegrationJobsModel";

[[nodiscard]] QString translated(const char* text)
{
    return QCoreApplication::translate(kContext, text);
}

/// What a job's status is, from how its last run ended.
[[nodiscard]] IntegrationJobStatus statusOf(const WorkflowRunEnd end)
{
    switch (end)
    {
    case WorkflowRunEnd::Completed:
        return IntegrationJobStatus::Completed;
    case WorkflowRunEnd::CompletedWithWarnings:
        return IntegrationJobStatus::CompletedWithWarnings;
    case WorkflowRunEnd::Partial:
        return IntegrationJobStatus::Partial;
    case WorkflowRunEnd::Failed:
        return IntegrationJobStatus::Failed;
    case WorkflowRunEnd::Cancelled:
        return IntegrationJobStatus::Cancelled;
    }
    return IntegrationJobStatus::NotRun;
}

/// The frames of every kind a session's model holds.
[[nodiscard]] int framesOf(const QJsonObject& model)
{
    int frames = 0;
    const QJsonObject byKind = model.value(QStringLiteral("frames")).toObject();
    for (auto kind = byKind.begin(); kind != byKind.end(); ++kind)
    {
        frames += static_cast<int>(kind.value().toArray().size());
    }
    return frames;
}

/// The jobs of one database: none when it is not there or is of another format, and it is neither made nor changed.
[[nodiscard]] std::vector<IntegrationJob> readDatabase(const IntegrationJobSource& source)
{
    std::vector<IntegrationJob> jobs;
    if (source.databasePath.isEmpty() || !QFileInfo(source.databasePath).isFile())
    {
        return jobs;
    }
    IntegrationDatabase database(source.databasePath);
    if (!database.open() || database.schemaVersion() != persistence::kSchemaVersion)
    {
        return jobs;
    }
    for (const QString& name : database.listSessions())
    {
        IntegrationJob job;
        job.name = name;
        job.databasePath = source.databasePath;
        job.project = source.project;
        const SessionLoadResult loaded = database.loadSessionResult(name);
        if (!loaded.model.has_value())
        {
            job.unreadable = true;
            jobs.push_back(std::move(job));
            continue;
        }
        job.outputDirectory = loaded.model->value(QStringLiteral("outputDirectory")).toString();
        job.frames = framesOf(*loaded.model);
        if (!job.outputDirectory.trimmed().isEmpty())
        {
            if (const std::optional<WorkflowRunRecord> record =
                    WorkflowRunStateStore(job.outputDirectory).loadRunRecord())
            {
                job.lastRun = record->ended;
                job.durationMs = record->durationMs;
                job.status = statusOf(record->end);
                job.masters = record->masters;
                if (job.status == IntegrationJobStatus::Failed || job.status == IntegrationJobStatus::Cancelled ||
                    job.status == IntegrationJobStatus::Partial)
                {
                    job.stage = record->stage;
                }
            }
        }
        jobs.push_back(std::move(job));
    }
    return jobs;
}

[[nodiscard]] bool isRunning(const IntegrationJob& job)
{
    return job.status == IntegrationJobStatus::Running;
}

} // namespace

QString applicationSessionDatabasePath()
{
    return QDir(an::ui::applicationLocalDataPath()).filePath(QString::fromLatin1(kSessionDatabaseFileName));
}

bool sameSessionDatabase(const QString& lhs, const QString& rhs)
{
    if (lhs.isEmpty() || rhs.isEmpty())
    {
        return lhs.isEmpty() && rhs.isEmpty();
    }
    const QString left = QDir::cleanPath(QFileInfo(lhs).absoluteFilePath());
    const QString right = QDir::cleanPath(QFileInfo(rhs).absoluteFilePath());
#ifdef _WIN32
    return left.compare(right, Qt::CaseInsensitive) == 0;
#else
    return left == right;
#endif
}

std::vector<IntegrationJob> readIntegrationJobs(const std::vector<IntegrationJobSource>& sources)
{
    std::vector<IntegrationJob> jobs;
    for (const IntegrationJobSource& source : sources)
    {
        std::vector<IntegrationJob> read = readDatabase(source);
        jobs.insert(jobs.end(), std::make_move_iterator(read.begin()), std::make_move_iterator(read.end()));
    }
    return jobs;
}

void orderIntegrationJobs(std::vector<IntegrationJob>& jobs)
{
    std::stable_sort(jobs.begin(), jobs.end(), [](const IntegrationJob& lhs, const IntegrationJob& rhs) {
        if (isRunning(lhs) != isRunning(rhs))
        {
            return isRunning(lhs);
        }
        if (lhs.lastRun.isValid() != rhs.lastRun.isValid())
        {
            return lhs.lastRun.isValid();
        }
        if (lhs.lastRun.isValid() && lhs.lastRun != rhs.lastRun)
        {
            return lhs.lastRun > rhs.lastRun;
        }
        return lhs.name.compare(rhs.name, Qt::CaseInsensitive) < 0;
    });
}

QString integrationStageTitle(const QString& stage)
{
    struct Title
    {
        const char* stage;
        const char* title;
    };
    // The progress window's words for its stages.
    static constexpr Title kTitles[] = {
        {"BiasStacking", QT_TRANSLATE_NOOP("ImageIntegration::IntegrationJobsModel", "Bias")},
        {"DarkStacking", QT_TRANSLATE_NOOP("ImageIntegration::IntegrationJobsModel", "Dark")},
        {"FlatStacking", QT_TRANSLATE_NOOP("ImageIntegration::IntegrationJobsModel", "Flat")},
        {"LightsCalibration", QT_TRANSLATE_NOOP("ImageIntegration::IntegrationJobsModel", "Lights calibration")},
        {"LightsRegistration", QT_TRANSLATE_NOOP("ImageIntegration::IntegrationJobsModel", "Registration")},
        {"LightsAlignment", QT_TRANSLATE_NOOP("ImageIntegration::IntegrationJobsModel", "Alignment")},
        {"LightsNormalization", QT_TRANSLATE_NOOP("ImageIntegration::IntegrationJobsModel", "Normalization")},
        {"LightsDrizzle", QT_TRANSLATE_NOOP("ImageIntegration::IntegrationJobsModel", "Drizzle")},
        {"LightsStacking", QT_TRANSLATE_NOOP("ImageIntegration::IntegrationJobsModel", "Stacking")},
    };
    for (const Title& entry : kTitles)
    {
        if (stage == QLatin1String(entry.stage))
        {
            return translated(entry.title);
        }
    }
    return stage;
}

QString integrationJobStatusText(const IntegrationJob& job)
{
    switch (job.status)
    {
    case IntegrationJobStatus::NotRun:
        return translated("Not run");
    case IntegrationJobStatus::Running: {
        QStringList parts;
        // A run that says only that it runs adds nothing to the status.
        const QString said = job.runText.trimmed();
        if (!said.isEmpty() && said.compare(QLatin1String("running"), Qt::CaseInsensitive) != 0 &&
            said.compare(translated("Running"), Qt::CaseInsensitive) != 0)
        {
            parts.append(said);
        }
        if (job.progress.has_value())
        {
            const int percent = static_cast<int>(std::lround(std::clamp(*job.progress, 0.0, 1.0) * 100.0));
            parts.append(QStringLiteral("%1 %").arg(percent));
        }
        if (parts.isEmpty())
        {
            return translated("Running");
        }
        return translated("Running") + QStringLiteral(" · ") + parts.join(QLatin1Char(' '));
    }
    case IntegrationJobStatus::Completed:
        return translated("Completed");
    case IntegrationJobStatus::CompletedWithWarnings:
        return translated("Completed with warnings");
    case IntegrationJobStatus::Partial:
        return translated("Partly completed");
    case IntegrationJobStatus::Failed:
        return job.stage.isEmpty() ? translated("Failed")
                                   : translated("Failed at %1").arg(integrationStageTitle(job.stage));
    case IntegrationJobStatus::Cancelled:
        return job.stage.isEmpty() ? translated("Cancelled")
                                   : translated("Cancelled at %1").arg(integrationStageTitle(job.stage));
    }
    return {};
}

QString integrationJobDurationText(const qint64 durationMs)
{
    if (durationMs < 0)
    {
        return QStringLiteral("—");
    }
    const qint64 seconds = (durationMs + 500) / 1000;
    const qint64 hours = seconds / 3600;
    const qint64 minutes = (seconds % 3600) / 60;
    const qint64 rest = seconds % 60;
    if (hours > 0)
    {
        return QStringLiteral("%1h %2m %3s").arg(hours).arg(minutes).arg(rest);
    }
    if (minutes > 0)
    {
        return QStringLiteral("%1m %2s").arg(minutes).arg(rest);
    }
    return QStringLiteral("%1s").arg(rest);
}

QString integrationJobLastRunText(const QDateTime& lastRun)
{
    if (!lastRun.isValid())
    {
        return QStringLiteral("—");
    }
    return lastRun.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

struct IntegrationJobsModel::Impl
{
    std::vector<IntegrationJobSource> sources;

    // The jobs as the last read found them, and as they are shown: the running ones told over them, in order.
    std::vector<IntegrationJob> read;
    std::vector<IntegrationJob> shown;
    std::vector<RunningIntegrationJob> running;

    // The read asked for last; a read that lands under an older number is dropped.
    quint64 generation = 0;
    bool reading = false;
};

IntegrationJobsModel::IntegrationJobsModel(QObject* parent)
    : QAbstractTableModel(parent), m_impl(std::make_unique<Impl>())
{
}

IntegrationJobsModel::~IntegrationJobsModel() = default;

void IntegrationJobsModel::setSources(std::vector<IntegrationJobSource> sources)
{
    m_impl->sources = std::move(sources);
    refresh();
}

const std::vector<IntegrationJobSource>& IntegrationJobsModel::sources() const noexcept
{
    return m_impl->sources;
}

void IntegrationJobsModel::refresh()
{
    const quint64 generation = ++m_impl->generation;
    m_impl->reading = true;
    const QPointer<IntegrationJobsModel> self(this);
    QThreadPool::globalInstance()->start([self, generation, sources = m_impl->sources]() {
        std::vector<IntegrationJob> read = readIntegrationJobs(sources);
        QCoreApplication* const application = QCoreApplication::instance();
        if (application == nullptr)
        {
            return;
        }
        QMetaObject::invokeMethod(
            application,
            [self, generation, read = std::move(read)]() mutable {
                if (self.isNull() || generation != self->m_impl->generation)
                {
                    return;
                }
                self->m_impl->reading = false;
                self->publish(std::move(read));
            },
            Qt::QueuedConnection);
    });
}

bool IntegrationJobsModel::isReading() const noexcept
{
    return m_impl->reading;
}

void IntegrationJobsModel::setRunningJobs(std::vector<RunningIntegrationJob> running)
{
    if (running == m_impl->running)
    {
        return;
    }
    m_impl->running = std::move(running);
    applyRunning();
}

const std::vector<IntegrationJob>& IntegrationJobsModel::jobs() const noexcept
{
    return m_impl->shown;
}

int IntegrationJobsModel::runningCount() const
{
    return static_cast<int>(std::count_if(m_impl->shown.begin(), m_impl->shown.end(), isRunning));
}

int IntegrationJobsModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_impl->shown.size());
}

int IntegrationJobsModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant IntegrationJobsModel::data(const QModelIndex& index, const int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
    {
        return {};
    }
    const IntegrationJob& job = m_impl->shown[static_cast<std::size_t>(index.row())];
    switch (role)
    {
    case Qt::DisplayRole:
    case Qt::ToolTipRole:
        switch (index.column())
        {
        case NameColumn:
            return job.name;
        case ProjectColumn:
            return job.project.isEmpty() ? translated("No project") : job.project;
        case FramesColumn:
            return job.frames;
        case MastersColumn:
            return job.masters;
        case LastRunColumn:
            return integrationJobLastRunText(job.lastRun);
        case TookColumn:
            return integrationJobDurationText(job.durationMs);
        case StatusColumn:
            return integrationJobStatusText(job);
        default:
            return {};
        }
    case Qt::TextAlignmentRole:
        if (index.column() == FramesColumn || index.column() == MastersColumn)
        {
            return QVariant::fromValue(Qt::Alignment(Qt::AlignRight | Qt::AlignVCenter));
        }
        return QVariant::fromValue(Qt::Alignment(Qt::AlignLeft | Qt::AlignVCenter));
    case StatusRole:
        return static_cast<int>(job.status);
    case ProgressRole:
        return job.progress.has_value() ? QVariant(*job.progress) : QVariant();
    case DatabaseRole:
        return job.databasePath;
    case SessionRole:
        return job.name;
    case ProjectRole:
        return job.project;
    default:
        return {};
    }
}

QVariant IntegrationJobsModel::headerData(const int section, const Qt::Orientation orientation, const int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
    {
        return QAbstractTableModel::headerData(section, orientation, role);
    }
    switch (section)
    {
    case NameColumn:
        return translated("Job");
    case ProjectColumn:
        return translated("Project");
    case FramesColumn:
        return translated("Frames");
    case MastersColumn:
        return translated("Masters");
    case LastRunColumn:
        return translated("Last run");
    case TookColumn:
        return translated("Took");
    case StatusColumn:
        return translated("Status");
    default:
        return {};
    }
}

void IntegrationJobsModel::publish(std::vector<IntegrationJob> read)
{
    m_impl->read = std::move(read);
    applyRunning();
}

void IntegrationJobsModel::applyRunning()
{
    std::vector<IntegrationJob> shown = m_impl->read;
    for (IntegrationJob& job : shown)
    {
        for (const RunningIntegrationJob& running : m_impl->running)
        {
            if (job.name == running.name && sameSessionDatabase(job.databasePath, running.databasePath))
            {
                job.status = IntegrationJobStatus::Running;
                job.stage.clear();
                job.runText = running.text;
                job.progress = running.progress;
                break;
            }
        }
    }
    orderIntegrationJobs(shown);
    beginResetModel();
    m_impl->shown = std::move(shown);
    endResetModel();
    emit jobsChanged();
}

} // namespace ImageIntegration
