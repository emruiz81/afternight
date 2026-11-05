// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// An application listener hears the view lifecycle events of the bus it subscribed to, from its construction until
/// its destruction, and nothing after: the bus here dispatches as the application's does.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "app/AfterNightAppListener.h"
#include "controls/IAppContext.h"

namespace
{

/// An event bus that keeps its subscribers and hands each view lifecycle event to every one of them, in order of
/// subscription, as the application's does.
class TestEventBus final : public an::ui::IAppEventBus
{
public:
    void subscribeListener(AfterNightAppListener* listener) override
    {
        m_listeners.push_back(listener);
    }

    void unsubscribeListener(AfterNightAppListener* listener) override
    {
        m_listeners.erase(std::remove(m_listeners.begin(), m_listeners.end(), listener), m_listeners.end());
    }

    void postAfterNightEvent(AfterNightEvent*, int) override {}
    void notifyProcessOpened(const std::string&, const ImageViewControl*) override {}
    void notifyProcessExecuted(const std::string&, const ImageViewControl*) override {}

    void notifyViewControlCreated(const ImageViewControl* view)
    {
        for (AfterNightAppListener* listener : m_listeners)
        {
            listener->onViewControlCreated(view);
        }
    }

    void notifyViewControlRenamed(const std::string& newName, const std::string& oldName, const ImageViewControl* view)
    {
        for (AfterNightAppListener* listener : m_listeners)
        {
            listener->onViewControlRenamed(newName, oldName, view);
        }
    }

    void notifyViewControlDestroyed(const ImageViewControl* view)
    {
        for (AfterNightAppListener* listener : m_listeners)
        {
            listener->onViewControlDestroyed(view);
        }
    }

    [[nodiscard]] std::size_t subscribers() const noexcept
    {
        return m_listeners.size();
    }

private:
    std::vector<AfterNightAppListener*> m_listeners;
};

struct LifecycleEventRecorder
{
    int createdCount = 0;
    int renamedCount = 0;
    int destroyedCount = 0;

    const ImageViewControl* lastCreated = nullptr;
    const ImageViewControl* lastRenamed = nullptr;
    const ImageViewControl* lastDestroyed = nullptr;

    std::string lastOldName;
    std::string lastNewName;
};

class RecordingListener final : public AfterNightAppListener
{
public:
    RecordingListener(an::ui::IAppEventBus* bus, LifecycleEventRecorder* recorder)
        : AfterNightAppListener(bus), m_recorder(recorder)
    {
    }

    void onViewControlCreated(const ImageViewControl* ctrl) override
    {
        ++m_recorder->createdCount;
        m_recorder->lastCreated = ctrl;
    }

    void onViewControlDestroyed(const ImageViewControl* ctrl) override
    {
        ++m_recorder->destroyedCount;
        m_recorder->lastDestroyed = ctrl;
    }

    void onViewControlRenamed(const std::string& newName, const std::string& oldName,
                              const ImageViewControl* ctrl) override
    {
        ++m_recorder->renamedCount;
        m_recorder->lastRenamed = ctrl;
        m_recorder->lastOldName = oldName;
        m_recorder->lastNewName = newName;
    }

private:
    LifecycleEventRecorder* m_recorder = nullptr;
};

} // namespace

TEST(ImageViewControlListenerNotification, NotifiesCreateRenameAndDestroy)
{
    TestEventBus bus;
    LifecycleEventRecorder recorder;
    RecordingListener listener(&bus, &recorder);
    ASSERT_EQ(bus.subscribers(), 1u) << "a listener subscribes to the bus it is made with";

    const auto* viewControl = reinterpret_cast<const ImageViewControl*>(static_cast<std::uintptr_t>(0x1234));

    bus.notifyViewControlCreated(viewControl);
    bus.notifyViewControlRenamed("Light_002", "Light_001", viewControl);
    bus.notifyViewControlDestroyed(viewControl);

    EXPECT_EQ(recorder.createdCount, 1);
    EXPECT_EQ(recorder.renamedCount, 1);
    EXPECT_EQ(recorder.destroyedCount, 1);

    EXPECT_EQ(recorder.lastCreated, viewControl);
    EXPECT_EQ(recorder.lastRenamed, viewControl);
    EXPECT_EQ(recorder.lastDestroyed, viewControl);
    EXPECT_EQ(recorder.lastOldName, "Light_001");
    EXPECT_EQ(recorder.lastNewName, "Light_002");
}

TEST(ImageViewControlListenerNotification, UnsubscribedListenerStopsReceivingEvents)
{
    TestEventBus bus;
    LifecycleEventRecorder persistentRecorder;
    RecordingListener persistentListener(&bus, &persistentRecorder);

    LifecycleEventRecorder transientRecorder;
    {
        RecordingListener transientListener(&bus, &transientRecorder);
        const auto* firstControl = reinterpret_cast<const ImageViewControl*>(static_cast<std::uintptr_t>(0x1111));
        bus.notifyViewControlCreated(firstControl);
    }
    EXPECT_EQ(bus.subscribers(), 1u) << "a listener unsubscribes when it is destroyed";

    const auto* secondControl = reinterpret_cast<const ImageViewControl*>(static_cast<std::uintptr_t>(0x2222));
    bus.notifyViewControlDestroyed(secondControl);

    EXPECT_EQ(persistentRecorder.createdCount, 1);
    EXPECT_EQ(persistentRecorder.destroyedCount, 1);
    EXPECT_EQ(transientRecorder.createdCount, 1);
    EXPECT_EQ(transientRecorder.destroyedCount, 0);
}
