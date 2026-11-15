// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The Observatory window's menu bar: the application's menu tree, hidden until Alt is pressed.

#include "AfterNightShellObservatoryExport.h"

#include "app/ActionLookup.h"

#include <QObject>
#include <QPointer>

#include <string_view>

class QEvent;
class QMainWindow;
class QMenu;
class QMenuBar;
class QWidget;

namespace an::ui::observatory
{

/// The menu bar of the window, built from the shell core's menu tree over the catalogue's actions - File, Edit, View,
/// Image, Process, Extensions, Window, Help - and hidden unless the person keeps it shown
/// (`interface/alwaysShowMenuBar`). Pressing and releasing Alt alone shows it and gives it the focus, and Alt again
/// hides it; Alt and a menu's first letter opens that menu at once; with the focus on the bar, Down, Enter or Space
/// opens the first menu, a letter its menu and Esc hides the bar; and the bar hides again once its menus close and the
/// focus is elsewhere. The actions' shortcuts do not depend on it: the window holds every action itself.
class AFTERNIGHTSHELLOBSERVATORY_API MenuBarOnAlt final : public QObject
{
public:
    /// Builds the bar over `actions` and makes it `window`'s menu bar, shown or hidden as `alwaysShown` says; listens
    /// to the keys the application delivers to the window.
    MenuBarOnAlt(QMainWindow& window, const an::ui::ActionLookup& actions, bool alwaysShown);
    ~MenuBarOnAlt() override;

    MenuBarOnAlt(const MenuBarOnAlt&) = delete;
    MenuBarOnAlt& operator=(const MenuBarOnAlt&) = delete;
    MenuBarOnAlt(MenuBarOnAlt&&) = delete;
    MenuBarOnAlt& operator=(MenuBarOnAlt&&) = delete;

    [[nodiscard]] QMenuBar* bar() const noexcept;

    /// A menu of the tree by its object name ("menuFile", "menuOpen_Recent"); null when the bar holds none.
    [[nodiscard]] QMenu* menu(std::string_view id) const;

    /// Whether the bar is kept shown; changed, the bar shows or hides at once.
    [[nodiscard]] bool isAlwaysShown() const noexcept;
    void setAlwaysShown(bool alwaysShown);

    /// Whether the bar is on screen, kept or shown by Alt.
    [[nodiscard]] bool isShown() const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    [[nodiscard]] bool isForTheWindow(const QObject* watched) const;
    [[nodiscard]] QMenu* menuByLetter(QChar letter) const;
    void reveal();
    void conceal();
    void concealWhenLeft();
    void openMenu(QMenu* menu);

    QMainWindow& m_window;
    QPointer<QMenuBar> m_bar;
    QPointer<QWidget> m_focusBefore;
    bool m_alwaysShown = false;
    bool m_altArmed = false;
};

} // namespace an::ui::observatory
