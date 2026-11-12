// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "controls/CoordinateGridDisplayModule.h"

#include "controls/AstrometryOverlayUtils.h"
#include "controls/ImageViewControl.h"
#include "controls/annotation/AnnotationLayout.h"
#include "controls/annotation/AnnotationRenderer.h"
#include "controls/annotation/AnnotationScene.h"
#include "controls/annotation/AnnotationSettings.h"
#include "widgets/ImageViewWidget.h"

#include <AfterNightEngine/Algorithms/Astrometry/WcsSolution.h>

#include <QPainter>

#include <cmath>
#include <optional>

namespace an::ui
{
namespace
{

/// What the grid laid out depends on: the solution, the view state and a label's size on the screen.
struct CacheKey
{
    std::optional<an::WcsSolution> solution;
    detail::OverlayViewState view;
    double fontPixels = 0.0;

    [[nodiscard]] bool matches(const CacheKey& other) const
    {
        return solution == other.solution && view.matches(other.view) && fontPixels == other.fontPixels;
    }
};

} // namespace

struct CoordinateGridDisplayModule::Impl
{
    CacheKey cacheKey;
    bool cacheValid = false;

    /// The grid of the part shown, laid out for the view state of the key.
    detail::OverlayGridLayout grid;

    /// The palette the grid is drawn in.
    AnnotationPalette palette = AnnotationPalette::Aurora;

    /// The text size its labels are drawn at.
    AnnotationViewTextSize textSize = AnnotationViewTextSize::Medium;

    /// The annotated picture's style in that palette: its grid's lines and labels.
    AnnotationStyle style = defaultAnnotationSettings().style;
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

    Impl& impl = *m_impl;
    CacheKey key;
    key.solution = control->imageSolution();
    key.view = detail::overlayViewState(control);
    key.fontPixels = detail::overlayLabelPixels(*view_widget, impl.textSize);
    if (!impl.cacheValid || !impl.cacheKey.matches(key))
    {
        // The grid follows the view: the part it shows, at its scale, is laid out again for every scroll and zoom.
        impl.cacheKey = key;
        impl.cacheValid = true;
        impl.grid = {};
        const double scale = key.view.scale;
        if (key.solution.has_value() && std::isfinite(scale) && scale > 0.0)
        {
            const QRectF visible = detail::visibleOverlayWidgetRect(control);
            const QRectF part(visible.left() / scale, visible.top() / scale, visible.width() / scale,
                              visible.height() / scale);
            impl.grid = detail::layoutOverlayGrid(*key.solution, part, scale, key.fontPixels, impl.style);
        }
    }

    if (impl.grid.scene == nullptr || impl.grid.layout == nullptr || impl.grid.scene->grid.empty())
    {
        return;
    }
    // The scene is in its own units, a whole number of them to an image pixel, from the corner of the part shown.
    const double scale = impl.cacheKey.view.scale;
    const double units = impl.grid.unitsPerPixel;
    painter.save();
    painter.scale(scale / units, scale / units);
    painter.translate(impl.grid.origin * units);
    paintAnnotations(painter, *impl.grid.scene, *impl.grid.layout, impl.style);
    painter.restore();
}

void CoordinateGridDisplayModule::onImageLoaded(ImageViewControl* /*ctrl*/)
{
    invalidate();
}

void CoordinateGridDisplayModule::onImageUpdated(ImageViewControl* /*ctrl*/)
{
    invalidate();
}

void CoordinateGridDisplayModule::onImageRenamed(ImageViewControl* /*ctrl*/)
{
    invalidate();
}

void CoordinateGridDisplayModule::onImageViewControlDestroyed(ImageViewControl* ctrl)
{
    ImageViewControlListener::onImageViewControlDestroyed(ctrl);
    detachFromViewWidget();
    invalidate();
}

void CoordinateGridDisplayModule::setPalette(const AnnotationPalette palette)
{
    Impl& impl = *m_impl;
    const AnnotationPalette drawn = palette == AnnotationPalette::Custom ? AnnotationPalette::Aurora : palette;
    if (drawn == impl.palette)
    {
        return;
    }
    // The colours alone change: the layout stays and the grid is drawn again.
    impl.palette = drawn;
    impl.style.colors = annotationPaletteColors(drawn);
    if (m_ViewWidget)
    {
        m_ViewWidget->update();
    }
}

AnnotationPalette CoordinateGridDisplayModule::palette() const
{
    return m_impl->palette;
}

void CoordinateGridDisplayModule::setTextSize(const AnnotationViewTextSize size)
{
    Impl& impl = *m_impl;
    if (size == impl.textSize)
    {
        return;
    }
    // The label's size is part of what the grid was laid out for: the next paint lays it out again.
    impl.textSize = size;
    if (m_ViewWidget)
    {
        m_ViewWidget->update();
    }
}

AnnotationViewTextSize CoordinateGridDisplayModule::textSize() const
{
    return m_impl->textSize;
}

int CoordinateGridDisplayModule::projectedLineCount() const
{
    return m_impl->grid.scene != nullptr ? static_cast<int>(m_impl->grid.scene->grid.size()) : 0;
}

QVector<QString> CoordinateGridDisplayModule::projectedLabels() const
{
    QVector<QString> labels;
    const detail::OverlayGridLayout& grid = m_impl->grid;
    if (grid.scene == nullptr || grid.layout == nullptr)
    {
        return labels;
    }
    for (const AnnotationLabel& label : grid.layout->labels)
    {
        if (label.role == AnnotationLabelRole::Grid && label.source < grid.scene->grid.size())
        {
            labels.push_back(grid.scene->grid[label.source].label);
        }
    }
    return labels;
}

QVector<QRectF> CoordinateGridDisplayModule::projectedLabelBoxes() const
{
    QVector<QRectF> boxes;
    const detail::OverlayGridLayout& grid = m_impl->grid;
    const double scale = m_impl->cacheKey.view.scale;
    if (grid.scene == nullptr || grid.layout == nullptr || !(scale > 0.0))
    {
        return boxes;
    }
    for (const AnnotationLabel& label : grid.layout->labels)
    {
        if (label.role == AnnotationLabelRole::Grid && label.source < grid.scene->grid.size())
        {
            // From the scene's units to image pixels, then to the widget's.
            const double units = grid.unitsPerPixel;
            const QRectF box = QRectF(label.box.topLeft() / units, label.box.size() / units).translated(grid.origin);
            boxes.push_back(QRectF(box.topLeft() * scale, box.size() * scale));
        }
    }
    return boxes;
}

void CoordinateGridDisplayModule::invalidate()
{
    m_impl->cacheValid = false;
    m_impl->grid = {};
    if (m_ViewWidget)
    {
        m_ViewWidget->update();
    }
}

} // namespace an::ui
