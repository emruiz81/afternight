// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The menus of a view: the one it shows on a right click, and the one of its tab.
///
/// The view's menu is 1.1's entry for entry: the menus it shares with the menu bar, which the shell holds and names by
/// their ids; Current Window, whose entries act through `DockLayoutOperations` on the views that share the view's
/// window; and Show In File Browser. The tab's menu acts on the tab's view: it makes it the active view, so the
/// catalogue's file actions it offers act on it, and closes its tab group or floats it.

#include "AfterNightShellCoreExport.h"

#include <array>
#include <functional>
#include <span>
#include <string_view>

class ImageViewControl;
class QMenu;

namespace an::ui::shell
{

class ApplicationCore;

class AFTERNIGHTSHELLCORE_API ViewMenuBuilder
{
public:
    /// The shell's menu of an id; null when it has none, and the entry is left out.
    using MenuLookup = std::function<QMenu*(std::string_view menuId)>;

    /// The menus of the menu bar the view's menu offers, in its order.
    static constexpr std::array<std::string_view, 9> kSharedMenus{
        "menuActions",  "menuProcess",     "menuConvert_Format", "menuExtract_Channel",
        "menuGeometry", "menuInformation", "menuMask",           "menuSequence",
        "menuView"};

    explicit ViewMenuBuilder(ApplicationCore& core);

    /// Fills `menu` with the view's menu: the shared menus, Current Window - Group All Images and Split All Images,
    /// withdrawn while the view is maximized, and Fit All Images to Window - a separator and Show In File Browser.
    void buildViewMenu(QMenu& menu, ImageViewControl& view, const MenuLookup& menus) const;

    /// Fills `menu` with the menu of the view's tab and makes the view the active one: Close, Close All Others, Close
    /// Tab Group; Float; Duplicate, Rename, Save File...; Show In File Browser.
    void buildTabMenu(QMenu& menu, ImageViewControl& view) const;

private:
    ApplicationCore& m_core;
};

} // namespace an::ui::shell
