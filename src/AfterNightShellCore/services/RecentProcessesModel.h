// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The processes launched last.

#include "AfterNightShellCoreExport.h"

#include "app/ProcessMenuState.h"

#include <cstddef>
#include <deque>
#include <string>

namespace an::ui
{
class IGlobalSettingsProvider;
}

namespace an::ui::shell
{

/// The processes launched last, most recent first, each under the name it was launched by, with the setting they are
/// stored in and the limit the settings give.
///
/// The list is stored in `interface/recentProcessIds`, one id to a line, and its limit is the setting
/// `general/recentProcessesLimit`, ten when nothing usable is stored. A process's name is the one the registry gives
/// when the list is read. Used on the GUI thread.
class AFTERNIGHTSHELLCORE_API RecentProcessesModel
{
public:
    /// @param lookup how a process is found by its id; empty, the process registry's.
    explicit RecentProcessesModel(an::ui::ProcessDescriptorLookup lookup = {});

    /// The settings the list is stored in; null, the list is kept in memory and reads empty.
    void setSettings(an::ui::IGlobalSettingsProvider* settings);

    /// How many processes the list keeps.
    [[nodiscard]] std::size_t limit() const;

    [[nodiscard]] const std::deque<an::ui::RecentProcessEntryData>& entries() const noexcept;

    /// Replaces the list with the stored one, under the names the registry gives; empty with no settings.
    void load();

    /// Stores the list.
    void save() const;

    /// Drops the oldest entries past the limit.
    void enforceLimit();

    /// Puts a launch of `processId` first, once, under `displayName` (its id when empty), drops the oldest past the
    /// limit and stores the list.
    /// @return whether the list changed: false for an empty id.
    bool record(const std::string& processId, const std::string& displayName);

    /// Drops the processes the registry no longer holds.
    /// @return whether any was dropped.
    bool pruneMissing();

    /// Empties the list and stores it empty.
    void clear();

private:
    an::ui::ProcessDescriptorLookup m_lookup;
    an::ui::IGlobalSettingsProvider* m_settings = nullptr;
    std::deque<an::ui::RecentProcessEntryData> m_entries;
};

} // namespace an::ui::shell
