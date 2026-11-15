// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The process windows that are open.

#include "AfterNightShellCoreExport.h"

#include <cstddef>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <vector>

class TopLevelProcessBaseControl;

namespace an::ui::shell
{

/// The process windows that are open: each registers itself as it is made and forgets itself as it goes.
///
/// The registry holds no window: a process window deletes itself when it closes, and the registry only knows it is
/// there. It is used from any thread; one mutex orders every access, and `forEach` visits under it.
///
/// Its observers are told of each window held and forgotten, on the thread that registered it - the GUI thread, where
/// process windows are made and destroyed - after the registry's lock is let go. A window is told as it registers, from
/// the base of its constructor: an observer reads what the window states then - its name, its descriptor - and nothing
/// a derived window adds.
class AFTERNIGHTSHELLCORE_API OpenProcessRegistry
{
public:
    /// Told of a window held (`added`) or forgotten.
    using Observer = std::function<void(TopLevelProcessBaseControl* window, bool added)>;
    /// Holds a window; a null one, or one held already, changes nothing.
    void add(TopLevelProcessBaseControl* window);

    /// Forgets a window; one it does not hold changes nothing.
    void remove(TopLevelProcessBaseControl* window);

    [[nodiscard]] bool contains(const TopLevelProcessBaseControl* window) const;
    [[nodiscard]] std::size_t size() const;

    /// The windows held now, in no particular order.
    [[nodiscard]] std::vector<TopLevelProcessBaseControl*> snapshot() const;

    /// Calls `visit` with each window held, under the registry's lock: `visit` may not register or forget one.
    void forEach(const std::function<void(TopLevelProcessBaseControl*)>& visit) const;

    /// Adds an observer, told of every window held and forgotten from now on; the token takes it out again.
    std::size_t subscribe(Observer observer);
    void unsubscribe(std::size_t token);

private:
    void tell(TopLevelProcessBaseControl* window, bool added);

    mutable std::mutex m_mutex;
    std::set<TopLevelProcessBaseControl*> m_windows;
    std::map<std::size_t, Observer> m_observers;
    std::size_t m_nextToken = 1;
};

} // namespace an::ui::shell
