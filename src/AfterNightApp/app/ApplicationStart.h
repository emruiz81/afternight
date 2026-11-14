// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The order the application starts in.

#include <functional>

class AfterNightApp;

namespace an::ui::shell
{
class ApplicationCore;
}

namespace an::ui
{

class ApplicationSession;

/// Starts the application in its order: the core's stores first - they make no widget, and the shell is chosen from
/// one of them - then the style, before the first widget exists; then the main window, made over the core, readied with
/// the session - which it opens from the limits the settings store states - and built. Nothing is shown.
///
/// @param core the application core; it outlives the window.
/// @param session the engine session; it outlives the window, and the caller shuts it down once the window is gone.
/// @param installStyle installs the application's style; called once, once the stores are open and before any widget.
/// @return the main window.
/// @throws std::runtime_error when the stores or the session cannot be opened, as the application reports it.
AfterNightApp* startApplication(an::ui::shell::ApplicationCore& core, ApplicationSession& session,
                                const std::function<void()>& installStyle);

} // namespace an::ui
