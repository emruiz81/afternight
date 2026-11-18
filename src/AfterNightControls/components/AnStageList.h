// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// A stage list: the stages of a piece of work, each with where it stands, one of them selected.

#include "AfterNightControlsExport.h"

#include <QListView>
#include <QSize>
#include <QString>

#include <verdigris/wobjectdefs.h>

#include <optional>
#include <vector>

class QFocusEvent;
class QStandardItemModel;

namespace an::ui
{

/// Where a stage stands.
enum class StageStatus
{
    /// Not started: nothing for it to do yet, or it is off.
    Pending,
    /// It has what it needs and has not run.
    Ready,
    Running,
    Done,
    Warning,
    Failed,
    /// It ran, and what it made is no longer what is asked of it.
    OutOfDate
};

/// A part of a stage on its way, as its row shows it under its line: "Detect".
struct StageStep
{
    QString name;

    /// Done, running - the part the stage is at - or pending for one to come.
    StageStatus status = StageStatus::Pending;

    [[nodiscard]] bool operator==(const StageStep&) const = default;
};

/// A stage as a stage list shows it.
struct StageRow
{
    QString id; ///< What the list tells the stage by; unique in a list.
    QString title; ///< "Registration".
    QString line; ///< One line of state: "running - 412 / 718".
    StageStatus status = StageStatus::Pending;
    std::optional<int> progress; ///< How far a run has come, 0 to 100; no bar when absent.
    QString toolTip; ///< What the row adds when it is pointed at; none when empty.
    std::vector<StageStep> steps; ///< The parts of a stage on its way, under its line; none for most.

    [[nodiscard]] bool operator==(const StageRow&) const = default;
};

/// Rows of a status mark, a title in the body text and semibold, and one line of state in the monospace at the
/// caption's size in the secondary text, elided at the right; a stage with a progress shows a thin bar under its line,
/// the accent over the accent's track. A stage that says its parts (`StageRow::steps`) lists them under its line, one
/// a line in the line's font, each after a small mark - a check for a part done, the accent's dot for the part it is
/// at, a ring for one to come - the part it is at in the primary text. The marks are joined by a line. A mark is a ring
/// of the tertiary text for a stage to come, a ring of the accent with a dot at its centre for one that is ready, the
/// accent filled while it runs, success with a check when done, the warning with an exclamation mark, the error with a
/// cross, and a ring of the warning with a check for one that is out of date: never colour alone. The selected row is
/// the accent's container with its texts in the container's text; a hovered row lays the state colour over itself.
///
/// The rows are given whole (`setRows`) or one at a time by id (`updateRow`), and a list given again keeps its
/// selection. The keyboard moves the selection with Up and Down, the pointer selects a row by a click, and every new
/// selection is told by the stage's id (`selectedStageChanged`). Assistive technology reads a list whose rows are named
/// by the title, the state and the line, and by the part a stage is at among its parts when it says them.
class AFTERNIGHTCONTROLS_API AnStageList : public QListView
{
    W_OBJECT(AnStageList)

public:
    explicit AnStageList(QWidget* parent = nullptr);
    ~AnStageList() override;

    AnStageList(const AnStageList&) = delete;
    AnStageList& operator=(const AnStageList&) = delete;

    void setRows(const std::vector<StageRow>& rows);

    /// Changes the row of the same id; nothing when there is none.
    void updateRow(const StageRow& row);

    [[nodiscard]] std::vector<StageRow> rows() const;
    [[nodiscard]] int rowCount() const;

    /// Selects the stage of an id, or none for an id no row has.
    void setSelectedId(const QString& id);
    [[nodiscard]] QString selectedId() const;

    /// As tall as every row; as wide as the widest title or line with the mark and the padding.
    [[nodiscard]] QSize sizeHint() const override;

    /// Whether the keyboard brought the focus: the current row draws the focus ring only then.
    [[nodiscard]] bool hasKeyboardFocus() const noexcept;

    /// Told when another stage is selected, with its id (empty for none).
void selectedStageChanged(const QString& id) W_SIGNAL(selectedStageChanged, id)

    protected : void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    void onSelectionChanged();

    QStandardItemModel* m_model = nullptr;
    QString m_selectedId;
    bool m_keyboardFocus = false;
    bool m_settingRows = false;
};

} // namespace an::ui
