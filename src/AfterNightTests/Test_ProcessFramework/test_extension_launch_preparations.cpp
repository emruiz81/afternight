// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The launches of extensions whose environment is prepared first: one preparation per extension at a time, recorded by
/// the extension until its work has returned rather than by its progress window; a launch while it runs raises its
/// window, a launch while it is being stopped starts once it has stopped, the application's closing stops every one and
/// starts none, and a preparation that throws is completed with its reason. The work is a stand-in a case lets return,
/// so no package or environment is made.

#include "Test_ProcessFramework/ProcessFrameworkSuiteSupport.h"

#include "app/ExtensionLaunchPreparations.h"
#include "controls/ProgressDialogControl.h"

#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QPointer>
#include <QString>
#include <QThreadPool>
#include <QWidget>

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{

using an::tests::processes::pumpQuietly;
using an::tests::processes::pumpUntil;
using an::ui::extensions::ExtensionLaunchPreparationOutcome;
using an::ui::extensions::ExtensionLaunchPreparations;
using an::ui::extensions::ExtensionOperationCancellationToken;

constexpr auto kExtension = "an.case.launchpreparation";
constexpr auto kDisplayName = "Launch Preparation Case";

/// The stand-in for a preparation's work: each run counts itself and waits until the case releases it or its
/// preparation is stopped, then returns prepared only when released.
struct WorkGate
{
    std::mutex mutex;
    std::condition_variable changed;
    int started = 0;
    int returned = 0;
    bool released = false;
    bool throws = false;

    [[nodiscard]] int startedCount()
    {
        const std::lock_guard lock(mutex);
        return started;
    }

    [[nodiscard]] int returnedCount()
    {
        const std::lock_guard lock(mutex);
        return returned;
    }

    void release()
    {
        {
            const std::lock_guard lock(mutex);
            released = true;
        }
        changed.notify_all();
    }
};

[[nodiscard]] ExtensionLaunchPreparations::Work gatedWork(const std::shared_ptr<WorkGate>& gate)
{
    return [gate](const std::string&, const ExtensionLaunchPreparations::Progress&,
                  const ExtensionOperationCancellationToken& cancellation) {
        std::unique_lock lock(gate->mutex);
        ++gate->started;
        gate->changed.notify_all();
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (!gate->released && !cancellation.isCancellationRequested() && std::chrono::steady_clock::now() < until)
        {
            gate->changed.wait_for(lock, std::chrono::milliseconds(2));
        }
        ++gate->returned;
        if (gate->throws)
        {
            throw std::runtime_error("The interpreter the environment is made from is missing.");
        }
        ExtensionLaunchPreparationOutcome outcome;
        outcome.prepared = gate->released && !cancellation.isCancellationRequested();
        return outcome;
    };
}

/// A launch a preparation completed, as the application completes one.
struct CompletedLaunch
{
    std::string processId;
    std::string extensionId;
    bool prepared = false;
    std::string diagnostic;
};

/// The progress windows of the cases' preparations that are shown now.
[[nodiscard]] int shownPreparationWindows()
{
    int shown = 0;
    for (QWidget* widget : QApplication::topLevelWidgets())
    {
        const auto* window = dynamic_cast<ProgressDialogControl*>(widget);
        if (window != nullptr && window->isVisible() && window->windowTitle().contains(QString::fromUtf8(kDisplayName)))
        {
            ++shown;
        }
    }
    return shown;
}

class ExtensionLaunchPreparationsTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_preparations = std::make_unique<ExtensionLaunchPreparations>(
            nullptr, gatedWork(m_gate),
            [this](const std::string& processId, const std::string& extensionId,
                   ExtensionLaunchPreparationOutcome outcome) {
                m_completed.push_back({processId, extensionId, outcome.prepared, outcome.diagnostic});
            });
    }

    void TearDown() override
    {
        m_gate->release();
        m_preparations.reset();
        QThreadPool::globalInstance()->waitForDone();
        QCoreApplication::processEvents();
        for (QWidget* widget : QApplication::topLevelWidgets())
        {
            if (auto* window = dynamic_cast<ProgressDialogControl*>(widget);
                window != nullptr && window->windowTitle().contains(QString::fromUtf8(kDisplayName)))
            {
                window->close();
            }
        }
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    std::shared_ptr<WorkGate> m_gate = std::make_shared<WorkGate>();
    std::vector<CompletedLaunch> m_completed;
    std::unique_ptr<ExtensionLaunchPreparations> m_preparations;
};

