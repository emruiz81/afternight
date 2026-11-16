// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "tasks/TaskWindow.h"

#include "pages/TaskPage.h"

#include <kddockwidgets/Config.h>
#include <kddockwidgets/DockWidget.h>
#include <kddockwidgets/core/DockWidget.h>
#include <kddockwidgets/core/FloatingWindow.h>
#include <kddockwidgets/core/Group.h>
#include <kddockwidgets/core/Stack.h>
#include <kddockwidgets/core/TitleBar.h>
#include <kddockwidgets/qtcommon/View.h>

#include <QCoreApplication>
#include <QCursor>
#include <QEvent>
#include <QMouseEvent>
#include <QTimer>
#include <QWidget>

#include <utility>

namespace an::ui::observatory
{

TaskWindow::TaskWindow(const an::ui::shell::TaskId id, Hooks hooks) : m_id(id), m_hooks(std::move(hooks))
{
    // Skipped by a saved layout: a task's window is the task's, which a project does not reopen.
    m_dock = new KDDockWidgets::QtWidgets::DockWidget(dockName(id), KDDockWidgets::DockWidgetOption_None,
                                                      KDDockWidgets::LayoutSaverOption::Skip);
    m_dock->setObjectName(QStringLiteral("observatoryTaskWindow.%1").arg(id));
    m_dock->setAffinities({QString::fromLatin1(kAffinity)});
    // One title bar and no tab strip: the system's title bar where the library uses one. A window rather than a tool
    // window, in which the main window's shortcuts would act.
    KDDockWidgets::FloatingWindowFlags flags = KDDockWidgets::FloatingWindowFlag::AlwaysTitleBarWhenFloating;
    flags |= KDDockWidgets::FloatingWindowFlag::UseQtWindow;
    if (KDDockWidgets::Config::self().flags().testFlag(KDDockWidgets::Config::Flag_NativeTitleBar))
    {
        flags |= KDDockWidgets::FloatingWindowFlag::NativeTitleBar;
    }
    m_dock->dockWidget()->setFloatingWindowFlags(flags);
}

TaskWindow::~TaskWindow()
{
    if (m_watched != nullptr)
    {
        m_watched->removeEventFilter(this);
    }
    if (m_page != nullptr)
    {
        m_page->removeEventFilter(this);
    }
    delete m_dock;
}

QString TaskWindow::dockName(const an::ui::shell::TaskId id)
{
    return QStringLiteral("an.task.%1").arg(id);
}

an::ui::shell::TaskId TaskWindow::task() const noexcept
{
    return m_id;
}

void TaskWindow::show(TaskPage& page, const QRect& geometry)
{
    const QRect placed(geometry.topLeft(), geometry.size().expandedTo(QSize(kMinimumWidth, kMinimumHeight)));
    m_page = &page;
    // The window is never smaller than its least size: the dock library takes a window's least size from what it holds.
    m_pageMinimum = page.minimumSize();
    page.setMinimumSize(m_pageMinimum.expandedTo(QSize(kMinimumWidth, kMinimumHeight)));
    if (m_dock->widget() != &page)
    {
        m_dock->setWidget(&page);
    }
    // A close the window's title bar asks for reaches the page the window holds.
    page.installEventFilter(this);
    page.show();
    m_dock->dockWidget()->setFloatingGeometry(placed);
    if (!m_dock->isOpen())
    {
        m_dock->open();
    }
    // A strip with one tab hides: the window's title names the task.
    if (KDDockWidgets::Core::Group* const group = m_dock->group())
    {
        group->stack()->setTabBarAutoHide(true);
    }
    // The library keeps a new window on a screen; the window goes where it was asked to - over the main window's
    // centre, or under the cursor.
    if (QWidget* const frame = window(); frame != nullptr && frame->geometry() != placed)
    {
        frame->setGeometry(placed);
    }
    watchWindow();
    m_lastSize = size();
}

TaskPage* TaskWindow::takePage()
{
    TaskPage* const page = m_page.data();
    if (page == nullptr)
    {
        return nullptr;
    }
    m_lastSize = size();
    page->removeEventFilter(this);
    if (m_watched != nullptr)
    {
        m_watched->removeEventFilter(this);
        m_watched = nullptr;
    }
    if (m_dock->isOpen())
    {
        m_dock->forceClose();
    }
    if (m_dock->widget() == page)
    {
        m_dock->setWidget(nullptr);
    }
    page->setMinimumSize(m_pageMinimum);
    m_page = nullptr;
    return page;
}

TaskPage* TaskWindow::page() const
{
    return m_page.data();
}

bool TaskWindow::isOpen() const
{
    return m_dock->isOpen() && window() != nullptr;
}

void TaskWindow::setTitle(const QString& title)
{
    m_dock->setTitle(title);
    if (QWidget* const frame = window(); frame != nullptr && frame->windowTitle() != title)
    {
        frame->setWindowTitle(title);
    }
}

QString TaskWindow::title() const
{
    return m_dock->title();
}

void TaskWindow::raiseAndActivate()
{
    QWidget* const frame = window();
    if (frame == nullptr)
    {
        return;
    }
    if (frame->isMinimized())
    {
        frame->showNormal();
    }
    m_dock->raise();
    frame->raise();
    frame->activateWindow();
}

QSize TaskWindow::size() const
{
    const QWidget* const frame = window();
    return frame != nullptr && frame->isVisible() ? frame->geometry().size() : m_lastSize;
}

KDDockWidgets::QtWidgets::DockWidget* TaskWindow::dockWidget() const noexcept
{
    return m_dock;
}

QWidget* TaskWindow::window() const
{
    KDDockWidgets::Core::FloatingWindow* const floating = m_dock->dockWidget()->floatingWindow();
    return floating != nullptr ? KDDockWidgets::QtCommon::View_qt::asQWidget(floating->view()) : nullptr;
}

bool TaskWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (event != nullptr && watched == m_page.data() && event->type() == QEvent::Close)
    {
        // The window's title bar asked to close the task: whether it closes is the owner's to say - not during a run -
        // and the window stays meanwhile. Asked once the library has finished with the close.
        event->ignore();
        endPress();
        QTimer::singleShot(0, this, [this]() {
            if (m_hooks.closeRequested)
            {
                m_hooks.closeRequested();
            }
        });
        return true;
    }
    if (event != nullptr && watched == m_watched.data())
    {
        if (event->type() == QEvent::Move && m_hooks.moved)
        {
            m_hooks.moved();
        }
        else if (event->type() == QEvent::Resize && m_watched->isVisible())
        {
            m_lastSize = m_watched->geometry().size();
        }
    }
    return QObject::eventFilter(watched, event);
}

