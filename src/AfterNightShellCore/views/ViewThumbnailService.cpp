// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "views/ViewThumbnailService.h"

#include "core/ApplicationCore.h"

#include "controls/AutostretchApply.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewControlListener.h"
#include "session/CommitTicket.h"
#include "session/DisplayProxy.h"
#include "session/IEngineSessionProvider.h"
#include "session/ImageDocument.h"
#include "widgets/ImageViewWidget.h"
#include "widgets/PresentationBridge.h"
#include "widgets/ViewPresentation.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Core/ErrorCode.h>

#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>
#include <QSize>
#include <QThreadPool>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <map>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace an::ui::shell
{

namespace
{

/// Counts the calls renderings make of the engine and the session, and those made on the GUI thread.
struct EngineCalls
{
    std::thread::id gui;
    std::atomic<std::uint64_t> all{0u};
    std::atomic<std::uint64_t> onGui{0u};

    void count()
    {
        ++all;
        if (std::this_thread::get_id() == gui)
        {
            ++onGui;
        }
    }
};

/// What one rendering came to.
struct Rendered
{
    bool refused = false;
    bool failed = false;
    QImage thumbnail;
};

/// The size of the image under a lease; empty when it states none.
[[nodiscard]] QSize leasedSize(const an::consumer::AssetReadLease& image)
{
    QSize size;
    image.visit([&size](const auto& typed) {
        if (const std::optional<an::ImageDescriptor> described = typed.descriptor(); described.has_value())
        {
            const an::ImageExtent& extent = described->extent();
            size = QSize(static_cast<int>(extent.width()), static_cast<int>(extent.height()));
        }
    });
    return size;
}

/// The reduction a thumbnail is presented from: the largest that leaves the image's shorter side at least a
/// thumbnail's side, one when the image is no larger than that.
[[nodiscard]] std::uint32_t thumbnailFactor(const QSize& image)
{
    const int shorter = (std::min)(image.width(), image.height());
    if (shorter <= ViewThumbnailService::kSide)
    {
        return 1u;
    }
    const auto wanted = static_cast<std::uint32_t>(shorter / ViewThumbnailService::kSide);
    return (std::max)(1u, (std::min)(wanted,
                                     an::ui::largestDisplayProxyFactor(static_cast<std::uint32_t>(image.width()),
                                                                       static_cast<std::uint32_t>(image.height()))));
}

/// A picture scaled to cover the thumbnail's square, centred, and cut to it.
[[nodiscard]] QImage coveringSquare(const QImage& picture)
{
    if (picture.isNull())
    {
        return {};
    }
    const QSize square(ViewThumbnailService::kSide, ViewThumbnailService::kSide);
    const QImage scaled = picture.convertToFormat(QImage::Format_ARGB32_Premultiplied)
                              .scaled(square, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    const int left = (scaled.width() - square.width()) / 2;
    const int top = (scaled.height() - square.height()) / 2;
    return scaled.copy(left, top, square.width(), square.height());
}

/// Renders one image as a view presents it, in one operation: the lease first, as an edit holds the image's writer
/// while it waits to be admitted, then an admission as a preview that never waits, then the reduction and the
/// presentation. Runs on a worker.
[[nodiscard]] Rendered render(const an::consumer::EngineSession& session,
                              const std::shared_ptr<const an::consumer::ImageAsset>& image,
                              const an::ui::ViewPresentationSettings& settings,
                              const std::optional<an::DisplayPresentationAnalysis>& analysis,
                              const std::stop_token& stop, EngineCalls& calls)
{
    Rendered rendered;
    calls.count();
    an::Result<an::consumer::AssetReadLease> lease = image->read(stop);
    if (!lease.hasValue())
    {
        rendered.failed = true;
        return rendered;
    }
    calls.count();
    an::Result<an::consumer::OperationScope> scope = session.tryAdmit(
        an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Preview, .stopToken = stop});
    if (!scope.hasValue())
    {
        rendered.refused = scope.status().code() == an::ErrorCode::Busy;
        rendered.failed = !rendered.refused;
        return rendered;
    }
    const QSize size = leasedSize(*lease);
    if (size.isEmpty())
    {
        rendered.failed = true;
        return rendered;
    }
    an::ui::ViewPresentationSettings presentation = settings;
    presentation.proxyFactor = thumbnailFactor(size);
    std::shared_ptr<const an::ui::DisplayProxy> proxy;
    if (presentation.proxyFactor >= 2u)
    {
        calls.count();
        an::Result<std::shared_ptr<const an::ui::DisplayProxy>> built =
            an::ui::DisplayProxy::build(scope->engine(), scope->execution(), *lease, presentation.proxyFactor);
        if (!built.hasValue())
        {
            rendered.failed = true;
            return rendered;
        }
        proxy = std::move(*built);
    }
    calls.count();
    an::Result<an::ui::ViewPresentationOutcome> presented = an::ui::presentForView(
        scope->engine(), scope->execution(), *lease, presentation, analysis.has_value() ? &*analysis : nullptr,
        &session.memory(), proxy.get(), an::ui::WholeMeasurement::Measured);
    if (!presented.hasValue())
    {
        rendered.failed = true;
        return rendered;
    }
    rendered.thumbnail = coveringSquare(an::ui::presentedQImage(std::move(presented->image)));
    scope->end();
    return rendered;
}

} // namespace

struct ViewThumbnailService::Impl : std::enable_shared_from_this<Impl>
{
    /// Hears one followed view.
    class Follower final : public ImageViewControlListener
    {
    public:
        Follower(Impl& impl, ImageViewControl* view) : ImageViewControlListener(view), m_impl(impl) {}

        void onImageLoaded(ImageViewControl* view) override
        {
            m_impl.request(view);
        }

        void onImageUpdated(ImageViewControl* view) override
        {
            m_impl.request(view);
        }

        void onImageDisplayChannelChanged(ImageViewControl* view, int /*mode*/) override
        {
            m_impl.request(view);
        }

        void onImageViewControlDestroyed(ImageViewControl* view) override
        {
            ImageViewControlListener::onImageViewControlDestroyed(view);
            // The view is let go at once, so nothing asks it for anything again; this follower is retired, not
            // destroyed, while the view still tells its listeners.
            m_impl.drop(view);
        }

    private:
        Impl& m_impl;
    };

    /// What the service keeps of one followed view.
    struct Followed
    {
        std::unique_ptr<Follower> follower;
        std::unique_ptr<an::ui::RequestTracker> tracker;
        QImage thumbnail;
        /// The settings the view last stated, kept for the renderings made while it presents nothing of its own.
        std::optional<an::ui::ViewPresentationSettings> shown;
        int inFlight = 0;
        bool retryAsked = false;
    };

    explicit Impl(ApplicationCore& application) : core(application)
    {
        calls->gui = std::this_thread::get_id();
        gate = new QObject();
    }

    ~Impl()
    {
        for (auto& [view, followed] : views)
        {
            followed.tracker.reset();
        }
        delete gate;
    }

    ApplicationCore& core;
    std::map<ImageViewControl*, Followed> views;
    /// The followers of views let go, destroyed once the event loop turns: a view tells its listeners it goes while
    /// one of them is still being told.
    std::vector<std::unique_ptr<Follower>> retired;
    std::map<std::size_t, Listener> listeners;
    std::size_t nextToken = 1;
    ViewThumbnailTelemetry counted;
    std::shared_ptr<EngineCalls> calls = std::make_shared<EngineCalls>();
    /// What queued work lands on: it goes with the service, and what was queued on it is dropped.
    QObject* gate = nullptr;

    [[nodiscard]] const an::consumer::EngineSession* session() const
    {
        an::ui::IEngineSessionProvider* const provider = core.engineSessionProvider();
        return provider != nullptr && provider->engineSession().valid() ? &provider->engineSession() : nullptr;
    }

    /// What the view presents its image through, whole: what it states now, else what it stated last, else its display
    /// mode's stretch over the whole image. Never in Night's form, never a part or a reduction of its own.
    [[nodiscard]] an::ui::ViewPresentationSettings settingsOf(ImageViewControl& view, Followed& followed) const
    {
        const ImageViewWidget* widget = view.getViewWidget();
        if (widget != nullptr)
        {
            if (std::optional<an::ui::ViewPresentationSettings> stated = widget->framePresentationSettings();
                stated.has_value())
            {
                followed.shown = std::move(stated);
            }
        }
        an::ui::ViewPresentationSettings settings;
        if (followed.shown.has_value())
        {
            settings = *followed.shown;
        }
        else if (widget != nullptr && widget->getViewMode() == ImageViewMode::AUTOSTRETCH_MODE)
        {
            // The view's automatic stretch, each colour channel on its own unless it is asked to link them.
            settings.request = an::ui::autostretchApplyRequest(an::ui::AutostretchApplySettings{});
        }
        else
        {
            settings.request.stretch = an::PresentationStretch::Linear;
        }
        settings.subsampleFactor = 1u;
        settings.proxyFactor = 1u;
        settings.region.reset();
        settings.night = false;
        settings.request.output = an::PresentationOutput::Bgra8;
        return settings;
    }

    void request(ImageViewControl* view)
    {
        const auto found = views.find(view);
        if (found == views.end() || view == nullptr || !view->hasImage())
        {
            return;
        }
        Followed& followed = found->second;
        const an::consumer::EngineSession* shared = session();
        const an::ui::ImageDocument& document = view->document();
        std::shared_ptr<const an::consumer::ImageAsset> image = document.asset();
        if (shared == nullptr || !image)
        {
            ++counted.failed;
            return;
        }
        an::Result<an::ui::CommitTicket> ticket = followed.tracker->issue(document);
        if (!ticket.hasValue())
        {
            ++counted.failed;
            return;
        }
        const an::ui::ViewPresentationSettings settings = settingsOf(*view, followed);
        std::optional<an::DisplayPresentationAnalysis> analysis;
        if (const ImageViewWidget* widget = view->getViewWidget())
        {
            analysis = widget->keptAnalysisOf(image->revision(), settings);
        }
        ++counted.started;
        ++followed.inFlight;
        QThreadPool::globalInstance()->start([shared, ticket = std::move(*ticket), image = std::move(image), settings,
                                              analysis, calls = calls, weak = weak_from_this(), view,
                                              gate = QPointer<QObject>(gate)]() {
            auto rendered =
                std::make_shared<Rendered>(render(*shared, image, settings, analysis, ticket.stopToken(), *calls));
            if (gate.isNull())
            {
                return;
            }
            QMetaObject::invokeMethod(
                gate.data(),
                [weak, ticket, rendered, view]() {
                    if (const std::shared_ptr<Impl> impl = weak.lock())
                    {
                        impl->land(view, ticket, *rendered);
                    }
                },
                Qt::QueuedConnection);
        });
    }

    void land(ImageViewControl* view, const an::ui::CommitTicket& ticket, Rendered& rendered)
    {
        const auto found = views.find(view);
        if (found == views.end())
        {
            ++counted.discarded;
            return;
        }
        Followed& followed = found->second;
        --followed.inFlight;
        if (ticket.verdict() != an::ui::CommitVerdict::Commit)
        {
            ++counted.discarded;
            return;
        }
        if (rendered.refused)
        {
            ++counted.refused;
            retry(view, followed);
            return;
        }
        if (rendered.failed || rendered.thumbnail.isNull())
        {
            ++counted.failed;
            return;
        }
        ++counted.published;
        followed.thumbnail = std::move(rendered.thumbnail);
        tell(view, followed.thumbnail);
    }

    void retry(ImageViewControl* view, Followed& followed)
    {
        if (followed.retryAsked)
        {
            return;
        }
        followed.retryAsked = true;
        QTimer::singleShot(kRetry, gate, [weak = weak_from_this(), view]() {
            if (const std::shared_ptr<Impl> impl = weak.lock())
            {
                if (const auto found = impl->views.find(view); found != impl->views.end())
                {
                    found->second.retryAsked = false;
                    impl->request(view);
                }
            }
        });
    }

    void drop(ImageViewControl* view)
    {
        const auto found = views.find(view);
        if (found == views.end())
        {
            return;
        }
        const bool hadThumbnail = !found->second.thumbnail.isNull();
        if (found->second.follower)
        {
            found->second.follower->setViewControl(nullptr);
            retired.push_back(std::move(found->second.follower));
            QMetaObject::invokeMethod(
                gate,
                [weak = weak_from_this()]() {
                    if (const std::shared_ptr<Impl> impl = weak.lock())
                    {
                        impl->retired.clear();
                    }
                },
                Qt::QueuedConnection);
        }
        views.erase(found);
        if (hadThumbnail)
        {
            tell(view, QImage());
        }
    }

    void tell(ImageViewControl* view, const QImage& thumbnail)
    {
        const std::map<std::size_t, Listener> told = listeners;
        for (const auto& [token, listener] : told)
        {
            if (listener && listeners.contains(token))
            {
                listener(view, thumbnail);
            }
        }
    }
};

ViewThumbnailService::ViewThumbnailService(ApplicationCore& core) : m_impl(std::make_shared<Impl>(core)) {}

ViewThumbnailService::~ViewThumbnailService() = default;

void ViewThumbnailService::follow(ImageViewControl* view)
{
    if (view == nullptr || m_impl->views.contains(view))
    {
        return;
    }
    Impl::Followed followed;
    followed.follower = std::make_unique<Impl::Follower>(*m_impl, view);
    followed.tracker = std::make_unique<an::ui::RequestTracker>();
    m_impl->views.emplace(view, std::move(followed));
    m_impl->request(view);
}

void ViewThumbnailService::forget(ImageViewControl* view)
{
    m_impl->drop(view);
}

void ViewThumbnailService::refresh(ImageViewControl* view)
{
    m_impl->request(view);
}

bool ViewThumbnailService::follows(const ImageViewControl* view) const
{
    return m_impl->views.contains(const_cast<ImageViewControl*>(view));
}

QImage ViewThumbnailService::thumbnail(const ImageViewControl* view) const
{
    const auto found = m_impl->views.find(const_cast<ImageViewControl*>(view));
    return found != m_impl->views.end() ? found->second.thumbnail : QImage();
}

std::size_t ViewThumbnailService::subscribe(Listener listener)
{
    const std::size_t token = m_impl->nextToken++;
    m_impl->listeners.emplace(token, std::move(listener));
    return token;
}

void ViewThumbnailService::unsubscribe(const std::size_t token)
{
    m_impl->listeners.erase(token);
}

bool ViewThumbnailService::isIdle() const
{
    return std::all_of(m_impl->views.begin(), m_impl->views.end(),
                       [](const auto& entry) { return entry.second.inFlight == 0 && !entry.second.retryAsked; });
}

ViewThumbnailTelemetry ViewThumbnailService::telemetry() const
{
    ViewThumbnailTelemetry telemetry = m_impl->counted;
    telemetry.engineCalls = m_impl->calls->all.load();
    telemetry.engineCallsOnGuiThread = m_impl->calls->onGui.load();
    return telemetry;
}

} // namespace an::ui::shell
