// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A brush stroke as the drawing tools lay it down: the stroke's points, kept here, and every dab of the brush tip
/// they place screened into a stroke plane by the engine.
///
/// A stroke's points belong to the tool; what a brush is and where its dabs fall are the engine's
/// (`Algorithms/Spatial/DrawingPrimitives.h`). Each new point closes a segment from the previous point, a Catmull-Rom
/// curve through the point before it and the point after it - extrapolated, since the tool does not have it yet - and
/// the engine places the segment's dabs at the brush's spacing and screens them into the stroke plane at the event's
/// pressure. The stroke plane only grows; what the stroke does to the mask is the layer blend the document runs once
/// per pixel against the mask as it was before the stroke.
#pragma once

#include "AfterNightControlsExport.h"
#include "controls/drawing/DrawingTypes.h"

#include <AfterNightEngine/Algorithms/Pointwise/LayerBlend.h>
#include <AfterNightEngine/Algorithms/Spatial/DrawingPrimitives.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/PlaneView.h>

#include <QPointF>
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

/// The engine's tip shape for the tool's.
///
/// @param shape the tool's tip shape.
/// @return the engine's.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::BrushTipShape brushTipShapeOf(BrushParams::TipShape shape) noexcept;

/// The engine's blend mode for the tool's.
///
/// @param mode the tool's blend mode.
/// @return the engine's.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::LayerBlendMode layerBlendModeOf(BrushParams::BlendMode mode) noexcept;

/// The brush the engine plans for the tool's parameters: the size held between one pixel and the engine's widest
/// brush, the spacing between a hundredth of the diameter and the diameter, and the hardness and the flow in the unit
/// interval, as the tools always held them. A value that is not a number stays one, and the plan refuses it.
///
/// @param brush the tool's parameters.
/// @return the request.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::BrushRequest brushRequestOf(const BrushParams& brush) noexcept;

/// The rectangle an engine stamp reports, in the plane's pixels.
///
/// @param rectangle the engine's rectangle.
/// @return the rectangle; an empty one for an empty one.
[[nodiscard]] AFTERNIGHTCONTROLS_API QRect rectOf(const an::BrushRectangle& rectangle) noexcept;

/// One stroke at a time.
///
/// Not thread-safe: a tool uses it on one thread. The stroke plane is the caller's, so the same brush can draw into
/// the document's plane or a test's.
class AFTERNIGHTCONTROLS_API BrushEngine
{
public:
    /// Starts a stroke: plans the brush and writes its tip.
    ///
    /// @param brush the tool's parameters, held as `brushRequestOf` holds them.
    /// @return success; or what `planBrush` refuses, in which case no stroke is active.
    [[nodiscard]] an::Status beginStroke(const BrushParams& brush);

    /// Adds a point to the stroke and screens the dabs of the segment it closes into the stroke plane.
    ///
    /// The stroke's first point places one dab on itself; every later one, the dabs of the segment from the previous
    /// point to it. A pressure is held in the unit interval, and one that is not a number is no pressure.
    ///
    /// @param execution the operation the stamp runs under.
    /// @param stroke the stroke plane, the same one for the whole stroke.
    /// @param imagePos where the point is, in the plane's pixels.
    /// @param pressure how hard it was pressed; one for a device that reports none.
    /// @return the rectangle the dabs reached, clipped to the plane - empty when none did; or `ErrorCode::InvalidState`
    ///         with no stroke active; or what placing or stamping the dabs refuses, in which case the point is not
    ///         kept.
    [[nodiscard]] an::Result<QRect> addStrokePoint(const an::ExecutionContext& execution, an::PlaneView<float> stroke,
                                                   const QPointF& imagePos, float pressure = 1.0f);

    /// Ends the stroke. Ending a stroke that is not active does nothing.
    void endStroke() noexcept;

    /// Whether a stroke is active.
    [[nodiscard]] bool hasActiveStroke() const noexcept;

    /// The parameters the active or last stroke began with, as the tool gave them.
    [[nodiscard]] const BrushParams& currentBrush() const noexcept;

    /// The union of the rectangles the active or last stroke's dabs reached.
    [[nodiscard]] QRect strokeBounds() const noexcept;

    /// How many samples wide and high the tip of the active or last stroke is; zero before the first stroke.
    [[nodiscard]] std::uint32_t tipSide() const noexcept;

private:
    BrushParams m_brush;
    std::optional<an::BrushPlan> m_plan;
    std::vector<float> m_tip;
    std::vector<QPointF> m_points;
    std::vector<an::BrushDab> m_dabs;
    QRect m_strokeBounds;
    bool m_activeStroke = false;
};

} // namespace an::ui::drawing
