// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The dashboard's settings, which the classic dashboard and Home share: the format they are kept in, the quick actions
/// as the person arranged them, the recent files pinned to the top, the pinned featured file, the recent files' sort
/// and search, and the record of the last action. Each is read from and written to the application's global settings
/// through the application context, under the keys of `SettingsKeys.h`; with no context nothing is read or written.
///
/// GUI thread.

#include "AfterNightControlsExport.h"

#include <QSet>
#include <QString>
#include <QStringList>

#include <functional>

class QMenu;
class QObject;

namespace an::ui
{

/// Brings the dashboard's settings to the format this version reads: settings of another format - older, newer, or
/// stating none - are never read; each one stored is set aside under its own name with the format it states beside it,
/// and the dashboard starts from its defaults.
AFTERNIGHTCONTROLS_API void ensureDashboardSettingsFormat();

/// The recent files pinned to the top, normalized. Every `load` below reads the settings in their format
/// (`ensureDashboardSettingsFormat` first), so no part reads settings of another format, whichever part reads first.
[[nodiscard]] AFTERNIGHTCONTROLS_API QSet<QString> loadPinnedRecentFiles();

/// Keeps the recent files pinned to the top, in a stable order.
AFTERNIGHTCONTROLS_API void storePinnedRecentFiles(const QSet<QString>& pinned);

/// The pinned featured file, normalized; stored again in its normalized form when it was not. A file that is gone is
/// not looked for here.
[[nodiscard]] AFTERNIGHTCONTROLS_API QString loadPinnedFeaturedFile();

/// Keeps the pinned featured file; an empty path pins none.
AFTERNIGHTCONTROLS_API void storePinnedFeaturedFile(const QString& path);

/// The recent files' sort - `DashboardPreferences::RECENT_SORT_*` - stored again in its canonical form when it was not.
[[nodiscard]] AFTERNIGHTCONTROLS_API int loadRecentSortMode();

/// Keeps the recent files' sort.
AFTERNIGHTCONTROLS_API void storeRecentSortMode(int sortMode);

/// The recent files' search, trimmed; stored again trimmed when it was not.
[[nodiscard]] AFTERNIGHTCONTROLS_API QString loadRecentSearchText();

/// Keeps the recent files' search, trimmed.
AFTERNIGHTCONTROLS_API void storeRecentSearchText(const QString& text);

/// How Home shows the recent files - `DashboardPreferences::RECENT_ARRANGEMENT_*` - stored again in its canonical form
/// when it was not.
[[nodiscard]] AFTERNIGHTCONTROLS_API int loadRecentArrangement();

/// Keeps how Home shows the recent files.
AFTERNIGHTCONTROLS_API void storeRecentArrangement(int arrangement);

/// Records the last action taken from the dashboard or Home, by its words; blank words record nothing.
AFTERNIGHTCONTROLS_API void recordDashboardLastAction(const QString& action);

/// The quick actions as the person arranged them: every action of `DashboardPreferences::dashboardActionIds` once, in
/// the person's order, and the ones hidden - each in its canonical form, read from the settings and kept there - and
/// the menu that moves, hides, shows and resets them.
class AFTERNIGHTCONTROLS_API DashboardQuickActionLayout
{
public:
    /// Every action, in the canonical order, none hidden.
    DashboardQuickActionLayout();

    /// Reads the order and the hidden actions from the settings, in the settings' format; stores them again in their
    /// canonical form when they were not.
    void load();

    /// Keeps the order and the hidden actions in the settings, in their canonical form.
    void store() const;

    /// Every action, in the person's order.
    [[nodiscard]] const QStringList& order() const noexcept;

    /// The hidden actions.
    [[nodiscard]] const QSet<QString>& hidden() const noexcept;

    /// The actions a row shows - the primary ones not hidden - in order.
    [[nodiscard]] QStringList shownPrimary() const;

    /// The actions a More menu holds - the others not hidden - in order.
    [[nodiscard]] QStringList shownOverflow() const;

    /// Fills a menu with the customization: a submenu per action that moves it earlier or later and hides or shows it
    /// (the last shown action cannot be hidden), a submenu that shows a hidden action, and Reset. A choice stores the
    /// layout and then calls `changed`, on `receiver`'s thread while `receiver` lives.
    void populateCustomizationMenu(QMenu& menu, QObject* receiver, const std::function<void()>& changed);

    /// Adds the customization to a menu as its submenu "Customize quick actions", as the dashboard's More menu holds
    /// it.
    void addCustomizationMenu(QMenu& menu, QObject* receiver, const std::function<void()>& changed);

private:
    /// Puts the order and the hidden actions in their canonical form.
    void canonicalize();

    QStringList m_order;
    QSet<QString> m_hidden;
};

} // namespace an::ui
