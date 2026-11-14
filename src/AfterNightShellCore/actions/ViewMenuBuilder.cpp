// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "actions/ViewMenuBuilder.h"

#include "actions/ActionCatalogue.h"
#include "actions/ActionIds.h"
#include "core/ApplicationCore.h"
#include "views/DockLayoutOperations.h"
#include "views/ViewService.h"

#include "app/IconManager.h"
#include "controls/ImageViewControl.h"

#include <kddockwidgets/DockWidget.h>
#include <kddockwidgets/core/DockWidget.h>
#include <kddockwidgets/core/Group.h>

#include <QAction>
#include <QIcon>
#include <QMenu>
#include <QObject>
#include <QPointer>
#include <QString>

#include <vector>

namespace an::ui::shell
{

namespace
{

/// The views listed now whose dock widgets are tabs of the group `view`'s is in, `view` among them, in the list's
/// order.
std::vector<QPointer<ImageViewControl>> viewsOfTheTabGroupOf(ImageViewControl& view, ViewService& views)
{
    std::vector<QPointer<ImageViewControl>> group;
    const KDDockWidgets::Core::Group* const tabGroup =
        view.getDockWidget() != nullptr ? view.getDockWidget()->group() : nullptr;
    if (tabGroup == nullptr)
    {
        group.emplace_back(&view);
        return group;
    }
    for (ImageViewControl* const listed : views.getListOfViewControls())
    {
        if (listed != nullptr && listed->getDockWidget() != nullptr && listed->getDockWidget()->group() == tabGroup)
        {
            group.emplace_back(listed);
        }
    }
    return group;
}

} // namespace

ViewMenuBuilder::ViewMenuBuilder(ApplicationCore& core) : m_core(core) {}

void ViewMenuBuilder::buildViewMenu(QMenu& menu, ImageViewControl& view, const MenuLookup& menus) const
{
    for (const std::string_view id : kSharedMenus)
    {
        if (QMenu* const shared = menus ? menus(id) : nullptr)
        {
            menu.addAction(shared->menuAction());
        }
    }

    const auto icon = [this](const char* key) {
        const auto& icons = m_core.iconManager();
        return icons ? icons->getIcon(QString::fromLatin1(key)) : QIcon();
    };
    QMenu* const currentWindow = menu.addMenu(QObject::tr("Current Window"));
    QAction* const group = currentWindow->addAction(icon("tab_group"), QObject::tr("Group All Images"));
    QAction* const split = currentWindow->addAction(icon("grid_3"), QObject::tr("Split All Images"));
    QAction* const fit = currentWindow->addAction(icon("zoom_fit"), QObject::tr("Fit All Images to Window"));

    DockLayoutOperations& layout = m_core.dockLayout();
    const QPointer<ImageViewControl> guarded(&view);
    QObject::connect(group, &QAction::triggered, &menu, [&layout, guarded]() {
        if (guarded)
        {
            layout.groupAll(*guarded);
        }
    });
    QObject::connect(split, &QAction::triggered, &menu, [&layout, guarded]() {
        if (guarded)
        {
            layout.splitAllEqually(*guarded);
        }
    });
    QObject::connect(fit, &QAction::triggered, &menu, [&layout, guarded]() {
        if (guarded)
        {
            layout.fitAllToWindow(*guarded);
        }
    });

    // A maximized view is alone on screen: there is nothing beside it to group or split.
    if (view.isMaximized())
    {
        group->setEnabled(false);
        split->setEnabled(false);
    }

    // The Image menu's own entry, so it is offered - and enabled - as the menu bar offers it.
    menu.addSeparator();
    if (QAction* const showInFileBrowser = m_core.actions().action(ids::kShowInFileBrowser))
    {
        menu.addAction(showInFileBrowser);
    }
}

void ViewMenuBuilder::buildTabMenu(QMenu& menu, ImageViewControl& view) const
{
    // The catalogue's actions act on the active view: the tab's view is made the active one.
    ViewService& views = m_core.viewService();
    views.setActiveImageViewControl(&view);

    ActionCatalogue& actions = m_core.actions();
    const auto addCatalogued = [&menu, &actions](std::string_view id) {
        if (QAction* const action = actions.action(id))
        {
            menu.addAction(action);
        }
    };

    addCatalogued(ids::kClose);
    addCatalogued(ids::kCloseAllOthers);
    QAction* const closeGroup = menu.addAction(QObject::tr("Close Tab Group"));
    menu.addSeparator();
    QAction* const floatView = menu.addAction(QObject::tr("Float"));
    menu.addSeparator();
    addCatalogued(ids::kDuplicate);
    addCatalogued(ids::kRename);
    addCatalogued(ids::kSaveFile);
    menu.addSeparator();
    addCatalogued(ids::kShowInFileBrowser);

    const QPointer<ImageViewControl> guarded(&view);
    QAction* const close = actions.action(ids::kClose);
    QObject::connect(closeGroup, &QAction::triggered, &menu, [guarded, &views, close]() {
        if (!guarded || close == nullptr)
        {
            return;
        }
        // Each view of the group is closed as Close closes it, asked about unsaved changes as it is.
        for (const QPointer<ImageViewControl>& member : viewsOfTheTabGroupOf(*guarded, views))
        {
            if (member)
            {
                views.setActiveImageViewControl(member.data());
                close->trigger();
            }
        }
    });
    QObject::connect(floatView, &QAction::triggered, &menu, [guarded]() {
        if (guarded && guarded->getDockWidget() != nullptr)
        {
            guarded->getDockWidget()->setFloating(true);
        }
    });
}

} // namespace an::ui::shell
