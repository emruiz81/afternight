// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// An extension's process drawn by the application while its code runs in a host, over a stand-in for that host: the
/// parameters the host tells become the panel's controls, a change made in the panel is told to it once, an action's
/// answer lands in the panel, each frame is lent from the application's side of the seam and shown only when its ticket
/// commits, frames pause for an Apply and resume after it, an Apply is one step of the target's history under its lock
/// and a cancelled one commits nothing, a description that is not whole draws no control and says why, the process
/// says that its host starts and waits for it, a host that dies leaves the process and lets the target go, Restart
/// brings the panel back, and the GUI thread waits for none of it.

#include "Test_ControlsExtensionSeam/ExtensionSeamTestSupport.h"
#include "Test_ControlsExtensionSeam/HostedPanelStandIn.h"
#include "Test_ControlsExtensionSeam/HostedProcessContext.h"
#include "Test_ControlsViewer/ViewerTestContext.h"

#include "app/ExtensionHostManager.h"
#include "app/ExtensionHostedSession.h"
#include "app/ExtensionLaunchPreparations.h"
#include "app/ExtensionPreviewFrames.h"
#include "controls/ExtensionHostedProcess.h"
#include "controls/ImageViewCompanions.h"
#include "controls/ImageViewControl.h"
#include "session/ImageDocument.h"

