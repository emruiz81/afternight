// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "actions/ProcessMenuModel.h"

#include "actions/MenuTree.h"
#include "services/FavoriteProcessesModel.h"
#include "services/RecentProcessesModel.h"

#include "app/ExtensionManager.h"
#include "app/ProcessRegistry.h"

#include <QAction>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QMenu>
#include <QPointer>
#include <QUrl>
#include <QVariant>

#include <set>
#include <utility>

namespace an::ui::shell
{

namespace
{

/// The marks of what the model adds to a menu that the menu tree does not hold, so it can be taken away again.
constexpr const char* kExtensionProcessEntryProperty = "anExtensionProcessMenuAction";
constexpr const char* kExtensionFamilyEntryProperty = "anExtensionFamilyMenuAction";

/// The registry categories an extension's process may have, and the menu of the Process menu each joins.
struct CategoryMenu
{
    std::string_view category;
    std::string_view menuId;
};

constexpr CategoryMenu kCategoryMenus[] = {
    {"tone_brightness", "menuAdjustments"},
    {"filters", "menuFilters"},
    {"geometry", "menuGeometry_2"},
    {"color", "menuColor_Calibration"},
    {"denoising", "menuNoise_Reduction"},
    {"sharpening_enhancement", "menuDetails"},
    {"star_object", "menuStar_Processing"},
    {"background_extraction", "menuBackground_Modelization"},
    {"transforms", "menuIntensity_Transformations"},
    {"sensor_calibration", "menuPreprocess"},
    {"multi_image_stacking", "menuPreprocess"},
    {"mask_generation", "menuMask_Generation"},
};

/// Takes away from a menu, and from its submenus, every entry carrying `property`: an action the model owns is taken
/// out, anything else - a separator, a submenu the model made in the menu - is deleted.
void removeMarkedEntries(QMenu& menu, const char* property, const std::set<const QAction*>& owned)
{
    const QList<QAction*> entries = menu.actions();
    for (QAction* entry : entries)
    {
        if (entry == nullptr)
        {
            continue;
        }
        if (entry->property(property).toBool())
        {
            QMenu* const submenu = entry->menu();
            menu.removeAction(entry);
            if (owned.contains(entry))
            {
                continue;
            }
            if (submenu != nullptr)
            {
                delete submenu;
            }
            else
            {
                delete entry;
            }
            continue;
        }
        if (QMenu* const submenu = entry->menu())
        {
            removeMarkedEntries(*submenu, property, owned);
        }
    }
}

void addExternalUrlAction(QMenu& menu, const QString& text, const std::string& url)
{
    if (url.empty())
    {
        return;
    }

    auto* action = menu.addAction(text);
    const QString target = QString::fromUtf8(url);
    action->setStatusTip(target);
    action->setToolTip(target);
    QObject::connect(action, &QAction::triggered, action, [target]() { QDesktopServices::openUrl(QUrl(target)); });
}

} // namespace

struct ProcessMenuModel::Impl
{
    /// An action an extension's process added, and the menu it joins.
    struct ExtensionEntry
    {
        std::string menuId;
        std::unique_ptr<QAction> action;
    };

    Dependencies deps;
    Hooks hooks;
    std::vector<ExtensionEntry> extensionEntries;
    /// The catalogue's actions an extension's process was bound to.
    std::vector<QPointer<QAction>> reboundActions;
    std::vector<ExtensionFamily> families;
    std::vector<std::pair<std::size_t, std::function<void(Change)>>> listeners;
    std::size_t nextSubscription = 1u;
    /// Whether the next refresh of the stored lists drops what the registry no longer holds.
    bool pruneMissingOnNextRefresh = false;

    [[nodiscard]] const process_registry::ProcessDescriptor* find(const std::string& id) const
    {
        return deps.lookup ? deps.lookup(id) : process_registry::ProcessRegistry::instance().find(id);
    }

    [[nodiscard]] std::set<const QAction*> ownedActions() const
    {
        std::set<const QAction*> owned;
        for (const ExtensionEntry& entry : extensionEntries)
        {
            owned.insert(entry.action.get());
        }
        return owned;
    }

