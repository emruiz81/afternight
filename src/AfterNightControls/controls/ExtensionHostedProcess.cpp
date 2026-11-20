// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "controls/ExtensionHostedProcess.h"

#include "app/ExtensionCommon.h"
#include "app/ExtensionHostManager.h"
#include "app/ExtensionHostedSession.h"
#include "app/ExtensionPreviewFrames.h"
#include "app/ProcessRegistry.h"
#include "app/SharedBufferManager.h"
#include "controls/ExtensionParamPanel.h"
#include "controls/IAppContext.h"
#include "controls/ImageViewControl.h"
#include "controls/ProcessPageLayout.h"
#include "controls/ProcessPresentation.h"
#include "session/ExecutionScope.h"
#include "theme/AnDesignTokens.h"
#include "theme/AnimationUtils.h"
#include "theme/ComponentParts.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightPlatform/Logging.h>

#include <QCloseEvent>
#include <QCoreApplication>
#include <QFontMetrics>
#include <QHideEvent>
#include <QPainter>
#include <QPointer>
#include <QShowEvent>
#include <QSizePolicy>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <utility>
#include <vector>

#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(an::ui::extensions::ExtensionHostedProcess)

namespace an::ui::extensions
{

namespace
{

using Clock = std::chrono::steady_clock;

/// The file the frames are traced to, a line a frame, as `AFTERNIGHT_PREVIEW_FRAME_TRACE` names it: the side of the
/// seam that lends the frames writes it.
std::filesystem::path frameTracePath()
{
    const QString named = qEnvironmentVariable("AFTERNIGHT_PREVIEW_FRAME_TRACE").trimmed();
    return named.isEmpty() ? std::filesystem::path() : std::filesystem::path(named.toStdWString());
}

/// What a request's answer says when it is not a success, as a status: a cancel is the engine's cancel, so the
/// framework refuses the run without a word, and so is a request its session's end answered, whose process says why in
/// its own stopped state; anything else says why.
an::Status statusOf(const HostedRequest::Answer& answer)
{
    if (answer.answered && answer.status == "cancelled")
    {
        return an::Status::failure(an::ErrorCode::Cancelled, "the extension's run was cancelled");
    }
    if (!answer.answered)
    {
        return an::Status::failure(an::ErrorCode::Cancelled,
                                   answer.error.empty() ? "the extension's host did not answer" : answer.error);
    }
    return an::Status::failure(an::ErrorCode::InvalidState,
                               answer.detail.empty() ? "the extension's run failed" : answer.detail);
}

/// What an Apply lends its host: the target exported, the destination the host writes and the manual mask, removed
/// when the request ends, however it ends.
struct ApplyLending
{
    SharedImageBuffer source;
    SharedImageBuffer destination;
    std::vector<SharedImageBuffer> auxiliary;

    ApplyLending() = default;
    ApplyLending(const ApplyLending&) = delete;
    ApplyLending& operator=(const ApplyLending&) = delete;

    ~ApplyLending()
    {
        std::vector<SharedImageBuffer*> buffers{&source, &destination};
        for (SharedImageBuffer& buffer : auxiliary)
        {
            buffers.push_back(&buffer);
        }
        removeLentBuffers(buffers);
    }
};

QString startingTitle()
{
    return QCoreApplication::translate("ExtensionHostedProcess", "Starting");
}

/// The quiet row a hosted process's body holds while its host starts: a turning arc, "Starting" and the process's
/// name, and under them how far the host has come, a line a step.
class StartingRow final : public QWidget
{
public:
    StartingRow(QString name, QWidget* parent) : QWidget(parent), m_name(std::move(name))
    {
        setObjectName(QStringLiteral("hostedStartingRow"));
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        m_turn.setInterval(parts::kPageRunSpinnerStep);
        QObject::connect(&m_turn, &QTimer::timeout, this, [this]() {
            // Animations turned off while it turned: the arc stops where an arc drawn with them off stands.
            if (!AnimationUtils::animationsEnabled())
            {
                m_turn.stop();
                m_angle = 0;
                update();
                return;
            }
            m_angle = (m_angle + parts::kPageRunSpinnerDegrees) % 360;
            update();
        });
    }

    void setLines(QStringList lines)
    {
        if (lines == m_lines)
        {
            return;
        }
        m_lines = std::move(lines);
        updateGeometry();
        update();
    }

