// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "services/OpenProcessRegistry.h"

#include <utility>

namespace an::ui::shell
{

void OpenProcessRegistry::add(TopLevelProcessBaseControl* window)
{
    // Process windows set Qt::WA_DeleteOnClose in their constructors; Qt deletes them when closed. The set holds raw
    // (non-owning) tracking pointers only.
    if (window == nullptr)
    {
        return;
    }
    bool added = false;
    {
        const std::lock_guard lock(m_mutex);
        added = m_windows.insert(window).second;
    }
    if (added)
    {
        tell(window, true);
    }
}

void OpenProcessRegistry::remove(TopLevelProcessBaseControl* window)
{
    bool removed = false;
    {
        const std::lock_guard lock(m_mutex);
        removed = m_windows.erase(window) > 0u;
    }
    if (removed)
    {
        tell(window, false);
    }
}

bool OpenProcessRegistry::contains(const TopLevelProcessBaseControl* window) const
{
    const std::lock_guard lock(m_mutex);
    return m_windows.contains(const_cast<TopLevelProcessBaseControl*>(window));
}

std::size_t OpenProcessRegistry::size() const
{
    const std::lock_guard lock(m_mutex);
    return m_windows.size();
}

std::vector<TopLevelProcessBaseControl*> OpenProcessRegistry::snapshot() const
{
    const std::lock_guard lock(m_mutex);
    return {m_windows.begin(), m_windows.end()};
}

void OpenProcessRegistry::forEach(const std::function<void(TopLevelProcessBaseControl*)>& visit) const
{
    if (!visit)
    {
        return;
    }
    const std::lock_guard lock(m_mutex);
    for (TopLevelProcessBaseControl* window : m_windows)
    {
        visit(window);
    }
}

std::size_t OpenProcessRegistry::subscribe(Observer observer)
{
    const std::lock_guard lock(m_mutex);
    const std::size_t token = m_nextToken++;
    m_observers.emplace(token, std::move(observer));
    return token;
}

void OpenProcessRegistry::unsubscribe(const std::size_t token)
{
    const std::lock_guard lock(m_mutex);
    m_observers.erase(token);
}

void OpenProcessRegistry::tell(TopLevelProcessBaseControl* window, const bool added)
{
    // Told outside the lock: an observer may read the registry, or leave it, while it is told.
    std::map<std::size_t, Observer> told;
    {
        const std::lock_guard lock(m_mutex);
        told = m_observers;
    }
    for (const auto& [token, observer] : told)
    {
        bool stillThere = false;
        {
            const std::lock_guard lock(m_mutex);
            stillThere = m_observers.contains(token);
        }
        if (observer && stillThere)
        {
            observer(window, added);
        }
    }
}

} // namespace an::ui::shell
