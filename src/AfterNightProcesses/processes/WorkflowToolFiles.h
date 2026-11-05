// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The file list the standalone workflow tools share - Image Calibration, Image Stacking, Image Registration and the
/// Image List Analyzer: each file's header probed through the engine on the thread pool, the figures the integration
/// workflow reads taken from it (`frameMetadataFromProbe`), the frame-type and filter filters an Add Folder asks for,
/// the columns each window shows, an ordering that reads nothing from disk, and the list's own moves, removals and
/// context menu.
///
/// **Nothing waits.** An import runs on the thread pool in operations the session admits, one thread each as the
/// integration's import asks for (`importProbeRequest`), and lands through the event loop in batches; a window that is
/// gone receives nothing. A question - deleting a file from disk - goes through the application's dialog service.
///
/// **The table and the list are one.** Row `i` of the table is entry `i` of the list, in every ordering; a selection
/// survives a reordering by its file, not by its row.
#pragma once

#include "AfterNightProcessesExport.h"

#include "controls/ImageListTableHelpers.h"
#include "processes/ImageIntegration/model/FrameMetadata.h"
#include "widgets/TableWidget.h"

#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>

#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <unordered_set>
#include <vector>

class QLabel;
class QMenu;
class QPushButton;
class QWidget;
class WaitingSpinnerWidget;

namespace an::consumer
{
class EngineSession;
} // namespace an::consumer

namespace an::ui::processes
{

/// One file of a workflow tool's list.
struct WorkflowFile
{
    /// The file, its description and its metadata, as the engine probed them.
    ImageListProcessHelpers::ImageListEntry entry;

    /// The figures the integration workflow reads from the same probe: exposure, sensor temperature, ISO or gain,
    /// offset, filter, target and capture time.
    FrameMetadata figures;

    /// The frame type the file's cards state, upper case as written, or "LIGHT" when no card states one.
    QString frameTypeText;

    /// The Date column's text: the creation time the cards state, else the exposure start, else the file's
    /// modification time; every one in UTC, so the texts order as the times do; read once, when the file is probed.
    QString displayDate;

    /// The file's path.
    [[nodiscard]] const QString& path() const noexcept
    {
        return entry.path;
    }
};

/// What an Add Folder narrows a folder's files to: a frame type and a filter name, either empty for every file.
struct WorkflowFileFilter
{
    /// "LIGHT", "DARK", "FLAT", "BIAS" or "DARKFLAT", or empty.
    QString frameType;

    /// The filter name, compared without regard to case, or empty.
    QString filterName;
};

/// Whether a file's frame type passes a type filter: LIGHT a type naming LIGHT; DARK one naming DARK and not FLAT;
/// FLAT one naming FLAT and not DARK; DARKFLAT one naming both; BIAS one naming BIAS or OFFSET; an empty or unknown
/// filter every type. The type is read as the file's cards spell it.
///
/// @param frameTypeText the type, as `WorkflowFile::frameTypeText` holds it.
/// @param filter the type filter.
/// @return whether the file passes.
[[nodiscard]] AFTERNIGHTPROCESSES_API bool matchesFrameTypeFilter(const QString& frameTypeText, const QString& filter);

/// Whether a file passes a filter: its type by `matchesFrameTypeFilter`, its filter name equal to the one asked for.
///
/// @param file the file.
/// @param filter the filter.
/// @return whether the file passes.
[[nodiscard]] AFTERNIGHTPROCESSES_API bool passesWorkflowFileFilter(const WorkflowFile& file,
                                                                    const WorkflowFileFilter& filter);

/// Probes one file: its header read through the session's codecs - no sample decoded - in a one-thread batch
/// operation that waits for its admission, and its figures read as the integration's import reads them.
///
/// @param session the process's session.
/// @param path the file.
/// @param stop stops the probe.
/// @return the file; `ErrorCode::InvalidArgument` for a path that names no file; or what the engine refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<WorkflowFile>
probeWorkflowFile(const an::consumer::EngineSession& session, const QString& path, std::stop_token stop);

/// A column the tools show a file's facts in.
enum class WorkflowFileColumn : std::uint8_t
{
    /// The file's name; the row's full path rides in the item's user role and tooltip.
    File,
    /// `WorkflowFile::displayDate`.
    Date,
    /// "width x height".
    Size,
    /// The filter, or "-".
    Filter,
    /// The ISO speed or the camera's gain setting, or "-".
    IsoGain,
    /// The camera's offset, or "-".
    Offset,
    /// The exposure in seconds, or "-".
    Exposure,
    /// "Yes" for a filter-array mosaic, "No" otherwise.
    Cfa,
    /// "8 bit", "16 bit", "32 bit int", "32 bit float" or "64 bit float".
    Depth,
    /// A column the window fills itself, through `WorkflowFileListOptions::customText`.
    Custom
};

/// The text a standard column shows for a file.
///
/// @param file the file.
/// @param column the column; `Custom` shows nothing.
/// @return the text.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString workflowFileColumnText(const WorkflowFile& file,
                                                                     WorkflowFileColumn column);

