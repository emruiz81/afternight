// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QPushButton>
#include <QString>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVariant>

#include <cstddef>
#include <set>
#include <vector>

namespace process_utils
{

inline void updateNavigationButtons(QPushButton* moveUp, QPushButton* moveDown, int row, std::size_t totalRows)
{
    if (!moveUp || !moveDown)
    {
        return;
    }

    if (row < 0 || totalRows == 0)
    {
        moveUp->setEnabled(false);
        moveDown->setEnabled(false);
        return;
    }

    const int lastIndex = static_cast<int>(totalRows) - 1;
    moveUp->setEnabled(row > 0);
    moveDown->setEnabled(row < lastIndex);
}

inline std::vector<QString> selectedFilesFromTable(QTableWidget* table, int column = 0)
{
    if (!table)
    {
        return {};
    }

    std::set<int> rows;
    const auto items = table->selectedItems();
    for (auto* item : items)
    {
        if (item)
        {
            rows.insert(item->row());
        }
    }

    std::vector<QString> files;
    files.reserve(rows.size());

    for (int row : rows)
    {
        if (auto* cell = table->item(row, column))
        {
            const QVariant storedValue = cell->data(Qt::UserRole);
            files.push_back(storedValue.isValid() ? storedValue.toString() : cell->text());
        }
    }

    return files;
}

inline void swapTableRows(QTableWidget* table, int row1, int row2)
{
    if (!table)
    {
        return;
    }

    if (row1 == row2)
    {
        return;
    }

    const int rowCount = table->rowCount();
    if (row1 < 0 || row2 < 0 || row1 >= rowCount || row2 >= rowCount)
    {
        return;
    }

    const int colCount = table->columnCount();
    for (int col = 0; col < colCount; ++col)
    {
        QTableWidgetItem* item1 = table->takeItem(row1, col);
        QTableWidgetItem* item2 = table->takeItem(row2, col);
        table->setItem(row1, col, item2);
        table->setItem(row2, col, item1);
    }
}

} // namespace process_utils
