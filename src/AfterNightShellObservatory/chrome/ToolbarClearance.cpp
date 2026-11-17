// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "chrome/ToolbarClearance.h"

#include "controls/ImageViewCompanions.h"
#include "controls/ImageViewControl.h"

#include <QEvent>
#include <QWidget>

#include <algorithm>
#include <utility>

namespace an::ui::observatory
{

ToolbarClearance::ToolbarClearance(std::function<void()> changed, QObject* parent)
    : QObject(parent), m_changed(std::move(changed))
{
}

ToolbarClearance::~ToolbarClearance()
{
    for (const QPointer<QObject>& watched : m_watched)
    {
        if (!watched.isNull())
        {
            watched->removeEventFilter(this);
        }
    }
}

QList<QRect> ToolbarClearance::playersIn(QWidget& area, const QWidget& over)
{
    QList<ImageViewControl*> views = area.findChildren<ImageViewControl*>();
    if (auto* const view = qobject_cast<ImageViewControl*>(&area))
    {
        views.prepend(view);
    }
    QList<QRect> strips;
    for (ImageViewControl* const view : views)
    {
        // A view floating in a window of its own is under no toolbar of this one.
        if (view->window() != over.window())
        {
            continue;
        }
        watch(*view);
        an::ui::IViewSequencePlayer* const player = view->sequencePlayer();
        QWidget* const strip = player != nullptr ? player->widget() : nullptr;
        if (strip == nullptr)
        {
            continue;
        }
        watch(*strip);
        if (strip->isVisibleTo(&over))
        {
            strips.append(QRect(over.mapFromGlobal(strip->mapToGlobal(QPoint(0, 0))), strip->size()));
        }
    }
    return strips;
}

bool ToolbarClearance::eventFilter(QObject* watched, QEvent* event)
{
    switch (event->type())
    {
    case QEvent::Show:
    case QEvent::Hide:
    case QEvent::Move:
    case QEvent::Resize:
    case QEvent::ChildAdded:
        // A view is given its player as a child; the player is watched once found, and then tells its own show.
        if (!m_telling && m_changed)
        {
            m_telling = true;
            m_changed();
            m_telling = false;
        }
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

void ToolbarClearance::watch(QObject& object)
{
    m_watched.erase(std::remove_if(m_watched.begin(), m_watched.end(),
                                   [](const QPointer<QObject>& watched) { return watched.isNull(); }),
                    m_watched.end());
    if (std::find(m_watched.begin(), m_watched.end(), &object) != m_watched.end())
    {
        return;
    }
    object.installEventFilter(this);
    m_watched.emplace_back(&object);
}

} // namespace an::ui::observatory
