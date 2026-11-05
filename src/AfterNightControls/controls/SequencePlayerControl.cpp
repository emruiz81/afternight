// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The sequence player: its source kept for the sequence a view shows, frames read on the thread pool and shown through
/// the view only while they are the frame last asked for, playback on a GUI-thread timer, and the companion the view
/// reaches it through.

#include "controls/SequencePlayerControl.h"

#include "app/AppContextHelpers.h"
#include "app/FileCompanions.h"
#include "controls/IAppContext.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewFiles.h"
#include "controls/QClickableSlider.h"
#include "controls/SequenceContainers.h"
#include "session/IEngineSessionProvider.h"
#include "widgets/ImageViewWidget.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/IO/FrameSource.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <AfterNightPlatform/Logging.h>

#include <QAction>
#include <QCoreApplication>
#include <QIcon>
#include <QMetaObject>
#include <QPointer>
#include <QSpinBox>
#include <QThreadPool>
#include <QTimer>
#include <QWidgetAction>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <mutex>
#include <stop_token>
#include <string>
#include <utility>
#include <variant>

namespace an::ui::detail
{

/// The source a player keeps open for the sequence it shows, read by one worker at a time.
///
/// A read is stopped only by `close`, never by a newer request: a read stopped half way would leave the source failed
/// and the next one would open it again, so a superseded read finishes its one frame and the GUI thread drops it.
/// `close` never waits: it stops a read still waiting for admission, and when a read is under way, that read closes the
/// source as it lands.
class SequenceFeed
{
public:
    SequenceFeed(const an::consumer::EngineSession& session, an::ui::DocumentSequence sequence, bool autoDebayer)
        : m_session(session), m_sequence(std::move(sequence)), m_autoDebayer(autoDebayer)
    {
    }

    SequenceFeed(const SequenceFeed&) = delete;
    SequenceFeed& operator=(const SequenceFeed&) = delete;

    /// The sequence the source reads.
    [[nodiscard]] const an::ui::DocumentSequence& sequence() const noexcept
    {
        return m_sequence;
    }

    /// Whether the frames are demosaiced as they are read.
    [[nodiscard]] bool demosaics() const noexcept
    {
        return m_autoDebayer;
    }

    /// Reads one frame into a new asset of the session, opening the source with the first read.
    ///
    /// @param frame the zero-based frame.
    /// @param stop observed before the frame is read; a read under way finishes.
    /// @return the frame, or why it could not be read.
    [[nodiscard]] an::Result<an::consumer::ImageAsset> read(std::uint64_t frame, const std::stop_token& stop)
    {
        an::Result<an::consumer::ImageAsset> result = refusal(an::ErrorCode::Internal, "no frame was read");
        {
            std::unique_lock lock(m_mutex);
            result = readLocked(frame, stop);
        }
        closeIfRequested();
        return result;
    }

    /// Lets the source go: now when no read is under way, and when the read under way lands otherwise. Never waits.
    void close() noexcept
    {
        m_closing.request_stop();
        closeIfRequested();
    }

    /// Whether the source is open.
    [[nodiscard]] bool holdsSource() const noexcept
    {
        return m_open.load();
    }

private:
    [[nodiscard]] static an::Status refusal(an::ErrorCode code, std::string message)
    {
        return an::Status::failure(code, std::move(message));
    }

    [[nodiscard]] an::Result<an::consumer::ImageAsset> readLocked(std::uint64_t frame, const std::stop_token& stop)
    {
        if (m_closing.stop_requested())
        {
            return refusal(an::ErrorCode::Cancelled, "the player let the sequence go");
        }
        if (stop.stop_requested())
        {
            return refusal(an::ErrorCode::Cancelled, "the frame is no longer asked for");
        }
        an::Result<an::consumer::OperationScope> scope = m_session.admit(an::consumer::OperationRequest{
            .purpose = an::consumer::OperationPurpose::Preview, .stopToken = m_closing.get_token()});
        if (!scope.hasValue())
        {
            return scope.status();
        }
        if (m_closing.stop_requested() || stop.stop_requested())
        {
            return refusal(an::ErrorCode::Cancelled, "the frame is no longer asked for");
        }
        if (!m_source.has_value())
        {
            an::Result<an::ui::OpenedSequence> opened = an::ui::openSequenceSource(
                scope->engine(), scope->execution(), m_sequence.sourcePath, m_sequence.descriptor.format);
            if (!opened.hasValue())
            {
                return opened.status();
            }
            m_source.emplace(std::move(opened->source));
            m_open.store(true);
        }
        an::Result<std::optional<std::pair<std::uint64_t, an::consumer::AnyImage>>> read =
            an::ui::readSequenceFrame(scope->engine(), scope->execution(), *m_source, frame);
        if (!read.hasValue())
        {
            if (m_source->state() == an::SequenceState::Failed)
            {
                // A source that failed reads nothing more; the next frame opens the sequence again.
                closeSource();
            }
            return read.status();
        }
        if (!read->has_value())
        {
            return refusal(an::ErrorCode::OutOfRange, "the sequence holds no frame " + std::to_string(frame + 1u));
        }
        an::consumer::AnyImage image = std::move((*read)->second);
        const bool mosaic = std::visit(
            [](const auto& typed) {
                const std::optional<an::ImageDescriptor> stored = typed.descriptor();
                return stored.has_value() && stored->isMosaiced();
            },
            image);
        if (m_autoDebayer && mosaic)
        {
            an::Result<an::consumer::AnyImage> colour = an::ui::demosaicViewFrame(
                scope->engine(), scope->execution(), image, m_sequence.demosaic.value_or(an::DemosaicMethod::Rcd));
            if (!colour.hasValue())
            {
                return colour.status();
            }
            image = std::move(*colour);
        }
        scope->end();
        return an::consumer::ImageAsset::adopt(m_session, std::move(image));
    }

