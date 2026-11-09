// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "controls/AstrometryOverlayUtils.h"

#include "app/AppContextHelpers.h"
#include "controls/ImageViewControl.h"
#include "controls/annotation/AnnotationLayout.h"
#include "controls/annotation/AnnotationScene.h"
#include "controls/annotation/AnnotationSettings.h"
#include "session/IEngineSessionProvider.h"
#include "theme/AnDesignTokens.h"
#include "widgets/ImageViewWidget.h"

#include <AfterNightPlatform/Logging.h>

#include <AfterNightEngine/Algorithms/Catalog/SkyCatalog.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <QCoreApplication>
#include <QFont>
#include <QMetaObject>
#include <QScrollBar>
#include <QThreadPool>

#include <algorithm>
#include <cmath>
#include <limits>
#include <span>
#include <utility>
#include <vector>

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
    QWidget* viewport = scroll_area->viewport();
    if (!horizontal_bar || !vertical_bar || viewport == nullptr)
    {
        return widget_rect;
    }

    // What the viewport shows at the scroll offsets, whether its scroll bars are drawn or not - a view not shown yet
    // has a viewport and offsets, but no bar of it is visible: the whole widget when it fits, else the viewport's part.
    const QRectF viewport_rect(horizontal_bar->value(), vertical_bar->value(), viewport->width(), viewport->height());
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

namespace
{

/// The level a view's overlay names at: a custom level, which a view does not offer, is the standard one.
[[nodiscard]] AnnotationContent levelContent(const AnnotationDetail detail)
{
    AnnotationContent content;
    applyAnnotationDetail(content, detail == AnnotationDetail::Custom ? AnnotationDetail::Standard : detail);
    return content;
}

} // namespace

AnnotationContent overlayAnnotationContent(const BundledCatalog catalog, const AnnotationDetail detail)
{
    const bool deepSky = catalog == BundledCatalog::DeepSky;
    AnnotationContent content = levelContent(detail);
    content.layers = AnnotationLayers{
        .grid = false, .deepSky = deepSky, .stars = !deepSky, .compass = false, .scaleBar = false, .infoPanel = false};
    // Every size: the view holds an object to the level's smallest size for the part it shows as it lays it out.
    content.deepSky.minimumSize = 0.0;
    content.gridLabels = false;
    return content;
}

double overlaySmallestObject(const AnnotationDetail detail)
{
    return levelContent(detail).deepSky.minimumSize;
}

double overlayShownReferenceLength(const QSize imageSize, const QSizeF viewportSize, const double scale) noexcept
{
    if (imageSize.isEmpty() || viewportSize.isEmpty() || !std::isfinite(scale) || !(scale > 0.0))
    {
        return 0.0;
    }
    const double width = (std::min)(static_cast<double>(imageSize.width()), viewportSize.width() / scale);
    const double height = (std::min)(static_cast<double>(imageSize.height()), viewportSize.height() / scale);
    const double length = std::sqrt(width * height);
    constexpr double kStepsPerDoubling = 4.0;
    return std::exp2(std::round(std::log2(length) * kStepsPerDoubling) / kStepsPerDoubling);
}

std::optional<double> overlayQueryMagnitudeLimit(const BundledCatalog catalog, const AnnotationContent& content)
{
    if (catalog == BundledCatalog::BrightStars)
    {
        return content.stars.magnitudeLimit;
    }
    const AnnotationDeepSkySelection& selection = content.deepSky;
    if (selection.unmeasured || selection.magnitudeLimit >= kAnnotationNoMagnitudeLimit)
    {
        return std::nullopt;
    }
    return selection.magnitudeLimit;
}

