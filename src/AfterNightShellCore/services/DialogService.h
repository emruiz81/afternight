// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The messages the application shows a person, and where its settings are opened.

#include "AfterNightShellCoreExport.h"

#include "controls/IAppContext.h"

#include <QString>

#include <functional>

class QWidget;

namespace an::ui::shell
{

class ApplicationCore;

/// The window a dialog belongs to: the window of the widget that asked for it, or the shell's window when none did -
/// none before a shell is made.
[[nodiscard]] AFTERNIGHTSHELLCORE_API QWidget* dialogParentFor(const ApplicationCore& core, QWidget* requested);

/// Shows the application's messages - an error, a warning, a note, a yes-or-no question - each modal over the window
/// it belongs to (`dialogParentFor`), and opens the settings and the extensions where the shell says.
///
/// The messages are shown on the GUI thread; the asynchronous ones may be asked from any thread and hand the message to
/// the GUI thread through the application core without waiting for it.
class AFTERNIGHTSHELLCORE_API DialogService final : public an::ui::IDialogService
{
public:
    /// Where the settings and the extensions are opened: by the shell's window, which holds the dialogs that show them.
    struct Hooks
    {
        std::function<void(const QString& pageName)> openSettingsAt;
        std::function<void(const QString& pageName)> openExtensionsAt;
    };

    explicit DialogService(ApplicationCore& core);

    /// Replaces the hooks; an empty one opens nothing.
    void setHooks(Hooks hooks);

    // ---- IDialogService
    void openSettingsAt(const QString& pageName) override;
    void openExtensionsAt(const QString& pageName) override;
    void messageBoxYesNo(QWidget* parent, const QString& title, const QString& text, bool warn, int* ret) override;
    void notifyErrorMessage(QWidget* parent, const QString& title, const QString& text) override;
    void notifyErrorMessageAsync(QWidget* parent, const QString& title, const QString& text) override;
    void notifyWarnMessage(QWidget* parent, const QString& title, const QString& text) override;
    void notifyWarnMessageAsync(QWidget* parent, const QString& title, const QString& text) override;
    void notifyInfoMessage(QWidget* parent, const QString& title, const QString& text) override;
    void notifyInfoMessageAsync(QWidget* parent, const QString& title, const QString& text) override;

private:
    ApplicationCore& m_core;
    Hooks m_hooks;
};

} // namespace an::ui::shell
