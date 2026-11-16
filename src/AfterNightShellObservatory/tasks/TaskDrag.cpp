// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "tasks/TaskDrag.h"

#include "components/AnIconRail.h"

#include <QObject>

#include <memory>
#include <utility>

namespace an::ui::observatory
{

bool windowFollowsTheCursor(const QString& platformName)
{
    // Wayland leaves the moving of a window to its compositor: an application cannot keep its window under the cursor.
    return !platformName.startsWith(QLatin1String("wayland"), Qt::CaseInsensitive);
}

TaskDrag::TaskDrag(Surface& surface) : m_surface(surface) {}

TaskDrag::~TaskDrag()
{
    for (const QMetaObject::Connection& connection : m_railConnections)
    {
        QObject::disconnect(connection);
    }
}

void TaskDrag::followRail(an::ui::AnIconRail& rail, std::function<an::ui::shell::TaskId(const QString& itemId)> taskOf)
{
    for (const QMetaObject::Connection& connection : m_railConnections)
    {
        QObject::disconnect(connection);
    }
    m_railConnections.clear();
    auto named = std::make_shared<std::function<an::ui::shell::TaskId(const QString&)>>(std::move(taskOf));
    // The rail tells a drag's start and then its first move, with the cursor's place.
    m_railConnections.push_back(
        QObject::connect(&rail, &an::ui::AnIconRail::itemDragStarted, &rail,
                         [this, named](const QString& id) { railDragStarted((*named)(id), QPoint()); }));
    m_railConnections.push_back(QObject::connect(&rail, &an::ui::AnIconRail::itemDragMoved, &rail,
                                                 [this, named](const QString& id, const QPoint& globalPos) {
                                                     if (m_task != 0u && (*named)(id) == m_task)
                                                     {
                                                         railDragMoved(globalPos);
                                                     }
                                                 }));
    m_railConnections.push_back(QObject::connect(&rail, &an::ui::AnIconRail::itemDragReleased, &rail,
                                                 [this, named](const QString& id, const QPoint& globalPos) {
                                                     if (m_task != 0u && (*named)(id) == m_task)
                                                     {
                                                         railDragReleased(globalPos);
                                                     }
                                                 }));
    m_railConnections.push_back(
        QObject::connect(&rail, &an::ui::AnIconRail::itemDragCancelled, &rail, [this, named](const QString& id) {
            if (m_task != 0u && (*named)(id) == m_task)
            {
                railDragCancelled();
            }
        }));
}

void TaskDrag::railDragStarted(const an::ui::shell::TaskId id, const QPoint /*globalPos*/)
{
    reset();
    if (id == 0u)
    {
        return;
    }
    m_phase = Phase::Ghost;
    m_task = id;
}

void TaskDrag::railDragMoved(const QPoint globalPos)
{
    if (m_phase != Phase::Ghost || m_surface.frame().contains(globalPos))
    {
        // Inside the main window the ghost alone follows the cursor.
        return;
    }
    if (!m_surface.windowFollowsTheCursor())
    {
        // The ghost goes on; the task floats where the button is released.
        return;
    }
    // Past the frame: the task floats under the cursor and the dock library's drag of its window takes the press over.
    const an::ui::shell::TaskId id = m_task;
    m_surface.endRailDrag();
    m_phase = Phase::Window;
    if (!m_surface.floatUnder(id, globalPos, true))
    {
        // The library took no drag: the task floats where it was put.
        reset();
    }
}

void TaskDrag::railDragReleased(const QPoint globalPos)
{
    if (m_phase != Phase::Ghost)
    {
        return;
    }
    const an::ui::shell::TaskId id = m_task;
    const bool inside = m_surface.frame().contains(globalPos);
    reset();
    if (!inside)
    {
        // Where a window cannot follow the cursor, the task floats at the release.
        (void)m_surface.floatUnder(id, globalPos, false);
    }
}

void TaskDrag::railDragCancelled()
{
    if (m_phase == Phase::Ghost)
    {
        reset();
    }
}

void TaskDrag::windowDragStarted(const an::ui::shell::TaskId id)
{
    if (id == 0u)
    {
        return;
    }
    if (m_task == id && (m_phase == Phase::Window || m_phase == Phase::Ghost))
    {
        // Asked again before the drag starts, or the rail's pull handing its press over: the same drag.
        m_phase = Phase::Window;
        return;
    }
    reset();
    m_phase = Phase::Window;
    m_task = id;
}

void TaskDrag::windowDragMoved(const QPoint globalPos)
{
    if (m_phase != Phase::Window)
    {
        return;
    }
    showZone(zoneAt(globalPos));
}

void TaskDrag::windowDragEnded(const QPoint globalPos)
{
    if (m_phase != Phase::Window)
    {
        return;
    }
    const an::ui::shell::TaskId id = m_task;
    const AttachZone zone = zoneAt(globalPos);
    reset();
    if (zone != AttachZone::None)
    {
        m_surface.attach(id);
    }
}

void TaskDrag::windowDragLeft()
{
    if (m_phase != Phase::Window)
    {
        return;
    }
    showZone(AttachZone::None);
}

void TaskDrag::windowDragCancelled()
{
    if (m_phase != Phase::Window)
    {
        return;
    }
    reset();
}

TaskDrag::Phase TaskDrag::phase() const noexcept
{
    return m_phase;
}

an::ui::shell::TaskId TaskDrag::task() const noexcept
{
    return m_task;
}

AttachZone TaskDrag::zone() const noexcept
{
    return m_zone;
}

AttachZone TaskDrag::zoneAt(const QPoint globalPos) const
{
    if (!m_surface.frame().contains(globalPos))
    {
        return AttachZone::None;
    }
    if (m_surface.rail().contains(globalPos))
    {
        return AttachZone::Rail;
    }
    if (m_surface.centre().contains(globalPos))
    {
        return AttachZone::Centre;
    }
    return AttachZone::None;
}

void TaskDrag::showZone(const AttachZone zone)
{
    if (zone == m_zone)
    {
        return;
    }
    m_zone = zone;
    m_surface.showAttachZone(zone, zone == AttachZone::None ? an::ui::shell::TaskId{0} : m_task);
}

void TaskDrag::reset()
{
    showZone(AttachZone::None);
    m_phase = Phase::Idle;
    m_task = 0;
}

} // namespace an::ui::observatory
