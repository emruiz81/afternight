// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The rail: the column of places, tasks and foot items at the left of the Observatory window.

#include "AfterNightControlsExport.h"
#include "components/AnTaskThumbnail.h"
#include "components/RailItem.h"

#include <QIcon>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QTimer>
#include <QWidget>

#include <verdigris/wobjectdefs.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

class QContextMenuEvent;
class QEnterEvent;
class QEvent;
class QFocusEvent;
class QKeyEvent;
class QMenu;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
class QWheelEvent;

namespace an::ui
{

class AnIconRailAccessible;

/// The rail: the rail's width, the first surface with a hairline at its right, showing the items it is given - the
/// places from the top, the tasks under them past a short hairline (drawn only when there are tasks), the foot at the
/// bottom - and saying what the user does with them. It decides nothing about what an item stands for or what a drag
/// means.
///
/// A place or a foot item is a 36 px row: a 32 px square with the item's 20 px icon in the secondary text, the state
/// colour laid over it under the pointer. The selected item - a place or a task - has a 3 x 20 bar of the accent at the
/// rail's left edge, and a selected square is the accent's container. A place's badge text is a digit on a 15 px badge
/// at its square's lower right (a floating place carries the window glyph instead); a foot item's is a count on a
/// warning pill at its upper right. A task is a 42 px row with its thumbnail (`AnTaskThumbnail`). A disabled item is
/// drawn at 38 % and does nothing.
///
/// When the tasks do not fit, the wheel scrolls them and the task area ends with an item counting the hidden ones,
/// which opens a list of every task. A click activates an item (`itemActivated`); a right click, the menu key or a
/// press held 500 ms asks for its menu (`itemMenuRequested`). A task's thumbnail pressed and moved the platform's drag
/// distance is dragged: `itemDragStarted`, `itemDragMoved` and `itemDragReleased` with global points, a ghost of the
/// thumbnail under the cursor meanwhile; Esc cancels (`itemDragCancelled`). Hovered for 400 ms an item shows its
/// tooltip at its right (`railItemDescription`). The rail is one stop for the keyboard - Up, Down, Home and End move
/// through the items, Enter and Space activate - and a list to assistive technology, each item named as its tooltip.
///
/// A run's progress repaints its thumbnail at most ten times a second and lays nothing out; a run that says nothing of
/// its progress shows a quarter arc that turns once in 1.2 s, read from the rail's clock, and stays still while the
/// animations are off.
class AFTERNIGHTCONTROLS_API AnIconRail : public QWidget
{
    W_OBJECT(AnIconRail)

public:
    /// What the turning arc reads the time from: the time since some fixed start.
    using Clock = std::function<std::chrono::milliseconds()>;

    /// What turns an icon's key into an icon; the default reads the product's icon files.
    using IconResolver = std::function<QIcon(const QString& key)>;

    explicit AnIconRail(QWidget* parent = nullptr);
    ~AnIconRail() override;

    AnIconRail(const AnIconRail&) = delete;
    AnIconRail& operator=(const AnIconRail&) = delete;

    /// Replaces the items, in their order within each section.
    void setItems(const std::vector<RailItem>& items);

    /// Changes the item of the same id; nothing when there is none. A change of a run's progress alone repaints the
    /// thumbnail at most ten times a second and lays nothing out.
    void updateItem(const RailItem& item);

    [[nodiscard]] const std::vector<RailItem>& items() const noexcept;

    void setClock(Clock clock);
    void setIconResolver(IconResolver resolver);

    /// Where an item is shown, in the rail; empty for an item it does not show (scrolled away, or no such item).
    [[nodiscard]] QRect itemRect(const QString& id) const;

    /// The short hairline between the places and the tasks; empty when there are no tasks.
    [[nodiscard]] QRect hairlineRect() const;

    /// How many tasks the overflow item counts, and where it is; 0 and empty when every task fits.
    [[nodiscard]] int hiddenTaskCount() const;
    [[nodiscard]] QRect overflowRect() const;

    /// The list of every task the overflow item opened, or none.
    [[nodiscard]] QMenu* taskList() const noexcept;