OverlayAnnotationLayout layoutOverlayAnnotations(const AnnotationScene* deepSky, const AnnotationScene* stars,
                                                 const double scale, const double fontPixels,
                                                 const AnnotationStyle& style, const AnnotationLabelText labelText,
                                                 const double smallestMajorPixels)
{
    OverlayAnnotationLayout result;
    const AnnotationScene* frame = deepSky != nullptr ? deepSky : stars;
    if (frame == nullptr || frame->imageSize.isEmpty() || !(frame->referenceLength > 0.0) || !std::isfinite(scale) ||
        !(scale > 0.0) || !std::isfinite(fontPixels) || !(fontPixels > 0.0))
    {
        return result;
    }
    const double unitFont = annotationMetrics(1.0, style).fontSize;
    if (!(unitFont > 0.0))
    {
        return result;
    }

    AnnotationScene merged;
    merged.imageSize = frame->imageSize;
    merged.referenceLength = frame->referenceLength;
    // A deep-sky object too small for what is shown is left out, unless it is notable or states no size.
    const auto tooSmall = [smallestMajorPixels](const AnnotationMark& mark) {
        return mark.kind != AnnotationMarkKind::Star && !mark.notable && mark.majorPixels > 0.0 &&
               mark.majorPixels < smallestMajorPixels;
    };
    for (const AnnotationScene* layer : {deepSky, stars})
    {
        if (layer == nullptr)
        {
            continue;
        }
        for (const AnnotationMark& mark : layer->marks)
        {
            if (!tooSmall(mark))
            {
                merged.marks.push_back(mark);
            }
        }
    }
    // The order a scene of both layers is built in: the most prominent first, then by designation.
    std::stable_sort(merged.marks.begin(), merged.marks.end(),
                     [](const AnnotationMark& left, const AnnotationMark& right) {
                         if (left.priority != right.priority)
                         {
                             return left.priority > right.priority;
                         }
                         return left.designation < right.designation;
                     });

    // Every length the scene gives a mark follows the reference length that sets a label `fontPixels` tall on the
    // screen at this scale.
    const double reference = fontPixels / (scale * unitFont);
    const double ratio = reference / merged.referenceLength;
    for (AnnotationMark& mark : merged.marks)
    {
        mark.radius *= ratio;
    }
    merged.referenceLength = reference;

    AnnotationContent content = overlayAnnotationContent(BundledCatalog::DeepSky, AnnotationDetail::Standard);
    content.labelText = labelText;
    // No line of a label is set below the caption's size, the smallest the screen's text is read at: a picture sets the
    // name under a designation a step smaller, too small to read here.
    AnnotationLayout laid = layoutAnnotations(merged, content, style, fontPixels / scale);

    // A star whose label found no room is left out, and one the level marks without a name is drawn alone; every
    // deep-sky object kept keeps its outline.
    auto shown = std::make_shared<AnnotationScene>();
    shown->imageSize = merged.imageSize;
    shown->referenceLength = merged.referenceLength;
    auto layout = std::make_shared<AnnotationLayout>();
    layout->metrics = laid.metrics;
    constexpr std::size_t kLeftOut = std::numeric_limits<std::size_t>::max();
    std::vector<std::size_t> kept(merged.marks.size(), kLeftOut);
    for (std::size_t index = 0u; index < merged.marks.size(); ++index)
    {
        const bool labelled = index < laid.labelled.size() && laid.labelled[index];
        const AnnotationMark& mark = merged.marks[index];
        if (mark.kind == AnnotationMarkKind::Star && !labelled && !mark.markOnly)
        {
            continue;
        }
        kept[index] = shown->marks.size();
        shown->marks.push_back(std::move(merged.marks[index]));
        layout->labelled.push_back(labelled);
    }
    for (AnnotationLabel& label : laid.labels)
    {
        if (label.role == AnnotationLabelRole::Object)
        {
            if (label.source >= kept.size() || kept[label.source] == kLeftOut)
            {
                continue;
            }
            label.source = kept[label.source];
        }
        layout->labels.push_back(std::move(label));
    }
    result.scene = std::move(shown);
    result.layout = std::move(layout);
    return result;
}