/// Orders files by a column, stably, the date breaking ties for every column but the date's own: the file name, the
/// date, the filter, the ISO or gain, the offset, the exposure, whether it is a mosaic, its representation. Size and a
/// custom column order nothing.
///
/// @param files the files.
/// @param column the column.
/// @param descending whether the largest comes first.
/// @return whether the column orders the files.
AFTERNIGHTPROCESSES_API bool sortWorkflowFiles(std::vector<WorkflowFile>& files, WorkflowFileColumn column,
                                               bool descending);

/// One column of a window's table.
struct WorkflowFileColumnSpec
{
    /// The header's text.
    QString title;

    /// What the column shows.
    WorkflowFileColumn column = WorkflowFileColumn::Custom;
};

/// The widgets a list drives; every pointer but the table may be null for a window that has no such widget.
struct WorkflowFileListWidgets
{
    TableWidget* table = nullptr;
    QPushButton* removeSelected = nullptr;
    QPushButton* moveUp = nullptr;
    QPushButton* moveDown = nullptr;
    QPushButton* clearAll = nullptr;
    QLabel* currentLabel = nullptr;
    QLabel* totalLabel = nullptr;
};

/// What a list does beyond the standard columns and actions.
struct WorkflowFileListOptions
{
    /// The table's columns, in order.
    std::vector<WorkflowFileColumnSpec> columns;

    /// The column a fresh list is ordered by, and whether descending; none keeps the order the files were added in.
    std::optional<std::size_t> initialSortColumn = 1u;
    bool initialSortDescending = false;

    /// Whether the context menu offers Move Up and Move Down.
    bool offerMoves = true;

    /// Whether the context menu offers Open In Editor.
    bool offerOpenInEditor = false;

    /// The text of a custom column for a row.
    std::function<QString(int row, const WorkflowFile& file, std::size_t column)> customText;

    /// Adds a window's own actions to the context menu, above the list's; given the row clicked.
    std::function<void(QMenu& menu, int row)> extendMenu;

    /// Whether the window is busy: a busy window's list is neither reordered nor edited.
    std::function<bool()> busy;

    /// The window's stop: an import in flight stops when it is requested - at the application's exit - as it does
    /// when the list goes or is cleared.
    std::stop_token stop;
};

/// What an import made of the files it was given.
struct WorkflowImportReport
{
    /// The files added.
    std::uint64_t added = 0u;

    /// The files already listed or passed over by the filter.
    std::uint64_t skipped = 0u;

    /// The files that could not be read, and why.
    std::vector<std::pair<QString, an::Status>> failed;

    /// Whether the import was stopped before it read every file.
    bool stopped = false;
};

/// What an import says to the person when it did not add every file it was given: how many it added, passed over -
/// already listed or kept out by the filter - and could not read, with the first reason.
///
/// @param report what the import made of its files.
/// @return the text, or nothing for an import that added every file or was stopped.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::optional<QString> workflowImportSummary(const WorkflowImportReport& report);

/// Says an import's summary in a window: as an error, "Some files were skipped", when a file could not be read, and as
/// a note, "Some files were passed over", when files were only passed over. Says nothing when there is no summary.
///
/// @param window the window the import added to.
/// @param report what the import made of its files.
AFTERNIGHTPROCESSES_API void sayWorkflowImport(QWidget* window, const WorkflowImportReport& report);

/// A workflow tool's file list over its table.
///
/// GUI thread only. A list made by a window lives as long as the window; work it started lands on a list that is
/// gone as nothing.
class AFTERNIGHTPROCESSES_API WorkflowFileList final : public TableWidgetListener
{
public:
    /// Drives a window's widgets.
    ///
    /// @param owner the window, parent of every question the list asks.
    /// @param widgets the table and its buttons.
    /// @param options the columns and the window's own behaviour.
    WorkflowFileList(QWidget* owner, WorkflowFileListWidgets widgets, WorkflowFileListOptions options);

    /// Stops the imports in flight; what they would land is dropped.
    ~WorkflowFileList();