    void bind(QAction* action, const std::string& category, const std::string& descriptorId) const
    {
        if (deps.bindExtensionAction)
        {
            deps.bindExtensionAction(action, category, descriptorId);
        }
    }
};

ProcessMenuModel::ProcessMenuModel(Dependencies dependencies) : m_impl(std::make_unique<Impl>())
{
    m_impl->deps = std::move(dependencies);
}

ProcessMenuModel::~ProcessMenuModel() = default;

std::vector<ProcessMenuModel::Category> ProcessMenuModel::categories()
{
    std::vector<Category> categories;
    const MenuNode* const process = MenuTree::menuBar().menu("menuProcess");
    if (process == nullptr)
    {
        return categories;
    }
    for (const MenuItem& item : process->items)
    {
        const MenuNode* const menu = item.kind == MenuItemKind::Menu ? MenuTree::menuBar().menu(item.id) : nullptr;
        if (menu != nullptr && !menu->varies)
        {
            categories.push_back(Category{menu->id, QCoreApplication::translate("AfterNightAppClass", menu->title)});
        }
    }
    return categories;
}

std::string_view ProcessMenuModel::menuOfCategory(const std::string_view category)
{
    for (const CategoryMenu& known : kCategoryMenus)
    {
        if (known.category == category)
        {
            return known.menuId;
        }
    }
    return {};
}

void ProcessMenuModel::setExtensions(const std::vector<an::ui::extensions::ExtensionLoadResult>& discovered)
{
    m_impl->extensionEntries.clear();
    m_impl->reboundActions.clear();
    m_impl->families = ProcessLaunchService::extensionFamilies(discovered);

    for (const auto& result : discovered)
    {
        if (!result.manifest || result.status == an::ui::extensions::ExtensionLoadStatus::Invalid)
        {
            continue;
        }

        for (const auto& process : result.manifest->processes)
        {
            const std::string_view menuId = menuOfCategory(process.category);
            if (menuId.empty())
            {
                if (an::platform::isLoggingInitialized())
                {
                    LOG_WARNING("No process-menu target is defined for extension category='{}'", process.category);
                }
                continue;
            }

            if (QAction* const existing =
                    m_impl->deps.actions ? m_impl->deps.actions(process.action_object_name) : nullptr)
            {
                existing->setProperty("anRegistryDescriptorId", QString::fromStdString(process.id));
                m_impl->bind(existing, process.category, process.id);
                m_impl->reboundActions.push_back(existing);
                continue;
            }

            const process_registry::ProcessDescriptor* const descriptor = m_impl->find(process.id);
            if (!descriptor)
            {
                if (an::platform::isLoggingInitialized())
                {
                    LOG_WARNING("Extension descriptor missing after registration, id={}", process.id);
                }
                continue;
            }

            auto action = std::make_unique<QAction>();
            action->setObjectName(QString::fromStdString(process.action_object_name));
            action->setText(QString::fromUtf8(descriptor->display_name.c_str()));
            action->setProperty("anRegistryDescriptorId", QString::fromStdString(process.id));
            action->setProperty(kExtensionProcessEntryProperty, true);
            m_impl->bind(action.get(), descriptor->category, process.id);
            m_impl->extensionEntries.push_back(Impl::ExtensionEntry{std::string(menuId), std::move(action)});
        }
    }
    notifyChanged(Change::Extensions);
}

void ProcessMenuModel::rebindExtensions(const std::vector<an::ui::extensions::ExtensionLoadResult>& discovered)
{
    for (const auto& result : discovered)
    {
        if (!result.manifest || result.status == an::ui::extensions::ExtensionLoadStatus::Invalid)
        {
            continue;
        }

        for (const auto& process : result.manifest->processes)
        {
            QAction* action = m_impl->deps.actions ? m_impl->deps.actions(process.action_object_name) : nullptr;
            if (action == nullptr)
            {
                action = extensionAction(process.action_object_name);
            }
            if (action != nullptr)
            {
                action->setProperty("anRegistryDescriptorId", QString::fromStdString(process.id));
                m_impl->bind(action, process.category, process.id);
            }
        }
    }
}

std::vector<QAction*> ProcessMenuModel::extensionActionsIn(const std::string_view menuId) const
{
    std::vector<QAction*> actions;
    for (const Impl::ExtensionEntry& entry : m_impl->extensionEntries)
    {
        if (entry.menuId == menuId)
        {
            actions.push_back(entry.action.get());
        }
    }
    return actions;
}

std::vector<QAction*> ProcessMenuModel::extensionActions() const
{
    std::vector<QAction*> actions;
    for (const QPointer<QAction>& rebound : m_impl->reboundActions)
    {
        if (!rebound.isNull())
        {
            actions.push_back(rebound.data());
        }
    }
    for (const Impl::ExtensionEntry& entry : m_impl->extensionEntries)
    {
        actions.push_back(entry.action.get());
    }
    return actions;
}

QAction* ProcessMenuModel::extensionAction(const std::string_view objectName) const
{
    const QString name = QString::fromUtf8(objectName.data(), static_cast<qsizetype>(objectName.size()));
    for (const Impl::ExtensionEntry& entry : m_impl->extensionEntries)
    {
        if (entry.action->objectName() == name)
        {
            return entry.action.get();
        }
    }
    return nullptr;
}

const std::vector<ExtensionFamily>& ProcessMenuModel::extensionFamilies() const noexcept
{
    return m_impl->families;
}

void ProcessMenuModel::buildFavoriteProcessesMenu(QMenu& menu) const
{
    menu.clear();
    FavoriteProcessesModel* const favourites = m_impl->deps.favoriteProcesses;
    if (favourites == nullptr)
    {
        return;
    }
    favourites->load();

    const std::vector<std::pair<QString, std::string>> entries = favourites->entries();
    const auto launch = m_impl->deps.launch;
    for (const auto& [displayName, processId] : entries)
    {
        auto* action = menu.addAction(displayName);
        action->setToolTip(QString::fromUtf8(processId));
        QObject::connect(action, &QAction::triggered, action, [launch, id = processId]() {
            if (!launch || !launch(id))
            {
                LOG_WARNING("Favorite process launch failed for descriptor id={}", id);
            }
        });
    }

    const an::ui::FavoriteProcessMenuLayout layout = an::ui::buildFavoriteProcessMenuLayout(entries.size());
    if (layout.addSeparatorBeforeManage)
    {
        menu.addSeparator();
    }

    auto* manage = menu.addAction(QCoreApplication::translate("AfterNightApp", "Manage Favorites..."));
    QObject::connect(manage, &QAction::triggered, manage, [impl = m_impl.get()]() {
        if (impl->hooks.manageFavoriteProcesses)
        {
            impl->hooks.manageFavoriteProcesses();
        }
    });

    auto* clear = menu.addAction(QCoreApplication::translate("AfterNightApp", "Clear favorites list"));
    clear->setEnabled(layout.clearEnabled);
    QObject::connect(clear, &QAction::triggered, clear, [impl = m_impl.get()]() {
        if (impl->hooks.clearFavoriteProcesses)
        {
            impl->hooks.clearFavoriteProcesses();
        }
    });
}

void ProcessMenuModel::buildRecentProcessesMenu(QMenu& menu) const
{
    menu.clear();
    RecentProcessesModel* const recent = m_impl->deps.recentProcesses;
    if (recent == nullptr || recent->entries().empty())
    {
        menu.setEnabled(false);
        return;
    }
    recent->enforceLimit();
    menu.setEnabled(true);

    const auto launch = m_impl->deps.launch;
    for (const auto& recentProcess : recent->entries())
    {
        const std::string processId = recentProcess.id;
        QString text = QString::fromUtf8(recentProcess.displayName);
        if (text.isEmpty())
        {
            text = QString::fromUtf8(processId);
        }

        auto* action = menu.addAction(text);
        action->setToolTip(QString::fromUtf8(processId));
        if (!m_impl->find(processId))
        {
            const QString pendingReason =
                QStringLiteral("This recent extension process is still loading and cannot be launched yet.");
            action->setEnabled(false);
            action->setToolTip(pendingReason);
            action->setStatusTip(pendingReason);
            action->setWhatsThis(pendingReason);
        }
        QObject::connect(action, &QAction::triggered, action, [launch, processId]() {
            if (!launch || !launch(processId))
            {
                LOG_WARNING("Recent process launch failed for descriptor id={}", processId);
            }
        });
    }

    menu.addSeparator();
    auto* clear = menu.addAction(QCoreApplication::translate("AfterNightApp", "Clear recent processes list"));
    QObject::connect(clear, &QAction::triggered, clear, [impl = m_impl.get()]() {
        if (impl->hooks.clearRecentProcesses)
        {
            impl->hooks.clearRecentProcesses();
        }
    });
}

void ProcessMenuModel::appendExtensionProcesses(QMenu& menu, const std::string_view menuId) const
{
    removeMarkedEntries(menu, kExtensionProcessEntryProperty, m_impl->ownedActions());
    const std::vector<QAction*> actions = extensionActionsIn(menuId);
    if (actions.empty())
    {
        return;
    }
    QAction* const separator = menu.addSeparator();
    separator->setProperty(kExtensionProcessEntryProperty, true);
    for (QAction* action : actions)
    {
        menu.addAction(action);
    }
}

void ProcessMenuModel::buildExtensionFamilyMenus(QMenu& extensionsMenu) const
{
    removeMarkedEntries(extensionsMenu, kExtensionFamilyEntryProperty, {});
    if (m_impl->families.empty())
    {
        return;
    }

    QAction* const separator = extensionsMenu.addSeparator();
    separator->setProperty(kExtensionFamilyEntryProperty, true);

    for (const ExtensionFamily& family : m_impl->families)
    {
        auto* familyMenu = new QMenu(family.displayName, &extensionsMenu);
        QAction* const familyAction = extensionsMenu.addMenu(familyMenu);
        familyAction->setProperty(kExtensionFamilyEntryProperty, true);

        auto* manage = familyMenu->addAction(QCoreApplication::translate("AfterNightAppClass", "Manage..."));
        QObject::connect(manage, &QAction::triggered, manage, [manageExtensions = m_impl->deps.manageExtensions]() {
            if (manageExtensions)
            {
                manageExtensions();
            }
        });

        addExternalUrlAction(*familyMenu, QCoreApplication::translate("AfterNightAppClass", "Homepage"),
                             family.homepageUrl);
        addExternalUrlAction(*familyMenu, QCoreApplication::translate("AfterNightAppClass", "Repository"),
                             family.repositoryUrl);
        addExternalUrlAction(*familyMenu, QCoreApplication::translate("AfterNightAppClass", "Support"),
                             family.supportUrl);
    }
}

void ProcessMenuModel::setHooks(Hooks hooks)
{
    m_impl->hooks = std::move(hooks);
}

void ProcessMenuModel::refreshStoredLists()
{
    const bool pruneMissing = m_impl->pruneMissingOnNextRefresh;
    m_impl->pruneMissingOnNextRefresh = false;

    if (RecentProcessesModel* const recent = m_impl->deps.recentProcesses)
    {
        recent->load();
        if (pruneMissing)
        {
            (void)recent->pruneMissing();
        }
        recent->enforceLimit();
        recent->save();
    }
    notifyChanged(Change::RecentProcesses);

    if (FavoriteProcessesModel* const favourites = m_impl->deps.favoriteProcesses)
    {
        favourites->load();
        if (pruneMissing)
        {
            (void)favourites->pruneMissing();
        }
    }
    notifyChanged(Change::FavoriteProcesses);
}

void ProcessMenuModel::pruneMissingOnNextRefresh() noexcept
{
    m_impl->pruneMissingOnNextRefresh = true;
}

std::size_t ProcessMenuModel::subscribe(std::function<void(Change change)> listener)
{
    if (!listener)
    {
        return 0u;
    }
    const std::size_t subscription = m_impl->nextSubscription++;
    m_impl->listeners.emplace_back(subscription, std::move(listener));
    return subscription;
}

void ProcessMenuModel::unsubscribe(const std::size_t subscription)
{
    std::erase_if(m_impl->listeners, [subscription](const auto& listener) { return listener.first == subscription; });
}

void ProcessMenuModel::notifyChanged(const Change change) const
{
    // A listener may subscribe or unsubscribe while it is told: the ones told are those subscribed when it was said.
    const auto listeners = m_impl->listeners;
    for (const auto& listener : listeners)
    {
        listener.second(change);
    }
}

} // namespace an::ui::shell
