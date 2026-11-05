// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The mask drawing document as the process framework's mask drawing reaches it, in the suite's session: a stroke adds
/// to the paint without taking away what an earlier stroke laid outside its own dabs and changes only the rectangle it
/// reached, and the eraser takes paint away.

#include "Test_ProcessFramework/ProcessFrameworkSuiteSupport.h"
#include "Test_MaskDrawing/MaskDrawingTestSupport.h"

#include "controls/drawing/DrawingDocument.h"

#include <QPointF>
#include <QRect>

#include <gtest/gtest.h>

#include <memory>
#include <optional>

namespace
{

using an::tests::drawing::makeBrush;
using an::tests::drawing::makeDrawing;
using an::tests::drawing::maskAt;
using an::ui::drawing::BrushParams;
using an::ui::drawing::DrawingDocument;

/// A drawing case in an admitted operation of the suite's session.
class DrawingDocumentTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_scope = an::tests::viewer::admitted(an::tests::processes::framework::suiteContext().engineSession());
        ASSERT_TRUE(m_scope.has_value());
    }

    void TearDown() override
    {
        m_scope.reset();
    }

    [[nodiscard]] const an::consumer::EngineSession& session() const
    {
        return an::tests::processes::framework::suiteContext().engineSession();
    }

    /// Draws a stroke of one dab at a point; the first refusal, or success.
    [[nodiscard]] an::Status stroke(DrawingDocument& document, const BrushParams& brush, const QPointF& point) const
    {
        const an::Status begun = document.beginStroke(m_scope->execution(), brush);
        if (begun.failed())
        {
            return begun;
        }
        const an::Status added = document.addStrokePoint(m_scope->execution(), point, 1.0f);
        if (added.failed())
        {
            return added;
        }
        return document.endStroke(m_scope->execution());
    }

    std::optional<an::consumer::OperationScope> m_scope;
};

} // namespace

// A second stroke overlapping the first keeps the first stroke's paint where its own dabs are transparent, adds its
// own, and changes only the rectangle it reached.
TEST_F(DrawingDocumentTest, RoundBrushDoesNotErasePreviousMaskFromTransparentDirtyRectPixels)
{
    const std::shared_ptr<DrawingDocument> document = makeDrawing(session(), 32, 32);
    ASSERT_NE(document, nullptr);
    const BrushParams brush = makeBrush(9.0f);

    ASSERT_FALSE(stroke(*document, brush, QPointF(12.0, 12.0)).failed());
    ASSERT_GT(maskAt(*document, 12, 12), 0.9f);

    ASSERT_FALSE(stroke(*document, brush, QPointF(16.0, 16.0)).failed());
    EXPECT_GT(maskAt(*document, 12, 12), 0.9f);
    EXPECT_GT(maskAt(*document, 16, 16), 0.9f);
    const QRect changed = document->dirtyRect();
    EXPECT_TRUE(changed.contains(16, 16));
    EXPECT_LT(changed.width(), 32);
    EXPECT_LT(changed.height(), 32);
}

// The eraser takes away the paint under its dabs.
TEST_F(DrawingDocumentTest, EraserRemovesPreviouslyPaintedMask)
{
    const std::shared_ptr<DrawingDocument> document = makeDrawing(session(), 32, 32);
    ASSERT_NE(document, nullptr);
    const BrushParams brush = makeBrush(9.0f);
    ASSERT_FALSE(stroke(*document, brush, QPointF(12.0, 12.0)).failed());
    ASSERT_GT(maskAt(*document, 12, 12), 0.9f);

    BrushParams eraser = brush;
    eraser.blendMode = BrushParams::BlendMode::Erase;
    ASSERT_FALSE(stroke(*document, eraser, QPointF(12.0, 12.0)).failed());
    EXPECT_LT(maskAt(*document, 12, 12), 0.1f);
}