#include <QCborArray>
#include <QCborMap>
#include <QElapsedTimer>
#include <QPointer>
#include <QPushButton>
#include <QSlider>
#include <QStringList>
#include <QThreadPool>
#include <QTimer>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace
{

using namespace an::tests::seam;
using an::tests::viewer::InstalledContext;
using an::tests::viewer::makeDocument;
using an::tests::viewer::makeView;
using an::tests::viewer::openViewerContext;
using an::tests::viewer::pumpQuietly;
using an::tests::viewer::pumpUntil;
using an::tests::viewer::sampleOf;
using an::tests::viewer::settle;
using an::tests::viewer::ViewerTestContext;
using an::tests::viewer::ViewHandle;
using an::ui::extensions::ExecutionExecuteMessage;
using an::ui::extensions::ExtensionHostedProcess;
using an::ui::extensions::ExtensionHostedSession;
using an::ui::extensions::ExtensionHostManager;
using an::ui::extensions::ExtensionHostSessionState;
using an::ui::extensions::ExtensionProtocolMessage;
using an::ui::extensions::ExtensionSessionController;
using an::ui::extensions::UiEventMessage;

constexpr const char* kSessionId = "hosted-session";
constexpr const char* kName = "Seam Extension";

/// The value the target holds at a position and channel: distinct, below a half, so doubling it stays below one.
[[nodiscard]] float targetValue(std::uint32_t x, std::uint32_t y, std::uint32_t channel) noexcept
{
    return static_cast<float>(seamSample(x, y, channel)) / 512.0f;
}

/// A float parameter, as an extension's `get_params()` states one.
[[nodiscard]] QCborMap strengthParam(double initial = 0.5)
{
    return QCborMap{{QStringLiteral("id"), QStringLiteral("strength")},
                    {QStringLiteral("type"), QStringLiteral("float")},
                    {QStringLiteral("label"), QStringLiteral("Strength")},
                    {QStringLiteral("default"), initial},
                    {QStringLiteral("min"), 0.0},
                    {QStringLiteral("max"), 1.0},
                    {QStringLiteral("step"), 0.01}};
}

/// An action, as an extension states one.
[[nodiscard]] QCborMap autoAction()
{
    return QCborMap{{QStringLiteral("id"), QStringLiteral("auto")},
                    {QStringLiteral("type"), QStringLiteral("action")},
                    {QStringLiteral("label"), QStringLiteral("Auto")}};
}

class HostedProcess : public ::testing::Test
{
protected:
    void SetUp() override
    {
        std::optional<an::tests::controls::TestSessionProvider> provider =
            an::tests::viewer::openFileProvider(2u, an::consumer::MemoryAllowances{});
        ASSERT_TRUE(provider.has_value());
        m_context = std::make_unique<HostedProcessContext>(std::move(provider->owned()));
        m_installed.emplace(*m_context);
        m_target = makeView("target");
        std::optional<an::ui::ImageDocument> document = makeDocument<float>(
            m_context->engineSession(), 6u, 4u, Channels::Rgb, targetValue,
            an::ui::DocumentNames{QStringLiteral("target"), QStringLiteral("C:/images/target.fit")});
        ASSERT_TRUE(document.has_value());
        m_target->setDocument(std::move(*document));
        ASSERT_TRUE(settle(*m_target));
        m_target->initializeNewImageHistory("Image opened");

        m_controller = std::make_unique<ExtensionSessionController>();
        m_controller->descriptor_id = "seam.extension.process";
        m_controller->display_name = kName;
        m_controller->target_control = m_target.get();
        m_host.emplace();
    }

    void TearDown() override
    {
        if (m_process)
        {
            // A frame's worker runs the process's body: the frames are refused and have landed before it goes.
            m_process->stopPreviews();
            static_cast<void>(pumpFor([this]() { return m_process->isProcessIdle(); }));
            delete m_process.data();
        }
        if (m_hosted)
        {
            m_hosted->unbind({.sessionId = kSessionId, .failed = false, .reason = {}});
        }
        QThreadPool::globalInstance()->waitForDone();
        pumpQuietly();
        m_controller.reset();
        m_host.reset();
        m_target.reset();
        m_installed.reset();
        m_context.reset();
    }

    /// Makes the process over the target and binds it to the stand-in's session.
    void open(const bool previews, const QCborArray& kept = {}, const bool runtimeReady = true)
    {
        m_host->install(*m_controller, kSessionId, runtimeReady);
        ExtensionHostedProcess::Setup setup;
        setup.displayName = kName;
        setup.previews = previews;
        setup.keptParams = kept.toJsonArray();
        setup.keepOpen = m_keepOpen;
        m_process = new ExtensionHostedProcess(m_target.get(), std::move(setup));
        m_hosted = m_process->hostedSession();
        m_hosted->bind(m_manager, *m_controller);
        // Shown as a process window is, its banner built at its first show.
        m_process->show();
        pumpQuietly();
    }

    /// What the coordinator does at each pump of the session: a session that ended is let go, as the coordinator
    /// stops it.
    void pump()
    {
        std::string error;
        static_cast<void>(m_manager.pumpSession(&m_controller->running_session, 0, &error));
        if (m_hosted && m_hosted->bound() && !m_controller->isActive())
        {
            m_hosted->unbind(an::ui::extensions::endOf(*m_controller));
            return;
        }
        if (m_hosted)
        {
            m_hosted->route();
            m_hosted->dispatch();
        }
    }

    /// The button of an act the process's message offers now, by its text; null when it offers none of that name.
    [[nodiscard]] QPushButton* messageAction(const QString& text) const
    {
        for (QPushButton* button : m_process->findChildren<QPushButton*>(QStringLiteral("anMessageAction")))
        {
            if (button->text() == text && !button->isHidden() && button->window() == m_process->window())
            {
                return button;
            }
        }
        return nullptr;
    }

    /// Whether what a person sets in the panel can be set now.
    [[nodiscard]] bool controlsEnabled() const
    {
        const auto* slider = m_process->findChild<QSlider*>(QStringLiteral("anNativeParamDoubleSlider"));
        return slider != nullptr && slider->isEnabled();
    }

    [[nodiscard]] bool applyEnabled() const
    {
        const auto* apply = m_process->findChild<QPushButton*>(QStringLiteral("anNativeProcessExecuteButton"));
        return apply != nullptr && apply->isEnabled();
    }

    [[nodiscard]] std::function<void()> pumper()
    {
        return [this]() { pump(); };
    }

    /// Turns the event loop, pumping the session, until a condition holds.
    [[nodiscard]] bool pumpFor(const std::function<bool()>& condition, int boundMs = 5000)
    {
        QElapsedTimer clock;
        clock.start();
        while (clock.elapsed() < boundMs)
        {
            pump();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            m_host->read();
            if (condition())
            {
                return true;
            }
        }
        return condition();
    }

    /// Tells the parameters and waits until the panel draws them.
    void describe(const QCborArray& params)
    {
        m_host->describe(params);
        ASSERT_TRUE(pumpFor([&]() { return m_process->drawnParams() == params.toJsonArray(); }));
    }

    [[nodiscard]] std::optional<float> previewSample(std::uint32_t x, std::uint32_t y, std::uint32_t channel) const
    {
        ImageViewControl* preview = m_process ? m_process->previewView() : nullptr;
        if (preview == nullptr || !preview->hasImage())
        {
            return std::nullopt;
        }
        return sampleOf<float>(*preview->document().asset(), x, y, channel);
    }

    std::unique_ptr<HostedProcessContext> m_context;
    std::optional<InstalledContext> m_installed;
    ViewHandle m_target;
    std::unique_ptr<ExtensionSessionController> m_controller;
    std::optional<HostedPanelStandIn> m_host;
    ExtensionHostManager m_manager;
    QPointer<ExtensionHostedProcess> m_process;
    std::shared_ptr<ExtensionHostedSession> m_hosted;
    /// Whether the process stays after an Apply, as a manifest's `keep_open` keeps it.
    bool m_keepOpen = false;
};

TEST_F(HostedProcess, TheDescriptionBecomesControls)
{
    open(false);
    EXPECT_EQ(m_process->findChild<QSlider*>(QStringLiteral("anNativeParamDoubleSlider")), nullptr);

    describe(QCborArray{strengthParam(0.25), autoAction()});

    EXPECT_NE(m_process->findChild<QSlider*>(QStringLiteral("anNativeParamDoubleSlider")), nullptr);
    EXPECT_NE(m_process->findChild<QPushButton*>(QStringLiteral("anNativeParamActionButton")), nullptr);
    EXPECT_DOUBLE_EQ(m_process->currentParams().value(QStringLiteral("strength")).toDouble(), 0.25);
    EXPECT_TRUE(m_process->hostReady());
}

TEST_F(HostedProcess, AValueChangeSendsOneEvent)
{
    open(false);
    describe(QCborArray{strengthParam()});
    auto* slider = m_process->findChild<QSlider*>(QStringLiteral("anNativeParamDoubleSlider"));
    ASSERT_NE(slider, nullptr);

    // A drag: five changes in one turn of the event loop.
    for (int step = 1; step <= 5; ++step)
    {
        slider->setValue(slider->minimum() + step * (slider->maximum() - slider->minimum()) / 10);
    }
    const double last = m_process->currentParams().value(QStringLiteral("strength")).toDouble();

    const std::optional<ExtensionProtocolMessage> event = m_host->next("ui.event", pumper());
    ASSERT_TRUE(event.has_value());
    UiEventMessage told;
    ASSERT_TRUE(an::ui::extensions::uiEventFromProtocolMessage(*event, &told));
    EXPECT_EQ(told.event_name, "parameters_changed");
    const QCborMap value = told.value.toMap();
    EXPECT_DOUBLE_EQ(value.value(QStringLiteral("params")).toMap().value(QStringLiteral("strength")).toDouble(), last);
    EXPECT_EQ(value.value(QStringLiteral("changed")).toArray(), QCborArray{QStringLiteral("strength")});
    EXPECT_EQ(value.value(QStringLiteral("reason")).toString(), QStringLiteral("user"));

    // Nothing more is told of the burst.
    static_cast<void>(pumpFor([]() { return false; }, 200));
    EXPECT_EQ(m_host->waiting("ui.event"), 0u);
}

TEST_F(HostedProcess, AnActionsAnswerLandsInThePanel)
{
    open(false);
    describe(QCborArray{strengthParam(0.5), autoAction()});
    auto* button = m_process->findChild<QPushButton*>(QStringLiteral("anNativeParamActionButton"));
    ASSERT_NE(button, nullptr);
    button->click();

    const std::optional<ExtensionProtocolMessage> event = m_host->next("ui.event", pumper());
    ASSERT_TRUE(event.has_value());
    UiEventMessage told;
    ASSERT_TRUE(an::ui::extensions::uiEventFromProtocolMessage(*event, &told));
    EXPECT_EQ(told.event_name, "clicked");
    EXPECT_EQ(told.widget_id, "auto");
    EXPECT_DOUBLE_EQ(
        told.value.toMap().value(QStringLiteral("params")).toMap().value(QStringLiteral("strength")).toDouble(), 0.5);

    an::ui::extensions::PanelPatchMessage patch;
    patch.action_id = "auto";
    patch.values = QCborMap{{QStringLiteral("strength"), 0.75}};
    m_host->send(an::ui::extensions::toProtocolMessage(patch));
    ASSERT_TRUE(pumpFor([&]() {
        return std::abs(m_process->currentParams().value(QStringLiteral("strength")).toDouble() - 0.75) < 1e-9;
    }));

    // The action's answer told the extension its parameters changed, as its window always did.
    const std::optional<ExtensionProtocolMessage> changed = m_host->next("ui.event", pumper());
    ASSERT_TRUE(changed.has_value());
    ASSERT_TRUE(an::ui::extensions::uiEventFromProtocolMessage(*changed, &told));
    EXPECT_EQ(told.event_name, "parameters_changed");
    EXPECT_EQ(told.value.toMap().value(QStringLiteral("reason")).toString(), QStringLiteral("param_action:auto"));
}

TEST_F(HostedProcess, AFrameIsLentFromThePoolAndShownWhenItsTicketCommits)
{
    open(true, QCborArray{strengthParam()});
    describe(QCborArray{strengthParam()});

    const std::optional<ExtensionProtocolMessage> request = m_host->next("execution.preview", pumper());
    ASSERT_TRUE(request.has_value());
    an::ui::extensions::ExecutionPreviewMessage preview;
    ASSERT_TRUE(an::ui::extensions::executionPreviewFromProtocolMessage(*request, &preview));
    // The frame's source and the output it is to write lie in the session's own directory.
    for (const auto& buffer : preview.buffers)
    {
        EXPECT_EQ(std::filesystem::path(buffer.mapped_file_path).parent_path().filename().string(), kSessionId)
            << buffer.mapped_file_path;
    }
    m_host->answerPreview(*request, [](const float value) { return value * 2.0f; });

    ASSERT_TRUE(pumpFor([&]() {
        const std::optional<float> shown = previewSample(3u, 2u, 1u);
        return shown.has_value() && std::abs(*shown - targetValue(3u, 2u, 1u) * 2.0f) < 1e-6f;
    }));

    // The frame's buffers went back to the pool: the next frame is lent the same files.
    m_process->applyParamUpdates(QCborMap{{QStringLiteral("strength"), 0.7}});
    std::optional<ExtensionProtocolMessage> next;
    for (int frame = 0; frame < 4; ++frame)
    {
        next = m_host->next("execution.preview", pumper());
        ASSERT_TRUE(next.has_value());
        an::ui::extensions::ExecutionPreviewMessage again;
        ASSERT_TRUE(an::ui::extensions::executionPreviewFromProtocolMessage(*next, &again));
        ASSERT_EQ(again.buffers.size(), preview.buffers.size());
        for (std::size_t index = 0; index < preview.buffers.size(); ++index)
        {
            EXPECT_EQ(again.buffers[index].mapped_file_path, preview.buffers[index].mapped_file_path);
        }
        if (std::abs(again.params.value(QStringLiteral("strength")).toDouble() - 0.7) < 1e-9)
        {
            break;
        }
        // A frame asked for before the change: answered, and the one of the change follows it.
        m_host->answerPreview(*next, [](const float value) { return value * 2.0f; });
    }
    m_host->answerPreview(*next, [](const float value) { return value * 3.0f; });
    ASSERT_TRUE(pumpFor([&]() {
        const std::optional<float> shown = previewSample(3u, 2u, 1u);
        return shown.has_value() && std::abs(*shown - targetValue(3u, 2u, 1u) * 3.0f) < 1e-6f;
    }));
}

TEST_F(HostedProcess, AStoppedFrameShowsNothingEvenWhenItsHostAnswersIt)
{
    open(true, QCborArray{strengthParam()});
    describe(QCborArray{strengthParam()});
    const std::optional<ExtensionProtocolMessage> first = m_host->next("execution.preview", pumper());
    ASSERT_TRUE(first.has_value());

    // The frame on its way is refused - as an Apply, a change of the image or the close refuse it - and the host is
    // asked to cancel it; the frame a change asks for next waits its turn behind it.
    m_process->stopPreviews();
    const std::optional<ExtensionProtocolMessage> cancel = m_host->next("execution.cancel", pumper());
    ASSERT_TRUE(cancel.has_value());
    m_process->applyParamUpdates(QCborMap{{QStringLiteral("strength"), 0.9}});
    static_cast<void>(pumpFor([]() { return false; }, 100));
    EXPECT_EQ(m_host->waiting("execution.preview"), 0u) << "the host takes one request at a time";

    // A host that did not hear the cancel in time answers the first frame all the same: nothing of it is shown.
    m_host->answerPreview(*first, [](const float) { return 0.875f; });
    const std::optional<ExtensionProtocolMessage> second = m_host->next("execution.preview", pumper());
    ASSERT_TRUE(second.has_value());
    EXPECT_NE(previewSample(1u, 1u, 0u), std::optional<float>(0.875f));
    m_host->answerPreview(*second, [](const float value) { return value + 0.25f; });
    ASSERT_TRUE(pumpFor([&]() {
        const std::optional<float> shown = previewSample(1u, 1u, 0u);
        return shown.has_value() && std::abs(*shown - (targetValue(1u, 1u, 0u) + 0.25f)) < 1e-6f;
    }));
    EXPECT_EQ(m_controller->running_session.state, ExtensionHostSessionState::Active);
}

TEST_F(HostedProcess, AFrameThatLandsAfterTheTaskClosedShowsNothing)
{
    open(true, QCborArray{strengthParam()});
    describe(QCborArray{strengthParam()});
    const std::optional<ExtensionProtocolMessage> request = m_host->next("execution.preview", pumper());
    ASSERT_TRUE(request.has_value());

    m_process->close();
    ASSERT_TRUE(pumpFor([&]() { return m_process.isNull(); }));

    // The host answers the frame of a process that is gone: the session takes the answer, and what was lent goes back.
    m_host->answerPreview(*request, [](const float value) { return value; });
    ASSERT_TRUE(pumpFor([&]() { return m_controller->running_session.in_flight_request_id.empty(); }));
    EXPECT_EQ(m_controller->running_session.state, ExtensionHostSessionState::Active);
    ASSERT_TRUE(pumpFor([&]() { return m_hosted->framePool()->idleBuffers() >= 2u; }));
}

TEST_F(HostedProcess, AnApplyIsOneStepOfTheTargetsHistoryUnderItsLock)
{
    open(false, QCborArray{strengthParam()});
    describe(QCborArray{strengthParam()});
    m_process->applyProcess();

    const std::optional<ExtensionProtocolMessage> request = m_host->next("execution.execute", pumper());
    ASSERT_TRUE(request.has_value());
    EXPECT_TRUE(m_target->isLockedForProcessing());
    ExecutionExecuteMessage execute;
    ASSERT_TRUE(an::ui::extensions::executionExecuteFromProtocolMessage(*request, &execute));
    EXPECT_DOUBLE_EQ(execute.params.value(QStringLiteral("strength")).toDouble(), 0.5);

    m_host->answerExecute(*request, [](const float value) { return value * 2.0f; });
    ASSERT_TRUE(pumpFor([&]() {
        const an::ui::IViewHistory* history = m_target->viewHistory();
        return !m_target->isLockedForProcessing() && history != nullptr && history->previousStepName() == kName;
    }));
    EXPECT_FLOAT_EQ(*sampleOf<float>(*m_target->document().asset(), 2u, 3u, 2u), targetValue(2u, 3u, 2u) * 2.0f);

    // One step: undoing it leaves nothing more to undo, and the Apply to redo.
    ASSERT_TRUE(m_target->viewHistory()->undo());
    ASSERT_TRUE(pumpFor([&]() {
        const an::ui::IViewHistory* history = m_target->viewHistory();
        return history->previousStepName().empty() && history->nextStepName() == kName;
    }));
    EXPECT_FLOAT_EQ(*sampleOf<float>(*m_target->document().asset(), 2u, 3u, 2u), targetValue(2u, 3u, 2u));
}

TEST_F(HostedProcess, ACancelledApplyCommitsNothing)
{
    open(false, QCborArray{strengthParam()});
    describe(QCborArray{strengthParam()});
    m_process->applyProcess();
    const std::optional<ExtensionProtocolMessage> request = m_host->next("execution.execute", pumper());
    ASSERT_TRUE(request.has_value());

    ASSERT_TRUE(m_process->cancelRun());
    const std::optional<ExtensionProtocolMessage> cancel = m_host->next("execution.cancel", pumper());
    ASSERT_TRUE(cancel.has_value());
    m_host->answerWith(request->request_id, "execute", "cancelled");

    ASSERT_TRUE(pumpFor([&]() {
        return !m_target->isLockedForProcessing() && m_controller->running_session.in_flight_request_id.empty() &&
               m_process->runState().phase != an::ui::ProcessRunState::Phase::Running;
    }));
    EXPECT_TRUE(m_target->viewHistory()->previousStepName().empty()) << m_target->viewHistory()->previousStepName();
    EXPECT_FLOAT_EQ(*sampleOf<float>(*m_target->document().asset(), 2u, 3u, 2u), targetValue(2u, 3u, 2u));
    // The session goes on: a cancel is no failure.
    EXPECT_EQ(m_controller->running_session.state, ExtensionHostSessionState::Active);
}

TEST_F(HostedProcess, ARefusedDescriptionShowsItsReasonAndNoControl)
{
    open(false, QCborArray{strengthParam()});
    // A range whose least is above its most: refused whole by the panel, which draws nothing and says why.
    const QCborArray refused{QCborMap{{QStringLiteral("id"), QStringLiteral("strength")},
                                      {QStringLiteral("type"), QStringLiteral("float")},
                                      {QStringLiteral("default"), 0.5},
                                      {QStringLiteral("min"), 1.0},
                                      {QStringLiteral("max"), 0.0}}};
    m_host->describe(refused);
    ASSERT_TRUE(pumpFor([&]() { return m_process->drawnParams() == refused.toJsonArray(); }));
    EXPECT_TRUE(m_process->processMessage().contains(QStringLiteral("strength")))
        << m_process->processMessage().toStdString();
    EXPECT_TRUE(m_process->currentParams().isEmpty());

    // A list that is no list is refused before the panel sees it.
    ExtensionProtocolMessage malformed;
    malformed.type = "ui.describe";
    malformed.fields[QStringLiteral("params")] = QStringLiteral("not a list");
    m_host->send(malformed);
    ASSERT_TRUE(pumpFor([&]() { return m_process->drawnParams().isEmpty(); }));
    EXPECT_TRUE(m_process->processMessage().contains(QStringLiteral("params")))
        << m_process->processMessage().toStdString();
    EXPECT_EQ(m_process->findChild<QSlider*>(QStringLiteral("anNativeParamDoubleSlider")), nullptr);
    EXPECT_EQ(m_controller->running_session.state, ExtensionHostSessionState::Active);
}

TEST_F(HostedProcess, NeverWaitsOnTheGuiThread)
{
    open(true, QCborArray{strengthParam()});
    describe(QCborArray{strengthParam()});
    const std::optional<ExtensionProtocolMessage> request = m_host->next("execution.preview", pumper());
    ASSERT_TRUE(request.has_value());

    // The frame waits for a host that does not answer; the GUI thread turns on, a timer firing at its pace.
    int ticks = 0;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&ticks]() { ++ticks; });
    timer.start(10);
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < 400)
    {
        pump();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    timer.stop();
    EXPECT_GE(ticks, 20);

    // An Apply asked for meanwhile waits its turn behind the frame, and its Cancel ends it at once though the host has
    // not answered the frame: nothing of it reaches the host.
    m_process->applyProcess();
    ASSERT_TRUE(pumpFor([&]() { return m_process->runState().running(); }));
    static_cast<void>(pumpFor([]() { return false; }, 100));
    EXPECT_EQ(m_host->waiting("execution.execute"), 0u);
    QElapsedTimer cancelClock;
    cancelClock.start();
    ASSERT_TRUE(m_process->cancelRun());
    ASSERT_TRUE(pumpFor([&]() { return !m_process->runState().running() && !m_target->isLockedForProcessing(); }));
    EXPECT_LT(cancelClock.elapsed(), 1000);
    m_host->answerPreview(*request, [](const float value) { return value; });
    static_cast<void>(pumpFor([]() { return false; }, 200));
    EXPECT_EQ(m_host->waiting("execution.execute"), 0u);
    EXPECT_TRUE(m_target->viewHistory()->previousStepName().empty());
}