    WorkflowFileList(const WorkflowFileList&) = delete;
    WorkflowFileList& operator=(const WorkflowFileList&) = delete;

    /// Probes files on the thread pool and adds those that pass the filter and are not listed yet.
    ///
    /// @param paths the files.
    /// @param filter what passes.
    void addFiles(const QStringList& paths, const WorkflowFileFilter& filter = {});

    /// Adds a saved list's files as `addFiles` does, in the order given: the list is no column's order until a header
    /// asks for one, as after a move by hand.
    ///
    /// @param paths the files, in the order the list showed them when it was saved.
    void restoreFiles(const QStringList& paths);

    /// Lists a folder's supported images on the thread pool, then adds them as `addFiles` does. The list is not idle
    /// while the folder is listed, and a clear meanwhile drops what the listing found: its import reports its end,
    /// stopped, having added nothing.
    ///
    /// @param directory the folder.
    /// @param recursive whether the folders inside it are listed too.
    /// @param filter what passes.
    void addFolder(const QString& directory, bool recursive, const WorkflowFileFilter& filter = {});

    /// Adds files probed elsewhere, at once, ordering the list by its column.
    ///
    /// @param files the files; those already listed are passed over.
    /// @return how many were added.
    std::uint64_t addProbed(std::vector<WorkflowFile>&& files);

    /// Removes files from the list.
    ///
    /// @param paths the files.
    void remove(const std::vector<QString>& paths);

    /// Removes every file and stops the imports in flight: what they would land is dropped, and each still reports
    /// its end, stopped. An import asked for afterwards lands as usual.
    void clear();

    /// The files, in the table's order.
    [[nodiscard]] const std::vector<WorkflowFile>& files() const noexcept;

    /// The files as the shared list helpers take them.
    [[nodiscard]] std::vector<ImageListProcessHelpers::ImageListEntry> entries() const;

    /// The paths, in the table's order.
    [[nodiscard]] QStringList paths() const;

    /// The row selected, or -1.
    [[nodiscard]] int selectedRow() const noexcept;

    /// The file selected, or null.
    [[nodiscard]] const WorkflowFile* selectedFile() const noexcept;

    /// Selects a row and tells the window.
    ///
    /// @param row the row.
    void selectRow(int row);

    /// Moves the selected row up or down by one.
    ///
    /// @param offset -1 or 1.
    void moveSelected(int offset);

    /// Whether no import is on its way.
    [[nodiscard]] bool idle() const noexcept;

    /// Writes every row's text again, after a custom column's facts changed.
    void refreshRows();

    /// Writes one row's text again.
    ///
    /// @param row the row.
    void refreshRow(int row);

    /// The row a file is in, or -1.
    ///
    /// @param path the file.
    [[nodiscard]] int rowOf(const QString& path) const;

    /// Tells the window a row was selected: the row, or -1 when the list is empty.
    std::function<void(int row)> onSelected;

    /// Tells the window the rows selected in the table changed while the current row stayed where it was - a
    /// selection extended or narrowed around it - so what reads the selection can follow it.
    std::function<void()> onSelectionChanged;

    /// Tells the window the list changed: files were added, removed or reordered.
    std::function<void()> onChanged;

    /// Tells the window an import ended.
    std::function<void(const WorkflowImportReport& report)> onImportFinished;

    void onRowSelected(TableWidget* sender, int row) override;
    void onColumnClicked(TableWidget* sender, int column) override;
    void onContextMenuRequested(TableWidget* sender, int row, int col, const QPoint& globalPos) override;

private:
    struct State;
    /// Makes a row the current one, as the table already shows it, and tells the window.
    void currentRowMoved(int bounded);
    void rebuildTable();
    void writeRow(int row);
    void updateHeaders();
    void updateLabels();
    void changed();
    std::uint64_t importLanded(std::vector<WorkflowFile>&& files);
    [[nodiscard]] bool isBusy() const;

    QWidget* m_owner;
    WorkflowFileListWidgets m_widgets;
    WorkflowFileListOptions m_options;
    std::shared_ptr<State> m_state;
    /// Forwards the window's stop (`WorkflowFileListOptions::stop`) to the imports in flight; after `m_state`, which it
    /// reaches.
    std::optional<std::stop_callback<std::function<void()>>> m_windowStop;
    std::vector<WorkflowFile> m_files;
    std::unordered_set<QString> m_paths;
    std::optional<std::size_t> m_sortColumn;
    bool m_sortDescending = false;
    int m_selectedRow = -1;
    bool m_ignoreSelection = false;
    std::uint32_t m_importsInFlight = 0u;
};

} // namespace an::ui::processes
