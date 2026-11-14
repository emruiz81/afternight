// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The classic shell: the main window of AfterNight 1.1, made by the application through its descriptor.
///
/// The window is the shell's answers - where a view goes, what the status line shows, whether the Session Console is
/// on screen - and this is its place in the application's start and end: the shell is made over the core, builds the
/// window when it is built, and destroys it when it goes.

#include "AfterNightShellClassicExport.h"

#include "core/IShell.h"

class AfterNightApp;

namespace an::ui::shell
{

/// The classic window behind the shell contract.
class AFTERNIGHTSHELLCLASSIC_API ClassicShell final : public IShell
{
public:
    /// The shell id the classic shell is chosen by.
    static constexpr char kId[] = "classic";

    /// Makes the shell over the core; the window is made when the shell is built.
    explicit ClassicShell(ApplicationCore& core);
    /// Destroys the window, its views, history and extension sessions with it.
    ~ClassicShell() override;

    ClassicShell(const ClassicShell&) = delete;
    ClassicShell& operator=(const ClassicShell&) = delete;
    ClassicShell(ClassicShell&&) = delete;
    ClassicShell& operator=(ClassicShell&&) = delete;

    /// Makes the window over the core and readies it with the session the core was given
    /// (`ApplicationCore::setApplicationSession`), which the window opens from the limits the settings store states.
    /// Nothing is shown.
    /// @throws std::runtime_error when the core holds no session, or when the window cannot open the stores or the
    ///         session, as the application reports it.
    void build() override;

    /// The calls below are the window's, and are made once the shell is built.
    [[nodiscard]] QWidget* mainWindow() override;
    [[nodiscard]] IViewHost& viewHost() override;
    [[nodiscard]] IStatusDisplay& statusDisplay() override;
    [[nodiscard]] an::ui::IProcessPresenter* processPresenter() override;
    void setSessionConsoleVisible(bool visible) override;
    [[nodiscard]] bool isSessionConsoleVisible() const override;
    void refreshTheme() override;
    void showWithSavedGeometry() override;
    void saveGeometry() override;

    /// The window; null until the shell is built.
    [[nodiscard]] AfterNightApp* window() const noexcept;

private:
    ApplicationCore& m_core;
    AfterNightApp* m_window = nullptr;
};

/// The classic shell as the application registers it: `classic`, "Classic", made over the core.
[[nodiscard]] AFTERNIGHTSHELLCLASSIC_API ShellDescriptor classicShellDescriptor();

} // namespace an::ui::shell
