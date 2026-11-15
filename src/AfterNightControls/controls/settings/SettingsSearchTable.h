// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The Settings dialog's pages as a search reads them, without making a page: each page's title and search words, as
/// the page answers them, and its rows - every labelled field and every check box it shows, with the section it sits
/// in. The command palette finds a setting through it; the shell core's suite holds it to the pages themselves.

#include "AfterNightControlsExport.h"

#include <QString>
#include <QStringList>

#include <vector>

namespace an::ui
{

/// A row of a settings page: a field's label without its colon, or a check box's text.
struct SettingsSearchRow
{
    QString label;
    QString section; ///< The title of the group the row sits in; empty for none.

    [[nodiscard]] bool operator==(const SettingsSearchRow&) const = default;
};

/// A settings page as a search reads it.
struct SettingsSearchPage
{
    QString title; ///< As `SettingsPage::pageTitle()` answers it, which the dialog opens a page by.
    QStringList keywords; ///< As `SettingsPage::searchKeywords()` answers them.
    std::vector<SettingsSearchRow> rows; ///< In the order the page makes them.

    [[nodiscard]] bool operator==(const SettingsSearchPage&) const = default;
};

/// The pages of the Settings dialog, in the order it lists them.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::vector<SettingsSearchPage> settingsSearchPages();

} // namespace an::ui
