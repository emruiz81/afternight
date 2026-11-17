// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "ShellActions.h"

#include "commands/CommandIndex.h"

#include <QAction>
#include <QActionGroup>
#include <QCoreApplication>
#include <QPointer>
#include <QWidget>

#include <utility>

namespace an::ui::observatory
{

namespace
{

constexpr char kContext[] = "an::ui::observatory::ShellActions";

/// The shell's actions, as the command index offers them.
class ShellActionCommands final : public an::ui::shell::ICommandProvider
{
public:
    explicit ShellActionCommands(const std::vector<QAction*>& actions)
    {
        for (QAction* action : actions)
        {
            m_actions.emplace_back(action);
        }
    }

    void collect(std::vector<an::ui::shell::CommandEntry>& entries) const override
    {
        for (const QPointer<QAction>& held : m_actions)
        {
            QAction* const action = held.data();
            if (action == nullptr)
            {
                continue;
            }
            an::ui::shell::CommandEntry entry;
            entry.id = QStringLiteral("action:") + action->objectName();
            entry.title = QString(action->text()).remove(QLatin1Char('&'));
            entry.shortcut = action->shortcut();
            entry.iconKey = action->property("iconKey").toString();
            entry.subtitle = action->property("commandSubtitle").toString();
            entry.group = an::ui::shell::CommandGroup::Views;
            entry.keywords = {action->property("commandMenu").toString()};
            entry.available = action->isEnabled();
            entry.run = [held]() {
                if (held.isNull() || !held->isEnabled())
                {
                    return false;
                }
                held->trigger();
                return true;
            };
            entries.push_back(std::move(entry));
        }
    }

private:
    std::vector<QPointer<QAction>> m_actions;
};

} // namespace

const std::vector<ShellActionSpec>& ShellActions::table()
{
    static const std::vector<ShellActionSpec> kTable = {
        {ids::kShowHome, QT_TRANSLATE_NOOP("an::ui::observatory::ShellActions", "Home"), QKeySequence(), "home", false,
         QT_TRANSLATE_NOOP("an::ui::observatory::ShellActions", "View")},
        {ids::kShowIntegrationHub, QT_TRANSLATE_NOOP("an::ui::observatory::ShellActions", "Integration"),
         QKeySequence(), "layers", false, "View"},
        {ids::kCommandPalette, QT_TRANSLATE_NOOP("an::ui::observatory::ShellActions", "Command Palette..."),
         QKeySequence(Qt::CTRL | Qt::Key_K), "search", false, "View"},
        {ids::kToggleReadout, QT_TRANSLATE_NOOP("an::ui::observatory::ShellActions", "Readout"), QKeySequence(),
         "panel_right", true, "View"},
        {ids::kToggleNight, QT_TRANSLATE_NOOP("an::ui::observatory::ShellActions", "Night"),
         QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N), "night", true, "View"},
        {ids::kDensityCompact, QT_TRANSLATE_NOOP("an::ui::observatory::ShellActions", "Compact"), QKeySequence(), "",
         true, "View"},
        {ids::kDensityComfortable, QT_TRANSLATE_NOOP("an::ui::observatory::ShellActions", "Comfortable"),
         QKeySequence(), "", true, "View"},
        {ids::kNewWorkspace, QT_TRANSLATE_NOOP("an::ui::observatory::ShellActions", "New Workspace"), QKeySequence(),
         "add", false, QT_TRANSLATE_NOOP("an::ui::observatory::ShellActions", "Window")},
        {ids::kRenameWorkspace, QT_TRANSLATE_NOOP("an::ui::observatory::ShellActions", "Rename Workspace..."),
         QKeySequence(), "edit", false, "Window"},
        {ids::kMoveWorkspaceToNewWindow,
         QT_TRANSLATE_NOOP("an::ui::observatory::ShellActions", "Move Workspace to New Window"), QKeySequence(),
         "open_in_new", false, "Window"},
        {ids::kCloseWorkspace, QT_TRANSLATE_NOOP("an::ui::observatory::ShellActions", "Close Workspace"),
         QKeySequence(), "close", false, "Window"},
        {ids::kApplyTask, QT_TRANSLATE_NOOP("an::ui::observatory::ShellActions", "Apply"),
         QKeySequence(Qt::CTRL | Qt::Key_Return), "check", false,
         QT_TRANSLATE_NOOP("an::ui::observatory::ShellActions", "Process")},
        {ids::kFloatTask, QT_TRANSLATE_NOOP("an::ui::observatory::ShellActions", "Float as Window"), QKeySequence(),
         "open_in_new", false, "Window"},
        {ids::kAttachTask, QT_TRANSLATE_NOOP("an::ui::observatory::ShellActions", "Attach"), QKeySequence(), "window",
         false, "Window"},
        {ids::kCloseTask, QT_TRANSLATE_NOOP("an::ui::observatory::ShellActions", "Close Task"), QKeySequence(), "close",
         false, "Window"},
        {ids::kNewProject, QT_TRANSLATE_NOOP("an::ui::observatory::ShellActions", "New Project..."), QKeySequence(),
         "add", false, QT_TRANSLATE_NOOP("an::ui::observatory::ShellActions", "File")},
        {ids::kOpenProject, QT_TRANSLATE_NOOP("an::ui::observatory::ShellActions", "Open Project..."), QKeySequence(),
         "folder_open", false, "File"},
        {ids::kSwitchProject, QT_TRANSLATE_NOOP("an::ui::observatory::ShellActions", "Switch Project..."),
         QKeySequence(), "folder_closed", false, "File"},
        {ids::kCloseProject, QT_TRANSLATE_NOOP("an::ui::observatory::ShellActions", "Close Project"), QKeySequence(),
         "close", false, "File"},
        {ids::kProjectSettings, QT_TRANSLATE_NOOP("an::ui::observatory::ShellActions", "Project Settings..."),
         QKeySequence(), "settings", false, "File"},
    };
    return kTable;
}

