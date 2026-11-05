// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "controls/BrightStarAnnotationDisplayModule.h"

#include "app/BundledCatalogs.h"
#include "controls/AstrometryOverlayUtils.h"
#include "controls/ImageViewControl.h"
#include "theme/AnDesignTokens.h"

#include <AfterNightPlatform/Logging.h>

#include <QFontMetrics>
#include <QPainter>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace an::ui
{
namespace
{

/// How far past the visible sky a star may lie and still be considered: its label may reach into the view.
constexpr double kVisibleMarginDegrees = 1.0;

/// What a projection depends on: the entries the field query answered and the view state.
struct CacheKey
{
    std::shared_ptr<const detail::FieldCatalogEntries> entries;
    detail::OverlayViewState view;

    [[nodiscard]] bool matches(const CacheKey& other) const
    {
        return entries == other.entries && view.matches(other.view);
    }
};

struct BrightStarAnnotation
{
    const an::CatalogEntry* star = nullptr;
    BrightStarAnnotationProjectedPoint projected;
    QString label;
};

} // namespace

struct BrightStarAnnotationDisplayModule::Impl
{
    CacheKey cacheKey;
    bool cacheValid = false;
    bool hasAstrometricSolution = false;
    // Default to 12.0 so the overlay starts with a reasonable visual density. The magnitude submenu overrides.
    double magnitudeLimit = 12.0;
    QVector<BrightStarAnnotation> annotations;
    QRectF cachedVisibleWidgetRect;

    /// The bright stars inside the image's field, queried on a worker.
    detail::FieldCatalogQueries queries{BundledCatalog::BrightStars, kVisibleMarginDegrees};
};

BrightStarAnnotationDisplayModule::BrightStarAnnotationDisplayModule(ImageViewControl* control)
    : ImageViewControlListener(control), m_impl(std::make_unique<Impl>())
{
}

BrightStarAnnotationDisplayModule::~BrightStarAnnotationDisplayModule()
{
    detachFromViewWidget();
}

void BrightStarAnnotationDisplayModule::onDraw(QPainter& painter)
{
    auto* control = getParentViewControl();
    auto* view_widget = control ? control->getViewWidget() : nullptr;
    if (!control || !view_widget || !control->hasImage())
    {
        return;
    }

    const std::optional<an::WcsSolution>& solution = control->imageSolution();
    if (!solution.has_value())
    {
        m_impl->cacheValid = false;
        m_impl->annotations.clear();
        return;
    }

    CacheKey key;
    key.entries = m_impl->queries.answer(*solution, m_impl->magnitudeLimit, view_widget);
    key.view = detail::overlayViewState(control);
    if (!key.entries)
    {
        // A worker is reading the catalog's stars in this field; its answer repaints the view.
        return;
    }

    if (!m_impl->cacheValid || !m_impl->cacheKey.matches(key))
    {
        m_impl->cacheKey = key;
        m_impl->cacheValid = true;
        m_impl->annotations.clear();
        m_impl->hasAstrometricSolution = false;

        const double scale = key.view.scale;
        if (!std::isfinite(scale) || scale <= 0.0)
        {
            return;
        }

        const QRectF visible_widget_rect = detail::visibleOverlayWidgetRect(control);
        const QRectF visible_image_rect(visible_widget_rect.left() / scale, visible_widget_rect.top() / scale,
                                        visible_widget_rect.width() / scale, visible_widget_rect.height() / scale);
        const std::optional<detail::VisibleSky> sky = detail::visibleSky(*solution, visible_image_rect);
        if (!sky.has_value())
        {
            return;
        }

        for (const an::CatalogEntry& star : key.entries->entries)
        {
            if (an::angularSeparationDegrees(sky->center, star.coordinate) > sky->radiusDegrees + kVisibleMarginDegrees)
            {
                continue;
            }

            const auto projected = detail::projectBrightStarAnnotation(*solution, star, scale);
            if (!projected.isValid() || !visible_widget_rect.adjusted(-8.0, -8.0, 8.0, 8.0).contains(projected.center))
            {
                continue;
            }

            BrightStarAnnotation annotation;
            annotation.star = &star;
            annotation.projected = projected;
            annotation.label = detail::formatBrightStarAnnotationLabel(star);
            if (annotation.label.isEmpty())
            {
                continue;
            }
            m_impl->annotations.append(std::move(annotation));
        }

        m_impl->hasAstrometricSolution = true;
        m_impl->cachedVisibleWidgetRect = visible_widget_rect;
    }

    if (!m_impl->hasAstrometricSolution || m_impl->annotations.isEmpty())
    {
        return;
    }

    const auto& token_set = an::ui::tokens();
    QColor stroke_color = token_set.colors.statusWarning;
    stroke_color.setAlpha(224);
    QColor dot_fill_color = token_set.colors.statusWarningSoft;
    dot_fill_color.setAlpha(176);
    QColor label_text_color = token_set.colors.textPrimary;
    label_text_color.setAlpha(235);
    QColor label_fill_color = token_set.colors.bgSurface;
    label_fill_color.setAlpha(205);
    QColor label_border_color = token_set.colors.borderSubtle;
    label_border_color.setAlpha(215);
    const QRect visible_rect = m_impl->cachedVisibleWidgetRect.toAlignedRect();

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setFont(token_set.typography.caption);
    const QFontMetrics metrics(painter.font());
    const int horizontal_padding = token_set.spacing.xs;
    const int vertical_padding = (std::max)(1, token_set.spacing.xs / 2);
    const int margin = token_set.spacing.xs;

    QPen pen(stroke_color);
    pen.setCosmetic(true);
    pen.setWidthF(1.0);
    painter.setPen(pen);

    for (const BrightStarAnnotation& annotation : m_impl->annotations)
    {
        if (!annotation.star || !annotation.projected.isValid())
        {
            continue;
        }

        const QPointF center = annotation.projected.center;
        painter.setBrush(dot_fill_color);
        painter.drawEllipse(center, 2.5, 2.5);
        painter.drawLine(center + QPointF(-4.0, 0.0), center + QPointF(4.0, 0.0));
        painter.drawLine(center + QPointF(0.0, -4.0), center + QPointF(0.0, 4.0));

        QRect text_rect = metrics.boundingRect(annotation.label);
        text_rect.adjust(-horizontal_padding, -vertical_padding, horizontal_padding, vertical_padding);

        QPoint top_left(qRound(center.x() + margin), qRound(center.y() - text_rect.height() - margin));
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
        painter.drawText(bubble_rect, Qt::AlignCenter, annotation.label);
        painter.setPen(pen);
    }
    painter.restore();
}

void BrightStarAnnotationDisplayModule::onImageLoaded(ImageViewControl* /*ctrl*/)
{
    invalidateCache();
}

void BrightStarAnnotationDisplayModule::onImageUpdated(ImageViewControl* /*ctrl*/)
{
    invalidateCache();
}

void BrightStarAnnotationDisplayModule::onImageRenamed(ImageViewControl* /*ctrl*/)
{
    invalidateCache();
}

void BrightStarAnnotationDisplayModule::onImageViewControlDestroyed(ImageViewControl* ctrl)
{
    ImageViewControlListener::onImageViewControlDestroyed(ctrl);
    detachFromViewWidget();
    invalidateCache();
}

void BrightStarAnnotationDisplayModule::setMagnitudeLimit(const double magnitude_limit)
{
    if (!std::isfinite(magnitude_limit))
    {
        return;
    }

    if (qFuzzyCompare(m_impl->magnitudeLimit + 1.0, magnitude_limit + 1.0))
    {
        return;
    }

    m_impl->magnitudeLimit = magnitude_limit;
    invalidateCache();
}

double BrightStarAnnotationDisplayModule::magnitudeLimit() const
{
    return m_impl->magnitudeLimit;
}

QVector<QString> BrightStarAnnotationDisplayModule::projectedLabels() const
{
    QVector<QString> labels;
    for (const BrightStarAnnotation& annotation : m_impl->annotations)
    {
        labels.push_back(annotation.label);
    }
    return labels;
}

std::uint64_t BrightStarAnnotationDisplayModule::catalogQueriesAsked() const
{
    return m_impl->queries.queriesAsked();
}

void BrightStarAnnotationDisplayModule::setCatalogSource(BundledCatalogSource source)
{
    m_impl->queries.setSource(std::move(source));
    invalidateCache();
}

void BrightStarAnnotationDisplayModule::invalidateCache()
{
    m_impl->cacheValid = false;
    m_impl->hasAstrometricSolution = false;
    m_impl->annotations.clear();
    // The field's entries are kept: they are asked for again only when the solution or the limit changes.

    if (m_ViewWidget)
    {
        m_ViewWidget->update();
    }
}

} // namespace an::ui
