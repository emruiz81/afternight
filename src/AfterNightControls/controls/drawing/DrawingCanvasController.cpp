// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "controls/drawing/DrawingCanvasController.h"

#include "controls/drawing/ShapeEngine.h"
#include "theme/AnDesignTokens.h"
#include "theme/NightImage.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Image/PlaneView.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QTabletEvent>
#include <QTransform>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <span>
#include <variant>

namespace
{

using an::ui::drawing::clampUnit;

[[nodiscard]] bool pointsDiffer(const QPointF& lhs, const QPointF& rhs)
{
    return !qFuzzyCompare(lhs.x(), rhs.x()) || !qFuzzyCompare(lhs.y(), rhs.y());
}

[[nodiscard]] bool pointsFarEnough(const QPointF& lhs, const QPointF& rhs, const double minDistance)
{
    const QPointF delta = lhs - rhs;
    return std::hypot(delta.x(), delta.y()) >= minDistance;
}

[[nodiscard]] bool isPathShape(const an::ui::drawing::ShapeObject::Type type)
{
    return type == an::ui::drawing::ShapeObject::Type::Polygon || type == an::ui::drawing::ShapeObject::Type::Line;
}

[[nodiscard]] bool isBrushLikeTool(const an::ui::drawing::DrawingTool tool)
{
    return tool == an::ui::drawing::DrawingTool::Brush || tool == an::ui::drawing::DrawingTool::Eraser;
}

[[nodiscard]] float angleDegrees(const QPointF& center, const QPointF& point)
{
    return static_cast<float>(std::atan2(point.y() - center.y(), point.x() - center.x()) *
                              (180.0 / std::numbers::pi_v<double>));
}

[[nodiscard]] float normalizedDeltaDegrees(const float startAngle, const float currentAngle)
{
    return static_cast<float>(std::remainder(static_cast<double>(currentAngle - startAngle), 360.0));
}

[[nodiscard]] Qt::CursorShape cursorFromAngleDegrees(float angleDegreesValue)
{
    float normalizedAngle = std::fmod(angleDegreesValue, 180.0f);
    if (normalizedAngle < 0.0f)
    {
        normalizedAngle += 180.0f;
    }

    if (normalizedAngle < 22.5f || normalizedAngle >= 157.5f)
    {
        return Qt::SizeHorCursor;
    }
    if (normalizedAngle < 67.5f)
    {
        return Qt::SizeFDiagCursor;
    }
    if (normalizedAngle < 112.5f)
    {
        return Qt::SizeVerCursor;
    }
    return Qt::SizeBDiagCursor;
}

[[nodiscard]] QRgb makePremultipliedPixel(const QColor& color, const int alpha)
{
    const int clampedAlpha = std::clamp(alpha, 0, 255);
    if (clampedAlpha <= 0)
    {
        return qRgba(0, 0, 0, 0);
    }

    const int red = (color.red() * clampedAlpha + 127) / 255;
    const int green = (color.green() * clampedAlpha + 127) / 255;
    const int blue = (color.blue() * clampedAlpha + 127) / 255;
    return qRgba(red, green, blue, clampedAlpha);
}

} // namespace