    void closeSource() noexcept
    {
        if (m_source.has_value())
        {
            (void)m_source->close();
            m_source.reset();
        }
        m_open.store(false);
    }

    void closeIfRequested() noexcept
    {
        if (!m_closing.stop_requested())
        {
            return;
        }
        std::unique_lock lock(m_mutex, std::try_to_lock);
        if (lock.owns_lock())
        {
            closeSource();
        }
    }

    const an::consumer::EngineSession& m_session;
    an::ui::DocumentSequence m_sequence;
    bool m_autoDebayer;
    std::mutex m_mutex;
    std::optional<an::FrameSource> m_source;
    std::stop_source m_closing;
    std::atomic_bool m_open{false};
};

} // namespace an::ui::detail

namespace
{

/// The slowest and fastest playback the frames-per-second control offers.
constexpr int kMinimumFramesPerSecond = 1;
constexpr int kMaximumFramesPerSecond = 60;

QIcon sequencePlayerIcon(const QString& name)
{
    if (const an::ui::IIconProviderPtr provider = process_utils::currentIconProvider())
    {
        return provider->getIcon(name);
    }
    return {};
}

/// The process's session, or null when the application lends none.
const an::consumer::EngineSession* currentSession()
{
    if (auto* provider = process_utils::currentEngineSessionProvider())
    {
        return &provider->engineSession();
    }
    return nullptr;
}

/// A frame rate as the control's whole frames per second.
int controlFramesPerSecond(const std::optional<an::SequenceRate>& rate)
{
    const double framesPerSecond = rate.has_value() ? an::ui::framesPerSecondOf(*rate) : 1.0;
    return std::clamp(static_cast<int>(std::lround(framesPerSecond)), kMinimumFramesPerSecond, kMaximumFramesPerSecond);
}

} // namespace

SequencePlayerControl::SequencePlayerControl(ImageViewControl& view)
    : QToolBar(nullptr), ImageViewControlListener(&view), m_View(view)
{
    setIconSize(QSize(32, 32));

    m_PlayTimer = new QTimer(this);
    QObject::connect(m_PlayTimer, &QTimer::timeout, this, &SequencePlayerControl::playbackTick);

    m_PlayAction = addAction(sequencePlayerIcon(QStringLiteral("play")), QStringLiteral("Play sequence"), this,
                             &SequencePlayerControl::onPlayTriggered);
    m_StopAction = addAction(sequencePlayerIcon(QStringLiteral("stop")), QStringLiteral("Stop sequence"), this,
                             [this]() { stop(); });
    m_PreviousAction = addAction(sequencePlayerIcon(QStringLiteral("previous")), QStringLiteral("Previous frame"), this,
                                 &SequencePlayerControl::showPreviousFrame);
    m_NextAction = addAction(sequencePlayerIcon(QStringLiteral("next")), QStringLiteral("Next frame"), this,
                             &SequencePlayerControl::showNextFrame);

    auto* spinBox = new QSpinBox();
    spinBox->setRange(0, 0);
    spinBox->setSingleStep(1);
    auto* slider = new QClickableSlider(Qt::Horizontal);
    slider->setRange(0, 0);
    INIT_SPINBOX_CTRL(m_FrameControl, spinBox, slider, &SequencePlayerControl::onFrameControlChanged);

    auto* sliderAction = new QWidgetAction(this);
    sliderAction->setDefaultWidget(m_FrameControl->getSlider());
    addAction(sliderAction);
    auto* spinBoxAction = new QWidgetAction(this);
    spinBoxAction->setDefaultWidget(m_FrameControl->getSpinBox());
    addAction(spinBoxAction);

    m_FramesPerSecondControl = std::make_unique<QSpinBox>();
    m_FramesPerSecondControl->setRange(kMinimumFramesPerSecond, kMaximumFramesPerSecond);
    m_FramesPerSecondControl->setValue(kMinimumFramesPerSecond);
    m_FramesPerSecondControl->setSuffix(QStringLiteral(" FPS"));
    m_FramesPerSecondControl->setToolTip(QStringLiteral("Frames per second"));
    m_FramesPerSecondAction = new QWidgetAction(this);
    m_FramesPerSecondAction->setDefaultWidget(m_FramesPerSecondControl.get());
    addAction(m_FramesPerSecondAction);
    QObject::connect(m_FramesPerSecondControl.get(), &QSpinBox::valueChanged, this,
                     &SequencePlayerControl::onFramesPerSecondChanged);
    m_FramesPerSecondAction->setVisible(false);

    followDocument();
}

