// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "commands/CommandProviders.h"

#include "actions/ActionCatalogue.h"
#include "actions/ActionIds.h"
#include "actions/MenuTree.h"
#include "actions/ProcessMenuModel.h"

#include "controls/settings/SettingsSearchTable.h"

#include <QAction>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QPointer>

#include <algorithm>
#include <utility>

namespace an::ui::shell
{

namespace
{

namespace registry = an::ui::process_registry;

/// What a person reads of a text the menus show: no mnemonic marks.
QString readable(QString text)
{
    text.remove(QLatin1Char('&'));
    return text;
}

/// The title of the menu of the menu bar a group of actions belongs to.
QString menuTitleOf(const ActionGroup group)
{
    switch (group)
    {
    case ActionGroup::File:
        return QStringLiteral("File");
    case ActionGroup::Edit:
        return QStringLiteral("Edit");
    case ActionGroup::View:
        return QStringLiteral("View");
    case ActionGroup::Image:
        return QStringLiteral("Image");
    case ActionGroup::Process:
        return QStringLiteral("Process");
    case ActionGroup::Extensions:
        return QStringLiteral("Extensions");
    case ActionGroup::Window:
        return QStringLiteral("Window");
    case ActionGroup::Help:
        return QStringLiteral("Help");
    case ActionGroup::None:
        break;
    }
    return {};
}

/// The menus a person reads down to an action, the first of them as its subtitle.
QStringList menuPathsOf(const MenuTree& menus, const std::string_view actionId)
{
    QStringList paths;
    for (const std::string& path : menus.pathsOf(actionId))
    {
        paths.append(readable(QString::fromStdString(path)));
    }
    return paths;
}

/// What the Process menu calls a process's category: the menu its action sits in under Process, or the menu its
/// registry category joins; the menu path of an action elsewhere, as the conversions under Image are.
QString categoryOf(const registry::ProcessDescriptor& descriptor)
{
    const MenuTree& menus = MenuTree::menuBar();
    const QStringList paths = menuPathsOf(menus, descriptor.action_object_name);
    if (!paths.isEmpty())
    {
        const QString processMenu = menuTitleOf(ActionGroup::Process) + QStringLiteral(" > ");
        return paths.front().startsWith(processMenu) ? paths.front().mid(processMenu.size()) : paths.front();
    }
    const std::string_view menuId = ProcessMenuModel::menuOfCategory(descriptor.category);
    if (const MenuNode* const menu = menuId.empty() ? nullptr : menus.menu(menuId); menu != nullptr)
    {
        return readable(QCoreApplication::translate("AfterNightAppClass", menu->title));
    }
    return menuTitleOf(ActionGroup::Process);
}

/// What stands in the way of a process now: what the registry says of it, unless its launch prepares its extension's
/// environment first, then what its guards say of the image a process of one image would act on.
registry::LaunchGuardResult
obstacleOf(const registry::ProcessDescriptor& descriptor,
           const std::function<registry::LaunchGuardResult(const registry::ProcessDescriptor&)>& imageGuards)
{
    registry::LaunchGuardResult result = registry::evaluateProcessAvailability(descriptor);
    if (!result.ok() && registry::preparesBeforeLaunch(descriptor))
    {
        result = registry::LaunchGuardResult{};
    }
    if (result.ok() && descriptor.launch_mode == registry::ProcessLaunchMode::SingleImage && imageGuards)
    {
        result = imageGuards(descriptor);
    }
    return result;
}

} // namespace

// ── The catalogue's actions ─────────────────────────────────────────────────────────────────────────────────────────

ActionCommands::ActionCommands(Dependencies dependencies) : m_dependencies(std::move(dependencies)) {}

QString ActionCommands::commandId(const std::string_view actionId)
{
    return QStringLiteral("action:") + QString::fromUtf8(actionId.data(), static_cast<qsizetype>(actionId.size()));
}

void ActionCommands::collect(std::vector<CommandEntry>& entries) const
{
    if (!m_dependencies.actions)
    {
        return;
    }
    const MenuTree& menus = m_dependencies.menus != nullptr ? *m_dependencies.menus : MenuTree::menuBar();
    for (const ActionSpec& spec : ActionCatalogue::specs())
    {
        // An action no menu holds is no command a person reaches; one that launches a process is the process's.
        if (spec.group == ActionGroup::None ||
            (m_dependencies.launchesProcess && m_dependencies.launchesProcess(spec.id)))
        {
            continue;
        }
        QAction* const action = m_dependencies.actions(spec.id);
        if (action == nullptr)
        {
            continue;
        }

        CommandEntry entry;
        entry.id = commandId(spec.id);
        const bool opensAFile = spec.id == ids::kOpen;
        entry.title = opensAFile ? QStringLiteral("Open file...") : readable(action->text());
        const QStringList paths = menuPathsOf(menus, spec.id);
        entry.subtitle = paths.isEmpty() ? menuTitleOf(spec.group) : paths.front();
        entry.keywords = paths;
        if (opensAFile)
        {
            entry.keywords.append(readable(action->text()));
        }
        entry.iconKey = QString::fromLatin1(spec.iconKey);
        entry.shortcut = action->shortcut();
        entry.group = opensAFile ? CommandGroup::Images
                                 : (spec.group == ActionGroup::Process ? CommandGroup::Processes : CommandGroup::Views);
        entry.available = action->isEnabled();
        // A disabled action that says why says it; another keeps its menus.
        const QString why = action->statusTip();
        entry.unavailableReason = why.isEmpty() || why == action->text() ? entry.subtitle : why;
        entry.run = [action = QPointer<QAction>(action)]() {
            if (action.isNull() || !action->isEnabled())
            {
                return false;
            }
            action->trigger();
            return true;
        };
        entries.push_back(std::move(entry));
    }
}

// ── The processes ───────────────────────────────────────────────────────────────────────────────────────────────────

ProcessCommands::ProcessCommands(Dependencies dependencies) : m_dependencies(std::move(dependencies)) {}

QString ProcessCommands::commandId(const std::string& descriptorId)
{
    return QStringLiteral("process:") + QString::fromStdString(descriptorId);
}

void ProcessCommands::collect(std::vector<CommandEntry>& entries) const
{
    std::vector<const registry::ProcessDescriptor*> descriptors;
    if (m_dependencies.descriptors)
    {
        descriptors = m_dependencies.descriptors();
    }
    else
    {
        for (const registry::ProcessDescriptor& descriptor : registry::ProcessRegistry::instance().all())
        {
            descriptors.push_back(&descriptor);
        }
    }

    for (const registry::ProcessDescriptor* const descriptor : descriptors)
    {
        if (descriptor == nullptr || descriptor->id.empty())
        {
            continue;
        }
        CommandEntry entry;
        entry.id = commandId(descriptor->id);
        entry.title = readable(QString::fromStdString(!descriptor->display_name.empty()   ? descriptor->display_name
                                                      : !descriptor->process_name.empty() ? descriptor->process_name
                                                                                          : descriptor->id));
        entry.subtitle = categoryOf(*descriptor);
        entry.keywords.append(entry.subtitle);
        if (!descriptor->process_name.empty() && QString::fromStdString(descriptor->process_name) != entry.title)
        {
            entry.keywords.append(QString::fromStdString(descriptor->process_name));
        }
        if (descriptor->is_extension_process)
        {
            // Listed under its family, which it is found by too, with the tag that says it runs in its own process.
            const QString family =
                m_dependencies.extensionFamily ? m_dependencies.extensionFamily(descriptor->extension_id) : QString();
            if (!family.isEmpty())
            {
                entry.subtitle = family;
                entry.keywords.append(family);
            }
            entry.tag = registry::extensionProcessTag();
            entry.tagToolTip = registry::extensionProcessTagToolTip();
        }
        entry.group = CommandGroup::Processes;

        const ActionSpec* const spec = ActionCatalogue::specOf(descriptor->action_object_name);
        entry.iconKey =
            spec != nullptr && spec->iconKey[0] != '\0' ? QString::fromLatin1(spec->iconKey) : QStringLiteral("tune");
        if (QAction* const action = m_dependencies.actions && !descriptor->action_object_name.empty()
                                        ? m_dependencies.actions(descriptor->action_object_name)
                                        : nullptr;
            action != nullptr)
        {
            entry.shortcut = action->shortcut();
        }

        const registry::LaunchGuardResult obstacle = obstacleOf(*descriptor, m_dependencies.imageGuards);
        entry.available = obstacle.ok();
        entry.unavailableReason = QString::fromStdString(obstacle.message);

        const std::string id = descriptor->id;
        entry.run = [launch = m_dependencies.launch, id]() { return launch && launch(id); };
        if (m_dependencies.setFavorite)
        {
            entry.setFavorite = [setFavorite = m_dependencies.setFavorite, id](const bool favorite) {
                setFavorite(id, favorite);
            };
            entry.favorite = m_dependencies.isFavorite && m_dependencies.isFavorite(id);
        }
        entries.push_back(std::move(entry));
    }
}

// ── The images ──────────────────────────────────────────────────────────────────────────────────────────────────────

ImageCommands::ImageCommands(Dependencies dependencies) : m_dependencies(std::move(dependencies)) {}

QString ImageCommands::viewCommandId(const QString& name)
{
    return QStringLiteral("view:") + name;
}

QString ImageCommands::fileCommandId(const QString& path)
{
    return QStringLiteral("file:") + path;
}

void ImageCommands::collect(std::vector<CommandEntry>& entries) const
{
    if (m_dependencies.views)
    {
        for (const OpenView& view : m_dependencies.views())
        {
            if (view.name.isEmpty())
            {
                continue;
            }
            CommandEntry entry;
            entry.id = viewCommandId(view.name);
            entry.title = view.name;
            entry.subtitle =
                view.filePath.isEmpty() ? QStringLiteral("Open image") : QDir::toNativeSeparators(view.filePath);
            if (!view.filePath.isEmpty())
            {
                entry.keywords.append(QFileInfo(view.filePath).fileName());
            }
            entry.iconKey = QStringLiteral("image");
            entry.group = CommandGroup::Images;
            entry.run = [activate = m_dependencies.activateView, name = view.name]() {
                if (!activate)
                {
                    return false;
                }
                activate(name);
                return true;
            };
            entries.push_back(std::move(entry));
        }
    }
    if (m_dependencies.recentFiles)
    {
        for (const QString& path : m_dependencies.recentFiles())
        {
            if (path.isEmpty())
            {
                continue;
            }
            const QFileInfo file(path);
            CommandEntry entry;
            entry.id = fileCommandId(path);
            entry.title = file.fileName();
            entry.subtitle = QDir::toNativeSeparators(file.path());
            entry.iconKey = QStringLiteral("image");
            entry.group = CommandGroup::Images;
            entry.run = [open = m_dependencies.openFile, path]() {
                if (!open)
                {
                    return false;
                }
                open(path);
                return true;
            };
            entries.push_back(std::move(entry));
        }
    }
}

// ── The settings ────────────────────────────────────────────────────────────────────────────────────────────────────

SettingsCommands::SettingsCommands(Dependencies dependencies) : m_dependencies(std::move(dependencies)) {}

QString SettingsCommands::pageCommandId(const QString& pageTitle)
{
    return QStringLiteral("settings:") + pageTitle;
}

QString SettingsCommands::rowCommandId(const QString& pageTitle, const QString& section, const QString& rowLabel)
{
    return pageCommandId(pageTitle) + QLatin1Char('/') + section + QLatin1Char('/') + rowLabel;
}

void SettingsCommands::collect(std::vector<CommandEntry>& entries) const
{
    const std::vector<an::ui::SettingsSearchPage> pages =
        m_dependencies.pages ? m_dependencies.pages() : an::ui::settingsSearchPages();
    for (const an::ui::SettingsSearchPage& page : pages)
    {
        const auto opener = [open = m_dependencies.openPage, title = page.title]() {
            if (!open)
            {
                return false;
            }
            open(title);
            return true;
        };

        CommandEntry entry;
        entry.id = pageCommandId(page.title);
        entry.title = page.title;
        entry.subtitle = QStringLiteral("Settings");
        entry.keywords = page.keywords;
        entry.iconKey = QStringLiteral("settings");
        entry.group = CommandGroup::Settings;
        entry.run = opener;
        entries.push_back(std::move(entry));

        for (const an::ui::SettingsSearchRow& row : page.rows)
        {
            // A label the page shows in two sections - a compression of two formats - is told apart by its section.
            const bool repeated =
                std::count_if(page.rows.begin(), page.rows.end(),
                              [&row](const an::ui::SettingsSearchRow& other) { return other.label == row.label; }) > 1;
            CommandEntry setting;
            setting.id = rowCommandId(page.title, row.section, row.label);
            setting.title = page.title + QStringLiteral(" > ") +
                            (repeated && !row.section.isEmpty() ? row.section + QStringLiteral(" > ") : QString()) +
                            row.label;
            setting.subtitle = row.section.isEmpty() ? QStringLiteral("Settings") : row.section;
            if (!row.section.isEmpty())
            {
                setting.keywords.append(row.section);
            }
            setting.iconKey = QStringLiteral("settings");
            setting.group = CommandGroup::Settings;
            setting.run = opener;
            entries.push_back(std::move(setting));
        }
    }
}

} // namespace an::ui::shell
