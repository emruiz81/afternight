// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QTableWidget>
#include <QTableWidgetItem>

namespace process_utils
{

class TableWidgetHelper
{
public:
    static void resizeColumns(QTableWidget* tableWidget, int extraMargin = 12)
    {
        if (!tableWidget)
        {
            return;
        }

        const bool wasUpdatesEnabled = tableWidget->updatesEnabled();
        tableWidget->setUpdatesEnabled(false);

        const int columnCount = tableWidget->columnCount();
        for (int i = 0; i < columnCount; ++i)
        {
            if (!tableWidget->isColumnHidden(i))
            {
                tableWidget->resizeColumnToContents(i);
                tableWidget->setColumnWidth(i, tableWidget->columnWidth(i) + extraMargin);
            }
        }

        tableWidget->setUpdatesEnabled(wasUpdatesEnabled);
    }

    static QTableWidgetItem* createReadOnlyItem(const QString& text, const QString& toolTip = {})
    {
        auto* item = new QTableWidgetItem(text);
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        if (!toolTip.isEmpty())
        {
            item->setToolTip(toolTip);
        }
        return item;
    }
};

} // namespace process_utils
