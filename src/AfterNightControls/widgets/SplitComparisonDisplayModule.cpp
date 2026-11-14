// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "widgets/SplitComparisonDisplayModule.h"

#include <algorithm>

#include <QEvent>
#include <QPainter>
#include <QPen>
#include <QScrollArea>
#include <QScrollBar>
#include <QWidget>

#include "widgets/ImageViewWidget.h"
#include "theme/AnDesignTokens.h"
#include "theme/NightImage.h"
#include "app/UiStyleTokens.h"
#include "controls/ImageViewThemeColors.h"

namespace an::ui
{

// ---------------------------------------------------------------------------
// SplitViewportOverlay — transparent widget parented to the scroll-area
// viewport.  It covers the entire viewport and paints the splitter line and
// image labels.  Mouse events pass through to the widget underneath.
// ---------------------------------------------------------------------------
class SplitViewportOverlay : public QWidget
{
public:
    explicit SplitViewportOverlay(SplitComparisonDisplayModule* module, QWidget* viewport)
        : QWidget(viewport), m_module(module)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_TranslucentBackground);
        setGeometry(viewport->rect());
        viewport->installEventFilter(this);
        show();
        raise();
    }

    ~SplitViewportOverlay() override
    {
        if (parent())
        {
            parent()->removeEventFilter(this);
        }
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::Resize)
        {
            setGeometry(static_cast<QWidget*>(watched)->rect());
        }
        return false;
    }

    void paintEvent(QPaintEvent* /*ev*/) override
    {
        if (!m_module || !m_module->isDisplayModuleEnabled() || !m_module->hasOverlayImage())
        {
            return;
        }

        QPainter painter(this);
        m_module->drawViewportOverlay(painter);
    }

private:
    SplitComparisonDisplayModule* m_module = nullptr;
};

// ---------------------------------------------------------------------------
// SplitComparisonDisplayModule implementation
// ---------------------------------------------------------------------------

SplitComparisonDisplayModule::~SplitComparisonDisplayModule()
{
    removeViewportOverlay();
}

void SplitComparisonDisplayModule::setOverlayImage(const QImage& image)
{
    m_overlayImage = image;
    m_overlayPixmap = QPixmap::fromImage(image);
    m_scaledOverlayPixmap = QPixmap();
    m_scaledOverlayTargetSize = QSize();
    requestUpdate();
}

void SplitComparisonDisplayModule::clearOverlayImage()
{
    if (m_overlayImage.isNull())
    {
        return;
    }

    m_overlayImage = QImage();
    m_overlayPixmap = QPixmap();
    m_scaledOverlayPixmap = QPixmap();
    m_scaledOverlayTargetSize = QSize();
    requestUpdate();
}

bool SplitComparisonDisplayModule::hasOverlayImage() const
{
    return !m_overlayImage.isNull();
}

void SplitComparisonDisplayModule::setOrientation(SplitOrientation orientation)
{
    if (m_orientation == orientation)
    {
        return;
    }

    m_orientation = orientation;
    requestUpdate();
}

SplitOrientation SplitComparisonDisplayModule::orientation() const
{
    return m_orientation;
}

void SplitComparisonDisplayModule::setSplitPosition(double position)
{
    position = std::clamp(position, 0.0, 1.0);
    if (m_splitPosition == position)
    {
        return;
    }

    m_splitPosition = position;
    requestUpdate();
}

double SplitComparisonDisplayModule::splitPosition() const
{
    return m_splitPosition;
}

void SplitComparisonDisplayModule::setSwapped(bool swapped)
{
    if (m_swapped == swapped)
    {
        return;
    }

    m_swapped = swapped;
    requestUpdate();
}

bool SplitComparisonDisplayModule::isSwapped() const
{
    return m_swapped;
}

void SplitComparisonDisplayModule::setLabels(const QString& baseLabel, const QString& overlayLabel)
{
    m_baseLabel = baseLabel;
    m_overlayLabel = overlayLabel;
    requestUpdate();
}

void SplitComparisonDisplayModule::installViewportOverlay(QWidget* viewport)
{
    removeViewportOverlay();
    if (!viewport)
    {
        return;
    }

    m_viewportOverlay = new SplitViewportOverlay(this, viewport);
}

void SplitComparisonDisplayModule::removeViewportOverlay()
{
    if (m_viewportOverlay)
    {
        delete m_viewportOverlay;
    }
}