    [[nodiscard]] QSize sizeHint() const override
    {
        const DesignTokens& t = tokens();
        const QFontMetrics title(titleFont(t));
        const QFontMetrics line(lineFont(t));
        const int head = (std::max)(title.height(), parts::kPageRunSpinner);
        return QSize(parts::kPageColumnStandard,
                     head + static_cast<int>(m_lines.size()) * line.lineSpacing() + t.metrics.gap);
    }

    [[nodiscard]] QSize minimumSizeHint() const override
    {
        return QSize(0, sizeHint().height());
    }

protected:
    void showEvent(QShowEvent* event) override
    {
        QWidget::showEvent(event);
        if (AnimationUtils::animationsEnabled())
        {
            m_turn.start();
        }
    }

    void hideEvent(QHideEvent* event) override
    {
        m_turn.stop();
        QWidget::hideEvent(event);
    }

    void paintEvent(QPaintEvent* /*event*/) override
    {
        const DesignTokens& t = tokens();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QFont strong = titleFont(t);
        const QFontMetrics strongMetrics(strong);
        const int head = (std::max)(strongMetrics.height(), parts::kPageRunSpinner);

        // The turning arc.
        const int spinner = parts::kPageRunSpinner;
        const QRectF arc(parts::kPageRunSpinnerStroke, (head - spinner) / 2.0, spinner, spinner);
        painter.setPen(QPen(t.colors.accentPrimary, parts::kPageRunSpinnerStroke, Qt::SolidLine, Qt::RoundCap));
        painter.drawArc(arc, -m_angle * 16, 270 * 16);

        // "Starting", then the process's name, quieter.
        const int textLeft = static_cast<int>(arc.right()) + t.metrics.gap;
        const QString title = startingTitle();
        painter.setFont(strong);
        painter.setPen(t.colors.textPrimary);
        painter.drawText(QRect(textLeft, 0, width() - textLeft, head), Qt::AlignVCenter | Qt::AlignLeft, title);
        const int nameLeft = textLeft + parts::textWidth(strong, title) + t.metrics.gap / 2;
        const QFont quiet = lineFont(t);
        painter.setFont(quiet);
        painter.setPen(t.colors.textSecondary);
        const QFontMetrics quietMetrics(quiet);
        painter.drawText(QRect(nameLeft, 0, (std::max)(0, width() - nameLeft), head), Qt::AlignVCenter | Qt::AlignLeft,
                         quietMetrics.elidedText(m_name, Qt::ElideRight, (std::max)(0, width() - nameLeft)));

        // How far the host has come, a line a step, under the text.
        int top = head;
        for (const QString& line : m_lines)
        {
            painter.drawText(QRect(textLeft, top, width() - textLeft, quietMetrics.lineSpacing()),
                             Qt::AlignVCenter | Qt::AlignLeft,
                             quietMetrics.elidedText(line, Qt::ElideRight, (std::max)(0, width() - textLeft)));
            top += quietMetrics.lineSpacing();
        }
    }

private:
    [[nodiscard]] static QFont titleFont(const DesignTokens& t)
    {
        return parts::sizedFont(t.typography.body, t.metrics.fontSmall, QFont::DemiBold);
    }

    [[nodiscard]] static QFont lineFont(const DesignTokens& t)
    {
        return parts::sizedFont(t.typography.body, t.metrics.fontSmall);
    }

