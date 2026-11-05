// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QLayout>
#include <QStyle>
#include <QWidget>

#include <algorithm>
#include <cmath>

/// A flow layout that arranges child widgets left-to-right, wrapping to a new
/// row when the available width is exceeded. Can be used in any context that
/// needs a tag-cloud / chip-bar style arrangement.
class FlowLayout final : public QLayout
{
public:
    explicit FlowLayout(QWidget* parent = nullptr, int margin = -1, int hSpacing = -1, int vSpacing = -1)
        : QLayout(parent), m_hSpace(hSpacing), m_vSpace(vSpacing)
    {
        setContentsMargins(margin, margin, margin, margin);
    }

    // Canonical Qt layout destructor: takeAt() transfers ownership of each
    // QLayoutItem out of the internal list, so the caller must delete it.
    // This is the standard pattern from Qt's own FlowLayout example.
    ~FlowLayout() override
    {
        QLayoutItem* item = nullptr;
        while ((item = takeAt(0)))
        {
            delete item;
        }
    }

    void addItem(QLayoutItem* item) override
    {
        m_itemList.append(item);
    }

    [[nodiscard]] int horizontalSpacing() const
    {
        if (m_hSpace >= 0)
        {
            return m_hSpace;
        }
        return smartSpacing(QStyle::PM_LayoutHorizontalSpacing);
    }

    [[nodiscard]] int verticalSpacing() const
    {
        if (m_vSpace >= 0)
        {
            return m_vSpace;
        }
        return smartSpacing(QStyle::PM_LayoutVerticalSpacing);
    }

    [[nodiscard]] int count() const override
    {
        return m_itemList.size();
    }

    [[nodiscard]] QLayoutItem* itemAt(int index) const override
    {
        return index >= 0 && index < m_itemList.size() ? m_itemList.at(index) : nullptr;
    }

    QLayoutItem* takeAt(int index) override
    {
        return index >= 0 && index < m_itemList.size() ? m_itemList.takeAt(index) : nullptr;
    }

    [[nodiscard]] Qt::Orientations expandingDirections() const override
    {
        return {};
    }

    [[nodiscard]] bool hasHeightForWidth() const override
    {
        return true;
    }

    [[nodiscard]] int heightForWidth(int width) const override
    {
        return doLayout(QRect(0, 0, width, 0), true);
    }

    void setGeometry(const QRect& rect) override
    {
        QLayout::setGeometry(rect);
        (void)doLayout(rect, false);
    }

    [[nodiscard]] QSize sizeHint() const override
    {
        return minimumSize();
    }

    [[nodiscard]] QSize minimumSize() const override
    {
        QSize size;
        for (QLayoutItem* item : m_itemList)
        {
            size = size.expandedTo(item->minimumSize());
        }

        const QMargins margins = contentsMargins();
        size += QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
        return size;
    }

private:
    [[nodiscard]] int doLayout(const QRect& rect, bool testOnly) const
    {
        int x = rect.x();
        int y = rect.y();
        int lineHeight = 0;

        const int hSpace = horizontalSpacing();
        const int vSpace = verticalSpacing();

        for (QLayoutItem* item : m_itemList)
        {
            const QSize itemSize = item->sizeHint();
            const int nextX = x + itemSize.width() + hSpace;
            if (nextX - hSpace > rect.right() && lineHeight > 0)
            {
                x = rect.x();
                y += lineHeight + vSpace;
                lineHeight = 0;
            }

            if (!testOnly)
            {
                item->setGeometry(QRect(QPoint(x, y), itemSize));
            }

            x += itemSize.width() + hSpace;
            lineHeight = (std::max)(lineHeight, itemSize.height());
        }

        return y + lineHeight - rect.y();
    }

    [[nodiscard]] int smartSpacing(QStyle::PixelMetric pixelMetric) const
    {
        QObject* parentObj = parent();
        if (!parentObj)
        {
            return -1;
        }
        if (parentObj->isWidgetType())
        {
            auto* parentWidget = static_cast<QWidget*>(parentObj);
            return parentWidget->style()->pixelMetric(pixelMetric, nullptr, parentWidget);
        }

        return static_cast<QLayout*>(parentObj)->spacing();
    }

    QList<QLayoutItem*> m_itemList;
    int m_hSpace = -1;
    int m_vSpace = -1;
};
