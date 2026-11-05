// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What a process window does around its runs: the messages it shows, which reach the application's dialog service at
/// once, or once the event loop turns when they are queued to the window and never after the window is gone; the code
/// events it runs with itself; the title that names its process and its target; and a close while its Apply runs,
/// which cancels the run's progress and closes the window when the run lands.

#include "Test_ControlsProcessFramework/ProcessFrameworkTestSupport.h"

#include "controls/IAppContext.h"
#include "controls/ImageProcessControl.h"
#include "controls/ProgressDialogControl.h"

#include <QCoreApplication>
#include <QEvent>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <gtest/gtest.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{

using namespace an::tests::processes;

/// A grey 8 by 6 Float32 target whose samples are `value`.
[[nodiscard]] ViewHandle flatTarget(const an::consumer::EngineSession& session, float value,
                                    const std::string& name = "target")
{
    std::optional<an::ui::ImageDocument> document =
        greyDocument(session, 8u, 6u, [value](std::uint32_t, std::uint32_t) { return value; });
    EXPECT_TRUE(document.has_value());
    return makeTargetView(name, std::move(*document));
}

/// An application context that lends everything another one lends except a dialog service.
class ContextWithoutDialogService final : public an::ui::IAppContext
{
public:
    explicit ContextWithoutDialogService(an::ui::IAppContext& lending) : m_lending(lending) {}

    [[nodiscard]] an::ui::IIconProviderPtr getIconProvider() override
    {
        return m_lending.getIconProvider();
    }
    [[nodiscard]] an::ui::IDialogService* getDialogService() override
    {
        return nullptr;
    }
    [[nodiscard]] an::ui::IGlobalSettingsProvider* getGlobalSettings() override
    {
        return m_lending.getGlobalSettings();
    }
    [[nodiscard]] an::ui::IInterfaceSettingsRuntimeService* getInterfaceSettingsRuntimeService() override
    {
        return m_lending.getInterfaceSettingsRuntimeService();
    }
    [[nodiscard]] an::ui::IProcessSettingsProvider* getProcessSettings() override
    {
        return m_lending.getProcessSettings();
    }
    [[nodiscard]] an::ui::ITempFileService* getTempFileService() override
    {
        return m_lending.getTempFileService();
    }
    [[nodiscard]] an::ui::IPathMemoryService* getPathMemoryService() override
    {
        return m_lending.getPathMemoryService();
    }
    [[nodiscard]] an::ui::IFileDialogService* getFileDialogService() override
    {
        return m_lending.getFileDialogService();
    }
    [[nodiscard]] an::ui::IViewFactory* getViewFactory() override
    {
        return m_lending.getViewFactory();
    }
    [[nodiscard]] an::ui::IViewRegistry* getViewRegistry() override
    {
        return m_lending.getViewRegistry();
    }
    [[nodiscard]] an::ui::IAppEventBus* getAppEventBus() override
    {
        return m_lending.getAppEventBus();
    }
    [[nodiscard]] an::ui::IProcessRegistry* getProcessRegistry() override
    {
        return m_lending.getProcessRegistry();
    }
    [[nodiscard]] an::ui::IOutputIntentProvider* getOutputIntentProvider() override
    {
        return m_lending.getOutputIntentProvider();
    }
    [[nodiscard]] an::ui::IRecentFilesService* getRecentFilesService() override
    {
        return m_lending.getRecentFilesService();
    }
    [[nodiscard]] an::ui::IThumbnailPathProvider* getThumbnailPathProvider() override
    {
        return m_lending.getThumbnailPathProvider();
    }
    [[nodiscard]] an::ui::IViewControlManager* getViewControlManager() override
    {
        return m_lending.getViewControlManager();
    }
    [[nodiscard]] an::ui::IStatusBarService* getStatusBarService() override
    {
        return m_lending.getStatusBarService();
    }
    [[nodiscard]] an::ui::IActionStateManager* getActionStateManager() override
    {
        return m_lending.getActionStateManager();
    }
    [[nodiscard]] an::ui::IEngineSessionProvider* getEngineSessionProvider() override
    {
        return m_lending.getEngineSessionProvider();
    }
    [[nodiscard]] an::ui::IRawOpenPolicyService* getRawOpenPolicyService() override
    {
        return m_lending.getRawOpenPolicyService();
    }
    [[nodiscard]] QWidget* getMainWindow() override
    {
        return m_lending.getMainWindow();
    }

private:
    an::ui::IAppContext& m_lending;
};

/// Installs another application context for a scope, and the one it replaced again when the scope ends.
class SwappedContext
{
public:
    SwappedContext(an::ui::IAppContext& during, an::ui::IAppContext& after) : m_after(after)
    {
        an::ui::clearAppContext();
        an::ui::setAppContext(&during);
    }
    ~SwappedContext()
    {
        an::ui::clearAppContext();
        an::ui::setAppContext(&m_after);
    }
    SwappedContext(const SwappedContext&) = delete;
    SwappedContext& operator=(const SwappedContext&) = delete;

private:
    an::ui::IAppContext& m_after;
};

