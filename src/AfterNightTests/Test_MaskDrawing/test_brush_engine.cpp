// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The brush over the engine's drawing primitives: what a dab covers, how a stroke's segments fill the gap between its
// points, and how hardness, the tip shape, spacing and pressure change what lands in the stroke plane.

#include "Test_MaskDrawing/MaskDrawingTestSupport.h"

#include "controls/drawing/BrushEngine.h"

#include <gtest/gtest.h>

#include <cmath>
#include <optional>

namespace
{

using an::tests::drawing::admitted;
using an::tests::drawing::makeBrush;
using an::tests::drawing::openDrawingProvider;
using an::tests::drawing::TestSessionProvider;
using an::ui::drawing::BrushEngine;
using an::ui::drawing::BrushParams;
using an::ui::drawing::CoveragePlane;

class BrushEngineTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_provider = openDrawingProvider();
        ASSERT_TRUE(m_provider.has_value());
        m_scope = admitted(m_provider->engineSession());
        ASSERT_TRUE(m_scope.has_value());
    }

    void TearDown() override
    {
        m_scope.reset();
        m_provider.reset();
    }

    /// A stroke plane of a size, every sample zero.
    [[nodiscard]] CoveragePlane plane(int width, int height) const
    {
        an::Result<CoveragePlane> made = CoveragePlane::create(m_scope->engine(), static_cast<std::uint32_t>(width),
                                                               static_cast<std::uint32_t>(height));
        EXPECT_TRUE(made.hasValue()) << made.status().message();
        return made.hasValue() ? std::move(*made) : CoveragePlane{};
    }

    /// Adds a point and fails the case when the brush refuses it.
    [[nodiscard]] QRect add(BrushEngine& brush, CoveragePlane& stroke, double x, double y, float pressure = 1.0f) const
    {
        const an::Result<QRect> dirty =
            brush.addStrokePoint(m_scope->execution(), stroke.view(), QPointF(x, y), pressure);
        EXPECT_TRUE(dirty.hasValue()) << dirty.status().message();
        return dirty.hasValue() ? *dirty : QRect();
    }

    [[nodiscard]] static double sum(const CoveragePlane& stroke)
    {
        double total = 0.0;
        for (int y = 0; y < stroke.height(); ++y)
        {
            for (int x = 0; x < stroke.width(); ++x)
            {
                total += stroke.at(x, y);
            }
        }
        return total;
    }

    std::optional<TestSessionProvider> m_provider;
    std::optional<an::consumer::OperationScope> m_scope;
};

TEST_F(BrushEngineTest, RoundBrushDabMarksExpectedPixels)
{
    CoveragePlane stroke = plane(32, 32);
    BrushEngine brush;
    ASSERT_FALSE(brush.beginStroke(makeBrush(7.0f)).failed());

    const QRect dirty = add(brush, stroke, 10.0, 10.0);

    EXPECT_TRUE(dirty.contains(QPoint(10, 10)));
    EXPECT_EQ(dirty, QRect(7, 7, 7, 7));
    EXPECT_GT(stroke.at(10, 10), 0.0f);
    EXPECT_FLOAT_EQ(stroke.at(0, 0), 0.0f);
}

TEST_F(BrushEngineTest, LinearInterpolationFillsGapBetweenPoints)
{
    CoveragePlane stroke = plane(40, 40);
    BrushEngine brush;
    ASSERT_FALSE(brush.beginStroke(makeBrush(5.0f)).failed());

    const QRect initialDirty = add(brush, stroke, 8.0, 12.0);
    const QRect dirty = add(brush, stroke, 24.0, 12.0);

    EXPECT_TRUE(initialDirty.contains(QPoint(8, 12)));
    EXPECT_TRUE(dirty.contains(QPoint(16, 12)));
    EXPECT_GT(stroke.at(16, 12), 0.0f);
    EXPECT_GT(stroke.at(24, 12), 0.0f);
    EXPECT_EQ(brush.strokeBounds(), initialDirty.united(dirty));
}

TEST_F(BrushEngineTest, HardnessControlsEdgeFalloff)
{
    BrushParams hardBrush = makeBrush(9.0f);
    hardBrush.hardness = 1.0f;
    BrushParams softBrush = hardBrush;
    softBrush.hardness = 0.0f;

    CoveragePlane hardStroke = plane(40, 40);
    BrushEngine hard;
    ASSERT_FALSE(hard.beginStroke(hardBrush).failed());
    const QRect hardDirty = add(hard, hardStroke, 20.0, 20.0);

    CoveragePlane softStroke = plane(40, 40);
    BrushEngine soft;
    ASSERT_FALSE(soft.beginStroke(softBrush).failed());
    const QRect softDirty = add(soft, softStroke, 20.0, 20.0);

    EXPECT_TRUE(hardDirty.contains(QPoint(20, 20)));
    EXPECT_TRUE(softDirty.contains(QPoint(20, 20)));
    EXPECT_FLOAT_EQ(hardStroke.at(20, 20), 1.0f);
    EXPECT_FLOAT_EQ(softStroke.at(20, 20), 1.0f);
    EXPECT_GT(hardStroke.at(24, 20), softStroke.at(24, 20));
    EXPECT_GT(softStroke.at(24, 20), 0.0f);
    EXPECT_FLOAT_EQ(hardStroke.at(26, 20), 0.0f);
}

