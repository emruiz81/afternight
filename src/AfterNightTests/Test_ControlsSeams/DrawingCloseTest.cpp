// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A drawing canvas closed while its edits wait for the session takes them with it: its retry never runs on the
/// destroyed canvas and the document is left as it was. The canvas is compiled into this executable so the
/// freed-memory quarantine, which replaces the global allocation functions, holds every block it frees poisoned while
/// the case runs.

#include "Test_MaskDrawing/MaskDrawingTestSupport.h"
#include "Test_ControlsSeams/FreedMemoryQuarantine.h"
#include "Test_ControlsViewer/ViewerTestContext.h"

#include "controls/drawing/DrawingCanvasController.h"
#include "controls/drawing/DrawingDocument.h"

#include <QMouseEvent>

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <optional>

namespace
{

using an::tests::drawing::admitted;
using an::tests::drawing::Channels;
using an::tests::drawing::makeBrush;
using an::tests::drawing::makeDrawing;
using an::tests::drawing::pumpQuietly;
using an::tests::viewer::InstalledContext;
using an::tests::viewer::makeDocument;
using an::tests::viewer::makeView;
using an::tests::viewer::settle;
using an::tests::viewer::ViewerTestContext;
using an::tests::viewer::ViewHandle;
using an::ui::drawing::DrawingCanvasController;
using an::ui::drawing::DrawingDocument;

constexpr int kWidth = 64;
constexpr int kHeight = 48;

class DrawingCloseTest : public ::testing::Test
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

        m_document = makeDrawing(m_context->engineSession(), kWidth, kHeight);
        ASSERT_NE(m_document, nullptr);
        m_canvas = std::make_unique<DrawingCanvasController>(m_view->getScrollArea(), m_document);
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

    void press(int x, int y)
    {
        QMouseEvent event(QEvent::MouseButtonPress, at(x, y), at(x, y), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        EXPECT_TRUE(m_canvas->onMousePressEvent(&event));
    }

    void move(int x, int y)
    {
        QMouseEvent event(QEvent::MouseMove, at(x, y), at(x, y), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        static_cast<void>(m_canvas->onMouseMoveEvent(&event));
    }

    std::unique_ptr<ViewerTestContext> m_context;
    std::optional<InstalledContext> m_installed;
    ViewHandle m_view;
    std::shared_ptr<DrawingDocument> m_document;
    std::unique_ptr<DrawingCanvasController> m_canvas;
};

} // namespace

// A canvas closed while its edits wait for the session takes them with it: its retry never runs on the destroyed
// canvas - every block it freed is poisoned while the case runs - and the document is left as it was.
TEST_F(DrawingCloseTest, ACanvasClosedWhileItsEditsWaitLeavesNothingBehind)
{
    std::optional<an::consumer::OperationScope> busy = admitted(m_context->engineSession());
    ASSERT_TRUE(busy.has_value());
    press(20, 20);
    move(30, 30);
    ASSERT_GT(m_canvas->pendingEdits(), 0u);
    {
        an::tests::viewer::FreedMemoryQuarantine quarantine;
        m_canvas->destroy();
        m_canvas.reset();
        busy.reset();
        pumpQuietly();
    }
    EXPECT_TRUE(m_document->isEmpty());
    EXPECT_FALSE(m_document->hasActiveStroke());
    EXPECT_FALSE(m_document->canUndo());
}
