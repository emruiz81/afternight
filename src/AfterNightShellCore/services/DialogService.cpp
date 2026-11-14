// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "services/DialogService.h"

#include "core/ApplicationCore.h"
#include "core/IShell.h"

#include "app/MessageBoxes.h"

#include <QMessageBox>
#include <QWidget>

#include <utility>

namespace an::ui::shell
{

QWidget* dialogParentFor(const ApplicationCore& core, QWidget* requested)
{
    if (requested != nullptr)
    {
        return requested->window();
    }
    IShell* const shell = core.shell();
    return shell != nullptr ? shell->mainWindow() : nullptr;
}

DialogService::DialogService(ApplicationCore& core) : m_core(core) {}

void DialogService::setHooks(Hooks hooks)
{
    m_hooks = std::move(hooks);
}

void DialogService::openSettingsAt(const QString& pageName)
{
    if (m_hooks.openSettingsAt)
    {
        m_hooks.openSettingsAt(pageName);
    }
}

void DialogService::openExtensionsAt(const QString& pageName)
{
    if (m_hooks.openExtensionsAt)
    {
        m_hooks.openExtensionsAt(pageName);
    }
}

void DialogService::messageBoxYesNo(QWidget* parent, const QString& title, const QString& text, const bool warn,
                                    int* ret)
{
    const an::ui::ScopedTopLevelFlagRestorer restorer;
    const auto icon = warn ? QMessageBox::Warning : QMessageBox::Question;
    const auto result = an::ui::showModernMessageBox(dialogParentFor(m_core, parent), icon, title, text,
                                                     QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret)
    {
        *ret = static_cast<int>(result);
    }
}

void DialogService::notifyErrorMessage(QWidget* parent, const QString& title, const QString& text)
{
    const an::ui::ScopedTopLevelFlagRestorer restorer;
    an::ui::showModernMessageBox(dialogParentFor(m_core, parent), QMessageBox::Critical, title, text, QMessageBox::Ok,
                                 QMessageBox::Ok);
}

void DialogService::notifyErrorMessageAsync(QWidget* parent, const QString& title, const QString& text)
{
    m_core.notifyErrorMessageAsync(parent, title, text);
}

void DialogService::notifyWarnMessage(QWidget* parent, const QString& title, const QString& text)
{
    const an::ui::ScopedTopLevelFlagRestorer restorer;
    an::ui::showModernMessageBox(dialogParentFor(m_core, parent), QMessageBox::Warning, title, text, QMessageBox::Ok,
                                 QMessageBox::Ok);
}

void DialogService::notifyWarnMessageAsync(QWidget* parent, const QString& title, const QString& text)
{
    m_core.notifyWarnMessageAsync(parent, title, text);
}

void DialogService::notifyInfoMessage(QWidget* parent, const QString& title, const QString& text)
{
    const an::ui::ScopedTopLevelFlagRestorer restorer;
    an::ui::showModernMessageBox(dialogParentFor(m_core, parent), QMessageBox::Information, title, text,
                                 QMessageBox::Ok, QMessageBox::Ok);
}

void DialogService::notifyInfoMessageAsync(QWidget* parent, const QString& title, const QString& text)
{
    m_core.notifyInfoMessageAsync(parent, title, text);
}

} // namespace an::ui::shell