TEST_F(HostedProcess, FramesPauseForAnApplyAndResumeAfterIt)
{
    m_keepOpen = true;
    open(true, QCborArray{strengthParam()});
    describe(QCborArray{strengthParam()});
    const std::optional<ExtensionProtocolMessage> frame = m_host->next("execution.preview", pumper());
    ASSERT_TRUE(frame.has_value());

    // The Apply refuses the frame on its way, which the host is asked to cancel, and waits for it to end.
    m_process->applyProcess();
    const std::optional<ExtensionProtocolMessage> cancel = m_host->next("execution.cancel", pumper());
    ASSERT_TRUE(cancel.has_value());
    EXPECT_EQ(m_host->waiting("execution.execute"), 0u);
    m_host->answerWith(frame->request_id, "preview", "cancelled");

    const std::optional<ExtensionProtocolMessage> execute = m_host->next("execution.execute", pumper());
    ASSERT_TRUE(execute.has_value());
    EXPECT_EQ(m_host->waiting("execution.preview"), 0u) << "no frame while the Apply runs";
    m_host->answerExecute(*execute, [](const float value) { return value * 2.0f; });
    ASSERT_TRUE(pumpFor([&]() { return m_target->viewHistory()->previousStepName() == kName; }));

    // A process its manifest keeps open asks for a frame of the image the Apply made: the frames resume.
    ASSERT_FALSE(m_process.isNull());
    const std::optional<ExtensionProtocolMessage> resumed = m_host->next("execution.preview", pumper());
    ASSERT_TRUE(resumed.has_value());
}

