// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "controls/CatalogAnnotationDisplayModule.h"

#include "app/BundledCatalogs.h"
#include "controls/AstrometryOverlayUtils.h"
#include "controls/ImageViewControl.h"
#include "controls/annotation/AnnotationLayout.h"
#include "controls/annotation/AnnotationRenderer.h"
#include "controls/annotation/AnnotationScene.h"
#include "controls/annotation/AnnotationSettings.h"
#include "widgets/ImageViewWidget.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QPainter>
#include <QPointer>
#include <QThreadPool>

#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

namespace an::ui
{
namespace
{

/// How far past the image's field each catalogue is asked: a large object centred outside the image still reaches
/// into it.
constexpr double kDeepSkyMarginDegrees = 2.0;
constexpr double kStarMarginDegrees = 1.0;

/// What a layout is made of: the layers' scenes, the view's scale and a label's size on the screen.
struct LayoutKey
{
    std::shared_ptr<const AnnotationScene> deepSky;
    std::shared_ptr<const AnnotationScene> stars;
    double scale = 0.0;
    double fontPixels = 0.0;

    /// Whether another layout is of the same scenes at whatever zoom: one the view draws until its own lands.
    [[nodiscard]] bool sameScenes(const LayoutKey& other) const
    {
        return deepSky == other.deepSky && stars == other.stars;
    }

    [[nodiscard]] bool matches(const LayoutKey& other) const
    {
        return sameScenes(other) && fontPixels == other.fontPixels && qFuzzyCompare(scale, other.scale);
    }
};

/// The layouts an overlay asked workers for: the last that landed and whether one is on its way. Shared with the
/// workers; an answer that lands after the overlay is gone, or after what it was made of was set aside, is never
/// drawn.
struct LayoutState
{
    std::optional<LayoutKey> drawnKey;
    detail::OverlayAnnotationLayout drawn;
    bool asking = false;
    std::uint64_t generation = 0u;
    std::uint64_t made = 0u;
};

} // namespace

struct CatalogAnnotationDisplayModule::Impl
{
    double magnitudeLimit = 12.0;
    bool deepSky = true;
    bool stars = true;

    /// The annotated picture's style: its palette, its marks and its labels' backing.
    AnnotationStyle style = defaultAnnotationSettings().style;

    /// Each catalogue's objects inside the image's field, queried on a worker, each with its layer's scene.
    detail::FieldCatalogQueries deepSkyQueries{BundledCatalog::DeepSky, kDeepSkyMarginDegrees};
    detail::FieldCatalogQueries starQueries{BundledCatalog::BrightStars, kStarMarginDegrees};

    std::shared_ptr<LayoutState> layouts = std::make_shared<LayoutState>();

    /// The scale the layout was drawn at last.
    double drawnScale = 0.0;

    /// Asks a worker for the layout of a key; its answer repaints the widget when it lands.
    void askLayout(const LayoutKey& key, QWidget* repaint)
    {
        LayoutState& state = *layouts;
        state.asking = true;
        const std::uint64_t generation = state.generation;
        const std::weak_ptr<LayoutState> weak = layouts;
        QPointer<QWidget> widget(repaint);
        QThreadPool::globalInstance()->start([key, drawnStyle = style, weak, widget, generation]() {
            auto made = std::make_shared<detail::OverlayAnnotationLayout>(detail::layoutOverlayAnnotations(
                key.deepSky.get(), key.stars.get(), key.scale, key.fontPixels, drawnStyle));
            QCoreApplication* application = QCoreApplication::instance();
            if (application == nullptr)
            {
                return;
            }
            QMetaObject::invokeMethod(
                application,
                [key, made, weak, widget, generation]() {
                    const std::shared_ptr<LayoutState> landed = weak.lock();
                    if (!landed)
                    {
                        return;
                    }
                    landed->asking = false;
                    if (landed->generation == generation)
                    {
                        landed->drawnKey = key;
                        landed->drawn = std::move(*made);
                        ++landed->made;
                    }
                    if (widget)
                    {
                        widget->update();
                    }
                },
                Qt::QueuedConnection);
        });
    }
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
        return;
    }
    Impl& impl = *m_impl;
    const double scale = view_widget->getCurrentScaleFactor();
    if (!std::isfinite(scale) || scale <= 0.0)
    {
        return;
    }

    // Each layer's scene, as its catalogue's query of the field answered: a layer whose catalogue could not be read
    // draws nothing, and the layout waits for a query still on its way.
    LayoutKey key;
    key.scale = scale;
    key.fontPixels = detail::overlayLabelPixels(*view_widget);
    bool waiting = false;
    const auto sceneOf = [&](detail::FieldCatalogQueries& queries) -> std::shared_ptr<const AnnotationScene> {
        const std::shared_ptr<const detail::FieldCatalogEntries> answer =
            queries.answer(*solution, impl.magnitudeLimit, view_widget);
        waiting = waiting || (answer == nullptr && queries.isAsking());
        return answer != nullptr ? answer->scene : nullptr;
    };
    if (impl.deepSky)
    {
        key.deepSky = sceneOf(impl.deepSkyQueries);
    }
    if (impl.stars)
    {
        key.stars = sceneOf(impl.starQueries);
    }

