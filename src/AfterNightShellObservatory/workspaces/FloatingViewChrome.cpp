// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "workspaces/FloatingViewChrome.h"

#include "chrome/ToolbarClearance.h"

#include "components/AnFloatingToolbar.h"
#include "controls/ImageViewControl.h"

#include <QEvent>

#include <algorithm>
#include <utility>

namespace an::ui::observatory
{

FloatingViewChrome::FloatingViewChrome(QObject* parent)
    : QObject(parent), m_clearance(std::make_unique<ToolbarClearance>([this]() {
          for (Entry& entry : m_entries)
          {
              place(entry);
          }
      }))
{
}

FloatingViewChrome::~FloatingViewChrome()
{
    // The players stop telling before the entries go.
    m_clearance.reset();
    for (Entry& entry : m_entries)
    {
        entry.binder.reset();
        if (!entry.view.isNull())
        {
            entry.view->removeEventFilter(this);
        }
        delete entry.toolbar.data();
    }
}

void FloatingViewChrome::setMenuFiller(an::ui::shell::ViewChromeBinder::MenuFiller filler)
{
    m_filler = std::move(filler);
    for (Entry& entry : m_entries)
    {
        entry.binder->setMenuFiller(m_filler);
    }
}

void FloatingViewChrome::update(const std::vector<ImageViewControl*>& views)
{
    // Every entry whose view went, docked or is not listed is taken away.
    m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(),
                                   [this, &views](Entry& entry) {
                                       ImageViewControl* const view = entry.view.data();
                                       const bool listed = view != nullptr &&
                                                           std::find(views.begin(), views.end(), view) != views.end();
                                       if (listed && view->isInFloatingWindow())
                                       {
                                           return false;
                                       }
                                       entry.binder.reset();
                                       if (view != nullptr)
                                       {
                                           view->removeEventFilter(this);
                                       }
                                       delete entry.toolbar.data();
                                       return true;
                                   }),
                    m_entries.end());

    for (ImageViewControl* view : views)
    {
        if (view == nullptr || !view->isInFloatingWindow() || toolbarOf(view) != nullptr)
        {
            continue;
        }
        Entry entry;
        entry.view = view;
        auto* toolbar = new an::ui::AnFloatingToolbar(view);
        toolbar->setObjectName(QStringLiteral("observatoryFloatingWindowToolbar"));
        toolbar->setForm(an::ui::AnFloatingToolbar::Form::Small);
        entry.toolbar = toolbar;
        entry.binder = std::make_unique<an::ui::shell::ViewChromeBinder>(toolbar, nullptr);
        entry.binder->setMenuFiller(m_filler);
        entry.binder->bindToolbar(view);
        view->installEventFilter(this);
        toolbar->show();
        place(entry);
        m_entries.push_back(std::move(entry));
    }
}

an::ui::AnFloatingToolbar* FloatingViewChrome::toolbarOf(const ImageViewControl* view) const
{
    for (const Entry& entry : m_entries)
    {
        if (entry.view.data() == view)
        {
            return entry.toolbar.data();
        }
    }
    return nullptr;
}

bool FloatingViewChrome::eventFilter(QObject* watched, QEvent* event)
{
    if (event != nullptr && (event->type() == QEvent::Resize || event->type() == QEvent::Show))
    {
        for (Entry& entry : m_entries)
        {
            if (entry.view.data() == watched)
            {
                place(entry);
            }
        }
    }
    return QObject::eventFilter(watched, event);
}

void FloatingViewChrome::place(Entry& entry) const
{
    if (entry.view.isNull() || entry.toolbar.isNull())
    {
        return;
    }
    // Over the view, clear of its player when it is a sequence's.
    entry.toolbar->placeOver(entry.view->rect(),
                             m_clearance ? m_clearance->playersIn(*entry.view, *entry.view) : QList<QRect>{});
    entry.toolbar->raise();
}

} // namespace an::ui::observatory
