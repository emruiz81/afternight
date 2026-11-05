// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The drawing tools' vector shapes: their geometry, hit-testing and editing handles, which stay with the toolkit, and
/// the coverage a shape lays on a mask, which the toolkit rasterizes and the engine feathers and screens.
///
/// A shape's coverage is kept as a layer over only the rectangle of the image it can reach - its painted outline,
/// widened by the reach of its feather - so a mask with a shape on it is not re-rasterized over the whole image every
/// time anything changes: the document keeps one feathered layer per shape and screens it over the paint inside the
/// rectangle that changed. The feather is the engine's exact Gaussian of half the feather width
/// (`featherCoverage`), and a layer's rectangle is wide enough that it holds every sample the blur of the whole image
/// would make non-zero, so a layer is what a whole-image raster would have been, cut to where it is not zero.
#pragma once

#include "AfterNightControlsExport.h"
#include "controls/drawing/DrawingTypes.h"

#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/PlaneView.h>

#include <QPainterPath>
#include <QRect>

#include <cstdint>
#include <optional>
#include <vector>

namespace an
{
class ExecutionContext;
} // namespace an

namespace an::ui::drawing
{

/// A shape's coverage over the rectangle of the image it can reach.
struct AFTERNIGHTCONTROLS_API ShapeLayer
{
    /// Where the layer lies, in image pixels and inside the image; empty for a shape that reaches no pixel.
    QRect bounds;

    /// One coverage sample per pixel of `bounds`, row by row and tightly packed.
    std::vector<float> coverage;

    /// The coverage at an image pixel: zero outside the layer.
    ///
    /// @param x the column, in image pixels.
    /// @param y the row, in image pixels.
    /// @return the coverage.
    [[nodiscard]] float at(int x, int y) const noexcept;

    /// The coverage as a plane, or no value for an empty layer.
    [[nodiscard]] std::optional<an::PlaneView<const float>> plane() const noexcept;

    /// The bytes the coverage holds.
    [[nodiscard]] std::uint64_t bytes() const noexcept;
};

class AFTERNIGHTCONTROLS_API ShapeEngine
{
public:
    [[nodiscard]] static QRectF boundingRect(const ShapeObject& shape);
    [[nodiscard]] static bool hitTest(const ShapeObject& shape, const QPointF& imagePos, double tolerance = 0.0);
    [[nodiscard]] static int hitTestHandle(const ShapeObject& shape, const QPointF& imagePos, double tolerance = 6.0);
    [[nodiscard]] static std::vector<QPointF> handlePoints(const ShapeObject& shape);
    static void translate(ShapeObject& shape, const QPointF& delta);
    static void rotate(ShapeObject& shape, float deltaDegrees);
    static void moveHandle(ShapeObject& shape, int handleIndex, const QPointF& imagePos);

    [[nodiscard]] static ShapeObject makeDragShape(ShapeObject::Type type, const QPointF& anchor,
                                                   const QPointF& current, float opacity, float feather, bool filled,
                                                   float lineWidth = 1.0f);
    [[nodiscard]] static ShapeObject makePathShape(ShapeObject::Type type, const std::vector<QPointF>& points,
                                                   float opacity, float feather, bool filled, float lineWidth = 1.0f);

    [[nodiscard]] static QPainterPath buildPainterPath(const ShapeObject& shape);

    /// The rectangle of an image a shape's feathered coverage can reach: its outline's bounds with the pen and the
    /// toolkit's antialiasing, widened by the reach of the feather's blur and once more by that reach, so that every
    /// sample the blur of the whole image would make non-zero lies inside and none near the layer's edge is.
    ///
    /// @param shape the shape.
    /// @param width the image's width in pixels.
    /// @param height the image's height in pixels.
    /// @return the rectangle, clipped to the image; empty for a shape outside it or a feather the engine refuses.
    [[nodiscard]] static QRect layerBounds(const ShapeObject& shape, int width, int height);

    /// A shape rasterized by the toolkit, antialiased and unfeathered, over a rectangle: a filled shape's area or an
    /// outline's stroke, one coverage sample per pixel, the toolkit's eight-bit alpha over 255.
    ///
    /// @param shape the shape.
    /// @param bounds the rectangle, in image pixels.
    /// @return the layer; an empty one for an empty rectangle.
    [[nodiscard]] static ShapeLayer rasterizeShapeCoverage(const ShapeObject& shape, const QRect& bounds);

    /// Feathers a layer in place: the engine's Gaussian of half the width, never below half a pixel. A width of zero
    /// leaves it as it is.
    ///
    /// @param execution the operation the blur runs under.
    /// @param layer the layer.
    /// @param feather the feather width in pixels.
    /// @return success; or what `planCoverageFeather` or `featherCoverage` refuses, in which case the layer is as it
    ///         was.
    [[nodiscard]] static an::Status featherShapeLayer(const an::ExecutionContext& execution, ShapeLayer& layer,
                                                      float feather);

    /// A shape's feathered layer over the rectangle `layerBounds` gives.
    ///
    /// @param execution the operation the feather runs under.
    /// @param shape the shape.
    /// @param width the image's width in pixels.
    /// @param height the image's height in pixels.
    /// @return the layer, empty for a shape that reaches no pixel; or what `featherShapeLayer` refuses.
    [[nodiscard]] static an::Result<ShapeLayer> renderShapeLayer(const an::ExecutionContext& execution,
                                                                 const ShapeObject& shape, int width, int height);

    /// Screens a layer over a plane inside a rectangle, at an opacity: the engine's screen blend moved by the opacity,
    /// which leaves a pixel the layer does not cover as it was.
    ///
    /// @param execution the operation the blend runs under.
    /// @param layer the layer.
    /// @param opacity the shape's opacity, held in the unit interval; one that is not a number is none.
    /// @param composite the whole plane, in image pixels.
    /// @param clip the rectangle of the plane to write.
    /// @return success; or what `planLayerBlend` or `blendLayerPlane` refuses.
    [[nodiscard]] static an::Status screenShapeLayer(const an::ExecutionContext& execution, const ShapeLayer& layer,
                                                     float opacity, an::PlaneView<float> composite, const QRect& clip);
};

} // namespace an::ui::drawing
