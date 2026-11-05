// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The shape engine: geometry, hit-testing and handles stay with the toolkit; a shape's coverage is a layer over the
// rectangle it can reach, rasterized by the toolkit and feathered by the engine's Gaussian.

#include "Test_MaskDrawing/MaskDrawingTestSupport.h"

#include "controls/drawing/ShapeEngine.h"

#include <gtest/gtest.h>

#include <cmath>
#include <optional>

namespace
{

using an::tests::drawing::admitted;
using an::tests::drawing::openDrawingProvider;
using an::tests::drawing::TestSessionProvider;
using an::ui::drawing::ShapeEngine;
using an::ui::drawing::ShapeLayer;
using an::ui::drawing::ShapeObject;

ShapeObject makeFilledRectangle()
{
    ShapeObject shape;
    shape.type = ShapeObject::Type::Rectangle;
    shape.center = QPointF(10.0, 12.0);
    shape.data = ShapeObject::RectData{QSizeF(8.0, 6.0), 0.0f};
    shape.opacity = 1.0f;
    shape.feather = 0.0f;
    shape.filled = true;
    return shape;
}

class ShapeEngineTest : public ::testing::Test
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

    [[nodiscard]] ShapeLayer render(const ShapeObject& shape, int width, int height) const
    {
        an::Result<ShapeLayer> layer = ShapeEngine::renderShapeLayer(m_scope->execution(), shape, width, height);
        EXPECT_TRUE(layer.hasValue()) << layer.status().message();
        return layer.hasValue() ? std::move(*layer) : ShapeLayer{};
    }

    std::optional<TestSessionProvider> m_provider;
    std::optional<an::consumer::OperationScope> m_scope;
};

TEST_F(ShapeEngineTest, CircleHitTestingAndHandleMovementUpdateGeometry)
{
    ShapeObject shape;
    shape.type = ShapeObject::Type::Circle;
    shape.center = QPointF(20.0, 20.0);
    shape.data = ShapeObject::CircleData{5.0f};
    shape.opacity = 1.0f;
    shape.feather = 0.0f;
    shape.filled = true;

    const QRectF bounds = ShapeEngine::boundingRect(shape);
    EXPECT_NEAR(bounds.left(), 15.0, 0.01);
    EXPECT_NEAR(bounds.top(), 15.0, 0.01);
    EXPECT_NEAR(bounds.width(), 10.0, 0.01);
    EXPECT_NEAR(bounds.height(), 10.0, 0.01);
    EXPECT_TRUE(ShapeEngine::hitTest(shape, QPointF(20.0, 20.0)));
    EXPECT_FALSE(ShapeEngine::hitTest(shape, QPointF(27.0, 20.0)));

    const int handleIndex = ShapeEngine::hitTestHandle(shape, QPointF(25.0, 20.0), 1.5);
    ASSERT_GE(handleIndex, 0);

    ShapeEngine::moveHandle(shape, handleIndex, QPointF(30.0, 20.0));
    const auto* circleData = std::get_if<ShapeObject::CircleData>(&shape.data);
    ASSERT_NE(circleData, nullptr);
    EXPECT_NEAR(circleData->radius, 10.0f, 0.01f);
}

TEST_F(ShapeEngineTest, RectangleRasterizationMatchesExpectedExtents)
{
    const ShapeLayer raster = render(makeFilledRectangle(), 32, 32);

    EXPECT_GT(raster.at(10, 12), 0.9f);
    EXPECT_GT(raster.at(13, 14), 0.9f);
    EXPECT_FLOAT_EQ(raster.at(2, 2), 0.0f);
    EXPECT_FLOAT_EQ(raster.at(25, 25), 0.0f);
    // The layer covers the rectangle and its antialiased edge, not the image.
    EXPECT_TRUE(raster.bounds.contains(QRect(6, 9, 8, 6)));
    EXPECT_LT(raster.bounds.width(), 32);
}

