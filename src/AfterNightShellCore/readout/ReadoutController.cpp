// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "readout/ReadoutController.h"

#include "core/ApplicationCore.h"
#include "readout/ReadoutSummaries.h"
#include "services/AppEventBus.h"
#include "views/ViewService.h"

#include "app/AfterNightAppListener.h"
#include "components/AnAstrometryCard.h"
#include "components/AnCompactReadout.h"
#include "components/AnHistogramCard.h"
#include "components/AnImageFactsCard.h"
#include "components/AnReadoutColumn.h"
#include "components/AnStatisticsCard.h"
#include "controls/IAppContext.h"
#include "controls/ImageStatisticsReport.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewControlListener.h"
#include "controls/settings/SettingsKeys.h"
#include "session/CommitTicket.h"
#include "session/IEngineSessionProvider.h"
#include "session/ImageDocument.h"
#include "widgets/ChannelDisplayStrategy.h"
#include "widgets/HistogramComputation.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Core/ErrorCode.h>

#include <QAction>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QMenu>
#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>
#include <QTimer>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <span>
#include <thread>
#include <utility>
#include <vector>

namespace an::ui::shell
{
namespace
{

/// The calls a measurement makes of the engine and the session, and how many of them on the GUI thread.
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

/// What one measurement came to.
struct Measured
{
    bool refused = false;
    bool failed = false;
    std::optional<an::ui::HistogramDisplayData> counts;
    std::optional<an::ui::ImageStatisticsReport> figures;
    std::optional<an::ui::ImageStatisticsReport> originalFigures;
    QString solver;
};

/// Measures an image - and the original it is a preview of, when one is given - in one operation: the leases first,
/// as an edit holds the image's writer while it waits to be admitted, then an admission as a preview that never waits,
/// then the counts and the figures. Runs on a worker.
[[nodiscard]] Measured measure(const an::consumer::EngineSession& session,
                               const std::shared_ptr<const an::consumer::ImageAsset>& image,
                               const std::shared_ptr<const an::consumer::ImageAsset>& original,
                               const an::ui::HistogramResolution resolution, const std::stop_token& stop,
                               EngineCalls& calls)
{
    Measured measured;
    calls.count();
    an::Result<an::consumer::AssetReadLease> lease = image->read(stop);
    if (!lease.hasValue())
    {
        measured.failed = true;
        return measured;
    }
    std::optional<an::consumer::AssetReadLease> originalLease;
    if (original)
    {
        calls.count();
        an::Result<an::consumer::AssetReadLease> read = original->read(stop);
        if (!read.hasValue())
        {
            measured.failed = true;
            return measured;
        }
        originalLease.emplace(std::move(*read));
    }

    calls.count();
    an::Result<an::consumer::OperationScope> scope = session.tryAdmit(
        an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Preview, .stopToken = stop});
    if (!scope.hasValue())
    {
        measured.refused = scope.status().code() == an::ErrorCode::Busy;
        measured.failed = !measured.refused;
        return measured;
    }

    calls.count();
    if (an::Result<an::ui::HistogramDisplayData> counts =
            an::ui::computeHistogramDisplayData(scope->execution(), *lease, resolution);
        counts.hasValue())
    {
        measured.counts = std::move(*counts);
    }
    // The figures are the statistics window's as it opens: the clipped samples left out.
    calls.count();
    if (an::Result<an::ui::ImageStatisticsReport> figures = an::ui::computeImageStatisticsReport(*scope, *lease, true);
        figures.hasValue())
    {
        measured.figures = std::move(*figures);
    }
    if (originalLease.has_value())
    {
        calls.count();
        if (an::Result<an::ui::ImageStatisticsReport> figures =
                an::ui::computeImageStatisticsReport(*scope, *originalLease, true);
            figures.hasValue())
        {
            measured.originalFigures = std::move(*figures);
        }
    }
    lease->visit([&measured](const auto& typed) {
        const auto stated = typed.metadata().codec().cards();
        const std::vector<an::CodecCard> cards(stated.begin(), stated.end());
        measured.solver = solverNamed(cards);
    });
    measured.failed = !measured.figures.has_value();
    return measured;
}

/// The view's actions each overlay chip is bound to, by their object names.
constexpr std::array<const char*, an::ui::kAstrometryOverlays> kOverlayActions{
    "astrometryGridAction", "astrometryAnnotationsAction", "astrometryStarLabelsAction",
    "astrometryDetectedStarsAction", "astrometrySipMapAction"};

} // namespace

