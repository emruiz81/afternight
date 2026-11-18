// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "ObservatoryShell.h"

#include "ObservatoryWindow.h"
#include "tasks/TaskPresenter.h"

#include "core/ApplicationCore.h"
#include "core/ApplicationSession.h"
#include "projects/JobProjects.h"

#include <QObject>
#include <QString>

#include <memory>
#include <stdexcept>

namespace an::ui::observatory
{

ObservatoryShell::ObservatoryShell(an::ui::shell::ApplicationCore& core) : m_core(core) {}

ObservatoryShell::~ObservatoryShell()
{
    if (m_window != nullptr)
    {
        ObservatoryWindow* const window = m_window;
        m_window = nullptr;
        delete window;
    }
    if (m_core.shell() == this)
    {
        m_core.setShell(nullptr);
    }
}

void ObservatoryShell::build()
{
    if (m_window != nullptr)
    {
        return;
    }
    if (!m_core.isStarted() || m_core.context() == nullptr)
    {
        throw std::runtime_error("The Observatory shell is built over an application that is initialized and started");
    }
    an::ui::ApplicationSession* const session = m_core.applicationSession();
    if (session == nullptr || !session->isOpen())
    {
        throw std::runtime_error("The Observatory shell is built over the application's session, and none is open");
    }

    // The core's services answer through this shell from here: where a view goes, what a dialog belongs to, what the
    // status line shows.
    m_core.setShell(this);
    m_window = new ObservatoryWindow(m_core);
    m_window->initialize();
}

QWidget* ObservatoryShell::mainWindow()
{
    return m_window;
}

an::ui::shell::IViewHost& ObservatoryShell::viewHost()
{
    return *m_window;
}

an::ui::shell::IStatusDisplay& ObservatoryShell::statusDisplay()
{
    return *m_window;
}

an::ui::IProcessPresenter* ObservatoryShell::processPresenter()
{
    return m_window != nullptr ? m_window->taskPresenter() : nullptr;
}

an::ui::IJobProjects* ObservatoryShell::jobProjects()
{
    return m_window != nullptr ? &m_core.jobProjects() : nullptr;
}

void ObservatoryShell::setSessionConsoleVisible(const bool visible)
{
    if (m_window != nullptr)
    {
        m_window->setSessionConsoleVisible(visible);
    }
}

bool ObservatoryShell::isSessionConsoleVisible() const
{
    return m_window != nullptr && m_window->isSessionConsoleVisible();
}

void ObservatoryShell::refreshTheme()
{
    if (m_window != nullptr)
    {
        m_window->refreshTheme();
    }
}

void ObservatoryShell::showWithSavedGeometry()
{
    if (m_window != nullptr)
    {
        m_window->showWithSavedGeometry();
    }
}

void ObservatoryShell::saveGeometry()
{
    if (m_window != nullptr)
    {
        m_window->keepGeometry();
    }
}

ObservatoryWindow* ObservatoryShell::window() const noexcept
{
    return m_window;
}

an::ui::shell::ShellDescriptor observatoryShellDescriptor()
{
    return an::ui::shell::ShellDescriptor{
        .id = QString::fromLatin1(ObservatoryShell::kId),
        .displayName = QObject::tr("Observatory"),
        .styleProfile = an::ui::StyleProfile::Observatory,
        .create = [](an::ui::shell::ApplicationCore& core) -> std::unique_ptr<an::ui::shell::IShell> {
            return std::make_unique<ObservatoryShell>(core);
        },
    };
}

} // namespace an::ui::observatory