TEST_F(HostedProcess, AKeptDescriptionWaitsDisabledWhileItsHostStarts)
{
    open(false, QCborArray{strengthParam(0.3)}, false);

    // The page is there at once, the description kept from an earlier start drawn and disabled, and the body says that
    // the extension starts and how far its host has come.
    EXPECT_TRUE(m_process->isStarting());
    const auto* row = m_process->findChild<QWidget*>(QStringLiteral("hostedStartingRow"));
    ASSERT_NE(row, nullptr);
    EXPECT_TRUE(row->isVisible());
    EXPECT_EQ(m_process->startingText(), (QStringList{QStringLiteral("Starting %1").arg(QString::fromLatin1(kName)),
                                                      QStringLiteral("Its host has started")}));
    ASSERT_NE(m_process->findChild<QSlider*>(QStringLiteral("anNativeParamDoubleSlider")), nullptr);
    EXPECT_FALSE(controlsEnabled());
    EXPECT_FALSE(applyEnabled());

    ExtensionProtocolMessage loaded;
    loaded.type = "runtime.bootstrap_state_ready";
    m_host->send(loaded);
    ASSERT_TRUE(pumpFor([&]() { return m_process->startingText().size() == 3; }));
    EXPECT_EQ(m_process->startingText().back(), QStringLiteral("The extension is loaded"));
    EXPECT_FALSE(controlsEnabled());

    // Ready: the row goes, and the controls and the Apply wait no more.
    ExtensionProtocolMessage ready;
    ready.type = "runtime.ready";
    m_host->send(ready);
    ASSERT_TRUE(pumpFor([&]() { return m_process->hostReady(); }));
    EXPECT_FALSE(m_process->isStarting());
    EXPECT_TRUE(m_process->startingText().isEmpty());
    EXPECT_FALSE(row->isVisible());
    EXPECT_TRUE(controlsEnabled());
    EXPECT_TRUE(applyEnabled());
}