    LayoutState& state = *impl.layouts;
    const bool laidOut = state.drawnKey.has_value() && state.drawnKey->matches(key);
    if (!waiting && !laidOut && !state.asking && (key.deepSky != nullptr || key.stars != nullptr))
    {
        impl.askLayout(key, view_widget);
    }

    // The layout of these scenes, at the last zoom until the one at this zoom lands; what it was made of changing
    // otherwise set it aside.
    if (!state.drawnKey.has_value() || state.drawn.scene == nullptr || state.drawn.layout == nullptr ||
        (!waiting && !state.drawnKey->sameScenes(key)))
    {
        return;
    }
    const QRectF visible = detail::visibleOverlayWidgetRect(control);
    if (visible.isEmpty())
    {
        return;
    }
    impl.drawnScale = scale;
    painter.save();
    painter.scale(scale, scale);
    paintAnnotations(
        painter, *state.drawn.scene, *state.drawn.layout, impl.style,
        QRectF(visible.left() / scale, visible.top() / scale, visible.width() / scale, visible.height() / scale));
    painter.restore();
}

void CatalogAnnotationDisplayModule::onImageLoaded(ImageViewControl* /*ctrl*/)
{
    invalidate();
}

void CatalogAnnotationDisplayModule::onImageUpdated(ImageViewControl* /*ctrl*/)
{
    invalidate();
}

void CatalogAnnotationDisplayModule::onImageRenamed(ImageViewControl* /*ctrl*/)
{
    invalidate();
}

void CatalogAnnotationDisplayModule::onImageViewControlDestroyed(ImageViewControl* ctrl)
{
    ImageViewControlListener::onImageViewControlDestroyed(ctrl);
    detachFromViewWidget();
    invalidate();
}

void CatalogAnnotationDisplayModule::setMagnitudeLimit(const double magnitude_limit)
{
    if (!std::isfinite(magnitude_limit) || qFuzzyCompare(m_impl->magnitudeLimit + 1.0, magnitude_limit + 1.0))
    {
        return;
    }
    m_impl->magnitudeLimit = magnitude_limit;
    invalidate();
}

double CatalogAnnotationDisplayModule::magnitudeLimit() const
{
    return m_impl->magnitudeLimit;
}

void CatalogAnnotationDisplayModule::setLayers(const bool deepSky, const bool stars)
{
    if (deepSky == m_impl->deepSky && stars == m_impl->stars)
    {
        return;
    }
    m_impl->deepSky = deepSky;
    m_impl->stars = stars;
    invalidate();
}

bool CatalogAnnotationDisplayModule::showsDeepSky() const
{
    return m_impl->deepSky;
}

bool CatalogAnnotationDisplayModule::showsStars() const
{
    return m_impl->stars;
}

QVector<QString> CatalogAnnotationDisplayModule::projectedLabels() const
{
    QVector<QString> labels;
    const LayoutState& state = *m_impl->layouts;
    if (state.drawn.scene == nullptr || state.drawn.layout == nullptr)
    {
        return labels;
    }
    for (const AnnotationLabel& label : state.drawn.layout->labels)
    {
        if (label.role == AnnotationLabelRole::Object && label.source < state.drawn.scene->marks.size())
        {
            labels.push_back(state.drawn.scene->marks[label.source].designation);
        }
    }
    return labels;
}

QVector<QRectF> CatalogAnnotationDisplayModule::projectedLabelBoxes() const
{
    QVector<QRectF> boxes;
    const LayoutState& state = *m_impl->layouts;
    const double scale = m_impl->drawnScale;
    if (state.drawn.scene == nullptr || state.drawn.layout == nullptr || !(scale > 0.0))
    {
        return boxes;
    }
    for (const AnnotationLabel& label : state.drawn.layout->labels)
    {
        if (label.role == AnnotationLabelRole::Object && label.source < state.drawn.scene->marks.size())
        {
            boxes.push_back(QRectF(label.box.topLeft() * scale, label.box.size() * scale));
        }
    }
    return boxes;
}

std::uint64_t CatalogAnnotationDisplayModule::catalogQueriesAsked() const
{
    return m_impl->deepSkyQueries.queriesAsked() + m_impl->starQueries.queriesAsked();
}

std::uint64_t CatalogAnnotationDisplayModule::layoutsMade() const
{
    return m_impl->layouts->made;
}

void CatalogAnnotationDisplayModule::setCatalogSource(BundledCatalogSource source)
{
    m_impl->deepSkyQueries.setSource(source);
    m_impl->starQueries.setSource(std::move(source));
    invalidate();
}

void CatalogAnnotationDisplayModule::invalidate()
{
    LayoutState& state = *m_impl->layouts;
    ++state.generation;
    state.drawnKey.reset();
    state.drawn = {};
    // The fields' entries are kept: they are asked for again only when the solution, the limit or the source changes.
    if (m_ViewWidget)
    {
        m_ViewWidget->update();
    }
}

} // namespace an::ui
