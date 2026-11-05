// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QtWidgets/QTableWidget>

class TableWidget;

class TableWidgetListener
{
public:
    virtual void onRowSelected(TableWidget* sender, int row) {};
    virtual void onColumnClicked(TableWidget* sender, int column) {};
    virtual void onRowDoubleClicked(TableWidget* sender, int row) {};
    virtual void onContextMenuRequested(TableWidget* sender, int row, int col, const QPoint& globalPos) {};
};

class AFTERNIGHTCONTROLS_API TableWidget : public QTableWidget
{
public:
    explicit TableWidget(QWidget* parent = nullptr);
    TableWidget(int rows, int columns, QWidget* parent = nullptr);

    void setEditable(bool enabled);

    void selectRow(int row, bool scrollIfNeeded = true);

    void setRowTextStyle(int row, bool bold, bool italic);

    void highlightRow(int row, const QColor& color);
    void clearHighlightRow(int row);

    // fullupdate: will force the internal cached labels to be updated as well, so after calling updateHeaders() will
    // keep this change IF disabled, the next call to updateHeaders() will overwrite this change
    void setColumnTitle(const int col, const QString& title, const bool fullupdate = true);
    void setRowTitle(const int row, const QString& title /*, const bool fullupdate = true*/);

    void setRowMarked(const int row, bool marked, const QString& marker = "", const QString& tooltip = "");

    void setListener(TableWidgetListener* listener);

    TableWidgetListener* getListener() const;

    void setHeaderLabels(const std::vector<QString>& headers);

    std::vector<QString> getHeaderLabels() const;

    QString getHeaderLabel(const int col) const;

    void updateHeaders();

    // TODO: Make this more customizable
    void setupSelectionColors();

    ~TableWidget();

protected:
private:
    void paintEvent(QPaintEvent* event) override;

    void initialize();

    void onCellClicked(int row, int column);

    void onCellDoubleClicked(int row, int column);

    void onTableContextMenu(const QPoint& pos);

    void onItemSelectionChanged();

    void onColumnClicked(int column);

    TableWidgetListener* m_Listener = nullptr;

    std::vector<QString> m_table_headers;

    bool m_FirstUpdate = true;
};