namespace an::ui::drawing
{

DrawingCanvasController::DrawingCanvasController(ImageViewScrollArea* parent, std::shared_ptr<DrawingDocument> document)
    : SelectionControllerBase(parent), m_document(std::move(document)),
      m_edits([this](const DrawingEditReport&) { editsLanded(); })
{
    m_edits.setDocument(m_document);
    m_accountedRevision = m_document ? m_document->revision() : 0u;
    m_brushParams.size = 24.0f;
    m_brushParams.opacity = 1.0f;
    m_brushParams.hardness = 1.0f;
    m_brushParams.flow = 1.0f;
    m_brushParams.spacing = 0.25f;
}

void DrawingCanvasController::setDocument(std::shared_ptr<DrawingDocument> document)
{
    m_document = std::move(document);
    m_edits.setDocument(m_document);
    m_strokeInProgress = false;
    m_accountedRevision = m_document ? m_document->revision() : 0u;
    m_overlayDirty = QRect();
    m_selectedShapeIndex.reset();
    clearShapeInteraction();
    invalidateOverlayCache();
    refreshView();
}

void DrawingCanvasController::setBrushParams(const BrushParams& brush)
{
    m_brushParams = makeBrushParams(DrawingTool::Brush, brush);
    refreshView();
}

void DrawingCanvasController::setTool(const DrawingTool tool)
{
    if (m_tool == tool)
    {
        return;
    }

    if (m_strokeInProgress)
    {
        finishStroke();
    }

    m_tool = tool;
    clearShapeInteraction();
    updateSelectionCursor(m_hasCursorPosition ? std::optional<QPointF>(m_lastCursorPos) : std::nullopt);
    refreshView();
}

void DrawingCanvasController::setBrushSize(const float size)
{
    m_brushParams.size = (std::max)(size, 1.0f);
    refreshView();
}

void DrawingCanvasController::setBrushHardness(const float hardness)
{
    m_brushParams.hardness = clampUnit(hardness);
    refreshView();
}

void DrawingCanvasController::setBrushOpacity(const float opacity)
{
    m_brushParams.opacity = clampUnit(opacity);
    refreshView();
}

void DrawingCanvasController::setBrushFlow(const float flow)
{
    m_brushParams.flow = clampUnit(flow);
}

void DrawingCanvasController::setBrushSpacing(const float spacing)
{
    m_brushParams.spacing = std::clamp(spacing, 0.01f, 1.0f);
}

void DrawingCanvasController::setTipShape(const BrushParams::TipShape tipShape)
{
    m_brushParams.tipShape = tipShape;
    refreshView();
}

void DrawingCanvasController::setBlendMode(const BrushParams::BlendMode blendMode)
{
    m_brushParams.blendMode = blendMode;
}

void DrawingCanvasController::setPreviewMode(const MaskPreviewMode previewMode)
{
    if (m_previewMode == previewMode)
    {
        return;
    }

    m_previewMode = previewMode;
    invalidateOverlayCache();
    refreshView();
}

void DrawingCanvasController::setShapeType(const ShapeObject::Type shapeType)
{
    m_shapeType = shapeType;
    clearShapeInteraction();
    refreshView();
}

void DrawingCanvasController::setShapeFeather(const float feather)
{
    m_shapeFeather = (std::max)(feather, 0.0f);
    refreshView();
}

void DrawingCanvasController::setShapeFilled(const bool filled)
{
    m_shapeFilled = filled;
    refreshView();
}

void DrawingCanvasController::setOverlayVisible(const bool visible)
{
    if (m_overlayVisible == visible)
    {
        return;
    }

    m_overlayVisible = visible;
    refreshView();
}

void DrawingCanvasController::setHideWhileAlternateImageVisible(const bool hide)
{
    if (m_hideWhileAlternateImageVisible == hide)
    {
        return;
    }

    m_hideWhileAlternateImageVisible = hide;
    refreshView();
}

void DrawingCanvasController::setStrokeCommittedCallback(std::function<void()> callback)
{
    m_strokeCommittedCallback = std::move(callback);
}

void DrawingCanvasController::removeSelectedShape()
{
    if (!m_document || !m_selectedShapeIndex || *m_selectedShapeIndex >= m_document->shapes().size())
    {
        return;
    }

    const std::size_t index = *m_selectedShapeIndex;
    submitEdit([index](DrawingDocument& document, const an::ExecutionContext& execution) {
        return document.removeShape(execution, index);
    });
    m_selectedShapeIndex.reset();
    clearShapeInteraction();
    refreshView();
}

void DrawingCanvasController::submitEdit(DrawingEdit edit)
{
    if (!edit)
    {
        return;
    }
    // Every change an edit publishes is accounted for, so the overlay redraws each rectangle that changed since its
    // last paint rather than only the last one; a change it cannot account for makes the next paint redraw it whole.
    m_edits.submit([this, edit = std::move(edit)](DrawingDocument& document, const an::ExecutionContext& execution) {
        const std::uint64_t before = document.revision();
        const an::Status landed = edit(document, execution);
        const std::uint64_t after = document.revision();
        if (after == before + 1u && before == m_accountedRevision)
        {
            m_overlayDirty =
                m_overlayDirty.isEmpty() ? document.dirtyRect() : m_overlayDirty.united(document.dirtyRect());
            m_accountedRevision = after;
        }
        return landed;
    });
}

std::size_t DrawingCanvasController::pendingEdits() const noexcept
{
    return m_edits.pending();
}

DrawingEditQueue& DrawingCanvasController::editQueue() noexcept
{
    return m_edits;
}

void DrawingCanvasController::editsLanded()
{
    refreshView();
    if (m_strokeCommittedCallback)
    {
        m_strokeCommittedCallback();
    }
}

void DrawingCanvasController::refreshView() const
{
    if (m_ViewControl && m_ViewControl->getViewWidget())
    {
        m_ViewControl->getViewWidget()->update();
    }
}

void DrawingCanvasController::onSelectionEnabledChanged(const bool enabled)
{
    if (m_ParentArea)
    {
        m_ParentArea->setMouseTracking(enabled);
        m_ParentArea->setAttribute(Qt::WA_TabletTracking, enabled);
        if (auto* viewportWidget = m_ParentArea->viewport())
        {
            viewportWidget->setMouseTracking(enabled);
            viewportWidget->setAttribute(Qt::WA_TabletTracking, enabled);
        }
        if (auto* contentWidget = m_ParentArea->widget())
        {
            contentWidget->setMouseTracking(enabled);
            contentWidget->setAttribute(Qt::WA_TabletTracking, enabled);
        }
    }

    if (!enabled)
    {
        finishStroke();
        m_hasCursorPosition = false;
        clearShapeInteraction();
        if (m_ParentArea)
        {
            m_ParentArea->restoreDefaultCursor();
        }
        refreshView();
    }
}

bool DrawingCanvasController::onMousePressEvent(QMouseEvent* ev)
{
    if (!isListenerEnabled() || !m_overlayVisible || !m_document)
    {
        return false;
    }

    const bool isLeftButton = ev->button() == Qt::LeftButton;
    const bool isMiddleButton = ev->button() == Qt::MiddleButton;
    if (!isLeftButton && !(m_tool == DrawingTool::ShapeSelect && isMiddleButton))
    {
        return false;
    }

    const auto imagePos = mapToImagePosition(ev->pos());
    updateCursorPreview(imagePos);
    updateSelectionCursor(imagePos);
    if (!imagePos.has_value())
    {
        return false;
    }

    switch (m_tool)
    {
    case DrawingTool::Brush:
    case DrawingTool::Eraser:
        if (!isLeftButton)
        {
            return false;
        }
        ev->accept();
        beginStrokeAt(*imagePos, 1.0f);
        return true;
    case DrawingTool::FloodFill:
        if (!isLeftButton)
        {
            return false;
        }
        ev->accept();
        {
            const QPoint seed = imagePos->toPoint();
            const BrushParams brush = currentBrush();
            submitEdit([seed, brush](DrawingDocument& document, const an::ExecutionContext& execution) {
                return document.floodFill(execution, seed, brush.opacity, 0.05f, brush.blendMode);
            });
        }
        refreshView();
        return true;
    case DrawingTool::GradientFill:
        if (!isLeftButton)
        {
            return false;
        }
        ev->accept();
        clearShapeInteraction();
        m_dragAnchor = *imagePos;
        m_pathHoverPoint = *imagePos;
        m_interactionMode = InteractionMode::GradientDrag;
        refreshView();
        return true;
    case DrawingTool::FreehandLasso:
        if (!isLeftButton)
        {
            return false;
        }
        ev->accept();
        clearShapeInteraction();
        m_pathPoints = {*imagePos};
        m_pathHoverPoint = *imagePos;
        m_interactionMode = InteractionMode::FreehandLasso;
        refreshView();
        return true;
    case DrawingTool::ShapeDraw:
        if (!isLeftButton)
        {
            return false;
        }
        ev->accept();
        if (isPathShape(m_shapeType))
        {
            if (m_interactionMode != InteractionMode::PathDraft)
            {
                clearShapeInteraction();
                m_interactionMode = InteractionMode::PathDraft;
                m_pathPoints = {*imagePos};
            }
            else if (m_pathPoints.empty() || pointsDiffer(m_pathPoints.back(), *imagePos))
            {
                m_pathPoints.push_back(*imagePos);
            }
            m_pathHoverPoint = *imagePos;
            refreshView();
            return true;
        }

        clearShapeInteraction();
        m_interactionMode = InteractionMode::ShapeDrag;
        m_dragAnchor = *imagePos;
        m_previewShape = ShapeEngine::makeDragShape(m_shapeType, *imagePos, *imagePos, currentBrush().opacity,
                                                    m_shapeFeather, m_shapeFilled, currentLineWidth());
        refreshView();
        return true;
    case DrawingTool::ShapeSelect: {
        ev->accept();
        const ShapeHoverState hoverState = shapeHoverState(*imagePos);
        if (hoverState.shapeIndex && *hoverState.shapeIndex < m_document->shapes().size())
        {
            const ShapeObject& hoveredShape = m_document->shapes()[*hoverState.shapeIndex];
            m_selectedShapeIndex = hoverState.shapeIndex;
            if (isMiddleButton && hoverState.handleIndex >= 0)
            {
                clearShapeInteraction();
                m_selectedHandle = hoverState.handleIndex;
                m_originalSelectedShape = hoveredShape;
                m_previewShape = hoveredShape;
                m_dragAnchor = *imagePos;
                m_initialShapeRotation = hoveredShape.rotation;
                m_rotationStartAngle = angleDegrees(hoveredShape.center, *imagePos);
                m_interactionMode = InteractionMode::ShapeRotate;
                updateSelectionCursor(imagePos);
                refreshView();
                return true;
            }

            if (hoverState.handleIndex >= 0)
            {
                clearShapeInteraction();
                m_selectedHandle = hoverState.handleIndex;
                m_originalSelectedShape = hoveredShape;
                m_previewShape = hoveredShape;
                m_dragAnchor = *imagePos;
                m_interactionMode = InteractionMode::ShapeResize;
                updateSelectionCursor(imagePos);
                refreshView();
                return true;
            }

            if (isLeftButton && hoverState.insideShape)
            {
                clearShapeInteraction();
                m_originalSelectedShape = hoveredShape;
                m_previewShape = hoveredShape;
                m_dragAnchor = *imagePos;
                m_interactionMode = InteractionMode::ShapeMove;
                updateSelectionCursor(imagePos);
                refreshView();
                return true;
            }
        }

        clearShapeInteraction();
        m_selectedShapeIndex = m_document->hitTestShape(*imagePos, handleHitTolerance());
        updateSelectionCursor(imagePos);
        refreshView();
        return true;
    }
    }

    return false;
}

bool DrawingCanvasController::onMouseDoubleClickEvent(QMouseEvent* ev)
{
    if (!isListenerEnabled() || !m_overlayVisible || !m_document || ev->button() != Qt::LeftButton ||
        m_tool != DrawingTool::ShapeDraw)
    {
        return false;
    }

    if (!isPathShape(m_shapeType) || m_interactionMode != InteractionMode::PathDraft)
    {
        return false;
    }

    const auto imagePos = mapToImagePosition(ev->pos());
    updateCursorPreview(imagePos);
    if (!imagePos.has_value())
    {
        return false;
    }

    ev->accept();
    if (m_pathPoints.empty() || pointsDiffer(m_pathPoints.back(), *imagePos))
    {
        m_pathPoints.push_back(*imagePos);
    }

    const size_t minPoints = (m_shapeType == ShapeObject::Type::Polygon) ? 3u : 2u;
    if (m_pathPoints.size() >= minPoints)
    {
        addShapeEdit(ShapeEngine::makePathShape(m_shapeType, m_pathPoints, currentBrush().opacity, m_shapeFeather,
                                                m_shapeFilled, currentLineWidth()));
    }

    clearShapeInteraction();
    refreshView();
    return true;
}

bool DrawingCanvasController::onMouseMoveEvent(QMouseEvent* ev)
{
    if (!isListenerEnabled() || !m_document)
    {
        return false;
    }

    const auto imagePos = mapToImagePosition(ev->pos());
    updateCursorPreview(imagePos);
    updateSelectionCursor(imagePos);

    if (!imagePos.has_value())
    {
        return false;
    }

    if (m_strokeInProgress)
    {
        if (!(ev->buttons() & Qt::LeftButton))
        {
            return false;
        }

        ev->accept();
        addStrokePointEdit(stabilizedStrokePoint(*imagePos), 1.0f);
        refreshView();
        return true;
    }

    const bool leftPressed = (ev->buttons() & Qt::LeftButton);
    const bool middlePressed = (ev->buttons() & Qt::MiddleButton);
    if (!leftPressed && !middlePressed)
    {
        if (m_interactionMode == InteractionMode::PathDraft)
        {
            m_pathHoverPoint = *imagePos;
            refreshView();
        }
        else if (m_interactionMode == InteractionMode::GradientDrag)
        {
            m_pathHoverPoint = *imagePos;
            refreshView();
        }
        return false;
    }

    switch (m_interactionMode)
    {
    case InteractionMode::FreehandLasso:
        if (!leftPressed)
        {
            return false;
        }
        ev->accept();
        if (m_pathPoints.empty() || pointsFarEnough(m_pathPoints.back(), *imagePos, 0.75))
        {
            m_pathPoints.push_back(*imagePos);
        }
        m_pathHoverPoint = *imagePos;
        refreshView();
        return true;
    case InteractionMode::ShapeDrag:
        if (!leftPressed)
        {
            return false;
        }
        ev->accept();
        if (m_dragAnchor)
        {
            m_previewShape = ShapeEngine::makeDragShape(m_shapeType, *m_dragAnchor, *imagePos, currentBrush().opacity,
                                                        m_shapeFeather, m_shapeFilled, currentLineWidth());
            refreshView();
        }
        return true;
    case InteractionMode::ShapeMove:
        if (!leftPressed)
        {
            return false;
        }
        ev->accept();
        if (m_dragAnchor)
        {
            ShapeObject movedShape = m_originalSelectedShape;
            ShapeEngine::translate(movedShape, *imagePos - *m_dragAnchor);
            m_previewShape = movedShape;
            refreshView();
        }
        return true;
    case InteractionMode::ShapeResize:
        if (!leftPressed)
        {
            return false;
        }
        ev->accept();
        if (m_previewShape)
        {
            ShapeObject resizedShape = m_originalSelectedShape;
            ShapeEngine::moveHandle(resizedShape, m_selectedHandle, *imagePos);
            m_previewShape = resizedShape;
            refreshView();
        }
        return true;
    case InteractionMode::ShapeRotate:
        if (!middlePressed)
        {
            return false;
        }
        ev->accept();
        if (m_previewShape)
        {
            ShapeObject rotatedShape = m_originalSelectedShape;
            const float currentAngle = angleDegrees(rotatedShape.center, *imagePos);
            rotatedShape.rotation = m_initialShapeRotation;
            ShapeEngine::rotate(rotatedShape, normalizedDeltaDegrees(m_rotationStartAngle, currentAngle));
            m_previewShape = rotatedShape;
            refreshView();
        }
        return true;
    case InteractionMode::PathDraft:
        ev->accept();
        m_pathHoverPoint = *imagePos;
        refreshView();
        return true;
    case InteractionMode::GradientDrag:
        ev->accept();
        m_pathHoverPoint = *imagePos;
        refreshView();
        return true;
    case InteractionMode::None:
    case InteractionMode::BrushStroke:
    default:
        return false;
    }
}

bool DrawingCanvasController::onMouseReleaseEvent(QMouseEvent* ev)
{
    if (!isListenerEnabled() || !m_document)
    {
        return false;
    }

    const bool isLeftButton = ev->button() == Qt::LeftButton;
    const bool isMiddleButton = ev->button() == Qt::MiddleButton;
    if (!isLeftButton && !isMiddleButton)
    {
        return false;
    }

    const auto imagePos = mapToImagePosition(ev->pos());
    updateCursorPreview(imagePos);
    updateSelectionCursor(imagePos);

    if (m_strokeInProgress && isLeftButton)
    {
        ev->accept();
        if (imagePos.has_value())
        {
            addStrokePointEdit(*imagePos, 1.0f);
            m_lastStabilizedStrokePoint = *imagePos;
        }
        finishStroke();
        return true;
    }

    switch (m_interactionMode)
    {
    case InteractionMode::FreehandLasso:
        if (!isLeftButton)
        {
            return false;
        }
        ev->accept();
        if (imagePos.has_value() && (m_pathPoints.empty() || pointsFarEnough(m_pathPoints.back(), *imagePos, 0.75)))
        {
            m_pathPoints.push_back(*imagePos);
        }
        if (m_pathPoints.size() >= 3)
        {
            addShapeEdit(ShapeEngine::makePathShape(ShapeObject::Type::Polygon, m_pathPoints, currentBrush().opacity,
                                                    0.0f, true, currentLineWidth()));
        }
        clearShapeInteraction();
        refreshView();
        return true;
    case InteractionMode::ShapeDrag:
        if (!isLeftButton)
        {
            return false;
        }
        ev->accept();
        if (imagePos.has_value() && m_dragAnchor)
        {
            m_previewShape = ShapeEngine::makeDragShape(m_shapeType, *m_dragAnchor, *imagePos, currentBrush().opacity,
                                                        m_shapeFeather, m_shapeFilled, currentLineWidth());
        }
        commitShapePreview();
        clearShapeInteraction();
        refreshView();
        return true;
    case InteractionMode::ShapeMove:
    case InteractionMode::ShapeResize:
    case InteractionMode::ShapeRotate:
        if ((m_interactionMode == InteractionMode::ShapeRotate && !isMiddleButton) ||
            (m_interactionMode != InteractionMode::ShapeRotate && !isLeftButton))
        {
            return false;
        }
        ev->accept();
        if (m_selectedShapeIndex && m_previewShape && *m_selectedShapeIndex < m_document->shapes().size() &&
            m_dragAnchor && imagePos.has_value() && pointsDiffer(*m_dragAnchor, *imagePos))
        {
            const std::size_t index = *m_selectedShapeIndex;
            const ShapeObject shape = *m_previewShape;
            submitEdit([index, shape](DrawingDocument& document, const an::ExecutionContext& execution) {
                return document.updateShape(execution, index, shape);
            });
        }
        clearShapeInteraction();
        updateSelectionCursor(imagePos);
        refreshView();
        return true;
    case InteractionMode::PathDraft:
        if (!isLeftButton)
        {
            return false;
        }
        ev->accept();
        return true;
    case InteractionMode::GradientDrag:
        if (!isLeftButton)
        {
            return false;
        }
        ev->accept();
        if (imagePos.has_value() && m_dragAnchor && pointsDiffer(*m_dragAnchor, *imagePos))
        {
            const QPointF start = *m_dragAnchor;
            const QPointF end = *imagePos;
            const BrushParams brush = currentBrush();
            submitEdit([start, end, brush](DrawingDocument& document, const an::ExecutionContext& execution) {
                return document.applyLinearGradient(execution, start, end, 0.0f, brush.opacity, brush.blendMode);
            });
        }
        clearShapeInteraction();
        refreshView();
        return true;
    case InteractionMode::None:
    case InteractionMode::BrushStroke:
    default:
        return false;
    }
}

bool DrawingCanvasController::onTabletEvent(QTabletEvent* ev)
{
    if (!isListenerEnabled() || !m_overlayVisible || !m_document || !isBrushLikeTool(m_tool))
    {
        return false;
    }

    const auto imagePos = mapToImagePosition(ev->position().toPoint());
    updateCursorPreview(imagePos);
    if (!imagePos.has_value())
    {
        return false;
    }

    const float pressure = clampUnit(ev->pressure());
    switch (ev->type())
    {
    case QEvent::TabletPress:
        ev->accept();
        beginStrokeAt(*imagePos, pressure);
        return true;
    case QEvent::TabletMove:
        if (!m_strokeInProgress)
        {
            return false;
        }
        ev->accept();
        addStrokePointEdit(stabilizedStrokePoint(*imagePos), pressure);
        refreshView();
        return true;
    case QEvent::TabletRelease:
        if (!m_strokeInProgress)
        {
            return false;
        }
        ev->accept();
        addStrokePointEdit(*imagePos, pressure);
        m_lastStabilizedStrokePoint = *imagePos;
        finishStroke();
        return true;
    default:
        return false;
    }
}

void DrawingCanvasController::onDraw(QPainter& painter)
{
    auto* viewWidget = m_ViewControl ? m_ViewControl->getViewWidget() : nullptr;
    if (!m_overlayVisible || !m_document || !viewWidget)
    {
        return;
    }

    if (m_hideWhileAlternateImageVisible && viewWidget->isAlternateImageRenderEnabled())
    {
        return;
    }

    rebuildOverlayCache();
    if (!m_cachedOverlay.isNull())
    {
        const double scale = imageScale();
        const QSize targetSize(static_cast<int>(std::lround(static_cast<double>(m_cachedOverlay.width()) * scale)),
                               static_cast<int>(std::lround(static_cast<double>(m_cachedOverlay.height()) * scale)));
        if (targetSize.width() > 0 && targetSize.height() > 0)
        {
            painter.drawImage(QRect(QPoint(0, 0), targetSize), m_cachedOverlay);
        }
    }

    drawShapeOverlay(painter);
    if (isBrushLikeTool(m_tool))
    {
        drawBrushCursor(painter);
    }
}

BrushParams DrawingCanvasController::currentBrush() const
{
    return makeBrushParams(m_tool, m_brushParams);
}

float DrawingCanvasController::currentLineWidth() const
{
    return (std::max)(1.0f, m_brushParams.size * 0.25f);
}

std::optional<QPointF> DrawingCanvasController::mapToImagePosition(const QPoint& mousePos) const
{
    if (!m_ViewControl || !m_ViewControl->getViewWidget() || !m_document || !m_ParentArea || !m_ParentArea->widget())
    {
        return std::nullopt;
    }

    const QPoint imagePos = m_ParentArea->widget()->pos();
    const QRect imageScaledArea = m_ViewControl->getViewWidget()->getCurrentScaledImageArea();
    const QPoint mouseRelativePos = mousePos - imagePos;
    if (!imageScaledArea.contains(mouseRelativePos))
    {
        return std::nullopt;
    }

    const double scale = imageScale();
    if (scale <= 0.0)
    {
        return std::nullopt;
    }

    const double imageX = std::clamp(static_cast<double>(mouseRelativePos.x()) / scale, 0.0,
                                     static_cast<double>((std::max)(m_document->width() - 1, 0)));
    const double imageY = std::clamp(static_cast<double>(mouseRelativePos.y()) / scale, 0.0,
                                     static_cast<double>((std::max)(m_document->height() - 1, 0)));
    return QPointF(imageX, imageY);
}

double DrawingCanvasController::imageScale() const
{
    return (m_ViewControl && m_ViewControl->getViewWidget()) ? m_ViewControl->getViewWidget()->getCurrentScaleFactor()
                                                             : 1.0;
}

double DrawingCanvasController::handleHitTolerance() const
{
    return (std::max)(1.5, 6.0 / (std::max)(imageScale(), 0.01));
}

DrawingCanvasController::ShapeHoverState DrawingCanvasController::shapeHoverState(const QPointF& imagePos) const
{
    ShapeHoverState result;
    if (!m_document)
    {
        return result;
    }

    const auto& shapes = m_document->shapes();
    const double tolerance = handleHitTolerance();
    const auto probeShape = [&](const size_t index) -> bool {
        if (index >= shapes.size())
        {
            return false;
        }

        const ShapeObject& shape = shapes[index];
        const int handleIndex = ShapeEngine::hitTestHandle(shape, imagePos, tolerance);
        if (handleIndex >= 0)
        {
            result.shapeIndex = index;
            result.handleIndex = handleIndex;
            result.insideShape = true;
            return true;
        }
        if (ShapeEngine::hitTest(shape, imagePos, tolerance))
        {
            result.shapeIndex = index;
            result.insideShape = true;
            return true;
        }
        return false;
    };

    if (m_selectedShapeIndex && probeShape(*m_selectedShapeIndex))
    {
        return result;
    }

    for (size_t index = shapes.size(); index > 0; --index)
    {
        const size_t shapeIndex = index - 1;
        if (m_selectedShapeIndex && shapeIndex == *m_selectedShapeIndex)
        {
            continue;
        }
        if (probeShape(shapeIndex))
        {
            return result;
        }
    }

    return result;
}

Qt::CursorShape DrawingCanvasController::cursorForSelectionHover(const ShapeObject& shape, const int handleIndex) const
{
    const auto handles = ShapeEngine::handlePoints(shape);
    if (handleIndex < 0 || static_cast<size_t>(handleIndex) >= handles.size())
    {
        return Qt::ArrowCursor;
    }

    const QPointF delta = handles[static_cast<size_t>(handleIndex)] - shape.center;
    if (std::hypot(delta.x(), delta.y()) <= 0.001)
    {
        return Qt::ArrowCursor;
    }

    return cursorFromAngleDegrees(angleDegrees(QPointF(0.0, 0.0), delta));
}

std::optional<ShapeObject> DrawingCanvasController::pathPreviewShape() const
{
    if (m_interactionMode == InteractionMode::FreehandLasso)
    {
        if (m_pathPoints.empty())
        {
            return std::nullopt;
        }

        std::vector<QPointF> points = m_pathPoints;
        if (m_pathHoverPoint.has_value() && (points.empty() || pointsDiffer(points.back(), *m_pathHoverPoint)))
        {
            points.push_back(*m_pathHoverPoint);
        }

        if (points.size() < 2)
        {
            return std::nullopt;
        }

        return ShapeEngine::makePathShape(ShapeObject::Type::Polygon, points, currentBrush().opacity, 0.0f, true,
                                          currentLineWidth());
    }

    if (m_interactionMode != InteractionMode::PathDraft || m_pathPoints.empty() || !isPathShape(m_shapeType))
    {
        return std::nullopt;
    }

    std::vector<QPointF> points = m_pathPoints;
    if (m_pathHoverPoint.has_value() && (points.empty() || pointsDiffer(points.back(), *m_pathHoverPoint)))
    {
        points.push_back(*m_pathHoverPoint);
    }

    if (points.size() < 2)
    {
        return std::nullopt;
    }

    return ShapeEngine::makePathShape(m_shapeType, points, currentBrush().opacity, m_shapeFeather, m_shapeFilled,
                                      currentLineWidth());
}

QColor DrawingCanvasController::primaryOverlayColor() const
{
    const auto& tokenColors = an::ui::tokens().colors;
    switch (m_previewMode)
    {
    case MaskPreviewMode::Amber:
        return QColor(255, 180, 0);
    case MaskPreviewMode::Green:
        return tokenColors.status().success;
    case MaskPreviewMode::Blue:
        return tokenColors.accent().primary;
    case MaskPreviewMode::Grayscale:
        return tokenColors.text().primary;
    case MaskPreviewMode::Checkerboard:
        return tokenColors.bg().card;
    case MaskPreviewMode::Red:
    default:
        return tokenColors.status().error;
    }
}

QColor DrawingCanvasController::secondaryOverlayColor() const
{
    if (m_previewMode != MaskPreviewMode::Checkerboard)
    {
        return QColor();
    }

    return an::ui::tokens().colors.bg().surface;
}

QPointF DrawingCanvasController::stabilizedStrokePoint(const QPointF& imagePos)
{
    if (!m_lastStabilizedStrokePoint.has_value() || m_brushParams.stabilization <= 0.001f)
    {
        m_lastStabilizedStrokePoint = imagePos;
        return imagePos;
    }

    const double alpha = std::clamp(1.0 - (static_cast<double>(m_brushParams.stabilization) * 0.85), 0.05, 1.0);
    const QPointF stabilized = *m_lastStabilizedStrokePoint + ((imagePos - *m_lastStabilizedStrokePoint) * alpha);
    m_lastStabilizedStrokePoint = stabilized;
    return stabilized;
}

void DrawingCanvasController::updateCursorPreview(const std::optional<QPointF>& imagePos)
{
    bool changed = false;
    if (imagePos.has_value())
    {
        if (!m_hasCursorPosition || pointsDiffer(m_lastCursorPos, *imagePos))
        {
            m_lastCursorPos = *imagePos;
            m_hasCursorPosition = true;
            changed = true;
        }
    }
    else if (m_hasCursorPosition)
    {
        m_hasCursorPosition = false;
        changed = true;
    }

    if (changed)
    {
        refreshView();
    }
}

void DrawingCanvasController::updateSelectionCursor(const std::optional<QPointF>& imagePos)
{
    if (!m_ParentArea)
    {
        return;
    }

    if (m_tool != DrawingTool::ShapeSelect || !imagePos.has_value() || !m_document)
    {
        m_ParentArea->restoreDefaultCursor();
        return;
    }

    switch (m_interactionMode)
    {
    case InteractionMode::ShapeMove:
        m_ParentArea->setCursor(Qt::SizeAllCursor);
        return;
    case InteractionMode::ShapeResize:
        if (m_previewShape)
        {
            m_ParentArea->setCursor(cursorForSelectionHover(*m_previewShape, m_selectedHandle));
            return;
        }
        break;
    case InteractionMode::ShapeRotate:
        m_ParentArea->setCursor(Qt::CrossCursor);
        return;
    case InteractionMode::None:
    case InteractionMode::BrushStroke:
    case InteractionMode::ShapeDrag:
    case InteractionMode::PathDraft:
    case InteractionMode::FreehandLasso:
    case InteractionMode::GradientDrag:
        break;
    }

    const ShapeHoverState hoverState = shapeHoverState(*imagePos);
    if (!hoverState.shapeIndex || *hoverState.shapeIndex >= m_document->shapes().size())
    {
        m_ParentArea->restoreDefaultCursor();
        return;
    }

    const ShapeObject& shape = m_document->shapes()[*hoverState.shapeIndex];
    if (hoverState.handleIndex >= 0)
    {
        m_ParentArea->setCursor(cursorForSelectionHover(shape, hoverState.handleIndex));
    }
    else if (hoverState.insideShape)
    {
        m_ParentArea->setCursor(Qt::SizeAllCursor);
    }
    else
    {
        m_ParentArea->restoreDefaultCursor();
    }
}

void DrawingCanvasController::clearShapeInteraction()
{
    m_interactionMode = InteractionMode::None;
    m_dragAnchor.reset();
    m_previewShape.reset();
    m_pathPoints.clear();
    m_pathHoverPoint.reset();
    m_lastStabilizedStrokePoint.reset();
    m_selectedHandle = -1;
    m_initialShapeRotation = 0.0f;
    m_rotationStartAngle = 0.0f;
}

void DrawingCanvasController::finishStroke()
{
    if (!m_strokeInProgress || !m_document)
    {
        return;
    }

    submitEdit(
        [](DrawingDocument& document, const an::ExecutionContext& execution) { return document.endStroke(execution); });
    m_strokeInProgress = false;
    m_interactionMode = InteractionMode::None;
    m_lastStabilizedStrokePoint.reset();
    refreshView();
}

void DrawingCanvasController::beginStrokeAt(const QPointF& imagePos, const float pressure)
{
    const BrushParams brush = currentBrush();
    submitEdit([brush](DrawingDocument& document, const an::ExecutionContext& execution) {
        return document.beginStroke(execution, brush);
    });
    addStrokePointEdit(imagePos, pressure);
    m_lastStabilizedStrokePoint = imagePos;
    m_strokeInProgress = true;
    m_interactionMode = InteractionMode::BrushStroke;
    refreshView();
}

void DrawingCanvasController::addStrokePointEdit(const QPointF& imagePos, const float pressure)
{
    submitEdit([imagePos, pressure](DrawingDocument& document, const an::ExecutionContext& execution) {
        return document.addStrokePoint(execution, imagePos, pressure);
    });
}

void DrawingCanvasController::addShapeEdit(const ShapeObject& shape)
{
    submitEdit([this, shape](DrawingDocument& document, const an::ExecutionContext& execution) -> an::Status {
        const an::Result<std::size_t> added = document.addShape(execution, shape);
        if (!added.hasValue())
        {
            return added.status();
        }
        m_selectedShapeIndex = *added;
        return an::Status::success();
    });
}

void DrawingCanvasController::commitShapePreview()
{
    if (!m_document || !m_previewShape)
    {
        return;
    }

    addShapeEdit(*m_previewShape);
}

void DrawingCanvasController::drawBrushCursor(QPainter& painter) const
{
    if (!m_hasCursorPosition || !m_ViewControl || !m_ViewControl->getViewWidget())
    {
        return;
    }

    const double scale = imageScale();
    if (scale <= 0.0)
    {
        return;
    }

    const QColor accent = an::ui::tokens().colors.accentPrimary;
    QPen pen(accent);
    pen.setCosmetic(true);
    pen.setWidth(1);
    pen.setStyle(m_tool == DrawingTool::Eraser ? Qt::DashLine : Qt::SolidLine);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(pen);

    const QPointF center(m_lastCursorPos.x() * scale, m_lastCursorPos.y() * scale);
    const double diameter = (std::max)(1.0, static_cast<double>(m_brushParams.size) * scale);
    const double radius = diameter * 0.5;
    const QRectF outerRect(center.x() - radius, center.y() - radius, diameter, diameter);

    if (m_brushParams.tipShape == BrushParams::TipShape::Square)
    {
        painter.drawRect(outerRect);
    }
    else
    {
        painter.drawEllipse(outerRect);
    }

    const double innerDiameter = diameter * clampUnit(m_brushParams.hardness);
    if (innerDiameter >= 2.0 && innerDiameter + 0.5 < diameter)
    {
        QPen innerPen = pen;
        innerPen.setColor(QColor(accent.red(), accent.green(), accent.blue(), 160));
        innerPen.setStyle(Qt::DotLine);
        painter.setPen(innerPen);

        const double innerRadius = innerDiameter * 0.5;
        const QRectF innerRect(center.x() - innerRadius, center.y() - innerRadius, innerDiameter, innerDiameter);
        if (m_brushParams.tipShape == BrushParams::TipShape::Square)
        {
            painter.drawRect(innerRect);
        }
        else
        {
            painter.drawEllipse(innerRect);
        }
    }
}

void DrawingCanvasController::drawShapeOverlay(QPainter& painter) const
{
    const QColor accent = an::ui::tokens().colors.accentPrimary;
    const double scale = imageScale();

    if (const auto draftPath = pathPreviewShape())
    {
        drawShapePath(painter, *draftPath, accent, Qt::DashLine, true);
    }

    if (m_interactionMode == InteractionMode::GradientDrag && m_dragAnchor.has_value() &&
        m_pathHoverPoint.has_value() && scale > 0.0)
    {
        QPen gradientPen(accent);
        gradientPen.setCosmetic(true);
        gradientPen.setWidth(2);
        gradientPen.setStyle(Qt::DashLine);
        painter.save();
        painter.setPen(gradientPen);
        painter.setBrush(Qt::NoBrush);
        painter.drawLine(QPointF(m_dragAnchor->x() * scale, m_dragAnchor->y() * scale),
                         QPointF(m_pathHoverPoint->x() * scale, m_pathHoverPoint->y() * scale));
        painter.restore();
    }

    if (m_previewShape &&
        (m_interactionMode == InteractionMode::ShapeDrag || m_interactionMode == InteractionMode::ShapeMove ||
         m_interactionMode == InteractionMode::ShapeResize || m_interactionMode == InteractionMode::ShapeRotate))
    {
        drawShapePath(painter, *m_previewShape, accent, Qt::DashLine, true);
    }

    if (m_selectedShapeIndex && m_document && *m_selectedShapeIndex < m_document->shapes().size())
    {
        const ShapeObject& selectedShape = (m_previewShape && (m_interactionMode == InteractionMode::ShapeMove ||
                                                               m_interactionMode == InteractionMode::ShapeResize ||
                                                               m_interactionMode == InteractionMode::ShapeRotate))
                                               ? *m_previewShape
                                               : m_document->shapes()[*m_selectedShapeIndex];
        drawShapePath(painter, selectedShape, accent, Qt::SolidLine, false);
        drawSelectionHandles(painter, selectedShape);
    }
}

void DrawingCanvasController::drawShapePath(QPainter& painter, const ShapeObject& shape, const QColor& color,
                                            const Qt::PenStyle penStyle, const bool drawFill) const
{
    const double scale = imageScale();
    if (scale <= 0.0)
    {
        return;
    }

    QPen pen(color);
    pen.setCosmetic(true);
    pen.setWidth(2);
    pen.setStyle(penStyle);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    const QPainterPath imagePath = ShapeEngine::buildPainterPath(shape);
    const QPainterPath screenPath = QTransform::fromScale(scale, scale).map(imagePath);
    if (drawFill && shape.filled && shape.type != ShapeObject::Type::Line)
    {
        QColor fillColor = color;
        fillColor.setAlpha(32);
        painter.fillPath(screenPath, fillColor);
    }
    painter.drawPath(screenPath);
}

void DrawingCanvasController::drawSelectionHandles(QPainter& painter, const ShapeObject& shape) const
{
    const double scale = imageScale();
    if (scale <= 0.0)
    {
        return;
    }

    const QColor accent = an::ui::tokens().colors.accentPrimary;
    painter.save();
    painter.setPen(QPen(an::ui::colorAsShown(QColor(Qt::white)), 1));
    painter.setBrush(accent);
    for (const QPointF& handle : ShapeEngine::handlePoints(shape))
    {
        const QPointF screenPoint(handle.x() * scale, handle.y() * scale);
        painter.drawRect(QRectF(screenPoint.x() - 4.0, screenPoint.y() - 4.0, 8.0, 8.0));
    }
    painter.restore();
}

void DrawingCanvasController::invalidateOverlayCache() const
{
    m_overlayFullRebuild = true;
}

void DrawingCanvasController::rebuildOverlayCache() const
{
    if (!m_document)
    {
        m_cachedOverlay = QImage();
        return;
    }

    // Under Night the overlay's colours are red, as everything on screen is.
    const QColor overlayColor = an::ui::colorAsShown(primaryOverlayColor());
    const QColor overlaySecondaryColor = an::ui::colorAsShown(secondaryOverlayColor());
    const QSize overlaySize(m_document->width(), m_document->height());
    const bool sameLook =
        m_cachedOverlay.size() == overlaySize && m_cachedOverlay.format() == QImage::Format_ARGB32_Premultiplied &&
        m_cachedOverlayColor == overlayColor && m_cachedSecondaryOverlayColor == overlaySecondaryColor &&
        m_cachedPreviewMode == m_previewMode;
    if (!m_overlayFullRebuild && sameLook && m_cachedRevision == m_document->revision())
    {
        return;
    }

    // The mask is read under a lease; while it is being written the overlay keeps what it showed and is rebuilt at
    // the next paint.
    const an::Result<an::consumer::AssetReadLease> lease = m_document->mask()->tryRead();
    if (!lease.hasValue())
    {
        return;
    }
    const an::PlaneView<const float> maskSamples = *std::get<an::Image<float>>(lease->image()).view()->plane(0u);

    const QRect fullRect(QPoint(0, 0), overlaySize);
    const bool requiresFullRebuild = m_overlayFullRebuild || !sameLook || m_document->revision() != m_accountedRevision;
    if (m_cachedOverlay.isNull() || m_cachedOverlay.size() != overlaySize ||
        m_cachedOverlay.format() != QImage::Format_ARGB32_Premultiplied)
    {
        m_cachedOverlay = QImage(overlaySize, QImage::Format_ARGB32_Premultiplied);
        m_cachedOverlay.fill(Qt::transparent);
    }

    // A grey is its own luma, so under Night it is that grey in red.
    const bool night = an::ui::isNightMode();
    auto* overlayPixels = reinterpret_cast<QRgb*>(m_cachedOverlay.bits());
    const QRect dirtyRect = requiresFullRebuild ? fullRect : m_overlayDirty.intersected(fullRect);
    const int width = overlaySize.width();
    for (int y = dirtyRect.top(); y <= dirtyRect.bottom() && !dirtyRect.isEmpty(); ++y)
    {
        const std::span<const float> row = *maskSamples.row(static_cast<std::uint32_t>(y));
        for (int x = dirtyRect.left(); x <= dirtyRect.right(); ++x)
        {
            const int index = (y * width) + x;
            const float maskValue = clampUnit(row[static_cast<std::size_t>(x)]);
            if (!(maskValue > 0.0f))
            {
                overlayPixels[index] = qRgba(0, 0, 0, 0);
                continue;
            }

            switch (m_previewMode)
            {
            case MaskPreviewMode::Grayscale: {
                const int alpha = static_cast<int>(std::lround(maskValue * 192.0f));
                const int gray = static_cast<int>(std::lround(maskValue * 255.0f));
                overlayPixels[index] =
                    makePremultipliedPixel(night ? QColor(gray, 0, 0) : QColor(gray, gray, gray), alpha);
                break;
            }
            case MaskPreviewMode::Checkerboard: {
                const bool usePrimary = (((x / 8) + (y / 8)) % 2) == 0;
                const int alpha = static_cast<int>(std::lround(maskValue * 176.0f));
                overlayPixels[index] = makePremultipliedPixel(usePrimary ? overlayColor : overlaySecondaryColor, alpha);
                break;
            }
            case MaskPreviewMode::Red:
            case MaskPreviewMode::Green:
            case MaskPreviewMode::Blue:
            case MaskPreviewMode::Amber:
            default: {
                const int alpha = static_cast<int>(std::lround(maskValue * 160.0f));
                overlayPixels[index] = makePremultipliedPixel(overlayColor, alpha);
                break;
            }
            }
        }
    }

    m_cachedOverlayColor = overlayColor;
    m_cachedSecondaryOverlayColor = overlaySecondaryColor;
    m_cachedPreviewMode = m_previewMode;
    m_cachedRevision = m_document->revision();
    m_accountedRevision = m_cachedRevision;
    m_overlayDirty = QRect();
    m_overlayFullRebuild = false;
}

} // namespace an::ui::drawing
