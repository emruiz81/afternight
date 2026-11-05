// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "TableWidget.h"

#include "app/ModernizationFeatureFlags.h"
#include "theme/AnDesignTokens.h"

namespace
{
constexpr int kSavedHighlightBackgroundRole = Qt::UserRole + 0x5200;
constexpr int kSavedHighlightForegroundRole = Qt::UserRole + 0x5201;

QColor resolvedHighlightTextColor(const QPalette& palette, const QColor& rowColor)
{
    const QColor baseColor =
        palette.color(QPalette::Base).isValid() ? palette.color(QPalette::Base) : palette.color(QPalette::Window);
    const QColor effectiveRowColor = an::ui::compositeOver(rowColor, baseColor);
    return an::ui::bestContrastTextColor(effectiveRowColor, an::ui::tokens().colors.textPrimary, QColor(255, 255, 255),
                                         QColor(0, 0, 0), 4.5);
}
} // namespace

TableWidget::TableWidget(QWidget* parent) : QTableWidget(parent)
{
    initialize();
}

TableWidget::TableWidget(int rows, int columns, QWidget* parent) : QTableWidget(rows, columns, parent)
{
    initialize();
}

void TableWidget::setEditable(bool enabled)
{
    setEditTriggers(enabled ? QAbstractItemView::AllEditTriggers : QAbstractItemView::NoEditTriggers);
}

void TableWidget::selectRow(int row, bool scrollIfNeeded)
{
    clearSelection();
    setCurrentCell(row, 0);

    if (scrollIfNeeded)
    {
        auto itm = item(row, 0);
        scrollToItem(itm);
    }

    setFocus();
}

static void setItemStyle(QTableWidgetItem* item, bool bold, bool italic)
{
    if (!item)
        return;

    QFont font = item->font();
    font.setBold(bold);
    font.setItalic(italic);
    // font.setUnderline(true);
    // font.setFamily("Courier New");  // or another distinct font

    item->setFont(font);
}

void TableWidget::setRowTextStyle(int row, bool bold, bool italic)
{
    for (int col = 0; col < columnCount(); ++col)
    {
        QTableWidgetItem* it = item(row, col);
        if (!it)
        {
            it = new QTableWidgetItem();
            setItem(row, col, it);
        }

        setItemStyle(it, bold, italic);
    }
}

void TableWidget::highlightRow(int row, const QColor& color)
{
    const QColor textColor = resolvedHighlightTextColor(palette(), color);

    for (int col = 0; col < columnCount(); ++col)
    {
        QTableWidgetItem* it = item(row, col);
        if (!it)
        {
            it = new QTableWidgetItem();
            setItem(row, col, it);
        }

        if (!it->data(kSavedHighlightBackgroundRole).isValid())
        {
            it->setData(kSavedHighlightBackgroundRole, it->background());
        }
        if (!it->data(kSavedHighlightForegroundRole).isValid())
        {
            it->setData(kSavedHighlightForegroundRole, it->foreground());
        }

        it->setBackground(color);
        it->setForeground(QBrush(textColor));
    }
}

void TableWidget::clearHighlightRow(int row)
{
    for (int col = 0; col < columnCount(); ++col)
    {
        QTableWidgetItem* it = item(row, col);
        if (it)
        {
            const QVariant savedBackground = it->data(kSavedHighlightBackgroundRole);
            const QVariant savedForeground = it->data(kSavedHighlightForegroundRole);

            if (savedBackground.isValid())
            {
                it->setBackground(savedBackground.value<QBrush>());
            }
            else
            {
                it->setBackground(Qt::NoBrush);
            }

            if (savedForeground.isValid())
            {
                it->setForeground(savedForeground.value<QBrush>());
            }
            else
            {
                it->setForeground(QBrush());
            }

            it->setData(kSavedHighlightBackgroundRole, QVariant());
            it->setData(kSavedHighlightForegroundRole, QVariant());
        }
    }
}

void TableWidget::setColumnTitle(const int col, const QString& title, const bool fullupdate)
{
    auto it = horizontalHeaderItem(col);
    it->setText(title);
    // it->setSelected(true);

    if (fullupdate)
    {
        m_table_headers[col] = title;
    }
}