SequencePlayerControl::~SequencePlayerControl()
{
    shutdown();
}

QWidget* SequencePlayerControl::widget()
{
    return this;
}

bool SequencePlayerControl::isPlaying() const
{
    return m_Playing;
}

void SequencePlayerControl::pause()
{
    pausePlayback();
}

void SequencePlayerControl::resume()
{
    if (!m_Playing)
    {
        startPlayback();
    }
}

void SequencePlayerControl::stop()
{
    stopPlayback();
}

std::uint64_t SequencePlayerControl::currentFrame() const
{
    return m_Current;
}

void SequencePlayerControl::setCurrentFrame(std::uint64_t frame, std::function<void()> shown)
{
    pausePlayback();
    if (m_Count == 0u)
    {
        if (shown)
        {
            shown();
        }
        return;
    }
    m_Current = (std::min)(frame, m_Count - 1u);
    updateControls();
    requestFrame(m_Current, std::move(shown));
}

void SequencePlayerControl::setEnabled(bool enabled)
{
    if (!enabled)
    {
        pausePlayback();
    }
    QToolBar::setEnabled(enabled && m_Count > 0u);
}

void SequencePlayerControl::shutdown()
{
    if (m_ShutDown)
    {
        return;
    }
    m_ShutDown = true;
    m_Playing = false;
    m_PlayTimer->stop();
    m_Requests.cancel();
    setViewControl(nullptr);
    if (m_Feed)
    {
        m_Feed->close();
        m_Feed.reset();
    }
}

an::Status SequencePlayerControl::saveTo(const QString& path)
{
    const std::optional<an::ui::DocumentSequence> sequence = m_View.document().sequence();
    const an::consumer::EngineSession* session = currentSession();
    if (!sequence.has_value() || session == nullptr)
    {
        return an::Status::failure(an::ErrorCode::InvalidState, "the view shows no sequence the player can write");
    }
    return an::ui::writeSequenceFile(*session, *sequence, path, {});
}

void SequencePlayerControl::onImageLoaded(ImageViewControl* ctrl)
{
    if (ctrl == &m_View)
    {
        followDocument();
    }
}

std::uint64_t SequencePlayerControl::frameCount() const
{
    return m_Count;
}

void SequencePlayerControl::showNextFrame()
{
    pausePlayback();
    if (m_Count == 0u || m_Current + 1u >= m_Count)
    {
        updateControls();
        return;
    }
    ++m_Current;
    updateControls();
    requestFrame(m_Current, {});
}

void SequencePlayerControl::showPreviousFrame()
{
    pausePlayback();
    if (m_Current == 0u)
    {
        updateControls();
        return;
    }
    --m_Current;
    updateControls();
    requestFrame(m_Current, {});
}

double SequencePlayerControl::framesPerSecond() const
{
    return static_cast<double>(m_FramesPerSecondControl->value());
}

void SequencePlayerControl::setFramesPerSecond(double framesPerSecond)
{
    m_FramesPerSecondControl->setValue(
        std::clamp(static_cast<int>(std::lround(framesPerSecond)), kMinimumFramesPerSecond, kMaximumFramesPerSecond));
}

void SequencePlayerControl::setFramesPerSecondControlVisible(bool visible)
{
    m_FramesPerSecondAction->setVisible(visible);
}