struct ReadoutController::Impl
{
    /// What a result that lands reaches the controller through: cleared when the controller goes, on the GUI thread
    /// where every result lands, so a worker never touches the controller.
    struct Gate
    {
        Impl* impl = nullptr;
    };

    /// Hears what the followed view tells its listeners.
    class ViewFollower final : public ImageViewControlListener
    {
    public:
        explicit ViewFollower(Impl& impl) : ImageViewControlListener(nullptr), m_impl(impl) {}

        void onImageLoaded(ImageViewControl* /*view*/) override
        {
            m_impl.schedule(true);
        }

        void onImageUpdated(ImageViewControl* /*view*/) override
        {
            m_impl.schedule(true);
        }

        void onImageRenamed(ImageViewControl* /*view*/) override
        {
            m_impl.present();
        }

        void onImageDisplayChannelChanged(ImageViewControl* /*view*/, int /*mode*/) override
        {
            m_impl.schedule(false);
        }

        void onImageViewControlDestroyed(ImageViewControl* view) override
        {
            ImageViewControlListener::onImageViewControlDestroyed(view);
            m_impl.viewGone(view);
        }

    private:
        Impl& m_impl;
    };

    /// Hears which view is active, and which go.
    class BusFollower final : public AfterNightAppListener
    {
    public:
        explicit BusFollower(Impl& impl) : AfterNightAppListener(nullptr), m_impl(impl) {}

        void onActiveViewControlChanged(ImageViewControl* view) override
        {
            if (m_impl.task == nullptr)
            {
                m_impl.follow(view);
            }
        }

        void onViewControlDestroyed(const ImageViewControl* view) override
        {
            m_impl.viewGone(view);
        }

    private:
        Impl& m_impl;
    };

    explicit Impl(ApplicationCore& owner) : core(owner), follower(*this), bus(*this)
    {
        gate = std::make_shared<Gate>();
        gate->impl = this;
        calls = std::make_shared<EngineCalls>();
        calls->gui = std::this_thread::get_id();
        debounce.setSingleShot(true);
        debounce.setInterval(kDebounce);
        QObject::connect(&debounce, &QTimer::timeout, &debounce, [this]() { update(); });
        retry.setSingleShot(true);
        QObject::connect(&retry, &QTimer::timeout, &retry, [this]() { update(); });
        core.eventBus().subscribe(&bus);
    }

