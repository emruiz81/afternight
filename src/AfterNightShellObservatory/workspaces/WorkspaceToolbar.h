// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The four layout icons of a workspace, in the Observatory top bar.

#include "AfterNightShellObservatoryExport.h"

#include "app/ActionLookup.h"

#include <QWidget>

#include <functional>

class QIcon;
class QToolButton;

namespace an::ui::observatory
{

/// Lock layout, Group All Images, Split All Images, a separator and Fit Images To Window, with their icons, tooltips
/// and behaviour, for the workspace on screen. Group, Split and Fit are the catalogue's own actions; Lock is a toggle
/// drawn as a button of its own - its icon and tooltip say what a click does, Lock while the layout is free and Unlock
/// while it is locked.
class AFTERNIGHTSHELLOBSERVATORY_API WorkspaceToolbar final : public QWidget
{
public:
    /// What the toolbar asks of the workspace it acts on.
    struct Dependencies
    {
        /// The catalogue's actions, by the object names they have.
        an::ui::ActionLookup actions;
        /// Whether the workspace's layout is locked.
        std::function<bool()> isLocked;
        /// Locks or unlocks the workspace's layout.
        std::function<void(bool locked)> setLocked;
        /// The application's icons, by name.
        std::function<QIcon(const QString& name)> icon;
    };

    explicit WorkspaceToolbar(Dependencies dependencies, QWidget* parent = nullptr);

    /// The lock's button, and the buttons of the catalogue's three actions.
    [[nodiscard]] QToolButton* lockButton() const noexcept;
    [[nodiscard]] QToolButton* groupButton() const noexcept;
    [[nodiscard]] QToolButton* splitButton() const noexcept;
    [[nodiscard]] QToolButton* fitButton() const noexcept;

    /// Shows the lock as the layout is now: its icon and its tooltip name what a click does.
    void refreshLock();

private:
    Dependencies m_dependencies;
    QToolButton* m_lock = nullptr;
    QToolButton* m_group = nullptr;
    QToolButton* m_split = nullptr;
    QToolButton* m_fit = nullptr;
};

} // namespace an::ui::observatory
