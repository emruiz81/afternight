// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"
#include "widgets/StructuringElementGrid.h"

#include <QtWidgets/QMainWindow>
#include <QtWidgets/QWidget>

#include <cstddef>

class StructuredElementWidgetListener
{
public:
    virtual void onGridUpdated() = 0;
};

// Shows and edits a structuring element's grid: a click turns a cell over. The grid is the widget's; its owner sets it,
// reads it back and is told when a click changed it.
class AFTERNIGHTCONTROLS_API StructuredElementWidget : public QWidget
{
public:
    StructuredElementWidget(QWidget* parent);

    void setStructuringElement(const an::ui::StructuringElementGrid& element);
    [[nodiscard]] const an::ui::StructuringElementGrid& structuringElement() const;

    void setGridWidth(const float w);
    float getGridWidth() const;

    [[nodiscard]] QColor getGridColor() const;

    void setListener(StructuredElementWidgetListener* listener);

    StructuredElementWidgetListener* getListener() const;

    ~StructuredElementWidget();

protected:
    void mousePressEvent(QMouseEvent* ev) override;
    void paintEvent(QPaintEvent* evt) override;
    void resizeEvent(QResizeEvent* evt) override;
    void changeEvent(QEvent* ev) override;

    void drawBackground(QPainter& painter);
    void drawGrid(QPainter& painter);
    void drawMorphStructElement(QPainter& painter);

    // The cell under a position, or false when there is no grid.
    bool getSelectedItem(const QPoint& pos, size_t& row, size_t& col) const;

    an::ui::StructuringElementGrid m_StructuringElement;

    float m_GridWidth = 1.0f;

    StructuredElementWidgetListener* m_Listener = nullptr;

private:
};