    QString m_name;
    QStringList m_lines;
    QTimer m_turn;
    int m_angle = 0;
};

/// The preview's body: each frame lent from the pool of the session it is bound to, run by the host's
/// `execute_preview`, and taken back. A frame stopped while the host runs it is given up there, and what it lent stays
/// lent until the host answers.
ExtensionPreviewBody hostedPreviewBody(std::weak_ptr<ExtensionHostedSession> weakHosted,
                                       const an::consumer::EngineSession* session,
                                       std::shared_ptr<PreviewFrameTrace> trace)
{
    return [weakHosted = std::move(weakHosted), session,
            trace = std::move(trace)](const ExtensionPreviewFrame& frame) -> an::Result<an::consumer::ImageAsset> {
        const Clock::time_point started = Clock::now();
        const std::shared_ptr<ExtensionHostedSession> hosted = weakHosted.lock();
        const std::shared_ptr<PreviewFrameBufferPool> pool = hosted ? hosted->framePool() : nullptr;
        if (pool == nullptr || session == nullptr)
        {
            // No session is bound - not yet, or no more: the frame is refused without a word, and the host's readiness
            // asks for one again. A host that failed says so in the banner itself.
            return an::Status::failure(an::ErrorCode::Cancelled, "the extension's host is not running");
        }

        PreviewFrameSpans spans;
        ExtensionPreviewFrame lentFrame = frame;
        lentFrame.outputRole = SharedBufferRole::Preview;
        an::Result<PreviewFrameBuffers> lent = lendPreviewFrame(*pool, lentFrame, &spans);
        if (!lent.hasValue())
        {
            return lent.status();
        }
        auto buffers = std::make_shared<PreviewFrameBuffers>(std::move(*lent));
        std::vector<SharedBufferDescriptor> descriptors{buffers->source.descriptor(), buffers->output.descriptor()};
        for (const SharedImageBuffer& auxiliary : buffers->auxiliary)
        {
            descriptors.push_back(auxiliary.descriptor());
        }

        const Clock::time_point sent = Clock::now();
        spans.prepare = std::chrono::duration_cast<std::chrono::nanoseconds>(sent - started) -
                        (spans.lendSource + spans.lendOutput + spans.lendMask);
        const std::shared_ptr<HostedRequest> request =
            hosted->submit(HostedRequest::Kind::Preview, frame.params, std::move(descriptors), buffers);
        const std::optional<HostedRequest::Answer> answer = request->wait(frame.stop);
        if (!answer.has_value())
        {
            hosted->withdraw(request);
            return an::Status::failure(an::ErrorCode::Cancelled, "the frame was stopped");
        }
        spans.run = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - sent);
        if (!answer->succeeded())
        {
            return statusOf(*answer);
        }
        an::Result<an::consumer::ImageAsset> made = takePreviewFrameResult(pool->manager(), *session, *buffers, &spans);
        buffers->release(&spans);
        if (made.hasValue() && trace && trace->enabled())
        {
            trace->write(spans);
        }
        return made;
    };
}

} // namespace

ExtensionHostedProcess::ExtensionHostedProcess(ImageViewControl* target, Setup setup)
    : ExtensionNativeProcessWindow(setup.displayName, std::string(), target, PageShape{.previews = setup.previews}),
      m_setup(std::move(setup)), m_hosted(std::make_shared<ExtensionHostedSession>()),
      m_trace(std::make_shared<PreviewFrameTrace>(frameTracePath()))
{
    // The panel's events cross to another process: a burst from one control is told once, its last.
    paramPanel()->setEventDelivery(ExtensionParamPanel::EventDelivery::LastOfABurst);
    setPreviewScaling(m_setup.previewScaling, m_setup.previewRegionMargin);

    ExtensionHostedSession::Listener listener;
    listener.started = [this](const ExtensionHostedSession::StartStep step) {
        m_preparation.clear();
        addStartingLine(step == ExtensionHostedSession::StartStep::HostStarted ? tr("Its host has started")
                                                                               : tr("The extension is loaded"));
    };
    listener.ready = [this]() { onHostReady(); };
    listener.described = [this](const QJsonArray& params, const QString& refusal) { onDescribed(params, refusal); };
    listener.answered = [this](const QString& actionId, const QCborMap& values, const QString& error) {
        onActionAnswered(actionId, values, error);
    };
    listener.progressed = [this](const std::optional<double> progress, const QString& text) {
        reportRunProgress(progress, text);
    };
    listener.ended = [this](const ExtensionSessionEnd& end) { onSessionEnded(end); };
    listener.hidden = [this]() { putAway(); };
    m_hosted->setListener(std::move(listener));

    // What a person does in the panel is told to the host, where the extension hears it as its window's always did.
    setParamChangedCallback([this](const QCborMap& params, const QStringList& changedKeys, const QString& reason) {
        std::string error;
        if (!m_hosted->tellParametersChanged(params, changedKeys, reason, &error))
        {
            LOG_DEBUG("A hosted panel's change was not told to its host: {}", error);
        }
    });
    setParamActionCallback([this](const std::string& actionId, const QCborMap& params, ParamActionCompletion done) {
        std::string error;
        if (!m_hosted->tellAction(QString::fromStdString(actionId), params, &error))
        {
            showProcessMessage(tr("Action refused"), QString::fromStdString(error), ProcessMessageTone::Error);
            done(QCborMap());
            return;
        }
        // The window refuses another action until this one is answered.
        m_actionAnswer = std::move(done);
    });
    setTargetChangedCallback([this](const std::string& viewName) {
        if (m_hooks.targetChosen)
        {
            m_hooks.targetChosen(viewName);
        }
    });

    if (!m_setup.keptParams.isEmpty())
    {
        m_drawnParams = m_setup.keptParams;
        setParamDefs(m_drawnParams);
    }
    if (m_setup.previews)
    {
        setPreviewBody(hostedPreviewBody(m_hosted, engineSession(), m_trace));
    }

    // Until its host is ready the body says it starts, and what is drawn - the parameters kept from an earlier start -
    // waits disabled.
    m_startingRow = new StartingRow(QString::fromStdString(m_setup.displayName), this);
    insertAtControlsHead(m_startingRow);
    applyHostState();
}

