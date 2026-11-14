// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The files opened and saved last.

#include "AfterNightShellCoreExport.h"

#include <QString>

#include <cstddef>
#include <deque>
#include <string>

namespace an::ui
{
class IGlobalSettingsProvider;
}

namespace an::ui::shell
{

/// The files opened and saved last, most recent first, with the file they are kept in and the limit the settings
/// give.
///
/// The list is kept in `recent_files` in the application's data folder, one path to a line in UTF-8, and its limit is
/// the setting `general/recentFilesLimit`, twenty when nothing usable is stored. Used on the GUI thread. The file
/// workflows add and remove entries through the list itself (`entriesForEdit`), as they did when the window held it,
/// and save it after each change.
class AFTERNIGHTSHELLCORE_API RecentFilesModel
{
public:
    /// The folder the list's file is in, and the settings its limit is read from; either may be empty or null, which
    /// keeps the list in memory or gives the default limit.
    void setStorage(QString folder, an::ui::IGlobalSettingsProvider* settings);

    /// How many files the list keeps.
    [[nodiscard]] std::size_t limit() const;

    [[nodiscard]] const std::deque<std::string>& entries() const noexcept;

    /// The list, for the units that change it in place; they call `save` after a change.
    [[nodiscard]] std::deque<std::string>& entriesForEdit() noexcept;

    /// Replaces the list with its file's, up to the limit: each path as the dashboard normalizes it, a repeated or
    /// blank line left out, and the file written again when one was. With no folder, the list is left as it is.
    void load();

    /// Writes the list into its file; with no folder, nothing.
    void save() const;

    /// Empties the list and its file.
    void clear();

private:
    QString m_folder;
    an::ui::IGlobalSettingsProvider* m_settings = nullptr;
    std::deque<std::string> m_entries;
};

} // namespace an::ui::shell
