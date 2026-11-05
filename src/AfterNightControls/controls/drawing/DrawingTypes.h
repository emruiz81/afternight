// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QPointF>
#include <QRectF>
#include <QSizeF>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>

namespace an::ui::drawing
{

enum class DrawingTool : std::uint8_t
{
    Brush = 0,
    Eraser = 1,
    ShapeDraw = 2,
    ShapeSelect = 3,
    FloodFill = 4,
    GradientFill = 5,
    FreehandLasso = 6
};

enum class MaskPreviewMode : std::uint8_t
{
    Red = 0,
    Green = 1,
    Blue = 2,
    Grayscale = 3,
    Checkerboard = 4,
    Amber = 5
};

struct BrushParams
{
    enum class TipShape : std::uint8_t
    {
        Round,
        Square
    };

    enum class BlendMode : std::uint8_t
    {
        Normal,
        Multiply,
        Screen,
        Overlay,
        Erase
    };

    TipShape tipShape = TipShape::Round;
    float size = 20.0f;
    float hardness = 1.0f;
    float roundness = 1.0f;
    float angle = 0.0f;
    float opacity = 1.0f;
    float flow = 1.0f;
    float spacing = 0.25f;
    float stabilization = 0.0f;
    BlendMode blendMode = BlendMode::Normal;
};

struct ShapeObject
{
    enum class Type : std::uint8_t
    {
        Circle,
        Ellipse,
        Rectangle,
        Polygon,
        Line
    };

    struct CircleData
    {
        float radius = 0.0f;
    };

    struct EllipseData
    {
        float rx = 0.0f;
        float ry = 0.0f;
    };

    struct RectData
    {
        QSizeF size;
        float cornerRadius = 0.0f;
    };

    struct PolyData
    {
        std::vector<QPointF> vertices;
    };

    struct LineData
    {
        std::vector<QPointF> points;
        float width = 1.0f;
    };

    using GeometryData = std::variant<CircleData, EllipseData, RectData, PolyData, LineData>;

    Type type = Type::Rectangle;
    QPointF center;
    float rotation = 0.0f;
    GeometryData data = RectData{QSizeF(1.0, 1.0), 0.0f};
    float opacity = 1.0f;
    float feather = 0.0f;
    bool filled = true;

    [[nodiscard]] QRectF boundingRect() const;
    [[nodiscard]] bool hitTest(const QPointF& imagePos, double tolerance = 0.0) const;
    [[nodiscard]] int hitTestHandle(const QPointF& imagePos, double tolerance = 6.0) const;
};

using ShapeHandle = std::size_t;
inline constexpr ShapeHandle kInvalidShapeHandle = static_cast<ShapeHandle>(-1);

[[nodiscard]] inline bool isEraseTool(const DrawingTool tool)
{
    return tool == DrawingTool::Eraser;
}

[[nodiscard]] inline bool isShapeTool(const DrawingTool tool)
{
    return tool == DrawingTool::ShapeDraw || tool == DrawingTool::ShapeSelect || tool == DrawingTool::FreehandLasso;
}

[[nodiscard]] inline BrushParams makeBrushParams(const DrawingTool tool, BrushParams brush)
{
    brush.size = (std::max)(brush.size, 1.0f);
    brush.hardness = std::clamp(brush.hardness, 0.0f, 1.0f);
    brush.opacity = std::clamp(brush.opacity, 0.0f, 1.0f);
    brush.flow = std::clamp(brush.flow, 0.0f, 1.0f);
    brush.spacing = std::clamp(brush.spacing, 0.01f, 1.0f);
    brush.stabilization = std::clamp(brush.stabilization, 0.0f, 1.0f);
    brush.blendMode = isEraseTool(tool) ? BrushParams::BlendMode::Erase : brush.blendMode;
    return brush;
}

[[nodiscard]] inline BrushParams makeBrushParams(const DrawingTool tool, const float size, const float opacity)
{
    BrushParams brush;
    brush.size = size;
    brush.opacity = opacity;
    return makeBrushParams(tool, brush);
}

[[nodiscard]] inline float clampUnit(const float value)
{
    return std::clamp(value, 0.0f, 1.0f);
}

inline constexpr int kMaxBrushSizePx = 200;

} // namespace an::ui::drawing
