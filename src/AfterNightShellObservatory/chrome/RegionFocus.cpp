// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "chrome/RegionFocus.h"

#include <QApplication>
#include <QWidget>

#include <utility>

namespace an::ui::observatory
{

namespace
{

/// Whether `widget` is `area` or lies in it.
[[nodiscard]] bool liesIn(const QWidget* widget, const QWidget* area)
{
    return widget != nullptr && area != nullptr && (widget == area || area->isAncestorOf(widget));
}

} // namespace

RegionFocus::RegionFocus(QWidget& window, std::vector<Region> regions)
    : m_window(&window), m_regions(std::move(regions)), m_last(m_regions.size())
{
    // The control the focus is in is kept for its region, so that the focus comes back to it.
    m_following = QObject::connect(qApp, &QApplication::focusChanged, &window, [this](QWidget* /*old*/, QWidget* now) {
        const int index = indexOf(now);
        if (index >= 0)
        {
            m_last[static_cast<std::size_t>(index)] = now;
        }
    });
}

RegionFocus::~RegionFocus()
{
    QObject::disconnect(m_following);
}

bool RegionFocus::next()
{
    return move(1);
}

bool RegionFocus::previous()
{
    return move(-1);
}

QString RegionFocus::current() const
{
    const int index = indexOf(QApplication::focusWidget());
    return index >= 0 ? m_regions[static_cast<std::size_t>(index)].name : QString();
}

QString RegionFocus::regionOf(const QWidget* widget) const
{
    const int index = indexOf(widget);
    return index >= 0 ? m_regions[static_cast<std::size_t>(index)].name : QString();
}

int RegionFocus::indexOf(const QWidget* widget) const
{
    if (widget == nullptr || m_window.isNull() || !liesIn(widget, m_window.data()))
    {
        return -1;
    }
    // The innermost region whose area holds the widget: an area inside another is the more particular.
    int found = -1;
    const QWidget* foundArea = nullptr;
    for (std::size_t index = 0; index < m_regions.size(); ++index)
    {
        if (!isShown(static_cast<int>(index)))
        {
            continue;
        }
        const QWidget* const area = m_regions[index].area();
        if (liesIn(widget, area) && (foundArea == nullptr || foundArea->isAncestorOf(area)))
        {
            found = static_cast<int>(index);
            foundArea = area;
        }
    }
    return found;
}

bool RegionFocus::isShown(const int index) const
{
    const Region& region = m_regions[static_cast<std::size_t>(index)];
    const QWidget* const area = region.area ? region.area() : nullptr;
    return area != nullptr && !m_window.isNull() && area->isVisibleTo(m_window.data()) && m_window->isVisible() &&
           area->isEnabled();
}

bool RegionFocus::canTakeFocus(const QWidget* widget, const int index) const
{
    return widget != nullptr && !m_window.isNull() && widget->isVisibleTo(m_window.data()) && widget->isEnabled() &&
           (widget->focusPolicy() & Qt::TabFocus) != 0 && widget->window() == m_window.data() &&
           indexOf(widget) == index;
}

QWidget* RegionFocus::target(const int index) const
{
    const Region& region = m_regions[static_cast<std::size_t>(index)];
    // The control the focus left the region from.
    if (QWidget* const last = m_last[static_cast<std::size_t>(index)].data(); canTakeFocus(last, index))
    {
        return last;
    }
    // The region's preferred widget, or what it hands the focus to.
    if (region.preferred)
    {
        QWidget* preferred = region.preferred();
        while (preferred != nullptr && preferred->focusProxy() != nullptr)
        {
            preferred = preferred->focusProxy();
        }
        if (canTakeFocus(preferred, index))
        {
            return preferred;
        }
    }
    // The region's first control in the focus chain.
    QWidget* const area = region.area();
    QWidget* candidate = area;
    do
    {
        if (canTakeFocus(candidate, index))
        {
            return candidate;
        }
        candidate = candidate->nextInFocusChain();
    } while (candidate != nullptr && candidate != area);
    return nullptr;
}

bool RegionFocus::move(const int step)
{
    const int count = static_cast<int>(m_regions.size());
    if (count == 0 || m_window.isNull())
    {
        return false;
    }
    const int from = indexOf(QApplication::focusWidget());
    // From no region, the first one forward and the last one back.
    int index = from >= 0 ? from : (step > 0 ? count - 1 : 0);
    for (int tried = 0; tried < count; ++tried)
    {
        index = ((index + step) % count + count) % count;
        if (index == from || !isShown(index))
        {
            continue;
        }
        if (QWidget* const widget = target(index))
        {
            widget->setFocus(Qt::ShortcutFocusReason);
            return true;
        }
    }
    return false;
}

} // namespace an::ui::observatory
