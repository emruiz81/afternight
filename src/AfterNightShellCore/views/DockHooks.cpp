// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "views/DockHooks.h"

#include "controls/KDDockWidgetsHelper.h"

#include <AfterNightPlatform/Logging.h>

#include <kddockwidgets/Config.h>
#include <kddockwidgets/core/DockRegistry.h>
#include <kddockwidgets/DockWidget.h>
#include <kddockwidgets/MainWindow.h>
#include <kddockwidgets/core/DockWidget.h>
#include <kddockwidgets/core/Draggable_p.h>
#include <kddockwidgets/core/DropArea.h>
#include <kddockwidgets/core/MainWindow.h>
#include <kddockwidgets/core/TabBar.h>
#include <kddockwidgets/core/View.h>
#include <kddockwidgets/qtcommon/View.h>

#include <QPointer>
#include <QSize>
#include <QWidget>

#include <utility>
#include <vector>

namespace an::ui::shell
{

namespace
{

struct Subscribed
{
    std::uint64_t token = 0;
    DockHooks::Subscriber subscriber;
};

/// The dispatcher's state, one for the process as the library's hooks are: they are plain functions that capture
/// nothing, so this is where they find the subscribers.
struct DispatcherState
{
    bool configured = false;
    std::uint64_t nextToken = 1;
    std::vector<Subscribed> subscribers;
    /// What the drag the subscribers last allowed named: the library tells the end of a drag with nothing.
    bool dragging = false;
    QPointer<KDDockWidgets::QtWidgets::MainWindow> draggedFrom;
    QPointer<KDDockWidgets::QtWidgets::DockWidget> dragged;
};

DispatcherState& dispatcher()
{
    static DispatcherState state;
    return state;
}

QWidget* widgetOf(KDDockWidgets::Core::View* view)
{
    return view != nullptr ? KDDockWidgets::QtCommon::View_qt::asQWidget(view) : nullptr;
}

KDDockWidgets::QtWidgets::DockWidget* dockWidgetOf(KDDockWidgets::Core::DockWidget* dockWidget)
{
    return dockWidget != nullptr ? qobject_cast<KDDockWidgets::QtWidgets::DockWidget*>(widgetOf(dockWidget->view()))
                                 : nullptr;
}

// The library's three hooks.

bool dragAboutToStart(KDDockWidgets::Core::Draggable* draggable)
{
    DockDrag drag;
    if (draggable != nullptr)
    {
        KDDockWidgets::Core::View* const view = draggable->asView();
        drag.mainWindow = DockHooks::mainWindowHolding(widgetOf(view));
        // A draggable names its dock widget when it carries one alone; of a strip of several tabs, the tab pressed is
        // the strip's current one by the time the drag is asked about.
        KDDockWidgets::Core::DockWidget* carried = draggable->singleDockWidget();
        if (carried == nullptr && view != nullptr)
        {
            if (KDDockWidgets::Core::TabBar* const tabBar = view->asTabBarController())
            {
                carried = tabBar->currentDockWidget();
            }
        }
        drag.dockWidget = dockWidgetOf(carried);
    }

    const bool allowed = DockHooks::allowsDrag(drag);
    if (allowed)
    {
        DispatcherState& state = dispatcher();
        state.dragging = true;
        state.draggedFrom = drag.mainWindow;
        state.dragged = drag.dockWidget;
    }
    return allowed;
}

bool dropIndicatorAllowed(KDDockWidgets::DropLocation location,
                          const KDDockWidgets::Vector<KDDockWidgets::Core::DockWidget*>& /*source*/,
                          const KDDockWidgets::Vector<KDDockWidgets::Core::DockWidget*>& /*target*/,
                          KDDockWidgets::Core::DropArea* dropArea)
{
    DockDrop drop{.location = location};
    if (dropArea != nullptr)
    {
        if (KDDockWidgets::Core::MainWindow* const mainWindow = dropArea->mainWindow())
        {
            drop.mainWindow = qobject_cast<KDDockWidgets::QtWidgets::MainWindow*>(widgetOf(mainWindow->view()));
        }
    }
    return DockHooks::allowsDrop(drop);
}

void dragEnded()
{
    DispatcherState& state = dispatcher();
    if (!state.dragging)
    {
        return;
    }
    const DockDrag drag{.mainWindow = state.draggedFrom.data(), .dockWidget = state.dragged.data()};
    state.dragging = false;
    state.draggedFrom = nullptr;
    state.dragged = nullptr;
    DockHooks::tellDragEnded(drag);
}

/// The subscribers as they are now, so one that leaves while it is asked does not move the list being read.
std::vector<DockHooks::Subscriber> subscribersNow()
{
    std::vector<DockHooks::Subscriber> now;
    for (const Subscribed& subscribed : dispatcher().subscribers)
    {
        now.push_back(subscribed.subscriber);
    }
    return now;
}

} // namespace

// ---- The subscription

DockHooks::Subscription::Subscription(std::uint64_t token) noexcept : m_token(token) {}

DockHooks::Subscription::~Subscription()
{
    reset();
}

DockHooks::Subscription::Subscription(Subscription&& other) noexcept : m_token(std::exchange(other.m_token, 0)) {}

DockHooks::Subscription& DockHooks::Subscription::operator=(Subscription&& other) noexcept
{
    if (this != &other)
    {
        reset();
        m_token = std::exchange(other.m_token, 0);
    }
    return *this;
}

bool DockHooks::Subscription::active() const noexcept
{
    return m_token != 0;
}

void DockHooks::Subscription::reset() noexcept
{
    if (m_token == 0)
    {
        return;
    }
    std::erase_if(dispatcher().subscribers,
                  [token = m_token](const Subscribed& subscribed) { return subscribed.token == token; });
    m_token = 0;
}

// ---- The dispatcher

void DockHooks::configure()
{
    DispatcherState& state = dispatcher();
    if (state.configured)
    {
        return;
    }
    // The library refuses its flags and its minimum once a dock widget or a main window exists; the next core made
    // asks again rather than leave the process half configured.
    if (!KDDockWidgets::DockRegistry::self()->isEmpty(/*excludeBeingDeleted=*/false))
    {
        LOG_WARNING("The dock library was not configured: dock widgets or main windows already exist");
        return;
    }
    state.configured = true;

    KDDockWidgets::initFrontend(KDDockWidgets::FrontendType::QtWidgets);
    KDDockWidgets::Config& config = KDDockWidgets::Config::self();
    KDDockWidgets::Config::Flags flags = config.flags();
    for (const KDDockWidgets::Config::Flag flag : kDockLibraryFlags)
    {
        flags |= flag;
    }
    config.setFlags(flags);
    config.setDraggedWindowOpacity(kDraggedDockWindowOpacity);
    config.setAbsoluteWidgetMinSize(QSize(kDockWidgetMinimumSide, kDockWidgetMinimumSide));
    config.setViewFactory(new AN_ViewFactory);

    config.setDragAboutToStartFunc(&dragAboutToStart);
    config.setDropIndicatorAllowedFunc(&dropIndicatorAllowed);
    config.setDragEndedFunc(&dragEnded);
}

bool DockHooks::isConfigured() noexcept
{
    return dispatcher().configured;
}

DockHooks::Subscription DockHooks::subscribe(Subscriber subscriber)
{
    DispatcherState& state = dispatcher();
    const std::uint64_t token = state.nextToken++;
    state.subscribers.push_back(Subscribed{.token = token, .subscriber = std::move(subscriber)});
    return Subscription(token);
}

bool DockHooks::allowsDrag(const DockDrag& drag)
{
    bool allowed = true;
    for (const Subscriber& subscriber : subscribersNow())
    {
        // Every subscriber is asked, so none misses a drag another refused.
        if (subscriber.dragAboutToStart && !subscriber.dragAboutToStart(drag))
        {
            allowed = false;
        }
    }
    return allowed;
}

bool DockHooks::allowsDrop(const DockDrop& drop)
{
    bool allowed = true;
    for (const Subscriber& subscriber : subscribersNow())
    {
        if (subscriber.dropIndicatorAllowed && !subscriber.dropIndicatorAllowed(drop))
        {
            allowed = false;
        }
    }
    return allowed;
}

void DockHooks::tellDragEnded(const DockDrag& drag)
{
    for (const Subscriber& subscriber : subscribersNow())
    {
        if (subscriber.dragEnded)
        {
            subscriber.dragEnded(drag);
        }
    }
}

void DockHooks::setDropIndicatorsInhibited(bool inhibited)
{
    KDDockWidgets::Config::self().setDropIndicatorsInhibited(inhibited);
}

bool DockHooks::dropIndicatorsInhibited()
{
    return KDDockWidgets::Config::self().dropIndicatorsInhibited();
}

KDDockWidgets::QtWidgets::MainWindow* DockHooks::mainWindowHolding(const QWidget* widget)
{
    for (const QWidget* current = widget; current != nullptr; current = current->parentWidget())
    {
        if (auto* const mainWindow = qobject_cast<KDDockWidgets::QtWidgets::MainWindow*>(const_cast<QWidget*>(current)))
        {
            return mainWindow;
        }
        if (current->isWindow())
        {
            return nullptr;
        }
    }
    return nullptr;
}

} // namespace an::ui::shell
