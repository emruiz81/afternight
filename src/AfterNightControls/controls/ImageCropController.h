// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"
#include "ImageViewScrollArea.h"

enum class MousePositionType : unsigned char
{
    outside_pos = 0,
    inside_pos,
    left_side_pos,
    right_side_pos,
    top_side_pos,
    bottom_side_pos,
    top_left_corner_pos,
    top_right_corner_pos,
    bottom_left_corner_pos,
    bottom_right_corner_pos
};

class AFTERNIGHTCONTROLS_API ImageAreaSelectorControl
{
public:
    // Real time, triggers every mouse move during drag operations
    virtual void selectionAreaUpdated() = 0;

    // Only triggers when mouse is released
    virtual void selectionAreaChanged() = 0;

    virtual void newSelectionFinished() = 0;
};

class AFTERNIGHTCONTROLS_API ImageCropController : public ImageViewScrollAreaListener
{
public:
    ImageCropController(ImageViewScrollArea* parent, ImageAreaSelectorControl* selectorControl);

    bool onMousePressEvent(QMouseEvent* ev) override;
    bool onMouseReleaseEvent(QMouseEvent* ev) override;
    bool onMouseMoveEvent(QMouseEvent* ev) override;

    void setListenerEnabled(const bool enabled) override;

    void startNewSelection();
    void cancelNewSelection();

    void enableOneClickMove(bool enable);
    bool isOneClickMoveEnabled() const;

    void updateSelectionArea(const QRect& area);

    void destroy();

    virtual ~ImageCropController();

protected:
private:
    MousePositionType getMousePositionType(const QPoint& currentMousePos, const QRect& currrentImageArea);

    void changeMouseMode(MousePositionType newMode);

    ImageAreaSelectorControl* m_SelectorControl = nullptr;
    bool m_MouseLeftButtonPressed = false;
    QPoint m_LastMouseDragLocation = QPoint(0, 0);

    MousePositionType m_LastMousePositionType = MousePositionType::outside_pos;

    bool m_EnableOneClickMove = false;
    bool m_EnableNewSelection = false;

    bool m_NewSelectionInProgress = false;
    QPoint m_NewSelectionOriginPoint = QPoint(0, 0);
};