void TaskWindow::endPress()
{
    // The press on the title bar that closes the window leaves the dock library waiting for a drag, which the next move
    // of the mouse over anything it drags by would start - of a window on its way out. A release on what the library
    // drags the window by ends the press: the system's title bar, an event outside the client area, where the library
    // uses it; its own title bar elsewhere.
    KDDockWidgets::Core::FloatingWindow* const floating = m_dock->dockWidget()->floatingWindow();
    QWidget* receiver = window();
    if (floating == nullptr || receiver == nullptr)
    {
        return;
    }
    QEvent::Type type = QEvent::NonClientAreaMouseButtonRelease;
    if (KDDockWidgets::Core::TitleBar* const titleBar = floating->titleBar();
        titleBar != nullptr && titleBar->isVisible())
    {
        receiver = KDDockWidgets::QtCommon::View_qt::asQWidget(titleBar->view());
        type = QEvent::MouseButtonRelease;
    }
    const QPoint at = QCursor::pos();
    QMouseEvent release(type, QPointF(receiver->mapFromGlobal(at)), QPointF(at), Qt::LeftButton, Qt::NoButton,
                        Qt::NoModifier);
    QCoreApplication::sendEvent(receiver, &release);
}

void TaskWindow::watchWindow()
{
    QWidget* const frame = window();
    if (frame == m_watched.data())
    {
        return;
    }
    if (m_watched != nullptr)
    {
        m_watched->removeEventFilter(this);
    }
    m_watched = frame;
    if (frame != nullptr)
    {
        frame->installEventFilter(this);
    }
}

} // namespace an::ui::observatory
