// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "controls/AstrometryOverlayUtils.h"

#include "app/AppContextHelpers.h"
#include "controls/ImageViewControl.h"
#include "controls/annotation/AnnotationScene.h"
#include "controls/annotation/AnnotationSettings.h"
#include "session/IEngineSessionProvider.h"
#include "widgets/ImageViewWidget.h"

#include <AfterNightPlatform/Logging.h>

#include <AfterNightEngine/Algorithms/Catalog/SkyCatalog.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <QCoreApplication>
#include <QMetaObject>
#include <QScrollBar>
#include <QThreadPool>

#include <algorithm>
#include <span>
#include <utility>

namespace an::ui::detail
{

OverlayViewState overlayViewState(ImageViewControl* control)
{
    OverlayViewState state;
    auto* view_widget = control ? control->getViewWidget() : nullptr;
    if (view_widget == nullptr)
    {
        return state;
    }
    state.scale = view_widget->getCurrentScaleFactor();
    if (auto* scroll_area = control->getScrollArea())
    {
        if (auto* horizontal_bar = scroll_area->horizontalScrollBar())
        {
            state.horizontalOffset = horizontal_bar->value();
        }
        if (auto* vertical_bar = scroll_area->verticalScrollBar())
        {
            state.verticalOffset = vertical_bar->value();
        }
        if (scroll_area->viewport())
        {
            state.viewportSize = scroll_area->viewport()->size();
        }
    }
    state.widgetSize = view_widget->size();
    return state;
}

QRectF visibleOverlayWidgetRect(ImageViewControl* control)
{
    if (!control || !control->getViewWidget() || !control->getScrollArea())
    {
        return {};
    }

    auto* view_widget = control->getViewWidget();
    auto* scroll_area = control->getScrollArea();
    const QRectF widget_rect = QRectF(view_widget->rect());

    auto* horizontal_bar = scroll_area->horizontalScrollBar();
    auto* vertical_bar = scroll_area->verticalScrollBar();
    if (!horizontal_bar || !vertical_bar || (!horizontal_bar->isVisible() && !vertical_bar->isVisible()))
    {
        return widget_rect;
    }

    const QRectF viewport_rect(horizontal_bar->value(), vertical_bar->value(), scroll_area->viewport()->width(),
                               scroll_area->viewport()->height());
    return widget_rect.intersected(viewport_rect);
}

std::optional<VisibleSky> visibleSky(const an::WcsSolution& solution, const QRectF& visibleImageRect)
{
    if (!visibleImageRect.isValid() || visibleImageRect.isEmpty())
    {
        return std::nullopt;
    }

    const an::Result<an::SkyCoordinate> center = solution.pixelToWorld(
        an::PixelCoordinate{.x = visibleImageRect.center().x(), .y = visibleImageRect.center().y()});
    if (!center.hasValue())
    {
        return std::nullopt;
    }

    VisibleSky sky{.center = *center, .radiusDegrees = 0.0};
    constexpr int samples_per_edge = 8;
    const auto sample_point = [&](const double pixel_x, const double pixel_y) {
        const an::Result<an::SkyCoordinate> direction =
            solution.pixelToWorld(an::PixelCoordinate{.x = pixel_x, .y = pixel_y});
        if (direction.hasValue())
        {
            sky.radiusDegrees = (std::max)(sky.radiusDegrees, an::angularSeparationDegrees(sky.center, *direction));
        }
    };

    for (int i = 0; i <= samples_per_edge; ++i)
    {
        const double t = static_cast<double>(i) / static_cast<double>(samples_per_edge);
        const double sample_x = visibleImageRect.left() + (visibleImageRect.width() * t);
        const double sample_y = visibleImageRect.top() + (visibleImageRect.height() * t);

        sample_point(sample_x, visibleImageRect.top());
        sample_point(sample_x, visibleImageRect.bottom());
        sample_point(visibleImageRect.left(), sample_y);
        sample_point(visibleImageRect.right(), sample_y);
    }

    if (!std::isfinite(sky.radiusDegrees) || sky.radiusDegrees <= 0.0)
    {
        return std::nullopt;
    }
    return sky;
}

AnnotationContent overlayAnnotationContent(const BundledCatalog catalog, const double magnitudeLimit)
{
    const bool deepSky = catalog == BundledCatalog::DeepSky;
    AnnotationContent content;
    content.layers = AnnotationLayers{
        .grid = false, .deepSky = deepSky, .stars = !deepSky, .compass = false, .scaleBar = false, .infoPanel = false};
    content.deepSky = AnnotationDeepSkySelection{};
    content.deepSky.namedOnly = false;
    content.deepSky.magnitudeLimit = magnitudeLimit;
    content.deepSky.unmeasured = magnitudeLimit >= kAnnotationNoMagnitudeLimit;
    content.deepSky.minimumSize = 0.0;
    content.deepSky.otherObjects = true;
    content.stars = AnnotationStarSelection{};
    content.stars.magnitudeLimit = magnitudeLimit;
    content.stars.naming = AnnotationStarNaming::CatalogueNumbers;
    content.stars.markUnnamed = false;
    content.labelText = deepSky ? AnnotationLabelText::Designation : AnnotationLabelText::Name;
    content.gridLabels = false;
    return content;
}

struct FieldCatalogQueries::State
{
    BundledCatalog catalog;
    double marginDegrees = 0.0;