    /// The item's tooltip, which is also its name to assistive technology.
    [[nodiscard]] QString itemToolTip(const QString& id) const;

    /// The item whose tooltip is shown, and the window it is shown in.
    [[nodiscard]] QString shownToolTipItem() const;
    [[nodiscard]] QWidget* toolTipWindow() const noexcept;

    /// The item the keyboard stands on.
    [[nodiscard]] QString focusedItemId() const;

    /// Whether a task's thumbnail is being dragged, and the ghost that follows the cursor meanwhile.
    [[nodiscard]] bool isDragging() const noexcept;
    [[nodiscard]] QWidget* dragGhost() const noexcept;

    /// Ends a drag without a word: what the rail's owner calls when another hand took the drag over - the dock
    /// library's, which then takes the release.
    void endDrag();

    /// Whether a turning arc is being turned.
    [[nodiscard]] bool isTurning() const;

    /// How many times the rail has painted and laid its items out. For the suites.
    [[nodiscard]] std::uint64_t paintCount() const noexcept;
    [[nodiscard]] std::uint64_t layoutCount() const noexcept;

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

void itemActivated(const QString& id)
    W_SIGNAL(itemActivated, id) void itemMenuRequested(const QString& id, const QPoint& globalPos)
        W_SIGNAL(itemMenuRequested, id, globalPos) void itemDragStarted(const QString& id)
            W_SIGNAL(itemDragStarted, id) void itemDragMoved(const QString& id, const QPoint& globalPos)
                W_SIGNAL(itemDragMoved, id, globalPos) void itemDragReleased(const QString& id, const QPoint& globalPos)
                    W_SIGNAL(itemDragReleased, id, globalPos) void itemDragCancelled(const QString& id)
                        W_SIGNAL(itemDragCancelled, id)

                            protected : bool event(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    friend class AnIconRailAccessible;

    /// What the rail shows at its height: each shown item's row, the hairline, the overflow item.
    struct Layout
    {
        std::map<QString, QRect> rows;
        std::vector<QString> order; ///< The shown items, top to bottom.
        QRect hairline;
        QRect overflow;
        int hiddenTasks = 0;
        int taskRows = 0; ///< How many task rows fit.
    };

    [[nodiscard]] Layout layoutAt(int height) const;
    void layOut();
    [[nodiscard]] const RailItem* find(const QString& id) const;
    [[nodiscard]] QString itemAt(const QPoint& position) const;
    [[nodiscard]] QIcon icon(const QString& key) const;
    [[nodiscard]] qreal turn() const;
    [[nodiscard]] int taskCount() const;
    void paintPlace(QPainter& painter, const RailItem& item, const QRect& row) const;
    void paintOverflow(QPainter& painter, const QRect& row) const;
    void refreshThumbnails();
    void refreshTurning();
    void scrollTasksBy(int rows);
    void ensureShown(const QString& id);
    void moveFocus(int step);
    void activate(const QString& id);
    void openTaskList();
    void startDrag(const QPoint& globalPos);
    void finishDrag();
    void showToolTip();
    void hideToolTip();
    void setHovered(const QString& id);

    std::vector<RailItem> m_items;
    std::map<QString, std::unique_ptr<AnTaskThumbnail>> m_thumbnails;
    Layout m_layout;
    int m_taskOffset = 0;
    Clock m_clock;
    IconResolver m_iconResolver;
    mutable std::map<QString, QIcon> m_icons;

    QString m_hovered;
    QString m_focused;
    bool m_keyboardFocus = false;
    std::chrono::steady_clock::time_point m_menuKeyHandled;

    QString m_pressed;
    QPoint m_pressGlobal;
    bool m_holdFired = false;
    bool m_dragging = false;
    QTimer m_holdTimer;

    QTimer m_toolTipTimer;
    QString m_toolTipItem;
    QWidget* m_toolTip = nullptr;
    QWidget* m_ghost = nullptr;
    QMenu* m_taskList = nullptr;

    QTimer m_progressTimer;
    std::vector<QString> m_progressDirty;
    QTimer m_turnTimer;

    std::uint64_t m_paints = 0;
    std::uint64_t m_layouts = 0;
};

} // namespace an::ui
