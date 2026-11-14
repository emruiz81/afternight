// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The application's listeners and what they are told.

#include "AfterNightShellCoreExport.h"

#include <cstddef>
#include <string>
#include <vector>

class AfterNightAppListener;
class ImageViewControl;

namespace an::ui::shell
{

/// The listeners the application tells about its views, its processes and its close, in the order they subscribed.
///
/// Used on the GUI thread only. A listener subscribed twice is told once. A notification goes to the listeners
/// subscribed when it began that are still subscribed when their turn comes: one that unsubscribes - itself or another,
/// as a window closed by what it was told does - is not told after it went, and one that subscribes meanwhile is told
/// from the next notification on.
class AFTERNIGHTSHELLCORE_API AppEventBus
{
public:
    /// Adds a listener at the end of the order; a null one, or one already subscribed, changes nothing.
    void subscribe(AfterNightAppListener* listener);

    /// Removes a listener; one the bus does not hold changes nothing.
    void unsubscribe(AfterNightAppListener* listener);

    [[nodiscard]] bool isSubscribed(const AfterNightAppListener* listener) const;
    [[nodiscard]] std::size_t listenerCount() const noexcept;

    void notifyViewControlCreated(const ImageViewControl* view);
    void notifyActiveViewControlChanged(ImageViewControl* view);
    void notifyViewControlDestroyed(const ImageViewControl* view);
    void notifyViewControlUpdated(const ImageViewControl* view);
    void notifyViewControlRenamed(const std::string& newName, const std::string& oldName, const ImageViewControl* view);
    void notifyProcessOpened(const std::string& processName, const ImageViewControl* target);
    void notifyProcessExecuted(const std::string& processName, const ImageViewControl* target);
    void notifyApplicationClose();

private:
    template <typename Notification> void notify(const Notification& notification);

    std::vector<AfterNightAppListener*> m_listeners;
};

} // namespace an::ui::shell
