// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "controls/BrightStarAnnotationDisplayModule.h"

#include "app/BundledCatalogs.h"
#include "controls/AstrometryOverlayUtils.h"
#include "controls/ImageViewControl.h"
#include "controls/annotation/AnnotationLayout.h"
#include "controls/annotation/AnnotationScene.h"
#include "controls/annotation/AnnotationSettings.h"
#include "theme/AnDesignTokens.h"

#include <AfterNightPlatform/Logging.h>

#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace an::ui
{
namespace
{

/// How far past the image's field the catalog is asked.
constexpr double kFieldMarginDegrees = 1.0;

/// The most stars labelled at once. Labels never cover one another, so a view rarely holds this many; the bound
/// keeps a wide field zoomed out to a fixed cost however many stars the catalog has in it.
constexpr qsizetype kMostLabels = 600;

/// How far a star's marker reaches from its centre, in widget pixels.
constexpr double kMarkerReach = 4.0;

/// The boxes placed so far, kept in cells so a test reads only the boxes near the box it tests.
class PlacedBoxes
{
public:
    explicit PlacedBoxes(const QRectF& area) : m_area(area)
    {
        m_columns = (std::max)(1, static_cast<int>(std::ceil(area.width() / kCell)));
        m_rows = (std::max)(1, static_cast<int>(std::ceil(area.height() / kCell)));
        m_cells.resize(static_cast<std::size_t>(m_columns) * static_cast<std::size_t>(m_rows));
    }

    [[nodiscard]] bool intersects(const QRectF& box) const
    {
        bool met = false;
        visit(box, [this, &box, &met](const std::vector<std::uint32_t>& cell) {
            for (const std::uint32_t index : cell)
            {
                if (m_boxes[index].intersects(box))
                {
                    met = true;
                    return true;
                }
            }
            return false;
        });
        return met;
    }

    void add(const QRectF& box)
    {
        const auto index = static_cast<std::uint32_t>(m_boxes.size());
        m_boxes.push_back(box);
        visit(box, [index](std::vector<std::uint32_t>& cell) {
            cell.push_back(index);
            return false;
        });
    }

private:
    static constexpr double kCell = 64.0;

    template <typename CellsT, typename VisitT> static void visitCells(CellsT& self, const QRectF& box, VisitT&& visit)
    {
        const int left =
            std::clamp(static_cast<int>(std::floor((box.left() - self.m_area.left()) / kCell)), 0, self.m_columns - 1);
        const int right =
            std::clamp(static_cast<int>(std::floor((box.right() - self.m_area.left()) / kCell)), 0, self.m_columns - 1);
        const int top =
            std::clamp(static_cast<int>(std::floor((box.top() - self.m_area.top()) / kCell)), 0, self.m_rows - 1);
        const int bottom =
            std::clamp(static_cast<int>(std::floor((box.bottom() - self.m_area.top()) / kCell)), 0, self.m_rows - 1);
        for (int row = top; row <= bottom; ++row)
        {
            for (int column = left; column <= right; ++column)
            {
                if (visit(self.m_cells[static_cast<std::size_t>(row) * static_cast<std::size_t>(self.m_columns) +
                                       static_cast<std::size_t>(column)]))
                {
                    return;
                }
            }
        }
    }

    template <typename VisitT> void visit(const QRectF& box, VisitT&& visit) const
    {
        visitCells(*this, box, std::forward<VisitT>(visit));
    }

    template <typename VisitT> void visit(const QRectF& box, VisitT&& visit)
    {
        visitCells(*this, box, std::forward<VisitT>(visit));
    }

    QRectF m_area;
    int m_columns = 1;
    int m_rows = 1;
    std::vector<QRectF> m_boxes;
    std::vector<std::vector<std::uint32_t>> m_cells;
};

/// What a label's box is measured with: the theme's caption face and its spacing, read in the paint.
struct LabelGeometry
{
    QFont font;
    int horizontalPadding = 0;
    int verticalPadding = 0;
    int margin = 0;

    [[nodiscard]] bool operator==(const LabelGeometry& other) const
    {
        return font == other.font && horizontalPadding == other.horizontalPadding &&
               verticalPadding == other.verticalPadding && margin == other.margin;
    }
};

[[nodiscard]] LabelGeometry currentLabelGeometry()
{
    const auto& tokenSet = an::ui::tokens();
    return LabelGeometry{.font = tokenSet.typography.caption,
                         .horizontalPadding = tokenSet.spacing.xs,
                         .verticalPadding = (std::max)(1, tokenSet.spacing.xs / 2),
                         .margin = tokenSet.spacing.xs};
}

/// What a projection depends on: the entries the field query answered, the view state and what labels are measured
/// with.
struct CacheKey
{
    std::shared_ptr<const detail::FieldCatalogEntries> entries;
    detail::OverlayViewState view;
    LabelGeometry geometry;

    [[nodiscard]] bool matches(const CacheKey& other) const
    {
        return entries == other.entries && view.matches(other.view) && geometry == other.geometry;
    }
};

struct BrightStarAnnotation
{
    /// The scene's mark, held by the field's answer the cache keeps.
    const AnnotationMark* mark = nullptr;
    BrightStarAnnotationProjectedPoint projected;
    QString label;

    /// Where the label is drawn, in widget pixels.
    QRect bubble;
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

    /// The bright stars inside the image's field, queried on a worker.
    detail::FieldCatalogQueries queries{BundledCatalog::BrightStars, kFieldMarginDegrees};
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
    key.geometry = currentLabelGeometry();
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
        const std::shared_ptr<const AnnotationScene>& scene = key.entries->scene;
        if (scene == nullptr || visible_widget_rect.isEmpty())
        {
            return;
        }
        const AnnotationLabelText text =
            detail::overlayAnnotationContent(BundledCatalog::BrightStars, m_impl->magnitudeLimit).labelText;

        // The scene's stars come in the order an annotated picture labels them: a star of a proper name before one of a
        // designation alone, each brighter before fainter. Each star's label is measured once here rather than in every
        // paint, and a star whose label or marker would cover one placed before it is left out, so a rich field shows
        // its most prominent stars legibly and the work a paint does is bounded by the view's area, not by the
        // catalog's depth.
        const LabelGeometry& geometry = key.geometry;
        const QFontMetrics metrics(geometry.font);
        const QRect visible_rect = visible_widget_rect.toAlignedRect();
        PlacedBoxes placed(visible_widget_rect.adjusted(-16.0, -16.0, 16.0, 16.0));
        for (const AnnotationMark& mark : scene->marks)
        {
            if (m_impl->annotations.size() >= kMostLabels)
            {
                break;
            }

            BrightStarAnnotationProjectedPoint projected;
            projected.center = mark.center * scale;
            projected.valid = true;
            if (!visible_widget_rect.adjusted(-8.0, -8.0, 8.0, 8.0).contains(projected.center))
            {
                continue;
            }

            BrightStarAnnotation annotation;
            annotation.mark = &mark;
            annotation.projected = projected;
            annotation.label = annotationLabelLines(mark, text).first;
            if (annotation.label.isEmpty())
            {
                continue;
            }

            const QPointF center = projected.center;
            QRect text_rect = metrics.boundingRect(annotation.label);
            text_rect.adjust(-geometry.horizontalPadding, -geometry.verticalPadding, geometry.horizontalPadding,
                             geometry.verticalPadding);
            QPoint top_left(qRound(center.x() + geometry.margin),
                            qRound(center.y() - text_rect.height() - geometry.margin));
            top_left.setX(std::clamp(top_left.x(), visible_rect.left() + geometry.margin,
                                     (std::max)(visible_rect.left() + geometry.margin,
                                                visible_rect.right() - text_rect.width() - geometry.margin)));
            top_left.setY(std::clamp(top_left.y(), visible_rect.top() + geometry.margin,
                                     (std::max)(visible_rect.top() + geometry.margin,
                                                visible_rect.bottom() - text_rect.height() - geometry.margin)));
            annotation.bubble = QRect(top_left, text_rect.size());
            const QRectF marker(center.x() - kMarkerReach, center.y() - kMarkerReach, 2.0 * kMarkerReach,
                                2.0 * kMarkerReach);
            if (placed.intersects(QRectF(annotation.bubble)) || placed.intersects(marker))
            {
                continue;
            }
            placed.add(QRectF(annotation.bubble));
            placed.add(marker);
            m_impl->annotations.append(std::move(annotation));
        }

        m_impl->hasAstrometricSolution = true;
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

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setFont(m_impl->cacheKey.geometry.font);

    QPen pen(stroke_color);
    pen.setCosmetic(true);
    pen.setWidthF(1.0);
    painter.setPen(pen);

    for (const BrightStarAnnotation& annotation : m_impl->annotations)
    {
        if (annotation.mark == nullptr || !annotation.projected.isValid())
        {
            continue;
        }

        const QPointF center = annotation.projected.center;
        painter.setBrush(dot_fill_color);
        painter.drawEllipse(center, 2.5, 2.5);
        painter.drawLine(center + QPointF(-kMarkerReach, 0.0), center + QPointF(kMarkerReach, 0.0));
        painter.drawLine(center + QPointF(0.0, -kMarkerReach), center + QPointF(0.0, kMarkerReach));

        const QRect& bubble_rect = annotation.bubble;
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