ExtensionHostedProcess::~ExtensionHostedProcess()
{
    // The session object outlives the process: it tells it nothing more. The frames on their way are refused before
    // the session's buffers go.
    m_hosted->setListener({});
    stopPreviews();
    if (!m_sessionEnded && m_hooks.closeSession)
    {
        m_hooks.closeSession();
    }
}

void ExtensionHostedProcess::setHooks(Hooks hooks)
{
    m_hooks = std::move(hooks);
    setSettingsCallbacks(m_hooks.settingGet, m_hooks.settingSet);
    setOpenSettingsCallback(m_hooks.openSettings);
    listOpenImages();
}

const std::shared_ptr<ExtensionHostedSession>& ExtensionHostedProcess::hostedSession() const noexcept
{
    return m_hosted;
}

bool ExtensionHostedProcess::hostReady() const noexcept
{
    return m_hostReady;
}

bool ExtensionHostedProcess::hostFailed() const noexcept
{
    return m_hostFailed;
}

bool ExtensionHostedProcess::isStarting() const noexcept
{
    return !m_hostReady && !m_hostFailed;
}

QStringList ExtensionHostedProcess::startingText() const
{
    if (!isStarting())
    {
        return {};
    }
    QStringList text{QStringLiteral("%1 %2").arg(startingTitle(), QString::fromStdString(m_setup.displayName))};
    if (!m_preparation.isEmpty())
    {
        text.push_back(m_preparation);
    }
    text.append(m_startingLines);
    return text;
}

void ExtensionHostedProcess::setPreparation(const QString& line)
{
    m_preparation = line.trimmed();
    applyHostState();
}

const QJsonArray& ExtensionHostedProcess::drawnParams() const noexcept
{
    return m_drawnParams;
}

an::ui::ProcessSurface ExtensionHostedProcess::processSurface() const
{
    return m_setup.previews ? an::ui::ProcessSurface::PreviewPage : an::ui::ProcessSurface::MirrorPage;
}

an::ui::ProcessColumnWidth ExtensionHostedProcess::processColumnWidth() const
{
    return configuredControlsPanelWidth() > kStandardColumnWidth ? an::ui::ProcessColumnWidth::Wide
                                                                 : an::ui::ProcessColumnWidth::Standard;
}

an::Status ExtensionHostedProcess::executeProcessImpl(const an::ui::ProcessSource& source,
                                                      an::ui::ProcessDestination& destination,
                                                      const an::ui::ProcessRun& run,
                                                      const ProcessExecutionParams* params,
                                                      an::ui::ExecutionScope& scope)
{
    if (run.isForPreview())
    {
        return ExtensionNativeProcessWindow::executeProcessImpl(source, destination, run, params, scope);
    }
    return applyThroughHost(source, destination, params, scope);
}

