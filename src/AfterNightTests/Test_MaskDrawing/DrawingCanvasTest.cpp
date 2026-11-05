// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The drawing tools on a real view: mouse and tablet input queued as edits of the document in the order it came, the
// strokes made while the session is busy kept and landed afterwards exactly as they would have been drawn, shapes
// dragged out and selected, and the overlay painted from the mask - every rectangle the landed edits changed since the
// last paint, not only the last one.

#include "Test_MaskDrawing/MaskDrawingTestSupport.h"
#include "Test_ControlsViewer/ViewerTestContext.h"

#include "controls/drawing/DrawingCanvasController.h"
#include "controls/drawing/DrawingDocument.h"

#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QPointingDevice>
#include <QTabletEvent>

#include <gtest/gtest.h>

#include <memory>
#include <optional>

namespace
{

using an::tests::drawing::admitted;
using an::tests::drawing::Channels;
using an::tests::drawing::makeBrush;
using an::tests::drawing::makeDrawing;
using an::tests::drawing::maskAt;
using an::tests::drawing::pumpQuietly;
using an::tests::drawing::pumpUntil;
using an::tests::viewer::InstalledContext;
using an::tests::viewer::makeDocument;
using an::tests::viewer::makeView;
using an::tests::viewer::settle;
using an::tests::viewer::ViewerTestContext;
using an::tests::viewer::ViewHandle;
using an::ui::drawing::BrushParams;
using an::ui::drawing::DrawingCanvasController;
using an::ui::drawing::DrawingDocument;
using an::ui::drawing::DrawingTool;
using an::ui::drawing::ShapeObject;

constexpr int kWidth = 64;
constexpr int kHeight = 48;

class DrawingCanvasTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_context = an::tests::viewer::openViewerContext(1u);
        ASSERT_NE(m_context, nullptr);
        m_installed.emplace(*m_context);
        m_view = makeView("drawing");
        std::optional<an::ui::ImageDocument> image =
            makeDocument<float>(m_context->engineSession(), kWidth, kHeight, Channels::Grey,
                                [](std::uint32_t, std::uint32_t, std::uint32_t) { return 0.25f; });
        ASSERT_TRUE(image.has_value());
        m_view->setDocument(std::move(*image));
        ASSERT_TRUE(settle(*m_view));
        m_view->zoom100();
        ASSERT_TRUE(settle(*m_view));
        ASSERT_DOUBLE_EQ(m_view->getViewWidget()->getCurrentScaleFactor(), 1.0);