    /// The catalog, read by the first query of its source; touched only by the one worker a query runs on at a time.
    std::shared_ptr<an::SkyCatalog> loaded;

    /// Which source the catalog was read from; touched only by that worker.
    std::uint64_t loadedGeneration = 0u;

    /// Where the catalog's text comes from, and how many times it changed; read by a query when it starts.
    BundledCatalogSource source;
    std::uint64_t sourceGeneration = 0u;

    // The rest is the GUI thread's.
    std::shared_ptr<const FieldCatalogEntries> last;
    bool asking = false;
    std::optional<an::WcsSolution> failedSolution;
    double failedLimit = 0.0;
    std::uint64_t queriesAsked = 0u;
};

FieldCatalogQueries::FieldCatalogQueries(const BundledCatalog catalog, const double marginDegrees)
    : m_state(std::make_shared<State>())
{
    m_state->catalog = catalog;
    m_state->marginDegrees = marginDegrees;
}

std::uint64_t FieldCatalogQueries::queriesAsked() const noexcept
{
    return m_state->queriesAsked;
}

void FieldCatalogQueries::setSource(BundledCatalogSource source)
{
    // The next query reads the new source; an answer still on its way from the old one is dropped when it lands.
    m_state->source = std::move(source);
    ++m_state->sourceGeneration;
    m_state->last.reset();
    m_state->failedSolution.reset();
}

namespace
{

/// What a worker found: the entries, or why there are none.
struct FieldQueryOutcome
{
    std::shared_ptr<const FieldCatalogEntries> entries;
    QString failure;
};

/// The catalog's entries inside a solution's field and a margin, at a magnitude limit, read and queried on a worker.
[[nodiscard]] FieldQueryOutcome queryField(const an::consumer::EngineSession& session, const BundledCatalog catalog,
                                           const BundledCatalogSource& source, const std::uint64_t generation,
                                           std::shared_ptr<an::SkyCatalog>& loaded, std::uint64_t& loadedGeneration,
                                           const an::WcsSolution& solution, const double magnitudeLimit,
                                           const double marginDegrees)
{
    FieldQueryOutcome outcome;
    const an::Result<an::SkyCoordinate> center = solution.fieldCenter();
    const an::Result<double> radius = solution.queryRadiusDegrees(0.0);
    if (!center.hasValue() || !radius.hasValue())
    {
        outcome.failure = QStringLiteral("the solution places no field on the sky");
        return outcome;
    }
    if (!loaded || loadedGeneration != generation)
    {
        loaded.reset();
        // The overlay asks only what lies in a field, so the catalog is built without a name index.
        an::Result<an::SkyCatalog> read = loadBundledCatalog(session, catalog, {}, source, false);
        if (!read.hasValue())
        {
            outcome.failure = QString::fromUtf8(read.status().message());
            return outcome;
        }
        loaded = std::make_shared<an::SkyCatalog>(std::move(*read));
        loadedGeneration = generation;
    }
    an::Result<an::consumer::OperationScope> scope =
        session.admit(an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Preview});
    if (!scope.hasValue())
    {
        outcome.failure = QString::fromUtf8(scope.status().message());
        return outcome;
    }
    const an::Result<an::CatalogQueryResult> found = an::queryCatalogRegion(
        scope->execution(), *loaded,
        bundledCatalogRegionQuery(catalog, *center, (std::min)(180.0, *radius + marginDegrees), magnitudeLimit));
    if (!found.hasValue())
    {
        outcome.failure = QString::fromUtf8(found.status().message());
        return outcome;
    }
    auto entries = std::make_shared<FieldCatalogEntries>(
        FieldCatalogEntries{.solution = solution, .magnitudeLimit = magnitudeLimit, .entries = {}});
    entries->entries.reserve(found->entryIndices.size());
    for (const std::uint32_t index : found->entryIndices)
    {
        if (const an::CatalogEntry* entry = loaded->entry(index); entry != nullptr)
        {
            entries->entries.push_back(*entry);
        }
    }
    scope->end();
    // The entries are kept, merged and named as an annotated picture keeps them, here on the worker.
    const bool deepSky = catalog == BundledCatalog::DeepSky;
    const std::span<const an::CatalogEntry> found_entries(entries->entries);
    entries->scene = std::make_shared<const AnnotationScene>(buildAnnotationScene(
        AnnotationSceneSources{.solution = &entries->solution,
                               .deepSky = deepSky ? found_entries : std::span<const an::CatalogEntry>(),
                               .stars = deepSky ? std::span<const an::CatalogEntry>() : found_entries,
                               .cards = {}},
        overlayAnnotationContent(catalog, magnitudeLimit)));
    outcome.entries = std::move(entries);
    return outcome;
}

} // namespace