TEST_F(HostedProcess, HostDiesLeavesThePageAndReleasesTheLock)
{
    open(false, QCborArray{strengthParam()});
    describe(QCborArray{strengthParam()});
    m_process->applyProcess();
    const std::optional<ExtensionProtocolMessage> request = m_host->next("execution.execute", pumper());
    ASSERT_TRUE(request.has_value());
    ASSERT_TRUE(m_target->isLockedForProcessing());

    // The host dies under the Apply.
    m_host->die(*m_controller);
    ASSERT_TRUE(pumpFor([&]() {
        return m_process->hostFailed() && !m_target->isLockedForProcessing() && !m_process->runState().running();
    }));

    // The process stays, says so with Close, its controls and its Apply disabled, its run failed; the target is as it
    // was, and the application can go on with it.
    ASSERT_FALSE(m_process.isNull());
    EXPECT_TRUE(m_process->isVisible());
    ASSERT_TRUE(pumpFor([&]() { return m_process->processMessage().contains(QStringLiteral("stopped responding")); }));
    EXPECT_EQ(m_process->processMessageActions(), QStringList{QStringLiteral("Close")})
        << "Restart is offered where the process can be started again";
    EXPECT_FALSE(controlsEnabled());
    EXPECT_FALSE(applyEnabled());
    EXPECT_EQ(m_process->runState().phase, an::ui::ProcessRunState::Phase::Failed);
    EXPECT_TRUE(m_target->viewHistory()->previousStepName().empty());
    EXPECT_FLOAT_EQ(*sampleOf<float>(*m_target->document().asset(), 2u, 3u, 2u), targetValue(2u, 3u, 2u));
    ASSERT_TRUE(m_target->tryRequestLockForProcessing());
    m_target->releaseLockForProcessing();

    // Close closes it.
    QPushButton* close = messageAction(QStringLiteral("Close"));
    ASSERT_NE(close, nullptr);
    close->click();
    ASSERT_TRUE(pumpFor([&]() { return m_process.isNull() || !m_process->isVisible(); }));
}

