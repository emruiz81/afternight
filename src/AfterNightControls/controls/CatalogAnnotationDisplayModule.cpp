// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "controls/CatalogAnnotationDisplayModule.h"

#include "app/BundledCatalogs.h"
#include "controls/AstrometryOverlayUtils.h"
#include "controls/ImageViewControl.h"
#include "controls/annotation/AnnotationLayout.h"
#include "controls/annotation/AnnotationScene.h"
#include "controls/annotation/AnnotationSettings.h"
#include "theme/AnDesignTokens.h"

#include <AfterNightPlatform/Logging.h>

#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QScrollBar>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <utility>

namespace an::ui
{
namespace
{

/// How close two objects' centres may be drawn, in widget pixels, before the less prominent is left out. The entries of
/// one object are one mark already (`buildAnnotationScene`); this keeps distinct objects drawn over one another apart.
constexpr double kDeclutterRadiusPx = 12.0;

/// The size from which an object's outline is drawn heavier, in arcminutes.
constexpr double kWideObjectArcminutes = 20.0;

struct CatalogAnnotationLabel
{
    QString text;
    QPointF anchor;
};

/// How far past the image's field the catalog is asked: a large object centred outside the image still reaches into it.
constexpr double kFieldMarginDegrees = 2.0;

struct CachedAnnotation
{
    /// The scene's mark, held by the field's answer the cache keeps.
    const AnnotationMark* mark = nullptr;
    CatalogAnnotationProjectedShape shape;
    CatalogAnnotationLabel label;

    /// Whether the object spans `kWideObjectArcminutes` or more.
    bool wide = false;
};

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

[[nodiscard]] QPainterPath buildClosedPath(const QVector<QPointF>& points)
{
    QPainterPath path;
    if (points.isEmpty())
    {
        return path;
    }

    path.moveTo(points.front());
    for (int i = 1; i < points.size(); ++i)
    {
        path.lineTo(points[i]);
    }
    if (points.size() >= 3)
    {
        path.closeSubpath();
    }
    return path;
}

[[nodiscard]] bool annotationTouchesVisibleRect(const CatalogAnnotationProjectedShape& shape,
                                                const QRectF& visible_rect)
{
    if (!visible_rect.isValid() || visible_rect.isEmpty())
    {
        return false;
    }

    if (visible_rect.adjusted(-2.0, -2.0, 2.0, 2.0).contains(shape.center))
    {
        return true;
    }

    for (const QPointF& point : shape.outline)
    {
        if (visible_rect.adjusted(-2.0, -2.0, 2.0, 2.0).contains(point))
        {
            return true;
        }
    }

    if (shape.outline.size() >= 3)
    {
        return buildClosedPath(shape.outline).intersects(visible_rect);
    }

    return false;
}

/// A mark's label: the first line of the text the overlay's choices give it, its designation, above its outline.
[[nodiscard]] CatalogAnnotationLabel buildCatalogAnnotationLabel(const AnnotationMark& mark,
                                                                 const AnnotationLabelText text,
                                                                 const CatalogAnnotationProjectedShape& shape)
{
    CatalogAnnotationLabel label;
    label.text = annotationLabelLines(mark, text).first;
    if (!shape.outline.isEmpty())
    {
        const QRectF bounds = buildClosedPath(shape.outline).boundingRect();
        label.anchor = QPointF(bounds.center().x(), bounds.top());
    }
    else
    {
        label.anchor = shape.center;
    }
    return label;
}

/// The colour an object of a kind is outlined in.
[[nodiscard]] QColor kindStrokeColor(const AnnotationMarkKind kind, const DesignTokens& token_set)
{
    switch (kind)
    {
    case AnnotationMarkKind::Galaxy:
        return token_set.colors.accentPrimary;
    case AnnotationMarkKind::OpenCluster:
    case AnnotationMarkKind::GlobularCluster:
        return token_set.colors.statusSuccess;
    case AnnotationMarkKind::PlanetaryNebula:
        return token_set.colors.statusWarning;
    case AnnotationMarkKind::Nebula:
    case AnnotationMarkKind::OtherObject:
    case AnnotationMarkKind::Star:
        break;
    }
    return token_set.colors.statusInfo;
}

/// How much sky an outline spans across, in arcminutes: twice its farthest point's distance from its centre.
[[nodiscard]] double outlineSpanArcminutes(const AnnotationMark& mark, const double arcsecondsPerPixel)
{
    double farthest = 0.0;
    for (const QPointF& point : mark.outline)
    {
        farthest = (std::max)(farthest, std::hypot(point.x() - mark.center.x(), point.y() - mark.center.y()));
    }
    return 2.0 * farthest * arcsecondsPerPixel / 60.0;
}

} // namespace

struct CatalogAnnotationDisplayModule::Impl
{
    CacheKey cacheKey;
    bool cacheValid = false;
    bool hasAstrometricSolution = false;
    double magnitudeLimit = 12.0;
    QVector<CachedAnnotation> annotations;
    QRectF cachedVisibleWidgetRect;