TEST_F(ShapeEngineTest, RectangleEdgesExposeStretchHandlesAcrossTheWholeEdge)
{
    ShapeObject shape = makeFilledRectangle();

    const auto handles = ShapeEngine::handlePoints(shape);
    ASSERT_EQ(handles.size(), 4u);
    EXPECT_NEAR(handles[0].x(), 14.0, 0.01);
    EXPECT_NEAR(handles[0].y(), 12.0, 0.01);
    EXPECT_NEAR(handles[1].x(), 10.0, 0.01);
    EXPECT_NEAR(handles[1].y(), 9.0, 0.01);
    EXPECT_NEAR(handles[2].x(), 6.0, 0.01);
    EXPECT_NEAR(handles[2].y(), 12.0, 0.01);
    EXPECT_NEAR(handles[3].x(), 10.0, 0.01);
    EXPECT_NEAR(handles[3].y(), 15.0, 0.01);

    EXPECT_EQ(ShapeEngine::hitTestHandle(shape, QPointF(13.5, 9.0), 0.75), 1);

    ShapeEngine::moveHandle(shape, 0, QPointF(18.0, 12.0));
    const auto* rectData = std::get_if<ShapeObject::RectData>(&shape.data);
    ASSERT_NE(rectData, nullptr);
    EXPECT_NEAR(rectData->size.width(), 12.0, 0.01);
    EXPECT_NEAR(rectData->size.height(), 6.0, 0.01);
    EXPECT_NEAR(shape.center.x(), 12.0, 0.01);
    EXPECT_NEAR(shape.center.y(), 12.0, 0.01);
}

TEST_F(ShapeEngineTest, FeatherSoftensShapeEdges)
{
    ShapeObject shape;
    shape.type = ShapeObject::Type::Circle;
    shape.center = QPointF(20.0, 20.0);
    shape.data = ShapeObject::CircleData{6.0f};
    shape.opacity = 1.0f;
    shape.feather = 3.0f;
    shape.filled = true;

    const ShapeLayer raster = render(shape, 40, 40);

    EXPECT_GT(raster.at(20, 20), 0.9f);
    EXPECT_GT(raster.at(26, 20), 0.0f);
    EXPECT_LT(raster.at(26, 20), 1.0f);
    EXPECT_FLOAT_EQ(raster.at(34, 20), 0.0f);
}

TEST_F(ShapeEngineTest, RotationHelperAccumulatesDegrees)
{
    ShapeObject shape = makeFilledRectangle();
    EXPECT_FLOAT_EQ(shape.rotation, 0.0f);

    ShapeEngine::rotate(shape, 37.5f);
    EXPECT_NEAR(shape.rotation, 37.5f, 0.01f);

    ShapeEngine::rotate(shape, -52.5f);
    EXPECT_NEAR(shape.rotation, -15.0f, 0.01f);
}

TEST_F(ShapeEngineTest, PolygonAndLineShapesExposeBoundingAndVertexHandles)
{
    ShapeObject polygon;
    polygon.type = ShapeObject::Type::Polygon;
    polygon.data = ShapeObject::PolyData{{QPointF(5.0, 5.0), QPointF(12.0, 6.0), QPointF(9.0, 14.0)}};
    polygon.opacity = 1.0f;
    polygon.filled = true;

    const QRectF polygonBounds = ShapeEngine::boundingRect(polygon);
    EXPECT_NEAR(polygonBounds.left(), 5.0, 0.01);
    EXPECT_NEAR(polygonBounds.top(), 5.0, 0.01);
    EXPECT_NEAR(polygonBounds.right(), 12.0, 0.01);
    EXPECT_NEAR(polygonBounds.bottom(), 14.0, 0.01);
    EXPECT_TRUE(ShapeEngine::hitTest(polygon, QPointF(9.0, 8.0)));

    ShapeObject line;
    line.type = ShapeObject::Type::Line;
    line.data = ShapeObject::LineData{{QPointF(2.0, 2.0), QPointF(8.0, 8.0), QPointF(14.0, 5.0)}, 2.0f};
    line.opacity = 1.0f;
    line.filled = false;

    const int handleIndex = ShapeEngine::hitTestHandle(line, QPointF(8.0, 8.0), 1.5);
    ASSERT_EQ(handleIndex, 1);

    ShapeEngine::moveHandle(line, handleIndex, QPointF(10.0, 10.0));
    const auto* lineData = std::get_if<ShapeObject::LineData>(&line.data);
    ASSERT_NE(lineData, nullptr);
    ASSERT_EQ(lineData->points.size(), 3u);
    EXPECT_NEAR(lineData->points[1].x(), 10.0, 0.01);
    EXPECT_NEAR(lineData->points[1].y(), 10.0, 0.01);
}