    ~Impl()
    {
        gate->impl = nullptr;
        core.eventBus().unsubscribe(&bus);
        disconnectColumn();
        if (task != nullptr)
        {
            task->setFrameListener({});
        }
        tracker.cancel();
        follower.setViewControl(nullptr);
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    // ---- Following

    void follow(ImageViewControl* next)
    {
        if (next == view.data() && (next != nullptr || !figures.has_value()))
        {
            return;
        }
        view = next;
        follower.setViewControl(next);
        tracker.cancel();
        stopRetrying();
        measuredRevision.reset();
        measuredFrame.reset();
        solver.clear();
        bindView();
        if (next == nullptr && task == nullptr)
        {
            // Nothing to read out: the figures go, as the view did.
            debounce.stop();
            figures.reset();
            counts.reset();
            setStale(false);
            present();
            publish();
            return;
        }
        // The figures of the view before stay, dimmed, until the new ones land.
        setStale(figures.has_value());
        present();
        schedule(true);
    }

    void viewGone(const ImageViewControl* gone)
    {
        if (gone != nullptr && gone == view.data())
        {
            follow(nullptr);
        }
    }

    // ---- Scheduling

    void schedule(const bool measureAgain)
    {
        measureWanted = measureWanted || measureAgain;
        // One update a burst: a change while one is pending joins it.
        if (!debounce.isActive())
        {
            debounce.start();
        }
    }

    void update()
    {
        QElapsedTimer timer;
        timer.start();
        ++telemetry.updates;
        // An update measures what is wanted, so a try pending is made by it.
        retry.stop();
        if (task != nullptr)
        {
            updateTask();
        }
        else
        {
            updateView();
        }
        telemetry.lastUpdateNanoseconds = timer.nsecsElapsed();
    }

    [[nodiscard]] an::ui::HistogramResolution resolution() const
    {
        return column ? column->histogramCard()->resolution() : an::ui::HistogramResolution::Bits8;
    }

    void updateView()
    {
        present();
        if (!view || !view->hasImage())
        {
            measureWanted = false;
            return;
        }
        const an::ui::ImageDocument& document = view->document();
        const an::ui::DocumentRevision revision = document.revision();
        const an::ui::HistogramResolution counted = resolution();
        if (!measureWanted && measuredRevision == revision && measuredResolution == counted)
        {
            ++telemetry.presented;
            return;
        }
        measureWanted = false;
        an::Result<an::ui::CommitTicket> ticket = tracker.issue(document);
        if (!ticket.hasValue())
        {
            ++telemetry.failed;
            return;
        }
        start(std::move(*ticket), document.asset(), nullptr, counted, revision, std::nullopt, 0u);
    }

    void updateTask()
    {
        present();
        const std::optional<IReadoutFrameSource::Frame> frame = task->latestFrame();
        if (!frame.has_value() || !frame->image)
        {
            measureWanted = false;
            return;
        }
        if (frame->interactive)
        {
            // Its pixels are approximated: the figures of the last settled frame stay, dimmed.
            setStale(true);
            ++telemetry.presented;
            return;
        }
        const an::ui::HistogramResolution counted = resolution();
        if (!measureWanted && measuredFrame == frame->number && measuredResolution == counted)
        {
            ++telemetry.presented;
            return;
        }
        measureWanted = false;
        std::shared_ptr<const an::consumer::ImageAsset> before = task->original();
        const std::optional<an::consumer::AssetRevision> beforeRevision =
            before ? std::optional<an::consumer::AssetRevision>(before->revision()) : std::nullopt;
        if (beforeRevision.has_value() && measuredOriginal == beforeRevision && original.has_value())
        {
            before.reset();
        }
        start(tracker.issueUnbound(), frame->image, std::move(before), counted, std::nullopt, beforeRevision,
              frame->number);
    }

    [[nodiscard]] const an::consumer::EngineSession* session() const
    {
        an::ui::IEngineSessionProvider* const provider = core.engineSessionProvider();
        return provider != nullptr && provider->engineSession().valid() ? &provider->engineSession() : nullptr;
    }

    void start(an::ui::CommitTicket ticket, std::shared_ptr<const an::consumer::ImageAsset> image,
               std::shared_ptr<const an::consumer::ImageAsset> before, const an::ui::HistogramResolution counted,
               const std::optional<an::ui::DocumentRevision> revision,
               const std::optional<an::consumer::AssetRevision> beforeRevision, const std::uint64_t frameNumber)
    {
        const an::consumer::EngineSession* const shared = session();
        if (shared == nullptr || !image)
        {
            ++telemetry.failed;
            return;
        }
        setStale(true);
        ++telemetry.started;
        ++inFlight;
        QThreadPool::globalInstance()->start([shared, ticket = std::move(ticket), image = std::move(image),
                                              before = std::move(before), counted, revision, beforeRevision,
                                              frameNumber, calls = calls, landing = std::weak_ptr<Gate>(gate)]() {
            auto measured =
                std::make_shared<Measured>(measure(*shared, image, before, counted, ticket.stopToken(), *calls));
            QCoreApplication* const application = QCoreApplication::instance();
            if (application == nullptr)
            {
                return;
            }
            QMetaObject::invokeMethod(
                application,
                [landing, ticket, measured, counted, revision, beforeRevision, frameNumber]() {
                    const std::shared_ptr<Gate> open = landing.lock();
                    if (open && open->impl != nullptr)
                    {
                        open->impl->land(ticket, *measured, counted, revision, beforeRevision, frameNumber);
                    }
                },
                Qt::QueuedConnection);
        });
    }

    void land(const an::ui::CommitTicket& ticket, Measured& measured, const an::ui::HistogramResolution counted,
              const std::optional<an::ui::DocumentRevision> revision,
              const std::optional<an::consumer::AssetRevision> beforeRevision, const std::uint64_t frameNumber)
    {
        --inFlight;
        if (ticket.verdict() != an::ui::CommitVerdict::Commit)
        {
            ++telemetry.discarded;
            return;
        }
        if (measured.refused)
        {
            // The figures stay, dimmed, and the measurement is tried again by itself: after an image opens, the view's
            // own work keeps the session busy and nothing changes once it is done, so waiting for a change would leave
            // the readout empty.
            ++telemetry.refused;
            measureWanted = true;
            retryLater();
            return;
        }
        stopRetrying();
        if (measured.failed)
        {
            ++telemetry.failed;
            setStale(false);
            return;
        }
        figures = std::move(measured.figures);
        counts = measured.counts.has_value()
                     ? std::make_shared<const an::ui::HistogramDisplayData>(std::move(*measured.counts))
                     : nullptr;
        if (task == nullptr)
        {
            solver = measured.solver;
        }
        measuredRevision = revision;
        measuredResolution = counted;
        if (task != nullptr)
        {
            measuredFrame = frameNumber;
            if (measured.originalFigures.has_value())
            {
                original = std::move(measured.originalFigures);
                measuredOriginal = beforeRevision;
            }
        }
        setStale(false);
        present();
        publish();
        ++telemetry.published;
    }

    /// Tries a refused measurement again a while later, unless an update or another measurement is on its way: first
    /// after `kDebounce`, then twice as long each time up to `kRetryLongest`, so a long run is asked little.
    void retryLater()
    {
        if (debounce.isActive() || inFlight > 0u)
        {
            return;
        }
        retry.start(retryWait);
        retryWait = (std::min)(retryWait * 2, kRetryLongest);
    }

    void stopRetrying()
    {
        retry.stop();
        retryWait = kDebounce;
    }

    // ---- Showing

    void setStale(const bool now)
    {
        stale = now;
        if (column)
        {
            column->histogramCard()->setStale(now);
            column->statisticsCard()->setStale(now);
        }
        if (compact && task != nullptr)
        {
            compact->setStale(now);
        }
    }

    /// Shows what is read from the view without measuring: its name, its facts, its solution, the channels it shows.
    void present()
    {
        ImageViewControl* const shown = view.data();
        const std::optional<an::ImageDescriptor> descriptor =
            shown != nullptr ? shown->document().descriptor() : std::nullopt;
        const std::size_t channels = descriptor.has_value() ? descriptor->extent().channelCount() : 0u;
        const an::ui::AstrometryReadout astrometry =
            shown != nullptr ? astrometryReadout(shown->imageSolution(), solver) : an::ui::AstrometryReadout{};
        if (column)
        {
            column->setViewName(shown != nullptr ? QString::fromStdString(shown->getUniqueName()) : QString());
            an::ui::AnHistogramCard* const histogram = column->histogramCard();
            histogram->setChannelCount(channels);
            const ImageCompositionMode composition =
                shown != nullptr ? shown->displayedComposition() : ImageCompositionMode::rgb_mode;
            histogram->setShownChannels(
                composition != ImageCompositionMode::g_mode && composition != ImageCompositionMode::b_mode,
                composition != ImageCompositionMode::r_mode && composition != ImageCompositionMode::b_mode,
                composition != ImageCompositionMode::r_mode && composition != ImageCompositionMode::g_mode);
            column->imageCard()->setFacts(descriptor.has_value() ? imageFactsReadout(*descriptor)
                                                                 : an::ui::ImageFactsReadout{});
            column->astrometryCard()->setReadout(astrometry);
        }
        if (compact && task != nullptr)
        {
            compact->setAstrometry(compactAstrometryLine(astrometry));
        }
    }

    /// Shows the figures and the counts measured.
    void publish()
    {
        QElapsedTimer timer;
        timer.start();
        if (column)
        {
            column->histogramCard()->setCounts(counts);
            column->statisticsCard()->setReport(figures);
        }
        if (compact && task != nullptr)
        {
            const std::size_t channels = figures.has_value() ? figures->channels.size() : 0u;
            compact->setCounts(counts);
            compact->setMode(channels, measuredResolution);
            compact->setFigures(figures, original);
        }
        telemetry.lastPublishNanoseconds = timer.nsecsElapsed();
    }

    /// Binds the astrometry card to the followed view's overlays and settings.
    void bindView()
    {
        if (!column)
        {
            return;
        }
        an::ui::AnAstrometryCard* const card = column->astrometryCard();
        ImageViewControl* const shown = view.data();
        for (std::size_t index = 0u; index < an::ui::kAstrometryOverlays; ++index)
        {
            card->bindOverlay(static_cast<an::ui::AstrometryOverlay>(index),
                              shown != nullptr ? shown->findChild<QAction*>(QString::fromLatin1(kOverlayActions[index]))
                                               : nullptr);
        }
        card->setSettingsMenu(shown != nullptr ? shown->findChild<QMenu*>(QStringLiteral("astrometrySettingsMenu"))
                                               : nullptr);
    }

    // ---- The column's wiring

    template <typename SenderT, typename SignalT, typename SlotT> void wire(SenderT* sender, SignalT signal, SlotT slot)
    {
        connections.push_back(QObject::connect(sender, signal, sender, std::move(slot)));
    }

    /// Runs one of the followed view's windows.
    template <typename MemberT> auto onView(MemberT member)
    {
        return [this, member]() {
            if (ImageViewControl* const shown = view.data())
            {
                (shown->*member)();
            }
        };
    }

    void connectColumn()
    {
        an::ui::IGlobalSettingsProvider* const store = core.globalSettings();
        const QString cardsKey = QString::fromLatin1(SettingsKeys::INTERFACE_READOUT_CARDS);
        const QString collapsedKey = QString::fromLatin1(SettingsKeys::INTERFACE_READOUT_COLLAPSED);
        if (store != nullptr)
        {
            if (const std::optional<QString> folded = store->selectOptional(cardsKey); folded.has_value())
            {
                column->setFoldedCards(*folded);
            }
            if (const std::optional<QString> collapsed = store->selectOptional(collapsedKey); collapsed.has_value())
            {
                column->setCollapsed(*collapsed == QStringLiteral("true"));
            }
        }
        an::ui::AnReadoutColumn* const shown = column.data();
        wire(shown, &an::ui::AnReadoutColumn::foldsChanged, [this, cardsKey]() {
            if (an::ui::IGlobalSettingsProvider* const settings = core.globalSettings(); settings && column)
            {
                // Every card open is an empty line, not a null one: the store keeps a null string as no value.
                const QString folded = column->foldedCards();
                (void)settings->upsert(cardsKey, folded.isNull() ? QStringLiteral("") : folded);
            }
        });
        wire(shown, &an::ui::AnReadoutColumn::collapsedChanged, [this, collapsedKey](const bool collapsed) {
            if (an::ui::IGlobalSettingsProvider* const settings = core.globalSettings())
            {
                (void)settings->upsert(collapsedKey, collapsed ? QStringLiteral("true") : QStringLiteral("false"));
            }
        });
        an::ui::AnHistogramCard* const histogram = shown->histogramCard();
        wire(histogram, &an::ui::AnHistogramCard::resolutionChanged, [this]() { schedule(true); });
        wire(histogram, &an::ui::AnHistogramCard::histogramWindowRequested,
             onView(&ImageViewControl::executeHistogram));
        wire(shown->statisticsCard(), &an::ui::AnStatisticsCard::statisticsWindowRequested,
             onView(&ImageViewControl::executeStatistics));
        an::ui::AnAstrometryCard* const astrometry = shown->astrometryCard();
        wire(astrometry, &an::ui::AnAstrometryCard::plateSolveRequested, onView(&ImageViewControl::executePlateSolve));
        wire(astrometry, &an::ui::AnAstrometryCard::annotateRequested, onView(&ImageViewControl::executeAnnotateImage));
        wire(astrometry, &an::ui::AnAstrometryCard::infoRequested,
             onView(&ImageViewControl::executeAstrometryInfoViewer));
        wire(astrometry, &an::ui::AnAstrometryCard::overlayToggled, [this](int, const bool on) {
            // An overlay turned on shows only while the view draws its overlays at all.
            ImageViewControl* const shownView = view.data();
            QAction* const overlays = shownView != nullptr
                                          ? shownView->findChild<QAction*>(QStringLiteral("astrometryOverlayAction"))
                                          : nullptr;
            if (on && overlays != nullptr && !overlays->isChecked())
            {
                overlays->setChecked(true);
            }
        });
        an::ui::AnImageFactsCard* const image = shown->imageCard();
        wire(image, &an::ui::AnImageFactsCard::fitsHeaderRequested, onView(&ImageViewControl::executeFITSHeaderViewer));
        wire(image, &an::ui::AnImageFactsCard::exifRequested, onView(&ImageViewControl::executeEXIFViewer));
        wire(image, &an::ui::AnImageFactsCard::summaryRequested, onView(&ImageViewControl::executeSummaryViewer));
    }

    void disconnectColumn()
    {
        for (const QMetaObject::Connection& connection : connections)
        {
            QObject::disconnect(connection);
        }
        connections.clear();
    }

    ApplicationCore& core;
    std::shared_ptr<Gate> gate;
    ViewFollower follower;
    BusFollower bus;
    QTimer debounce;
    QTimer retry;
    std::chrono::milliseconds retryWait = kDebounce;
    QPointer<an::ui::AnReadoutColumn> column;
    QPointer<an::ui::AnCompactReadout> compact;
    IReadoutFrameSource* task = nullptr;
    QPointer<ImageViewControl> view;
    an::ui::RequestTracker tracker;
    std::vector<QMetaObject::Connection> connections;
    std::shared_ptr<EngineCalls> calls;
    ReadoutTelemetry telemetry;
    std::uint64_t inFlight = 0u;
    bool measureWanted = false;
    bool stale = false;
    std::optional<an::ui::ImageStatisticsReport> figures;
    std::optional<an::ui::ImageStatisticsReport> original;
    std::shared_ptr<const an::ui::HistogramDisplayData> counts;
    std::optional<an::ui::DocumentRevision> measuredRevision;
    std::optional<std::uint64_t> measuredFrame;
    std::optional<an::consumer::AssetRevision> measuredOriginal;
    an::ui::HistogramResolution measuredResolution = an::ui::HistogramResolution::Bits8;
    QString solver;
};

ReadoutController::ReadoutController(ApplicationCore& core) : m_impl(std::make_unique<Impl>(core))
{
    m_impl->follow(core.viewService().getActiveImageViewControl());
}

ReadoutController::~ReadoutController() = default;

void ReadoutController::setColumn(an::ui::AnReadoutColumn* column)
{
    Impl& impl = *m_impl;
    impl.disconnectColumn();
    impl.column = column;
    if (column == nullptr)
    {
        return;
    }
    impl.connectColumn();
    impl.bindView();
    impl.setStale(impl.stale);
    impl.present();
    impl.publish();
}

void ReadoutController::setCompactReadout(an::ui::AnCompactReadout* compact)
{
    m_impl->compact = compact;
    if (compact != nullptr && m_impl->task != nullptr)
    {
        m_impl->setStale(m_impl->stale);
        m_impl->present();
        m_impl->publish();
    }
}

void ReadoutController::followTask(IReadoutFrameSource* source)
{
    Impl& impl = *m_impl;
    if (impl.task != nullptr)
    {
        impl.task->setFrameListener({});
    }
    impl.task = source;
    impl.original.reset();
    impl.measuredOriginal.reset();
    impl.measuredFrame.reset();
    if (source == nullptr)
    {
        // The active view again, measured again: the figures shown were the preview's.
        impl.view = nullptr;
        impl.follow(impl.core.viewService().getActiveImageViewControl());
        return;
    }
    source->setFrameListener([&impl]() {
        const std::optional<IReadoutFrameSource::Frame> frame = impl.task->latestFrame();
        if (!frame.has_value())
        {
            return;
        }
        if (frame->interactive)
        {
            ++impl.telemetry.interactiveFrames;
            impl.setStale(true);
            return;
        }
        impl.schedule(true);
    });
    impl.view = nullptr;
    impl.follow(source->target());
    impl.schedule(true);
}

ImageViewControl* ReadoutController::followedView() const
{
    return m_impl->view.data();
}

bool ReadoutController::followsTask() const noexcept
{
    return m_impl->task != nullptr;
}

bool ReadoutController::isStale() const noexcept
{
    return m_impl->stale;
}

bool ReadoutController::isIdle() const
{
    return !m_impl->debounce.isActive() && !m_impl->retry.isActive() && m_impl->inFlight == 0u;
}

ReadoutTelemetry ReadoutController::telemetry() const
{
    ReadoutTelemetry shown = m_impl->telemetry;
    shown.engineCalls = m_impl->calls->all.load();
    shown.engineCallsOnGuiThread = m_impl->calls->onGui.load();
    return shown;
}

const std::optional<an::ui::ImageStatisticsReport>& ReadoutController::shownFigures() const noexcept
{
    return m_impl->figures;
}

std::shared_ptr<const an::ui::HistogramDisplayData> ReadoutController::shownCounts() const
{
    return m_impl->counts;
}

const std::optional<an::ui::ImageStatisticsReport>& ReadoutController::originalFigures() const noexcept
{
    return m_impl->original;
}

void ReadoutController::updateNow()
{
    if (m_impl->debounce.isActive() || m_impl->retry.isActive())
    {
        m_impl->debounce.stop();
        m_impl->update();
    }
}

} // namespace an::ui::shell
