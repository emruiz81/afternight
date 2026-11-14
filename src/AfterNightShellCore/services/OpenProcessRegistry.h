// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The process windows that are open.

#include "AfterNightShellCoreExport.h"

#include <cstddef>
#include <functional>
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
class AFTERNIGHTSHELLCORE_API OpenProcessRegistry
{
public:
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

private:
    mutable std::mutex m_mutex;
    std::set<TopLevelProcessBaseControl*> m_windows;
};

} // namespace an::ui::shell
