// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "components/AnSideBySideLayout.h"

#include <QByteArray>
#include <QLayoutItem>
#include <QMargins>
#include <QSpacerItem>
#include <QStyle>
#include <QWidget>

#include <algorithm>

namespace an::ui
{

namespace
{

/// The box layout that holds a widget itself, searched from a layout down; null where none does.
QBoxLayout* boxHolding(QLayout& layout, const QWidget& widget)
{
    for (int index = 0; index < layout.count(); ++index)
    {
        QLayoutItem* const item = layout.itemAt(index);
        if (item == nullptr)
        {
            continue;
        }
        if (item->widget() == &widget)
        {
            return qobject_cast<QBoxLayout*>(&layout);
        }
        if (QLayout* const inner = item->layout(); inner != nullptr)
        {
            if (QBoxLayout* const found = boxHolding(*inner, widget); found != nullptr)
            {
                return found;
            }
        }
    }
    return nullptr;
}

} // namespace

AnSideBySideLayout::AnSideBySideLayout(QWidget* parent) : QBoxLayout(QBoxLayout::LeftToRight, parent) {}

AnSideBySideLayout::~AnSideBySideLayout() = default;

bool AnSideBySideLayout::sideBySide() const
{
    return direction() == QBoxLayout::LeftToRight;
}

int AnSideBySideLayout::sideBySideWidth() const
{
    int gap = spacing();
    if (gap < 0)
    {
        const QWidget* const owner = parentWidget();
        gap = owner != nullptr ? owner->style()->pixelMetric(QStyle::PM_LayoutHorizontalSpacing, nullptr, owner) : 0;
        gap = (std::max)(0, gap);
    }
    const QMargins margins = contentsMargins();
    int width = margins.left() + margins.right();
    int shown = 0;
    for (int index = 0; index < count(); ++index)
    {
        QLayoutItem* const item = itemAt(index);
        if (item == nullptr)
        {
            continue;
        }
        if (QSpacerItem* const room = item->spacerItem(); room != nullptr)
        {
            // A stated spacing holds its room side by side and a stretch none; the layout turns a spacing's room with
            // its direction, so it is read whichever way it stands now.
            const QSize least = room->minimumSize();
            width += (std::max)(least.width(), least.height());
            continue;
        }
        if (item->isEmpty())
        {
            continue;
        }
        width += item->minimumSize().width();
        ++shown;
    }
    return width + gap * (std::max)(0, shown - 1);
}

QSize AnSideBySideLayout::minimumSize() const
{
    // One under another the row needs no more than its widest item; side by side it needs more, and stands so only
    // where it is given that.
    const QMargins margins = contentsMargins();
    int widest = 0;
    for (int index = 0; index < count(); ++index)
    {
        QLayoutItem* const item = itemAt(index);
        if (item != nullptr && !item->isEmpty())
        {
            widest = (std::max)(widest, item->minimumSize().width());
        }
    }
    return {margins.left() + margins.right() + widest, QBoxLayout::minimumSize().height()};
}

void AnSideBySideLayout::setGeometry(const QRect& rect)
{
    if (rect.width() > 0)
    {
        const QBoxLayout::Direction wanted =
            rect.width() >= sideBySideWidth() ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom;
        // Turned only where it changes: the turn asks the parent to lay out again, which comes back here at the same
        // width and leaves the items as they stand.
        if (direction() != wanted)
        {
            setDirection(wanted);
        }
    }
    QBoxLayout::setGeometry(rect);
}

AnSideBySideLayout* standSideBySideWhereItFits(QBoxLayout& row)
{
    auto* const turned = new AnSideBySideLayout();
    if (!row.objectName().isEmpty())
    {
        turned->setObjectName(row.objectName() + QStringLiteral("SideBySide"));
    }
    for (const QByteArray& name : row.dynamicPropertyNames())
    {
        turned->setProperty(name.constData(), row.property(name.constData()));
    }
    turned->setContentsMargins(row.contentsMargins());
    // Placed in the row first, so that the widgets it takes keep the widget they lie in.
    const int moved = row.count();
    row.addLayout(turned);
    for (int index = 0; index < moved; ++index)
    {
        const int stretch = row.stretch(0);
        QLayoutItem* const item = row.takeAt(0);
        if (QWidget* const widget = item->widget(); widget != nullptr)
        {
            const Qt::Alignment alignment = item->alignment();
            delete item;
            turned->addWidget(widget, stretch, alignment);
        }
        else if (QLayout* const layout = item->layout(); layout != nullptr)
        {
            turned->addLayout(layout, stretch);
        }
        else
        {
            turned->addItem(item);
            turned->setStretch(turned->count() - 1, stretch);
        }
    }
    row.setContentsMargins(0, 0, 0, 0);
    return turned;
}

AnSideBySideLayout* standSideBySideWhereItFits(QWidget& member)
{
    QWidget* const holder = member.parentWidget();
    QLayout* const layout = holder != nullptr ? holder->layout() : nullptr;
    QBoxLayout* const row = layout != nullptr ? boxHolding(*layout, member) : nullptr;
    return row != nullptr ? standSideBySideWhereItFits(*row) : nullptr;
}

} // namespace an::ui