// An extension that hides its process window puts its process away (`ui.hide`): shown as a window of its own, it hides,
// and its session goes on - a change in the panel still reaches the host, and it is shown again as it was.
TEST_F(HostedProcess, AHostThatHidesItsProcessPutsItAwayAndKeepsItsSession)
{
    open(false, QCborArray{strengthParam()});
    describe(QCborArray{strengthParam()});
    ASSERT_TRUE(m_process->isVisible());

    m_host->send(an::ui::extensions::toProtocolMessage(an::ui::extensions::PanelHideMessage{}));
    ASSERT_TRUE(pumpFor([&]() { return !m_process.isNull() && !m_process->isVisible(); }));
    EXPECT_TRUE(m_hosted->bound()) << "the session goes on";
    EXPECT_TRUE(m_process->hostReady());

    m_process->show();
    auto* slider = m_process->findChild<QSlider*>(QStringLiteral("anNativeParamDoubleSlider"));
    ASSERT_NE(slider, nullptr);
    slider->setValue(slider->maximum());
    EXPECT_TRUE(m_host->next("ui.event", pumper()).has_value()) << "the panel still speaks to its host";
}

// An extension that closes its process window ends its session as a window's extension does (`runtime.closed`), and
// the process closes with it - no failure, no banner.
TEST_F(HostedProcess, AHostThatClosesItsProcessClosesItsTask)
{
    open(false, QCborArray{strengthParam()});
    describe(QCborArray{strengthParam()});

    ExtensionProtocolMessage closed;
    closed.type = "runtime.closed";
    closed.request_id = an::ui::extensions::generateOpaqueToken();
    closed.fields.insert(QStringLiteral("active_work"), false);
    m_host->send(closed);
    ASSERT_TRUE(pumpFor([&]() { return m_process.isNull() || !m_process->isVisible(); }));
    EXPECT_FALSE(m_hosted->bound());
    EXPECT_TRUE(m_process.isNull() || !m_process->hostFailed()) << "a process its extension closed did not fail";
}

