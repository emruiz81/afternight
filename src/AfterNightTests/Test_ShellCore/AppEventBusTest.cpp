// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The application's listeners: who is told, in what order, and what a listener that comes or goes while one is being
/// told changes.

#include "services/AppEventBus.h"

#include "app/AfterNightAppListener.h"

#include <gtest/gtest.h>

#include <functional>
#include <string>
#include <vector>

namespace
{

using an::ui::shell::AppEventBus;

/// A listener that writes "<name>:<what it was told>" into a log the case reads. It is made without a bus, so it
/// subscribes nowhere by itself: the case subscribes it.
class RecordingListener final : public AfterNightAppListener
{
public:
    RecordingListener(std::vector<std::string>& log, std::string name)
        : AfterNightAppListener(nullptr), m_log(log), m_name(std::move(name))
    {
    }

    /// Runs once, the next time the listener is told a view was created, before it records it.
    std::function<void()> onNextCreated;

    void onViewControlCreated(const ImageViewControl* /*ctrl*/) override
    {
        if (onNextCreated)
        {
            const std::function<void()> once = std::move(onNextCreated);
            onNextCreated = nullptr;
            once();
        }
        record("created");
    }

    void onViewControlDestroyed(const ImageViewControl* /*ctrl*/) override
    {
        record("destroyed");
    }

    void onViewControlUpdated(const ImageViewControl* /*ctrl*/) override
    {
        record("updated");
    }

    void onActiveViewControlChanged(ImageViewControl* /*ctrl*/) override
    {
        record("active");
    }

    void onViewControlRenamed(const std::string& newName, const std::string& oldName,
                              const ImageViewControl* /*ctrl*/) override
    {
        record("renamed " + oldName + "->" + newName);
    }

    void onProcessOpened(const std::string& processName, const ImageViewControl* /*targetCtrl*/) override
    {
        record("opened " + processName);
    }

    void onProcessExecuted(const std::string& processName, const ImageViewControl* /*targetCtrl*/) override
    {
        record("executed " + processName);
    }

    void onApplicationClose() override
    {
        record("close");
    }

private:
    void record(const std::string& what)
    {
        m_log.push_back(m_name + ":" + what);
    }

    std::vector<std::string>& m_log;
    std::string m_name;
};

using Log = std::vector<std::string>;

TEST(AppEventBus, TellsEachListenerInTheOrderItSubscribed)
{
    Log log;
    RecordingListener third(log, "c");
    RecordingListener first(log, "a");
    RecordingListener second(log, "b");
    AppEventBus bus;
    bus.subscribe(&first);
    bus.subscribe(&second);
    bus.subscribe(&third);

    bus.notifyApplicationClose();

    EXPECT_EQ(log, (Log{"a:close", "b:close", "c:close"}));
    EXPECT_EQ(bus.listenerCount(), 3u);
}

TEST(AppEventBus, EachNotificationReachesItsOwnHandler)
{
    Log log;
    RecordingListener listener(log, "a");
    AppEventBus bus;
    bus.subscribe(&listener);

    bus.notifyViewControlCreated(nullptr);
    bus.notifyActiveViewControlChanged(nullptr);
    bus.notifyViewControlUpdated(nullptr);
    bus.notifyViewControlRenamed("new", "old", nullptr);
    bus.notifyViewControlDestroyed(nullptr);
    bus.notifyProcessOpened("Curves", nullptr);
    bus.notifyProcessExecuted("Curves", nullptr);
    bus.notifyApplicationClose();

    EXPECT_EQ(log, (Log{"a:created", "a:active", "a:updated", "a:renamed old->new", "a:destroyed", "a:opened Curves",
                        "a:executed Curves", "a:close"}));
}

TEST(AppEventBus, ASecondSubscriptionOfAListenerIsNotASecondListener)
{
    Log log;
    RecordingListener listener(log, "a");
    AppEventBus bus;
    bus.subscribe(&listener);
    bus.subscribe(&listener);

    bus.notifyApplicationClose();

    EXPECT_EQ(log, (Log{"a:close"}));
    EXPECT_EQ(bus.listenerCount(), 1u);
}

TEST(AppEventBus, AnUnsubscribedListenerIsToldNothing)
{
    Log log;
    RecordingListener kept(log, "a");
    RecordingListener gone(log, "b");
    AppEventBus bus;
    bus.subscribe(&kept);
    bus.subscribe(&gone);
    bus.unsubscribe(&gone);
    // A listener the bus never held is no error.
    bus.unsubscribe(&gone);
    bus.subscribe(nullptr);

    bus.notifyApplicationClose();

    EXPECT_EQ(log, (Log{"a:close"}));
    EXPECT_FALSE(bus.isSubscribed(&gone));
    EXPECT_TRUE(bus.isSubscribed(&kept));
    EXPECT_EQ(bus.listenerCount(), 1u);
}

// A listener torn down by another one's handler - a window closed by what it was told - is not told after it went.
TEST(AppEventBus, AListenerUnsubscribedByAnotherDuringANotificationIsNotToldIt)
{
    Log log;
    RecordingListener first(log, "a");
    RecordingListener second(log, "b");
    RecordingListener third(log, "c");
    AppEventBus bus;
    bus.subscribe(&first);
    bus.subscribe(&second);
    bus.subscribe(&third);
    first.onNextCreated = [&bus, &second]() { bus.unsubscribe(&second); };

    bus.notifyViewControlCreated(nullptr);
    bus.notifyApplicationClose();

    EXPECT_EQ(log, (Log{"a:created", "c:created", "a:close", "c:close"}));
}

TEST(AppEventBus, AListenerThatUnsubscribesItselfDuringANotificationIsNotToldAgain)
{
    Log log;
    RecordingListener first(log, "a");
    RecordingListener second(log, "b");
    AppEventBus bus;
    bus.subscribe(&first);
    bus.subscribe(&second);
    first.onNextCreated = [&bus, &first]() { bus.unsubscribe(&first); };

    bus.notifyViewControlCreated(nullptr);
    bus.notifyApplicationClose();

    EXPECT_EQ(log, (Log{"a:created", "b:created", "b:close"}));
}

TEST(AppEventBus, AListenerSubscribedDuringANotificationIsToldFromTheNextOne)
{
    Log log;
    RecordingListener first(log, "a");
    RecordingListener late(log, "b");
    AppEventBus bus;
    bus.subscribe(&first);
    first.onNextCreated = [&bus, &late]() { bus.subscribe(&late); };

    bus.notifyViewControlCreated(nullptr);
    bus.notifyApplicationClose();

    EXPECT_EQ(log, (Log{"a:created", "a:close", "b:close"}));
}

} // namespace