// ---------------------------------------------------------------------------
// onDraw — paints the overlay image clipped to one half of the widget.
// This runs inside ImageViewWidget::paintEvent() and is clipped to the
// widget boundaries.  Splitter line / labels are handled by the viewport
// overlay instead.
// ---------------------------------------------------------------------------
void SplitComparisonDisplayModule::onDraw(QPainter& painter)
{
    if (!hasOverlayImage() || !m_ViewWidget)
    {
        return;
    }

    const QRect targetRect = m_ViewWidget->getCurrentScaledImageArea();
    if (!targetRect.isValid() || targetRect.isEmpty())
    {
        return;
    }

    QRect clipRect;

    if (m_orientation == SplitOrientation::Horizontal)
    {
        const int splitX = targetRect.left() + static_cast<int>(targetRect.width() * m_splitPosition);

        if (!m_swapped)
        {
            clipRect = QRect(splitX, targetRect.top(), targetRect.right() - splitX + 1, targetRect.height());
        }
        else
        {
            clipRect = QRect(targetRect.left(), targetRect.top(), splitX - targetRect.left() + 1, targetRect.height());
        }
    }
    else
    {
        const int splitY = targetRect.top() + static_cast<int>(targetRect.height() * m_splitPosition);

        if (!m_swapped)
        {
            clipRect = QRect(targetRect.left(), splitY, targetRect.width(), targetRect.bottom() - splitY + 1);
        }
        else
        {
            clipRect = QRect(targetRect.left(), targetRect.top(), targetRect.width(), splitY - targetRect.top() + 1);
        }
    }

    painter.save();
    painter.setClipRect(clipRect);

    if (an::ui::isNightMode())
    {
        // Under Night the overlay's Night form, made off the GUI thread: nothing until it is, and the view painted
        // again then.
        ImageViewWidget* const view = m_ViewWidget;
        const QImage overlay = an::ui::nightImage(m_overlayImage, view, [view]() { view->update(); });
        if (!overlay.isNull())
        {
            painter.drawImage(targetRect, overlay);
        }
        painter.restore();
        return;
    }

    const QPixmap& overlayPixmap = overlayPixmapForTarget(targetRect.size());
    if (!overlayPixmap.isNull() && overlayPixmap.size() == targetRect.size())
    {
        painter.drawPixmap(targetRect.topLeft(), overlayPixmap);
    }
    else
    {
        painter.drawPixmap(targetRect, m_overlayPixmap);
    }

    painter.restore();
}

// ---------------------------------------------------------------------------
// drawViewportOverlay — called by SplitViewportOverlay::paintEvent().
// Coordinates are in viewport space.
// ---------------------------------------------------------------------------
void SplitComparisonDisplayModule::drawViewportOverlay(QPainter& painter) const
{
    if (!m_ViewWidget)
    {
        return;
    }

    const QRect vpRect(0, 0, painter.device()->width(), painter.device()->height());

    // Map the split position from widget coordinates to viewport coordinates.
    // m_ViewWidget->pos() gives the widget position relative to the viewport
    // (its direct parent).
    const QPoint widgetPos = m_ViewWidget->pos();
    const int widgetW = m_ViewWidget->width();
    const int widgetH = m_ViewWidget->height();

    int splitPosVp;
    if (m_orientation == SplitOrientation::Horizontal)
    {
        splitPosVp = widgetPos.x() + static_cast<int>(widgetW * m_splitPosition);
    }
    else
    {
        splitPosVp = widgetPos.y() + static_cast<int>(widgetH * m_splitPosition);
    }

    drawSplitterLine(painter, vpRect, splitPosVp);
    drawLabels(painter, vpRect, splitPosVp);
}

bool SplitComparisonDisplayModule::handleMousePress(const QPoint& widgetPos)
{
    if (!m_ViewWidget || !hasOverlayImage())
    {
        return false;
    }

    if (isNearSplitter(widgetPos))
    {
        m_dragging = true;
        return true;
    }

    return false;
}

bool SplitComparisonDisplayModule::handleMouseMove(const QPoint& widgetPos)
{
    if (!m_dragging || !m_ViewWidget)
    {
        return false;
    }

    const QRect area = m_ViewWidget->getCurrentScaledImageArea();
    if (!area.isValid() || area.isEmpty())
    {
        return false;
    }

    double newPos;
    if (m_orientation == SplitOrientation::Horizontal)
    {
        newPos = static_cast<double>(widgetPos.x() - area.left()) / area.width();
    }
    else
    {
        newPos = static_cast<double>(widgetPos.y() - area.top()) / area.height();
    }

    setSplitPosition(newPos);
    return true;
}

