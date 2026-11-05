// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "widgets/HistogramCompanions.h"

#include "controls/IAppContext.h"
#include "session/IEngineSessionProvider.h"
#include "widgets/HistogramComputation.h"
#include "widgets/HistogramWidget.h"
#include "widgets/ViewPresentation.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>

#include <QCoreApplication>
#include <QMetaObject>
#include <QScrollArea>
#include <QThreadPool>

#include <atomic>
#include <numeric>
#include <utility>

namespace an::ui
{
namespace
{

/// The session every Controls operation runs in, or null when the application has none.
[[nodiscard]] const an::consumer::EngineSession* currentSession()
{
    auto* context = an::ui::appContext();
    auto* provider = context != nullptr ? context->getEngineSessionProvider() : nullptr;
    return provider != nullptr && provider->engineSession().valid() ? &provider->engineSession() : nullptr;
}

} // namespace

an::Result<HistogramDisplayData> countHistogram(const an::consumer::EngineSession& session,
                                                const an::consumer::ImageAsset& image,
                                                const HistogramResolution resolution, std::stop_token stop,
                                                const an::consumer::OperationPurpose purpose)
{
    // The lease before the admission: an edit holds the image's writer while it waits to be admitted, so a count
    // admitted first would hold a thread the edit waits for while it waited for the edit.
    an::Result<an::consumer::AssetReadLease> lease = image.read(stop);
    if (!lease.hasValue())
    {
        return lease.status();
    }
    an::Result<an::consumer::OperationScope> scope =
        session.admit(an::consumer::OperationRequest{.purpose = purpose, .stopToken = stop});
    if (!scope.hasValue())
    {
        return scope.status();
    }
    return computeHistogramDisplayData(scope->execution(), *lease, resolution);
}

/// What a scheduler shares with the counts it started: which one is the latest, and what they came to. Touched on the
/// GUI thread only - a worker reads nothing of it and hands its result to the GUI thread first.
struct HistogramCountScheduler::State
{
    std::uint64_t latest = 0u;
    std::uint64_t landed = 0u;
    HistogramCountTelemetry telemetry;
};

HistogramCountScheduler::HistogramCountScheduler(HistogramWidget* widget, const an::consumer::OperationPurpose purpose)
    : m_widget(widget), m_purpose(purpose), m_state(std::make_shared<State>())
{
}

HistogramCountScheduler::~HistogramCountScheduler()
{
    m_stop.request_stop();
}

void HistogramCountScheduler::count(std::shared_ptr<const an::consumer::ImageAsset> image)
{
    m_image = std::move(image);
    recount();
}

void HistogramCountScheduler::recount()
{
    // A count on its way is for an image or a resolution that has moved on.
    m_stop.request_stop();
    m_stop = std::stop_source();
    const std::uint64_t request = ++m_state->latest;

    if (!m_widget)
    {
        return;
    }
    if (!m_image)
    {
        ++m_state->landed;
        m_widget->setHistogram(nullptr);
        return;
    }
    const an::consumer::EngineSession* session = currentSession();
    if (session == nullptr)
    {
        ++m_state->landed;
        ++m_state->telemetry.failed;
        m_widget->setHistogram(nullptr);
        return;
    }

    ++m_state->telemetry.started;
    const HistogramResolution resolution = m_widget->getHistogramResolution();
    QPointer<HistogramWidget> widget = m_widget;
    std::weak_ptr<State> state = m_state;
    const auto land = [widget, state, request](an::Result<HistogramDisplayData>&& counted) {
        const std::shared_ptr<State> held = state.lock();
        if (!held)
        {
            return;
        }
        ++held->landed;
        if (request != held->latest)
        {
            ++held->telemetry.superseded;
            return;
        }
        if (!widget)
        {
            return;
        }
        if (!counted.hasValue())
        {
            ++held->telemetry.failed;
            widget->setHistogram(nullptr);
            return;
        }
        ++held->telemetry.shown;
        widget->setHistogram(std::make_shared<const HistogramDisplayData>(std::move(*counted)));
    };
    QThreadPool::globalInstance()->start([session, image = m_image, resolution, land, stop = m_stop.get_token(),
                                          purpose = m_purpose]() {
        auto counted = std::make_shared<an::Result<HistogramDisplayData>>(
            countHistogram(*session, *image, resolution, stop, purpose));
        QCoreApplication* application = QCoreApplication::instance();
        if (application == nullptr)
        {
            return;
        }
        QMetaObject::invokeMethod(application, [land, counted]() { land(std::move(*counted)); }, Qt::QueuedConnection);
    });
}

void HistogramCountScheduler::show(std::shared_ptr<const HistogramDisplayData> counts)
{
    // A count on its way is of what these replace.
    m_stop.request_stop();
    m_stop = std::stop_source();
    ++m_state->latest;
    ++m_state->landed;
    m_image.reset();
    ++m_state->telemetry.handed;
    if (m_widget)
    {
        m_widget->setHistogram(std::move(counts));
    }
}

const std::shared_ptr<const an::consumer::ImageAsset>& HistogramCountScheduler::image() const noexcept
{
    return m_image;
}

HistogramCountTelemetry HistogramCountScheduler::telemetry() const
{
    return m_state->telemetry;
}

bool HistogramCountScheduler::isIdle() const
{
    return m_state->landed == m_state->latest;
}

HistogramResolution histogramResolutionOf(const PreviewHistogramResolution resolution) noexcept
{
    switch (resolution)
    {
    case PreviewHistogramResolution::Bits6:
        return HistogramResolution::Bits6;
    case PreviewHistogramResolution::Bits7:
        return HistogramResolution::Bits7;
    case PreviewHistogramResolution::Bits8:
        return HistogramResolution::Bits8;
    case PreviewHistogramResolution::Bits9:
        return HistogramResolution::Bits9;
    case PreviewHistogramResolution::Bits10:
        return HistogramResolution::Bits10;
    case PreviewHistogramResolution::Bits12:
        return HistogramResolution::Bits12;
    case PreviewHistogramResolution::Bits14:
        return HistogramResolution::Bits14;
    case PreviewHistogramResolution::Bits16:
        return HistogramResolution::Bits16;
    case PreviewHistogramResolution::Levels100:
        return HistogramResolution::Levels100;
    case PreviewHistogramResolution::Levels1000:
        return HistogramResolution::Levels1000;
    case PreviewHistogramResolution::Levels10000:
        return HistogramResolution::Levels10000;
    }
    return HistogramResolution::Bits8;
}

LensHistogramView::LensHistogramView(QWidget* parent) : m_widget(new HistogramWidget(parent))
{
    m_widget->setGridEnabled(true);
    m_widget->setGridSize(4, 4);
    m_widget->setGridWidth(0.2f);
    m_widget->setGradientAreaEnabled(false);
    m_widget->setClippingInfoEnabled(false);
    m_widget->setHistogramResolution(HistogramResolution::Bits6);
}

LensHistogramView::~LensHistogramView() = default;

QWidget* LensHistogramView::widget()
{
    return m_widget;
}

void LensHistogramView::setCounts(const LensHistogram& counts)
{
    if (!m_widget)
    {
        return;
    }
    auto data = std::make_shared<HistogramDisplayData>();
    const std::uint32_t channels = (std::min)(counts.channels, static_cast<std::uint32_t>(counts.counts.size()));
    data->channels.reserve(channels);
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        HistogramDisplayChannel shown;
        shown.bins.assign(counts.counts[channel].begin(), counts.counts[channel].end());
        shown.samples = std::accumulate(shown.bins.begin(), shown.bins.end(), std::uint64_t{0});
        data->channels.push_back(std::move(shown));
    }
    m_widget->setHistogram(std::move(data));
}