void TableWidget::setRowTitle(const int row, const QString& title /* ,const bool fullupdate */)
{
    auto it = verticalHeaderItem(row);
    it->setText(title);
}

void TableWidget::setRowMarked(const int row, bool marked, const QString& marker, const QString& tooltip)
{
    QTableWidgetItem* headerItem = verticalHeaderItem(row);

    if (!headerItem)
    {
        // If no item exists yet, create one with the default row number
        headerItem = new QTableWidgetItem(QString::number(row + 1));
        setVerticalHeaderItem(row, headerItem);
    }

    if (!marked)
    {
        setRowTextStyle(row, false, false);
        headerItem->setText(QString::number(row + 1));
        headerItem->setToolTip("");
    }
    else
    {
        setRowTextStyle(row, true, false);
        headerItem->setText(QString::number(row + 1) + marker); // QString("   ⇒"));  // still show the number
        headerItem->setToolTip(tooltip);
    }
}

void TableWidget::setListener(TableWidgetListener* listener)
{
    m_Listener = listener;
}

TableWidgetListener* TableWidget::getListener() const
{
    return m_Listener;
}

void TableWidget::setHeaderLabels(const std::vector<QString>& headers)
{
    m_table_headers = headers;
    updateHeaders();
}

std::vector<QString> TableWidget::getHeaderLabels() const
{
    return m_table_headers;
}

QString TableWidget::getHeaderLabel(const int col) const
{
    return m_table_headers.at(col);
}

void TableWidget::updateHeaders()
{
    setHorizontalHeaderLabels(QStringList(m_table_headers.begin(), m_table_headers.end()));
}

void TableWidget::setupSelectionColors()
{
    // Proxy style handles table selection colors via QPalette.
}

TableWidget::~TableWidget() {}

void TableWidget::paintEvent(QPaintEvent* event)
{
    QTableWidget::paintEvent(event);
    if (m_FirstUpdate)
    {
        horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
        m_FirstUpdate = false;
    }
}

void TableWidget::initialize()
{
    setSelectionMode(QAbstractItemView::SingleSelection);
    setSelectionBehavior(SelectionBehavior::SelectRows);

    connect(this, &TableWidget::cellClicked, this, &TableWidget::onCellClicked);
    connect(this, &TableWidget::cellDoubleClicked, this, &TableWidget::onCellDoubleClicked);
    connect(this, &TableWidget::itemSelectionChanged, this, &TableWidget::onItemSelectionChanged);

    setContextMenuPolicy(Qt::CustomContextMenu);

    connect(this, &QTableWidget::customContextMenuRequested, this, &TableWidget::onTableContextMenu);

    auto header = horizontalHeader();
    connect(header, &QHeaderView::sectionClicked, this, &TableWidget::onColumnClicked);

    horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    // By default is not editable
    setEditTriggers(QAbstractItemView::NoEditTriggers);
}

void TableWidget::onCellClicked(int row, int column)
{
    selectRow(row);

    if (m_Listener)
    {
        m_Listener->onRowSelected(this, row);
    }
}

void TableWidget::onCellDoubleClicked(int row, int column)
{
    selectRow(row);

    if (m_Listener)
    {
        m_Listener->onRowSelected(this, row);
        m_Listener->onRowDoubleClicked(this, row);
    }
}

void TableWidget::onTableContextMenu(const QPoint& pos)
{
    if (m_Listener)
    {
        QModelIndex index = this->indexAt(pos);
        if (!index.isValid())
            return;

        int row = index.row();
        int col = index.column();

        auto globalPos = this->viewport()->mapToGlobal(pos);
        m_Listener->onContextMenuRequested(this, row, col, globalPos);

        //// Show a custom context menu
        // QMenu contextMenu;
        // contextMenu.addAction("Option 1");
        // contextMenu.addAction("Option 2");
        // contextMenu.exec(this->viewport()->mapToGlobal(pos));
    }
}

void TableWidget::onItemSelectionChanged()
{
    auto idx = selectionModel()->currentIndex();

    if (m_Listener)
    {
        m_Listener->onRowSelected(this, idx.row());
    }
}

void TableWidget::onColumnClicked(int column)
{
    if (m_Listener)
    {
        m_Listener->onColumnClicked(this, column);
    }
}