bool SplitComparisonDisplayModule::handleMouseRelease(const QPoint& /*widgetPos*/)
{
    if (!m_dragging)
    {
        return false;
    }

    m_dragging = false;
    return true;
}

bool SplitComparisonDisplayModule::isDragging() const
{
    return m_dragging;
}

bool SplitComparisonDisplayModule::isNearSplitter(const QPoint& widgetPos) const
{
    if (!m_ViewWidget)
    {
        return false;
    }

    const QRect area = m_ViewWidget->getCurrentScaledImageArea();
    if (!area.isValid() || area.isEmpty())
    {
        return false;
    }

    const int pos = splitterPixelPos();

    if (m_orientation == SplitOrientation::Horizontal)
    {
        return std::abs(widgetPos.x() - pos) <= kSplitterHitZone;
    }
    else
    {
        return std::abs(widgetPos.y() - pos) <= kSplitterHitZone;
    }
}

bool SplitComparisonDisplayModule::isPointOnOverlaySide(const QPoint& widgetPos) const
{
    if (!m_ViewWidget)
    {
        return false;
    }

    const int pos = splitterPixelPos();

    bool onSecondHalf;
    if (m_orientation == SplitOrientation::Horizontal)
    {
        onSecondHalf = widgetPos.x() > pos;
    }
    else
    {
        onSecondHalf = widgetPos.y() > pos;
    }

    // Without swap: second half = overlay. With swap: first half = overlay.
    return m_swapped ? !onSecondHalf : onSecondHalf;
}

void SplitComparisonDisplayModule::requestUpdate()
{
    if (m_ViewWidget)
    {
        m_ViewWidget->update();
    }

    if (m_viewportOverlay)
    {
        m_viewportOverlay->update();
    }
}