void SequencePlayerControl::setPlayButtonEnabled(bool enable, const QString& tooltip)
{
    m_PlayAction->setEnabled(enable);
    m_PlayAction->setToolTip(tooltip);
}

std::uint64_t SequencePlayerControl::framesShown() const
{
    return m_Shown;
}

bool SequencePlayerControl::isIdle() const
{
    return m_Asked == m_Landed;
}

bool SequencePlayerControl::holdsSource() const
{
    return m_Feed != nullptr && m_Feed->holdsSource();
}

void SequencePlayerControl::followDocument()
{
    if (m_ShutDown)
    {
        return;
    }
    const an::ui::ImageDocument& document = m_View.document();
    const std::optional<an::ui::DocumentSequence> sequence = document.valid() ? document.sequence() : std::nullopt;
    m_DocumentIdentity = document.identity();
    if (!sequence.has_value())
    {
        pausePlayback();
        m_Requests.cancel();
        if (m_Feed)
        {
            m_Feed->close();
            m_Feed.reset();
        }
        m_Count = 0u;
        m_Current = 0u;
        updateControls();
        return;
    }
    const bool autoDebayer = m_View.isSequenceAutodebayerEnabled();
    const bool sameSource = m_Feed && m_Feed->sequence().sourcePath == sequence->sourcePath &&
                            m_Feed->sequence().descriptor.format == sequence->descriptor.format &&
                            m_Feed->sequence().descriptor.frameCount == sequence->descriptor.frameCount &&
                            m_Feed->sequence().demosaic == sequence->demosaic && m_Feed->demosaics() == autoDebayer;
    if (!sameSource)
    {
        pausePlayback();
        m_Requests.cancel();
        if (m_Feed)
        {
            m_Feed->close();
            m_Feed.reset();
        }
        if (const an::consumer::EngineSession* session = currentSession(); session != nullptr)
        {
            m_Feed = std::make_shared<an::ui::detail::SequenceFeed>(*session, *sequence, autoDebayer);
        }
        else
        {
            LOG_WARNING("Sequence player: no engine session is lent, so the frames of '{}' cannot be read",
                        sequence->sourcePath.toStdString());
        }
        m_FramesPerSecondControl->blockSignals(true);
        m_FramesPerSecondControl->setValue(controlFramesPerSecond(sequence->descriptor.nominalFrameRate));
        m_FramesPerSecondControl->blockSignals(false);
    }
    m_Count = sequence->descriptor.frameCount.value_or(0u);
    m_Current = m_Count > 0u ? (std::min)(sequence->currentFrame, m_Count - 1u) : 0u;
    updateControls();
}

void SequencePlayerControl::requestFrame(std::uint64_t frame, std::function<void()> shown)
{
    if (m_ShutDown || !m_Feed)
    {
        if (shown)
        {
            shown();
        }
        return;
    }
    an::ui::CommitTicket ticket = m_Requests.issueUnbound();
    ++m_Asked;
    QPointer<SequencePlayerControl> guard(this);
    QThreadPool::globalInstance()->start([feed = m_Feed, frame, ticket, guard, shown = std::move(shown)]() {
        auto read = std::make_shared<an::Result<an::consumer::ImageAsset>>(feed->read(frame, ticket.stopToken()));
        QCoreApplication* application = QCoreApplication::instance();
        if (application == nullptr)
        {
            return;
        }
        QMetaObject::invokeMethod(
            application,
            [guard, frame, ticket, read, shown]() {
                if (guard)
                {
                    guard->frameRead(frame, ticket, std::move(*read), shown);
                }
            },
            Qt::QueuedConnection);
    });
}

void SequencePlayerControl::frameRead(std::uint64_t frame, an::ui::CommitTicket ticket,
                                      an::Result<an::consumer::ImageAsset>&& read, std::function<void()> shown)
{
    ++m_Landed;
    // A request that ends without showing its frame still tells its caller, which may show a frame of its own.
    if (m_ShutDown || ticket.verdict() != an::ui::CommitVerdict::Commit)
    {
        if (shown)
        {
            shown();
        }
        return;
    }
    if (!read.hasValue())
    {
        if (read.status().code() != an::ErrorCode::Cancelled)
        {
            LOG_WARNING("Sequence player: frame {} of '{}' could not be read: {}", frame + 1u, m_View.getUniqueName(),
                        read.status().message());
            pausePlayback();
            if (auto* dialogs = process_utils::currentDialogService())
            {
                dialogs->notifyErrorMessageAsync(&m_View, QStringLiteral("Sequence Player"),
                                                 QString::fromStdString(read.status().message()));
            }
        }
        if (shown)
        {
            shown();
        }
        return;
    }
    if (m_View.document().identity() != m_DocumentIdentity)
    {
        if (shown)
        {
            shown();
        }
        return;
    }
    QPointer<SequencePlayerControl> guard(this);
    const std::uint64_t identity = m_DocumentIdentity;
    m_View.replaceImage(std::move(*read), true, [guard, frame, identity, shown](const an::Status& replaced) {
        if (guard && replaced.ok() && !guard->m_ShutDown && guard->m_View.document().identity() == identity)
        {
            if (std::optional<an::ui::DocumentSequence> sequence = guard->m_View.document().sequence())
            {
                sequence->currentFrame = frame;
                guard->m_View.document().setSequence(std::move(sequence));
            }
            ++guard->m_Shown;
        }
        if (shown)
        {
            shown();
        }
    });
}

