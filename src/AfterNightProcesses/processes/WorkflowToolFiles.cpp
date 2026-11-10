// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The standalone workflow tools' file list: the probe, the filters, the columns, the ordering and the list's table.

#include "stdafx.hpp"
#include "processes/WorkflowToolFiles.h"

#include "app/AppContextHelpers.h"
#include "controls/TableWidgetUtils.h"
#include "processes/ImageIntegration/core/FileImport.h"
#include "processes/ImageListProcessHelpers.h"
#include "processes/ProcessCommonUtils.h"
#include "session/IEngineSessionProvider.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecMetadataProjection.h>
#include <AfterNightEngine/IO/CodecRequests.h>
#include <AfterNightEngine/IO/FrameTypeCards.h>

#include <AfterNightPlatform/Logging.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMetaObject>
#include <QPointer>
#include <QPushButton>
#include <QThreadPool>
#include <QTimeZone>

#include <algorithm>
#include <atomic>
#include <utility>

namespace an::ui::processes
{

namespace
{

/// How many probed files land on the GUI thread at once.
constexpr std::size_t kImportBatchSize = 50u;

/// The header arrows drawn beside the column a list is ordered by.
const QString kAscendingArrow = QStringLiteral(" \U0001F847");
const QString kDescendingArrow = QStringLiteral(" \U0001F845");

/// Runs a function on the GUI thread through the event loop, never waited on.
void postToGui(std::function<void()> function)
{
    if (QCoreApplication* application = QCoreApplication::instance())
    {
        QMetaObject::invokeMethod(application, std::move(function), Qt::QueuedConnection);
    }
}

/// The model's frame type for the engine's, light when the cards state none.
::FrameType modelFrameType(const an::FrameType type) noexcept
{
    switch (type)
    {
    case an::FrameType::Bias:
        return ::FrameType::Bias;
    case an::FrameType::Dark:
        return ::FrameType::Dark;
    case an::FrameType::Flat:
        return ::FrameType::Flat;
    case an::FrameType::Light:
    case an::FrameType::Unknown:
        break;
    }
    return ::FrameType::Light;
}

/// A time as the tables show it, in UTC, whether a header or the file system stated it, so the texts order as the
/// times do.
QString tableTime(const QDateTime& time)
{
    return time.toTimeZone(QTimeZone(QTimeZone::UTC)).toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz"));
}

QString tableTime(const an::UtcInstant& instant)
{
    return tableTime(
        QDateTime::fromMSecsSinceEpoch(instant.nanosecondsSinceEpoch / 1000000, QTimeZone(QTimeZone::UTC)));
}

QString depthText(const an::SampleType type)
{
    switch (type)
    {
    case an::SampleType::UInt8:
    case an::SampleType::Int8:
        return QStringLiteral("8 bit");
    case an::SampleType::UInt16:
    case an::SampleType::Int16:
        return QStringLiteral("16 bit");
    case an::SampleType::UInt32:
    case an::SampleType::Int32:
        return QStringLiteral("32 bit int");
    case an::SampleType::Float32:
        return QStringLiteral("32 bit float");
    case an::SampleType::Float64:
        return QStringLiteral("64 bit float");
    }
    return QStringLiteral("-");
}

/// The number a sort reads for an optional figure: an absent one before every present one.
template <typename Number> double sortValue(const std::optional<Number>& value)
{
    return value.has_value() ? static_cast<double>(*value) : -std::numeric_limits<double>::infinity();
}

/// The figure a column orders by, or nothing for a column ordered by text.
std::optional<double> numericKey(const WorkflowFile& file, const WorkflowFileColumn column)
{
    switch (column)
    {
    case WorkflowFileColumn::IsoGain: {
        const auto isoOrGain = ImageListProcessHelpers::isoSpeedOrGainOf(file.entry.metadata);
        return isoOrGain.has_value() ? static_cast<double>(isoOrGain->second)
                                     : -std::numeric_limits<double>::infinity();
    }
    case WorkflowFileColumn::Offset:
        return sortValue(ImageListProcessHelpers::cameraOffsetOf(file.entry.metadata));
    case WorkflowFileColumn::Exposure:
        return sortValue(ImageListProcessHelpers::exposureSecondsOf(file.entry.metadata));
    case WorkflowFileColumn::Cfa:
        return file.entry.descriptor.isMosaiced() ? 1.0 : 0.0;
    case WorkflowFileColumn::Depth:
        return static_cast<double>(static_cast<int>(file.entry.descriptor.sampleType()));
    case WorkflowFileColumn::File:
    case WorkflowFileColumn::Date:
    case WorkflowFileColumn::Size:
    case WorkflowFileColumn::Filter:
    case WorkflowFileColumn::Custom:
        break;
    }
    return std::nullopt;
}

} // namespace

bool matchesFrameTypeFilter(const QString& frameTypeText, const QString& filter)
{
    const QString wanted = filter.trimmed().toUpper();
    if (wanted.isEmpty())
    {
        return true;
    }
    const QString type = frameTypeText.trimmed().toUpper();
    const bool dark = type.contains(QStringLiteral("DARK"));
    const bool flat = type.contains(QStringLiteral("FLAT"));
    if (wanted == QStringLiteral("DARKFLAT"))
    {
        return dark && flat;
    }
    if (wanted == QStringLiteral("LIGHT"))
    {
        return type.contains(QStringLiteral("LIGHT"));
    }
    if (wanted == QStringLiteral("DARK"))
    {
        return dark && !flat;
    }
    if (wanted == QStringLiteral("FLAT"))
    {
        return flat && !dark;
    }
    if (wanted == QStringLiteral("BIAS"))
    {
        return type.contains(QStringLiteral("BIAS")) || type.contains(QStringLiteral("OFFSET"));
    }
    LOG_WARNING("Workflow file list: unknown frame type filter '{}' passes every file", wanted.toStdString());
    return true;
}

std::optional<QString> workflowImportSummary(const WorkflowImportReport& report)
{
    if (report.stopped || (report.skipped == 0u && report.failed.empty()))
    {
        return std::nullopt;
    }
    QString text = QStringLiteral("Added %1 file(s); passed over %2 already listed or kept out by the filter")
                       .arg(report.added)
                       .arg(report.skipped);
    if (report.failed.empty())
    {
        return text + QLatin1Char('.');
    }
    QString first = QString::fromStdString(report.failed.front().second.message());
    if (!first.endsWith(QLatin1Char('.')))
    {
        first += QLatin1Char('.');
    }
    return text + QStringLiteral("; %1 could not be read. First error: %2 See the log for per-file details.")
                      .arg(report.failed.size())
                      .arg(first);
}

void sayWorkflowImport(QWidget* window, const WorkflowImportReport& report)
{
    const std::optional<QString> summary = workflowImportSummary(report);
    if (!summary.has_value())
    {
        return;
    }
    if (report.failed.empty())
    {
        process_utils::notifyInfoMessage(window, QStringLiteral("Some files were passed over"), *summary);
        return;
    }
    process_utils::notifyErrorMessage(window, QStringLiteral("Some files were skipped"), *summary);
}

bool passesWorkflowFileFilter(const WorkflowFile& file, const WorkflowFileFilter& filter)
{
    if (!matchesFrameTypeFilter(file.frameTypeText, filter.frameType))
    {
        return false;
    }
    const QString wanted = filter.filterName.trimmed();
    return wanted.isEmpty() || file.figures.filter().trimmed().compare(wanted, Qt::CaseInsensitive) == 0;
}

an::Result<WorkflowFile> probeWorkflowFile(const an::consumer::EngineSession& session, const QString& path,
                                           std::stop_token stop)
{
    const QFileInfo info(path);
    if (path.trimmed().isEmpty() || !info.isFile())
    {
        return an::Status::failure(
            an::ErrorCode::InvalidArgument,
            an::StatusDetail{.message = "'" + path.toStdString() + "' is not a file", .operation = "probe"});
    }
    an::consumer::OperationRequest request = ImageIntegration::importProbeRequest();
    request.stopToken = std::move(stop);
    an::Result<an::consumer::OperationScope> scope = session.admit(request);
    if (!scope.hasValue())
    {
        return scope.status();
    }
    const std::string file = path.toUtf8().toStdString();
    an::Result<an::CodecImageInfo> probed = an::probeImage(
        scope->engine(), scope->execution(), an::CodecReadRequest{.path = file, .validatePayload = false});
    if (!probed.hasValue())
    {
        return probed.status();
    }
    const std::optional<an::ImageDescriptor> descriptor = probed->descriptor.toImageDescriptor();
    if (!descriptor.has_value())
    {
        return an::Status::failure(
            an::ErrorCode::Unsupported,
            an::StatusDetail{.message = "'" + file + "' holds samples outside the eight this engine processes",
                             .operation = "probe"});
    }

    const an::FrameTypeReading reading = an::readFrameTypeCards(probed->metadata.codec());
    QString typeText = QString::fromStdString(reading.spelling).trimmed().toUpper();
    if (typeText.isEmpty())
    {
        typeText = QStringLiteral("LIGHT");
    }

    const an::AcquisitionProjection projected = an::projectAcquisition(probed->metadata.codec());
    std::optional<an::UtcInstant> created = projected.creationTime;
    if (!created.has_value())
    {
        created = probed->metadata.acquisition().exposureStart();
    }

    const an::ImageExtent& extent = descriptor->extent();
    FrameMetadata figures = ImageIntegration::frameMetadataFromProbe(
        probed->metadata, modelFrameType(reading.type), static_cast<int>(extent.width()),
        static_cast<int>(extent.height()), info, ImageIntegration::sampleLayoutOf(probed->descriptor));
    return WorkflowFile{.entry = ImageListProcessHelpers::ImageListEntry{.path = path,
                                                                         .descriptor = *descriptor,
                                                                         .metadata = std::move(probed->metadata)},
                        .figures = std::move(figures),
                        .frameTypeText = std::move(typeText),
                        .displayDate = created.has_value() ? tableTime(*created) : tableTime(info.lastModified())};
}

QString workflowFileColumnText(const WorkflowFile& file, const WorkflowFileColumn column)
{
    switch (column)
    {
    case WorkflowFileColumn::File:
        return QFileInfo(file.path()).fileName();
    case WorkflowFileColumn::Date:
        return file.displayDate;
    case WorkflowFileColumn::Size: {
        const an::ImageExtent& extent = file.entry.descriptor.extent();
        return QStringLiteral("%1x%2").arg(extent.width()).arg(extent.height());
    }
    case WorkflowFileColumn::Filter: {
        const QString filter = file.figures.filter().trimmed();
        return filter.isEmpty() ? QStringLiteral("-") : filter;
    }
    case WorkflowFileColumn::IsoGain: {
        const auto isoOrGain = ImageListProcessHelpers::isoSpeedOrGainOf(file.entry.metadata);
        return isoOrGain.has_value() ? QString::number(isoOrGain->second) : QStringLiteral("-");
    }
    case WorkflowFileColumn::Offset: {
        const auto offset = ImageListProcessHelpers::cameraOffsetOf(file.entry.metadata);
        return offset.has_value() ? QString::number(*offset) : QStringLiteral("-");
    }
    case WorkflowFileColumn::Exposure: {
        const auto exposure = ImageListProcessHelpers::exposureSecondsOf(file.entry.metadata);
        return exposure.has_value() ? QString::number(*exposure) : QStringLiteral("-");
    }
    case WorkflowFileColumn::Cfa:
        return file.entry.descriptor.isMosaiced() ? QStringLiteral("Yes") : QStringLiteral("No");
    case WorkflowFileColumn::Depth:
        return depthText(file.entry.descriptor.sampleType());
    case WorkflowFileColumn::Custom:
        break;
    }
    return {};
}

bool sortWorkflowFiles(std::vector<WorkflowFile>& files, const WorkflowFileColumn column, const bool descending)
{
    if (column == WorkflowFileColumn::Size || column == WorkflowFileColumn::Custom)
    {
        return false;
    }
    const auto ordered = [descending](const auto& left, const auto& right) {
        return descending ? right < left : left < right;
    };
    std::stable_sort(files.begin(), files.end(), [&](const WorkflowFile& left, const WorkflowFile& right) {
        if (column == WorkflowFileColumn::Date)
        {
            return ordered(left.displayDate, right.displayDate);
        }
        if (const std::optional<double> leftKey = numericKey(left, column); leftKey.has_value())
        {
            const double rightKey = *numericKey(right, column);
            if (*leftKey != rightKey)
            {
                return ordered(*leftKey, rightKey);
            }
        }
        else
        {
            const QString leftText = workflowFileColumnText(left, column);
            const QString rightText = workflowFileColumnText(right, column);
            if (leftText != rightText)
            {
                return ordered(leftText, rightText);
            }
        }
        return ordered(left.displayDate, right.displayDate);
    });
    return true;
}

/// What the list's imports share with the workers that run them.
struct WorkflowFileList::State
{
    /// Stopped when the list goes or is cleared, so the imports in flight stop; a clear replaces it for the imports
    /// asked for after it.
    std::stop_source stop;

