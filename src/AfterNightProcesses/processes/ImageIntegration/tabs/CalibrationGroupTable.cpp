// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "CalibrationGroupTable.h"
#include "TabStyling.h"
#include "app/UiStyleTokens.h"

#include <QEvent>
#include <QHeaderView>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QTableWidgetItem>

#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(CalibrationGroupTable)

namespace
{
class CalibrationGroupTableHighlightDelegate final : public QStyledItemDelegate
{
public:
    explicit CalibrationGroupTableHighlightDelegate(CalibrationGroupTable* table)
        : QStyledItemDelegate(table), m_table(table)
    {
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        if (!m_table)
        {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }

        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);

        const bool isSelected = (opt.state & QStyle::State_Selected) != 0;
        if (!isSelected)
        {
            const QModelIndex keyIndex = index.siblingAtColumn(0);
            const QString keyStr = keyIndex.data(Qt::UserRole).toString();
            if (!keyStr.isEmpty() && m_table->isKeyHighlighted(keyStr))
            {
                const QPalette pal = m_table->palette();
                const QColor highlightBgColor =
                    an::ui::styling::semanticColor(pal, an::ui::styling::SemanticColorRole::HighlightBackground);
                const QColor highlightTextColor =
                    an::ui::styling::semanticColor(pal, an::ui::styling::SemanticColorRole::TextOnAccent);

                // Explicitly paint the full cell background. Some styles do not fully respect
                // QStyleOptionViewItem::backgroundBrush for non-selected cells.
                painter->fillRect(opt.rect, highlightBgColor);

                opt.palette.setColor(QPalette::Text, highlightTextColor);
                opt.palette.setColor(QPalette::WindowText, highlightTextColor);
            }
        }

        QStyledItemDelegate::paint(painter, opt, index);
    }

private:
    CalibrationGroupTable* m_table = nullptr;
};
} // namespace

CalibrationGroupTable::CalibrationGroupTable(QWidget* parent) : QTableWidget(parent)
{
    ImageIntegration::Style::applyTableStyle(this);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setItemDelegate(new CalibrationGroupTableHighlightDelegate(this));
}

void CalibrationGroupTable::setHighlightedKeys(const QSet<QString>& keys)
{
    m_highlightedKeys = keys;
    viewport()->update();
}

void CalibrationGroupTable::setEmptyMessage(const QString& message)
{
    clearContents();
    clearSpans();
    if (columnCount() == 0)
    {
        return;
    }

    setRowCount(1);
    setSpan(0, 0, 1, columnCount());

    auto* item = new QTableWidgetItem(message);
    item->setTextAlignment(Qt::AlignCenter);
    item->setForeground(QBrush(palette().color(QPalette::PlaceholderText)));
    setItem(0, 0, item);
}

void CalibrationGroupTable::clearEmptyMessage()
{
    clearContents();
    clearSpans();
    setRowCount(0);
}

bool CalibrationGroupTable::isKeyHighlighted(const QString& key) const
{
    return m_highlightedKeys.contains(key);
}

void CalibrationGroupTable::changeEvent(QEvent* event)
{
    QTableWidget::changeEvent(event);
    if (!event)
    {
        return;
    }

    const auto type = event->type();
    if (type == QEvent::ThemeChange)
    {
        ImageIntegration::Style::applyTableStyle(this);
        viewport()->update();
    }
}
