// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The drawing tools on a view: mouse and tablet input turned into edits of a drawing document, the mask painted over
/// the view as an overlay, and the shapes' outlines and handles drawn over that.
///
/// Every edit is queued in input order on the window's `DrawingEditQueue` and lands when the session admits its
/// operation, so an input event is never dropped and the GUI thread never waits. The overlay reads the mask under a
/// read lease and redraws every rectangle the landed edits changed since its last paint.
#pragma once

#include "AfterNightControlsExport.h"
#include "controls/SelectionControllerBase.h"
#include "controls/drawing/DrawingDocument.h"
#include "controls/drawing/DrawingEditQueue.h"
#include "controls/drawing/DrawingTypes.h"

#include <QColor>
#include <QImage>
#include <QPointF>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

class QTabletEvent;

namespace an::ui::drawing
{

class AFTERNIGHTCONTROLS_API DrawingCanvasController final : public SelectionControllerBase<DrawingCanvasController>
{
public:
    DrawingCanvasController(ImageViewScrollArea* parent, std::shared_ptr<DrawingDocument> document);

    void setDocument(std::shared_ptr<DrawingDocument> document);
    void setBrushParams(const BrushParams& brush);
    void setTool(DrawingTool tool);
    void setBrushSize(float size);
    void setBrushHardness(float hardness);
    void setBrushOpacity(float opacity);
    void setBrushFlow(float flow);
    void setBrushSpacing(float spacing);
    void setTipShape(BrushParams::TipShape tipShape);
    void setBlendMode(BrushParams::BlendMode blendMode);
    void setPreviewMode(MaskPreviewMode previewMode);
    void setShapeType(ShapeObject::Type shapeType);
    void setShapeFeather(float feather);
    void setShapeFilled(bool filled);
    void setOverlayVisible(bool visible);
    void setHideWhileAlternateImageVisible(bool hide);
    void setStrokeCommittedCallback(std::function<void()> callback);
    void removeSelectedShape();
    void refreshView() const;

    /// Queues an edit of the document after every edit the input has queued - the window's own undo, redo, clear and
    /// import go through here, so they land in the order they were asked for.
    ///
    /// @param edit the edit.
    void submitEdit(DrawingEdit edit);

    /// How many edits are waiting for the session.
    [[nodiscard]] std::size_t pendingEdits() const noexcept;

    /// The queue the edits land through.
    [[nodiscard]] DrawingEditQueue& editQueue() noexcept;

    void onSelectionEnabledChanged(bool enabled);
    bool onMousePressEvent(QMouseEvent* ev) override;
    bool onMouseDoubleClickEvent(QMouseEvent* ev) override;
    bool onMouseMoveEvent(QMouseEvent* ev) override;
    bool onMouseReleaseEvent(QMouseEvent* ev) override;
    bool onTabletEvent(QTabletEvent* ev) override;
    void onDraw(QPainter& painter) override;

private:
    enum class InteractionMode : std::uint8_t
    {
        None,
        BrushStroke,
        ShapeDrag,
        ShapeMove,
        ShapeResize,
        ShapeRotate,
        PathDraft,
        GradientDrag,
        FreehandLasso
    };

    struct ShapeHoverState
    {
        std::optional<size_t> shapeIndex;
        int handleIndex = -1;
        bool insideShape = false;
    };

    [[nodiscard]] BrushParams currentBrush() const;
    [[nodiscard]] float currentLineWidth() const;
    [[nodiscard]] std::optional<QPointF> mapToImagePosition(const QPoint& mousePos) const;
    [[nodiscard]] double imageScale() const;
    [[nodiscard]] double handleHitTolerance() const;
    [[nodiscard]] std::optional<ShapeObject> pathPreviewShape() const;
    [[nodiscard]] ShapeHoverState shapeHoverState(const QPointF& imagePos) const;
    [[nodiscard]] Qt::CursorShape cursorForSelectionHover(const ShapeObject& shape, int handleIndex) const;
    [[nodiscard]] QColor primaryOverlayColor() const;
    [[nodiscard]] QColor secondaryOverlayColor() const;
    [[nodiscard]] QPointF stabilizedStrokePoint(const QPointF& imagePos);
    void updateCursorPreview(const std::optional<QPointF>& imagePos);
    void updateSelectionCursor(const std::optional<QPointF>& imagePos);
    void clearShapeInteraction();
    void finishStroke();
    void beginStrokeAt(const QPointF& imagePos, float pressure);
    void addStrokePointEdit(const QPointF& imagePos, float pressure);
    void addShapeEdit(const ShapeObject& shape);
    void editsLanded();
    void commitShapePreview();
    void drawBrushCursor(QPainter& painter) const;
    void drawShapeOverlay(QPainter& painter) const;
    void drawShapePath(QPainter& painter, const ShapeObject& shape, const QColor& color, Qt::PenStyle penStyle,
                       bool drawFill) const;
    void drawSelectionHandles(QPainter& painter, const ShapeObject& shape) const;
    void invalidateOverlayCache() const;
    void rebuildOverlayCache() const;

    std::shared_ptr<DrawingDocument> m_document;
    DrawingTool m_tool = DrawingTool::Brush;
    BrushParams m_brushParams;
    MaskPreviewMode m_previewMode = MaskPreviewMode::Red;
    ShapeObject::Type m_shapeType = ShapeObject::Type::Rectangle;
    float m_shapeFeather = 0.0f;
    bool m_shapeFilled = true;
    bool m_overlayVisible = true;
    bool m_hideWhileAlternateImageVisible = false;
    bool m_strokeInProgress = false;
    bool m_hasCursorPosition = false;
    QPointF m_lastCursorPos;
    std::function<void()> m_strokeCommittedCallback;
    InteractionMode m_interactionMode = InteractionMode::None;
    std::optional<QPointF> m_dragAnchor;
    std::optional<ShapeObject> m_previewShape;
    std::vector<QPointF> m_pathPoints;
    std::optional<QPointF> m_pathHoverPoint;
    std::optional<QPointF> m_lastStabilizedStrokePoint;
    std::optional<size_t> m_selectedShapeIndex;
    int m_selectedHandle = -1;
    ShapeObject m_originalSelectedShape;
    float m_initialShapeRotation = 0.0f;
    float m_rotationStartAngle = 0.0f;
    mutable QImage m_cachedOverlay;
    mutable QColor m_cachedOverlayColor;
    mutable QColor m_cachedSecondaryOverlayColor;
    mutable MaskPreviewMode m_cachedPreviewMode = MaskPreviewMode::Red;
    mutable std::uint64_t m_cachedRevision = (std::numeric_limits<std::uint64_t>::max)();
    /// The document revision up to which `m_overlayDirty` accounts for every change.
    mutable std::uint64_t m_accountedRevision = 0u;
    /// Every rectangle the landed edits changed since the overlay was last painted.
    mutable QRect m_overlayDirty;
    mutable bool m_overlayFullRebuild = true;
    DrawingEditQueue m_edits;
};

} // namespace an::ui::drawing
