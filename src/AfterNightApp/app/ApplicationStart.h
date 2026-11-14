// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The order the application starts and ends in.

#include "core/IShell.h"
#include "core/ShellSelection.h"

#include <AfterNightEngine/Core/Status.h>

#include <QProcessEnvironment>
#include <QStringList>

#include <functional>
#include <memory>

namespace an::ui::shell
{
class ApplicationCore;
class ShellRegistry;
} // namespace an::ui::shell

namespace an::ui
{

class ApplicationSession;

/// The shell a start made, and how it was chosen.
struct StartedApplication
{
    std::unique_ptr<an::ui::shell::IShell> shell;
    an::ui::shell::ShellChoice choice;
};

/// Starts the application in its order: the core's stores first - they make no widget, and the shell is chosen from
/// one of them - then the shell chosen among the registered ones (the command line, the environment, the setting, the
/// default; a source that names none is passed over with a warning in the log), then the style, before the first widget
/// exists, then the shell made over the core and built. Nothing is shown.
///
/// The core is given the application's session first (`ApplicationCore::setApplicationSession`): a shell's window opens
/// it from the limits the settings store states.
///
/// @param core the application core; it outlives the shell.
/// @param registry the shells the application can make.
/// @param arguments the process's arguments as `main` received them, the program first.
/// @param environment the process's environment.
/// @param installStyle installs the application's style; called once, once the stores are open and the shell is chosen,
///        before any widget.
/// @return the shell built, and how it was chosen.
/// @throws std::runtime_error when the stores cannot be opened, no registered shell can be chosen, or the shell cannot
///         be built, as the application reports it.
[[nodiscard]] StartedApplication startApplication(an::ui::shell::ApplicationCore& core,
                                                  const an::ui::shell::ShellRegistry& registry,
                                                  const QStringList& arguments, const QProcessEnvironment& environment,
                                                  const std::function<void()>& installStyle);

/// Shows a started shell where its window was.
void showApplication(an::ui::shell::IShell& shell);

/// Ends a started application in its order, on the GUI thread once its event loop has returned: the shell is asked to
/// keep its geometry, then the application is torn down (`shutDownApplication`) - admission closed, the shell destroyed
/// with its window, views and extension sessions, the thread pool's work finished, the session shut down.
///
/// @return what the session's shutdown answered.
an::Status stopApplication(std::unique_ptr<an::ui::shell::IShell> shell, ApplicationSession& session);

} // namespace an::ui
