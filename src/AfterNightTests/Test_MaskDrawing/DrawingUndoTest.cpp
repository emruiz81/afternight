// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The drawing document's undo: steps that keep the paint a change touched and the shape list it replaced, admitted
// against the session's undo allowance before they are kept, the oldest dropped first, at most a hundred, and a
// shape's feathered layer kept with its step rather than drawn again.

#include "Test_MaskDrawing/MaskDrawingTestSupport.h"

#include "controls/drawing/DrawingDocument.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>

namespace
{

using an::tests::drawing::admitted;
using an::tests::drawing::greyAsset;
using an::tests::drawing::importInto;
using an::tests::drawing::makeBrush;
using an::tests::drawing::makeDrawing;
using an::tests::drawing::makeRectangle;
using an::tests::drawing::maskAt;
using an::tests::drawing::openDrawingProvider;
using an::tests::drawing::TestSessionProvider;
using an::tests::drawing::undoBytesHeld;
using an::ui::drawing::BrushParams;
using an::ui::drawing::DrawingDocument;

/// The bytes a step of a rectangle's paint holds: its samples before and after.
[[nodiscard]] std::uint64_t patchBytes(int width, int height)
{
    return 2u * static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * sizeof(float);
}

class DrawingUndoTest : public ::testing::Test
{
protected:
    void open(const an::consumer::MemoryAllowances& allowances = {})
    {
        m_provider = openDrawingProvider(2u, allowances);
        ASSERT_TRUE(m_provider.has_value());
        m_scope = admitted(m_provider->engineSession());
        ASSERT_TRUE(m_scope.has_value());
    }

    void SetUp() override
    {
        open();
    }

    void TearDown() override
    {
        m_scope.reset();
        m_provider.reset();
    }

    [[nodiscard]] const an::consumer::EngineSession& session() const
    {
        return m_provider->engineSession();
    }

    [[nodiscard]] const an::ExecutionContext& execution() const
    {
        return m_scope->execution();
    }

    [[nodiscard]] an::Status stroke(DrawingDocument& document, const BrushParams& brush,
                                    std::initializer_list<QPointF> points, float pressure = 1.0f)
    {
        const an::Status begun = document.beginStroke(execution(), brush);
        if (begun.failed())
        {
            return begun;
        }
        for (const QPointF& point : points)
        {
            const an::Status added = document.addStrokePoint(execution(), point, pressure);
            if (added.failed())
            {
                return added;
            }
        }
        return document.endStroke(execution());
    }

