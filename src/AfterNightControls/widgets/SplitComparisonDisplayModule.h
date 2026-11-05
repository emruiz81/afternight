// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QImage>
#include <QPoint>
#include <QPointer>
#include <QPixmap>
#include <QSize>
#include <QString>

#include "widgets/ImageViewWidgetDisplayModule.h"

class QMouseEvent;
class QWidget;

namespace an::ui
{

enum class SplitOrientation : unsigned char
{
    Horizontal, // Vertical divider — left shows base, right shows overlay
    Vertical // Horizontal divider — top shows base, bottom shows overlay
};

class SplitViewportOverlay;

class AFTERNIGHTCONTROLS_API SplitComparisonDisplayModule : public ImageViewWidgetDisplayModule
{
public:
    ~SplitComparisonDisplayModule();

    void setOverlayImage(const QImage& image);
    void clearOverlayImage();
    [[nodiscard]] bool hasOverlayImage() const;

    void setOrientation(SplitOrientation orientation);
    [[nodiscard]] SplitOrientation orientation() const;

    void setSplitPosition(double position);
    [[nodiscard]] double splitPosition() const;

    void setSwapped(bool swapped);
    [[nodiscard]] bool isSwapped() const;

    void setLabels(const QString& baseLabel, const QString& overlayLabel);

    /// Install a transparent overlay widget on the given viewport to draw
    /// the splitter line and labels across the full canvas area.
    void installViewportOverlay(QWidget* viewport);
    void removeViewportOverlay();

    void onDraw(QPainter& painter) override;

    /// Paint the splitter line and labels on the viewport overlay.
    /// Called by SplitViewportOverlay::paintEvent().
    void drawViewportOverlay(QPainter& painter) const;

    // Mouse interaction — called by the owning process via event filter.
    // Returns true if the event was consumed (splitter interaction).
    bool handleMousePress(const QPoint& widgetPos);
    bool handleMouseMove(const QPoint& widgetPos);
    bool handleMouseRelease(const QPoint& widgetPos);
    [[nodiscard]] bool isDragging() const;
    [[nodiscard]] bool isNearSplitter(const QPoint& widgetPos) const;
    [[nodiscard]] bool isPointOnOverlaySide(const QPoint& widgetPos) const;

private:
    [[nodiscard]] const QPixmap& overlayPixmapForTarget(const QSize& targetSize) const;
    void requestUpdate();
    [[nodiscard]] int splitterPixelPos() const;
    void drawSplitterLine(QPainter& painter, const QRect& viewportRect, int splitPosVp) const;
    void drawLabels(QPainter& painter, const QRect& viewportRect, int splitPosVp) const;

    QImage m_overlayImage;
    QPixmap m_overlayPixmap;
    SplitOrientation m_orientation = SplitOrientation::Horizontal;
    double m_splitPosition = 0.5;
    bool m_dragging = false;
    bool m_swapped = false;
    QString m_baseLabel;
    QString m_overlayLabel;
    QPointer<SplitViewportOverlay> m_viewportOverlay;
    mutable QPixmap m_scaledOverlayPixmap;
    mutable QSize m_scaledOverlayTargetSize;

    static constexpr int kSplitterHitZone = 6;
    static constexpr int kSplitterLineWidth = 2;
    static constexpr int kHandleRadius = 8;
    static constexpr int kLabelPaddingX = 16;
    static constexpr int kLabelPaddingY = 10;
    static constexpr int kLabelMargin = 15;
};

} // namespace an::ui
