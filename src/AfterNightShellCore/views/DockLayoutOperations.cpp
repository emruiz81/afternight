// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "views/DockLayoutOperations.h"

#include "actions/ActionCatalogue.h"
#include "actions/ActionIds.h"
#include "core/ApplicationCore.h"
#include "views/DockHooks.h"
#include "views/ViewService.h"

#include "controls/ImageViewControl.h"
#include "controls/ImageViewControlManager.h"

#include <kddockwidgets/DockWidget.h>
#include <kddockwidgets/MainWindow.h>
#include <kddockwidgets/core/DockWidget.h>
#include <kddockwidgets/core/DropArea.h>
#include <kddockwidgets/core/FloatingWindow.h>
#include <kddockwidgets/core/MainWindow.h>

#include <QAction>
#include <QList>
#include <QPointer>

#include <algorithm>
#include <type_traits>
#include <utility>

namespace an::ui::shell
{

namespace
{

using DockWidget = KDDockWidgets::QtWidgets::DockWidget;
using MainWindow = KDDockWidgets::QtWidgets::MainWindow;

/// Places dock widgets in a grid of equal shares in a container: a main window's dock area, or the window a dock
/// widget is in (`addDockWidgetToContainingWindow`). One to sixteen in the patterns the layout always used - columns
/// first, then rows below them - and past sixteen in further columns of four.
template <typename ContainerType> void placeInEqualShares(ContainerType* container, const QList<DockWidget*>& docks)
{
    const auto add = [container](DockWidget* dockWidget, KDDockWidgets::Location location,
                                 DockWidget* relativeTo = nullptr) {
        if constexpr (std::is_same_v<ContainerType, MainWindow>)
        {
            if (relativeTo != nullptr)
            {
                container->addDockWidget(dockWidget, location, relativeTo);
            }
            else
            {
                container->addDockWidget(dockWidget, location);
            }
        }
        else
        {
            if (relativeTo != nullptr)
            {
                container->addDockWidgetToContainingWindow(dockWidget, location, relativeTo);
            }
            else
            {
                container->addDockWidgetToContainingWindow(dockWidget, location);
            }
        }
    };

    const auto layoutEqually = [container]() {
        if constexpr (std::is_same_v<ContainerType, MainWindow>)
        {
            container->layoutEqually();
        }
        else
        {
            if (auto* const floatingWindow = container->dockWidget()->floatingWindow())
            {
                floatingWindow->dropArea()->layoutEqually();
            }
            else if (auto* const mainWindow = container->dockWidget()->mainWindow())
            {
                mainWindow->dropArea()->layoutEqually();
            }
        }
    };

    using KDDockWidgets::Location_OnBottom;
    using KDDockWidgets::Location_OnLeft;
    using KDDockWidgets::Location_OnRight;
    const auto& d = docks;
    const qsizetype count = docks.size();

    if (count == 1)
    {
        add(d[0], Location_OnLeft);
    }
    else if (count == 2)
    {
        add(d[0], Location_OnLeft);
        add(d[1], Location_OnRight, d[0]);
    }
    else if (count == 3)
    {
        add(d[0], Location_OnLeft);
        add(d[1], Location_OnBottom, d[0]);
        add(d[2], Location_OnRight);
    }
    else if (count == 4)
    {
        add(d[0], Location_OnLeft);
        add(d[1], Location_OnRight, d[0]);
        add(d[2], Location_OnBottom, d[0]);
        add(d[3], Location_OnBottom, d[1]);
    }
    else if (count == 5)
    {
        add(d[0], Location_OnLeft);
        add(d[1], Location_OnRight, d[0]);
        add(d[2], Location_OnBottom);
        add(d[3], Location_OnRight, d[2]);
        add(d[4], Location_OnRight, d[3]);
    }
    else if (count == 6)
    {
        add(d[0], Location_OnLeft);
        add(d[1], Location_OnRight, d[0]);
        add(d[2], Location_OnRight, d[1]);
        add(d[3], Location_OnBottom, d[0]);
        add(d[4], Location_OnBottom, d[1]);
        add(d[5], Location_OnBottom, d[2]);
    }
    else if (count == 7)
    {
        add(d[0], Location_OnLeft);
        add(d[1], Location_OnRight, d[0]);
        add(d[2], Location_OnRight, d[1]);
        add(d[3], Location_OnRight, d[2]);

        add(d[4], Location_OnBottom);
        add(d[5], Location_OnRight, d[4]);
        add(d[6], Location_OnRight, d[5]);
    }
    else if (count == 8)
    {
        add(d[0], Location_OnLeft);
        add(d[1], Location_OnRight, d[0]);
        add(d[2], Location_OnRight, d[1]);
        add(d[3], Location_OnRight, d[2]);

        add(d[4], Location_OnBottom, d[0]);
        add(d[5], Location_OnBottom, d[1]);
        add(d[6], Location_OnBottom, d[2]);
        add(d[7], Location_OnBottom, d[3]);
    }
    else if (count == 9)
    {
        add(d[0], Location_OnLeft);
        add(d[1], Location_OnRight, d[0]);
        add(d[2], Location_OnRight, d[1]);
        add(d[3], Location_OnBottom, d[0]);
        add(d[4], Location_OnBottom, d[1]);
        add(d[5], Location_OnBottom, d[2]);
        add(d[6], Location_OnBottom, d[3]);
        add(d[7], Location_OnBottom, d[4]);
        add(d[8], Location_OnBottom, d[5]);
    }
    else if (count == 10)
    {
        add(d[0], Location_OnLeft);
        add(d[1], Location_OnRight, d[0]);
        add(d[2], Location_OnRight, d[1]);
        add(d[3], Location_OnRight, d[2]);
        add(d[4], Location_OnRight, d[3]);

        add(d[5], Location_OnBottom, d[0]);
        add(d[6], Location_OnBottom, d[1]);
        add(d[7], Location_OnBottom, d[2]);
        add(d[8], Location_OnBottom, d[3]);
        add(d[9], Location_OnBottom, d[4]);
    }
    else if (count == 11)
    {
        add(d[0], Location_OnLeft);
        add(d[1], Location_OnRight, d[0]);
        add(d[2], Location_OnRight, d[1]);
        add(d[3], Location_OnRight, d[2]);

        add(d[4], Location_OnBottom, d[0]);
        add(d[5], Location_OnBottom, d[1]);
        add(d[6], Location_OnBottom, d[2]);
        add(d[7], Location_OnBottom, d[3]);

        add(d[8], Location_OnBottom);
        add(d[9], Location_OnRight, d[8]);
        add(d[10], Location_OnRight, d[9]);
    }
    else if (count == 12)
    {
        add(d[0], Location_OnLeft);
        add(d[1], Location_OnRight, d[0]);
        add(d[2], Location_OnRight, d[1]);
        add(d[3], Location_OnRight, d[2]);

        add(d[4], Location_OnBottom, d[0]);
        add(d[5], Location_OnBottom, d[1]);
        add(d[6], Location_OnBottom, d[2]);
        add(d[7], Location_OnBottom, d[3]);

        add(d[8], Location_OnBottom, d[4]);
        add(d[9], Location_OnBottom, d[5]);
        add(d[10], Location_OnBottom, d[6]);
        add(d[11], Location_OnBottom, d[7]);
    }
    else if (count == 13)
    {
        add(d[0], Location_OnLeft);
        add(d[1], Location_OnRight, d[0]);
        add(d[2], Location_OnRight, d[1]);
        add(d[3], Location_OnRight, d[2]);

        add(d[4], Location_OnBottom, d[0]);
        add(d[5], Location_OnBottom, d[1]);
        add(d[6], Location_OnBottom, d[2]);
        add(d[7], Location_OnBottom, d[3]);

        add(d[8], Location_OnBottom);
        add(d[9], Location_OnRight, d[8]);
        add(d[10], Location_OnRight, d[9]);
        add(d[11], Location_OnRight, d[10]);
        add(d[12], Location_OnRight, d[11]);
    }
    else if (count == 14)
    {
        add(d[0], Location_OnLeft);
        add(d[1], Location_OnRight, d[0]);
        add(d[2], Location_OnRight, d[1]);
        add(d[3], Location_OnRight, d[2]);

        add(d[4], Location_OnBottom, d[0]);
        add(d[5], Location_OnBottom, d[1]);
        add(d[6], Location_OnBottom, d[2]);
        add(d[7], Location_OnBottom, d[3]);

        add(d[8], Location_OnBottom, d[4]);
        add(d[9], Location_OnBottom, d[5]);
        add(d[10], Location_OnBottom, d[6]);
        add(d[11], Location_OnBottom, d[7]);

        add(d[12], Location_OnBottom);
        add(d[13], Location_OnRight, d[12]);
    }
    else if (count == 15)
    {
        add(d[0], Location_OnLeft);
        add(d[1], Location_OnRight, d[0]);
        add(d[2], Location_OnRight, d[1]);
        add(d[3], Location_OnRight, d[2]);

        add(d[4], Location_OnBottom, d[0]);
        add(d[5], Location_OnBottom, d[1]);
        add(d[6], Location_OnBottom, d[2]);
        add(d[7], Location_OnBottom, d[3]);

        add(d[8], Location_OnBottom, d[4]);
        add(d[9], Location_OnBottom, d[5]);
        add(d[10], Location_OnBottom, d[6]);
        add(d[11], Location_OnBottom, d[7]);

        add(d[12], Location_OnBottom);
        add(d[13], Location_OnRight, d[12]);
        add(d[14], Location_OnRight, d[13]);
    }
    else if (count >= 16)
    {
        add(d[0], Location_OnLeft);
        add(d[1], Location_OnRight, d[0]);
        add(d[2], Location_OnRight, d[1]);
        add(d[3], Location_OnRight, d[2]);

        add(d[4], Location_OnBottom, d[0]);
        add(d[5], Location_OnBottom, d[1]);
        add(d[6], Location_OnBottom, d[2]);
        add(d[7], Location_OnBottom, d[3]);

        add(d[8], Location_OnBottom, d[4]);
        add(d[9], Location_OnBottom, d[5]);
        add(d[10], Location_OnBottom, d[6]);
        add(d[11], Location_OnBottom, d[7]);

        add(d[12], Location_OnBottom, d[8]);
        add(d[13], Location_OnBottom, d[9]);
        add(d[14], Location_OnBottom, d[10]);
        add(d[15], Location_OnBottom, d[11]);

        int inColumn = 0;
        for (qsizetype i = 16; i < count; ++i)
        {
            if (inColumn == 0)
            {
                add(d[i], Location_OnRight);
            }
            else
            {
                add(d[i], Location_OnBottom, d[i - 1]);
            }

            ++inColumn;
            if (inColumn == 4)
            {
                inColumn = 0;
            }
        }
    }

    layoutEqually();
}

/// The dock widgets of views, in the views' order, leaving out a view with none.
template <typename Views> QList<DockWidget*> dockWidgetsOf(const Views& views)
{
    QList<DockWidget*> docks;
    for (ImageViewControl* view : views)
    {
        if (view != nullptr && view->getDockWidget() != nullptr)
        {
            docks.push_back(view->getDockWidget());
        }
    }
    return docks;
}

/// A lock taken on a dock area.
struct Lock
{
    QPointer<MainWindow> mainWindow;
    bool wholeLayout = false;
    DockHooks::Subscription refusals;
};

} // namespace

struct DockLayoutOperations::Impl
{
    explicit Impl(ApplicationCore& core) : core(core) {}