TEST_F(ExtensionLaunchPreparationsTest, ALaunchWhileItsExtensionIsPreparedRaisesItsWindowAndPreparesItOnce)
{
    m_preparations->launch("an.case.process", kExtension, kDisplayName);
    ASSERT_TRUE(pumpUntil([this]() { return m_gate->startedCount() == 1; }));
    const QPointer<ProgressDialogControl> window = m_preparations->windowOf(kExtension);
    ASSERT_FALSE(window.isNull());
    EXPECT_EQ(shownPreparationWindows(), 1);

    m_preparations->launch("an.case.process", kExtension, kDisplayName);
    pumpQuietly();
    EXPECT_EQ(m_gate->startedCount(), 1) << "a second launch prepared the extension beside the first";
    EXPECT_EQ(m_preparations->windowOf(kExtension), window.data());
    EXPECT_EQ(shownPreparationWindows(), 1);

    m_gate->release();
    ASSERT_TRUE(pumpUntil([this]() { return m_completed.size() == 1; }));
    EXPECT_EQ(m_completed.front().processId, "an.case.process");
    EXPECT_EQ(m_completed.front().extensionId, kExtension);
    EXPECT_TRUE(m_completed.front().prepared);
    EXPECT_FALSE(m_preparations->isPreparing(kExtension));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    EXPECT_TRUE(window.isNull()) << "the window outlived the preparation it showed";
}

TEST_F(ExtensionLaunchPreparationsTest, ALaunchAfterItsWindowClosedStartsOnceTheStoppedPreparationHasStopped)
{
    m_preparations->launch("an.case.first", kExtension, kDisplayName);
    ASSERT_TRUE(pumpUntil([this]() { return m_gate->startedCount() == 1; }));
    const QPointer<ProgressDialogControl> first = m_preparations->windowOf(kExtension);
    ASSERT_FALSE(first.isNull());

    // The window goes at once; its work stops only at its next check of the stop, and until its completion lands on
    // this thread the preparation is still the extension's.
    first->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    ASSERT_TRUE(first.isNull());
    EXPECT_TRUE(m_preparations->isPreparing(kExtension));

    m_preparations->launch("an.case.second", kExtension, kDisplayName);
    m_preparations->launch("an.case.third", kExtension, kDisplayName);
    EXPECT_EQ(m_gate->startedCount(), 1) << "a launch prepared the extension beside the preparation being stopped";
    EXPECT_EQ(shownPreparationWindows(), 0) << "a launch opened a window beside the preparation being stopped";

    // Once the stopped preparation has completed, the launch asked for meanwhile starts, once.
    ASSERT_TRUE(pumpUntil([this]() { return m_gate->startedCount() == 2; }));
    pumpQuietly();
    EXPECT_EQ(m_gate->startedCount(), 2);
    EXPECT_TRUE(m_completed.empty()) << "the stopped preparation completed a launch";
    const QPointer<ProgressDialogControl> next = m_preparations->windowOf(kExtension);
    ASSERT_FALSE(next.isNull());
    EXPECT_EQ(shownPreparationWindows(), 1);

    // The record is the running preparation's: a launch now raises its window.
    m_preparations->launch("an.case.fourth", kExtension, kDisplayName);
    pumpQuietly();
    EXPECT_EQ(m_gate->startedCount(), 2);
    EXPECT_EQ(m_preparations->windowOf(kExtension), next.data());
    EXPECT_EQ(shownPreparationWindows(), 1);

    m_gate->release();
    ASSERT_TRUE(pumpUntil([this]() { return m_completed.size() == 1; }));
    EXPECT_EQ(m_completed.front().processId, "an.case.third") << "the launch asked for last is the one completed";
    EXPECT_TRUE(m_completed.front().prepared);
    EXPECT_FALSE(m_preparations->isPreparing(kExtension));
}

// The work may return before any stop reaches it, and its completion then waits for the GUI thread; the person may
// close the window and launch another process of the extension meanwhile. That launch is the one the completion
// completes, with the outcome the work returned: the first launch's window was closed and the second was asked for. It
// was dropped, and the first launched instead.
TEST_F(ExtensionLaunchPreparationsTest, ALaunchWhileAStoppedPreparationsCompletionWaitsIsTheOneCompleted)
{
    m_preparations->launch("an.case.first", kExtension, kDisplayName);
    ASSERT_TRUE(pumpUntil([this]() { return m_gate->startedCount() == 1; }));
    const QPointer<ProgressDialogControl> window = m_preparations->windowOf(kExtension);
    ASSERT_FALSE(window.isNull());

    // The work returns prepared; its completion waits for the GUI thread, which does not turn yet.
    m_gate->release();
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (m_gate->returnedCount() < 1 && std::chrono::steady_clock::now() < until)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_EQ(m_gate->returnedCount(), 1);
    window->close();
    m_preparations->launch("an.case.second", kExtension, kDisplayName);

    ASSERT_TRUE(pumpUntil([this]() { return !m_completed.empty(); }));
    pumpQuietly();
    ASSERT_EQ(m_completed.size(), 1u);
    EXPECT_EQ(m_completed.front().processId, "an.case.second")
        << "the launch asked for while the completion waited was dropped";
    EXPECT_TRUE(m_completed.front().prepared);
    EXPECT_FALSE(m_preparations->isPreparing(kExtension));
    EXPECT_EQ(m_gate->startedCount(), 1) << "the environment was prepared again";
}