    /// The deep-sky objects inside the image's field, queried on a worker.
    detail::FieldCatalogQueries queries{BundledCatalog::DeepSky, kFieldMarginDegrees};
};

CatalogAnnotationDisplayModule::CatalogAnnotationDisplayModule(ImageViewControl* control)
    : ImageViewControlListener(control), m_impl(std::make_unique<Impl>())
{
}

CatalogAnnotationDisplayModule::~CatalogAnnotationDisplayModule()
{
    detachFromViewWidget();
}

void CatalogAnnotationDisplayModule::onDraw(QPainter& painter)
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
        // A worker is reading the catalog's objects in this field; its answer repaints the view.
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
        const std::shared_ptr<const AnnotationScene>& scene = key.entries->scene;
        if (scene == nullptr || visible_widget_rect.isEmpty())
        {
            return;
        }
        if (scene->marks.empty())
        {
            LOG_DEBUG("Catalog annotation overlay: 0 objects in the field at magnitude <= {:.1f}",
                      m_impl->magnitudeLimit);
        }
        const AnnotationLabelText text =
            detail::overlayAnnotationContent(BundledCatalog::DeepSky, m_impl->magnitudeLimit).labelText;
        const double arcseconds_per_pixel = solution->meanPixelScaleArcsecPerPixel();

        // The scene holds the objects an annotated picture keeps, the entries of one object merged under one
        // designation, most prominent first; each is drawn at the view's scale.
        for (const AnnotationMark& mark : scene->marks)
        {
            CatalogAnnotationProjectedShape shape;
            shape.center = mark.center * scale;
            shape.outline.reserve(mark.outline.size());
            for (const QPointF& point : mark.outline)
            {
                shape.outline.append(point * scale);
            }
            if (!annotationTouchesVisibleRect(shape, visible_widget_rect))
            {
                continue;
            }

            // Distinct objects drawn over one another: the more prominent, drawn first, is kept.
            const bool crowded = std::any_of(m_impl->annotations.cbegin(), m_impl->annotations.cend(),
                                             [&shape](const CachedAnnotation& existing) {
                                                 const double dx = existing.shape.center.x() - shape.center.x();
                                                 const double dy = existing.shape.center.y() - shape.center.y();
                                                 return (dx * dx + dy * dy) < (kDeclutterRadiusPx * kDeclutterRadiusPx);
                                             });
            if (crowded)
            {
                continue;
            }

            CachedAnnotation annotation;
            annotation.mark = &mark;
            annotation.label = buildCatalogAnnotationLabel(mark, text, shape);
            annotation.wide = outlineSpanArcminutes(mark, arcseconds_per_pixel) >= kWideObjectArcminutes;
            annotation.shape = std::move(shape);
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

    for (const CachedAnnotation& annotation : m_impl->annotations)
    {
        if (annotation.mark == nullptr)
        {
            continue;
        }

        QColor stroke_color = kindStrokeColor(annotation.mark->kind, token_set);
        QColor fill_color = stroke_color;
        fill_color.setAlpha(32);
        stroke_color.setAlpha(220);

        QPen pen(stroke_color);
        pen.setCosmetic(true);
        pen.setWidthF(annotation.wide ? 1.4 : 1.0);
        painter.setPen(pen);
        painter.setBrush(fill_color);

        if (annotation.shape.outline.size() >= 3)
        {
            painter.drawPath(buildClosedPath(annotation.shape.outline));
        }
        else
        {
            painter.drawEllipse(annotation.shape.center, 6.0, 6.0);
        }

        painter.drawLine(annotation.shape.center + QPointF(-4.0, 0.0), annotation.shape.center + QPointF(4.0, 0.0));
        painter.drawLine(annotation.shape.center + QPointF(0.0, -4.0), annotation.shape.center + QPointF(0.0, 4.0));

        QRect text_rect = metrics.boundingRect(annotation.label.text);
        text_rect.adjust(-horizontal_padding, -vertical_padding, horizontal_padding, vertical_padding);

        // Center the label horizontally above the anchor, with a small gap
        QPoint top_left(qRound(annotation.label.anchor.x() - text_rect.width() / 2),
                        qRound(annotation.label.anchor.y() - text_rect.height() - margin));
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
        painter.drawText(bubble_rect, Qt::AlignCenter, annotation.label.text);
    }
    painter.restore();
}

void CatalogAnnotationDisplayModule::onImageLoaded(ImageViewControl* /*ctrl*/)
{
    invalidateCache();
}

void CatalogAnnotationDisplayModule::onImageUpdated(ImageViewControl* /*ctrl*/)
{
    invalidateCache();
}

void CatalogAnnotationDisplayModule::onImageRenamed(ImageViewControl* /*ctrl*/)
{
    invalidateCache();
}

void CatalogAnnotationDisplayModule::onImageViewControlDestroyed(ImageViewControl* ctrl)
{
    ImageViewControlListener::onImageViewControlDestroyed(ctrl);
    detachFromViewWidget();
    invalidateCache();
}

void CatalogAnnotationDisplayModule::setMagnitudeLimit(const double magnitude_limit)
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

double CatalogAnnotationDisplayModule::magnitudeLimit() const
{
    return m_impl->magnitudeLimit;
}

QVector<QString> CatalogAnnotationDisplayModule::projectedLabels() const
{
    QVector<QString> labels;
    for (const CachedAnnotation& annotation : m_impl->annotations)
    {
        labels.push_back(annotation.label.text);
    }
    return labels;
}

std::uint64_t CatalogAnnotationDisplayModule::catalogQueriesAsked() const
{
    return m_impl->queries.queriesAsked();
}

void CatalogAnnotationDisplayModule::setCatalogSource(BundledCatalogSource source)
{
    m_impl->queries.setSource(std::move(source));
    invalidateCache();
}

void CatalogAnnotationDisplayModule::invalidateCache()
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
