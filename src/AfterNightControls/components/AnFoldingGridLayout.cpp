// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "components/AnFoldingGridLayout.h"

#include <QByteArray>
#include <QLayoutItem>
#include <QMargins>
#include <QWidget>

#include <algorithm>
#include <map>

namespace an::ui
{

AnFoldingGridLayout::AnFoldingGridLayout(const int foldColumn, QWidget* parent)
    : QGridLayout(parent), m_foldColumn((std::max)(1, foldColumn))
{
}

AnFoldingGridLayout::~AnFoldingGridLayout() = default;

int AnFoldingGridLayout::foldColumn() const
{
    return m_foldColumn;
}

bool AnFoldingGridLayout::folded() const
{
    return m_folded;
}

std::vector<AnFoldingGridLayout::Cell> AnFoldingGridLayout::cellsNow() const
{
    std::vector<Cell> cells;
    cells.reserve(static_cast<std::size_t>(count()));
    for (int index = 0; index < count(); ++index)
    {
        int row = 0;
        int column = 0;
        int rowSpan = 1;
        int columnSpan = 1;
        getItemPosition(index, &row, &column, &rowSpan, &columnSpan);
        cells.push_back(Cell{.item = itemAt(index), .row = row, .column = column, .columnSpan = columnSpan});
    }
    return cells;
}

int AnFoldingGridLayout::foldedWidth() const
{
    // The folded grid's columns as a grid sums them: each as wide as the widest cell standing in it alone, a cell over
    // several columns widening the last of them by what they lack, and the spacing between the columns that hold any.
    const std::vector<Cell> cells = m_folded ? m_built : cellsNow();
    const int gap = (std::max)(0, horizontalSpacing());
    std::map<int, int> widths;
    for (const Cell& cell : cells)
    {
        if (cell.item != nullptr && !cell.item->isEmpty() && cell.columnSpan == 1)
        {
            const int column = cell.column >= m_foldColumn ? cell.column - m_foldColumn : cell.column;
            widths[column] = (std::max)(widths[column], cell.item->minimumSize().width());
        }
    }
    for (const Cell& cell : cells)
    {
        if (cell.item == nullptr || cell.item->isEmpty() || cell.columnSpan == 1)
        {
            continue;
        }
        const int first = cell.column >= m_foldColumn ? cell.column - m_foldColumn : cell.column;
        const int last = first + cell.columnSpan - 1;
        int spanned = gap * (cell.columnSpan - 1);
        for (int column = first; column <= last; ++column)
        {
            spanned += widths[column];
        }
        widths[last] += (std::max)(0, cell.item->minimumSize().width() - spanned);
    }
    int width = 0;
    int held = 0;
    for (const auto& [column, wide] : widths)
    {
        if (wide > 0)
        {
            width += wide;
            ++held;
        }
    }
    const QMargins margins = contentsMargins();
    return margins.left() + margins.right() + width + gap * (std::max)(0, held - 1);
}

QSize AnFoldingGridLayout::minimumSize() const
{
    // Folded, the grid needs what it states; as built, it states the least of the folded grid, so that a column
    // narrower than it gives it the width it has.
    const QSize least = QGridLayout::minimumSize();
    if (m_folded)
    {
        return least;
    }
    return {(std::min)(least.width(), foldedWidth()), least.height()};
}

void AnFoldingGridLayout::setGeometry(const QRect& rect)
{
    if (rect.width() > 0)
    {
        // The width the grid needs as built is measured while it stands as built, and kept while it is folded.
        if (!m_folded)
        {
            m_builtWidth = QGridLayout::minimumSize().width();
        }
        const bool fold = rect.width() < m_builtWidth;
        // Placed again only where it changes: that asks the parent to lay out again, which comes back here at the same
        // width and leaves the cells where they stand.
        if (fold != m_folded)
        {
            place(fold);
        }
    }
    QGridLayout::setGeometry(rect);
}

void AnFoldingGridLayout::place(const bool fold)
{
    if (!m_folded)
    {
        m_built = cellsNow();
    }
    // Out of the grid and back in, each where the arrangement puts it; the items stay as they are.
    while (count() > 0)
    {
        (void)takeAt(0);
    }
    for (const Cell& cell : m_built)
    {
        const bool trailing = fold && cell.column >= m_foldColumn;
        const int row = fold ? 2 * cell.row + (trailing ? 1 : 0) : cell.row;
        const int column = trailing ? cell.column - m_foldColumn : cell.column;
        if (QLayout* const layout = cell.item->layout(); layout != nullptr)
        {
            addLayout(layout, row, column, 1, cell.columnSpan, layout->alignment());
        }
        else
        {
            addItem(cell.item, row, column, 1, cell.columnSpan, cell.item->alignment());
        }
    }
    m_folded = fold;
}

AnFoldingGridLayout* foldWhereItDoesNotFit(QGridLayout& grid, const int foldColumn)
{
    auto* const folding = new AnFoldingGridLayout(foldColumn);
    if (!grid.objectName().isEmpty())
    {
        folding->setObjectName(grid.objectName() + QStringLiteral("Folding"));
    }
    for (const QByteArray& name : grid.dynamicPropertyNames())
    {
        folding->setProperty(name.constData(), grid.property(name.constData()));
    }
    folding->setContentsMargins(grid.contentsMargins());
    if (grid.horizontalSpacing() >= 0)
    {
        folding->setHorizontalSpacing(grid.horizontalSpacing());
    }
    if (grid.verticalSpacing() >= 0)
    {
        folding->setVerticalSpacing(grid.verticalSpacing());
    }
    for (int column = 0; column < grid.columnCount(); ++column)
    {
        folding->setColumnStretch(column, grid.columnStretch(column));
        folding->setColumnMinimumWidth(column, grid.columnMinimumWidth(column));
    }

    struct Taken
    {
        QLayoutItem* item = nullptr;
        int row = 0;
        int column = 0;
        int columnSpan = 1;
    };
    std::vector<Taken> taken;
    for (int index = 0; index < grid.count(); ++index)
    {
        int row = 0;
        int column = 0;
        int rowSpan = 1;
        int columnSpan = 1;
        grid.getItemPosition(index, &row, &column, &rowSpan, &columnSpan);
        taken.push_back(Taken{.item = grid.itemAt(index), .row = row, .column = column, .columnSpan = columnSpan});
    }
    while (grid.count() > 0)
    {
        (void)grid.takeAt(0);
    }
    grid.setContentsMargins(0, 0, 0, 0);
    // Placed in the grid first, so that the widgets it takes keep the widget they lie in.
    grid.addLayout(folding, 0, 0);
    for (const Taken& cell : taken)
    {
        if (QLayout* const layout = cell.item->layout(); layout != nullptr)
        {
            folding->addLayout(layout, cell.row, cell.column, 1, cell.columnSpan, layout->alignment());
        }
        else
        {
            folding->addItem(cell.item, cell.row, cell.column, 1, cell.columnSpan, cell.item->alignment());
        }
    }
    return folding;
}

} // namespace an::ui
