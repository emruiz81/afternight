// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "controls/drawing/BrushEngine.h"

#include "controls/drawing/CoveragePlane.h"

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <utility>

namespace
{

/// A brush point at an image position.
[[nodiscard]] an::BrushPoint brushPointOf(const QPointF& point) noexcept
{
    return an::BrushPoint{.x = point.x(), .y = point.y()};
}

/// A pressure as the stamp reads it: held in the unit interval, and no pressure for one that is not a number.
[[nodiscard]] float heldPressure(const float pressure) noexcept
{
    return std::isfinite(pressure) ? std::clamp(pressure, 0.0f, 1.0f) : 0.0f;
}

} // namespace

namespace an::ui::drawing
{

an::BrushTipShape brushTipShapeOf(const BrushParams::TipShape shape) noexcept
{
    return shape == BrushParams::TipShape::Square ? an::BrushTipShape::Square : an::BrushTipShape::Round;
}

an::LayerBlendMode layerBlendModeOf(const BrushParams::BlendMode mode) noexcept
{
    switch (mode)
    {
    case BrushParams::BlendMode::Multiply:
        return an::LayerBlendMode::Multiply;
    case BrushParams::BlendMode::Screen:
        return an::LayerBlendMode::Screen;
    case BrushParams::BlendMode::Overlay:
        return an::LayerBlendMode::Overlay;
    case BrushParams::BlendMode::Erase:
        return an::LayerBlendMode::Erase;
    case BrushParams::BlendMode::Normal:
    default:
        return an::LayerBlendMode::Normal;
    }
}

an::BrushRequest brushRequestOf(const BrushParams& brush) noexcept
{
    return an::BrushRequest{.diameter = std::clamp(brush.size, 1.0f, an::kMaxBrushDiameter),
                            .hardness = clampUnit(brush.hardness),
                            .shape = brushTipShapeOf(brush.tipShape),
                            .spacing = std::clamp(brush.spacing, an::kMinimumBrushSpacing, 1.0f),
                            .flow = clampUnit(brush.flow)};
}

QRect rectOf(const an::BrushRectangle& rectangle) noexcept
{
    if (rectangle.empty())
    {
        return {};
    }
    return QRect(static_cast<int>(rectangle.x), static_cast<int>(rectangle.y), static_cast<int>(rectangle.width),
                 static_cast<int>(rectangle.height));
}

an::Status BrushEngine::beginStroke(const BrushParams& brush)
{
    m_activeStroke = false;
    m_points.clear();
    m_strokeBounds = QRect();
    m_brush = brush;

    const an::Result<an::BrushPlan> plan = an::planBrush(brushRequestOf(brush));
    if (!plan.hasValue())
    {
        m_plan.reset();
        return plan.status();
    }
    m_plan = *plan;
    const std::uint32_t side = plan->tipSide();
    m_tip.assign(static_cast<std::size_t>(side) * side, 0.0f);
    const std::optional<an::PlaneView<float>> tip = packedPlane(m_tip, side, side);
    if (!tip.has_value())
    {
        m_plan.reset();
        return an::Status::failure(an::ErrorCode::Internal, "the brush tip cannot be viewed as a plane");
    }
    const an::Status written = an::writeBrushTip(*plan, *tip);
    if (written.failed())
    {
        m_plan.reset();
        return written;
    }
    m_activeStroke = true;
    return an::Status::success();
}

an::Result<QRect> BrushEngine::addStrokePoint(const an::ExecutionContext& execution, const an::PlaneView<float> stroke,
                                              const QPointF& imagePos, const float pressure)
{
    if (!m_activeStroke || !m_plan.has_value())
    {
        return an::Status::failure(an::ErrorCode::InvalidState, "a stroke point needs an active stroke");
    }

    // A stroke's first point is a segment of that point alone, which places one dab on it. Every later point closes
    // the segment from the previous point, through the point before that.
    const an::BrushPoint end = brushPointOf(imagePos);
    an::BrushSegment segment = an::makeBrushSegment(end, end, end);
    if (!m_points.empty())
    {
        const QPointF& start = m_points.back();
        const QPointF& before = m_points.size() >= 2u ? m_points[m_points.size() - 2u] : start;
        segment = an::makeBrushSegment(brushPointOf(before), brushPointOf(start), end);
    }

    const an::Result<std::uint32_t> count = an::countBrushSegmentDabs(*m_plan, segment);
    if (!count.hasValue())
    {
        return count.status();
    }
    m_dabs.resize(*count);
    const an::Result<std::uint32_t> placed = an::placeBrushSegmentDabs(*m_plan, segment, m_dabs);
    if (!placed.hasValue())
    {
        return placed.status();
    }
    const std::optional<an::PlaneView<const float>> tip =
        packedPlane(std::as_const(m_tip), m_plan->tipSide(), m_plan->tipSide());
    if (!tip.has_value())
    {
        return an::Status::failure(an::ErrorCode::Internal, "the brush tip cannot be viewed as a plane");
    }
    const an::Result<an::BrushStampResult> stamped =
        an::stampBrushDabs(execution, *m_plan, *tip, std::span<const an::BrushDab>(m_dabs.data(), *placed),
                           heldPressure(pressure), stroke);
    if (!stamped.hasValue())
    {
        return stamped.status();
    }

    m_points.push_back(imagePos);
    const QRect dirty = rectOf(stamped->dirty);
    if (!dirty.isEmpty())
    {
        m_strokeBounds = m_strokeBounds.isEmpty() ? dirty : m_strokeBounds.united(dirty);
    }
    return dirty;
}

void BrushEngine::endStroke() noexcept
{
    m_points.clear();
    m_activeStroke = false;
}

bool BrushEngine::hasActiveStroke() const noexcept
{
    return m_activeStroke;
}

const BrushParams& BrushEngine::currentBrush() const noexcept
{
    return m_brush;
}

QRect BrushEngine::strokeBounds() const noexcept
{
    return m_strokeBounds;
}

std::uint32_t BrushEngine::tipSide() const noexcept
{
    return m_plan.has_value() ? m_plan->tipSide() : 0u;
}

} // namespace an::ui::drawing
