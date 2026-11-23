// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The pages of the Settings dialog a shell opens, in their order: General, Projects, Interface, Processing, Raw
/// Images, Plate Solving, Catalogs & Data, Export, External Tools, Logging - what a person changes most first, Projects
/// after General.

#include "AfterNightControlsExport.h"
#include "controls/IAppContext.h"

#include <functional>
#include <vector>

class GlobalSettingsPersistence;
class SettingsDialog;

namespace an::ui
{

/// What a shell tells its Settings dialog.
struct SettingsPagesOptions
{
    /// The shells a person can choose among, for the Interface page's Shell row; shown when there are two or more.
    std::vector<ShellOption> shells;
    /// Keeps the shell's menu bar shown, or lets it show on Alt again, at once. A shell whose menu bar shows on Alt
    /// sets it, and the Interface page then offers "Always show the menu bar".
    std::function<void(bool)> keepMenuBarShown;
};

/// Adds the Settings dialog's pages, over a settings store, in their order.
AFTERNIGHTCONTROLS_API void addSettingsPages(SettingsDialog& dialog, GlobalSettingsPersistence* settings,
                                             const SettingsPagesOptions& options);

} // namespace an::ui