/// A process window opened as the launcher opens one, which the framework deletes when it closes. Whatever is left of
/// it when the case ends has its gate opened and its progress cancelled, and is deleted once its runs have landed.
class LaunchedProcess
{
public:
    LaunchedProcess(ImageViewControl& target, const TestProcessOptions& options)
        : m_process(new TestProcess(&target, options))
    {
        m_process->resize(800, 500);
    }

    ~LaunchedProcess()
    {
        if (m_process.isNull())
        {
            return;
        }
        m_process->gate.open();
        if (!m_process->lastProgress.isNull())
        {
            m_process->lastProgress->requestCancel();
        }
        (void)pumpUntil([this]() { return m_process.isNull() || m_process->isProcessIdle(); });
        delete m_process.data();
        QCoreApplication::processEvents();
    }

    LaunchedProcess(const LaunchedProcess&) = delete;
    LaunchedProcess& operator=(const LaunchedProcess&) = delete;

    [[nodiscard]] TestProcess* operator->() const
    {
        return m_process.data();
    }

    /// Whether the framework has deleted the window.
    [[nodiscard]] bool gone() const
    {
        return m_process.isNull();
    }

private:
    QPointer<TestProcess> m_process;
};

/// Turns the event loop, deleting what was let go, until a launched window is gone.
[[nodiscard]] bool pumpUntilGone(const LaunchedProcess& process)
{
    return pumpUntil([&process]() {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        return process.gone();
    });
}

} // namespace

// Each message a process window shows reaches the application's dialog service at once, as its kind.
TEST(ImageProcessControlMessageTest, EachMessageReachesTheDialogServiceAtOnceAsItsKind)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = flatTarget(context->engineSession(), 0.25f);
    ProcessHandle process = openProcess(*target, TestProcessOptions{});
    ASSERT_TRUE(settleProcess(*process));
    context->messages.clear();
    context->errorTexts.clear();

    process->notifyErrorMessage(QStringLiteral("Error title"), QStringLiteral("error text"));
    process->notifyWarnMessage(QStringLiteral("Warning title"), QStringLiteral("warning text"));
    process->notifyInfoMessage(QStringLiteral("Info title"), QStringLiteral("info text"));

    EXPECT_EQ(context->messages,
              (QStringList{QStringLiteral("error:Error title"), QStringLiteral("warning:Warning title"),
                           QStringLiteral("info:Info title")}));
    EXPECT_EQ(context->errorTexts, QStringList{QStringLiteral("error text")});
}

// A window whose application lends no dialog service shows its messages to no one else rather than failing.
TEST(ImageProcessControlMessageTest, AMessageWithNoDialogServiceLentReachesNoOne)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = flatTarget(context->engineSession(), 0.25f);
    ProcessHandle process = openProcess(*target, TestProcessOptions{});
    ASSERT_TRUE(settleProcess(*process));
    context->messages.clear();

    ContextWithoutDialogService withoutDialogs(*context);
    {
        const SwappedContext swapped(withoutDialogs, *context);
        process->notifyErrorMessage(QStringLiteral("Unseen error"), QStringLiteral("no dialog service is lent"));
        process->notifyWarnMessage(QStringLiteral("Unseen warning"), QStringLiteral("no dialog service is lent"));
        process->notifyInfoMessage(QStringLiteral("Unseen info"), QStringLiteral("no dialog service is lent"));
    }
    EXPECT_TRUE(context->messages.isEmpty());

    // Lent again, the dialog service hears the next one.
    process->notifyErrorMessage(QStringLiteral("Seen error"), QStringLiteral("a dialog service is lent"));
    EXPECT_EQ(context->messages, QStringList{QStringLiteral("error:Seen error")});
}

// An asynchronous message is queued to its window: the dialog service hears it once the event loop turns, not before.
TEST(ImageProcessControlMessageTest, AnAsyncMessageReachesTheDialogServiceOnceTheEventLoopTurns)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = flatTarget(context->engineSession(), 0.25f);
    ProcessHandle process = openProcess(*target, TestProcessOptions{});
    ASSERT_TRUE(settleProcess(*process));
    context->messages.clear();
    context->errorTexts.clear();

    process->notifyErrorMessageAsync(QStringLiteral("Queued error"), QStringLiteral("error text"));
    process->notifyWarnMessageAsync(QStringLiteral("Queued warning"), QStringLiteral("warning text"));
    process->notifyInfoMessageAsync(QStringLiteral("Queued info"), QStringLiteral("info text"));
    EXPECT_TRUE(context->messages.isEmpty()) << "nothing is shown before the event loop turns";

    ASSERT_TRUE(pumpUntil([&context]() { return context->messages.size() >= 3; }));
    EXPECT_EQ(context->messages,
              (QStringList{QStringLiteral("error:Queued error"), QStringLiteral("warning:Queued warning"),
                           QStringLiteral("info:Queued info")}));
    EXPECT_EQ(context->errorTexts, QStringList{QStringLiteral("error text")});
}

