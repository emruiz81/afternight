// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "StructuredElementWidget.h"
#include "theme/AnDesignTokens.h"

StructuredElementWidget::StructuredElementWidget(QWidget* parent) : QWidget(parent)
{
    this->setAttribute(Qt::WA_DeleteOnClose);
}

void StructuredElementWidget::setStructuringElement(const an::ui::StructuringElementGrid& element)
{
    m_StructuringElement = element;

    update();
}

const an::ui::StructuringElementGrid& StructuredElementWidget::structuringElement() const
{
    return m_StructuringElement;
}

void StructuredElementWidget::setGridWidth(const float w)
{
    m_GridWidth = w;
}

float StructuredElementWidget::getGridWidth() const
{
    return m_GridWidth;
}

void StructuredElementWidget::setListener(StructuredElementWidgetListener* listener)
{
    m_Listener = listener;
}

StructuredElementWidgetListener* StructuredElementWidget::getListener() const
{
    return m_Listener;
}

StructuredElementWidget::~StructuredElementWidget() {}

void StructuredElementWidget::mousePressEvent(QMouseEvent* ev)
{
    if (ev->button() == Qt::LeftButton)
    {
        ev->accept();

        size_t row = 0;
        size_t col = 0;
        if (!getSelectedItem(ev->pos(), row, col))
        {
            return;
        }

        m_StructuringElement.toggle(row, col);

        if (m_Listener)
        {
            m_Listener->onGridUpdated();
        }

        update();
    }
}

void StructuredElementWidget::paintEvent(QPaintEvent* evt)
{
    evt->accept();

    QPainter painter(this);

    painter.setRenderHint(QPainter::Antialiasing, true);

    drawBackground(painter);

    drawMorphStructElement(painter);

    drawGrid(painter);
}

void StructuredElementWidget::resizeEvent(QResizeEvent* evt)
{
    QWidget::resizeEvent(evt);
}

void StructuredElementWidget::changeEvent(QEvent* ev)
{
    if (ev->type() == QEvent::ApplicationPaletteChange || ev->type() == QEvent::ThemeChange)
        update();
    QWidget::changeEvent(ev);
}

QColor StructuredElementWidget::getGridColor() const
{
    return an::ui::tokens().colors.borderPrimary;
}

void StructuredElementWidget::drawBackground(QPainter& painter)
{
    painter.fillRect(this->rect(), an::ui::tokens().colors.bgPrimary);
}

void StructuredElementWidget::drawGrid(QPainter& painter)
{
    if (m_StructuringElement.size() == 0)
        return;

    QPen pen;
    pen.setStyle(Qt::SolidLine);
    pen.setColor(an::ui::tokens().colors.borderPrimary);
    pen.setWidthF(m_GridWidth);
    pen.setCosmetic(true);

    painter.setPen(pen);

    int gridSize = (int)(width() / m_StructuringElement.size());

    int rest = width() % m_StructuringElement.size();

    int x = 0;
    int y = 0;

    size_t currentIdx = 0;

    // Draw vertical lines
    while (x < width())
    {
        painter.drawLine(x, 0, x, height());

        x += gridSize;
        if (currentIdx++ < rest)
        {
            x += 1;
        }
    }

    currentIdx = 0;

    // Draw horizontal lines
    while (y < height())
    {
        painter.drawLine(0, y, width(), y);

        y += gridSize;
        if (currentIdx++ < rest)
        {
            y += 1;
        }
    }
}

void StructuredElementWidget::drawMorphStructElement(QPainter& painter)
{
    if (m_StructuringElement.size() == 0)
        return;

    /*QPen pen;

    pen.setStyle(Qt::SolidLine);
    pen.setWidthF(1);*/

    int gridSize = (int)(width() / m_StructuringElement.size());

    int rest = width() % m_StructuringElement.size();

    size_t size = m_StructuringElement.size();

    int ypos = 0;

    for (size_t y = 0; y < size; ++y)
    {
        int ysize = gridSize;
        if (y < rest)
        {
            ysize++;
        }

        int xpos = 0;
        for (size_t x = 0; x < size; ++x)
        {
            int xsize = gridSize;
            if (x < rest)
            {
                xsize++;
            }

            QRect r(xpos, ypos, xsize, ysize);
            // Inset by 1px so the background shows through as grid lines,
            // guaranteeing contrast regardless of cell or grid-line color.
            r.adjust(0, 0, -1, -1);

            if (m_StructuringElement.at(y, x))
            {
                painter.fillRect(r, an::ui::tokens().colors.textPrimary);
            }

            xpos += xsize;
        }

        ypos += ysize;
    }
}

bool StructuredElementWidget::getSelectedItem(const QPoint& pos, size_t& row, size_t& col) const
{
    if (m_StructuringElement.size() == 0)
        return false;

    int gridSize = (int)(width() / m_StructuringElement.size());

    int rest = width() % m_StructuringElement.size();

    int currentElementX = 0;
    int currentElementY = 0;

    int currentOffsetX = 0;
    int currentOffsetY = 0;

    for (size_t i = 0; i < m_StructuringElement.size(); ++i)
    {
        currentOffsetX += gridSize;
        if (i < rest)
        {
            currentOffsetX++;
        }

        if (pos.x() < currentOffsetX)
        {
            break;
        }

        currentElementX++;
    }

    for (size_t i = 0; i < m_StructuringElement.size(); ++i)
    {
        currentOffsetY += gridSize;
        if (i < rest)
        {
            currentOffsetY++;
        }

        if (pos.y() < currentOffsetY)
        {
            break;
        }

        currentElementY++;
    }

    row = currentElementY;
    col = currentElementX;
    return row < m_StructuringElement.size() && col < m_StructuringElement.size();
}