ShellActions::ShellActions(QWidget& window)
{
    m_densities = new QActionGroup(&window);
    m_densities->setExclusive(true);
    for (const ShellActionSpec& spec : table())
    {
        auto* action = new QAction(QCoreApplication::translate(kContext, spec.text), &window);
        action->setObjectName(QString::fromLatin1(spec.id.data(), static_cast<qsizetype>(spec.id.size())));
        action->setShortcut(spec.shortcut);
        action->setShortcutContext(Qt::WindowShortcut);
        action->setCheckable(spec.checkable);
        action->setProperty("iconKey", QString::fromLatin1(spec.iconKey));
        action->setProperty("commandMenu", QCoreApplication::translate(kContext, spec.menu));
        if (!spec.shortcut.isEmpty())
        {
            action->setToolTip(
                QStringLiteral("%1 (%2)").arg(action->text(), spec.shortcut.toString(QKeySequence::NativeText)));
        }
        if (spec.id == ids::kDensityCompact || spec.id == ids::kDensityComfortable)
        {
            m_densities->addAction(action);
        }
        if (spec.id == ids::kApplyTask)
        {
            // Ctrl+Enter written as a sequence is the keypad's Enter: Apply answers both Enter keys.
            action->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_Return), QKeySequence(Qt::CTRL | Qt::Key_Enter)});
        }
        window.addAction(action);
        m_actions.push_back(action);
    }
}

QAction* ShellActions::action(const std::string_view id) const
{
    const QString name = QString::fromLatin1(id.data(), static_cast<qsizetype>(id.size()));
    for (QAction* action : m_actions)
    {
        if (action->objectName() == name)
        {
            return action;
        }
    }
    return nullptr;
}

const std::vector<QAction*>& ShellActions::all() const noexcept
{
    return m_actions;
}

QActionGroup* ShellActions::densities() const noexcept
{
    return m_densities;
}

std::unique_ptr<an::ui::shell::ICommandProvider> ShellActions::commandProvider() const
{
    return std::make_unique<ShellActionCommands>(m_actions);
}

} // namespace an::ui::observatory
