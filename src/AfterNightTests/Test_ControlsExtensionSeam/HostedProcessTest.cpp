// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// An extension's process drawn by the application while its code runs in a host, over a stand-in for that host: the
/// parameters the host tells become the panel's controls, a change made in the panel is told to it once, an action's
/// answer lands in the panel, each frame is lent from the application's side of the seam and shown only when its ticket
/// commits, an Apply is one step of the target's history under its lock and a cancelled one commits nothing, a
/// description that is not whole draws no control and says why, and the GUI thread waits for none of it.

#include "Test_ControlsExtensionSeam/ExtensionSeamTestSupport.h"
#include "Test_ControlsExtensionSeam/HostedPanelStandIn.h"
#include "Test_ControlsViewer/ViewerTestContext.h"

#include "app/ExtensionHostManager.h"
#include "app/ExtensionHostedSession.h"
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
#include <QThreadPool>
#include <QTimer>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

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

/// The viewer's context, with what a process window reaches as it opens and closes: a store of process settings and the
/// register of the open process windows.
class HostedProcessContext final : public ViewerTestContext,
                                   public an::ui::IProcessSettingsProvider,
                                   public an::ui::IProcessRegistry
{
public:
    using ViewerTestContext::ViewerTestContext;

    [[nodiscard]] an::ui::IProcessSettingsProvider* getProcessSettings() override
    {
        return this;
    }

    [[nodiscard]] an::ui::IProcessRegistry* getProcessRegistry() override
    {
        return this;
    }

    void registerProcessInstance(TopLevelProcessBaseControl* process) override
    {
        m_open.push_back(process);
    }

    void unregisterProcessInstance(TopLevelProcessBaseControl* process) override
    {
        m_open.erase(std::remove(m_open.begin(), m_open.end(), process), m_open.end());
    }

    bool upsert(const QString& processName, int winState, int width, int height, const QString& json) override
    {
        m_stored[processName] = Stored{winState, width, height, json};
        return true;
    }

    [[nodiscard]] bool select(const QString& processName, int& winState, int& width, int& height,
                              QString& json) override
    {
        const auto found = m_stored.find(processName);
        if (found == m_stored.end())
        {
            return false;
        }
        winState = found->second.winState;
        width = found->second.width;
        height = found->second.height;
        json = found->second.json;
        return true;
    }

private:
    struct Stored
    {
        int winState = 0;
        int width = 0;
        int height = 0;
        QString json;
    };
    std::map<QString, Stored> m_stored;
    std::vector<TopLevelProcessBaseControl*> m_open;
};

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
        m_process = new ExtensionHostedProcess(m_target.get(), std::move(setup));
        m_hosted = m_process->hostedSession();
        m_hosted->bind(m_manager, *m_controller);
        // Shown as a process window is, its banner built at its first show.
        m_process->show();
        pumpQuietly();
    }

    /// What the coordinator does at each pump of the session.
    void pump()
    {
        std::string error;
        static_cast<void>(m_manager.pumpSession(&m_controller->running_session, 0, &error));
        if (m_hosted)
        {
            m_hosted->route();
            m_hosted->dispatch();
        }
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