std::shared_ptr<const FieldCatalogEntries> FieldCatalogQueries::answer(const an::WcsSolution& solution,
                                                                       const double magnitudeLimit, QWidget* repaint)
{
    State& state = *m_state;
    if (state.last && state.last->solution == solution && state.last->magnitudeLimit == magnitudeLimit)
    {
        return state.last;
    }
    if (state.asking)
    {
        return nullptr;
    }
    if (state.failedSolution.has_value() && *state.failedSolution == solution && state.failedLimit == magnitudeLimit)
    {
        return nullptr;
    }
    auto* provider = process_utils::currentEngineSessionProvider();
    if (provider == nullptr)
    {
        return nullptr;
    }

    state.asking = true;
    ++state.queriesAsked;
    const an::consumer::EngineSession* session = &provider->engineSession();
    std::weak_ptr<State> weak = m_state;
    QPointer<QWidget> widget(repaint);
    QThreadPool::globalInstance()->start([session, worker = m_state, source = state.source,
                                          generation = state.sourceGeneration, weak, widget, solution,
                                          magnitudeLimit]() {
        auto outcome = std::make_shared<FieldQueryOutcome>(queryField(*session, worker->catalog, source, generation,
                                                                      worker->loaded, worker->loadedGeneration,
                                                                      solution, magnitudeLimit, worker->marginDegrees));
        QCoreApplication* application = QCoreApplication::instance();
        if (application == nullptr)
        {
            return;
        }
        QMetaObject::invokeMethod(
            application,
            [weak, widget, outcome, generation, solution, magnitudeLimit]() {
                const std::shared_ptr<State> state = weak.lock();
                if (!state)
                {
                    return;
                }
                state->asking = false;
                if (state->sourceGeneration != generation)
                {
                    // The source changed while the query ran: the next paint asks the new one.
                }
                else if (outcome->entries)
                {
                    state->last = outcome->entries;
                    state->failedSolution.reset();
                }
                else
                {
                    LOG_WARNING("Catalog overlay query failed: {}", outcome->failure.toStdString());
                    state->failedSolution = solution;
                    state->failedLimit = magnitudeLimit;
                }
                if (widget)
                {
                    widget->update();
                }
            },
            Qt::QueuedConnection);
    });
    return nullptr;
}

} // namespace an::ui::detail
