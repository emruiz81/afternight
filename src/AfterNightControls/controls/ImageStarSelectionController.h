// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include "ImageViewScrollArea.h"
#include "widgets/ImageViewWidget.h"

#include <optional>

#include "controls/StarFilter.h"

class ImageStarSelectorControl
{
public:
    virtual void onStarSelected(size_t idx) = 0;
};

// Draws a star list over a view - each star's fitted ellipse at its engine pixel coordinate, a saturated one in magenta
// - and reports the star a click lands on.
class AFTERNIGHTCONTROLS_API ImageStarSelectionController : public ImageViewScrollAreaListener,
                                                            public ImageViewWidgetDisplayModule
{
public:
    ImageStarSelectionController(ImageViewScrollArea* parent, ImageStarSelectorControl* control);

    bool onMousePressEvent(QMouseEvent* ev) override;
    void setListenerEnabled(const bool enabled) override;

    void destroy();

    void enableStarSelection(bool enable);

    bool isStarSelectionEnabled() const;

    void updateStarSelection(const an::ui::StarListPtr& starSelection);

    void setHighlightStarSelection(const QPointF& center);

    QPointF getHighlightStarSelection() const;

    void resetHighlightStarSelection();

    void setStarSelectionScaleFactor(float factor);
    void resetStarSelectionScaleFactor();
    float getStarSelectionScaleFactor() const;

    an::ui::StarListPtr getStarSelection() const;

    // The index in the drawn list of the star under a position in the view's widget, or nothing.
    [[nodiscard]] std::optional<size_t> starAt(const QPoint& widgetPosition) const;

    void resetStarSelection();

    void setInputCaptureEnabled(bool enabled);

    bool isInputCaptureEnabled() const;

    virtual ~ImageStarSelectionController();

protected:
private:
    void onDraw(QPainter& painter) override;

    void drawStarSelection(QPainter& painter);

    void drawHighlightStarSelection(QPainter& painter);

    ImageStarSelectorControl* m_Control;

    an::ui::StarListPtr m_StarSelection = std::make_shared<const an::ui::StarList>();

    QPointF m_HighlightStarSelection = QPoint(0, 0);

    float m_StarSelectionScaleFactor = 1.0f;

    bool m_InputCaptureEnabled = true;
};
