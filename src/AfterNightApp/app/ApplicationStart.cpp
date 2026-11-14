// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "app/ApplicationStart.h"

#include "core/ApplicationCore.h"
#include "core/ApplicationSession.h"
#include "core/ShellRegistry.h"

#include <AfterNightPlatform/Logging.h>

#include <stdexcept>
#include <utility>

namespace an::ui
{

StartedApplication startApplication(an::ui::shell::ApplicationCore& core, const an::ui::shell::ShellRegistry& registry,
                                    const QStringList& arguments, const QProcessEnvironment& environment,
                                    const std::function<void()>& installStyle)
{
    // The stores first: they make no widget, and the shell is chosen from one of them.
    core.openStores();

    StartedApplication started;
    started.choice = an::ui::shell::ShellSelection::resolve(arguments, environment, core.globalSettings(), registry);
    if (started.choice.descriptor == nullptr)
    {
        throw std::runtime_error("No shell is registered under the name the settings or the default give");
    }
    LOG_INFO("Starting the {} shell", started.choice.descriptor->id.toStdString());

    // The style before the first widget exists.
    if (installStyle)
    {
        installStyle();
    }

    started.shell = started.choice.descriptor->create(core);
    started.shell->build();
    return started;
}

void showApplication(an::ui::shell::IShell& shell)
{
    shell.showWithSavedGeometry();
}

an::Status stopApplication(std::unique_ptr<an::ui::shell::IShell> shell, ApplicationSession& session)
{
    if (shell != nullptr)
    {
        shell->saveGeometry();
    }
    return shutDownApplication(session, [&shell]() { shell.reset(); });
}

} // namespace an::ui
