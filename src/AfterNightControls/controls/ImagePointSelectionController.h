// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include "SelectionControllerBase.h"

class ImagePointSelectorControl
{
public:
    virtual void onPointSelected(const QPoint& point) = 0;
};

class AFTERNIGHTCONTROLS_API ImagePointSelectionController
    : public SelectionControllerBase<ImagePointSelectionController>
{
public:
    ImagePointSelectionController(ImageViewScrollArea* parent, ImagePointSelectorControl* control);

    void enablePointSelection(bool enable);

    bool isPointSelectionEnabled() const;

    void updatePointSelection(const QPoint& selection);

    QPoint getPointSelection() const;

    void resetPointSelection();

    bool onMousePressEvent(QMouseEvent* ev) override;
    void onSelectionEnabledChanged(bool enabled);

    void setDrawCoords(bool enable);
    bool getDrawCoord() const;

    virtual ~ImagePointSelectionController() = default;

protected:
private:
    void onDraw(QPainter& painter) override;

    void drawPointSelection(QPainter& painter);

    ImagePointSelectorControl* m_Control;

    QPoint m_PointSelection = QPoint(0, 0);

    bool m_DrawCoords = false;
};