an::Status ExtensionHostedProcess::applyThroughHost(const an::ui::ProcessSource& source,
                                                    an::ui::ProcessDestination& destination,
                                                    const ProcessExecutionParams* params, an::ui::ExecutionScope& scope)
{
    // Read on the worker, and only here: the snapshot the run took on the GUI thread, and the session object, which
    // never changes. Nothing of the process is read after, since a close cancels the run and lets the process go while
    // the worker wakes from its wait.
    const std::optional<RunReading> reading = runReading(params);
    const std::shared_ptr<ExtensionHostedSession> hosted = m_hosted;
    const std::string bufferSession = hosted->bufferSessionId();
    const an::consumer::EngineSession* session = engineSession();
    if (!reading.has_value() || bufferSession.empty() || session == nullptr)
    {
        return an::Status::failure(an::ErrorCode::InvalidState, "the extension's host is not running");
    }

    // The target exported once, a destination of its shape allocated beside it, and the manual mask the person uses,
    // as the extension's window lent its own Apply.
    const SharedBufferManager manager(extensionHostManagedRootPath());
    const std::string requestId = generateOpaqueToken();
    auto lending = std::make_shared<ApplyLending>();
    ImageExportRequest sourceRequest;
    sourceRequest.sessionId = bufferSession;
    sourceRequest.role = SharedBufferRole::Source;
    sourceRequest.accessMode = SharedBufferAccessMode::ReadWrite;
    sourceRequest.bufferId = requestId + ".src";
    sourceRequest.symbolicName = reading->viewName;
    sourceRequest.fileName = reading->fileName;
    an::Result<SharedImageBuffer> exported = manager.exportImage(source.image(), sourceRequest);
    if (!exported.hasValue())
    {
        return exported.status();
    }
    lending->source = std::move(*exported);

    const SharedBufferDescriptor& described = lending->source.descriptor();
    SharedImageBufferSpec destinationSpec;
    destinationSpec.sessionId = bufferSession;
    destinationSpec.bufferId = requestId + ".dst";
    destinationSpec.symbolicName = "destination";
    destinationSpec.width = described.width;
    destinationSpec.height = described.height;
    destinationSpec.channels = described.channels;
    destinationSpec.pixelType = described.pixel_type;
    destinationSpec.pixelFormat = described.pixel_format;
    destinationSpec.colorSpace = described.color_space;
    destinationSpec.accessMode = SharedBufferAccessMode::ReadWrite;
    destinationSpec.bufferRole = SharedBufferRole::Destination;
    destinationSpec.metadataSidecar = described.metadata_sidecar;
    an::Result<SharedImageBuffer> allocated = manager.allocateBuffer(destinationSpec);
    if (!allocated.hasValue())
    {
        return allocated.status();
    }
    lending->destination = std::move(*allocated);

    if (reading->manualMask)
    {
        an::Result<an::consumer::AssetReadLease> maskLease = reading->manualMask->read(scope.execution().stopToken());
        if (!maskLease.hasValue())
        {
            return maskLease.status();
        }
        ImageExportRequest maskRequest;
        maskRequest.sessionId = bufferSession;
        maskRequest.role = SharedBufferRole::Mask;
        maskRequest.accessMode = SharedBufferAccessMode::ReadOnly;
        maskRequest.bufferId = requestId + ".selection";
        maskRequest.symbolicName = "selection";
        an::Result<SharedImageBuffer> mask = manager.exportImage(maskLease->image(), maskRequest);
        if (!mask.hasValue())
        {
            return mask.status();
        }
        lending->auxiliary.push_back(std::move(*mask));
    }

    std::vector<SharedBufferDescriptor> descriptors{lending->source.descriptor(), lending->destination.descriptor()};
    for (const SharedImageBuffer& auxiliary : lending->auxiliary)
    {
        descriptors.push_back(auxiliary.descriptor());
    }
    const std::shared_ptr<HostedRequest> request =
        hosted->submit(HostedRequest::Kind::Execute, reading->params, std::move(descriptors), lending);
    const std::optional<HostedRequest::Answer> answer = request->wait(scope.execution().stopToken());
    if (!answer.has_value())
    {
        // Cancelled: nothing is committed, and what the run lent goes once the host has answered.
        hosted->withdraw(request);
        return an::Status::failure(an::ErrorCode::Cancelled, "the Apply was cancelled");
    }
    if (!answer->succeeded())
    {
        return statusOf(*answer);
    }
    const auto written = std::find_if(answer->outputs.begin(), answer->outputs.end(), [](const auto& output) {
        return output.buffer_role == SharedBufferRole::Destination;
    });
    if (written == answer->outputs.end())
    {
        return an::Status::failure(an::ErrorCode::InvalidState, "the extension's host returned no result");
    }
    an::Result<ImportedImage> imported = manager.importImage(*session, *written);
    if (!imported.hasValue())
    {
        return imported.status();
    }
    if (scope.stopRequested())
    {
        return an::Status::failure(an::ErrorCode::Cancelled, "the Apply was cancelled");
    }
    return destination.replace(std::move(imported->asset));
}

void ExtensionHostedProcess::resetSettings()
{
    resetParamsToDefaults();
}