    std::optional<TestSessionProvider> m_provider;
    std::optional<an::consumer::OperationScope> m_scope;
};

// A stroke's step holds the rectangle its dabs reached, before and after, and reserves exactly that against the undo
// allowance - not a copy of the whole paint.
TEST_F(DrawingUndoTest, AStrokeKeepsOnlyThePatchItChanged)
{
    const std::shared_ptr<DrawingDocument> document = makeDrawing(session(), 200, 160);
    ASSERT_NE(document, nullptr);
    ASSERT_FALSE(stroke(*document, makeBrush(7.0f), {QPointF(20.0, 30.0), QPointF(40.0, 30.0)}).failed());

    // A seven-pixel tip from x = 17 to 43 and y = 27 to 33.
    const std::uint64_t expected = patchBytes(27, 7);
    EXPECT_EQ(document->counters().undoSteps, 1u);
    EXPECT_EQ(document->counters().undoBytes, expected);
    EXPECT_EQ(undoBytesHeld(session()), expected);

    ASSERT_FALSE(document->undo(execution()).failed());
    EXPECT_FLOAT_EQ(maskAt(*document, 30, 30), 0.0f);
    ASSERT_FALSE(document->redo(execution()).failed());
    EXPECT_FLOAT_EQ(maskAt(*document, 30, 30), 1.0f);
}

// Adding, moving and removing a shape keeps no paint at all.
TEST_F(DrawingUndoTest, AShapeEditKeepsNoPaint)
{
    const std::shared_ptr<DrawingDocument> document = makeDrawing(session(), 300, 300);
    ASSERT_NE(document, nullptr);
    an::ui::drawing::ShapeObject shape = makeRectangle(50.0, 50.0, 20.0, 20.0);
    ASSERT_TRUE(document->addShape(execution(), shape).hasValue());
    shape.center = QPointF(120.0, 80.0);
    ASSERT_FALSE(document->updateShape(execution(), 0u, shape).failed());
    ASSERT_FALSE(document->removeShape(execution(), 0u).failed());

    EXPECT_EQ(document->counters().undoSteps, 3u);
    EXPECT_EQ(document->counters().undoBytes, 0u);
    EXPECT_EQ(undoBytesHeld(session()), 0u);

    ASSERT_FALSE(document->undo(execution()).failed());
    ASSERT_EQ(document->shapes().size(), 1u);
    EXPECT_GT(maskAt(*document, 120, 80), 0.9f);
    EXPECT_FLOAT_EQ(maskAt(*document, 50, 50), 0.0f);
}

// A flood fill's step holds the rectangle the fill changed, from an imported mask whose own step holds it all.
TEST_F(DrawingUndoTest, AFillKeepsOnlyTheRectangleItChanged)
{
    const std::shared_ptr<DrawingDocument> document = makeDrawing(session(), 64, 48);
    ASSERT_NE(document, nullptr);
    std::optional<an::consumer::ImageAsset> imported =
        greyAsset(session(), 64, 48, [](std::uint32_t x, std::uint32_t y) {
            return x >= 10u && x < 20u && y >= 5u && y < 12u ? 0.0f : 1.0f;
        });
    ASSERT_TRUE(imported.has_value());
    importInto(*document, execution(), *imported);
    const std::uint64_t importBytes = patchBytes(64, 48);
    EXPECT_EQ(document->counters().undoBytes, importBytes);

    ASSERT_FALSE(document->floodFill(execution(), QPoint(12, 8), 0.5f, 0.01f).failed());
    EXPECT_EQ(document->dirtyRect(), QRect(10, 5, 10, 7));
    EXPECT_EQ(document->counters().undoBytes, importBytes + patchBytes(10, 7));
    EXPECT_EQ(undoBytesHeld(session()), importBytes + patchBytes(10, 7));
}

// Clearing keeps the rectangle the paint covered, not the whole paint.
TEST_F(DrawingUndoTest, ClearKeepsOnlyThePaintedRectangle)
{
    const std::shared_ptr<DrawingDocument> document = makeDrawing(session(), 200, 200);
    ASSERT_NE(document, nullptr);
    ASSERT_FALSE(stroke(*document, makeBrush(5.0f), {QPointF(100.0, 100.0)}).failed());
    const std::uint64_t strokeBytes = document->counters().undoBytes;
    ASSERT_FALSE(document->clear(execution()).failed());
    EXPECT_EQ(document->counters().undoBytes, strokeBytes + patchBytes(5, 5));
    EXPECT_TRUE(document->isEmpty());
    ASSERT_FALSE(document->undo(execution()).failed());
    EXPECT_FLOAT_EQ(maskAt(*document, 100, 100), 1.0f);
}

// A shape's feathered layer is drawn once when the shape is made or changed and is kept with the steps: undoing and
// redoing a shape edit draws nothing, and neither does a stroke over a mask with shapes on it, whose rebuild screens
// the kept layer only where the stroke landed.
TEST_F(DrawingUndoTest, AShapesLayerIsDrawnOnceAndKeptWithItsSteps)
{
    const std::shared_ptr<DrawingDocument> document = makeDrawing(session(), 120, 90);
    ASSERT_NE(document, nullptr);
    an::ui::drawing::ShapeObject shape = makeRectangle(40.0, 40.0, 30.0, 20.0, 4.0f, 0.5f);
    ASSERT_TRUE(document->addShape(execution(), shape).hasValue());
    EXPECT_EQ(document->counters().shapeLayersRendered, 1u);
    shape.center = QPointF(70.0, 45.0);
    ASSERT_FALSE(document->updateShape(execution(), 0u, shape).failed());
    EXPECT_EQ(document->counters().shapeLayersRendered, 2u);

    ASSERT_FALSE(document->undo(execution()).failed());
    ASSERT_FALSE(document->redo(execution()).failed());
    ASSERT_FALSE(document->undo(execution()).failed());
    EXPECT_EQ(document->counters().shapeLayersRendered, 2u);
    EXPECT_NEAR(maskAt(*document, 40, 40), 0.5f, 1.0e-6f);
    // The kept layer is the feathered one: the pixel left of the rectangle's edge is covered.
    EXPECT_GT(maskAt(*document, 24, 40), 0.05f);

    // A half-opacity screened shape under a half-flow stroke: 1 - (1 - 0.5) * (1 - 0.5).
    BrushParams half = makeBrush(3.0f);
    half.flow = 0.5f;
    ASSERT_FALSE(stroke(*document, half, {QPointF(40.0, 40.0)}).failed());
    EXPECT_EQ(document->counters().shapeLayersRendered, 2u);
    EXPECT_NEAR(maskAt(*document, 40, 40), 0.75f, 1.0e-6f);
    EXPECT_NEAR(maskAt(*document, 45, 40), 0.5f, 1.0e-6f);
}

// An edit whose step the undo allowance cannot hold, even with every other step dropped, is not made.
TEST_F(DrawingUndoTest, AnEditWhoseStepCannotBeAdmittedIsNotMade)
{
    an::consumer::MemoryAllowances allowances;
    allowances.historyCopies.bytes = 1024u;
    TearDown();
    open(allowances);
    const std::shared_ptr<DrawingDocument> document = makeDrawing(session(), 32, 32);
    ASSERT_NE(document, nullptr);
    const std::uint64_t revision = document->revision();

    const an::Status refused = document->applyLinearGradient(execution(), QPointF(0.0, 0.0), QPointF(31.0, 0.0), 0.0f,
                                                             1.0f, BrushParams::BlendMode::Normal);
    ASSERT_TRUE(refused.failed());
    EXPECT_EQ(refused.code(), an::ErrorCode::MemoryBudgetExceeded);
    EXPECT_EQ(document->revision(), revision);
    EXPECT_TRUE(document->isEmpty());
    EXPECT_FALSE(document->canUndo());
}

// A stroke whose step cannot be admitted is taken back when it ends: the mask is as it was before it, and no step is
// kept.
TEST_F(DrawingUndoTest, AStrokeWhoseStepCannotBeAdmittedIsTakenBack)
{
    an::consumer::MemoryAllowances allowances;
    allowances.historyCopies.bytes = 64u;
    TearDown();
    open(allowances);
    const std::shared_ptr<DrawingDocument> document = makeDrawing(session(), 32, 32);
    ASSERT_NE(document, nullptr);
    ASSERT_FALSE(document->beginStroke(execution(), makeBrush(7.0f)).failed());
    ASSERT_FALSE(document->addStrokePoint(execution(), QPointF(16.0, 16.0)).failed());
    EXPECT_FLOAT_EQ(maskAt(*document, 16, 16), 1.0f);

    const an::Status ended = document->endStroke(execution());
    ASSERT_TRUE(ended.failed());
    EXPECT_EQ(ended.code(), an::ErrorCode::MemoryBudgetExceeded);
    EXPECT_FALSE(document->hasActiveStroke());
    EXPECT_FLOAT_EQ(maskAt(*document, 16, 16), 0.0f);
    EXPECT_FALSE(document->canUndo());
    EXPECT_EQ(undoBytesHeld(session()), 0u);
}

// Older steps go first to make room: three strokes each reserving one patch under an allowance of two keep the last
// two, and the first is beyond undo.
TEST_F(DrawingUndoTest, OlderStepsAreDroppedToMakeRoomBeforeAnEditIsRefused)
{
    an::consumer::MemoryAllowances allowances;
    allowances.historyCopies.bytes = 2u * patchBytes(5, 5) + 8u;
    TearDown();
    open(allowances);
    const std::shared_ptr<DrawingDocument> document = makeDrawing(session(), 40, 20);
    ASSERT_NE(document, nullptr);
    for (const double x : {5.0, 15.0, 25.0})
    {
        ASSERT_FALSE(stroke(*document, makeBrush(5.0f), {QPointF(x, 10.0)}).failed());
    }
    EXPECT_EQ(document->counters().undoSteps, 2u);
    EXPECT_LE(undoBytesHeld(session()), allowances.historyCopies.bytes);

    ASSERT_FALSE(document->undo(execution()).failed());
    ASSERT_FALSE(document->undo(execution()).failed());
    EXPECT_FALSE(document->canUndo());
    EXPECT_FLOAT_EQ(maskAt(*document, 5, 10), 1.0f);
    EXPECT_FLOAT_EQ(maskAt(*document, 15, 10), 0.0f);
    EXPECT_FLOAT_EQ(maskAt(*document, 25, 10), 0.0f);
}

// At most a hundred steps are kept, the oldest dropped first.
TEST_F(DrawingUndoTest, NoMoreThanAHundredStepsAreKept)
{
    const std::shared_ptr<DrawingDocument> document = makeDrawing(session(), 110, 3);
    ASSERT_NE(document, nullptr);
    for (int x = 0; x < 105; ++x)
    {
        ASSERT_FALSE(stroke(*document, makeBrush(1.0f), {QPointF(x, 1.0)}).failed());
    }
    EXPECT_EQ(document->counters().undoSteps, an::ui::drawing::kMaxDrawingUndoDepth);
    for (std::size_t step = 0u; step < an::ui::drawing::kMaxDrawingUndoDepth; ++step)
    {
        ASSERT_FALSE(document->undo(execution()).failed());
    }
    EXPECT_FALSE(document->canUndo());
    EXPECT_FLOAT_EQ(maskAt(*document, 4, 1), 1.0f);
    EXPECT_FLOAT_EQ(maskAt(*document, 5, 1), 0.0f);
}

// A new step discards the steps that were undone.
TEST_F(DrawingUndoTest, ANewStepDiscardsTheRedoSteps)
{
    const std::shared_ptr<DrawingDocument> document = makeDrawing(session(), 30, 10);
    ASSERT_NE(document, nullptr);
    ASSERT_FALSE(stroke(*document, makeBrush(3.0f), {QPointF(5.0, 5.0)}).failed());
    ASSERT_FALSE(stroke(*document, makeBrush(3.0f), {QPointF(15.0, 5.0)}).failed());
    ASSERT_FALSE(document->undo(execution()).failed());
    EXPECT_TRUE(document->canRedo());
    ASSERT_FALSE(stroke(*document, makeBrush(3.0f), {QPointF(25.0, 5.0)}).failed());
    EXPECT_FALSE(document->canRedo());
    EXPECT_EQ(document->counters().undoSteps, 2u);
    EXPECT_FLOAT_EQ(maskAt(*document, 15, 5), 0.0f);
}

// Erasing over shapes flattens them into the paint first, so the eraser erases what the mask shows; undoing the stroke
// brings the shapes back as shapes, and redoing it flattens them again.
TEST_F(DrawingUndoTest, ErasingOverShapesFlattensThemAndItsUndoRestoresThem)
{
    const std::shared_ptr<DrawingDocument> document = makeDrawing(session(), 60, 40);
    ASSERT_NE(document, nullptr);
    ASSERT_TRUE(document->addShape(execution(), makeRectangle(30.0, 20.0, 40.0, 20.0)).hasValue());
    EXPECT_FLOAT_EQ(maskAt(*document, 30, 20), 1.0f);

    BrushParams eraser = makeBrush(5.0f);
    eraser.blendMode = BrushParams::BlendMode::Erase;
    ASSERT_FALSE(stroke(*document, eraser, {QPointF(30.0, 20.0)}).failed());
    EXPECT_TRUE(document->shapes().empty());
    EXPECT_FLOAT_EQ(maskAt(*document, 30, 20), 0.0f);
    EXPECT_FLOAT_EQ(maskAt(*document, 15, 20), 1.0f);

    ASSERT_FALSE(document->undo(execution()).failed());
    ASSERT_EQ(document->shapes().size(), 1u);
    EXPECT_FLOAT_EQ(maskAt(*document, 30, 20), 1.0f);
    EXPECT_FLOAT_EQ(maskAt(*document, 15, 20), 1.0f);

    ASSERT_FALSE(document->redo(execution()).failed());
    EXPECT_TRUE(document->shapes().empty());
    EXPECT_FLOAT_EQ(maskAt(*document, 30, 20), 0.0f);
    // The shape away from the stroke is in the paint now, which the redo rebuilt the mask from.
    EXPECT_FLOAT_EQ(maskAt(*document, 15, 20), 1.0f);
}

// An erase that lays nothing - a stroke at no pressure - leaves the shapes as shapes and keeps no step.
TEST_F(DrawingUndoTest, AnEraseThatLaysNothingLeavesTheShapes)
{
    const std::shared_ptr<DrawingDocument> document = makeDrawing(session(), 40, 40);
    ASSERT_NE(document, nullptr);
    ASSERT_TRUE(document->addShape(execution(), makeRectangle(20.0, 20.0, 10.0, 10.0)).hasValue());
    const std::uint64_t steps = document->counters().undoSteps;

    BrushParams eraser = makeBrush(5.0f);
    eraser.blendMode = BrushParams::BlendMode::Erase;
    ASSERT_FALSE(stroke(*document, eraser, {QPointF(20.0, 20.0)}, 0.0f).failed());
    EXPECT_EQ(document->shapes().size(), 1u);
    EXPECT_EQ(document->counters().undoSteps, steps);
    EXPECT_FLOAT_EQ(maskAt(*document, 20, 20), 1.0f);
}

// A flood fill over shapes flattens them into the paint first, as an eraser does, and fills the region of the mask as
// it showed; undoing the fill brings the shapes back as shapes.
TEST_F(DrawingUndoTest, AFillOverShapesFlattensThemAndItsUndoRestoresThem)
{
    const std::shared_ptr<DrawingDocument> document = makeDrawing(session(), 40, 30);
    ASSERT_NE(document, nullptr);
    ASSERT_TRUE(document->addShape(execution(), makeRectangle(20.0, 15.0, 10.0, 10.0)).hasValue());

    ASSERT_FALSE(document->floodFill(execution(), QPoint(2, 2), 0.25f, 0.01f).failed());
    EXPECT_TRUE(document->shapes().empty());
    EXPECT_FLOAT_EQ(maskAt(*document, 2, 2), 0.25f);
    EXPECT_FLOAT_EQ(maskAt(*document, 20, 15), 1.0f);
    EXPECT_EQ(document->counters().undoBytes, undoBytesHeld(session()));

    ASSERT_FALSE(document->undo(execution()).failed());
    ASSERT_EQ(document->shapes().size(), 1u);
    EXPECT_FLOAT_EQ(maskAt(*document, 2, 2), 0.0f);
    EXPECT_FLOAT_EQ(maskAt(*document, 20, 15), 1.0f);
}

// Each stroke lays its own dabs: a second half-flow stroke over the first lands half coverage against what the first
// left, not the two strokes' coverage together.
TEST_F(DrawingUndoTest, EachStrokeLandsOnlyItsOwnDabs)
{
    const std::shared_ptr<DrawingDocument> document = makeDrawing(session(), 20, 20);
    ASSERT_NE(document, nullptr);
    BrushParams half = makeBrush(3.0f);
    half.flow = 0.5f;
    half.opacity = 0.5f;
    ASSERT_FALSE(stroke(*document, half, {QPointF(10.0, 10.0)}).failed());
    EXPECT_FLOAT_EQ(maskAt(*document, 10, 10), 0.25f);
    ASSERT_FALSE(stroke(*document, half, {QPointF(10.0, 10.0)}).failed());
    EXPECT_FLOAT_EQ(maskAt(*document, 10, 10), 0.375f);
}

} // anonymous namespace
