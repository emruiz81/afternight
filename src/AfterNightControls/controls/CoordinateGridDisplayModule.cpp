// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "controls/CoordinateGridDisplayModule.h"

#include "controls/AstrometryOverlayUtils.h"
#include "controls/ImageViewControl.h"
#include "theme/AnDesignTokens.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QScrollBar>

#include <limits>
#include <optional>

namespace an::ui
{
namespace
{

struct CoordinateGridLabel
{
    QString text;
    QPointF anchor;
};

/// What a projected grid depends on: the solution and the view state.
struct CacheKey
{
    std::optional<an::WcsSolution> solution;
    detail::OverlayViewState view;

    [[nodiscard]] bool matches(const CacheKey& other) const
    {
        return solution == other.solution && view.matches(other.view);
    }
};

/// The sky a rectangle of the image covers, as offsets in right ascension from the direction at its centre and a
/// declination range, sampled along its edges.
[[nodiscard]] bool collectVisibleWorldBounds(const an::WcsSolution& solution, const QRectF& visible_image_rect,
                                             double& center_ra_deg, double& ra_min_offset_deg,
                                             double& ra_max_offset_deg, double& dec_min_deg, double& dec_max_deg)
{
    if (!visible_image_rect.isValid() || visible_image_rect.isEmpty())
    {
        return false;
    }

    const an::Result<an::SkyCoordinate> center = solution.pixelToWorld(
        an::PixelCoordinate{.x = visible_image_rect.center().x(), .y = visible_image_rect.center().y()});
    if (!center.hasValue())
    {
        return false;
    }
    center_ra_deg = center->rightAscensionDegrees;

    ra_min_offset_deg = std::numeric_limits<double>::infinity();
    ra_max_offset_deg = -std::numeric_limits<double>::infinity();
    dec_min_deg = std::numeric_limits<double>::infinity();
    dec_max_deg = -std::numeric_limits<double>::infinity();

    constexpr int samples_per_edge = 6;
    auto sample_point = [&](const double pixel_x, const double pixel_y) {
        const an::Result<an::SkyCoordinate> direction =
            solution.pixelToWorld(an::PixelCoordinate{.x = pixel_x, .y = pixel_y});
        if (!direction.hasValue())
        {
            return;
        }

        const double ra_offset_deg =
            detail::shortestAngularDistanceDegrees(center_ra_deg, direction->rightAscensionDegrees);
        ra_min_offset_deg = (std::min)(ra_min_offset_deg, ra_offset_deg);
        ra_max_offset_deg = (std::max)(ra_max_offset_deg, ra_offset_deg);
        dec_min_deg = (std::min)(dec_min_deg, direction->declinationDegrees);
        dec_max_deg = (std::max)(dec_max_deg, direction->declinationDegrees);
    };

    for (int i = 0; i <= samples_per_edge; ++i)
    {
        const double t = static_cast<double>(i) / static_cast<double>(samples_per_edge);
        const double sample_x = visible_image_rect.left() + (visible_image_rect.width() * t);
        const double sample_y = visible_image_rect.top() + (visible_image_rect.height() * t);

        sample_point(sample_x, visible_image_rect.top());
        sample_point(sample_x, visible_image_rect.bottom());
        sample_point(visible_image_rect.left(), sample_y);
        sample_point(visible_image_rect.right(), sample_y);
    }

    return std::isfinite(ra_min_offset_deg) && std::isfinite(ra_max_offset_deg) && std::isfinite(dec_min_deg) &&
           std::isfinite(dec_max_deg);
}

[[nodiscard]] QPainterPath buildPolylinePath(const CoordinateGridProjectedLine& line, const double max_gap)
{
    QPainterPath path;
    bool started = false;
    QPointF previous_point;

    for (const QPointF& point : line.points)
    {
        if (!started)
        {
            path.moveTo(point);
            started = true;
        }
        else
        {
            const double dx = point.x() - previous_point.x();
            const double dy = point.y() - previous_point.y();
            if (std::hypot(dx, dy) > max_gap)
            {
                path.moveTo(point);
            }
            else
            {
                path.lineTo(point);
            }
        }
        previous_point = point;
    }

    return path;
}

[[nodiscard]] bool lineTouchesVisibleRect(const CoordinateGridProjectedLine& line, const QRectF& visible_rect)
{
    if (line.points.size() < 2 || !visible_rect.isValid() || visible_rect.isEmpty())
    {
        return false;
    }

    for (const QPointF& point : line.points)
    {
        if (visible_rect.contains(point))
        {
            return true;
        }
    }

    const double max_gap = (std::max)(visible_rect.width(), visible_rect.height()) * 0.5;
    const QPainterPath path = buildPolylinePath(line, max_gap);
    return path.intersects(visible_rect);
}

[[nodiscard]] std::optional<CoordinateGridLabel> buildLabelForLine(const CoordinateGridProjectedLine& line,
                                                                   const QRectF& visible_rect, const double spacing_deg)
{
    if (line.points.isEmpty() || !visible_rect.isValid() || visible_rect.isEmpty())
    {
        return std::nullopt;
    }

    const auto within_visible_rect = [&](const QPointF& point) {
        return visible_rect.adjusted(-1.0, -1.0, 1.0, 1.0).contains(point);
    };

    QPointF anchor;
    bool found = false;
    for (const QPointF& point : line.points)
    {
        if (!within_visible_rect(point))
        {
            continue;
        }

        if (!found)
        {
            anchor = point;
            found = true;
            continue;
        }

        if (line.axis == CoordinateGridAxis::RightAscension)
        {
            if (point.y() < anchor.y())
            {
                anchor = point;
            }
        }
        else if (point.x() < anchor.x())
        {
            anchor = point;
        }
    }

    if (!found)
    {
        return std::nullopt;
    }

    CoordinateGridLabel label;
    label.anchor = anchor;
    label.text = line.axis == CoordinateGridAxis::RightAscension
                     ? detail::formatCoordinateGridRaLabel(line.coordinateDegrees, spacing_deg)
                     : detail::formatCoordinateGridDecLabel(line.coordinateDegrees, spacing_deg);
    return label;
}

void drawProjectedLine(QPainter& painter, const CoordinateGridProjectedLine& line, const QRectF& visible_rect)
{
    if (line.points.size() < 2)
    {
        return;
    }

    const double max_gap = (std::max)(visible_rect.width(), visible_rect.height()) * 0.5;
    const QPainterPath path = buildPolylinePath(line, max_gap);
    painter.drawPath(path);
}

} // namespace

struct CoordinateGridDisplayModule::Impl
{
    CacheKey cacheKey;
    bool cacheValid = false;
    bool hasAstrometricSolution = false;
    QVector<CoordinateGridProjectedLine> lines;
    QVector<CoordinateGridLabel> labels;
    QRectF cachedVisibleWidgetRect;
};

CoordinateGridDisplayModule::CoordinateGridDisplayModule(ImageViewControl* control)
    : ImageViewControlListener(control), m_impl(std::make_unique<Impl>())
{
}

CoordinateGridDisplayModule::~CoordinateGridDisplayModule()
{
    detachFromViewWidget();
}

void CoordinateGridDisplayModule::onDraw(QPainter& painter)
{
    auto* control = getParentViewControl();
    auto* view_widget = control ? control->getViewWidget() : nullptr;
    if (!control || !view_widget || !control->hasImage())
    {
        return;
    }

    CacheKey key;
    key.solution = control->imageSolution();
    key.view = detail::overlayViewState(control);

    if (!m_impl->cacheValid || !m_impl->cacheKey.matches(key))
    {
        m_impl->cacheKey = key;
        m_impl->cacheValid = true;
        m_impl->lines.clear();
        m_impl->labels.clear();
        m_impl->hasAstrometricSolution = false;

        if (!key.solution.has_value() || !std::isfinite(key.view.scale) || key.view.scale <= 0.0)
        {
            return;
        }

        const an::WcsSolution& wcs = *key.solution;
        const double scale = key.view.scale;

        const QRectF visible_widget_rect = detail::visibleOverlayWidgetRect(control);
        const QRectF visible_image_rect(visible_widget_rect.left() / scale, visible_widget_rect.top() / scale,
                                        visible_widget_rect.width() / scale, visible_widget_rect.height() / scale);
        if (!visible_image_rect.isValid() || visible_image_rect.isEmpty())
        {
            return;
        }

        double center_ra_deg = 0.0;
        double ra_min_offset_deg = 0.0;
        double ra_max_offset_deg = 0.0;
        double dec_min_deg = 0.0;
        double dec_max_deg = 0.0;
        if (!collectVisibleWorldBounds(wcs, visible_image_rect, center_ra_deg, ra_min_offset_deg, ra_max_offset_deg,
                                       dec_min_deg, dec_max_deg))
        {
            return;
        }

        const double spacing_deg = detail::chooseCoordinateGridSpacingDegrees(
            (std::max)(ra_max_offset_deg - ra_min_offset_deg, dec_max_deg - dec_min_deg));

        const double ra_start_unwrapped = center_ra_deg + ra_min_offset_deg;
        const double ra_end_unwrapped = center_ra_deg + ra_max_offset_deg;
        const double dec_start_deg = std::clamp(dec_min_deg - (spacing_deg * 0.25), -90.0, 90.0);
        const double dec_end_deg = std::clamp(dec_max_deg + (spacing_deg * 0.25), -90.0, 90.0);

        const double ra_grid_start = std::floor(ra_start_unwrapped / spacing_deg) * spacing_deg;
        const double ra_grid_end = std::ceil(ra_end_unwrapped / spacing_deg) * spacing_deg;
        for (double ra_deg = ra_grid_start; ra_deg <= ra_grid_end + (spacing_deg * 0.25); ra_deg += spacing_deg)
        {
            CoordinateGridProjectedLine line = detail::projectCoordinateGridLine(
                wcs, CoordinateGridAxis::RightAscension, ra_deg, dec_start_deg, dec_end_deg, scale);

            if (!lineTouchesVisibleRect(line, visible_widget_rect))
            {
                continue;
            }

            if (const auto label = buildLabelForLine(line, visible_widget_rect, spacing_deg); label.has_value())
            {
                m_impl->labels.append(*label);
            }
            m_impl->lines.append(std::move(line));
        }

        const double dec_grid_start = std::floor(dec_start_deg / spacing_deg) * spacing_deg;
        const double dec_grid_end = std::ceil(dec_end_deg / spacing_deg) * spacing_deg;
        for (double dec_deg = dec_grid_start; dec_deg <= dec_grid_end + (spacing_deg * 0.25); dec_deg += spacing_deg)
        {
            CoordinateGridProjectedLine line = detail::projectCoordinateGridLine(
                wcs, CoordinateGridAxis::Declination, dec_deg, ra_start_unwrapped, ra_end_unwrapped, scale);

            if (!lineTouchesVisibleRect(line, visible_widget_rect))
            {
                continue;
            }

            if (const auto label = buildLabelForLine(line, visible_widget_rect, spacing_deg); label.has_value())
            {
                m_impl->labels.append(*label);
            }
            m_impl->lines.append(std::move(line));
        }

        m_impl->hasAstrometricSolution = true;
        m_impl->cachedVisibleWidgetRect = visible_widget_rect;
    }

    if (!m_impl->hasAstrometricSolution || m_impl->lines.isEmpty())
    {
        return;
    }

    const QRectF& visible_widget_rect = m_impl->cachedVisibleWidgetRect;
    if (!visible_widget_rect.isValid() || visible_widget_rect.isEmpty())
    {
        return;
    }

    const auto& token_set = an::ui::tokens();
    QColor grid_color = token_set.colors.accentPrimary;
    grid_color.setAlpha(170);
    QColor label_text_color = token_set.colors.textPrimary;
    label_text_color.setAlpha(230);
    QColor label_fill_color = token_set.colors.bgSurface;
    label_fill_color.setAlpha(190);
    QColor label_border_color = token_set.colors.borderSubtle;
    label_border_color.setAlpha(210);

    QPen grid_pen(grid_color);
    grid_pen.setCosmetic(true);
    grid_pen.setWidthF(1.0);
    grid_pen.setStyle(Qt::DashLine);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(grid_pen);
    painter.setBrush(Qt::NoBrush);
    for (const auto& line : m_impl->lines)
    {
        drawProjectedLine(painter, line, visible_widget_rect);
    }

    painter.setFont(token_set.typography.caption);
    const QFontMetrics metrics(painter.font());
    const int horizontal_padding = token_set.spacing.xs;
    const int vertical_padding = (std::max)(1, token_set.spacing.xs / 2);
    const int margin = token_set.spacing.xs;
    for (const auto& label : m_impl->labels)
    {
        QRect text_rect = metrics.boundingRect(label.text);
        text_rect.adjust(-horizontal_padding, -vertical_padding, horizontal_padding, vertical_padding);

        QPoint top_left(qRound(label.anchor.x() + margin), qRound(label.anchor.y() + margin));
        if (label.anchor.y() > visible_widget_rect.center().y())
        {
            top_left.setY(qRound(label.anchor.y() - text_rect.height() - margin));
        }
        if (label.anchor.x() > visible_widget_rect.center().x())
        {
            top_left.setX(qRound(label.anchor.x() - text_rect.width() - margin));
        }

        const QRect visible_rect = visible_widget_rect.toAlignedRect();
        top_left.setX(
            std::clamp(top_left.x(), visible_rect.left() + margin,
                       (std::max)(visible_rect.left() + margin, visible_rect.right() - text_rect.width() - margin)));
        top_left.setY(
            std::clamp(top_left.y(), visible_rect.top() + margin,
                       (std::max)(visible_rect.top() + margin, visible_rect.bottom() - text_rect.height() - margin)));

        const QRect bubble_rect(top_left, text_rect.size());
        painter.setPen(label_border_color);
        painter.setBrush(label_fill_color);
        painter.drawRoundedRect(bubble_rect, token_set.radius.sm, token_set.radius.sm);
        painter.setPen(label_text_color);
        painter.drawText(bubble_rect, Qt::AlignCenter, label.text);
    }
    painter.restore();
}

void CoordinateGridDisplayModule::onImageLoaded(ImageViewControl* /*ctrl*/)
{
    invalidateCache();
}

void CoordinateGridDisplayModule::onImageUpdated(ImageViewControl* /*ctrl*/)
{
    invalidateCache();
}

void CoordinateGridDisplayModule::onImageRenamed(ImageViewControl* /*ctrl*/)
{
    invalidateCache();
}

void CoordinateGridDisplayModule::onImageViewControlDestroyed(ImageViewControl* ctrl)
{
    ImageViewControlListener::onImageViewControlDestroyed(ctrl);
    detachFromViewWidget();
    invalidateCache();
}

int CoordinateGridDisplayModule::projectedLineCount() const
{
    return static_cast<int>(m_impl->lines.size());
}

void CoordinateGridDisplayModule::invalidateCache()
{
    m_impl->cacheValid = false;
    m_impl->hasAstrometricSolution = false;
    m_impl->lines.clear();
    m_impl->labels.clear();

    if (m_ViewWidget)
    {
        m_ViewWidget->update();
    }
}

} // namespace an::ui
