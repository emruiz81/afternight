// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "components/AnFlowLayout.h"

#include <QLayoutItem>
#include <QWidget>

#include <algorithm>

namespace an::ui
{

AnFlowLayout::AnFlowLayout(QWidget* parent) : QLayout(parent) {}

AnFlowLayout::~AnFlowLayout()
{
    while (QLayoutItem* item = takeAt(0))
    {
        delete item;
    }
}

void AnFlowLayout::addItem(QLayoutItem* item)
{
    m_items.append(item);
}

int AnFlowLayout::count() const
{
    return static_cast<int>(m_items.size());
}

QLayoutItem* AnFlowLayout::itemAt(const int index) const
{
    return index >= 0 && index < m_items.size() ? m_items.at(index) : nullptr;
}

QLayoutItem* AnFlowLayout::takeAt(const int index)
{
    return index >= 0 && index < m_items.size() ? m_items.takeAt(index) : nullptr;
}

Qt::Orientations AnFlowLayout::expandingDirections() const
{
    return {};
}

bool AnFlowLayout::hasHeightForWidth() const
{
    return true;
}

int AnFlowLayout::heightForWidth(const int width) const
{
    return arrange(QRect(0, 0, width, 0), false);
}

QSize AnFlowLayout::sizeHint() const
{
    // One row of every item, as a layout with no width to wrap in measures.
    int width = 0;
    int height = 0;
    for (const QLayoutItem* item : m_items)
    {
        if (item->isEmpty())
        {
            continue;
        }
        const QSize hint = item->sizeHint();
        width += (width > 0 ? spacing() : 0) + hint.width();
        height = (std::max)(height, hint.height());
    }
    const QMargins margins = contentsMargins();
    return QSize(width + margins.left() + margins.right(), height + margins.top() + margins.bottom());
}

QSize AnFlowLayout::minimumSize() const
{
    // As wide as its widest item: anything wider wraps.
    QSize size;
    for (const QLayoutItem* item : m_items)
    {
        if (!item->isEmpty())
        {
            size = size.expandedTo(item->minimumSize());
        }
    }
    const QMargins margins = contentsMargins();
    return size + QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
}

void AnFlowLayout::setGeometry(const QRect& rect)
{
    QLayout::setGeometry(rect);
    (void)arrange(rect, true);
}

int AnFlowLayout::arrange(const QRect& rect, const bool place) const
{
    const QRect area = rect.marginsRemoved(contentsMargins());
    const int gap = (std::max)(0, spacing());
    int x = area.x();
    int y = area.y();
    int rowHeight = 0;
    bool any = false;
    for (QLayoutItem* item : m_items)
    {
        if (item->isEmpty())
        {
            continue;
        }
        const QSize hint = item->sizeHint();
        if (any && x > area.x() && x + hint.width() > area.right() + 1)
        {
            x = area.x();
            y += rowHeight + gap;
            rowHeight = 0;
        }
        if (place)
        {
            item->setGeometry(QRect(QPoint(x, y), hint));
        }
        x += hint.width() + gap;
        rowHeight = (std::max)(rowHeight, hint.height());
        any = true;
    }
    const QMargins margins = contentsMargins();
    return any ? y + rowHeight - rect.y() + margins.bottom() : margins.top() + margins.bottom();
}

} // namespace an::ui
