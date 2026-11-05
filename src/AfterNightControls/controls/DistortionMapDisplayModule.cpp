// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "controls/DistortionMapDisplayModule.h"

#include "controls/AstrometryOverlayUtils.h"
#include "controls/ImageViewControl.h"
#include "theme/AnDesignTokens.h"

#include <QFontMetrics>
#include <QLineF>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QScrollBar>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>

namespace an::ui
{
namespace
{

constexpr int kDefaultGridColumns = 20;
constexpr int kDefaultGridRows = 20;

constexpr double kArrowPenWidthMin = 1.15;
constexpr double kArrowPenWidthRange = 0.65;
constexpr double kArrowHeadSizeMin = 5.0;
constexpr double kArrowHeadSizeRange = 4.0;

/// What a sampled map depends on: the solution's distortion and the frame's size.
struct CacheKey
{
    std::optional<an::SipDistortion> distortion;
    int imageWidth = 0;
    int imageHeight = 0;

    [[nodiscard]] bool matches(const CacheKey& other) const
    {
        return distortion == other.distortion && imageWidth == other.imageWidth && imageHeight == other.imageHeight;
    }
};

[[nodiscard]] bool vectorTouchesVisibleRect(const QPointF& start, const QPointF& end, const QRectF& visible_rect)
{
    if (!visible_rect.isValid() || visible_rect.isEmpty())
    {
        return false;
    }

    const QRectF expanded_visible_rect = visible_rect.adjusted(-10.0, -10.0, 10.0, 10.0);
    if (expanded_visible_rect.contains(start) || expanded_visible_rect.contains(end))
    {
        return true;
    }

    const QRectF line_bounds = QRectF(start, end).normalized().adjusted(-6.0, -6.0, 6.0, 6.0);
    return line_bounds.intersects(expanded_visible_rect);
}

[[nodiscard]] QColor blendColor(const QColor& from, const QColor& to, const double t)
{
    const double clamped_t = std::clamp(t, 0.0, 1.0);
    return QColor::fromRgbF(from.redF() + ((to.redF() - from.redF()) * clamped_t),
                            from.greenF() + ((to.greenF() - from.greenF()) * clamped_t),
                            from.blueF() + ((to.blueF() - from.blueF()) * clamped_t),
                            from.alphaF() + ((to.alphaF() - from.alphaF()) * clamped_t));
}

[[nodiscard]] QColor colorForMagnitude(const double normalized_magnitude, const DesignTokens& token_set)
{
    const double clamped = std::clamp(normalized_magnitude, 0.0, 1.0);
    if (clamped <= 0.5)
    {
        return blendColor(token_set.colors.statusSuccess, token_set.colors.statusWarning, clamped * 2.0);
    }

    return blendColor(token_set.colors.statusWarning, token_set.colors.statusError, (clamped - 0.5) * 2.0);
}

void drawArrow(QPainter& painter, const QPointF& start, const QPointF& end, const QColor& color,
               const double normalized_magnitude)
{
    const QLineF line(start, end);
    if (line.length() <= 0.01)
    {
        return;
    }

    QPen pen(color);
    pen.setCosmetic(true);
    pen.setWidthF(kArrowPenWidthMin + (normalized_magnitude * kArrowPenWidthRange));

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(pen);
    painter.setBrush(color);
    painter.drawLine(line);

    const double arrow_size = kArrowHeadSizeMin + (normalized_magnitude * kArrowHeadSizeRange);
    const double angle = std::atan2(-line.dy(), line.dx());
    const QPointF arrow_p1 = end + QPointF(std::sin(angle + (std::numbers::pi / 3.0)) * arrow_size,
                                           std::cos(angle + (std::numbers::pi / 3.0)) * arrow_size);
    const QPointF arrow_p2 = end + QPointF(std::sin(angle + std::numbers::pi - (std::numbers::pi / 3.0)) * arrow_size,
                                           std::cos(angle + std::numbers::pi - (std::numbers::pi / 3.0)) * arrow_size);

    QPainterPath head;
    head.moveTo(end);
    head.lineTo(arrow_p1);
    head.lineTo(arrow_p2);
    head.closeSubpath();
    painter.drawPath(head);
    painter.restore();
}

} // namespace

struct DistortionMapDisplayModule::Impl
{
    CacheKey cacheKey;
    bool cacheValid = false;
    QVector<DistortionVectorSample> samples;
    double globalMaxMagnitude = 0.0;
    bool hasSip = false;
};

DistortionMapDisplayModule::DistortionMapDisplayModule(ImageViewControl* control)
    : ImageViewControlListener(control), m_impl(std::make_unique<Impl>())
{
}

DistortionMapDisplayModule::~DistortionMapDisplayModule()
{
    detachFromViewWidget();
}

void DistortionMapDisplayModule::onDraw(QPainter& painter)
{
    auto* control = getParentViewControl();
    auto* view_widget = control ? control->getViewWidget() : nullptr;
    const std::optional<an::ImageDescriptor> described = control ? control->imageDescriptor() : std::nullopt;
    if (!control || !view_widget || !described.has_value())
    {
        return;
    }

    CacheKey key;
    if (const std::optional<an::WcsSolution>& solution = control->imageSolution(); solution.has_value())
    {
        key.distortion = solution->distortion();
    }
    key.imageWidth = static_cast<int>(described->extent().width());
    key.imageHeight = static_cast<int>(described->extent().height());

    if (!m_impl->cacheValid || !m_impl->cacheKey.matches(key))
    {
        m_impl->cacheKey = key;
        m_impl->cacheValid = true;
        m_impl->samples.clear();
        m_impl->hasSip = false;

        if (key.distortion.has_value())
        {
            m_impl->samples = detail::sampleDistortionVectors(*key.distortion, key.imageWidth, key.imageHeight,
                                                              kDefaultGridColumns, kDefaultGridRows);
        }
        m_impl->hasSip = !m_impl->samples.isEmpty();
        m_impl->globalMaxMagnitude = 0.0;
        for (const auto& sample : m_impl->samples)
        {
            m_impl->globalMaxMagnitude = (std::max)(m_impl->globalMaxMagnitude, sample.magnitudePixels);
        }
    }

    if (!m_impl->hasSip || m_impl->samples.isEmpty())
    {
        return;
    }

    const double view_scale = view_widget->getCurrentScaleFactor();
    if (!std::isfinite(view_scale) || view_scale <= 0.0)
    {
        return;
    }

    const QRectF visible_widget_rect = detail::visibleOverlayWidgetRect(control);
    if (!visible_widget_rect.isValid() || visible_widget_rect.isEmpty())
    {
        return;
    }

    QVector<const DistortionVectorSample*> visible_samples;
    visible_samples.reserve(m_impl->samples.size());
    for (const DistortionVectorSample& sample : m_impl->samples)
    {
        const QPointF start_widget = sample.sourceImagePoint * view_scale;
        const QPointF end_widget = sample.correctedImagePoint * view_scale;
        if (!vectorTouchesVisibleRect(start_widget, end_widget, visible_widget_rect))
        {
            continue;
        }

        visible_samples.append(&sample);
    }

    if (visible_samples.isEmpty() || m_impl->globalMaxMagnitude <= std::numeric_limits<double>::epsilon())
    {
        return;
    }

    const auto& token_set = an::ui::tokens();
    const double target_max_arrow_length =
        std::clamp((std::min)(visible_widget_rect.width(), visible_widget_rect.height()) * 0.12, 24.0, 96.0);
    const double max_actual_length = m_impl->globalMaxMagnitude * view_scale;
    const double arrow_scale =
        max_actual_length > 0.0 ? std::clamp(target_max_arrow_length / max_actual_length, 0.75, 40.0) : 1.0;

    painter.save();
    painter.setBrush(Qt::NoBrush);
    for (const DistortionVectorSample* sample : visible_samples)
    {
        const double normalized_magnitude = std::clamp(sample->magnitudePixels / m_impl->globalMaxMagnitude, 0.0, 1.0);
        QColor arrow_color = colorForMagnitude(normalized_magnitude, token_set);
        arrow_color.setAlpha(220);

        const QPointF start_widget = sample->sourceImagePoint * view_scale;
        const QPointF scaled_displacement = sample->displacement() * (view_scale * arrow_scale);
        const QPointF end_widget = start_widget + scaled_displacement;
        drawArrow(painter, start_widget, end_widget, arrow_color, normalized_magnitude);
    }

    painter.setFont(token_set.typography.caption);
    const QFontMetrics metrics(painter.font());
    const QString legend_text = QStringLiteral("SIP distortion  max %1 px").arg(m_impl->globalMaxMagnitude, 0, 'f', 2);
    QRect legend_rect = metrics.boundingRect(legend_text);
    legend_rect.adjust(-token_set.spacing.xs, -(std::max)(1, token_set.spacing.xs / 2), token_set.spacing.xs,
                       (std::max)(1, token_set.spacing.xs / 2));
    legend_rect.moveTopLeft(QPoint(qRound(visible_widget_rect.left()) + token_set.spacing.sm,
                                   qRound(visible_widget_rect.top()) + token_set.spacing.sm));

    QColor legend_fill = token_set.colors.bgSurface;
    legend_fill.setAlpha(210);
    QColor legend_border = token_set.colors.borderSubtle;
    legend_border.setAlpha(220);
    QColor legend_text_color = token_set.colors.textPrimary;
    legend_text_color.setAlpha(235);

    painter.setPen(legend_border);
    painter.setBrush(legend_fill);
    painter.drawRoundedRect(legend_rect, token_set.radius.sm, token_set.radius.sm);
    painter.setPen(legend_text_color);
    painter.drawText(legend_rect, Qt::AlignCenter, legend_text);
    painter.restore();
}

void DistortionMapDisplayModule::onImageLoaded(ImageViewControl* /*ctrl*/)
{
    invalidateCache();
}

void DistortionMapDisplayModule::onImageUpdated(ImageViewControl* /*ctrl*/)
{
    invalidateCache();
}

void DistortionMapDisplayModule::onImageRenamed(ImageViewControl* /*ctrl*/)
{
    invalidateCache();
}

void DistortionMapDisplayModule::onImageViewControlDestroyed(ImageViewControl* ctrl)
{
    detachFromViewWidget();
    ImageViewControlListener::onImageViewControlDestroyed(ctrl);
    invalidateCache();
}

int DistortionMapDisplayModule::sampledVectorCount() const
{
    return static_cast<int>(m_impl->samples.size());
}

void DistortionMapDisplayModule::invalidateCache()
{
    m_impl->cacheValid = false;
    m_impl->samples.clear();
    m_impl->globalMaxMagnitude = 0.0;
    m_impl->hasSip = false;
}

} // namespace an::ui
