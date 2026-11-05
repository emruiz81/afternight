// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The application's events and listeners through the event bus: an event scheduled from any thread runs on the GUI
/// thread through the installed context's bus and says when it has finished, even when it throws; a listener subscribes
/// itself when it is made and unsubscribes when it is destroyed; and process notifications reach the bus as stated.

#include "ControlsServicesTestSupport.h"

#include "app/AfterNightAppListener.h"
#include "app/AfterNightEvent.h"
#include "controls/IAppContext.h"

#include <QCoreApplication>
#include <QThread>

#include <gtest/gtest.h>

#include <atomic>
#include <functional>
#include <memory>
#include <stdexcept>
#include <thread>

namespace
{

using an::tests::services::InstalledContext;
using an::tests::services::openContext;
using an::tests::services::pumpUntil;
using an::tests::services::TestAppContext;

/// An event that runs a function and remembers the thread it ran on.
class FunctionEvent final : public AfterNightEvent
{
public:
    FunctionEvent(std::atomic_bool* finished, std::function<void()> body, std::atomic<QThread*>* ranOn)
        : AfterNightEvent(finished), m_body(std::move(body)), m_ranOn(ranOn)
    {
    }

protected:
    void runImpl() override
    {
        if (m_ranOn != nullptr)
        {
            m_ranOn->store(QThread::currentThread());
        }
        m_body();
    }

private:
    std::function<void()> m_body;
    std::atomic<QThread*>* m_ranOn;
};

TEST(AppEventsTest, AnEventScheduledFromAWorkerRunsOnTheGuiThroughTheBus)
{
    std::unique_ptr<TestAppContext> context = openContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    std::atomic_bool finished{false};
    std::atomic<QThread*> ranOn{nullptr};
    std::atomic_int runs{0};

    std::thread worker([&]() { (new FunctionEvent(&finished, [&runs]() { ++runs; }, &ranOn))->schedule(); });
    worker.join();

    ASSERT_TRUE(pumpUntil([&]() { return finished.load(); })) << "the scheduled event never ran";
    EXPECT_EQ(runs.load(), 1);
    EXPECT_EQ(ranOn.load(), QCoreApplication::instance()->thread());
    EXPECT_EQ(context->eventsPosted, 1);
}

TEST(AppEventsTest, AnEventSaysItFinishedWhenItsBodyThrows)
{
    std::atomic_bool finished{false};
    FunctionEvent event(&finished, []() { throw std::runtime_error("the body failed"); }, nullptr);
    EXPECT_THROW(event.run(), std::runtime_error);
    EXPECT_TRUE(finished.load()) << "a waiter on a throwing event would never be released";
}

TEST(AppEventsTest, AnEventIsUnfinishedWhileItsBodyRuns)
{
    std::atomic_bool finished{true};
    bool seenWhileRunning = true;
    FunctionEvent event(&finished, [&]() { seenWhileRunning = finished.load(); }, nullptr);
    event.run();
    EXPECT_FALSE(seenWhileRunning);
    EXPECT_TRUE(finished.load());
}

TEST(AppEventsTest, AListenerSubscribesWhenMadeAndUnsubscribesWhenDestroyed)
{
    std::unique_ptr<TestAppContext> context = openContext();
    ASSERT_NE(context, nullptr);
    an::ui::IAppEventBus* bus = context->getAppEventBus();
    {
        AfterNightAppListener first(bus);
        AfterNightAppListener second(bus);
        ASSERT_EQ(context->listeners.size(), 2u);
        EXPECT_EQ(context->listeners[0], &first);
        EXPECT_EQ(context->listeners[1], &second);
    }
    EXPECT_TRUE(context->listeners.empty()) << "a destroyed listener is still subscribed";

    const AfterNightAppListener detached(nullptr);
    EXPECT_TRUE(context->listeners.empty());
}

TEST(AppEventsTest, ProcessNotificationsReachTheBusAsStated)
{
    std::unique_ptr<TestAppContext> context = openContext();
    ASSERT_NE(context, nullptr);
    an::ui::IAppEventBus* bus = context->getAppEventBus();
    bus->notifyProcessOpened("Curves", nullptr);
    bus->notifyProcessExecuted("Curves", nullptr);
    ASSERT_EQ(context->opened.size(), 1u);
    ASSERT_EQ(context->executed.size(), 1u);
    EXPECT_EQ(context->opened[0].processName, "Curves");
    EXPECT_EQ(context->executed[0].processName, "Curves");
}

} // namespace