        m_document = makeDrawing(m_context->engineSession(), kWidth, kHeight);
        ASSERT_NE(m_document, nullptr);
        m_canvas = std::make_unique<DrawingCanvasController>(m_view->getScrollArea(), m_document);
        m_canvas->setStrokeCommittedCallback([this]() { ++m_committed; });
        m_canvas->setBrushParams(makeBrush(5.0f));
        m_canvas->editQueue().setRetryInterval(std::chrono::milliseconds(1));
        m_canvas->setListenerEnabled(true);
    }

    void TearDown() override
    {
        if (m_canvas)
        {
            m_canvas->destroy();
        }
        m_canvas.reset();
        m_document.reset();
        if (m_view)
        {
            static_cast<void>(settle(*m_view));
        }
        m_view.reset();
        m_installed.reset();
        m_context.reset();
    }

    /// Where an image pixel's centre is in the scroll area's viewport, at the view's scale of one.
    [[nodiscard]] QPointF at(int x, int y) const
    {
        const QPoint origin = m_view->getScrollArea()->widget()->pos();
        return QPointF(origin.x() + x, origin.y() + y);
    }

    void press(int x, int y, Qt::MouseButton button = Qt::LeftButton)
    {
        QMouseEvent event(QEvent::MouseButtonPress, at(x, y), at(x, y), button, button, Qt::NoModifier);
        EXPECT_TRUE(m_canvas->onMousePressEvent(&event));
    }

    void move(int x, int y, Qt::MouseButtons buttons = Qt::LeftButton)
    {
        QMouseEvent event(QEvent::MouseMove, at(x, y), at(x, y), Qt::NoButton, buttons, Qt::NoModifier);
        static_cast<void>(m_canvas->onMouseMoveEvent(&event));
    }

    void release(int x, int y, Qt::MouseButton button = Qt::LeftButton)
    {
        QMouseEvent event(QEvent::MouseButtonRelease, at(x, y), at(x, y), button, Qt::NoButton, Qt::NoModifier);
        EXPECT_TRUE(m_canvas->onMouseReleaseEvent(&event));
    }

    void tablet(QEvent::Type type, int x, int y, double pressure)
    {
        QTabletEvent event(type, QPointingDevice::primaryPointingDevice(), at(x, y), at(x, y), pressure, 0.0f, 0.0f,
                           0.0f, 0.0, 0.0f, Qt::NoModifier, Qt::LeftButton,
                           type == QEvent::TabletRelease ? Qt::NoButton : Qt::LeftButton);
        EXPECT_TRUE(m_canvas->onTabletEvent(&event));
    }

    /// What the canvas paints over the view, at the view's scale of one.
    [[nodiscard]] QImage paintOverlay() const
    {
        QImage painted(kWidth, kHeight, QImage::Format_ARGB32_Premultiplied);
        painted.fill(Qt::transparent);
        QPainter painter(&painted);
        m_canvas->onDraw(painter);
        painter.end();
        return painted;
    }

    [[nodiscard]] bool landed() const
    {
        return pumpUntil([this]() { return m_canvas->pendingEdits() == 0u; });
    }

    std::unique_ptr<ViewerTestContext> m_context;
    std::optional<InstalledContext> m_installed;
    ViewHandle m_view;
    std::shared_ptr<DrawingDocument> m_document;
    std::unique_ptr<DrawingCanvasController> m_canvas;
    int m_committed = 0;
};

} // namespace

// A press, a drag and a release land one stroke on the mask and keep one undo step, and the window is told.
TEST_F(DrawingCanvasTest, AMouseStrokeLandsOnTheMaskAndKeepsOneStep)
{
    press(10, 10);
    move(20, 10);
    move(30, 10);
    release(30, 10);
    ASSERT_TRUE(landed());

    EXPECT_FLOAT_EQ(maskAt(*m_document, 20, 10), 1.0f);
    EXPECT_FLOAT_EQ(maskAt(*m_document, 20, 20), 0.0f);
    EXPECT_TRUE(m_document->canUndo());
    EXPECT_EQ(m_document->counters().undoSteps, 1u);
    EXPECT_FALSE(m_document->hasActiveStroke());
    EXPECT_GT(m_committed, 0);
}

// A stroke made while another operation holds the session's only thread lands nothing until the thread is free, and
// then lands as the same stroke drawn with nothing in the way: no point dropped, none out of order.
TEST_F(DrawingCanvasTest, PointsMadeWhileTheSessionIsBusyWaitAndLandInOrder)
{
    std::optional<an::consumer::OperationScope> busy = admitted(m_context->engineSession());
    ASSERT_TRUE(busy.has_value());
    press(8, 8);
    move(20, 30);
    move(40, 12);
    release(52, 40);
    pumpQuietly();
    EXPECT_GT(m_canvas->pendingEdits(), 0u);
    EXPECT_TRUE(m_document->isEmpty());

    // The same stroke, drawn directly under the busy operation.
    const std::shared_ptr<DrawingDocument> direct = makeDrawing(m_context->engineSession(), kWidth, kHeight);
    ASSERT_NE(direct, nullptr);
    ASSERT_FALSE(direct->beginStroke(busy->execution(), makeBrush(5.0f)).failed());
    for (const QPointF& point : {QPointF(8.0, 8.0), QPointF(20.0, 30.0), QPointF(40.0, 12.0), QPointF(52.0, 40.0)})
    {
        ASSERT_FALSE(direct->addStrokePoint(busy->execution(), point).failed());
    }
    ASSERT_FALSE(direct->endStroke(busy->execution()).failed());
    busy.reset();

    ASSERT_TRUE(landed());
    for (int y = 0; y < kHeight; ++y)
    {
        for (int x = 0; x < kWidth; ++x)
        {
            ASSERT_EQ(maskAt(*m_document, x, y), maskAt(*direct, x, y)) << x << ", " << y;
        }
    }
    EXPECT_EQ(m_document->counters().undoSteps, 1u);
}