// The work may return before the person closes the window; the close is still the launch's cancel. Nothing else was
// asked for meanwhile, so the completion completes no launch: the process window of a launch the person had just
// cancelled opened.
TEST_F(ExtensionLaunchPreparationsTest, AWindowClosedAfterItsWorkReturnedCompletesNoLaunch)
{
    m_preparations->launch("an.case.first", kExtension, kDisplayName);
    ASSERT_TRUE(pumpUntil([this]() { return m_gate->startedCount() == 1; }));
    const QPointer<ProgressDialogControl> window = m_preparations->windowOf(kExtension);
    ASSERT_FALSE(window.isNull());

    // The work returns prepared; its completion waits for the GUI thread, which does not turn yet.
    m_gate->release();
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (m_gate->returnedCount() < 1 && std::chrono::steady_clock::now() < until)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_EQ(m_gate->returnedCount(), 1);
    window->close();

    ASSERT_TRUE(pumpUntil([this]() { return !m_preparations->isPreparing(kExtension); }));
    pumpQuietly();
    EXPECT_TRUE(m_completed.empty()) << "the launch whose window was closed was completed";

    // A launch asked for afterwards prepares the extension afresh.
    m_preparations->launch("an.case.second", kExtension, kDisplayName);
    ASSERT_TRUE(pumpUntil([this]() { return m_gate->startedCount() == 2; }));
    ASSERT_TRUE(pumpUntil([this]() { return m_completed.size() == 1; }));
    EXPECT_EQ(m_completed.front().processId, "an.case.second");
    EXPECT_TRUE(m_completed.front().prepared);
}

TEST_F(ExtensionLaunchPreparationsTest, ALaunchAfterItsCancelStartsOnceTheCancelledPreparationHasStopped)
{
    m_preparations->launch("an.case.first", kExtension, kDisplayName);
    ASSERT_TRUE(pumpUntil([this]() { return m_gate->startedCount() == 1; }));
    const QPointer<ProgressDialogControl> first = m_preparations->windowOf(kExtension);
    ASSERT_FALSE(first.isNull());

    // Cancel leaves the window shown, saying it is cancelling, until the work has stopped.
    first->requestCancel();
    m_preparations->launch("an.case.second", kExtension, kDisplayName);
    EXPECT_EQ(m_gate->startedCount(), 1);

    ASSERT_TRUE(pumpUntil([this]() { return m_gate->startedCount() == 2; }))
        << "the launch asked for while the preparation was cancelled was never started";
    EXPECT_TRUE(m_completed.empty()) << "the cancelled preparation completed a launch";
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    EXPECT_TRUE(first.isNull()) << "the cancelled preparation's window outlived it";
    EXPECT_EQ(shownPreparationWindows(), 1);

    m_gate->release();
    ASSERT_TRUE(pumpUntil([this]() { return m_completed.size() == 1; }));
    EXPECT_EQ(m_completed.front().processId, "an.case.second");
    EXPECT_TRUE(m_completed.front().prepared);
}

TEST_F(ExtensionLaunchPreparationsTest, TheApplicationsClosingStopsEveryPreparationAndStartsNoneAfterwards)
{
    m_preparations->launch("an.case.process", kExtension, kDisplayName);
    ASSERT_TRUE(pumpUntil([this]() { return m_gate->startedCount() == 1; }));

    m_preparations->cancelAll();
    ASSERT_TRUE(pumpUntil([this]() { return !m_preparations->isPreparing(kExtension); }));
    EXPECT_EQ(m_gate->returnedCount(), 1);
    EXPECT_TRUE(m_completed.empty()) << "a preparation the closing stopped completed its launch";
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    EXPECT_EQ(shownPreparationWindows(), 0);

    m_preparations->launch("an.case.process", kExtension, kDisplayName);
    m_preparations->launch("an.case.other", "an.case.otherextension", kDisplayName);
    pumpQuietly();
    EXPECT_EQ(m_gate->startedCount(), 1) << "a launch after the closing prepared an extension";
    EXPECT_FALSE(m_preparations->isPreparing(kExtension));
    EXPECT_FALSE(m_preparations->isPreparing("an.case.otherextension"));
    EXPECT_EQ(shownPreparationWindows(), 0);
}

TEST_F(ExtensionLaunchPreparationsTest, APreparationThatThrowsIsCompletedWithItsReasonAndPreparedAfresh)
{
    {
        const std::lock_guard lock(m_gate->mutex);
        m_gate->throws = true;
        m_gate->released = true;
    }
    m_preparations->launch("an.case.process", kExtension, kDisplayName);
    ASSERT_TRUE(pumpUntil([this]() { return m_completed.size() == 1; }));
    EXPECT_FALSE(m_completed.front().prepared);
    EXPECT_EQ(m_completed.front().diagnostic, "The interpreter the environment is made from is missing.");
    EXPECT_FALSE(m_preparations->isPreparing(kExtension));

    m_preparations->launch("an.case.process", kExtension, kDisplayName);
    ASSERT_TRUE(pumpUntil([this]() { return m_completed.size() == 2; }));
    EXPECT_EQ(m_gate->startedCount(), 2);
}

} // namespace