    /// Moved by every clear: an import asked for before it lands none of its files. GUI thread only.
    std::uint64_t generation = 0u;
};

WorkflowFileList::WorkflowFileList(QWidget* owner, WorkflowFileListWidgets widgets, WorkflowFileListOptions options)
    : m_owner(owner), m_widgets(widgets), m_options(std::move(options)), m_state(std::make_shared<State>()),
      m_sortColumn(m_options.initialSortColumn), m_sortDescending(m_options.initialSortDescending)
{
    if (m_options.stop.stop_possible())
    {
        // The window's stop stops the imports in flight as the list's own does, through whichever source the list
        // holds then - a clear replaces it.
        m_windowStop.emplace(m_options.stop, [state = std::weak_ptr<State>(m_state)]() {
            if (const std::shared_ptr<State> alive = state.lock())
            {
                alive->stop.request_stop();
            }
        });
    }
    std::vector<QString> headers;
    headers.reserve(m_options.columns.size());
    for (const WorkflowFileColumnSpec& spec : m_options.columns)
    {
        headers.push_back(spec.title);
    }
    if (m_widgets.table != nullptr)
    {
        m_widgets.table->setAlternatingRowColors(true);
        m_widgets.table->setColumnCount(static_cast<int>(headers.size()));
        m_widgets.table->setHeaderLabels(headers);
        m_widgets.table->setupSelectionColors();
        m_widgets.table->setListener(this);
    }
    updateHeaders();
    updateLabels();
}

WorkflowFileList::~WorkflowFileList()
{
    m_state->stop.request_stop();
    if (m_widgets.table != nullptr)
    {
        m_widgets.table->setListener(nullptr);
    }
}

void WorkflowFileList::addFiles(const QStringList& paths, const WorkflowFileFilter& filter)
{
    QStringList fresh;
    std::unordered_set<QString> batch;
    std::uint64_t skipped = 0u;
    for (const QString& path : paths)
    {
        if (m_paths.count(path) == 0u && batch.insert(path).second)
        {
            fresh.push_back(path);
        }
        else
        {
            ++skipped;
        }
    }
    const an::ui::IEngineSessionProvider* provider = process_utils::currentEngineSessionProvider();
    if (fresh.isEmpty() || provider == nullptr)
    {
        if (onImportFinished)
        {
            WorkflowImportReport report{.skipped = skipped};
            if (!fresh.isEmpty())
            {
                report.failed.emplace_back(
                    fresh.front(), an::Status::failure(an::ErrorCode::NotInitialized,
                                                       an::StatusDetail{.message = "no engine session is lent"}));
            }
            onImportFinished(report);
        }
        return;
    }
    const an::consumer::EngineSession* session = &provider->engineSession();

    ++m_importsInFlight;
    // The list is reached through the window that owns it; a window that is gone receives nothing.
    QPointer<QWidget> owner(m_owner);
    std::weak_ptr<State> alive = m_state;
    WorkflowFileList* self = this;
    const std::stop_token stop = m_state->stop.get_token();
    const std::uint64_t generation = m_state->generation;
    QThreadPool::globalInstance()->start([session, fresh, filter, owner, alive, self, stop, skipped, generation]() {
        const auto deliver = [owner, alive, self](std::function<void(WorkflowFileList&)> landing) {
            postToGui([owner, alive, self, landing = std::move(landing)]() {
                if (owner && !alive.expired())
                {
                    landing(*self);
                }
            });
        };
        // Files an import probed before the list was cleared are dropped where they land.
        const auto land = [generation](WorkflowFileList& list, std::vector<WorkflowFile>&& files) -> std::uint64_t {
            return list.m_state->generation == generation ? list.importLanded(std::move(files)) : 0u;
        };
        auto report = std::make_shared<WorkflowImportReport>();
        report->skipped = skipped;
        std::vector<WorkflowFile> pending;
        for (const QString& path : fresh)
        {
            if (stop.stop_requested())
            {
                report->stopped = true;
                break;
            }
            an::Result<WorkflowFile> probed = probeWorkflowFile(*session, path, stop);
            if (!probed.hasValue())
            {
                if (probed.status().code() == an::ErrorCode::Cancelled)
                {
                    report->stopped = true;
                    break;
                }
                LOG_WARNING("Workflow file list: '{}' could not be read: {}", path.toStdString(),
                            probed.status().message());
                report->failed.emplace_back(path, probed.status());
                continue;
            }
            if (!passesWorkflowFileFilter(*probed, filter))
            {
                ++report->skipped;
                continue;
            }
            pending.push_back(std::move(*probed));
            if (pending.size() == kImportBatchSize)
            {
                auto shipped = std::make_shared<std::vector<WorkflowFile>>(std::move(pending));
                pending = {};
                // Only the GUI thread counts what landed, in a member the worker never touches.
                deliver([shipped, report, land](WorkflowFileList& list) {
                    report->added += land(list, std::move(*shipped));
                });
            }
        }
        auto shipped = std::make_shared<std::vector<WorkflowFile>>(std::move(pending));
        deliver([shipped, report, land, generation](WorkflowFileList& list) {
            report->added += land(list, std::move(*shipped));
            if (list.m_state->generation != generation)
            {
                report->stopped = true;
            }
            if (list.m_importsInFlight > 0u)
            {
                --list.m_importsInFlight;
            }
            if (list.onImportFinished)
            {
                list.onImportFinished(*report);
            }
        });
    });
}

void WorkflowFileList::restoreFiles(const QStringList& paths)
{
    // The order saved is what the person saw, sorted or made by hand.
    m_sortColumn.reset();
    updateHeaders();
    addFiles(paths);
}

void WorkflowFileList::addFolder(const QString& directory, bool recursive, const WorkflowFileFilter& filter)
{
    // A large tree is not walked on the GUI thread.
    ++m_importsInFlight;
    QPointer<QWidget> owner(m_owner);
    std::weak_ptr<State> alive = m_state;
    WorkflowFileList* self = this;
    const std::uint64_t generation = m_state->generation;
    QThreadPool::globalInstance()->start([directory, recursive, filter, owner, alive, self, generation]() {
        LOG_DEBUG("Workflow file list: listing folder '{}'", directory.toStdString());
        QStringList found = ImageListProcessHelpers::scanFolderForSupportedImages(directory, recursive);
        postToGui([owner, alive, self, filter, generation, found = std::move(found)]() {
            if (!owner || alive.expired())
            {
                return;
            }
            if (self->m_importsInFlight > 0u)
            {
                --self->m_importsInFlight;
            }
            // What a folder listed before the list was cleared found is dropped, as an import's files are.
            if (self->m_state->generation != generation)
            {
                if (self->onImportFinished)
                {
                    self->onImportFinished(WorkflowImportReport{.stopped = true});
                }
                return;
            }
            self->addFiles(found, filter);
        });
    });
}

std::uint64_t WorkflowFileList::importLanded(std::vector<WorkflowFile>&& files)
{
    return addProbed(std::move(files));
}

std::uint64_t WorkflowFileList::addProbed(std::vector<WorkflowFile>&& files)
{
    std::uint64_t added = 0u;
    const QString selected = selectedFile() != nullptr ? selectedFile()->path() : QString();
    for (WorkflowFile& file : files)
    {
        if (!m_paths.insert(file.path()).second)
        {
            continue;
        }
        m_files.push_back(std::move(file));
        ++added;
    }
    if (added == 0u)
    {
        return 0u;
    }
    if (m_sortColumn.has_value() && *m_sortColumn < m_options.columns.size())
    {
        (void)sortWorkflowFiles(m_files, m_options.columns[*m_sortColumn].column, m_sortDescending);
    }
    rebuildTable();
    const int keep = selected.isEmpty() ? 0 : rowOf(selected);
    selectRow(keep < 0 ? 0 : keep);
    changed();
    return added;
}

void WorkflowFileList::remove(const std::vector<QString>& paths)
{
    if (paths.empty() || isBusy())
    {
        return;
    }
    const QString selected = selectedFile() != nullptr ? selectedFile()->path() : QString();
    const std::unordered_set<QString> gone(paths.begin(), paths.end());
    std::erase_if(m_files, [&gone](const WorkflowFile& file) { return gone.count(file.path()) != 0u; });
    for (const QString& path : paths)
    {
        m_paths.erase(path);
    }
    rebuildTable();
    const int keep = rowOf(selected);
    m_selectedRow = -1;
    selectRow(keep >= 0 ? keep : (m_files.empty() ? -1 : 0));
    changed();
}

void WorkflowFileList::clear()
{
    if (isBusy())
    {
        return;
    }
    // The imports in flight stop, and what they probed before they stopped lands as nothing.
    m_state->stop.request_stop();
    m_state->stop = std::stop_source();
    ++m_state->generation;
    m_files.clear();
    m_paths.clear();
    m_sortColumn = m_options.initialSortColumn;
    m_sortDescending = m_options.initialSortDescending;
    rebuildTable();
    m_selectedRow = -1;
    selectRow(-1);
    updateHeaders();
    changed();
}

const std::vector<WorkflowFile>& WorkflowFileList::files() const noexcept
{
    return m_files;
}

std::vector<ImageListProcessHelpers::ImageListEntry> WorkflowFileList::entries() const
{
    std::vector<ImageListProcessHelpers::ImageListEntry> out;
    out.reserve(m_files.size());
    for (const WorkflowFile& file : m_files)
    {
        out.push_back(file.entry);
    }
    return out;
}

QStringList WorkflowFileList::paths() const
{
    QStringList out;
    out.reserve(static_cast<qsizetype>(m_files.size()));
    for (const WorkflowFile& file : m_files)
    {
        out.push_back(file.path());
    }
    return out;
}

int WorkflowFileList::selectedRow() const noexcept
{
    return m_selectedRow;
}

const WorkflowFile* WorkflowFileList::selectedFile() const noexcept
{
    if (m_selectedRow < 0 || m_selectedRow >= static_cast<int>(m_files.size()))
    {
        return nullptr;
    }
    return &m_files[static_cast<std::size_t>(m_selectedRow)];
}

void WorkflowFileList::selectRow(const int row)
{
    const int bounded = (row >= 0 && row < static_cast<int>(m_files.size())) ? row : -1;
    if (m_widgets.table != nullptr && bounded >= 0)
    {
        m_ignoreSelection = true;
        m_widgets.table->selectRow(bounded);
        m_ignoreSelection = false;
    }
    currentRowMoved(bounded);
}

void WorkflowFileList::currentRowMoved(const int bounded)
{
    m_selectedRow = bounded;
    if (m_widgets.moveUp != nullptr && m_widgets.moveDown != nullptr)
    {
        process_utils::updateNavigationButtons(m_widgets.moveUp, m_widgets.moveDown, bounded, m_files.size());
    }
    updateLabels();
    if (onSelected)
    {
        onSelected(bounded);
    }
}

void WorkflowFileList::moveSelected(const int offset)
{
    const int from = m_selectedRow;
    const int to = from + offset;
    if (isBusy() || from < 0 || to < 0 || to >= static_cast<int>(m_files.size()))
    {
        return;
    }
    std::swap(m_files[static_cast<std::size_t>(from)], m_files[static_cast<std::size_t>(to)]);
    // A hand-made order is no column's.
    m_sortColumn.reset();
    updateHeaders();
    writeRow(from);
    writeRow(to);
    selectRow(to);
    changed();
}

bool WorkflowFileList::idle() const noexcept
{
    return m_importsInFlight == 0u;
}

void WorkflowFileList::refreshRows()
{
    for (int row = 0; row < static_cast<int>(m_files.size()); ++row)
    {
        writeRow(row);
    }
}

void WorkflowFileList::refreshRow(const int row)
{
    if (row >= 0 && row < static_cast<int>(m_files.size()))
    {
        writeRow(row);
    }
}

int WorkflowFileList::rowOf(const QString& path) const
{
    for (std::size_t index = 0; index < m_files.size(); ++index)
    {
        if (m_files[index].path() == path)
        {
            return static_cast<int>(index);
        }
    }
    return -1;
}

void WorkflowFileList::onRowSelected(TableWidget* sender, const int row)
{
    Q_UNUSED(sender);
    if (m_ignoreSelection)
    {
        return;
    }
    if (row == m_selectedRow)
    {
        if (onSelectionChanged)
        {
            onSelectionChanged();
        }
        return;
    }
    // The table already shows what the person selected, one row or several; selecting the current row in it again
    // would collapse several to one.
    currentRowMoved((row >= 0 && row < static_cast<int>(m_files.size())) ? row : -1);
}

void WorkflowFileList::onColumnClicked(TableWidget* sender, const int column)
{
    Q_UNUSED(sender);
    if (isBusy() || column < 0 || column >= static_cast<int>(m_options.columns.size()))
    {
        return;
    }
    const auto index = static_cast<std::size_t>(column);
    const bool descending = m_sortColumn == index ? !m_sortDescending : true;
    const QString selected = selectedFile() != nullptr ? selectedFile()->path() : QString();
    if (!sortWorkflowFiles(m_files, m_options.columns[index].column, descending))
    {
        return;
    }
    m_sortColumn = index;
    m_sortDescending = descending;
    updateHeaders();
    rebuildTable();
    // The selection follows its file, not its row.
    const int keep = rowOf(selected);
    m_selectedRow = -1;
    selectRow(keep >= 0 ? keep : (m_files.empty() ? -1 : 0));
    changed();
}

void WorkflowFileList::onContextMenuRequested(TableWidget* sender, const int row, const int col,
                                              const QPoint& globalPos)
{
    Q_UNUSED(sender);
    Q_UNUSED(col);
    if (isBusy() || row < 0 || row >= static_cast<int>(m_files.size()))
    {
        return;
    }
    if (row != m_selectedRow)
    {
        selectRow(row);
    }
    const QString path = m_files[static_cast<std::size_t>(row)].path();

    QMenu menu(m_owner);
    if (m_options.extendMenu)
    {
        m_options.extendMenu(menu, row);
        if (!menu.isEmpty())
        {
            menu.addSeparator();
        }
    }
    QAction* openInEditor = m_options.offerOpenInEditor ? menu.addAction(QStringLiteral("Open In Editor")) : nullptr;
    QAction* moveUp = m_options.offerMoves ? menu.addAction(QStringLiteral("Move Up")) : nullptr;
    QAction* moveDown = m_options.offerMoves ? menu.addAction(QStringLiteral("Move Down")) : nullptr;
    if (moveUp != nullptr)
    {
        moveUp->setEnabled(row > 0);
        moveDown->setEnabled(row + 1 < static_cast<int>(m_files.size()));
    }
    QAction* removeAction = menu.addAction(QStringLiteral("Remove"));
    QAction* deleteAction = menu.addAction(QStringLiteral("Delete From Disk"));

    QAction* chosen = menu.exec(globalPos);
    if (chosen == nullptr)
    {
        return;
    }
    if (chosen == openInEditor)
    {
        process_utils::openRecentSingleFile(path);
    }
    else if (chosen == moveUp)
    {
        moveSelected(-1);
    }
    else if (chosen == moveDown)
    {
        moveSelected(1);
    }
    else if (chosen == removeAction)
    {
        remove(process_utils::selectedFilesFromTable(m_widgets.table));
    }
    else if (chosen == deleteAction)
    {
        int answer = 0;
        process_utils::messageBoxYesNo(
            m_owner, QStringLiteral("Delete selected file"),
            QStringLiteral("This will delete the selected file from the disk permanently. Are you sure you want to "
                           "proceed?"),
            true, &answer);
        if (answer != QMessageBox::Yes)
        {
            return;
        }
        remove({path});
        if (!QFile::remove(path))
        {
            process_utils::notifyErrorMessageAsync(m_owner, QStringLiteral("Delete selected file"),
                                                   QStringLiteral("Could not delete '%1' from the disk.").arg(path));
        }
    }
}

void WorkflowFileList::rebuildTable()
{
    if (m_widgets.table == nullptr)
    {
        return;
    }
    m_ignoreSelection = true;
    m_widgets.table->clearContents();
    m_widgets.table->setRowCount(static_cast<int>(m_files.size()));
    for (int row = 0; row < static_cast<int>(m_files.size()); ++row)
    {
        writeRow(row);
    }
    process_utils::applyStandardFileTableColumnWidths(m_widgets.table, 0);
    m_ignoreSelection = false;
}

void WorkflowFileList::writeRow(const int row)
{
    if (m_widgets.table == nullptr)
    {
        return;
    }
    const WorkflowFile& file = m_files[static_cast<std::size_t>(row)];
    for (std::size_t column = 0; column < m_options.columns.size(); ++column)
    {
        const WorkflowFileColumn kind = m_options.columns[column].column;
        const QString text = kind == WorkflowFileColumn::Custom
                                 ? (m_options.customText ? m_options.customText(row, file, column) : QString())
                                 : workflowFileColumnText(file, kind);
        ImageListProcessHelpers::setTableItemText(m_widgets.table, row, static_cast<int>(column), text);
        if (kind == WorkflowFileColumn::File)
        {
            if (QTableWidgetItem* item = m_widgets.table->item(row, static_cast<int>(column)))
            {
                item->setData(Qt::UserRole, file.path());
                item->setToolTip(file.path());
            }
        }
    }
}

void WorkflowFileList::updateHeaders()
{
    if (m_widgets.table == nullptr)
    {
        return;
    }
    for (std::size_t column = 0; column < m_options.columns.size(); ++column)
    {
        QString title = m_options.columns[column].title;
        if (m_sortColumn == column)
        {
            title += m_sortDescending ? kDescendingArrow : kAscendingArrow;
        }
        m_widgets.table->setColumnTitle(static_cast<int>(column), title, false);
    }
    m_widgets.table->updateHeaders();
}

void WorkflowFileList::updateLabels()
{
    const auto rows = static_cast<int>(m_files.size());
    if (m_widgets.currentLabel != nullptr)
    {
        m_widgets.currentLabel->setText(QString::number(m_selectedRow >= 0 ? m_selectedRow + 1 : 0));
    }
    if (m_widgets.totalLabel != nullptr)
    {
        m_widgets.totalLabel->setText(QString::number(rows));
    }
    if (m_widgets.removeSelected != nullptr)
    {
        m_widgets.removeSelected->setEnabled(rows > 0);
    }
    if (m_widgets.clearAll != nullptr)
    {
        m_widgets.clearAll->setEnabled(rows > 0);
    }
}

void WorkflowFileList::changed()
{
    updateLabels();
    if (onChanged)
    {
        onChanged();
    }
}

bool WorkflowFileList::isBusy() const
{
    return m_options.busy && m_options.busy();
}

} // namespace an::ui::processes