// The overlay is painted from the mask, and after edits that landed together it redraws every rectangle they changed:
// two strokes made while the session was busy both show once they land.
TEST_F(DrawingCanvasTest, TheOverlayRedrawsEveryRectangleChangedSinceItsLastPaint)
{
    // The first paint rebuilds the whole overlay; nothing is drawn yet.
    const QImage blank = paintOverlay();
    EXPECT_EQ(qAlpha(blank.pixel(10, 10)), 0);

    {
        std::optional<an::consumer::OperationScope> busy = admitted(m_context->engineSession());
        ASSERT_TRUE(busy.has_value());
        press(10, 10);
        release(10, 10);
        press(50, 36);
        release(50, 36);
        EXPECT_GT(m_canvas->pendingEdits(), 0u);
    }
    ASSERT_TRUE(landed());
    EXPECT_FLOAT_EQ(maskAt(*m_document, 10, 10), 1.0f);
    EXPECT_FLOAT_EQ(maskAt(*m_document, 50, 36), 1.0f);

    const QImage painted = paintOverlay();
    EXPECT_GT(qAlpha(painted.pixel(10, 10)), 0);
    EXPECT_GT(qAlpha(painted.pixel(50, 36)), 0);
    EXPECT_EQ(qAlpha(painted.pixel(30, 24)), 0);
}

// A tablet stroke is stamped at the pressure the pen reports: a hard brush at half pressure lays half coverage at the
// press, and the release on the same pixel lays a second dab - one minus the product of the two complements.
TEST_F(DrawingCanvasTest, ATabletStrokeIsStampedAtItsPressure)
{
    tablet(QEvent::TabletPress, 16, 16, 0.5);
    ASSERT_TRUE(landed());
    EXPECT_FLOAT_EQ(maskAt(*m_document, 16, 16), 0.5f);
    tablet(QEvent::TabletRelease, 16, 16, 0.5);
    ASSERT_TRUE(landed());
    EXPECT_FLOAT_EQ(maskAt(*m_document, 16, 16), 0.75f);
}

// Dragging out a rectangle with the shape tool adds it to the document and selects it, and the mask shows it.
TEST_F(DrawingCanvasTest, DraggingOutAShapeAddsItAndShowsIt)
{
    m_canvas->setTool(DrawingTool::ShapeDraw);
    m_canvas->setShapeType(ShapeObject::Type::Rectangle);
    press(10, 10);
    move(30, 24);
    release(30, 24);
    ASSERT_TRUE(landed());

    ASSERT_EQ(m_document->shapes().size(), 1u);
    EXPECT_GT(maskAt(*m_document, 20, 17), 0.9f);
    EXPECT_FLOAT_EQ(maskAt(*m_document, 40, 40), 0.0f);
    EXPECT_TRUE(m_document->canUndo());
}

// The window's own undo queued after a stroke lands after it, whatever the session was doing when either was asked for.
TEST_F(DrawingCanvasTest, TheWindowsUndoLandsAfterTheStrokeBeforeIt)
{
    {
        std::optional<an::consumer::OperationScope> busy = admitted(m_context->engineSession());
        ASSERT_TRUE(busy.has_value());
        press(12, 12);
        release(12, 12);
        m_canvas->submitEdit(
            [](DrawingDocument& document, const an::ExecutionContext& execution) { return document.undo(execution); });
    }
    ASSERT_TRUE(landed());
    EXPECT_TRUE(m_document->isEmpty());
    EXPECT_TRUE(m_document->canRedo());
    EXPECT_FALSE(m_document->canUndo());
}
