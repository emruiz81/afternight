// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "IAppContext.h"

namespace an::ui
{

namespace
{
IAppContext* g_app_context = nullptr;
}

void setAppContext(IAppContext* ctx)
{
    assert(ctx && "setAppContext() called with nullptr");
    assert(!g_app_context && "setAppContext() called more than once");
    g_app_context = ctx;
}

void clearAppContext()
{
    g_app_context = nullptr;
}

IAppContext* appContext()
{
    return g_app_context;
}

} // namespace an::ui
