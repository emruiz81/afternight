// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "services/OpenProcessRegistry.h"

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
    const std::lock_guard lock(m_mutex);
    m_windows.insert(window);
}

void OpenProcessRegistry::remove(TopLevelProcessBaseControl* window)
{
    const std::lock_guard lock(m_mutex);
    m_windows.erase(window);
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

} // namespace an::ui::shell
