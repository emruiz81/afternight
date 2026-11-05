// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the process framework suite's cases share: the application context the suite runs under - one engine session,
/// the views it makes, the process registry and settings a window asks for, every message - installed by the entry
/// point for the whole run, and a scope that installs another context for one case and puts back the one it replaced.
#pragma once

#include "Test_ProcessesShared/ProcessesSharedTestSupport.h"

#include "controls/IAppContext.h"

namespace an::tests::processes::framework
{

/// The context the suite runs under, installed by the entry point for the whole run. GUI thread only.
[[nodiscard]] ProcessTestContext& suiteContext();

/// Installs an application context for its lifetime and puts back the one it replaced, passing or failing: a case that
/// needs a context of its own - another session, another width - declares one.
class ScopedAppContext
{
public:
    explicit ScopedAppContext(an::ui::IAppContext& context) : m_replaced(an::ui::appContext())
    {
        // setAppContext installs over nothing, so the context installed now is taken down first.
        an::ui::clearAppContext();
        an::ui::setAppContext(&context);
    }
    ~ScopedAppContext()
    {
        QThreadPool::globalInstance()->waitForDone();
        QCoreApplication::processEvents();
        an::ui::clearAppContext();
        if (m_replaced != nullptr)
        {
            an::ui::setAppContext(m_replaced);
        }
    }
    ScopedAppContext(const ScopedAppContext&) = delete;
    ScopedAppContext& operator=(const ScopedAppContext&) = delete;

private:
    an::ui::IAppContext* m_replaced = nullptr;
};

} // namespace an::tests::processes::framework
