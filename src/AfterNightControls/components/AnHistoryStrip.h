// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The history strip: the steps of a view's history as chips, and its Undo, Redo and Image History.

#include "AfterNightControlsExport.h"
#include "controls/ViewChrome.h"

#include <QSize>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <verdigris/wobjectdefs.h>

#include <cstddef>
#include <memory>
#include <optional>

class QAbstractButton;
class QAction;
class QEvent;
class QPaintEvent;
class QResizeEvent;

namespace an::ui
{

/// The chip a strip adds after the current step while a task is open on the image: the step it will make.
struct HistoryPendingStep
{
    /// The process the task runs: "Curves Transformation".
    QString processName;

    /// Whether a run is writing the image now; otherwise the step waits for Apply.
    bool writing = false;

    [[nodiscard]] bool operator==(const HistoryPendingStep&) const = default;
};

/// A strip `metrics.strip` high on the first surface with a hairline above it: a caps label "History", then a view's
/// steps as chips - pills `metrics.chip` high in an outline, their names in semibold - with chevrons between them, the
/// step shown in the accent's container with a check, and an inherited step in the tertiary text. A task open on the
/// image adds a dashed chip after the current step: an eye, the process's name and "pending", or while a run writes the
/// image a spinner and "writing". When the steps do not fit, the oldest give way to a "..." chip that opens the Image
/// History window. At its right, the view's own Undo, Redo and Image History, enabled as the history has them.
///
/// A click on one of the view's own steps other than the one shown asks for it (`stepClicked`); a host moves the image
/// there through the history (`IViewHistory::jumpToStep`). The strip reads nothing of the view but what it is told.
class AFTERNIGHTCONTROLS_API AnHistoryStrip : public QWidget
{
    W_OBJECT(AnHistoryStrip)

public:
    explicit AnHistoryStrip(QWidget* parent = nullptr);
    ~AnHistoryStrip() override;

    AnHistoryStrip(const AnHistoryStrip&) = delete;
    AnHistoryStrip& operator=(const AnHistoryStrip&) = delete;

    /// Shows a history's steps and the one shown; none shows no chip.
    void setSteps(const ViewHistorySteps& steps);
    [[nodiscard]] const ViewHistorySteps& steps() const noexcept;

    /// Shows the chip of a task open on the image, or none.
    void setPendingStep(std::optional<HistoryPendingStep> pending);
    [[nodiscard]] const std::optional<HistoryPendingStep>& pendingStep() const noexcept;

    /// The view's Undo, Redo and Image History, which its buttons are; null shows no button for one. The "..." chip
    /// triggers Image History.
    void setActions(QAction* undo, QAction* redo, QAction* history);

    /// The chips shown, from the left: each step's name, "..." for the steps that gave way, and the pending chip's
    /// process.
    [[nodiscard]] QStringList shownChips() const;

    /// The chip of a step of `steps()`, or null while it is not shown.
    [[nodiscard]] QAbstractButton* chipForStep(std::size_t index) const;

    /// The "..." chip, or null while every step fits.
    [[nodiscard]] QAbstractButton* overflowChip() const;

    /// The pending chip, or null while no task is open.
    [[nodiscard]] QAbstractButton* pendingChip() const;

    /// The buttons at the right.
    [[nodiscard]] QAbstractButton* undoButton() const;
    [[nodiscard]] QAbstractButton* redoButton() const;
    [[nodiscard]] QAbstractButton* historyButton() const;

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

    /// Asked by a click on one of the view's own steps other than the one shown, with its index in `steps()`.
    void stepClicked(int index) W_SIGNAL(stepClicked, index);

protected:
    bool event(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace an::ui