// A layer is a whole-image raster cut to where it is not zero: the same shape rasterized over the whole image and
// feathered there agrees with its layer sample for sample, inside the layer and out, at every feather width up to the
// product's widest - one Gaussian pass and several.
TEST_F(ShapeEngineTest, ALayerIsTheWholeImageRasterCutToWhereItIsNotZero)
{
    for (const float feather : {0.0f, 1.0f, 3.0f, 9.0f, 100.0f})
    {
        ShapeObject shape;
        shape.type = ShapeObject::Type::Ellipse;
        shape.center = QPointF(70.3, 64.6);
        shape.data = ShapeObject::EllipseData{9.5f, 6.25f};
        shape.rotation = 25.0f;
        shape.feather = feather;
        shape.filled = true;

        const int width = 160;
        const int height = 140;
        const ShapeLayer layer = render(shape, width, height);
        ShapeLayer whole = ShapeEngine::rasterizeShapeCoverage(shape, QRect(0, 0, width, height));
        ASSERT_FALSE(ShapeEngine::featherShapeLayer(m_scope->execution(), whole, shape.feather).failed());

        float largest = 0.0f;
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                largest = (std::max)(largest, std::abs(layer.at(x, y) - whole.at(x, y)));
            }
        }
        EXPECT_EQ(largest, 0.0f) << "feather " << feather;
    }
}

// A shape feathered by one pixel softens its edge rather than leaving it as hard as it was drawn.
TEST_F(ShapeEngineTest, AFeatherOfOnePixelSoftensTheEdge)
{
    ShapeObject hard = makeFilledRectangle();
    hard.center = QPointF(16.0, 16.0);
    hard.data = ShapeObject::RectData{QSizeF(10.0, 10.0), 0.0f};
    ShapeObject thin = hard;
    thin.feather = 1.0f;

    const ShapeLayer drawn = render(hard, 32, 32);
    const ShapeLayer feathered = render(thin, 32, 32);

    // The rectangle runs from 11 to 21: the first pixel outside it has no coverage drawn and some feathered, and the
    // last pixel inside loses some.
    EXPECT_FLOAT_EQ(drawn.at(10, 16), 0.0f);
    EXPECT_GT(feathered.at(10, 16), 0.01f);
    EXPECT_FLOAT_EQ(drawn.at(11, 16), 1.0f);
    EXPECT_LT(feathered.at(11, 16), 0.99f);
}

// Screening a layer over a plane moves each covered pixel by the opacity towards the screen and leaves an uncovered one
// as it was; a rectangle outside the clip is not written.
TEST_F(ShapeEngineTest, ScreeningALayerMovesCoveredPixelsByItsOpacityAndOnlyInsideTheClip)
{
    an::Result<an::ui::drawing::CoveragePlane> made =
        an::ui::drawing::CoveragePlane::create(m_scope->engine(), 32u, 32u);
    ASSERT_TRUE(made.hasValue());
    an::ui::drawing::CoveragePlane composite = std::move(*made);
    an::ui::drawing::fillPlane(composite.view(), 0.5f);
    const ShapeLayer layer = render(makeFilledRectangle(), 32, 32);

    ASSERT_FALSE(ShapeEngine::screenShapeLayer(m_scope->execution(), layer, 0.5f, composite.view(), QRect(0, 0, 11, 32))
                     .failed());
    // Inside the rectangle and the clip: 0.5 moved half way to the screen of 0.5 and 1.
    EXPECT_FLOAT_EQ(composite.at(10, 12), 0.75f);
    // Inside the rectangle, outside the clip.
    EXPECT_FLOAT_EQ(composite.at(12, 12), 0.5f);
    // Outside the rectangle.
    EXPECT_FLOAT_EQ(composite.at(2, 2), 0.5f);
}

} // anonymous namespace