// An asynchronous message is queued to its window, so one whose window is deleted first is dropped with it.
TEST(ImageProcessControlMessageTest, AnAsyncMessageToAWindowDeletedFirstIsDropped)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = flatTarget(context->engineSession(), 0.25f);
    ProcessHandle process = openProcess(*target, TestProcessOptions{});
    ASSERT_TRUE(settleProcess(*process));
    context->messages.clear();

    process->notifyErrorMessageAsync(QStringLiteral("Dropped error"), QStringLiteral("never shown"));
    process->notifyWarnMessageAsync(QStringLiteral("Dropped warning"), QStringLiteral("never shown"));
    process->notifyInfoMessageAsync(QStringLiteral("Dropped info"), QStringLiteral("never shown"));
    // The window goes before the event loop turns.
    delete process.release();

    pumpQuietly();
    EXPECT_FALSE(context->messages.contains(QStringLiteral("error:Dropped error")));
    EXPECT_FALSE(context->messages.contains(QStringLiteral("warning:Dropped warning")));
    EXPECT_FALSE(context->messages.contains(QStringLiteral("info:Dropped info")));
}

// A code event runs its function with its window, at once or once the event loop turns; an empty one does nothing.
TEST(ImageProcessControlEventTest, ACodeEventRunsItsFunctionWithItsWindowAndAnEmptyOneDoesNothing)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = flatTarget(context->engineSession(), 0.25f);
    std::vector<ImageProcessControl*> ranWith;
    const std::function<void(ImageProcessControl*)> record = [&ranWith](ImageProcessControl* window) {
        ranWith.push_back(window);
    };
    ProcessHandle process = openProcess(*target, TestProcessOptions{});
    ASSERT_TRUE(settleProcess(*process));
    ImageProcessControl* const window = process.get();

    process->notifyCustomProcessCodeEvent(record);
    EXPECT_EQ(ranWith, std::vector<ImageProcessControl*>{window});

    EXPECT_NO_THROW(process->notifyCustomProcessCodeEvent(std::function<void(ImageProcessControl*)>()));
    EXPECT_EQ(ranWith.size(), 1u);

    process->notifyCustomProcessCodeEventAsync(record);
    EXPECT_EQ(ranWith.size(), 1u) << "an asynchronous event waits for the event loop";
    ASSERT_TRUE(pumpUntil([&ranWith]() { return ranWith.size() == 2u; }));
    EXPECT_EQ(ranWith.back(), window);
}

// A window closed while its Apply runs cancels the run's progress, is hidden at once and closes when the run lands.
TEST(ImageProcessControlCloseTest, AWindowClosedWhileItsApplyRunsCancelsItsProgressAndClosesWhenTheRunLands)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = flatTarget(context->engineSession(), 0.25f);
    LaunchedProcess process(*target, TestProcessOptions{.progressDialog = true});
    // The body runs until its run is stopped, and nothing but the progress dialog's Cancel stops it here.
    process->setParams(TestParams{.body = TestBody::WaitForStop});
    process->show();

    process->applyProcess();
    ASSERT_TRUE(pumpUntil([&process]() { return process->runsOf(an::ui::ProcessRunKind::Apply) == 1; }));
    const QPointer<ProgressDialogControl> progress = process->lastProgress;
    ASSERT_FALSE(progress.isNull());
    EXPECT_FALSE(progress->cancelRequested());

    EXPECT_FALSE(process->close()) << "the window waits for its run; the GUI thread does not";
    EXPECT_TRUE(process->isHidden());
    ASSERT_FALSE(progress.isNull());
    EXPECT_TRUE(progress->cancelRequested());

    ASSERT_TRUE(pumpUntilGone(process)) << "the window closes once its stopped run has landed";
    EXPECT_TRUE(context->openProcesses.empty());
    ASSERT_TRUE(settleView(*target));
    EXPECT_NEAR(documentSample(target->document(), 0u, 0u), 0.25, 1e-6) << "the stopped Apply published nothing";
    EXPECT_EQ(context->errorCount(), 0);
}

// A window's title is its process's name and, in angle brackets, its target's, and it follows the target's name.
TEST(ImageProcessControlTitleTest, TheTitleNamesTheProcessAndItsTargetInAngleBrackets)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = flatTarget(context->engineSession(), 0.25f, "target");
    ProcessHandle process = openProcess(*target, TestProcessOptions{.preview = true});
    ASSERT_TRUE(settleProcess(*process));
    // The preview view is named after the window when it is made.
    ASSERT_NE(process->previewView(), nullptr);
    EXPECT_EQ(process->previewView()->getUniqueName(), "Test Scale <target>");

    target->setUniqueName("m42");
    EXPECT_EQ(process->windowTitle(), QStringLiteral("Test Scale <m42>"));
    EXPECT_EQ(process->previewView()->getUniqueName(), "Test Scale <m42>");
}
