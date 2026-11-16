// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// A task dragged between the rail and a window of its own: what a drag of a task's rail thumbnail and a drag of a
/// task's window come to, decided from where the cursor stands against the main window.

#include "AfterNightShellObservatoryExport.h"

#include "tasks/TaskModel.h"

#include <QMetaObject>
#include <QPoint>
#include <QRect>
#include <QString>

#include <cstdint>
#include <functional>
#include <vector>

namespace an::ui
{
class AnIconRail;
} // namespace an::ui

namespace an::ui::observatory
{

/// Where a task's window dragged over the main window would attach: over the rail, over the centre, or nowhere.
enum class AttachZone : std::uint8_t
{
    None,
    Rail,
    Centre
};

/// Whether an application can keep its own window under the cursor while a button is down, by the platform's name:
/// everywhere but under Wayland, whose compositor alone moves a window. On Windows the system's move follows the
/// cursor, and on X11 the dock library moves the window at each move it hears.
[[nodiscard]] AFTERNIGHTSHELLOBSERVATORY_API bool windowFollowsTheCursor(const QString& platformName);

/// The controller of a task's drags. It takes what the rail says of a drag of a task's thumbnail, and what the dock
/// library says of a drag of a task's window, and decides what each comes to from the cursor's place against the main
/// window - its frame, its rail and its centre - which a `Surface` answers, and which acts on what it decides:
///
/// - A thumbnail pressed and moved the platform's drag distance shows its ghost under the cursor (the rail draws it).
///   While the cursor is inside the main window's frame nothing else happens, and a release there changes nothing.
/// - When the cursor leaves the frame the task floats in its window with the window's top under the cursor, and the
///   press goes on as the dock library's drag of that window, which follows the cursor until the button is released;
///   the rail's ghost ends. Where a window cannot follow the cursor (`windowFollowsTheCursor`) the ghost goes on and
///   the task floats where the button is released, outside the frame.
/// - While a task's window is dragged - pulled out of the rail, or by its title bar - and the cursor is over the rail
///   or the centre, that zone is covered by the attach overlay and the rail marks the task's place. Released there,
///   the task attaches: its page in the centre, the current task, the focus in it. Released anywhere else - over the
///   top bar, the column, the status line or outside - it stays a window.
///
/// It holds no widget. Used on the GUI thread.
class AFTERNIGHTSHELLOBSERVATORY_API TaskDrag final
{
public:
    /// What a drag reads of the main window, and what it does there. The Observatory window answers it; a suite injects
    /// one of its own.
    class Surface
    {
    public:
        Surface() = default;
        virtual ~Surface() = default;
        Surface(const Surface&) = delete;
        Surface& operator=(const Surface&) = delete;
        Surface(Surface&&) = delete;
        Surface& operator=(Surface&&) = delete;

        /// The main window's frame, its rail and its centre, on the screen.
        [[nodiscard]] virtual QRect frame() const = 0;
        [[nodiscard]] virtual QRect rail() const = 0;
        [[nodiscard]] virtual QRect centre() const = 0;

        /// Whether a window can follow the cursor here.
        [[nodiscard]] virtual bool windowFollowsTheCursor() const = 0;

        /// Floats a task in its window, the window's top under a point on the screen. With `follow`, the press goes on
        /// as the dock library's drag of that window. True when a drag of the window follows.
        virtual bool floatUnder(an::ui::shell::TaskId id, QPoint globalPos, bool follow) = 0;

        /// Attaches a floating task: its page in the centre, the current task, the focus in it.
        virtual void attach(an::ui::shell::TaskId id) = 0;

        /// Covers a zone with the attach overlay - `None` takes it away - and marks a task's place in the rail, none
        /// for zero.
        virtual void showAttachZone(AttachZone zone, an::ui::shell::TaskId id) = 0;

        /// Ends the rail's own drag and its ghost, once the dock library has taken the drag over.
        virtual void endRailDrag() = 0;
    };

    /// Where a drag stands.
    enum class Phase : std::uint8_t
    {
        /// No drag.
        Idle,
        /// A task's thumbnail dragged from the rail, its ghost under the cursor.
        Ghost,
        /// A task's window dragged by the dock library: pulled out of the rail, or by its title bar.
        Window
    };

    /// The surface outlives the controller.
    explicit TaskDrag(Surface& surface);
    ~TaskDrag();

    TaskDrag(const TaskDrag&) = delete;
    TaskDrag& operator=(const TaskDrag&) = delete;
    TaskDrag(TaskDrag&&) = delete;
    TaskDrag& operator=(TaskDrag&&) = delete;

    /// Hears a rail's drags of its tasks' thumbnails. `taskOf` names the task of a rail item: zero for an item that is
    /// no task. The rail outlives the controller, or the controller follows another before it goes.
    void followRail(an::ui::AnIconRail& rail, std::function<an::ui::shell::TaskId(const QString& itemId)> taskOf);

    // ---- What the rail says of a thumbnail's drag, with the cursor's place on the screen.

    void railDragStarted(an::ui::shell::TaskId id, QPoint globalPos);
    void railDragMoved(QPoint globalPos);
    void railDragReleased(QPoint globalPos);
    /// Esc: nothing happens.
    void railDragCancelled();

    // ---- What the dock library says of a drag of a task's window.

    /// Asked again at every move before the drag starts: a drag of the same task already known changes nothing.
    void windowDragStarted(an::ui::shell::TaskId id);
    void windowDragMoved(QPoint globalPos);
    void windowDragEnded(QPoint globalPos);

    [[nodiscard]] Phase phase() const noexcept;
    /// The task dragged; zero for none.
    [[nodiscard]] an::ui::shell::TaskId task() const noexcept;
    /// The zone the overlay covers now.
    [[nodiscard]] AttachZone zone() const noexcept;

    /// The zone under a point on the screen: the rail, the centre, or none - the top bar, the column, the status line,
    /// the console's drawer and everything outside the main window.
    [[nodiscard]] AttachZone zoneAt(QPoint globalPos) const;

private:
    void showZone(AttachZone zone);
    void reset();

    Surface& m_surface;
    Phase m_phase = Phase::Idle;
    an::ui::shell::TaskId m_task = 0;
    AttachZone m_zone = AttachZone::None;
    std::vector<QMetaObject::Connection> m_railConnections;
};

} // namespace an::ui::observatory
