// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "ClassicShell.h"

#include "AfterNightApp.h"

#include "core/ApplicationCore.h"
#include "core/ApplicationSession.h"

#include <QString>

#include <stdexcept>

namespace an::ui::shell
{

ClassicShell::ClassicShell(ApplicationCore& core) : m_core(core) {}

ClassicShell::~ClassicShell()
{
    if (m_window != nullptr)
    {
        m_window = nullptr;
        AfterNightApp::destroyInstance();
    }
}

void ClassicShell::build()
{
    if (m_window != nullptr)
    {
        return;
    }
    an::ui::ApplicationSession* const session = m_core.applicationSession();
    if (session == nullptr)
    {
        throw std::runtime_error("The classic shell is built over the application's session, and the core holds none");
    }

    m_window = AfterNightApp::createInstance(m_core);
    m_window->initialize(*session);
    // The classic window builds itself as it is made: its own build makes nothing more.
    m_window->build();
}

QWidget* ClassicShell::mainWindow()
{
    return m_window;
}

IViewHost& ClassicShell::viewHost()
{
    return *m_window;
}

IStatusDisplay& ClassicShell::statusDisplay()
{
    return *m_window;
}

an::ui::IProcessPresenter* ClassicShell::processPresenter()
{
    return m_window != nullptr ? m_window->processPresenter() : nullptr;
}

void ClassicShell::setSessionConsoleVisible(bool visible)
{
    if (m_window != nullptr)
    {
        m_window->setSessionConsoleVisible(visible);
    }
}

bool ClassicShell::isSessionConsoleVisible() const
{
    return m_window != nullptr && m_window->isSessionConsoleVisible();
}

void ClassicShell::refreshTheme()
{
    if (m_window != nullptr)
    {
        m_window->refreshTheme();
    }
}

void ClassicShell::showWithSavedGeometry()
{
    if (m_window != nullptr)
    {
        m_window->showWithSavedGeometry();
    }
}

void ClassicShell::saveGeometry()
{
    if (m_window != nullptr)
    {
        m_window->saveGeometry();
    }
}

AfterNightApp* ClassicShell::window() const noexcept
{
    return m_window;
}

ShellDescriptor classicShellDescriptor()
{
    return ShellDescriptor{
        .id = QString::fromLatin1(ClassicShell::kId),
        .displayName = QObject::tr("Classic"),
        .create = [](ApplicationCore& core) -> std::unique_ptr<IShell> { return std::make_unique<ClassicShell>(core); },
    };
}

} // namespace an::ui::shell
