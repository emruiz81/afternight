// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "views/DockHooks.h"

#include "controls/KDDockWidgetsHelper.h"

#include <AfterNightPlatform/Logging.h>

#include <kddockwidgets/Config.h>
#include <kddockwidgets/LayoutSaver.h>
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

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLayout>
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
    /// What answers a name while a layout is applied; null otherwise. The library's factory is a plain function too.
    const DockHooks::DockWidgetResolver* resolver = nullptr;
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

/// The library's factory of dock widgets, asked for a name a layout holds that no dock widget has.
KDDockWidgets::Core::DockWidget* dockWidgetNamed(const QString& name)
{
    const DockHooks::DockWidgetResolver* const resolver = dispatcher().resolver;
    if (resolver == nullptr || !*resolver)
    {
        return nullptr;
    }
    KDDockWidgets::QtWidgets::DockWidget* const dockWidget = (*resolver)(name);
    return dockWidget != nullptr ? dockWidget->dockWidget() : nullptr;
}

/// Lays out a widget that is not on screen, and what is inside it: a hidden widget is laid out only as it is shown.
void layOutUnseen(QWidget& widget)
{
    if (QLayout* const layout = widget.layout())
    {
        layout->invalidate();
        layout->activate();
    }
    for (QObject* const child : widget.children())
    {
        if (auto* const inner = qobject_cast<QWidget*>(child); inner != nullptr && !inner->isWindow())
        {
            layOutUnseen(*inner);
        }
    }
}

/// Destroys the dock widgets that have lost their guest: ones that delete themselves when closed - a view's - whose
/// view was deleted from under them. Nothing is left to show in one, and the library does not expect one.
void destroyDockWidgetsWithNoGuest()
{
    std::vector<QPointer<KDDockWidgets::QtWidgets::DockWidget>> gone;
    for (KDDockWidgets::Core::DockWidget* const dockWidget : KDDockWidgets::DockRegistry::self()->dockwidgets())
    {
        if (dockWidget == nullptr || !(dockWidget->options() & KDDockWidgets::DockWidgetOption_DeleteOnClose))
        {
            continue;
        }
        // The guest is the dock widget's one child widget. It is asked of Qt, not of the library, which goes on
        // naming a guest that was deleted.
        KDDockWidgets::QtWidgets::DockWidget* const widget = dockWidgetOf(dockWidget);
        if (widget != nullptr && widget->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly).isEmpty())
        {
            gone.emplace_back(widget);
        }
    }
    for (const QPointer<KDDockWidgets::QtWidgets::DockWidget>& widget : gone)
    {
        delete widget.data();
    }
}