const QPixmap& SplitComparisonDisplayModule::overlayPixmapForTarget(const QSize& targetSize) const
{
    if (!m_ViewWidget || m_overlayPixmap.isNull())
    {
        return m_overlayPixmap;
    }

    if (m_ViewWidget->getCurrentScaleFactor() >= 1.0 || !targetSize.isValid() || targetSize.isEmpty() ||
        targetSize == m_overlayPixmap.size())
    {
        return m_overlayPixmap;
    }

    if (m_scaledOverlayPixmap.isNull() || m_scaledOverlayTargetSize != targetSize)
    {
        m_scaledOverlayPixmap = m_overlayPixmap.scaled(targetSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        m_scaledOverlayTargetSize = targetSize;
    }

    return m_scaledOverlayPixmap;
}

int SplitComparisonDisplayModule::splitterPixelPos() const
{
    if (!m_ViewWidget)
    {
        return 0;
    }

    const QRect area = m_ViewWidget->getCurrentScaledImageArea();

    if (m_orientation == SplitOrientation::Horizontal)
    {
        return area.left() + static_cast<int>(area.width() * m_splitPosition);
    }
    else
    {
        return area.top() + static_cast<int>(area.height() * m_splitPosition);
    }
}

void SplitComparisonDisplayModule::drawSplitterLine(QPainter& painter, const QRect& viewportRect, int splitPosVp) const
{
    const auto& colors = an::ui::tokens().colors;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPen linePen(colors.textPrimary, kSplitterLineWidth);
    linePen.setCosmetic(true);
    painter.setPen(linePen);

    if (m_orientation == SplitOrientation::Horizontal)
    {
        painter.drawLine(splitPosVp, viewportRect.top(), splitPosVp, viewportRect.bottom());
    }
    else
    {
        painter.drawLine(viewportRect.left(), splitPosVp, viewportRect.right(), splitPosVp);
    }

    // Draw handle circle at center of the viewport line
    const QPoint center = (m_orientation == SplitOrientation::Horizontal)
                              ? QPoint(splitPosVp, viewportRect.center().y())
                              : QPoint(viewportRect.center().x(), splitPosVp);

    painter.setBrush(colors.bgSurface);
    painter.setPen(QPen(colors.textPrimary, 1.5));
    painter.drawEllipse(center, kHandleRadius, kHandleRadius);

    // Draw arrows inside the handle
    const int arrowSize = 3;
    painter.setPen(QPen(colors.textPrimary, 1.5));

    if (m_orientation == SplitOrientation::Horizontal)
    {
        // Left arrow
        painter.drawLine(center.x() - arrowSize - 1, center.y(), center.x() - 1, center.y() - arrowSize);
        painter.drawLine(center.x() - arrowSize - 1, center.y(), center.x() - 1, center.y() + arrowSize);
        // Right arrow
        painter.drawLine(center.x() + arrowSize + 1, center.y(), center.x() + 1, center.y() - arrowSize);
        painter.drawLine(center.x() + arrowSize + 1, center.y(), center.x() + 1, center.y() + arrowSize);
    }
    else
    {
        // Up arrow
        painter.drawLine(center.x(), center.y() - arrowSize - 1, center.x() - arrowSize, center.y() - 1);
        painter.drawLine(center.x(), center.y() - arrowSize - 1, center.x() + arrowSize, center.y() - 1);
        // Down arrow
        painter.drawLine(center.x(), center.y() + arrowSize + 1, center.x() - arrowSize, center.y() + 1);
        painter.drawLine(center.x(), center.y() + arrowSize + 1, center.x() + arrowSize, center.y() + 1);
    }

    painter.restore();
}

void SplitComparisonDisplayModule::drawLabels(QPainter& painter, const QRect& viewportRect, int splitPosVp) const
{
    if (m_baseLabel.isEmpty() && m_overlayLabel.isEmpty())
    {
        return;
    }

    // Use the viewport widget's palette to determine dark/light mode
    const bool darkMode = m_viewportOverlay ? an::ui::styling::isDarkPalette(m_viewportOverlay->palette()) : true;
    const QColor backdropColor = an::ui::imageViewOverlayBackdropColor(darkMode);
    const QColor baseColor = an::ui::imageViewOverlayAutostretchColor(darkMode);
    const QColor overlayColor = an::ui::imageViewOverlayOriginalColor(darkMode);
    const QFont labelFont = an::ui::tokens().typography.heading1;
    const QFontMetrics metrics(labelFont);

    // Without swap: first side = base, second side = overlay.
    // With swap:    first side = overlay, second side = base.
    const QString& firstLabel = m_swapped ? m_overlayLabel : m_baseLabel;
    const QColor& firstColor = m_swapped ? overlayColor : baseColor;
    const QString& secondLabel = m_swapped ? m_baseLabel : m_overlayLabel;
    const QColor& secondColor = m_swapped ? baseColor : overlayColor;

    painter.save();
    painter.setFont(labelFont);

    auto drawLabelBox = [&](const QString& text, const QColor& color, const QPoint& topLeft) {
        if (text.isEmpty())
        {
            return;
        }

        const QRect textBounds = metrics.boundingRect(text);
        const QSize boxSize(textBounds.width() + kLabelPaddingX * 2, textBounds.height() + kLabelPaddingY * 2);
        const QRect boxRect(topLeft, boxSize);

        painter.fillRect(boxRect, backdropColor);

        QPen pen;
        pen.setColor(color);
        pen.setWidth(3);
        painter.setPen(pen);
        painter.drawText(boxRect, Qt::AlignCenter, text);
    };

    if (m_orientation == SplitOrientation::Horizontal)
    {
        // Vertical divider — left/right halves.
        // First label at top-left of first area.
        drawLabelBox(firstLabel, firstColor,
                     QPoint(viewportRect.left() + kLabelMargin, viewportRect.top() + kLabelMargin));

        // Second label at top-right of second area (right-aligned).
        const QRect secondBounds = metrics.boundingRect(secondLabel);
        const int secondBoxW = secondBounds.width() + kLabelPaddingX * 2;
        drawLabelBox(secondLabel, secondColor,
                     QPoint(viewportRect.right() - secondBoxW - kLabelMargin, viewportRect.top() + kLabelMargin));
    }
    else
    {
        // Horizontal divider — top/bottom halves.
        // First label at top-left of first area.
        drawLabelBox(firstLabel, firstColor,
                     QPoint(viewportRect.left() + kLabelMargin, viewportRect.top() + kLabelMargin));

        // Second label at bottom-left of second area.
        const QRect secondBounds2 = metrics.boundingRect(secondLabel);
        const int secondBoxH = secondBounds2.height() + kLabelPaddingY * 2;
        drawLabelBox(secondLabel, secondColor,
                     QPoint(viewportRect.left() + kLabelMargin, viewportRect.bottom() - secondBoxH - kLabelMargin));
    }

    painter.restore();
}

} // namespace an::ui
