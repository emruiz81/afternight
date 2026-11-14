// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The application's menu bar as data, and the menus made from it.

#include "AfterNightShellCoreExport.h"

#include "app/ActionLookup.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

class QMenu;
class QMenuBar;

namespace an::ui::shell
{

/// What an entry of a menu is.
enum class MenuItemKind : std::uint8_t
{
    Action,
    Menu,
    Separator,
};

/// An entry of a menu: an action of the catalogue by its id, a menu of the tree by its id, or a separator.
struct MenuItem
{
    MenuItemKind kind = MenuItemKind::Separator;
    std::string_view id;
};

/// A menu of the tree: its id - the object name the menu carries - its title, and its entries.
struct MenuNode
{
    std::string_view id;
    const char* title = "";
    /// Whether its entries follow what a person did - the files and processes used last, the favourites - which the
    /// shell lists: it is made empty.
    bool varies = false;
    /// Whether it is offered when it is made.
    bool enabled = true;
    std::span<const MenuItem> items;
};

/// A menu bar declared as data: its menus in order, each menu's entries, and where each action sits. `menuBar()` is the
/// application's: File, Edit, View, Image, Process, Extensions, Window, Help and About, entry for entry the classic
/// window's. A menu is made of the catalogue's actions handed through a lookup; the menus it makes are the caller's.
class AFTERNIGHTSHELLCORE_API MenuTree
{
public:
    constexpr MenuTree(std::span<const MenuNode> menus, std::span<const std::string_view> topLevel) noexcept
        : m_menus(menus), m_topLevel(topLevel)
    {
    }

    /// The application's menu bar.
    [[nodiscard]] static const MenuTree& menuBar() noexcept;

    /// The ids of the menus of the bar, in order.
    [[nodiscard]] std::vector<std::string_view> topLevel() const;

    /// The menu of an id; null when the tree has none.
    [[nodiscard]] const MenuNode* menu(std::string_view id) const noexcept;

    /// The menus that hold an action, each by the titles a person reads down to it (`Image > Mask`), in the order the
    /// bar is read; none for an action no menu holds. A menu whose entries vary is not read.
    [[nodiscard]] std::vector<std::string> pathsOf(std::string_view actionId) const;

    /// Adds the bar's menus to `bar`, each made with its submenus; an action the lookup does not answer is left out.
    void buildMenuBar(QMenuBar& bar, const an::ui::ActionLookup& actions) const;

    /// Makes `menu` the menu of an id: its object name, title and state, its entries and submenus.
    /// @return false, changing nothing, when the tree has no menu of that id.
    bool buildMenu(QMenu& menu, std::string_view id, const an::ui::ActionLookup& actions) const;

private:
    void collectPaths(std::string_view menuId, const std::string& above, std::string_view actionId,
                      std::vector<std::string>& paths) const;

    std::span<const MenuNode> m_menus;
    std::span<const std::string_view> m_topLevel;
};

} // namespace an::ui::shell