TEST_F(HostedProcess, RestartBringsThePanelBack)
{
    open(true, QCborArray{strengthParam()});
    int restarts = 0;
    ExtensionHostedProcess::Hooks hooks;
    hooks.restartSession = [this, &restarts](std::string* /*error*/) {
        // A host started again, as the coordinator starts one into the same process.
        ++restarts;
        m_host.emplace();
        m_controller = std::make_unique<ExtensionSessionController>();
        m_controller->descriptor_id = "seam.extension.process";
        m_controller->display_name = kName;
        m_controller->target_control = m_target.get();
        m_host->install(*m_controller, kSessionId);
        m_hosted->bind(m_manager, *m_controller);
        return true;
    };
    m_process->setHooks(std::move(hooks));
    describe(QCborArray{strengthParam()});
    const std::optional<ExtensionProtocolMessage> frame = m_host->next("execution.preview", pumper());
    ASSERT_TRUE(frame.has_value());

    m_host->die(*m_controller);
    ASSERT_TRUE(pumpFor([&]() { return m_process->hostFailed(); }));
    ASSERT_TRUE(pumpFor([&]() { return messageAction(QStringLiteral("Restart")) != nullptr; }));
    EXPECT_EQ(m_process->processMessageActions(), (QStringList{QStringLiteral("Restart"), QStringLiteral("Close")}));
    EXPECT_FALSE(controlsEnabled());

    messageAction(QStringLiteral("Restart"))->click();
    EXPECT_EQ(restarts, 1);
    describe(QCborArray{strengthParam(0.4)});

    // The panel is back as the host tells it, the banner and the failure gone, and frames go to the new host.
    ASSERT_TRUE(pumpFor([&]() { return m_process->hostReady(); }));
    EXPECT_FALSE(m_process->hostFailed());
    EXPECT_TRUE(m_process->processMessage().isEmpty()) << m_process->processMessage().toStdString();
    EXPECT_TRUE(m_process->processMessageActions().isEmpty());
    EXPECT_NE(m_process->runState().phase, an::ui::ProcessRunState::Phase::Failed);
    EXPECT_TRUE(controlsEnabled());
    EXPECT_TRUE(applyEnabled());
    const std::optional<ExtensionProtocolMessage> again = m_host->next("execution.preview", pumper());
    ASSERT_TRUE(again.has_value());
    m_host->answerPreview(*again, [](const float value) { return value * 2.0f; });
    ASSERT_TRUE(pumpFor([&]() {
        const std::optional<float> shown = previewSample(3u, 2u, 1u);
        return shown.has_value() && std::abs(*shown - targetValue(3u, 2u, 1u) * 2.0f) < 1e-6f;
    }));
}

/// A preparation of an extension's environment held until a case lets it go, as `ExtensionLaunchPreparations` runs one
/// on a worker: it says what it does, waits, and returns prepared unless it was stopped. The worker shares it, so it
/// outlives a case that ends before the work returns.
struct HeldPreparation : std::enable_shared_from_this<HeldPreparation>
{
    std::mutex mutex;
    std::condition_variable letGo;
    bool released = false;
    std::atomic<bool> returned{false};
    std::atomic<bool> sawItsStop{false};

    [[nodiscard]] an::ui::extensions::ExtensionLaunchPreparations::Work work()
    {
        return [this, self = shared_from_this()](
                   const std::string&, const an::ui::extensions::ExtensionLaunchPreparations::Progress& progress,
                   const an::ui::extensions::ExtensionOperationCancellationToken& cancellation) {
            progress(an::ui::extensions::ExtensionOperationProgress{
                .phase_id = "install", .message = "Installing numpy", .current = 0, .total = 0, .indeterminate = true});
            {
                // As a preparation's work does, it looks at its stop as it goes, not only when it is woken.
                const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(20);
                std::unique_lock lock(mutex);
                while (!released && !cancellation.isCancellationRequested() && std::chrono::steady_clock::now() < until)
                {
                    letGo.wait_for(lock, std::chrono::milliseconds(10));
                }
            }
            an::ui::extensions::ExtensionLaunchPreparationOutcome outcome;
            sawItsStop = cancellation.isCancellationRequested();
            outcome.prepared = !sawItsStop;
            returned = true;
            return outcome;
        };
    }