TEST_F(BrushEngineTest, SquareTipCoversCornersThatRoundTipMisses)
{
    BrushParams roundBrush = makeBrush(9.0f);
    roundBrush.tipShape = BrushParams::TipShape::Round;
    BrushParams squareBrush = roundBrush;
    squareBrush.tipShape = BrushParams::TipShape::Square;

    CoveragePlane roundStroke = plane(40, 40);
    BrushEngine round;
    ASSERT_FALSE(round.beginStroke(roundBrush).failed());
    const QRect roundDirty = add(round, roundStroke, 20.0, 20.0);

    CoveragePlane squareStroke = plane(40, 40);
    BrushEngine square;
    ASSERT_FALSE(square.beginStroke(squareBrush).failed());
    const QRect squareDirty = add(square, squareStroke, 20.0, 20.0);

    EXPECT_TRUE(roundDirty.contains(QPoint(20, 20)));
    EXPECT_TRUE(squareDirty.contains(QPoint(20, 20)));
    EXPECT_FLOAT_EQ(roundStroke.at(24, 24), 0.0f);
    EXPECT_GT(squareStroke.at(24, 24), 0.0f);
}

TEST_F(BrushEngineTest, TighterSpacingProducesDenserStrokeAccumulation)
{
    BrushParams tightSpacingBrush = makeBrush(7.0f);
    tightSpacingBrush.flow = 0.35f;
    tightSpacingBrush.spacing = 0.10f;
    BrushParams sparseSpacingBrush = tightSpacingBrush;
    sparseSpacingBrush.spacing = 1.0f;

    CoveragePlane tightStroke = plane(48, 32);
    BrushEngine tight;
    ASSERT_FALSE(tight.beginStroke(tightSpacingBrush).failed());
    const QRect tightStart = add(tight, tightStroke, 8.0, 12.0);
    const QRect tightEnd = add(tight, tightStroke, 32.0, 12.0);

    CoveragePlane sparseStroke = plane(48, 32);
    BrushEngine sparse;
    ASSERT_FALSE(sparse.beginStroke(sparseSpacingBrush).failed());
    const QRect sparseStart = add(sparse, sparseStroke, 8.0, 12.0);
    const QRect sparseEnd = add(sparse, sparseStroke, 32.0, 12.0);

    EXPECT_TRUE(tightStart.contains(QPoint(8, 12)));
    EXPECT_TRUE(tightEnd.contains(QPoint(32, 12)));
    EXPECT_TRUE(sparseStart.contains(QPoint(8, 12)));
    EXPECT_TRUE(sparseEnd.contains(QPoint(32, 12)));
    EXPECT_GT(sum(tightStroke), sum(sparseStroke));
}

// A dab lays the tip times the flow times the pressure: half the pressure is half of a hard tip's centre, and a
// pressure that is not a number lays nothing.
TEST_F(BrushEngineTest, PressureScalesADabAndOneThatIsNotANumberLaysNothing)
{
    CoveragePlane full = plane(20, 20);
    BrushEngine fullBrush;
    ASSERT_FALSE(fullBrush.beginStroke(makeBrush(5.0f)).failed());
    static_cast<void>(add(fullBrush, full, 10.0, 10.0, 1.0f));

    CoveragePlane half = plane(20, 20);
    BrushEngine halfBrush;
    ASSERT_FALSE(halfBrush.beginStroke(makeBrush(5.0f)).failed());
    static_cast<void>(add(halfBrush, half, 10.0, 10.0, 0.5f));

    CoveragePlane none = plane(20, 20);
    BrushEngine noneBrush;
    ASSERT_FALSE(noneBrush.beginStroke(makeBrush(5.0f)).failed());
    const QRect noneDirty = add(noneBrush, none, 10.0, 10.0, std::nanf(""));

    EXPECT_FLOAT_EQ(full.at(10, 10), 1.0f);
    EXPECT_FLOAT_EQ(half.at(10, 10), 0.5f);
    EXPECT_TRUE(noneDirty.isEmpty());
    EXPECT_FLOAT_EQ(none.at(10, 10), 0.0f);
}

// A point added with no stroke begun is refused, and the plane is left as it was; a brush the engine cannot plan
// starts no stroke.
TEST_F(BrushEngineTest, APointWithNoStrokeIsRefusedAndABrushTheEngineRefusesStartsNone)
{
    CoveragePlane stroke = plane(16, 16);
    BrushEngine brush;
    const an::Result<QRect> refused = brush.addStrokePoint(m_scope->execution(), stroke.view(), QPointF(8.0, 8.0));
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidState);
    EXPECT_FLOAT_EQ(stroke.at(8, 8), 0.0f);

    BrushParams unplannable = makeBrush(5.0f);
    unplannable.hardness = std::nanf("");
    EXPECT_TRUE(brush.beginStroke(unplannable).failed());
    EXPECT_FALSE(brush.hasActiveStroke());
}

} // anonymous namespace
