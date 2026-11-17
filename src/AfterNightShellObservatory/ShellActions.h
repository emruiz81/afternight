// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The Observatory shell's own actions: what this shell adds beside the catalogue, which the classic shell never shows.

#include "AfterNightShellObservatoryExport.h"

#include <QKeySequence>

#include <memory>
#include <string_view>
#include <vector>

class QAction;
class QActionGroup;
class QWidget;

namespace an::ui::shell
{
class ICommandProvider;
} // namespace an::ui::shell

namespace an::ui::observatory
{

/// The ids of the shell's own actions, in the catalogue's style: the object names they carry.
namespace ids
{
inline constexpr std::string_view kShowHome = "actionShow_Home";
inline constexpr std::string_view kCommandPalette = "actionCommand_Palette";
inline constexpr std::string_view kToggleReadout = "actionToggle_Readout";
inline constexpr std::string_view kToggleNight = "actionToggle_Night";
inline constexpr std::string_view kDensityCompact = "actionDensity_Compact";
inline constexpr std::string_view kDensityComfortable = "actionDensity_Comfortable";
inline constexpr std::string_view kNewWorkspace = "actionNew_Workspace";
inline constexpr std::string_view kRenameWorkspace = "actionRename_Workspace";
inline constexpr std::string_view kMoveWorkspaceToNewWindow = "actionMove_Workspace_To_New_Window";
inline constexpr std::string_view kCloseWorkspace = "actionClose_Workspace";
inline constexpr std::string_view kApplyTask = "actionApply_Task";
inline constexpr std::string_view kFloatTask = "actionFloat_Task";
inline constexpr std::string_view kAttachTask = "actionAttach_Task";
inline constexpr std::string_view kCloseTask = "actionClose_Task";
inline constexpr std::string_view kNewProject = "actionNew_Project";
inline constexpr std::string_view kOpenProject = "actionOpen_Project";
inline constexpr std::string_view kSwitchProject = "actionSwitch_Project";
inline constexpr std::string_view kCloseProject = "actionClose_Project";
inline constexpr std::string_view kProjectSettings = "actionProject_Settings";
} // namespace ids

/// One action of the shell's table.
struct ShellActionSpec
{
    std::string_view id;
    const char* text;
    QKeySequence shortcut;
    /// The product's icon of the action, by its file's name; none when empty.
    const char* iconKey;
    bool checkable;
    /// The menu of the menu bar it is in, which the palette finds it by.
    const char* menu;
};

/// The shell's own actions: Home, the Command Palette (`Ctrl+K`), the Readout, Night (`Ctrl+Shift+N`), the two
/// densities, which are exclusive, the workspace's: New Workspace, Rename Workspace..., Move Workspace to New Window
/// and Close Workspace, and the current task's: Apply (`Ctrl+Enter`, from either Enter key), which runs its primary
/// action - inside a page the page answers the key itself - and, in the Window menu, Float as Window, Attach and Close
/// Task; and the project's, in the File menu: New Project..., Open Project..., Switch Project..., Close Project and
/// Project Settings.... They are children of the window and its own, so their
/// shortcuts work wherever the focus is in it; the window connects what each does. The classic shell holds none of
/// them, so its records of the actions, the menus and the shortcuts stay as they are.
class AFTERNIGHTSHELLOBSERVATORY_API ShellActions final
{
public:
    /// The table: the View menu's in its order, the Window menu's, the Process menu's, then the File menu's.
    [[nodiscard]] static const std::vector<ShellActionSpec>& table();

    /// Makes the table's actions as children of `window`, and adds them to it.
    explicit ShellActions(QWidget& window);

    ShellActions(const ShellActions&) = delete;
    ShellActions& operator=(const ShellActions&) = delete;

    /// The action of an id; null for one the table does not hold.
    [[nodiscard]] QAction* action(std::string_view id) const;
    [[nodiscard]] const std::vector<QAction*>& all() const noexcept;
    /// The group that keeps one density checked.
    [[nodiscard]] QActionGroup* densities() const noexcept;

    /// What the command index offers of the shell's actions: each, under its text, its shortcut and its icon, found
    /// with the window's other views and by the menu it is in; an action that says what it is about (its
    /// `commandSubtitle` property) shows that under its text. It reads the actions as they are when the index
    /// refreshes, and offers none once they have gone.
    [[nodiscard]] std::unique_ptr<an::ui::shell::ICommandProvider> commandProvider() const;

private:
    std::vector<QAction*> m_actions;
    QActionGroup* m_densities = nullptr;
};

} // namespace an::ui::observatory