an::ui::ProcessStep ExtensionHostedProcess::describeProcessStep(const ProcessExecutionParams* params) const
{
    const std::optional<RunReading> reading = runReading(params);
    return an::ui::ProcessStep{.name = m_setup.displayName,
                               .description = extensionHistoryParametersText(reading ? reading->params : QCborMap())};
}

void ExtensionHostedProcess::getBatchExecutionParameters(bool& createNewImage, bool& closeAtEnd,
                                                         bool& executeInBackground, std::string& newImageName)
{
    ExtensionNativeProcessWindow::getBatchExecutionParameters(createNewImage, closeAtEnd, executeInBackground,
                                                              newImageName);
    closeAtEnd = !m_setup.keepOpen;
}

void ExtensionHostedProcess::pageLayoutMade()
{
    ExtensionNativeProcessWindow::pageLayoutMade();
    if (an::ui::ProcessPageLayout* const layout = pageLayout())
    {
        layout->setTag(an::ui::process_registry::extensionProcessTag(),
                       an::ui::process_registry::extensionProcessTagToolTip());
    }
}

bool ExtensionHostedProcess::acceptsRuns() const
{
    return m_hostReady && !m_hostFailed;
}

void ExtensionHostedProcess::executeProcessEndedImpl(const RTPreviewMode previewMode, const bool sequenceMode)
{
    ExtensionNativeProcessWindow::executeProcessEndedImpl(previewMode, sequenceMode);
    if (m_hostReady && !m_hostFailed)
    {
        return;
    }
    // What the run's end gives back - its controls, its message, its run's state - follows in the same call: the
    // process is shown as its host stands once that has returned.
    const QPointer<ExtensionHostedProcess> guard(this);
    QTimer::singleShot(0, this, [guard]() {
        if (guard)
        {
            guard->applyHostState();
        }
    });
}

void ExtensionHostedProcess::applyHostState()
{
    const bool starting = isStarting();
    if (auto* const row = static_cast<StartingRow*>(m_startingRow))
    {
        QStringList lines;
        if (!m_preparation.isEmpty())
        {
            lines.push_back(m_preparation);
        }
        lines.append(m_startingLines);
        row->setLines(lines);
        row->setVisible(starting);
    }
    setControlsEnabled(m_hostReady && !m_hostFailed);
    refreshApplyEnabled();
    if (!m_hostFailed)
    {
        return;
    }
    const QPointer<ExtensionHostedProcess> guard(this);
    std::vector<an::ui::ProcessMessageAction> actions;
    if (m_hooks.restartSession)
    {
        actions.push_back(an::ui::ProcessMessageAction{.text = tr("Restart"), .act = [guard]() {
                                                           if (guard)
                                                           {
                                                               guard->restartHost();
                                                           }
                                                       }});
    }
    actions.push_back(an::ui::ProcessMessageAction{.text = tr("Close"), .act = [guard]() {
                                                       if (guard)
                                                       {
                                                           guard->close();
                                                       }
                                                   }});
    showProcessMessageWithActions(tr("The extension stopped responding"), m_failure, ProcessMessageTone::Error,
                                  std::move(actions));
    if (!runState().running())
    {
        an::ui::ProcessRunState failed;
        failed.phase = an::ui::ProcessRunState::Phase::Failed;
        failed.text = m_failure;
        setRunState(failed);
    }
}

void ExtensionHostedProcess::addStartingLine(const QString& line)
{
    if (!m_startingLines.contains(line))
    {
        m_startingLines.push_back(line);
    }
    applyHostState();
}

void ExtensionHostedProcess::restartHost()
{
    if (!m_hostFailed || !m_hooks.restartSession)
    {
        return;
    }
    // The process starts again as it first did: the row, its controls waiting, nothing failed.
    m_hostFailed = false;
    m_failure.clear();
    m_startingLines.clear();
    m_sessionEnded = false;
    clearProcessMessage();
    setRunState(an::ui::ProcessRunState{});
    applyHostState();
    std::string error;
    if (!m_hooks.restartSession(&error) && !m_hostReady)
    {
        m_hostFailed = true;
        m_failure = error.empty() ? tr("The extension could not be started again.") : QString::fromStdString(error);
        applyHostState();
    }
}

void ExtensionHostedProcess::closeEvent(QCloseEvent* event)
{
    ExtensionNativeProcessWindow::closeEvent(event);
    if (event != nullptr && !event->isAccepted())
    {
        return;
    }
    // The process closes: its session ends with it.
    stopPreviews();
    if (!m_sessionEnded && m_hooks.closeSession)
    {
        m_sessionEnded = true;
        m_hooks.closeSession();
    }
}