PreviewHistogramView::PreviewHistogramView(QScrollArea* scrollArea, const an::consumer::OperationPurpose purpose)
    : m_widget(new HistogramWidget(scrollArea))
{
    m_widget->setClippingInfoEnabled(true);
    if (scrollArea != nullptr)
    {
        scrollArea->setWidget(m_widget);
    }
    m_widget->setMouseTracking(true);
    m_widget->setParentScrollArea(scrollArea);
    m_widget->setInteractiveZoomEnabled(true);
    m_scheduler = std::make_unique<HistogramCountScheduler>(m_widget, purpose);
}

PreviewHistogramView::~PreviewHistogramView() = default;

QWidget* PreviewHistogramView::widget()
{
    return m_widget;
}

void PreviewHistogramView::setImage(std::shared_ptr<const an::consumer::ImageAsset> image)
{
    m_scheduler->count(std::move(image));
}

void PreviewHistogramView::showCounts(std::shared_ptr<const HistogramDisplayData> counts)
{
    m_scheduler->show(std::move(counts));
}

PreviewHistogramResolution PreviewHistogramView::resolution() const
{
    return m_resolution;
}

void PreviewHistogramView::setDisplayedChannels(const bool red, const bool green, const bool blue)
{
    if (m_widget)
    {
        m_widget->setDisplayChannels(red, green, blue);
    }
}

void PreviewHistogramView::setResolution(const PreviewHistogramResolution resolution)
{
    m_resolution = resolution;
    if (!m_widget)
    {
        return;
    }
    const HistogramResolution counted = histogramResolutionOf(resolution);
    if (m_widget->getHistogramResolution() == counted)
    {
        return;
    }
    m_widget->setHistogramResolution(counted);
    // Counts handed in are made again by whoever made them; an image given is counted again here.
    if (m_scheduler->image())
    {
        m_scheduler->recount();
    }
}

void PreviewHistogramView::setGridEnabled(const bool enabled)
{
    if (m_widget)
    {
        m_widget->setGridEnabled(enabled);
    }
}

void PreviewHistogramView::zoomIn()
{
    if (m_widget)
    {
        m_widget->zoomIn();
    }
}

void PreviewHistogramView::zoomOut()
{
    if (m_widget)
    {
        m_widget->zoomOut();
    }
}

void PreviewHistogramView::resetZoom()
{
    if (m_widget)
    {
        m_widget->resetZoom();
    }
}

void PreviewHistogramView::setLocalChannelNormalization(const bool enabled)
{
    if (m_widget)
    {
        m_widget->setLocalChannelNormalization(enabled);
    }
}

void PreviewHistogramView::setClippingInfoEnabled(const bool enabled)
{
    if (m_widget)
    {
        m_widget->setClippingInfoEnabled(enabled);
    }
}

void PreviewHistogramView::setAreaModeEnabled(const bool enabled)
{
    if (m_widget)
    {
        m_widget->setAreaModeEnabled(enabled);
    }
}

const HistogramCountScheduler& PreviewHistogramView::scheduler() const noexcept
{
    return *m_scheduler;
}

std::unique_ptr<ILensHistogram> createLensHistogram(QWidget* parent)
{
    return std::make_unique<LensHistogramView>(parent);
}

std::unique_ptr<IPreviewHistogram> createPreviewHistogram(QScrollArea* scrollArea,
                                                          const an::consumer::OperationPurpose purpose)
{
    return std::make_unique<PreviewHistogramView>(scrollArea, purpose);
}

} // namespace an::ui