void SequencePlayerControl::playbackTick()
{
    if (!m_Playing || m_ShutDown)
    {
        return;
    }
    // A frame still on its way is not overtaken: playback drops a tick rather than a frame it asked for.
    if (!isIdle())
    {
        return;
    }
    if (m_Current + 1u >= m_Count)
    {
        pausePlayback();
        return;
    }
    ++m_Current;
    updateControls();
    requestFrame(m_Current, {});
}

void SequencePlayerControl::startPlayback()
{
    if (m_ShutDown || m_Count == 0u || !m_Feed)
    {
        return;
    }
    if (m_Current + 1u >= m_Count)
    {
        // Playing from the last frame plays the sequence again from its first.
        m_Current = 0u;
        requestFrame(m_Current, {});
    }
    m_Playing = true;
    if (ImageViewWidget* widget = m_View.getViewWidget())
    {
        widget->enablePerformanceMode(true);
    }
    m_PlayTimer->start((std::max)(1, 1000 / m_FramesPerSecondControl->value()));
    updateControls();
}

void SequencePlayerControl::pausePlayback()
{
    if (!m_Playing)
    {
        return;
    }
    m_Playing = false;
    m_PlayTimer->stop();
    if (ImageViewWidget* widget = m_View.getViewWidget())
    {
        widget->enablePerformanceMode(false);
    }
    updateControls();
}

void SequencePlayerControl::stopPlayback()
{
    const bool wasPlaying = m_Playing;
    pausePlayback();
    if (!wasPlaying || m_Count == 0u)
    {
        updateControls();
        return;
    }
    m_Current = 0u;
    updateControls();
    requestFrame(m_Current, {});
}

void SequencePlayerControl::onPlayTriggered()
{
    if (m_Playing)
    {
        pausePlayback();
    }
    else
    {
        startPlayback();
    }
}

void SequencePlayerControl::onFrameControlChanged(int value)
{
    if (m_UpdatingControls || value <= 0 || m_Count == 0u)
    {
        return;
    }
    pausePlayback();
    m_Current = (std::min)(static_cast<std::uint64_t>(value - 1), m_Count - 1u);
    updateControls();
    requestFrame(m_Current, {});
}

void SequencePlayerControl::onFramesPerSecondChanged(int value)
{
    if (m_Playing)
    {
        m_PlayTimer->start((std::max)(1, 1000 / (std::max)(value, kMinimumFramesPerSecond)));
    }
}

void SequencePlayerControl::updateControls()
{
    m_UpdatingControls = true;
    const int count =
        static_cast<int>((std::min)(m_Count, static_cast<std::uint64_t>((std::numeric_limits<int>::max)())));
    if (count == 0)
    {
        m_FrameControl->setMinimum(0, false);
        m_FrameControl->setMaximum(0, false);
        m_FrameControl->setValue(0, false);
        m_FrameControl->getSpinBox()->setSuffix(QString());
    }
    else
    {
        m_FrameControl->setMinimum(1, false);
        m_FrameControl->setMaximum(count, false);
        m_FrameControl->setValue(static_cast<int>(m_Current) + 1, false);
        m_FrameControl->getSpinBox()->setSuffix(QStringLiteral("/%1").arg(count));
    }
    m_PreviousAction->setEnabled(m_Current > 0u);
    m_NextAction->setEnabled(m_Current + 1u < m_Count);
    m_StopAction->setEnabled(m_Playing);
    m_PlayAction->setIcon(sequencePlayerIcon(m_Playing ? QStringLiteral("pause") : QStringLiteral("play")));
    m_UpdatingControls = false;
}

namespace an::ui
{

std::unique_ptr<IViewSequencePlayer> createViewSequencePlayer(::ImageViewControl& view)
{
    return std::make_unique<SequencePlayerControl>(view);
}

} // namespace an::ui