    ApplicationCore& core;
    std::vector<Lock> locks;

    [[nodiscard]] std::vector<ImageViewControl*> listedViews() const
    {
        return core.viewService().getListOfViewControls();
    }

    [[nodiscard]] ImageViewControl* activeView() const
    {
        return core.viewService().getActiveImageViewControl();
    }

    void activate(ImageViewControl* view) const
    {
        if (view != nullptr)
        {
            core.viewService().setActiveImageViewControl(view);
        }
    }

    [[nodiscard]] std::vector<Lock>::iterator lockOf(const MainWindow* mainWindow)
    {
        return std::find_if(locks.begin(), locks.end(),
                            [mainWindow](const Lock& lock) { return lock.mainWindow.data() == mainWindow; });
    }

    /// Offers Lock Layout or Unlock Layout, whichever undoes the state the arrangement is in.
    void offerTheOtherOf(bool locked) const
    {
        if (QAction* const lockAction = core.actions().action(ids::kLockLayout))
        {
            lockAction->setEnabled(!locked);
        }
        if (QAction* const unlockAction = core.actions().action(ids::kUnlockLayout))
        {
            unlockAction->setEnabled(locked);
        }
    }

    void lift(std::vector<Lock>::iterator lock)
    {
        const bool wholeLayout = lock->wholeLayout;
        locks.erase(lock);
        if (wholeLayout &&
            std::none_of(locks.begin(), locks.end(), [](const Lock& other) { return other.wholeLayout; }))
        {
            DockHooks::setDropIndicatorsInhibited(false);
        }
    }
};

DockLayoutOperations::DockLayoutOperations(ApplicationCore& core) : m_impl(std::make_unique<Impl>(core)) {}

DockLayoutOperations::~DockLayoutOperations()
{
    while (!m_impl->locks.empty())
    {
        m_impl->lift(m_impl->locks.begin());
    }
}

void DockLayoutOperations::groupAll(const DockTarget& target)
{
    if (target.mainWindow == nullptr)
    {
        return;
    }
    ImageViewControl* const active = m_impl->activeView();
    const QList<DockWidget*> docks = dockWidgetsOf(m_impl->listedViews());
    if (docks.empty())
    {
        return;
    }

    target.mainWindow->addDockWidget(docks[0], KDDockWidgets::Location_OnLeft);
    for (qsizetype i = 1; i < docks.size(); ++i)
    {
        docks[0]->addDockWidgetAsTab(docks[i]);
    }
    m_impl->activate(active);
}

void DockLayoutOperations::splitAllEqually(const DockTarget& target)
{
    if (target.mainWindow == nullptr)
    {
        return;
    }
    ImageViewControl* const active = m_impl->activeView();
    const QList<DockWidget*> docks = dockWidgetsOf(m_impl->listedViews());
    if (!docks.empty())
    {
        placeInEqualShares(target.mainWindow, docks);
    }
    m_impl->activate(active);
}

void DockLayoutOperations::groupAll(ImageViewControl& view)
{
    const QList<DockWidget*> docks = dockWidgetsOf(viewsSharingTheWindowOf(view, m_impl->listedViews()));
    if (docks.empty())
    {
        return;
    }

    docks[0]->addDockWidgetToContainingWindow(docks[0], KDDockWidgets::Location_OnLeft);
    for (qsizetype i = 1; i < docks.size(); ++i)
    {
        docks[0]->addDockWidgetAsTab(docks[i]);
    }
    m_impl->activate(&view);
}

void DockLayoutOperations::splitAllEqually(ImageViewControl& view)
{
    const QList<DockWidget*> docks = dockWidgetsOf(viewsSharingTheWindowOf(view, m_impl->listedViews()));
    if (docks.size() > 1 && view.getDockWidget() != nullptr)
    {
        placeInEqualShares(view.getDockWidget(), docks);
    }
    m_impl->activate(&view);
}

void DockLayoutOperations::fitAllToWindow(ImageViewControl& view)
{
    for (ImageViewControl* const sharing : viewsSharingTheWindowOf(view, m_impl->listedViews()))
    {
        if (sharing != nullptr)
        {
            sharing->fitToWindow();
        }
    }
}

void DockLayoutOperations::lock(const DockTarget& target)
{
    if (target.mainWindow == nullptr || isLocked(target))
    {
        return;
    }

    // A shell with one dock area locks its whole layout, as the layout lock always did: every drag, a floating
    // window's too, and every indicator. A workspace locks its own dock area alone.
    const bool wholeLayout = target.workspaceId.isEmpty();
    MainWindow* const locked = target.mainWindow;
    const QPointer<MainWindow> guarded(locked);
    DockHooks::Subscriber refusals;
    refusals.dragAboutToStart = [guarded, wholeLayout](const DockDrag& drag) {
        return !(drag.mainWindow == guarded.data() || (wholeLayout && drag.mainWindow == nullptr));
    };
    refusals.dropIndicatorAllowed = [guarded, wholeLayout](const DockDrop& drop) {
        return !(drop.mainWindow == guarded.data() || (wholeLayout && drop.mainWindow == nullptr));
    };

    m_impl->locks.push_back(
        Lock{.mainWindow = guarded, .wholeLayout = wholeLayout, .refusals = DockHooks::subscribe(std::move(refusals))});
    if (wholeLayout)
    {
        DockHooks::setDropIndicatorsInhibited(true);
    }
    m_impl->offerTheOtherOf(true);
}

void DockLayoutOperations::unlock(const DockTarget& target)
{
    const auto lock = m_impl->lockOf(target.mainWindow);
    if (lock != m_impl->locks.end())
    {
        m_impl->lift(lock);
    }
    m_impl->offerTheOtherOf(false);
}

bool DockLayoutOperations::isLocked(const DockTarget& target) const
{
    return target.mainWindow != nullptr && m_impl->lockOf(target.mainWindow) != m_impl->locks.end();
}

std::list<ImageViewControl*> DockLayoutOperations::viewsSharingTheWindowOf(ImageViewControl& view,
                                                                           const std::vector<ImageViewControl*>& views)
{
    std::list<ImageViewControl*> sharing;
    DockWidget* const dockWidget = view.getDockWidget();
    if (dockWidget == nullptr)
    {
        return sharing;
    }

    // Docked views share no floating window: every view docked in a dock area is taken, as it always was.
    KDDockWidgets::Core::FloatingWindow* const floatingWindow = dockWidget->dockWidget()->floatingWindow();
    for (ImageViewControl* const other : views)
    {
        if (other == nullptr || other->getDockWidget() == nullptr)
        {
            continue;
        }
        if (other->getDockWidget()->dockWidget()->floatingWindow() == floatingWindow)
        {
            sharing.push_back(other);
        }
    }
    return sharing;
}

} // namespace an::ui::shell
