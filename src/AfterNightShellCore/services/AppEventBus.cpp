// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "services/AppEventBus.h"

#include "app/AfterNightAppListener.h"

#include <algorithm>

namespace an::ui::shell
{

void AppEventBus::subscribe(AfterNightAppListener* listener)
{
    if (listener != nullptr && !isSubscribed(listener))
    {
        m_listeners.push_back(listener);
    }
}

void AppEventBus::unsubscribe(AfterNightAppListener* listener)
{
    const auto found = std::find(m_listeners.begin(), m_listeners.end(), listener);
    if (found != m_listeners.end())
    {
        m_listeners.erase(found);
    }
}

bool AppEventBus::isSubscribed(const AfterNightAppListener* listener) const
{
    return std::find(m_listeners.begin(), m_listeners.end(), listener) != m_listeners.end();
}

std::size_t AppEventBus::listenerCount() const noexcept
{
    return m_listeners.size();
}

template <typename Notification> void AppEventBus::notify(const Notification& notification)
{
    // The listeners as the notification began: a handler may subscribe or unsubscribe one, and a listener that went
    // meanwhile - whose object may be gone - is not told.
    const std::vector<AfterNightAppListener*> listeners = m_listeners;
    for (AfterNightAppListener* listener : listeners)
    {
        if (isSubscribed(listener))
        {
            notification(*listener);
        }
    }
}

void AppEventBus::notifyViewControlCreated(const ImageViewControl* view)
{
    notify([view](AfterNightAppListener& listener) { listener.onViewControlCreated(view); });
}

void AppEventBus::notifyActiveViewControlChanged(ImageViewControl* view)
{
    notify([view](AfterNightAppListener& listener) { listener.onActiveViewControlChanged(view); });
}

void AppEventBus::notifyViewControlDestroyed(const ImageViewControl* view)
{
    notify([view](AfterNightAppListener& listener) { listener.onViewControlDestroyed(view); });
}

void AppEventBus::notifyViewControlUpdated(const ImageViewControl* view)
{
    notify([view](AfterNightAppListener& listener) { listener.onViewControlUpdated(view); });
}

void AppEventBus::notifyViewControlRenamed(const std::string& newName, const std::string& oldName,
                                           const ImageViewControl* view)
{
    notify([&newName, &oldName, view](AfterNightAppListener& listener) {
        listener.onViewControlRenamed(newName, oldName, view);
    });
}

void AppEventBus::notifyProcessOpened(const std::string& processName, const ImageViewControl* target)
{
    notify([&processName, target](AfterNightAppListener& listener) { listener.onProcessOpened(processName, target); });
}

void AppEventBus::notifyProcessExecuted(const std::string& processName, const ImageViewControl* target)
{
    notify(
        [&processName, target](AfterNightAppListener& listener) { listener.onProcessExecuted(processName, target); });
}

void AppEventBus::notifyApplicationClose()
{
    notify([](AfterNightAppListener& listener) { listener.onApplicationClose(); });
}

} // namespace an::ui::shell
