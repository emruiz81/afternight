// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "app/ApplicationStart.h"

#include "app/AfterNightApp.h"
#include "app/ApplicationSession.h"

#include "core/ApplicationCore.h"

namespace an::ui
{

AfterNightApp* startApplication(an::ui::shell::ApplicationCore& core, ApplicationSession& session,
                                const std::function<void()>& installStyle)
{
    core.openStores();
    if (installStyle)
    {
        installStyle();
    }

    AfterNightApp* const window = AfterNightApp::createInstance(core);
    window->initialize(session);
    window->build();
    return window;
}

} // namespace an::ui