void ExtensionHostedProcess::externalToolRecordMayHaveChanged()
{
    const QCborMap record = resolvedExternalToolRecord();
    if (!m_hostReady || (m_toldToolRecord.has_value() && *m_toldToolRecord == record) ||
        (!m_toldToolRecord.has_value() && record.isEmpty()))
    {
        return;
    }
    std::string error;
    if (m_hosted->tellToolRecord(record, &error))
    {
        m_toldToolRecord = record;
    }
    else
    {
        LOG_DEBUG("A hosted panel's tool configuration was not told to its host: {}", error);
    }
}

void ExtensionHostedProcess::onHostReady()
{
    m_hostReady = true;
    m_hostFailed = false;
    m_startingLines.clear();
    applyHostState();
    // A host started anew knows nothing of what the tool's configuration resolved.
    m_toldToolRecord.reset();
    externalToolRecordMayHaveChanged();
    // A frame asked for before the host was ready was refused: one of the parameters as they stand follows.
    requestPreviewFrame();
}

void ExtensionHostedProcess::onDescribed(const QJsonArray& params, const QString& refusal)
{
    if (!refusal.isEmpty())
    {
        // A list that is not whole draws nothing: what was drawn goes, and the banner says why.
        m_drawnParams = QJsonArray();
        setParamDefs(QJsonArray());
        showProcessMessage(tr("Parameters refused"), refusal, ProcessMessageTone::Error);
        return;
    }
    if (params != m_drawnParams)
    {
        m_drawnParams = params;
        setParamDefs(m_drawnParams);
    }
    listOpenImages();
}

void ExtensionHostedProcess::onActionAnswered(const QString& actionId, const QCborMap& values, const QString& error)
{
    if (!error.isEmpty())
    {
        showProcessMessage(tr("Action failed"),
                           actionId.isEmpty() ? error : QStringLiteral("%1: %2").arg(actionId, error),
                           ProcessMessageTone::Error);
    }
    if (m_actionAnswer)
    {
        std::function<void(QCborMap)> done = std::move(m_actionAnswer);
        m_actionAnswer = nullptr;
        done(error.isEmpty() ? values : QCborMap());
    }
}

void ExtensionHostedProcess::onSessionEnded(const ExtensionSessionEnd& end)
{
    m_hostReady = false;
    if (m_actionAnswer)
    {
        std::function<void(QCborMap)> done = std::move(m_actionAnswer);
        m_actionAnswer = nullptr;
        done(QCborMap());
    }
    if (m_sessionEnded)
    {
        // The process asked for it as it closes.
        return;
    }
    if (!end.failed)
    {
        // The host closed as it should - the extension closed its window, or the application stopped it: the process
        // closes with it.
        m_sessionEnded = true;
        const QPointer<ExtensionHostedProcess> guard(this);
        QTimer::singleShot(0, this, [guard]() {
            if (guard)
            {
                guard->close();
            }
        });
        return;
    }
    // The host failed: the process stays, its run refused and the target's lock let go, its controls disabled, and its
    // banner says so with Restart and Close.
    m_hostFailed = true;
    m_failure = end.reason;
    stopPreviews();
    applyHostState();
}

void ExtensionHostedProcess::putAway()
{
    // Taken into a page, its task is put away by the shell that shows it; a window of its own hides, as the extension's
    // own window did.
    an::ui::IAppContext* const context = an::ui::appContext();
    an::ui::IProcessPresenter* const presenter = context != nullptr ? context->getProcessPresenter() : nullptr;
    if (presenter != nullptr && hasPageLayout())
    {
        presenter->hideTask(*this);
        return;
    }
    hide();
}

void ExtensionHostedProcess::listOpenImages()
{
    if (!m_hooks.listViews)
    {
        return;
    }
    if (!hasPendingTargetSelector() && !hasViewSelectorParams())
    {
        return;
    }
    const std::vector<std::map<std::string, QVariant>> views = m_hooks.listViews();
    if (hasPendingTargetSelector())
    {
        enableTargetSelector(views, pendingTargetChannelFilter());
        clearPendingTargetSelector();
    }
    else
    {
        setAvailableViewSelectorViews(views);
    }
}

} // namespace an::ui::extensions