    void release()
    {
        {
            const std::lock_guard lock(mutex);
            released = true;
        }
        letGo.notify_all();
    }
};

/// Lets a held preparation go however the case ends, before the preparations that run it go.
struct LetGoAtTheEnd
{
    std::shared_ptr<HeldPreparation> held;

    ~LetGoAtTheEnd()
    {
        held->release();
    }
};

TEST_F(HostedProcess, AFirstStartSaysWhatItsPreparationDoesInThePageAndStartsIntoIt)
{
    const auto held = std::make_shared<HeldPreparation>();
    std::vector<std::string> completed;
    an::ui::extensions::ExtensionLaunchPreparations preparations(
        nullptr, held->work(),
        [this, &completed](const std::string& processId, const std::string&,
                           an::ui::extensions::ExtensionLaunchPreparationOutcome) {
            // The launch that follows: a session started into the page, as the coordinator starts one.
            completed.push_back(processId);
            m_host->install(*m_controller, kSessionId);
            m_process->hostedSession()->bind(m_manager, *m_controller);
            m_hosted = m_process->hostedSession();
        });
    preparations.setStartingPage([this](const std::string&) {
        ExtensionHostedProcess::Setup setup;
        setup.displayName = kName;
        m_process = new ExtensionHostedProcess(m_target.get(), std::move(setup));
        m_process->show();
        return m_process.data();
    });
    const LetGoAtTheEnd letGo{held};
    preparations.launch("seam.extension.process", "seam.extension", kName);

    // The page is there at once, and no window of the preparation's own.
    ASSERT_FALSE(m_process.isNull());
    EXPECT_EQ(preparations.windowOf("seam.extension"), nullptr);
    EXPECT_EQ(preparations.pageOf("seam.extension"), m_process.data());
    EXPECT_TRUE(m_process->isStarting());
    ASSERT_TRUE(pumpFor([&]() { return m_process->startingText().contains(QStringLiteral("Installing numpy")); }));
    EXPECT_EQ(m_process->startingText().front(), QStringLiteral("Starting %1").arg(QString::fromLatin1(kName)));

    // Prepared: the launch starts into the page, which stays and says how far its host has come.
    held->release();
    ASSERT_TRUE(pumpFor([&]() { return held->returned.load() && !completed.empty(); }));
    EXPECT_EQ(completed, std::vector<std::string>{"seam.extension.process"});
    ASSERT_FALSE(m_process.isNull());
    EXPECT_TRUE(m_process->isVisible());
    EXPECT_FALSE(m_process->startingText().contains(QStringLiteral("Installing numpy")));
    EXPECT_TRUE(m_process->startingText().contains(QStringLiteral("Its host has started")));
    describe(QCborArray{strengthParam()});
    EXPECT_TRUE(m_process->hostReady());
}

TEST_F(HostedProcess, ClosingTheStartingPageStopsThePreparation)
{
    const auto held = std::make_shared<HeldPreparation>();
    bool completed = false;
    an::ui::extensions::ExtensionLaunchPreparations preparations(
        nullptr, held->work(),
        [&completed](const std::string&, const std::string&, an::ui::extensions::ExtensionLaunchPreparationOutcome) {
            completed = true;
        });
    preparations.setStartingPage([this](const std::string&) {
        ExtensionHostedProcess::Setup setup;
        setup.displayName = kName;
        m_process = new ExtensionHostedProcess(m_target.get(), std::move(setup));
        m_process->show();
        return m_process.data();
    });
    const LetGoAtTheEnd letGo{held};
    preparations.launch("seam.extension.process", "seam.extension", kName);
    ASSERT_FALSE(m_process.isNull());

    m_process->close();
    ASSERT_TRUE(pumpFor([&]() { return held->returned.load() && !preparations.isPreparing("seam.extension"); }));
    EXPECT_TRUE(held->sawItsStop.load()) << "the page's close stops the preparation, as its window's Cancel does";
    EXPECT_FALSE(completed) << "a stopped preparation launches nothing";
}

TEST(HostedPanelEvents, AreRefusedForASessionWhosePanelTheApplicationDoesNotDraw)
{
    ExtensionSessionController controller;
    HostedPanelStandIn host;
    host.install(controller, kSessionId);
    controller.running_session.hosted_panel = false;
    ExtensionHostManager manager;
    UiEventMessage event;
    event.widget_id = "panel";
    event.event_name = "parameters_changed";
    event.value = QCborMap();
    std::string error;
    EXPECT_FALSE(manager.sendPanelEvent(&controller.running_session, event, &error));
    EXPECT_NE(error.find("panel"), std::string::npos) << error;
    controller.running_session.hosted_panel = true;
    EXPECT_TRUE(manager.sendPanelEvent(&controller.running_session, event, &error)) << error;
}

} // namespace
