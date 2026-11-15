// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The Observatory shell: AfterNight's window of places and tasks, made by the application through its descriptor.
///
/// The shell is the window's answers to the application - where a view goes, what the status line shows, whether the
/// Session Console is on screen - and its place in the application's start and end: it is made over a core that is
/// initialized and started, builds its window when it is built, and destroys it when it goes.

#include "AfterNightShellObservatoryExport.h"

#include "core/IShell.h"

namespace an::ui::observatory
{

class ObservatoryWindow;

/// The Observatory window behind the shell contract.
class AFTERNIGHTSHELLOBSERVATORY_API ObservatoryShell final : public an::ui::shell::IShell
{
public:
    /// The shell id the Observatory shell is chosen by.
    static constexpr char kId[] = "observatory";

    /// Makes the shell over the core; the window is made when the shell is built.
    explicit ObservatoryShell(an::ui::shell::ApplicationCore& core);
    /// Destroys the window, and its views, history and extension sessions with it.
    ~ObservatoryShell() override;

    ObservatoryShell(const ObservatoryShell&) = delete;
    ObservatoryShell& operator=(const ObservatoryShell&) = delete;
    ObservatoryShell(ObservatoryShell&&) = delete;
    ObservatoryShell& operator=(ObservatoryShell&&) = delete;

    /// Makes the window over the core and readies it. Nothing is shown.
    /// @throws std::runtime_error when the core is not initialized and started, or holds no open session, as the
    ///         application reports it.
    void build() override;

    /// The calls below are the window's, and are made once the shell is built.
    [[nodiscard]] QWidget* mainWindow() override;
    [[nodiscard]] an::ui::shell::IViewHost& viewHost() override;
    [[nodiscard]] an::ui::shell::IStatusDisplay& statusDisplay() override;
    /// The window's tasks (`TaskPresenter`): every process is a window of its own still, and each is a task in the
    /// rail. Null once the window is on its way out.
    [[nodiscard]] an::ui::IProcessPresenter* processPresenter() override;
    void setSessionConsoleVisible(bool visible) override;
    [[nodiscard]] bool isSessionConsoleVisible() const override;
    void refreshTheme() override;
    void showWithSavedGeometry() override;
    void saveGeometry() override;

    /// The window; null until the shell is built.
    [[nodiscard]] ObservatoryWindow* window() const noexcept;

private:
    an::ui::shell::ApplicationCore& m_core;
    ObservatoryWindow* m_window = nullptr;
};

/// The Observatory shell as the application registers it: `observatory`, "Observatory", drawn in the Observatory
/// style profile, made over the core.
[[nodiscard]] AFTERNIGHTSHELLOBSERVATORY_API an::ui::shell::ShellDescriptor observatoryShellDescriptor();

} // namespace an::ui::observatory