/// The size the layout of a main window had when it was saved; an empty one when the entry states none.
[[nodiscard]] QSize savedLayoutSize(const QJsonObject& mainWindow)
{
    const QJsonObject geometry = mainWindow.value(QLatin1String("multiSplitterLayout"))
                                     .toObject()
                                     .value(QLatin1String("layout"))
                                     .toObject()
                                     .value(QLatin1String("sizingInfo"))
                                     .toObject()
                                     .value(QLatin1String("geometry"))
                                     .toObject();
    return QSize(geometry.value(QLatin1String("width")).toInt(), geometry.value(QLatin1String("height")).toInt());
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

QByteArray DockHooks::saveLayout()
{
    destroyDockWidgetsWithNoGuest();
    const KDDockWidgets::LayoutSaver saver;
    return saver.serializeLayout();
}

QStringList DockHooks::mainWindowsOf(const QByteArray& layout)
{
    QStringList names;
    const QJsonDocument stored = QJsonDocument::fromJson(layout);
    if (!stored.isObject())
    {
        return names;
    }
    for (const QJsonValue& entry : stored.object().value(QLatin1String("mainWindows")).toArray())
    {
        const QString name = entry.toObject().value(QLatin1String("uniqueName")).toString();
        if (!name.isEmpty())
        {
            names.append(name);
        }
    }
    return names;
}

bool DockHooks::restoreLayout(const QByteArray& layout, const DockWidgetResolver& resolver)
{
    KDDockWidgets::DockRegistry* const registry = KDDockWidgets::DockRegistry::self();
    // The library gives up part way at a main window it does not find, with the others already emptied: a layout
    // that holds one the process lacks is not applied at all.
    const QStringList mainWindows = mainWindowsOf(layout);
    if (mainWindows.isEmpty())
    {
        return false;
    }
    for (const QString& name : mainWindows)
    {
        if (!registry->containsMainWindow(name))
        {
            LOG_WARNING("A saved layout holds the dock area '{}', which there is none of: it is not applied",
                        name.toStdString());
            return false;
        }
    }

    // The library fits each layout to the size its main window has now. One that is not on screen may have no size
    // of its own yet - a page never shown - and a layout squeezed into it loses where its splitters were: it takes
    // the size its layout was saved at, and is laid out at it. Its parent gives it its place when it is shown.
    for (const QJsonValue& entry :
         QJsonDocument::fromJson(layout).object().value(QLatin1String("mainWindows")).toArray())
    {
        const QJsonObject saved = entry.toObject();
        KDDockWidgets::Core::MainWindow* const mainWindow =
            registry->mainWindowByName(saved.value(QLatin1String("uniqueName")).toString());
        QWidget* const widget = mainWindow != nullptr ? widgetOf(mainWindow->view()) : nullptr;
        const QSize size = savedLayoutSize(saved);
        if (widget == nullptr || widget->isVisible() || size.isEmpty() || widget->size() == size)
        {
            continue;
        }
        widget->resize(size);
        layOutUnseen(*widget);
    }

    // The library shows the guest of every dock widget a layout names: one that has lost its guest goes first.
    destroyDockWidgetsWithNoGuest();

    // A dock widget the layout names is closed before it is placed, and one that deletes itself when closed would be
    // placed and gone: each such one is told not to while the layout is applied.
    std::vector<QPointer<KDDockWidgets::QtWidgets::DockWidget>> kept;
    for (KDDockWidgets::Core::DockWidget* const dockWidget : registry->dockwidgets())
    {
        if (dockWidget == nullptr || !(dockWidget->options() & KDDockWidgets::DockWidgetOption_DeleteOnClose))
        {
            continue;
        }
        const QByteArray quoted = '"' + dockWidget->uniqueName().toUtf8() + '"';
        if (!layout.contains(quoted))
        {
            continue;
        }
        if (KDDockWidgets::QtWidgets::DockWidget* const widget = dockWidgetOf(dockWidget))
        {
            widget->setOptions(widget->options() &
                               ~KDDockWidgets::DockWidgetOptions(KDDockWidgets::DockWidgetOption_DeleteOnClose));
            kept.emplace_back(widget);
        }
    }

    DispatcherState& state = dispatcher();
    state.resolver = &resolver;
    KDDockWidgets::Config::self().setDockWidgetFactoryFunc(&dockWidgetNamed);
    // The window the dock areas are in keeps its place and its size, which are its own setting; a floating window
    // goes where it was on the screen.
    KDDockWidgets::LayoutSaver saver(KDDockWidgets::RestoreOptions(
        KDDockWidgets::RestoreOption_RelativeToMainWindow | KDDockWidgets::RestoreOption_AbsoluteFloatingDockWindows));
    const bool restored = saver.restoreLayout(layout);
    KDDockWidgets::Config::self().setDockWidgetFactoryFunc(nullptr);
    state.resolver = nullptr;

    for (const QPointer<KDDockWidgets::QtWidgets::DockWidget>& widget : kept)
    {
        if (!widget.isNull())
        {
            widget->setOptions(widget->options() | KDDockWidgets::DockWidgetOption_DeleteOnClose);
        }
    }
    return restored;
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