OverlayGridLayout layoutOverlayGrid(const an::WcsSolution& solution, const QRectF& part, const double scale,
                                    const double fontPixels, const AnnotationStyle& style)
{
    OverlayGridLayout result;
    if (!part.isValid() || part.isEmpty() || !std::isfinite(scale) || !(scale > 0.0) || !std::isfinite(fontPixels) ||
        !(fontPixels > 0.0))
    {
        return result;
    }
    const double unitFont = annotationMetrics(1.0, style).fontSize;
    if (!(unitFont > 0.0))
    {
        return result;
    }

    // The part as a frame of its own, its origin at the part's corner: the picture's layout sets the grid's labels
    // where the lines meet the frame's edges, the edges of what the view shows.
    std::vector<AnnotationGridLine> lines = buildAnnotationGrid(solution, part, AnnotationGridDensity::Normal);
    const QPointF origin = part.topLeft();
    for (AnnotationGridLine& line : lines)
    {
        for (QPolygonF& segment : line.segments)
        {
            segment.translate(-origin);
        }
        for (AnnotationGridLine::Crossing& crossing : line.crossings)
        {
            crossing.point -= origin;
        }
    }
    auto scene = std::make_shared<AnnotationScene>();
    scene->imageSize = QSize(static_cast<int>(std::ceil(part.width())), static_cast<int>(std::ceil(part.height())));
    scene->referenceLength = fontPixels / (scale * unitFont);
    scene->grid = std::move(lines);

    AnnotationContent content;
    content.layers = AnnotationLayers{
        .grid = true, .deepSky = false, .stars = false, .compass = false, .scaleBar = false, .infoPanel = false};
    content.gridLabels = true;
    auto layout = std::make_shared<AnnotationLayout>(layoutAnnotations(*scene, content, style));
    result.scene = std::move(scene);
    result.layout = std::move(layout);
    result.origin = origin;
    return result;
}

double overlayLabelPixels(const QWidget& widget, const AnnotationViewTextSize size)
{
    const QFont caption = an::ui::tokens().typography.caption;
    const double scale = annotationViewTextScale(size);
    if (caption.pixelSize() > 0)
    {
        return scale * caption.pixelSize();
    }
    const double points = caption.pointSizeF() > 0.0 ? caption.pointSizeF() : 9.0;
    return scale * points * (std::max)(1, widget.logicalDpiY()) / 72.0;
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
    AnnotationContent failedContent;
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

bool FieldCatalogQueries::isAsking() const noexcept
{
    return m_state->asking;
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

/// The catalog's entries inside a solution's field and a margin, for a scene's choices, read and queried on a worker.
[[nodiscard]] FieldQueryOutcome queryField(const an::consumer::EngineSession& session, const BundledCatalog catalog,
                                           const BundledCatalogSource& source, const std::uint64_t generation,
                                           std::shared_ptr<an::SkyCatalog>& loaded, std::uint64_t& loadedGeneration,
                                           const an::WcsSolution& solution, const AnnotationContent& content,
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
    const an::Result<an::CatalogQueryResult> found =
        an::queryCatalogRegion(scope->execution(), *loaded,
                               bundledCatalogRegionQuery(catalog, *center, (std::min)(180.0, *radius + marginDegrees),
                                                         overlayQueryMagnitudeLimit(catalog, content)));
    if (!found.hasValue())
    {
        outcome.failure = QString::fromUtf8(found.status().message());
        return outcome;
    }
    auto entries = std::make_shared<FieldCatalogEntries>(
        FieldCatalogEntries{.solution = solution, .content = content, .entries = {}, .scene = nullptr});
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
        content));
    outcome.entries = std::move(entries);
    return outcome;
}

} // namespace

std::shared_ptr<const FieldCatalogEntries>
FieldCatalogQueries::answer(const an::WcsSolution& solution, const AnnotationContent& content, QWidget* repaint)
{
    State& state = *m_state;
    if (state.last && state.last->solution == solution && state.last->content == content)
    {
        return state.last;
    }
    if (state.asking)
    {
        return nullptr;
    }
    if (state.failedSolution.has_value() && *state.failedSolution == solution && state.failedContent == content)
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
                                          generation = state.sourceGeneration, weak, widget, solution, content]() {
        auto outcome = std::make_shared<FieldQueryOutcome>(queryField(*session, worker->catalog, source, generation,
                                                                      worker->loaded, worker->loadedGeneration,
                                                                      solution, content, worker->marginDegrees));
        QCoreApplication* application = QCoreApplication::instance();
        if (application == nullptr)
        {
            return;
        }
        QMetaObject::invokeMethod(
            application,
            [weak, widget, outcome, generation, solution, content]() {
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
                    state->failedContent = content;
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
