// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The processes a person marked as favourites.

#include "AfterNightShellCoreExport.h"

#include "app/ProcessMenuState.h"

#include <QString>

#include <set>
#include <string>
#include <utility>
#include <vector>

namespace an::ui
{
class IGlobalSettingsProvider;
}

namespace an::ui::shell
{

/// The processes a person marked as favourites: a set of ids, stored in `interface/favoriteProcessIds`, one to a line.
/// A set has no order and no limit; it is listed by the names the registry gives. Used on the GUI thread.
class AFTERNIGHTSHELLCORE_API FavoriteProcessesModel
{
public:
    /// @param lookup how a process is found by its id; empty, the process registry's.
    explicit FavoriteProcessesModel(an::ui::ProcessDescriptorLookup lookup = {});

    /// The settings the set is stored in; null, the set is kept in memory and reads empty.
    void setSettings(an::ui::IGlobalSettingsProvider* settings);

    [[nodiscard]] const std::set<std::string>& ids() const noexcept;

    /// The favourites the registry holds, each as its name and its id, in the alphabetical order of the names, case
    /// aside.
    [[nodiscard]] std::vector<std::pair<QString, std::string>> entries() const;

    /// Replaces the set with the stored one; empty with no settings.
    void load();

    /// Stores the set.
    void save() const;

    /// Replaces the set and stores it.
    void setIds(std::set<std::string> ids);

    /// Drops the ids the registry no longer holds and stores what is left when any was dropped.
    /// @return whether any was dropped.
    bool pruneMissing();

    /// Empties the set and stores it empty.
    void clear();

private:
    an::ui::ProcessDescriptorLookup m_lookup;
    an::ui::IGlobalSettingsProvider* m_settings = nullptr;
    std::set<std::string> m_ids;
};

} // namespace an::ui::shell
